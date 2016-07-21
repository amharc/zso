#include "alloc.h"
#include "common.h"
#include "raise.h"
#include "error.h"
#include "notes.h"
#include "pass_control.h"
#include "syscalls.h"

#include <assert.h>
#include <asm/ldt.h>
#include <elf.h>
#include <fcntl.h>
#include <linux/unistd.h>
#include <linux/limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/procfs.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/user.h>
#include <unistd.h>

static Elf32_Ehdr get_elf_header(FILE *file) {
    Elf32_Ehdr eh;
    read_all(fileno(file), (void*)&eh, sizeof(eh), 0);
    EXPECT(!memcmp(eh.e_ident, ELFMAG, SELFMAG));
    EXPECT(eh.e_ident[EI_CLASS] == ELFCLASS32);
    EXPECT(eh.e_ident[EI_DATA] == ELFDATA2LSB);
    EXPECT(eh.e_ident[EI_VERSION] == EV_CURRENT);
    EXPECT(eh.e_type == ET_CORE);
    EXPECT(eh.e_machine == EM_386);
    EXPECT(eh.e_version == EV_CURRENT);
    EXPECT(eh.e_ehsize == sizeof(Elf32_Ehdr));
    EXPECT(eh.e_phnum != 0);
    EXPECT(eh.e_phoff != 0);
    EXPECT(eh.e_phentsize == sizeof(Elf32_Phdr));
    return eh;
}

static Elf32_Phdr* get_program_headers(FILE *file, const Elf32_Ehdr *eh) {
    Elf32_Phdr *phs;
    EXPECT(phs = alloc(sizeof(Elf32_Phdr) * eh->e_phnum));
    read_all(fileno(file), (void*)phs, sizeof(Elf32_Phdr) * eh->e_phnum, eh->e_phoff);
    return phs;
}

static void* get_note_section(FILE *file, const Elf32_Phdr *ph) {
    void *data;
    EXPECT(NULL != (data = malloc(ph->p_filesz)));
    read_all(fileno(file), data, ph->p_filesz, ph->p_offset);
    return data;
}

static void free_note_section(void *data) {
    free(data);
}

/** Creates anonymous private mappings for all PT_LOADs */
static void mk_anon_maps(const Elf32_Ehdr *eh, const Elf32_Phdr *ph) {
    for(int i = 0; i < eh->e_phnum; ++i) {
        if(ph[i].p_type != PT_LOAD)
            continue;
        if(ph[i].p_memsz > 0)
            EXPECT((void*)ph[i].p_vaddr == raw_mmap2((void*)ph[i].p_vaddr, ph[i].p_memsz,
                        PROT_NONE, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    }
}

#define MAX_FILE_SIZE 4 * 1024 * 1024

/** Restore the file mappings from the parsed notes */
static void mk_file_maps(struct parsed_notes *parsed, int corefd) {
    int fd;

    static char buffer[MAX_FILE_SIZE + 2];

    for(unsigned j = 0; j < parsed->mapped_file_count; ++j) {
        EXPECT(parsed->mapped_files[j].path.size <= MAX_FILE_SIZE);
        read_all(corefd, buffer, parsed->mapped_files[j].path.size, parsed->mapped_files[j].path.offset);
        buffer[parsed->mapped_files[j].path.size] = '\0';

        EXPECT((fd = raw_open(buffer, O_RDONLY)) > 0);
        EXPECT((void*)parsed->mapped_files[j].start == raw_mmap2((void*) parsed->mapped_files[j].start,
                    parsed->mapped_files[j].end - parsed->mapped_files[j].start, PROT_NONE,
                    MAP_FIXED | MAP_PRIVATE, fd, parsed->mapped_files[j].file_offset));
        EXPECT(0 == raw_close(fd));
    }
}

/** Map pages dumped to PT_LOAD */
static void mk_core_maps(const Elf32_Ehdr *eh, const Elf32_Phdr *ph, struct parsed_notes *parsed, int fd) {
    for(int i = 0; i < eh->e_phnum; ++i) {
        if(ph[i].p_type != PT_LOAD)
            continue;
        if(ph[i].p_filesz > 0)
            EXPECT((void*)ph[i].p_vaddr == raw_mmap2((void*)ph[i].p_vaddr, ph[i].p_filesz,
                        PROT_NONE, MAP_FIXED | MAP_PRIVATE, fd, ph[i].p_offset / parsed->page_size));
    }
}

/** All maps created by the previous functions have protection level PROT_NONE
 *  (as a sanity check to check if no further calls read or, worse, modify
 *  the memory of the restored process). The correct privileges are set here.
 */
static void mk_mprotects(const Elf32_Ehdr *eh, const Elf32_Phdr *ph) {
    for(int i = 0; i < eh->e_phnum; ++i) {
        if(ph[i].p_type != PT_LOAD || ph[i].p_flags == 0)
            continue;

        int prot = 0;
        if(ph[i].p_flags & PF_W) prot |= PROT_WRITE;
        if(ph[i].p_flags & PF_R) prot |= PROT_READ;
        if(ph[i].p_flags & PF_X) prot |= PROT_EXEC;

        EXPECT(0 == raw_mprotect((void*)ph[i].p_vaddr, ph[i].p_memsz, prot));
    }
}

/**
 *  Restore the thread areas read from the NT_386_TLS note.
 *
 *  Note: the %gs register is not changed here, so the descriptors are not reloaded.
 *  Instead, %gs is updated in pass_control.
 */
static void mk_tls(struct parsed_notes *parsed) {
    for(unsigned i = 0; i < parsed->ldt_count; ++i) {
       EXPECT(0 == raw_set_thread_area(&parsed->ldts[i]));
    }
}

_Noreturn void restore(FILE *file) {
    Elf32_Ehdr eh = get_elf_header(file);
    Elf32_Phdr *ph = get_program_headers(file, &eh);
    struct parsed_notes parsed;
    int fd = fileno(file);
    memset(&parsed, 0, sizeof(parsed));

    bool had_notes = false;

    for(int i = 0; i < eh.e_phnum; ++i) {
        EXPECT(ph[i].p_type == PT_LOAD || ph[i].p_type == PT_NOTE);
        if(ph[i].p_type == PT_NOTE) {
            EXPECT(!had_notes);
            had_notes = true;

            char *data = get_note_section(file, &ph[i]);
            iter_notes(ph[i].p_offset, data, data + ph[i].p_filesz, &handle_note, &parsed);
            free_note_section(data);
        }
    }

    EXPECT(had_notes);

    /* No calls to glibc or vdso pass this point */

    mk_anon_maps(&eh, ph);
    mk_file_maps(&parsed, fd);
    mk_core_maps(&eh, ph, &parsed, fd);
    mk_tls(&parsed);
    mk_mprotects(&eh, ph);

    raw_close(fd);

    pass_control((struct user_regs_struct*)&parsed.prstatus->pr_reg);
}

_Noreturn void run(int argc, char **argv) {
    if(argc != 2) {
        fprintf(stderr, "Usage: %s COREDUMP", argv[0]);
        exit(EXIT_FAILURE);
    }

    FILE *f;
    CALL_NEQ(0, f = fopen(argv[1], "rb"));
    restore(f);
}
