/* BBSFILE.C
 *
 * BBS-PC 4.20 file/catalog runtime module
 *
 * Updated to wire transfers through:
 * - BBSPROTO.C
 * - BBSMODM.C
 *
 * Implements:
 * - print file catalog
 * - browse thru files
 * - upload a file
 * - upload a file from local
 * - download a file
 * - read a file
 * - kill a file
 * - search catalog descriptions
 * - display new files
 *
 * Notes:
 * - Catalog/database updates are now separated from actual transfer
 *   dispatch.
 * - Binary/text transfer choice is routed through proto_*.
 * - Upload/download are still first-pass, but now use the protocol
 *   layer instead of fake success messages.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "bbsdata.h"
#include "bbsfunc.h"

#define READBUF_SIZE 160

/* ------------------------------------------------------------ */
/* access helpers                                               */
/* ------------------------------------------------------------ */

static int can_read_file(u)
UDHEAD *u;
{
    int sec;

    sec = u->section;
    if (sec < 0 || sec >= NUM_SECT)
        return 0;

    return (g_sess.user.rd_acc & (1 << sec)) != 0;
}

static int can_download_file(u)
UDHEAD *u;
{
    int sec;

    sec = u->section;
    if (sec < 0 || sec >= NUM_SECT)
        return 0;

    return (g_sess.user.dn_acc & (1 << sec)) != 0;
}

static int can_upload_section(sec)
int sec;
{
    if (sec < 0 || sec >= NUM_SECT)
        return 0;

    return (g_sess.user.up_acc & (1 << sec)) != 0;
}

static int can_kill_file(u)
UDHEAD *u;
{
    if (g_sess.user.priv >= 100)
        return 1;

    if (data_user_match(u->owner, g_sess.user.name))
        return 1;

    return 0;
}

static int file_is_online(u)
UDHEAD *u;
{
    char full[MAX_PATHNAME];

    data_make_updn_path(full, u->section, u->disk_name);
    return data_disk_file_exists(full);
}

static void build_file_path(u, out)
UDHEAD *u;
char *out;
{
    data_make_updn_path(out, u->section, u->disk_name);
}

/* ------------------------------------------------------------ */
/* lookup                                                       */
/* ------------------------------------------------------------ */

static long find_file_by_catalog(name, u)
char *name;
UDHEAD *u;
{
    return data_find_file_by_catalog(name, u);
}

static long first_file_record(out)
UDHEAD *out;
{
    return data_first_file(out);
}

static long next_file_record(cur, out)
long cur;
UDHEAD *out;
{
    return data_next_file(cur, out);
}

/* ------------------------------------------------------------ */
/* display helpers                                              */
/* ------------------------------------------------------------ */

static void show_file_brief(u)
UDHEAD *u;
{
    char d[20];

    data_unpack_date(u->date, d);

    printf("%-15s  %-12s  %8ld  %c  %s\n",
        u->cat_name,
        u->disk_name,
        (long)u->length,
        'A' + u->section,
        d);
}

static void show_file_full(u)
UDHEAD *u;
{
    char d[20];
    char path[MAX_PATHNAME];

    data_unpack_date(u->date, d);
    build_file_path(u, path);

    puts("");
    printf("File: %s  %ld-%c\n",
        u->cat_name,
        (long)u->length,
        'A' + u->section);
    printf("Dir: %d  Sec: %c - %s\n",
        (int)u->dir,
        'A' + u->section,
        g_cfg.sec_name[u->section][0] ? g_cfg.sec_name[u->section] : "(unnamed)");
    printf("Disk: %s\n", u->disk_name);
    printf("Path: %s\n", path);
    printf("Owner: %s\n", u->owner);
    printf("Acc: %u\n", (unsigned)u->accesses);
    printf("Date: %s\n", d);
    printf("Desc: %s\n", u->desc);
    printf("Type: %s\n", u->bin ? "Binary" : "Text");

    if (!file_is_online(u))
        puts("File not currently online");
}

