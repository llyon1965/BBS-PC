/*
 * BBSFILE - BBS-PC local file utility
 *
 * Manual-aligned reconstruction for BBS-PC! 4.20
 * Regenerated with hex-capable section parsing.
 *
 * Copyright (c) 1985,86,87 Micro-Systems Software Inc.
 * Reconstruction Copyright (c) 2026 Lance Lyon
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <io.h>
#include <dos.h>
#include <dir.h>
#include <sys\stat.h>
#include "bbs420.h"

#define OWNER_NAME "Sysop"
#define MAX_MASKS   64
#define MAX_PATH   128

typedef struct {
    char mask[MAX_PATH];
} MASKREC;

static MASKREC masks[MAX_MASKS];
static int mask_count = 0;

static int opt_section = 0;
static int opt_dir = 0;
static int opt_force_binary = 0;
static int opt_force_text = 0;
static int opt_no_desc = 0;

static int ud_fd = -1;
static long total_added = 0L;
static long total_catalog = 0L;

/* ------------------------------------------------------------ */

void fatal(msg) char *msg;
{
    puts(msg);
    exit(1);
}

void trim_crlf(s) char *s;
{
    char *p;
    p = strchr(s, '\r'); if (p) *p = 0;
    p = strchr(s, '\n'); if (p) *p = 0;
}

void write_header(fd) int fd;
{
    char hdr[FILEHDR_LEN];
    memset(hdr, 0, sizeof(hdr));
    write(fd, hdr, sizeof(hdr));
}

int open_ud_file(name) char *name;
{
    long sz;

    ud_fd = open(name, O_CREAT | O_BINARY | O_RDWR, 0666);
    if (ud_fd < 0)
        return 0;

    sz = filelength(ud_fd);
    if (sz == 0L)
        write_header(ud_fd);

    return 1;
}

void close_ud_file()
{
    if (ud_fd >= 0)
        close(ud_fd);
    ud_fd = -1;
}

long indexed_record_count(fd, reclen) int fd; int reclen;
{
    long sz = filelength(fd);
    if (sz < FILEHDR_LEN) return 0L;
    return (sz - FILEHDR_LEN) / (long)reclen;
}

long indexed_offset(recno, reclen) long recno; int reclen;
{
    return FILEHDR_LEN + recno * (long)reclen;
}

/* ------------------------------------------------------------ */

UWORD dos_date_serial(st) struct stat *st;
{
    (void)st;
    return 0;   /* exact serial still not fully recovered */
}

int is_text_extension(name) char *name;
{
    char *e;

    e = strrchr(name, '.');
    if (!e) return 0;

    if (!stricmp(e, ".ASC")) return 1;
    if (!stricmp(e, ".TXT")) return 1;
    if (!stricmp(e, ".DOC")) return 1;
    if (!stricmp(e, ".DIR")) return 1;
    if (!stricmp(e, ".PAS")) return 1;
    if (!stricmp(e, ".ASM")) return 1;
    if (!stricmp(e, ".MAC")) return 1;
    if (!stricmp(e, ".BAT")) return 1;
    if (!stricmp(e, ".PRN")) return 1;
    if (!stricmp(e, ".LST")) return 1;
    if (!stricmp(e, ".HEX")) return 1;
    if (!stricmp(e, ".ME"))  return 1;
    if (!stricmp(e, ".C"))   return 1;
    if (!stricmp(e, ".H"))   return 1;

    return 0;
}

int should_mark_binary(name) char *name;
{
    if (opt_force_binary) return 1;
    if (opt_force_text)   return 0;
    return !is_text_extension(name);
}

/* ------------------------------------------------------------ */

int file_already_in_catalog(name) char *name;
{
    int fd;
    long nrec, i;
    UDHEAD u;

    fd = open("UDHEAD.DAT", O_RDONLY | O_BINARY);
    if (fd < 0)
        return 0;

    nrec = indexed_record_count(fd, sizeof(UDHEAD));

    for (i = 0; i < nrec; i++)
    {
        lseek(fd, indexed_offset(i, sizeof(UDHEAD)), SEEK_SET);
        if (read(fd, &u, sizeof(u)) != sizeof(u))
            continue;

        if (!(u.flags & UDH_VALID))
            continue;

        if (!stricmp(u.disk_name, name))
        {
            close(fd);
            return 1;
        }
    }

    close(fd);
    return 0;
}

long current_catalog_count()
{
    int fd;
    long nrec, i, count;
    UDHEAD u;

    fd = open("UDHEAD.DAT", O_RDONLY | O_BINARY);
    if (fd < 0)
        return 0L;

    nrec = indexed_record_count(fd, sizeof(UDHEAD));
    count = 0L;

    for (i = 0; i < nrec; i++)
    {
        lseek(fd, indexed_offset(i, sizeof(UDHEAD)), SEEK_SET);
        if (read(fd, &u, sizeof(u)) != sizeof(u))
            continue;

        if (u.flags & UDH_VALID)
            count++;
    }

    close(fd);
    return count;
}

/* ------------------------------------------------------------ */

