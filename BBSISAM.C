/* BBSISAM.C
 *
 * BBS-PC ! 4.21
 *
 * Reconstructed and modernised source
 * derived from BBS-PC 4.20
 *
 * Fixed-record storage helpers with strengthened safety:
 * - record bounds checking
 * - safer open modes
 * - record-length verification from file header when available
 * - basic corruption detection
 *
 * Notes:
 * - This module preserves the legacy fixed-record on-disk layout model.
 * - Header handling is intentionally conservative:
 *     first 2 bytes may contain a stored record length
 *     if present and plausible, it is verified
 * - If the header does not contain a usable record length, the module
 *   falls back to structural validation using file size and HDRLEN.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bbsdata.h"
#include "bbsfunc.h"

#ifndef HDRLEN
#define HDRLEN 128L
#endif

#ifndef MAX_PATHNAME
#define MAX_PATHNAME 128
#endif

/* ------------------------------------------------------------ */
/* local helpers                                                */
/* ------------------------------------------------------------ */

static long isam_get_file_size(fp)
FILE *fp;
{
    long cur, end;

    if (!fp)
        return -1L;

    cur = ftell(fp);
    if (cur < 0L)
        cur = 0L;

    if (fseek(fp, 0L, SEEK_END) != 0)
        return -1L;

    end = ftell(fp);

    (void)fseek(fp, cur, SEEK_SET);
    return end;
}

static int isam_valid_reclen(reclen)
int reclen;
{
    return (reclen > 0 && reclen < 32768);
}

static long isam_data_bytes(fp)
FILE *fp;
{
    long size;

    size = isam_get_file_size(fp);
    if (size < 0L)
        return -1L;
    if (size < HDRLEN)
        return 0L;

    return size - HDRLEN;
}

static int isam_header_reclen(fp, out_reclen)
FILE *fp;
int *out_reclen;
{
    long cur;
    unsigned char hdr[4];
    int rl16;

    if (!fp || !out_reclen)
        return 0;

    cur = ftell(fp);
    if (cur < 0L)
        cur = 0L;

    if (fseek(fp, 0L, SEEK_SET) != 0)
        return 0;

    memset(hdr, 0, sizeof(hdr));
    if (fread(hdr, 1, 4, fp) < 2)
    {
        (void)fseek(fp, cur, SEEK_SET);
        return 0;
    }

    (void)fseek(fp, cur, SEEK_SET);

    rl16 = (int)hdr[0] | ((int)hdr[1] << 8);

    if (!isam_valid_reclen(rl16))
        return 0;

    *out_reclen = rl16;
    return 1;
}

static int isam_verify_layout(fp, reclen)
FILE *fp;
int reclen;
{
    long data_bytes;
    int hdr_reclen;

    if (!fp || !isam_valid_reclen(reclen))
        return 0;

    data_bytes = isam_data_bytes(fp);
    if (data_bytes < 0L)
        return 0;

    if ((data_bytes % (long)reclen) != 0L)
        return 0;

    if (isam_header_reclen(fp, &hdr_reclen))
    {
        if (hdr_reclen != reclen)
            return 0;
    }

    return 1;
}

static int isam_verify_recno(fp, recno, reclen)
FILE *fp;
long recno;
int reclen;
{
    long nrec;

    if (!fp || !isam_valid_reclen(reclen))
        return 0;
    if (recno < 0L)
        return 0;
    if (!isam_verify_layout(fp, reclen))
        return 0;

    nrec = isam_record_count(fp, reclen);
    if (nrec < 0L)
        return 0;

    return recno < nrec;
}

static int isam_safe_open_existing(path, mode)
char *path;
char *mode;
{
    FILE *fp;

    fp = fopen(path, mode);
    if (!fp)
        return 0;

    fclose(fp);
    return 1;
}

static FILE *isam_open_with_fallback(path, mode)
char *path;
char *mode;
{
    FILE *fp;

    fp = fopen(path, mode);
    if (fp)
        return fp;

    if (!strcmp(mode, "r+b"))
    {
        fp = fopen(path, "rb");
        if (fp)
            return fp;
    }

    return (FILE *)0;
}

/* ------------------------------------------------------------ */
/* public fixed-record helpers                                  */
/* ------------------------------------------------------------ */

long isam_file_size(fp)
FILE *fp;
{
    return isam_get_file_size(fp);
}

long isam_record_count(fp, reclen)
FILE *fp;
int reclen;
{
    long bytes;

    if (!fp || !isam_valid_reclen(reclen))
        return -1L;

    bytes = isam_data_bytes(fp);
    if (bytes < 0L)
        return -1L;

    if ((bytes % (long)reclen) != 0L)
        return -1L;

    return bytes / (long)reclen;
}

