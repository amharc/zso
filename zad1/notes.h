#pragma once

#include <elf.h>
#include <stddef.h>
#include <stdlib.h>

typedef void (*note_handler)(off_t offset, int type, size_t namesz, size_t descsz,
        const char *name, const char *desc, void *aux);

struct core_loc {
    off_t offset;
    size_t size;
};

struct mapped_file {
    struct core_loc path;
    size_t start, end, file_offset;
};

struct parsed_notes {
    struct elf_prstatus *prstatus;
    struct mapped_file *mapped_files;
    size_t mapped_file_count;
    struct user_desc *ldts;
    size_t ldt_count;
    size_t page_size;
};

/** Calls handler for each note in [start, end) */
void iter_notes(off_t offset, const char *start, const char *end, note_handler handler, void *aux);

/** A note_handler which fills the parsed_notes struct */
void handle_note(off_t offset, int type, size_t namesz, size_t descsz, const char *name, const char *desc, void /* struct parsed_notes */ *aux);
