/* BBSISAM.C
 *
 * BBS-PC ! 4.21
 *
 * Fixed-record ISAM-style access helpers.
 *
 * Caller-log note:
 *   USRLOG is an on-disk record and is read/written raw.
 *   This matches the expanded CALLER.DAT layout used for
 *   BBSINFO / recent-caller output.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bbsdata.h"
#include "bbsfunc.h"

/* ------------------------------------------------------------ */
/* local conversion helpers                                     */
/* ------------------------------------------------------------ */

static void userrec_to_usrdesc(dst, src)
USRDESC *dst;
USERREC *src;
{
    memset(dst, 0, sizeof(*dst));

    strncpy(dst->name, src->name, NAME_LEN);
    dst->name[NAME_LEN] = 0;

    strncpy(dst->pwd, src->pwd, PWD_LEN);
    dst->pwd[PWD_LEN] = 0;

    strncpy(dst->city, src->city, CITY_LEN);
    dst->city[CITY_LEN] = 0;

    dst->lastdate    = src->lastdate;
    dst->lasttime    = src->lasttime;
    dst->calls       = src->calls;
    dst->uploads     = src->uploads;
    dst->downloads   = src->downloads;
    dst->highmsgread = src->highmsgread;

    dst->rd_acc      = src->rd_acc;
    dst->wr_acc      = src->wr_acc;
    dst->up_acc      = src->up_acc;
    dst->dn_acc      = src->dn_acc;
    dst->sys_acc     = src->sys_acc;
    dst->sect_mask   = src->sect_mask;

    dst->time_limit  = src->time_limit;
    dst->menu_set    = src->menu_set;

    dst->protocol    = src->protocol;
    dst->expert      = src->expert;
    dst->seclevel    = src->seclevel;
    dst->dnldratio   = src->dnldratio;
}

static void usrdesc_to_userrec(dst, src)
USERREC *dst;
USRDESC *src;
{
    memset(dst, 0, sizeof(*dst));

    strncpy(dst->name, src->name, DISK_NAME_LEN);
    dst->name[DISK_NAME_LEN] = 0;

    strncpy(dst->pwd, src->pwd, DISK_PWD_LEN);
    dst->pwd[DISK_PWD_LEN] = 0;

    strncpy(dst->city, src->city, DISK_CITY_LEN);
    dst->city[DISK_CITY_LEN] = 0;

    dst->lastdate    = src->lastdate;
    dst->lasttime    = src->lasttime;
    dst->calls       = src->calls;
    dst->uploads     = src->uploads;
    dst->downloads   = src->downloads;
    dst->highmsgread = src->highmsgread;

    dst->rd_acc      = src->rd_acc;
    dst->wr_acc      = src->wr_acc;
    dst->up_acc      = src->up_acc;
    dst->dn_acc      = src->dn_acc;
    dst->sys_acc     = src->sys_acc;
    dst->sect_mask   = src->sect_mask;

    dst->time_limit  = src->time_limit;
    dst->menu_set    = src->menu_set;

    dst->protocol    = src->protocol;
    dst->expert      = src->expert;
    dst->seclevel    = src->seclevel;
    dst->dnldratio   = src->dnldratio;
}

static void msgrec_to_msghead(dst, src)
MSGHEAD *dst;
MSGREC *src;
{
    memset(dst, 0, sizeof(*dst));

    dst->number  = src->number;
    dst->replyto = src->replyto;
    memcpy(dst->replys, src->replys, sizeof(dst->replys));

    dst->msgptr  = src->msgptr;
    dst->date    = src->date;
    dst->time    = src->time;
    dst->section = src->section;

    strncpy(dst->from, src->from, NAME_LEN);
    dst->from[NAME_LEN] = 0;

    strncpy(dst->to, src->to, NAME_LEN);
    dst->to[NAME_LEN] = 0;

    strncpy(dst->subject, src->subject, SUBJ_LEN);
    dst->subject[SUBJ_LEN] = 0;
}