void fill_ud_record(u, fullpath, fname, desc)
UDHEAD *u;
char *fullpath, *fname, *desc;
{
    struct stat st;

    memset(u, 0, sizeof(UDHEAD));

    u->type = 0;
    u->flags = UDH_LOCAL | UDH_VALID | UDH_ONLINE;
    u->flags |= (((UWORD)opt_dir << UDH_DIRNUM_SHIFT) & UDH_DIRNUM_MASK);

    if (should_mark_binary(fname))
        u->flags |= UDH_BIN;

    strncpy(u->cat_name, fname, CAT_LEN - 1);
    u->cat_name[CAT_LEN - 1] = 0;

    strncpy(u->disk_name, fname, FNAME_LEN);
    u->disk_name[FNAME_LEN] = 0;

    strncpy(u->owner, OWNER_NAME, NAME_LEN);
    u->owner[NAME_LEN] = 0;

    strncpy(u->desc, desc, DESC_LEN);
    u->desc[DESC_LEN] = 0;

    u->dir = (BYTE)opt_dir;
    u->section = (BYTE)opt_section;
    u->accesses = 0;

    if (stat(fullpath, &st) == 0)
    {
        u->date = dos_date_serial(&st);
        u->length = (LONG)st.st_size;
    }
}

int append_ud(rec) UDHEAD *rec;
{
    if (lseek(ud_fd, 0L, SEEK_END) < 0L)
        return 0;

    if (write(ud_fd, rec, sizeof(UDHEAD)) != sizeof(UDHEAD))
        return 0;

    total_added++;
    return 1;
}

/* ------------------------------------------------------------ */

void prompt_desc_for_file(name, out) char *name, *out;
{
    if (opt_no_desc)
    {
        out[0] = 0;
        return;
    }

    puts(name);
    printf("Description: ");
    fgets(out, DESC_LEN + 2, stdin);
    trim_crlf(out);

    /* space + Return skips description */
    if (out[0] == ' ' && out[1] == 0)
        out[0] = 0;
}

/* ------------------------------------------------------------ */

int add_one_file(fullpath, fname) char *fullpath, *fname;
{
    UDHEAD u;
    char desc[DESC_LEN + 2];

    puts(fname);

    if (file_already_in_catalog(fname))
        return 0;

    prompt_desc_for_file(fname, desc);
    fill_ud_record(&u, fullpath, fname, desc);

    if (!append_ud(&u))
        fatal("Write error on UDHEAD output");

    return 1;
}

/* ------------------------------------------------------------ */

void process_mask(mask) char *mask;
{
    struct find_t ff;
    int done;
    char full[MAX_PATH];
    char base[MAX_PATH];
    char *p;

    strcpy(base, mask);
    p = strrchr(base, '\\');
    if (!p) p = strrchr(base, '/');

    if (p)
        *(p + 1) = 0;
    else
        strcpy(base, "");

    done = _dos_findfirst(mask, _A_NORMAL, &ff);

    while (!done)
    {
        strcpy(full, base);
        strcat(full, ff.name);

        add_one_file(full, ff.name);

        done = _dos_findnext(&ff);
    }
}

/* ------------------------------------------------------------ */

int is_switch(s) char *s;
{
    return (s[0] == '-' || s[0] == '/');
}

int parse_section_value(s) char *s;
{
    if (s[0] >= '0' && s[0] <= '9' && s[1] == 0)
        return s[0] - '0';

    if (s[0] >= 'A' && s[0] <= 'F' && s[1] == 0)
        return 10 + (s[0] - 'A');

    if (s[0] >= 'a' && s[0] <= 'f' && s[1] == 0)
        return 10 + (s[0] - 'a');

    return atoi(s);
}

void parse_switch(s) char *s;
{
    if ((s[0] == '-' || s[0] == '/') &&
        (s[1] == 's' || s[1] == 'S') &&
        s[2] == ':')
    {
        opt_section = parse_section_value(s + 3);
        return;
    }

    if ((s[0] == '-' || s[0] == '/') &&
        (s[1] == 'd' || s[1] == 'D') &&
        s[2] == ':')
    {
        opt_dir = atoi(s + 3);
        return;
    }

    if (!stricmp(s, "-b") || !stricmp(s, "/b"))
    {
        opt_force_binary = 1;
        return;
    }

    if (!stricmp(s, "-t") || !stricmp(s, "/t"))
    {
        opt_force_text = 1;
        return;
    }

    if (!stricmp(s, "-n") || !stricmp(s, "/n"))
    {
        opt_no_desc = 1;
        return;
    }

    printf("Unknown switch: %s\n", s);
    exit(1);
}

void parse_args(argc, argv) int argc; char *argv[];
{
    int i;

    for (i = 1; i < argc; i++)
    {
        if (is_switch(argv[i]))
            parse_switch(argv[i]);
        else
        {
            if (mask_count >= MAX_MASKS)
                fatal("Too many wildmasks");
            strncpy(masks[mask_count].mask, argv[i], MAX_PATH - 1);
            masks[mask_count].mask[MAX_PATH - 1] = 0;
            mask_count++;
        }
    }

    if (opt_force_binary && opt_force_text)
        fatal("Only one of -b or -t may be used");

    if (opt_section < 0 || opt_section >= NUM_SECT)
        fatal("Invalid section number");

    if (opt_dir < 0 || opt_dir > 255)
        fatal("Invalid directory number");

    if (mask_count == 0)
        strcpy(masks[mask_count++].mask, "*.*");
}

/* ------------------------------------------------------------ */

int main(argc, argv)
int argc;
char *argv[];
{
    int i;

    puts("BBSFILE - BBS-PC Local file utility - 4.20");
    puts("Copyright (c) 1985,86,87 Micro-Systems Software Inc.");
    puts("");

    parse_args(argc, argv);

    total_catalog = current_catalog_count();

    if (!open_ud_file("UDHEAD.NEW"))
        fatal("Can't open UDHEAD.NEW");

    for (i = 0; i < mask_count; i++)
        process_mask(masks[i].mask);

    printf("Total files in file catalog: %ld\n", total_catalog + total_added);

    close_ud_file();
    return 0;
}