long isam_record_offset(recno, reclen)
long recno;
int reclen;
{
    if (recno < 0L || !isam_valid_reclen(reclen))
        return -1L;

    return HDRLEN + (recno * (long)reclen);
}

int isam_seek_record(fp, recno, reclen)
FILE *fp;
long recno;
int reclen;
{
    long off;

    if (!isam_verify_recno(fp, recno, reclen))
        return 0;

    off = isam_record_offset(recno, reclen);
    if (off < 0L)
        return 0;

    return (fseek(fp, off, SEEK_SET) == 0);
}

int isam_read_record(fp, recno, reclen, buf)
FILE *fp;
long recno;
int reclen;
void *buf;
{
    if (!buf)
        return 0;

    if (!isam_seek_record(fp, recno, reclen))
        return 0;

    return (fread(buf, (unsigned)reclen, 1, fp) == 1);
}

int isam_write_record(fp, recno, reclen, buf)
FILE *fp;
long recno;
int reclen;
void *buf;
{
    if (!buf)
        return 0;

    if (!isam_seek_record(fp, recno, reclen))
        return 0;

    if (fwrite(buf, (unsigned)reclen, 1, fp) != 1)
        return 0;

    return (fflush(fp) == 0);
}

int isam_zero_record(fp, recno, reclen)
FILE *fp;
long recno;
int reclen;
{
    char *zbuf;
    int ok;

    if (!isam_valid_reclen(reclen))
        return 0;

    zbuf = (char *)calloc((unsigned)reclen, 1);
    if (!zbuf)
        return 0;

    ok = isam_write_record(fp, recno, reclen, zbuf);
    free(zbuf);
    return ok;
}

/* ------------------------------------------------------------ */
/* file open / close                                            */
/* ------------------------------------------------------------ */

FILE *isam_open(path, mode)
char *path;
char *mode;
{
    FILE *fp;

    if (!path || !*path || !mode || !*mode)
        return (FILE *)0;

    /* Prefer non-destructive opens. */
    if (!strcmp(mode, "w+b") || !strcmp(mode, "wb"))
        return (FILE *)0;

    fp = isam_open_with_fallback(path, mode);
    if (!fp)
        return (FILE *)0;

    /* Basic corruption/layout sanity on open is deferred until a
     * record length is known, but the header must at least exist.
     */
    if (isam_get_file_size(fp) >= 0L && isam_get_file_size(fp) < HDRLEN)
    {
        fclose(fp);
        return (FILE *)0;
    }

    return fp;
}

void isam_close(fp)
FILE *fp;
{
    if (fp)
        fclose(fp);
}

/* ------------------------------------------------------------ */
/* header helpers                                               */
/* ------------------------------------------------------------ */

int isam_read_header(fp, hdr, hdrlen)
FILE *fp;
void *hdr;
unsigned hdrlen;
{
    long cur;

    if (!fp || !hdr || hdrlen == 0U || hdrlen > (unsigned)HDRLEN)
        return 0;

    cur = ftell(fp);
    if (cur < 0L)
        cur = 0L;

    if (fseek(fp, 0L, SEEK_SET) != 0)
        return 0;

    if (fread(hdr, hdrlen, 1, fp) != 1)
    {
        (void)fseek(fp, cur, SEEK_SET);
        return 0;
    }

    (void)fseek(fp, cur, SEEK_SET);
    return 1;
}

int isam_write_header(fp, hdr, hdrlen)
FILE *fp;
void *hdr;
unsigned hdrlen;
{
    long cur;

    if (!fp || !hdr || hdrlen == 0U || hdrlen > (unsigned)HDRLEN)
        return 0;

    cur = ftell(fp);
    if (cur < 0L)
        cur = 0L;

    if (fseek(fp, 0L, SEEK_SET) != 0)
        return 0;

    if (fwrite(hdr, hdrlen, 1, fp) != 1)
    {
        (void)fseek(fp, cur, SEEK_SET);
        return 0;
    }

    (void)fflush(fp);
    (void)fseek(fp, cur, SEEK_SET);
    return 1;
}

int isam_blank_header(fp)
FILE *fp;
{
    unsigned char hdr[HDRLEN];

    memset(hdr, 0, sizeof(hdr));
    return isam_write_header(fp, hdr, sizeof(hdr));
}

/* ------------------------------------------------------------ */
/* append / blank record search                                 */
/* ------------------------------------------------------------ */

