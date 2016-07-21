#include "alloc.h"
#include "common.h"
#include "error.h"
#include "notes.h"

#include <string.h>
#include <sys/procfs.h>

static void handle_nt_prstatus(off_t offset, struct parsed_notes *parsed, const char *desc, size_t descsz);
static void handle_nt_file(off_t offset, struct parsed_notes *parsed, const char *desc, size_t descsz);
static void handle_nt_386_tls(off_t offset, struct parsed_notes *parsed, const char *desc, size_t descsz);

static const char NM_CORE[] = "CORE";
static const char NM_LINUX[] = "LINUX";

void iter_notes(off_t offset, const char *start, const char *end, note_handler handler, void *aux) {
    while(start != end) {
        const Elf32_Nhdr *header = (const Elf32_Nhdr*)start;
        size_t namesz = header->n_namesz;
        size_t descsz = header->n_descsz;
        int type = header->n_type;

        size_t desc_diff = 3 * 4 + round_up(namesz, 4);

        handler(offset + desc_diff, type, namesz, descsz, start + 3 * 4, start + desc_diff, aux);
        size_t diff = 3 * 4 + round_up(namesz, 4) + round_up(descsz, 4);
        start += diff;
        offset += diff;
    }
}

void handle_note(off_t offset, int type, size_t namesz, size_t descsz, const char *name, const char *desc, void *aux) {
    struct parsed_notes *parsed = aux;

    if(type == NT_PRSTATUS && namesz == sizeof(NM_CORE) && !strcmp(name, NM_CORE))
        handle_nt_prstatus(offset, parsed, desc, descsz);
    else if(type == NT_FILE && namesz == sizeof(NM_CORE) && !strcmp(name, NM_CORE))
        handle_nt_file(offset, parsed, desc, descsz);
    else if(type == NT_386_TLS && namesz == sizeof(NM_LINUX) && !strcmp(name, NM_LINUX))
        handle_nt_386_tls(offset, parsed, desc, descsz);
}

static void handle_nt_prstatus(off_t offset, struct parsed_notes *parsed, const char *desc, size_t descsz) {
    (void) offset;
    (void) descsz;

    EXPECT(!parsed->prstatus);
    parsed->prstatus = alloc(sizeof(struct elf_prstatus));
    *parsed->prstatus = *(struct elf_prstatus*)desc;
}

static void handle_nt_file(off_t offset, struct parsed_notes *parsed, const char *desc, size_t descsz) {
   EXPECT(!parsed->mapped_files);

    struct file {
        size_t start, end, file_offset;
    } __attribute__((packed));

    struct files {
        long count;
        long page_size;
        struct file files[];
    } __attribute__((packed));

    const struct files *files = (const struct files*)desc;
    EXPECT(descsz >= sizeof(struct files));
    EXPECT(parsed->mapped_files = alloc(files->count * sizeof(struct mapped_file)));
    parsed->mapped_file_count = files->count;
    parsed->page_size = files->page_size;
    const char *filename = desc + sizeof(struct files) + files->count * sizeof(struct file);
    for(int i = 0; i < files->count; ++i) {
        EXPECT(filename < desc + descsz);
        size_t len = strnlen(filename, desc + descsz - filename);
        parsed->mapped_files[i].path.offset = filename - desc + offset;
        parsed->mapped_files[i].path.size = len;
        parsed->mapped_files[i].start = files->files[i].start;
        parsed->mapped_files[i].end = files->files[i].end;
        parsed->mapped_files[i].file_offset = files->files[i].file_offset;
        filename += len + 1;
    }
}

static void handle_nt_386_tls(off_t offset, struct parsed_notes *parsed, const char *desc, size_t descsz) {
    (void) offset;

    EXPECT(!parsed->ldts);
    parsed->ldts = alloc(descsz); /* There are max. 3 TLS entries */
    memcpy(parsed->ldts, desc, descsz);
    parsed->ldt_count = descsz / sizeof(struct user_desc);
}

