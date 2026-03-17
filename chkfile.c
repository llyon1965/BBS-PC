/*
 * CHKFILE - Check file online status
 *
 * Manual-aligned reconstruction for BBS-PC! 4.20
 *
 * Copyright (c) 1985,86,87 Micro-Systems Software Inc.
 * Reconstruction Copyright (c) 2026 Lance Lyon
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <io.h>
#include <fcntl.h>
#include <dos.h>
#include <dir.h>
#include <sys\stat.h>
#include "bbs420.h"

#define MAX_PATHSTR   128
#define MAX_MATCHES  4096

typedef struct {
    char catalog[FNAME_LEN + 1];
    char disk[FNAME_LEN + 1];
    char full[MAX_PATHSTR];
    BYTE section;
    BYTE dir;
    UWORD flags;
} UREC;

static CFGINFO cfg;
static int delete_mode = 0;
static int single_mode = 0;
static char search_arg[MAX_PATHSTR] = "";
static UREC recs[MAX_MATCHES];
static int rec_count = 0;

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

int indexed_record_count(fd, reclen) int fd; int reclen;
{
    long sz = filelength(fd);
    if (sz < FILEHDR_LEN) return 0L;
    return (int)((sz - FILEHDR_LEN) / (long)reclen);
}

long indexed_offset(recno, reclen) long recno; int reclen;
{
    return FILEHDR_LEN + recno * (long)reclen;
}

int yesno(prompt) char *prompt;
{
    int ch;
    printf("%s", prompt);
    ch = getchar();
    while (ch == '\r' || ch == '\n')
        ch = getchar();
    while (getchar() != '\n' && !feof(stdin))
        ;
    return (toupper(ch) == 'Y');
}

/* ------------------------------------------------------------ */

void load_cfg()
{
    int fd;

    fd = open("CFGINFO.DAT", O_RDONLY | O_BINARY);
    if (fd < 0)
        fatal("Can't open CFGINFO.DAT");

    if (read(fd, &cfg, sizeof(cfg)) != sizeof(cfg))
    {
        close(fd);
        fatal("Can't read from CFGINFO.DAT");
    }

    close(fd);
}

void build_ud_path(out, sec)
char *out;
int sec;
{
    out[0] = 0;

    if (sec >= 0 && sec < NUM_SECT && cfg.ud_alt[sec][0])
    {
        strcpy(out, cfg.ud_alt[sec]);
        return;
    }

    strcpy(out, ".\\");
}

void join_path(out, base, name)
char *out, *base, *name;
{
    strcpy(out, base);
    if (out[0] && out[strlen(out) - 1] != '\\' && out[strlen(out) - 1] != '/')
        strcat(out, "\\");
    strcat(out, name);
}

/* ------------------------------------------------------------ */

void load_ud_catalog()
{
    int fd, nrec, i;
    UDHEAD u;
    char path[MAX_PATHSTR];

    fd = open("UDHEAD.DAT", O_RDONLY | O_BINARY);
    if (fd < 0)
        fatal("Can't open ISAM files");

    nrec = indexed_record_count(fd, sizeof(UDHEAD));

    for (i = 0; i < nrec && rec_count < MAX_MATCHES; i++)
    {
        lseek(fd, indexed_offset(i, sizeof(UDHEAD)), SEEK_SET);
        if (read(fd, &u, sizeof(u)) != sizeof(u))
            continue;

        if (!(u.flags & UDH_VALID))
            continue;

        memset(&recs[rec_count], 0, sizeof(UREC));

        strncpy(recs[rec_count].catalog, u.cat_name, CAT_LEN);
        recs[rec_count].catalog[CAT_LEN] = 0;

        strncpy(recs[rec_count].disk, u.disk_name, FNAME_LEN);
        recs[rec_count].disk[FNAME_LEN] = 0;

        recs[rec_count].section = u.section;
        recs[rec_count].dir = u.dir;
        recs[rec_count].flags = u.flags;

        build_ud_path(path, u.section);
        join_path(recs[rec_count].full, path, recs[rec_count].disk);

        rec_count++;
    }

    close(fd);
}

int cmp_catalog(a, b)
const void *a, *b;
{
    return stricmp(((UREC *)a)->catalog, ((UREC *)b)->catalog);
}