static void show_catalog_header(void)
{
    puts("");
    puts("Catalog:");
    puts("Catalog Name     Disk Name         Length  S  Date");
    puts("-----------------------------------------------------");
}

/* ------------------------------------------------------------ */
/* transfer helpers                                             */
/* ------------------------------------------------------------ */

static int file_transfer_download(u)
UDHEAD *u;
{
    char path[MAX_PATHNAME];
    int proto;
    int ok;

    if (!file_is_online(u))
    {
        puts("File not currently online");
        return 0;
    }

    build_file_path(u, path);

    proto = g_sess.user.protocol;
    if (!proto_can_send_file(proto, u->bin ? 1 : 0))
        proto = proto_recommended_for_file(u->disk_name, u->bin ? 1 : 0);

    printf("Protocol: %s\n", proto_name(proto));
    ok = proto_download(path, proto);

    return ok;
}

static int file_transfer_upload(path, is_binary)
char *path;
int is_binary;
{
    int proto;

    proto = g_sess.user.protocol;
    if (!proto_can_send_file(proto, is_binary))
        proto = proto_recommended_for_file(path, is_binary);

    printf("Protocol: %s\n", proto_name(proto));
    return proto_upload(path, proto);
}

static int file_guess_binary_from_name(name)
char *name;
{
    char *p;

    p = strrchr(name, '.');
    if (!p)
        return 1;

    if (!stricmp(p, ".ASC")) return 0;
    if (!stricmp(p, ".TXT")) return 0;
    if (!stricmp(p, ".DOC")) return 0;
    if (!stricmp(p, ".DIR")) return 0;
    if (!stricmp(p, ".PAS")) return 0;
    if (!stricmp(p, ".ASM")) return 0;
    if (!stricmp(p, ".MAC")) return 0;
    if (!stricmp(p, ".BAT")) return 0;
    if (!stricmp(p, ".PRN")) return 0;
    if (!stricmp(p, ".LST")) return 0;
    if (!stricmp(p, ".HEX")) return 0;
    if (!stricmp(p, ".ME"))  return 0;
    if (!stricmp(p, ".C"))   return 0;
    if (!stricmp(p, ".H"))   return 0;

    return 1;
}

/* ------------------------------------------------------------ */
/* printing and browsing                                        */
/* ------------------------------------------------------------ */

void do_print_catalog(void)
{
    long recno;
    UDHEAD u;

    if (!g_fp_udhead)
    {
        puts("Catalog file not open");
        bbs_pause();
        return;
    }

    show_catalog_header();

    recno = first_file_record(&u);
    while (recno >= 0L)
    {
        if (can_read_file(&u))
            show_file_brief(&u);

        recno = next_file_record(recno, &u);
    }

    bbs_pause();
}

void do_browse_files(void)
{
    long recno;
    UDHEAD u;
    char line[16];

    if (!g_fp_udhead)
    {
        puts("Catalog file not open");
        bbs_pause();
        return;
    }

    recno = first_file_record(&u);
    if (recno < 0L)
    {
        puts("No files in catalog");
        bbs_pause();
        return;
    }

    for (;;)
    {
        if (u.cat_name[0] && can_read_file(&u))
        {
            show_file_full(&u);
            puts("");
            puts("N: Next  R: Read  D: Download  K: Kill  Q: Quit");
            data_prompt_line("Command? ", line, sizeof(line));

            if (line[0] >= 'a' && line[0] <= 'z')
                line[0] -= 32;

            switch (line[0])
            {
                case 'R':
                    do_read_file();
                    break;

                case 'D':
                    do_download_file();
                    break;

                case 'K':
                    do_kill_file();
                    break;

                case 'Q':
                    return;

                default:
                    break;
            }
        }

        recno = next_file_record(recno, &u);
        if (recno < 0L)
            break;
    }

    bbs_pause();
}

/* ------------------------------------------------------------ */
/* read/download/kill                                           */
/* ------------------------------------------------------------ */