long isam_append_blank_record(fp, reclen)
FILE *fp;
int reclen;
{
    long recno;
    char *zbuf;

    if (!fp || !isam_valid_reclen(reclen))
        return -1L;

    if (!isam_verify_layout(fp, reclen))
        return -1L;

    recno = isam_record_count(fp, reclen);
    if (recno < 0L)
        return -1L;

    zbuf = (char *)calloc((unsigned)reclen, 1);
    if (!zbuf)
        return -1L;

    if (fseek(fp, 0L, SEEK_END) != 0)
    {
        free(zbuf);
        return -1L;
    }

    if (fwrite(zbuf, (unsigned)reclen, 1, fp) != 1)
    {
        free(zbuf);
        return -1L;
    }

    free(zbuf);
    (void)fflush(fp);
    return recno;
}

long isam_find_blank_record(fp, reclen, blank_test, ctx)
FILE *fp;
int reclen;
int (*blank_test)();
void *ctx;
{
    long i, n;
    char *buf;

    if (!fp || !blank_test || !isam_valid_reclen(reclen))
        return -1L;

    if (!isam_verify_layout(fp, reclen))
        return -1L;

    n = isam_record_count(fp, reclen);
    if (n < 0L)
        return -1L;

    buf = (char *)malloc((unsigned)reclen);
    if (!buf)
        return -1L;

    for (i = 0L; i < n; i++)
    {
        if (!isam_read_record(fp, i, reclen, buf))
            break;

        if ((*blank_test)(buf, ctx))
        {
            free(buf);
            return i;
        }
    }

    free(buf);
    return -1L;
}

/* ------------------------------------------------------------ */
/* file-level counts                                            */
/* ------------------------------------------------------------ */

long isam_count_users(void)
{
    return isam_record_count(g_usrfp, sizeof(USRDESC));
}

long isam_count_msgs(void)
{
    return isam_record_count(g_msgfp, sizeof(MSGHEAD));
}

long isam_count_msgtext(void)
{
    return isam_record_count(g_txtfp, sizeof(MSGTEXT));
}

long isam_count_files(void)
{
    return isam_record_count(g_udfp, sizeof(UDHEAD));
}

long isam_count_callers(void)
{
    return isam_record_count(g_logfp, sizeof(USRLOG));
}

/* ------------------------------------------------------------ */
/* typed record wrappers                                        */
/* ------------------------------------------------------------ */

int isam_read_user(recno, u)
long recno;
USRDESC *u;
{
    return isam_read_record(g_usrfp, recno, sizeof(USRDESC), u);
}

int isam_write_user(recno, u)
long recno;
USRDESC *u;
{
    return isam_write_record(g_usrfp, recno, sizeof(USRDESC), u);
}

int isam_read_msghead(recno, h)
long recno;
MSGHEAD *h;
{
    return isam_read_record(g_msgfp, recno, sizeof(MSGHEAD), h);
}

int isam_write_msghead(recno, h)
long recno;
MSGHEAD *h;
{
    return isam_write_record(g_msgfp, recno, sizeof(MSGHEAD), h);
}

int isam_read_msgtext(recno, t)
long recno;
MSGTEXT *t;
{
    return isam_read_record(g_txtfp, recno, sizeof(MSGTEXT), t);
}

int isam_write_msgtext(recno, t)
long recno;
MSGTEXT *t;
{
    return isam_write_record(g_txtfp, recno, sizeof(MSGTEXT), t);
}

int isam_read_udhead(recno, u)
long recno;
UDHEAD *u;
{
    return isam_read_record(g_udfp, recno, sizeof(UDHEAD), u);
}

int isam_write_udhead(recno, u)
long recno;
UDHEAD *u;
{
    return isam_write_record(g_udfp, recno, sizeof(UDHEAD), u);
}

int isam_read_caller(recno, c)
long recno;
USRLOG *c;
{
    return isam_read_record(g_logfp, recno, sizeof(USRLOG), c);
}

int isam_write_caller(recno, c)
long recno;
USRLOG *c;
{
    return isam_write_record(g_logfp, recno, sizeof(USRLOG), c);
}

/* ------------------------------------------------------------ */
/* blank record tests                                           */
/* ------------------------------------------------------------ */

int isam_blank_user(buf, ctx)
void *buf;
void *ctx;
{
    USRDESC *u;

    (void)ctx;
    u = (USRDESC *)buf;
    return (u->name[0] == 0);
}

int isam_blank_msghead(buf, ctx)
void *buf;
void *ctx;
{
    MSGHEAD *h;

    (void)ctx;
    h = (MSGHEAD *)buf;
    return (h->number == 0L);
}

int isam_blank_msgtext(buf, ctx)
void *buf;
void *ctx;
{
    MSGTEXT *t;

    (void)ctx;
    t = (MSGTEXT *)buf;
    return (t->text[0] == 0);
}