void sort_catalog()
{
    qsort(recs, rec_count, sizeof(UREC), cmp_catalog);
}

/* ------------------------------------------------------------ */

int file_exists_disk(name) char *name;
{
    struct stat st;
    return (stat(name, &st) == 0);
}

int find_catalog_by_catalog(name) char *name;
{
    int i;
    for (i = 0; i < rec_count; i++)
        if (!stricmp(recs[i].catalog, name))
            return i;
    return -1;
}

int find_catalog_by_disk(name) char *name;
{
    int i;
    for (i = 0; i < rec_count; i++)
        if (!stricmp(recs[i].disk, name))
            return i;
    return -1;
}

/* ------------------------------------------------------------ */
/* catalog -> disk                                              */
/* ------------------------------------------------------------ */

void delete_catalog_entry(idx) int idx;
{
    /*
     * True ISAM delete not yet fully reconstructed.
     * Placeholder for now.
     */
    (void)idx;
    puts("Can't delete ISAM record");
}

void run_catalog_to_disk()
{
    int i;

    sort_catalog();

    for (i = 0; i < rec_count; i++)
    {
        if (single_mode && stricmp(recs[i].catalog, search_arg))
            continue;

        if (file_exists_disk(recs[i].full))
            printf("%-15s -> %s\n", recs[i].catalog, recs[i].full);
        else
        {
            if (delete_mode)
            {
                printf("%-15s -> %-15s * Not Online * - Killed\n",
                    recs[i].catalog, recs[i].disk);
                delete_catalog_entry(i);
            }
            else
            {
                printf("%-15s -> %-15s * Not Online *\n",
                    recs[i].catalog, recs[i].disk);
            }
        }
    }
}

/* ------------------------------------------------------------ */
/* disk -> catalog                                              */
/* ------------------------------------------------------------ */

void maybe_delete_disk_file(full) char *full;
{
    char prompt[MAX_PATHSTR + 32];

    sprintf(prompt, "%s Del (Y/N)? ", full);
    if (yesno(prompt))
    {
        if (unlink(full) != 0)
            puts("Can't delete file");
    }
}

void run_disk_to_catalog(mask)
char *mask;
{
    struct find_t ff;
    int done;
    char base[MAX_PATHSTR];
    char full[MAX_PATHSTR];
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

        if (find_catalog_by_disk(ff.name) < 0)
        {
            if (delete_mode)
                maybe_delete_disk_file(full);
            else
                printf("%-20s * Not in Catalog *\n", ff.name);
        }

        done = _dos_findnext(&ff);
    }
}

/* ------------------------------------------------------------ */

int has_wild(s) char *s;
{
    return (strchr(s, '*') != NULL || strchr(s, '?') != NULL);
}

int looks_like_path_search(s) char *s;
{
    if (has_wild(s))
        return 1;
    if (strchr(s, '\\') || strchr(s, '/'))
        return 1;
    if (strlen(s) == 2 && s[1] == ':')
        return 1;
    return 0;
}

void parse_args(argc, argv)
int argc; char *argv[];
{
    int i;

    for (i = 1; i < argc; i++)
    {
        if (!stricmp(argv[i], "-d") || !stricmp(argv[i], "/d"))
            delete_mode = 1;
        else if (!stricmp(argv[i], "-s") || !stricmp(argv[i], "/s"))
            single_mode = 1;
        else
            strcpy(search_arg, argv[i]);
    }
}

/* ------------------------------------------------------------ */

int main(argc, argv)
int argc; char *argv[];
{
    puts("CHKFILE - Check file online status - 4.20");
    puts("Copyright (c) 1985,86,87 Micro-Systems Software Inc.");
    puts("");

    parse_args(argc, argv);

    load_cfg();
    load_ud_catalog();

    if (!search_arg[0])
    {
        run_catalog_to_disk();
        return 0;
    }

    if (single_mode)
    {
        run_catalog_to_disk();
        return 0;
    }

    if (looks_like_path_search(search_arg))
    {
        run_disk_to_catalog(search_arg);
        return 0;
    }

    strcpy(search_arg, search_arg);
    single_mode = 1;
    run_catalog_to_disk();

    return 0;
}