void do_read_file(void)
{
    char name[CAT_LEN + 4];
    long recno;
    UDHEAD u;
    char path[MAX_PATHNAME];
    FILE *fp;
    char line[READBUF_SIZE];

    data_prompt_line("Catalog filename: ", name, sizeof(name));
    if (!name[0])
        return;

    recno = find_file_by_catalog(name, &u);
    if (recno < 0L)
    {
        puts("File not found");
        return;
    }

    if (!can_read_file(&u))
    {
        puts("You cannot read this file");
        return;
    }

    if (u.bin)
    {
        puts("Binary files unreadable");
        return;
    }

    build_file_path(&u, path);
    fp = fopen(path, "rt");
    if (!fp)
    {
        puts("File not currently online");
        return;
    }

    show_file_full(&u);
    puts("");

    while (fgets(line, sizeof(line), fp))
        fputs(line, stdout);

    fclose(fp);
    bbs_pause();
}

void do_download_file(void)
{
    char name[CAT_LEN + 4];
    long recno;
    UDHEAD u;
    int ok;

    data_prompt_line("Catalog filename: ", name, sizeof(name));
    if (!name[0])
        return;

    recno = find_file_by_catalog(name, &u);
    if (recno < 0L)
    {
        puts("File not found");
        return;
    }

    if (!can_download_file(&u))
    {
        puts("You cannot download this file");
        return;
    }

    ok = file_transfer_download(&u);
    if (!ok)
        return;

    u.accesses++;
    data_write_udhead(recno, &u);

    g_sess.user.dnl_total++;
    user_save_current();
}

void do_kill_file(void)
{
    char name[CAT_LEN + 4];
    long recno;
    UDHEAD u;

    data_prompt_line("Catalog filename: ", name, sizeof(name));
    if (!name[0])
        return;

    recno = find_file_by_catalog(name, &u);
    if (recno < 0L)
    {
        puts("File not found");
        return;
    }

    if (!can_kill_file(&u))
    {
        puts("You cannot kill this file");
        return;
    }

    if (!data_yesno("Kill file (Y/N)? ", 0))
        return;

    memset(&u, 0, sizeof(u));

    if (!data_write_udhead(recno, &u))
    {
        puts("Can't delete file");
        return;
    }

    puts("Killed");
}

/* ------------------------------------------------------------ */
/* upload                                                       */
/* ------------------------------------------------------------ */

static int select_upload_section(void)
{
    char line[16];
    int sec;

    puts("");
    puts("Upload sections:");
    do_section_names();

    data_prompt_line("Section (A-P)? ", line, sizeof(line));
    if (!line[0])
        return -1;

    if (line[0] >= 'a' && line[0] <= 'z')
        line[0] -= 32;

    if (line[0] < 'A' || line[0] >= ('A' + NUM_SECT))
        return -1;

    sec = line[0] - 'A';

    if (!can_upload_section(sec))
    {
        puts("Illegal access");
        return -1;
    }

    return sec;
}

static int catalog_name_exists(name)
char *name;
{
    UDHEAD u;
    return find_file_by_catalog(name, &u) >= 0L;
}

static void fill_upload_record(u, sec, cat, disk, desc, local_flag, is_binary)
UDHEAD *u;
int sec;
char *cat, *disk, *desc;
int local_flag;
int is_binary;
{
    memset(u, 0, sizeof(*u));

    u->type = 0;
    u->section = (byte)sec;
    u->dir = (byte)sec;
    u->date = data_pack_date_now();
    u->accesses = 0;
    u->local = local_flag ? 1 : 0;
    u->bin = is_binary ? 1 : 0;

    strncpy(u->cat_name, cat, CAT_LEN - 1);
    u->cat_name[CAT_LEN - 1] = 0;

    strncpy(u->disk_name, disk, FNAME_LEN);
    u->disk_name[FNAME_LEN] = 0;

    strncpy(u->owner, g_sess.user.name, NAME_LEN);
    u->owner[NAME_LEN] = 0;

    strncpy(u->desc, desc, DESC_LEN);
    u->desc[DESC_LEN] = 0;
}