static void msghead_to_msgrec(dst, src)
MSGREC *dst;
MSGHEAD *src;
{
    memset(dst, 0, sizeof(*dst));

    dst->number  = src->number;
    dst->replyto = src->replyto;
    memcpy(dst->replys, src->replys, sizeof(dst->replys));

    dst->msgptr  = src->msgptr;
    dst->date    = src->date;
    dst->time    = src->time;
    dst->section = src->section;

    strncpy(dst->from, src->from, DISK_NAME_LEN);
    dst->from[DISK_NAME_LEN] = 0;

    strncpy(dst->to, src->to, DISK_NAME_LEN);
    dst->to[DISK_NAME_LEN] = 0;

    strncpy(dst->subject, src->subject, DISK_SUBJ_LEN);
    dst->subject[DISK_SUBJ_LEN] = 0;
}

static void udrec_to_udhead(dst, src)
UDHEAD *dst;
UDREC *src;
{
    memset(dst, 0, sizeof(*dst));
    strncpy(dst->cat_name, src->cat_name, CAT_LEN);
    dst->cat_name[CAT_LEN] = 0;
    dst->section = src->section;
}

static void udhead_to_udrec(dst, src)
UDREC *dst;
UDHEAD *src;
{
    memset(dst, 0, sizeof(*dst));
    strncpy(dst->cat_name, src->cat_name, DISK_CAT_LEN);
    dst->cat_name[DISK_CAT_LEN] = 0;
    dst->section = src->section;
}

static int isam_userrec_blank(r)
USERREC *r;
{
    return (!r || r->name[0] == 0);
}

static int isam_msgrec_blank(r)
MSGREC *r;
{
    return (!r || r->number == 0L);
}

static int isam_udrec_blank(r)
UDREC *r;
{
    return (!r || r->cat_name[0] == 0);
}

static int isam_usrlog_blank(r)
USRLOG *r;
{
    return (!r || r->name[0] == 0);
}

static int isam_msgtext_blank(r)
MSGTEXT *r;
{
    return (!r || r->text[0] == 0);
}

/* ------------------------------------------------------------ */
/* generic fixed-record helpers                                 */
/* ------------------------------------------------------------ */

long isam_file_size(fp)
FILE *fp;
{
    long cur;
    long len;

    if (!fp)
        return -1L;

    cur = ftell(fp);
    if (cur < 0L)
        cur = 0L;

    if (fseek(fp, 0L, SEEK_END) != 0)
        return -1L;

    len = ftell(fp);

    (void)fseek(fp, cur, SEEK_SET);
    return len;
}

long isam_record_count(fp, reclen)
FILE *fp;
int reclen;
{
    long len;

    if (!fp || reclen <= 0)
        return 0L;

    len = isam_file_size(fp);
    if (len <= HDRLEN)
        return 0L;

    return (len - HDRLEN) / (long)reclen;
}

long isam_record_offset(recno, reclen)
long recno;
int reclen;
{
    if (recno < 0L || reclen <= 0)
        return -1L;

    return HDRLEN + (recno * (long)reclen);
}

int isam_seek_record(fp, recno, reclen)
FILE *fp;
long recno;
int reclen;
{
    long off;

    if (!fp)
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
    if (!fp || !buf || reclen <= 0)
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
    if (!fp || !buf || reclen <= 0)
        return 0;

    if (!isam_seek_record(fp, recno, reclen))
        return 0;

    if (fwrite(buf, (unsigned)reclen, 1, fp) != 1)
        return 0;

    fflush(fp);
    return 1;
}

int isam_zero_record(fp, recno, reclen)
FILE *fp;
long recno;
int reclen;
{
    char *zbuf;
    int ok;

    if (!fp || reclen <= 0)
        return 0;

    zbuf = (char *)calloc(1, (unsigned)reclen);
    if (!zbuf)
        return 0;

    ok = isam_write_record(fp, recno, reclen, zbuf);
    free(zbuf);
    return ok;
}

FILE *isam_open(path, mode)
char *path;
char *mode;
{
    return fopen(path, mode);
}

void isam_close(fp)
FILE *fp;
{
    if (fp)
        fclose(fp);
}

int isam_read_header(fp, hdr, hdrlen)
FILE *fp;
void *hdr;
unsigned hdrlen;
{
    if (!fp || !hdr || hdrlen > (unsigned)HDRLEN)
        return 0;

    if (fseek(fp, 0L, SEEK_SET) != 0)
        return 0;

    return (fread(hdr, hdrlen, 1, fp) == 1);
}