int isam_blank_udhead(buf, ctx)
void *buf;
void *ctx;
{
    UDHEAD *u;

    (void)ctx;
    u = (UDHEAD *)buf;
    return (u->cat_name[0] == 0);
}

int isam_blank_caller(buf, ctx)
void *buf;
void *ctx;
{
    USRLOG *c;

    (void)ctx;
    c = (USRLOG *)buf;
    return (c->name[0] == 0);
}

/* ------------------------------------------------------------ */
/* typed blank record searches                                  */
/* ------------------------------------------------------------ */

long isam_find_blank_user(void)
{
    return isam_find_blank_record(g_usrfp, sizeof(USRDESC),
        isam_blank_user, (void *)0);
}

long isam_find_blank_msghead(void)
{
    return isam_find_blank_record(g_msgfp, sizeof(MSGHEAD),
        isam_blank_msghead, (void *)0);
}

long isam_find_blank_msgtext(void)
{
    return isam_find_blank_record(g_txtfp, sizeof(MSGTEXT),
        isam_blank_msgtext, (void *)0);
}

long isam_find_blank_udhead(void)
{
    return isam_find_blank_record(g_udfp, sizeof(UDHEAD),
        isam_blank_udhead, (void *)0);
}

long isam_find_blank_caller(void)
{
    return isam_find_blank_record(g_logfp, sizeof(USRLOG),
        isam_blank_caller, (void *)0);
}

/* ------------------------------------------------------------ */
/* typed searches                                               */
/* ------------------------------------------------------------ */

long isam_find_user_by_name(name, out)
char *name;
USRDESC *out;
{
    long i, n;
    USRDESC u;

    n = isam_count_users();
    if (n < 0L)
        return -1L;

    for (i = 0L; i < n; i++)
    {
        if (!isam_read_user(i, &u))
            break;
        if (u.name[0] && data_user_match(u.name, name))
        {
            if (out)
                *out = u;
            return i;
        }
    }

    return -1L;
}

long isam_find_msg_by_number(msgno, out)
long msgno;
MSGHEAD *out;
{
    long i, n;
    MSGHEAD h;

    n = isam_count_msgs();
    if (n < 0L)
        return -1L;

    for (i = 0L; i < n; i++)
    {
        if (!isam_read_msghead(i, &h))
            break;
        if (h.number == msgno)
        {
            if (out)
                *out = h;
            return i;
        }
    }

    return -1L;
}

long isam_find_file_by_catalog(name, out)
char *name;
UDHEAD *out;
{
    long i, n;
    UDHEAD u;

    n = isam_count_files();
    if (n < 0L)
        return -1L;

    for (i = 0L; i < n; i++)
    {
        if (!isam_read_udhead(i, &u))
            break;
        if (u.cat_name[0] && data_cat_match(u.cat_name, name))
        {
            if (out)
                *out = u;
            return i;
        }
    }

    return -1L;
}

/* ------------------------------------------------------------ */
/* maintenance / mutation                                       */
/* ------------------------------------------------------------ */

int isam_rebuild_keys(path_dat, path_key)
char *path_dat;
char *path_key;
{
    (void)path_dat;
    (void)path_key;

    /* Placeholder: legacy key rebuild logic still deferred. */
    return 1;
}

int isam_delete_record(fp, recno, reclen)
FILE *fp;
long recno;
int reclen;
{
    return isam_zero_record(fp, recno, reclen);
}

int isam_insert_record(fp, recno, reclen, buf)
FILE *fp;
long recno;
int reclen;
void *buf;
{
    long nrec;
    long i;
    char *tmp;

    if (!fp || !buf || !isam_valid_reclen(reclen))
        return 0;

    if (!isam_verify_layout(fp, reclen))
        return 0;

    nrec = isam_record_count(fp, reclen);
    if (nrec < 0L)
        return 0;

    if (recno < 0L || recno > nrec)
        return 0;

    if (recno == nrec)
    {
        long newrec;

        newrec = isam_append_blank_record(fp, reclen);
        if (newrec != nrec)
            return 0;

        return isam_write_record(fp, recno, reclen, buf);
    }

    tmp = (char *)malloc((unsigned)reclen);
    if (!tmp)
        return 0;

    if (isam_append_blank_record(fp, reclen) < 0L)
    {
        free(tmp);
        return 0;
    }

    for (i = nrec; i > recno; i--)
    {
        if (!isam_read_record(fp, i - 1L, reclen, tmp))
        {
            free(tmp);
            return 0;
        }
        if (!isam_write_record(fp, i, reclen, tmp))
        {
            free(tmp);
            return 0;
        }
    }

    free(tmp);
    return isam_write_record(fp, recno, reclen, buf);
}