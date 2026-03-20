/* BBSFILE.C
 *
 * BBS-PC ! 4.21
 *
 * Reconstructed and modernised source
 * derived from BBS-PC 4.20
 *
 * File subsystem
 *
 * Updated:
 * - upload success writes catalogue entries reliably
 * - pseudo/catalogue filenames are normalised consistently
 * - DOS-era filename safety checks added
 * - F5 session override honoured through g_sess.ratio_off
 * - download ratio enforcement routed through file_ratio_allows_download()
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "bbsdata.h"
#include "bbsfunc.h"

#ifndef MAX_FILE_DESC
#define MAX_FILE_DESC 128
#endif

#ifndef DOS_BASENAME_LEN
#define DOS_BASENAME_LEN 8
#endif

#ifndef DOS_EXT_LEN
#define DOS_EXT_LEN 3
#endif

/* ------------------------------------------------------------ */
/* local helpers                                                */
/* ------------------------------------------------------------ */

static int file_is_blank(u)
UDHEAD *u;
{
    if (!u)
        return 1;

    return (u->cat_name[0] == 0);
}

static long file_count(void)
{
    long n;

    n = data_file_count();
    if (n < 0L)
        return 0L;

    return n;
}

static int file_load_by_recno(recno, u)
long recno;
UDHEAD *u;
{
    if (recno < 0L || recno >= file_count())
        return 0;

    if (!data_read_udhead(recno, u))
        return 0;

    if (file_is_blank(u))
        return 0;

    return 1;
}

static void file_make_path(out, u)
char *out;
UDHEAD *u;
{
    data_make_updn_path(out, u->section, u->cat_name);
}

static int file_exists_for_record(u)
UDHEAD *u;
{
    char path[MAX_PATHNAME];

    file_make_path(path, u);
    return data_disk_file_exists(path);
}

static void file_show_one(u)
UDHEAD *u;
{
    char path[MAX_PATHNAME];
    long len;

    file_make_path(path, u);
    len = data_disk_file_length(path);
    if (len < 0L)
        len = 0L;

    printf("%-12s  Sec:%2u  %8ld bytes\n",
           u->cat_name,
           (unsigned)u->section,
           len);
}

static int file_ratio_allows_download(void)
{
    if (g_sess.ratio_off)
        return 1;

    if (g_sess.user.dnldratio <= 0)
        return 1;

    if ((g_sess.user.uploads * g_sess.user.dnldratio) <
        (g_sess.user.downloads + 1))
        return 0;

    return 1;
}

static void file_increment_download_count(void)
{
    g_sess.user.downloads++;
    user_save_current();
}

static void file_increment_upload_count(void)
{
    g_sess.user.uploads++;
    g_sess.uploads++;
    user_save_current();
}

static void file_strupper(dst, src, maxlen)
char *dst;
char *src;
int maxlen;
{
    int i;

    if (maxlen <= 0)
        return;

    for (i = 0; i < (maxlen - 1) && src[i]; i++)
        dst[i] = (char)toupper((unsigned char)src[i]);

    dst[i] = 0;
}

static void file_trim(s)
char *s;
{
    int n;

    if (!s)
        return;

    while (*s == ' ' || *s == '\t')
        memmove(s, s + 1, strlen(s));

    n = strlen(s);
    while (n > 0 &&
           (s[n - 1] == ' ' || s[n - 1] == '\t' ||
            s[n - 1] == '\r' || s[n - 1] == '\n'))
    {
        s[n - 1] = 0;
        n--;
    }
}

static int file_is_valid_dos_char(ch)
int ch;
{
    ch = toupper(ch);

    if (isalnum(ch))
        return 1;

    switch (ch)
    {
        case '$':
        case '%':
        case '\'':
        case '-':
        case '_':
        case '@':
        case '~':
        case '`':
        case '!':
        case '(':
        case ')':
        case '{':
        case '}':
        case '^':
        case '#':
        case '&':
            return 1;
    }

    return 0;
}