int isam_write_header(fp, hdr, hdrlen)
FILE *fp;
void *hdr;
unsigned hdrlen;
{
    if (!fp || !hdr || hdrlen > (unsigned)HDRLEN)
        return 0;

    if (fseek(fp, 0L, SEEK_SET) != 0)
        return 0;

    if (fwrite(hdr, hdrlen, 1, fp) != 1)
        return 0;

    fflush(fp);
    return 1;
}

int isam_blank_header(fp)
FILE *fp;
{
    char hdr[HDRLEN];

    memset(hdr, 0, sizeof(hdr));
    return isam_write_header(fp, hdr, sizeof(hdr));
}

long isam_append_blank_record(fp, reclen)
FILE *fp;
int reclen;
{
    long recno;
    char *zbuf;

    if (!fp || reclen <= 0)
        return -1L;

    recno = isam_record_count(fp, reclen);

    if (fseek(fp, 0L, SEEK_END) != 0)
        return -1L;

    zbuf = (char *)calloc(1, (unsigned)reclen);
    if (!zbuf)
        return -1L;

    if (fwrite(zbuf, (unsigned)reclen, 1, fp) != 1)
    {
        free(zbuf);
        return -1L;
    }

    free(zbuf);
    fflush(fp);
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

    if (!fp || reclen <= 0 || !blank_test)
        return -1L;

    n = isam_record_count(fp, reclen);
    buf = (char *)malloc((unsigned)reclen);
    if (!buf)
        return -1L;

    for (i = 0L; i < n; i++)
    {
        if (!isam_read_record(fp, i, reclen, buf))
            continue;

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
/* file counts                                                  */
/* ------------------------------------------------------------ */

long isam_count_users(void)
{
    return isam_record_count(g_usrfp, USERREC_SIZE);
}

long isam_count_msgs(void)
{
    return isam_record_count(g_msgfp, MSGREC_SIZE);
}

long isam_count_msgtext(void)
{
    return isam_record_count(g_txtfp, MSGTEXT_SIZE);
}

long isam_count_files(void)
{
    return isam_record_count(g_udfp, UDREC_SIZE);
}

long isam_count_callers(void)
{
    return isam_record_count(g_logfp, USRLOG_SIZE);
}

/* ------------------------------------------------------------ */
/* typed record reads/writes                                    */
/* ------------------------------------------------------------ */

int isam_read_user(recno, u)
long recno;
USRDESC *u;
{
    USERREC r;

    if (!u)
        return 0;

    if (!isam_read_record(g_usrfp, recno, USERREC_SIZE, &r))
        return 0;

    userrec_to_usrdesc(u, &r);
    return 1;
}

int isam_write_user(recno, u)
long recno;
USRDESC *u;
{
    USERREC r;

    if (!u)
        return 0;

    usrdesc_to_userrec(&r, u);
    return isam_write_record(g_usrfp, recno, USERREC_SIZE, &r);
}

int isam_read_msghead(recno, h)
long recno;
MSGHEAD *h;
{
    MSGREC r;

    if (!h)
        return 0;

    if (!isam_read_record(g_msgfp, recno, MSGREC_SIZE, &r))
        return 0;

    msgrec_to_msghead(h, &r);
    return 1;
}

int isam_write_msghead(recno, h)
long recno;
MSGHEAD *h;
{
    MSGREC r;

    if (!h)
        return 0;

    msghead_to_msgrec(&r, h);
    return isam_write_record(g_msgfp, recno, MSGREC_SIZE, &r);
}

int isam_read_msgtext(recno, t)
long recno;
MSGTEXT *t;
{
    if (!t)
        return 0;

    return isam_read_record(g_txtfp, recno, MSGTEXT_SIZE, t);
}

int isam_write_msgtext(recno, t)
long recno;
MSGTEXT *t;
{
    if (!t)
        return 0;

    return isam_write_record(g_txtfp, recno, MSGTEXT_SIZE, t);
}

int isam_read_udhead(recno, u)
long recno;
UDHEAD *u;
{
    UDREC r;

    if (!u)
        return 0;

    if (!isam_read_record(g_udfp, recno, UDREC_SIZE, &r))
        return 0;

    udrec_to_udhead(u, &r);
    return 1;
}

int isam_write_udhead(recno, u)
long recno;
UDHEAD *u;
{
    UDREC r;

    if (!u)
        return 0;

    udhead_to_udrec(&r, u);
    return isam_write_record(g_udfp, recno, UDREC_SIZE, &r);
}

int isam_read_caller(recno, c)
long recno;
USRLOG *c;
{
    if (!c)
        return 0;

    return isam_read_record(g_logfp, recno, USRLOG_SIZE, c);
}

int isam_write_caller(recno, c)
long recno;
USRLOG *c;
{
    if (!c)
        return 0;

    return isam_write_record(g_logfp, recno, USRLOG_SIZE, c);
}

/* ------------------------------------------------------------ */
/* blank tests                                                  */
/* ------------------------------------------------------------ */

int isam_blank_user(buf, ctx)
void *buf;
void *ctx;
{
    (void)ctx;
    return isam_userrec_blank((USERREC *)buf);
}

int isam_blank_msghead(buf, ctx)
void *buf;
void *ctx;
{
    (void)ctx;
    return isam_msgrec_blank((MSGREC *)buf);
}

int isam_blank_msgtext(buf, ctx)
void *buf;
void *ctx;
{
    (void)ctx;
    return isam_msgtext_blank((MSGTEXT *)buf);
}

int isam_blank_udhead(buf, ctx)
void *buf;
void *ctx;
{
    (void)ctx;
    return isam_udrec_blank((UDREC *)buf);
}

int isam_blank_caller(buf, ctx)
void *buf;
void *ctx;
{
    (void)ctx;
    return isam_usrlog_blank((USRLOG *)buf);
}

/* ------------------------------------------------------------ */
/* blank record finders                                         */
/* ------------------------------------------------------------ */

long isam_find_blank_user(void)
{
    return isam_find_blank_record(g_usrfp, USERREC_SIZE, isam_blank_user, NULL);
}

long isam_find_blank_msghead(void)
{
    return isam_find_blank_record(g_msgfp, MSGREC_SIZE, isam_blank_msghead, NULL);
}

long isam_find_blank_msgtext(void)
{
    return isam_find_blank_record(g_txtfp, MSGTEXT_SIZE, isam_blank_msgtext, NULL);
}

long isam_find_blank_udhead(void)
{
    return isam_find_blank_record(g_udfp, UDREC_SIZE, isam_blank_udhead, NULL);
}

long isam_find_blank_caller(void)
{
    return isam_find_blank_record(g_logfp, USRLOG_SIZE, isam_blank_caller, NULL);
}

/* ------------------------------------------------------------ */
/* record finders                                               */
/* ------------------------------------------------------------ */

long isam_find_user_by_name(name, out)
char *name;
USRDESC *out;
{
    long i, n;
    USRDESC u;

    n = isam_count_users();
    for (i = 0L; i < n; i++)
    {
        if (!isam_read_user(i, &u))
            continue;

        if (!u.name[0])
            continue;

        if (data_user_match(u.name, name))
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
    for (i = 0L; i < n; i++)
    {
        if (!isam_read_msghead(i, &h))
            continue;

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
    for (i = 0L; i < n; i++)
    {
        if (!isam_read_udhead(i, &u))
            continue;

        if (!u.cat_name[0])
            continue;

        if (data_cat_match(u.cat_name, name))
        {
            if (out)
                *out = u;
            return i;
        }
    }

    return -1L;
}

/* ------------------------------------------------------------ */
/* utility ops                                                  */
/* ------------------------------------------------------------ */

int isam_rebuild_keys(path_dat, path_key)
char *path_dat;
char *path_key;
{
    (void)path_dat;
    (void)path_key;
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
    long n;
    long i;
    char *tmp;

    if (!fp || !buf || recno < 0L || reclen <= 0)
        return 0;

    n = isam_record_count(fp, reclen);
    if (recno > n)
        recno = n;

    tmp = (char *)malloc((unsigned)reclen);
    if (!tmp)
        return 0;

    if (n == recno)
    {
        if (isam_append_blank_record(fp, reclen) < 0L)
        {
            free(tmp);
            return 0;
        }
        free(tmp);
        return isam_write_record(fp, recno, reclen, buf);
    }

    if (isam_append_blank_record(fp, reclen) < 0L)
    {
        free(tmp);
        return 0;
    }

    for (i = n; i > recno; i--)
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