static int complete_upload_to_catalog(sec, cat, disk, desc, local_flag, source_path)
int sec;
char *cat;
char *disk;
char *desc;
int local_flag;
char *source_path;
{
    UDHEAD u;
    long slot, len;
    int is_binary;
    int ok;

    slot = data_find_blank_udhead();
    if (slot < 0L)
    {
        puts("Catalog full");
        return 0;
    }

    is_binary = file_guess_binary_from_name(disk);

    ok = file_transfer_upload(source_path, is_binary);
    if (!ok)
        return 0;

    fill_upload_record(&u, sec, cat, disk, desc, local_flag, is_binary);

    len = data_disk_file_length(source_path);
    if (len >= 0L)
        u.length = len;

    if (!data_write_udhead(slot, &u))
    {
        puts("Upload unsuccessful");
        return 0;
    }

    g_sess.user.upl_total++;
    user_save_current();

    puts("Catalog updated");
    return 1;
}

void do_upload_file(void)
{
    char cat[CAT_LEN + 2];
    char disk[FNAME_LEN + 2];
    char desc[DESC_LEN + 2];
    char path[MAX_PATHNAME];
    int sec;

    sec = select_upload_section();
    if (sec < 0)
        return;

    data_prompt_line("Catalog filename: ", cat, sizeof(cat));
    if (!cat[0])
        return;

    if (catalog_name_exists(cat))
    {
        puts("File already in catalog");
        return;
    }

    data_prompt_line("Disk filename: ", disk, sizeof(disk));
    if (!disk[0])
        return;

    data_prompt_line("Description (40 chars max): ", desc, sizeof(desc));

    data_make_updn_path(path, sec, disk);

    if (!complete_upload_to_catalog(sec, cat, disk, desc, 0, path))
        return;
}

void do_upload_local(void)
{
    char cat[CAT_LEN + 2];
    char disk[FNAME_LEN + 2];
    char desc[DESC_LEN + 2];
    char path[MAX_PATHNAME];
    int sec;

    sec = select_upload_section();
    if (sec < 0)
        return;

    data_prompt_line("Catalog filename: ", cat, sizeof(cat));
    if (!cat[0])
        return;

    if (catalog_name_exists(cat))
    {
        puts("File already in catalog");
        return;
    }

    data_prompt_line("Local filename: ", disk, sizeof(disk));
    if (!disk[0])
        return;

    data_prompt_line("Description (40 chars max): ", desc, sizeof(desc));

    data_make_updn_path(path, sec, disk);

    if (!complete_upload_to_catalog(sec, cat, disk, desc, 1, path))
        return;
}

/* ------------------------------------------------------------ */
/* search and new files                                         */
/* ------------------------------------------------------------ */

void do_search_catdesc(void)
{
    char text[DESC_LEN + 2];
    long recno;
    UDHEAD u;
    char needle[DESC_LEN + 2];
    char hay[DESC_LEN + 2];
    int j;

    data_prompt_line("Search string: ", text, sizeof(text));
    if (!text[0])
        return;

    strcpy(needle, text);
    for (j = 0; needle[j]; j++)
        if (needle[j] >= 'a' && needle[j] <= 'z')
            needle[j] -= 32;

    show_catalog_header();

    recno = first_file_record(&u);
    while (recno >= 0L)
    {
        if (can_read_file(&u))
        {
            strcpy(hay, u.desc);
            for (j = 0; hay[j]; j++)
                if (hay[j] >= 'a' && hay[j] <= 'z')
                    hay[j] -= 32;

            if (strstr(hay, needle))
                show_file_brief(&u);
        }

        recno = next_file_record(recno, &u);
    }

    bbs_pause();
}

void do_new_files(void)
{
    long recno;
    UDHEAD u;
    ushort today, cutoff;

    today = data_pack_date_now();
    cutoff = (today > 30) ? (today - 30) : 0;

    show_catalog_header();

    recno = first_file_record(&u);
    while (recno >= 0L)
    {
        if (can_read_file(&u) && u.date >= cutoff)
            show_file_brief(&u);

        recno = next_file_record(recno, &u);
    }

    bbs_pause();
}