static int file_normalize_pseudo_name(in, out, outlen)
char *in;
char *out;
int outlen;
{
    char base[DOS_BASENAME_LEN + 1];
    char ext[DOS_EXT_LEN + 1];
    char tmp[CAT_LEN + 8];
    char *dot;
    int i, bi, ei;

    if (!in || !out || outlen <= 0)
        return 0;

    strncpy(tmp, in, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = 0;
    file_trim(tmp);

    if (!tmp[0])
        return 0;

    if (strchr(tmp, '\\') || strchr(tmp, '/') || strchr(tmp, ':'))
        return 0;

    if (!strcmp(tmp, ".") || !strcmp(tmp, ".."))
        return 0;

    dot = strrchr(tmp, '.');

    memset(base, 0, sizeof(base));
    memset(ext, 0, sizeof(ext));

    bi = 0;
    ei = 0;

    for (i = 0; tmp[i]; i++)
    {
        int ch = (unsigned char)tmp[i];

        if (&tmp[i] == dot)
            break;

        if (ch == ' ')
            ch = '_';

        if (!file_is_valid_dos_char(ch))
            return 0;

        if (bi < DOS_BASENAME_LEN)
            base[bi++] = (char)toupper(ch);
    }
    base[bi] = 0;

    if (dot)
    {
        for (i = 1; dot[i]; i++)
        {
            int ch = (unsigned char)dot[i];

            if (ch == ' ')
                ch = '_';

            if (!file_is_valid_dos_char(ch))
                return 0;

            if (ei < DOS_EXT_LEN)
                ext[ei++] = (char)toupper(ch);
        }
        ext[ei] = 0;
    }

    if (!base[0])
        return 0;

    if (ext[0])
        sprintf(out, "%s.%s", base, ext);
    else
        strncpy(out, base, outlen - 1);

    out[outlen - 1] = 0;
    return 1;
}

static int file_name_matches_pattern(name, pat)
char *name;
char *pat;
{
    char uname[CAT_LEN + 8];
    char upat[CAT_LEN + 8];

    file_strupper(uname, name, sizeof(uname));
    file_strupper(upat, pat, sizeof(upat));

    return strstr(uname, upat) != (char *)0;
}

static int file_find_by_name(name, out, recno_out)
char *name;
UDHEAD *out;
long *recno_out;
{
    long recno;
    UDHEAD u;
    char norm[CAT_LEN + 8];

    if (!file_normalize_pseudo_name(name, norm, sizeof(norm)))
        return 0;

    recno = data_find_file_by_catalog(norm, &u);
    if (recno < 0L)
        return 0;

    if (out)
        *out = u;
    if (recno_out)
        *recno_out = recno;

    return 1;
}

static int file_add_catalogue_entry(name, section)
char *name;
int section;
{
    UDHEAD u;
    long recno;
    char norm[CAT_LEN + 8];

    if (!file_normalize_pseudo_name(name, norm, sizeof(norm)))
        return 0;

    if (file_find_by_name(norm, (UDHEAD *)0, (long *)0))
        return 1;

    memset(&u, 0, sizeof(u));
    strncpy(u.cat_name, norm, CAT_LEN);
    u.cat_name[CAT_LEN] = 0;
    u.section = (byte)section;

    recno = data_find_blank_udhead();
    if (recno < 0L)
        recno = data_file_count();

    return data_write_udhead(recno, &u);
}

static int file_remove_catalogue_entry(recno)
long recno;
{
    UDHEAD u;

    memset(&u, 0, sizeof(u));
    return data_write_udhead(recno, &u);
}

static int file_upload_to_catalogue(pseudo_name, section)
char *pseudo_name;
int section;
{
    char norm[CAT_LEN + 8];
    char path[MAX_PATHNAME];

    if (!file_normalize_pseudo_name(pseudo_name, norm, sizeof(norm)))
    {
        puts("Invalid filename");
        return 0;
    }

    if (file_find_by_name(norm, (UDHEAD *)0, (long *)0))
    {
        puts("File already exists in catalogue");
        return 0;
    }

    data_make_updn_path(path, section, norm);

    if (data_disk_file_exists(path))
    {
        puts("File already exists on disk");
        return 0;
    }

    if (!proto_upload(path, g_sess.user.protocol))
    {
        puts("Upload failed");
        return 0;
    }

    if (!file_add_catalogue_entry(norm, section))
    {
        remove(path);
        puts("Upload received but catalogue update failed");
        return 0;
    }

    file_increment_upload_count();
    puts("Upload complete");
    return 1;
}

/* ------------------------------------------------------------ */
/* public file actions                                          */
/* ------------------------------------------------------------ */

void do_list_files(void)
{
    long i, n;
    UDHEAD u;

    n = file_count();
    if (n <= 0L)
    {
        puts("No files in catalogue");
        return;
    }

    for (i = 0L; i < n; i++)
    {
        if (!file_load_by_recno(i, &u))
            continue;

        file_show_one(&u);
    }
}

void do_download_file(void)
{
    char name[CAT_LEN + 8];
    UDHEAD u;
    long recno;
    char path[MAX_PATHNAME];

    term_getline("Download file: ", name, sizeof(name));
    if (!name[0])
        return;

    if (!file_find_by_name(name, &u, &recno))
    {
        puts("File not found");
        return;
    }

    if (!file_exists_for_record(&u))
    {
        puts("File missing on disk");
        return;
    }

    if (!file_ratio_allows_download())
    {
        puts("Upload/download ratio exceeded");
        return;
    }

    file_make_path(path, &u);

    if (!proto_download(path, g_sess.user.protocol))
    {
        puts("Download failed");
        return;
    }

    file_increment_download_count();
    puts("Download complete");
}

void do_upload_file(void)
{
    char name[CAT_LEN + 8];

    term_getline("Upload filename: ", name, sizeof(name));
    if (!name[0])
        return;

    (void)file_upload_to_catalogue(name, 0);
}

void do_kill_file(void)
{
    char name[CAT_LEN + 8];
    UDHEAD u;
    long recno;
    char path[MAX_PATHNAME];

    if (!sysop_password_prompt())
        return;

    term_getline("Kill file: ", name, sizeof(name));
    if (!name[0])
        return;

    if (!file_find_by_name(name, &u, &recno))
    {
        puts("File not found");
        return;
    }

    file_make_path(path, &u);
    remove(path);
    (void)file_remove_catalogue_entry(recno);

    puts("File removed");
}

void do_browse_files(void)
{
    do_list_files();
}

void do_new_files_scan(void)
{
    do_list_files();
}

void do_search_catdesc(void)
{
    char pat[CAT_LEN + 8];
    long i, n;
    UDHEAD u;
    int found;

    term_getline("Search: ", pat, sizeof(pat));
    if (!pat[0])
        return;

    found = 0;
    n = file_count();

    for (i = 0L; i < n; i++)
    {
        if (!file_load_by_recno(i, &u))
            continue;

        if (file_name_matches_pattern(u.cat_name, pat))
        {
            file_show_one(&u);
            found = 1;
        }
    }

    if (!found)
        puts("No matching files found");
}

void do_read_file(void)
{
    char name[CAT_LEN + 8];
    UDHEAD u;
    long recno;
    char path[MAX_PATHNAME];

    term_getline("Read file: ", name, sizeof(name));
    if (!name[0])
        return;

    if (!file_find_by_name(name, &u, &recno))
    {
        puts("File not found");
        return;
    }

    file_make_path(path, &u);
    bbs_type_file(path, 1, 1);
}

void do_upload_direct(void)
{
    do_upload_file();
}

void do_download_direct(void)
{
    do_download_file();
}

void do_upload_local(void)
{
    do_upload_file();
}

void do_direct_file_kill(void)
{
    do_kill_file();
}

void do_print_catalog(void)
{
    do_list_files();
}

void do_new_files(void)
{
    do_new_files_scan();
}