/* BBSDATA.C
 *
 * BBS-PC 4.20 common data/runtime helpers
 *
 * Date handling updated:
 * - keeps legacy packed on-disk ushort date format for compatibility
 * - normalizes internally to 4-digit years
 * - always displays YYYY in generated text/log output
 *
 * Notes:
 * - Conversion utilities are intentionally left untouched.
 * - Packed date format preserved here is DOS-style:
 *      bits 0-4   day   (1-31)
 *      bits 5-8   month (1-12)
 *      bits 9-15  year offset from 1980
 * - Display/output routines always expand to 4-digit year.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

#include "bbsdata.h"
#include "bbsfunc.h"

#ifndef HDRLEN
#define HDRLEN 128L
#endif

CFGINFO    g_cfg;
BBSPATHS   g_paths;
BBSSESSION g_sess;
NODEINFO   g_node;

FILE *g_fp_msghead  = (FILE *)0;
FILE *g_fp_msgtext  = (FILE *)0;
FILE *g_fp_userdesc = (FILE *)0;
FILE *g_fp_udhead   = (FILE *)0;
FILE *g_fp_caller   = (FILE *)0;

/* ------------------------------------------------------------ */
/* local helpers                                                */
/* ------------------------------------------------------------ */

static void trim_right_spaces(s)
char *s;
{
    int n;

    if (!s)
        return;

    n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t'))
    {
        s[n - 1] = 0;
        n--;
    }
}

static int str_ieq(a, b)
char *a;
char *b;
{
    while (*a && *b)
    {
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b))
            return 0;
        a++;
        b++;
    }

    return (*a == 0 && *b == 0);
}

static long record_offset(recno, reclen)
long recno;
int reclen;
{
    return HDRLEN + (recno * (long)reclen);
}

static int open_one(fp, path, mode)
FILE **fp;
char *path;
char *mode;
{
    *fp = fopen(path, mode);
    return (*fp != (FILE *)0);
}

static void close_one(fp)
FILE **fp;
{
    if (*fp)
        fclose(*fp);
    *fp = (FILE *)0;
}

static int read_fixed(fp, recno, reclen, out)
FILE *fp;
long recno;
int reclen;
void *out;
{
    if (!fp)
        return 0;

    if (fseek(fp, record_offset(recno, reclen), SEEK_SET) != 0)
        return 0;

    return fread(out, (unsigned)reclen, 1, fp) == 1;
}

static int write_fixed(fp, recno, reclen, in)
FILE *fp;
long recno;
int reclen;
void *in;
{
    if (!fp)
        return 0;

    if (fseek(fp, record_offset(recno, reclen), SEEK_SET) != 0)
        return 0;

    return fwrite(in, (unsigned)reclen, 1, fp) == 1;
}

static long count_fixed(fp, reclen)
FILE *fp;
int reclen;
{
    long cur, end;

    if (!fp || reclen <= 0)
        return 0L;

    cur = ftell(fp);
    fseek(fp, 0L, SEEK_END);
    end = ftell(fp);
    fseek(fp, cur, SEEK_SET);

    if (end < HDRLEN)
        return 0L;

    return (end - HDRLEN) / (long)reclen;
}

static int blank_user(u)
USRDESC *u;
{
    return !u->name[0];
}

static int blank_msg(h)
MSGHEAD *h;
{
    return h->number == 0L;
}

static int blank_file(u)
UDHEAD *u;
{
    return !u->cat_name[0];
}

static int blank_caller(c)
USRLOG *c;
{
    return !c->name[0];
}

static int normalize_year_4(y)
int y;
{
    /* Accept already-expanded years. */
    if (y >= 1980)
        return y;

    /* Conservative legacy interpretation for 2-digit inputs. */
    if (y >= 80)
        return 1900 + y;

    return 2000 + y;
}

static ushort pack_dos_date_from_parts(year4, month, day)
int year4, month, day;
{
    int yr;

    if (year4 < 1980)
        year4 = 1980;
    if (year4 > 2107)
        year4 = 2107;

    if (month < 1)
        month = 1;
    if (month > 12)
        month = 12;

    if (day < 1)
        day = 1;
    if (day > 31)
        day = 31;

    yr = year4 - 1980;

    return (ushort)(((yr & 0x7F) << 9) |
                    ((month & 0x0F) << 5) |
                    (day & 0x1F));
}

static void unpack_dos_date_to_parts(packed, year4, month, day)
ushort packed;
int *year4;
int *month;
int *day;
{
    int y, m, d;

    d = packed & 0x1F;
    m = (packed >> 5) & 0x0F;
    y = ((packed >> 9) & 0x7F) + 1980;

    if (d < 1)
        d = 1;
    if (m < 1)
        m = 1;

    if (year4)
        *year4 = normalize_year_4(y);
    if (month)
        *month = m;
    if (day)
        *day = d;
}

static ushort pack_dos_time_from_parts(hour, minute, second)
int hour, minute, second;
{
    if (hour < 0) hour = 0;
    if (hour > 23) hour = 23;
    if (minute < 0) minute = 0;
    if (minute > 59) minute = 59;
    if (second < 0) second = 0;
    if (second > 59) second = 59;

    return (ushort)(((hour & 0x1F) << 11) |
                    ((minute & 0x3F) << 5) |
                    ((second / 2) & 0x1F));
}

static void unpack_dos_time_to_parts(packed, hour, minute, second)
ushort packed;
int *hour;
int *minute;
int *second;
{
    int h, m, s;

    h = (packed >> 11) & 0x1F;
    m = (packed >> 5) & 0x3F;
    s = (packed & 0x1F) * 2;

    if (hour)   *hour = h;
    if (minute) *minute = m;
    if (second) *second = s;
}

/* ------------------------------------------------------------ */
/* path / config loading                                        */
/* ------------------------------------------------------------ */

void data_trim_crlf(s)
char *s;
{
    if (!s)
        return;

    while (*s)
    {
        if (*s == '\r' || *s == '\n')
        {
            *s = 0;
            break;
        }
        s++;
    }
}

static void copy_value(dst, src, maxlen)
char *dst;
char *src;
int maxlen;
{
    strncpy(dst, src, maxlen - 1);
    dst[maxlen - 1] = 0;
    trim_right_spaces(dst);
}

int load_bbs_paths(fname)
char *fname;
{
    FILE *fp;
    char line[256];
    char *eq;
    int secno;

    memset(&g_paths, 0, sizeof(g_paths));

    fp = fopen(fname, "rt");
    if (!fp)
        return 0;

    while (fgets(line, sizeof(line), fp))
    {
        data_trim_crlf(line);
        eq = strchr(line, '=');
        if (!eq)
            continue;

        *eq++ = 0;

        if (str_ieq(line, "MSG"))
            copy_value(g_paths.msg_path, eq, sizeof(g_paths.msg_path));
        else if (str_ieq(line, "USR"))
            copy_value(g_paths.usr_path, eq, sizeof(g_paths.usr_path));
        else if (str_ieq(line, "UD"))
            copy_value(g_paths.ud_path, eq, sizeof(g_paths.ud_path));
        else if (str_ieq(line, "LOG"))
            copy_value(g_paths.log_path, eq, sizeof(g_paths.log_path));
        else if (!strnicmp(line, "UPDN", 4))
        {
            secno = atoi(line + 4);
            if (secno >= 0 && secno < NUM_SECT)
                copy_value(g_paths.updn_path[secno], eq,
                    sizeof(g_paths.updn_path[secno]));
        }
    }

    fclose(fp);
    return 1;
}

int load_cfginfo(fname)
char *fname;
{
    FILE *fp;

    memset(&g_cfg, 0, sizeof(g_cfg));

    fp = fopen(fname, "rb");
    if (!fp)
        return 0;

    if (fread(&g_cfg, sizeof(g_cfg), 1, fp) != 1)
    {
        fclose(fp);
        memset(&g_cfg, 0, sizeof(g_cfg));
        return 0;
    }

    fclose(fp);
    return 1;
}

void data_join_path(dst, dir, name)
char *dst;
char *dir;
char *name;
{
    int n;

    if (!dir || !dir[0])
    {
        strcpy(dst, name ? name : "");
        return;
    }

    strcpy(dst, dir);
    n = strlen(dst);

    if (n > 0 && dst[n - 1] != '\\' && dst[n - 1] != '/')
        strcat(dst, "\\");

    if (name)
        strcat(dst, name);
}

void make_data_path(dst, dir, name)
char *dst;
char *dir;
char *name;
{
    data_join_path(dst, dir, name);
}

void data_make_menu_name(dst, base, menuset)
char *dst;
char *base;
int menuset;
{
    char name[32];

    if (menuset <= 0)
        sprintf(name, "%s.MEN", base);
    else
        sprintf(name, "%s%d.MEN", base, menuset);

    data_join_path(dst, g_paths.msg_path, name);
}

void data_make_bulletin_name(dst, secch)
char *dst;
int secch;
{
    char name[16];

    sprintf(name, "BULL%c.TXT", secch);
    data_join_path(dst, g_paths.msg_path, name);
}

void data_make_node_name(dst, node_num)
char *dst;
int node_num;
{
    char name[16];

    sprintf(name, "NODE%02d.DAT", node_num);
    data_join_path(dst, g_paths.log_path[0] ? g_paths.log_path : ".", name);
}

void data_make_updn_path(dst, section, fname)
char *dst;
int section;
char *fname;
{
    if (section < 0 || section >= NUM_SECT)
        section = 0;

    data_join_path(dst, g_paths.updn_path[section], fname);
}

void data_make_msg_path(dst, fname)
char *dst;
char *fname;
{
    data_join_path(dst, g_paths.msg_path, fname);
}

void data_make_user_path(dst, fname)
char *dst;
char *fname;
{
    data_join_path(dst, g_paths.usr_path, fname);
}

void data_make_ud_path(dst, fname)
char *dst;
char *fname;
{
    data_join_path(dst, g_paths.ud_path, fname);
}

void data_make_log_path(dst, fname)
char *dst;
char *fname;
{
    data_join_path(dst, g_paths.log_path, fname);
}

/* ------------------------------------------------------------ */
/* prompts                                                      */
/* ------------------------------------------------------------ */

void data_prompt_line(prompt, out, len)
char *prompt;
char *out;
int len;
{
    if (prompt)
        fputs(prompt, stdout);

    if (!fgets(out, len, stdin))
    {
        out[0] = 0;
        return;
    }

    data_trim_crlf(out);
}

void data_prompt_hidden(prompt, out, len)
char *prompt;
char *out;
int len;
{
    /* First-pass portable fallback: plain line input. */
    data_prompt_line(prompt, out, len);
}

int data_yesno(prompt, def_yes)
char *prompt;
int def_yes;
{
    char line[16];

    data_prompt_line(prompt, line, sizeof(line));
    if (!line[0])
        return def_yes ? 1 : 0;

    return (line[0] == 'Y' || line[0] == 'y');
}

/* ------------------------------------------------------------ */
/* date / time helpers                                          */
/* ------------------------------------------------------------ */

ushort data_pack_date_now(void)
{
    time_t now;
    struct tm *tmv;

    now = time((time_t *)0);
    tmv = localtime(&now);
    if (!tmv)
        return pack_dos_date_from_parts(1980, 1, 1);

    return pack_dos_date_from_parts(tmv->tm_year + 1900,
                                    tmv->tm_mon + 1,
                                    tmv->tm_mday);
}

ushort data_pack_time_now(void)
{
    time_t now;
    struct tm *tmv;

    now = time((time_t *)0);
    tmv = localtime(&now);
    if (!tmv)
        return pack_dos_time_from_parts(0, 0, 0);

    return pack_dos_time_from_parts(tmv->tm_hour,
                                    tmv->tm_min,
                                    tmv->tm_sec);
}

void data_unpack_date(d, out)
ushort d;
char *out;
{
    int y, m, day;

    unpack_dos_date_to_parts(d, &y, &m, &day);
    sprintf(out, "%04d-%02d-%02d", y, m, day);
}

void data_unpack_time(t, out)
ushort t;
char *out;
{
    int h, m, s;

    unpack_dos_time_to_parts(t, &h, &m, &s);
    sprintf(out, "%02d:%02d:%02d", h, m, s);
}

void data_now_strings(datebuf, timebuf)
char *datebuf;
char *timebuf;
{
    ushort d, t;

    d = data_pack_date_now();
    t = data_pack_time_now();

    data_unpack_date(d, datebuf);
    data_unpack_time(t, timebuf);
}

/* ------------------------------------------------------------ */
/* file existence / length                                      */
/* ------------------------------------------------------------ */

long data_disk_file_length(path)
char *path;
{
    FILE *fp;
    long len;

    fp = fopen(path, "rb");
    if (!fp)
        return -1L;

    fseek(fp, 0L, SEEK_END);
    len = ftell(fp);
    fclose(fp);
    return len;
}

int data_disk_file_exists(path)
char *path;
{
    return data_disk_file_length(path) >= 0L;
}

/* ------------------------------------------------------------ */
/* main datafile open/close                                     */
/* ------------------------------------------------------------ */

int open_main_datafiles(void)
{
    char path[MAX_PATHNAME];
    int ok = 1;

    data_make_msg_path(path, "MSGHEAD.DAT");
    ok &= open_one(&g_fp_msghead, path, "r+b");

    data_make_msg_path(path, "MSGTEXT.DAT");
    ok &= open_one(&g_fp_msgtext, path, "r+b");

    data_make_user_path(path, "USERDESC.DAT");
    ok &= open_one(&g_fp_userdesc, path, "r+b");

    data_make_ud_path(path, "UDHEAD.DAT");
    ok &= open_one(&g_fp_udhead, path, "r+b");

    data_make_log_path(path, "CALLER.DAT");
    ok &= open_one(&g_fp_caller, path, "r+b");

    if (!ok)
        close_main_datafiles();

    return ok;
}

void close_main_datafiles(void)
{
    close_one(&g_fp_msghead);
    close_one(&g_fp_msgtext);
    close_one(&g_fp_userdesc);
    close_one(&g_fp_udhead);
    close_one(&g_fp_caller);
}

int data_verify_open_files(void)
{
    return g_fp_msghead && g_fp_msgtext && g_fp_userdesc &&
           g_fp_udhead && g_fp_caller;
}

/* ------------------------------------------------------------ */
/* counts                                                       */
/* ------------------------------------------------------------ */

long data_user_count(void)
{
    return count_fixed(g_fp_userdesc, sizeof(USRDESC));
}

long data_msg_count(void)
{
    return count_fixed(g_fp_msghead, sizeof(MSGHEAD));
}

long data_msgtext_count(void)
{
    return count_fixed(g_fp_msgtext, sizeof(MSGTEXT));
}

long data_file_count(void)
{
    return count_fixed(g_fp_udhead, sizeof(UDHEAD));
}

long data_caller_count(void)
{
    return count_fixed(g_fp_caller, sizeof(USRLOG));
}

/* ------------------------------------------------------------ */
/* direct record I/O                                            */
/* ------------------------------------------------------------ */

int data_read_user(recno, u)
long recno;
USRDESC *u;
{
    return read_fixed(g_fp_userdesc, recno, sizeof(USRDESC), u);
}

int data_write_user(recno, u)
long recno;
USRDESC *u;
{
    return write_fixed(g_fp_userdesc, recno, sizeof(USRDESC), u);
}

int data_read_msghead(recno, h)
long recno;
MSGHEAD *h;
{
    return read_fixed(g_fp_msghead, recno, sizeof(MSGHEAD), h);
}

int data_write_msghead(recno, h)
long recno;
MSGHEAD *h;
{
    return write_fixed(g_fp_msghead, recno, sizeof(MSGHEAD), h);
}

int data_read_msgtext(recno, t)
long recno;
MSGTEXT *t;
{
    return read_fixed(g_fp_msgtext, recno, sizeof(MSGTEXT), t);
}

int data_write_msgtext(recno, t)
long recno;
MSGTEXT *t;
{
    return write_fixed(g_fp_msgtext, recno, sizeof(MSGTEXT), t);
}

int data_read_udhead(recno, u)
long recno;
UDHEAD *u;
{
    return read_fixed(g_fp_udhead, recno, sizeof(UDHEAD), u);
}

int data_write_udhead(recno, u)
long recno;
UDHEAD *u;
{
    return write_fixed(g_fp_udhead, recno, sizeof(UDHEAD), u);
}

int data_read_caller(recno, c)
long recno;
USRLOG *c;
{
    return read_fixed(g_fp_caller, recno, sizeof(USRLOG), c);
}

int data_write_caller(recno, c)
long recno;
USRLOG *c;
{
    return write_fixed(g_fp_caller, recno, sizeof(USRLOG), c);
}

/* ------------------------------------------------------------ */
/* blank slot finders                                           */
/* ------------------------------------------------------------ */

long data_find_blank_user(void)
{
    long i, n;
    USRDESC u;

    n = data_user_count();
    for (i = 0L; i < n; i++)
        if (data_read_user(i, &u) && blank_user(&u))
            return i;

    return -1L;
}

long data_find_blank_msghead(void)
{
    long i, n;
    MSGHEAD h;

    n = data_msg_count();
    for (i = 0L; i < n; i++)
        if (data_read_msghead(i, &h) && blank_msg(&h))
            return i;

    return -1L;
}

long data_find_blank_msgtext(void)
{
    long i, n;
    MSGTEXT t;

    n = data_msgtext_count();
    for (i = 0L; i < n; i++)
    {
        if (!data_read_msgtext(i, &t))
            continue;
        if (!t.text[0])
            return i;
    }

    return -1L;
}

long data_find_blank_udhead(void)
{
    long i, n;
    UDHEAD u;

    n = data_file_count();
    for (i = 0L; i < n; i++)
        if (data_read_udhead(i, &u) && blank_file(&u))
            return i;

    return -1L;
}

long data_find_blank_caller(void)
{
    long i, n;
    USRLOG c;

    n = data_caller_count();
    for (i = 0L; i < n; i++)
        if (data_read_caller(i, &c) && blank_caller(&c))
            return i;

    return -1L;
}

/* ------------------------------------------------------------ */
/* search                                                       */
/* ------------------------------------------------------------ */

int data_name_match(a, b, maxlen)
char *a;
char *b;
int maxlen;
{
    int i;
    int ca, cb;

    for (i = 0; i < maxlen; i++)
    {
        ca = toupper((unsigned char)a[i]);
        cb = toupper((unsigned char)b[i]);

        if (ca != cb)
            return 0;
        if (!ca && !cb)
            return 1;
        if (!ca || !cb)
            return 0;
    }

    return 1;
}

int data_cat_match(a, b)
char *a;
char *b;
{
    return data_name_match(a, b, CAT_LEN);
}

int data_user_match(a, b)
char *a;
char *b;
{
    return data_name_match(a, b, NAME_LEN);
}

long data_find_user_by_name(name, out)
char *name;
USRDESC *out;
{
    long i, n;
    USRDESC u;

    n = data_user_count();
    for (i = 0L; i < n; i++)
    {
        if (!data_read_user(i, &u))
            continue;
        if (blank_user(&u))
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

long data_find_msg_by_number(msgno, out)
long msgno;
MSGHEAD *out;
{
    long i, n;
    MSGHEAD h;

    n = data_msg_count();
    for (i = 0L; i < n; i++)
    {
        if (!data_read_msghead(i, &h))
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

long data_find_file_by_catalog(name, out)
char *name;
UDHEAD *out;
{
    long i, n;
    UDHEAD u;

    n = data_file_count();
    for (i = 0L; i < n; i++)
    {
        if (!data_read_udhead(i, &u))
            continue;
        if (blank_file(&u))
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
/* iteration                                                    */
/* ------------------------------------------------------------ */

long data_first_user(out)
USRDESC *out;
{
    return data_next_user(-1L, out);
}

long data_next_user(cur, out)
long cur;
USRDESC *out;
{
    long i, n;
    USRDESC u;

    n = data_user_count();
    for (i = cur + 1L; i < n; i++)
    {
        if (!data_read_user(i, &u))
            continue;
        if (blank_user(&u))
            continue;
        if (out)
            *out = u;
        return i;
    }

    return -1L;
}

long data_first_msg(out)
MSGHEAD *out;
{
    return data_next_msg(-1L, out);
}

long data_next_msg(cur, out)
long cur;
MSGHEAD *out;
{
    long i, n;
    MSGHEAD h;

    n = data_msg_count();
    for (i = cur + 1L; i < n; i++)
    {
        if (!data_read_msghead(i, &h))
            continue;
        if (blank_msg(&h))
            continue;
        if (out)
            *out = h;
        return i;
    }

    return -1L;
}

long data_first_file(out)
UDHEAD *out;
{
    return data_next_file(-1L, out);
}

long data_next_file(cur, out)
long cur;
UDHEAD *out;
{
    long i, n;
    UDHEAD u;

    n = data_file_count();
    for (i = cur + 1L; i < n; i++)
    {
        if (!data_read_udhead(i, &u))
            continue;
        if (blank_file(&u))
            continue;
        if (out)
            *out = u;
        return i;
    }

    return -1L;
}

long data_first_caller(out)
USRLOG *out;
{
    return data_next_caller(-1L, out);
}

long data_next_caller(cur, out)
long cur;
USRLOG *out;
{
    long i, n;
    USRLOG c;

    n = data_caller_count();
    for (i = cur + 1L; i < n; i++)
    {
        if (!data_read_caller(i, &c))
            continue;
        if (blank_caller(&c))
            continue;
        if (out)
            *out = c;
        return i;
    }

    return -1L;
}

/* ------------------------------------------------------------ */
/* message text helpers                                         */
/* ------------------------------------------------------------ */

int data_load_message_text(first_recno, out, outlen)
ushort first_recno;
char *out;
int outlen;
{
    long recno;
    MSGTEXT t;
    int used;

    if (!out || outlen <= 0)
        return 0;

    out[0] = 0;
    used = 0;
    recno = (long)first_recno;

    while (recno >= 0 && recno < data_msgtext_count())
    {
        if (!data_read_msgtext(recno, &t))
            break;
        if (!t.text[0])
            break;

        if ((used + strlen(t.text) + 2) >= outlen)
            break;

        strcpy(out + used, t.text);
        used += strlen(t.text);
        out[used++] = '\n';
        out[used] = 0;

        recno++;
    }

    return 1;
}

int data_store_message_text(text, first_recno)
char *text;
ushort *first_recno;
{
    long rec;
    MSGTEXT t;
    int len, pos, chunk;

    rec = data_find_blank_msgtext();
    if (rec < 0L)
        return 0;

    if (first_recno)
        *first_recno = (ushort)rec;

    len = strlen(text);
    pos = 0;

    while (pos < len)
    {
        memset(&t, 0, sizeof(t));
        chunk = sizeof(t.text) - 1;
        if ((len - pos) < chunk)
            chunk = len - pos;

        memcpy(t.text, text + pos, chunk);
        t.text[chunk] = 0;

        if (!data_write_msgtext(rec, &t))
            return 0;

        rec++;
        pos += chunk;
    }

    return 1;
}

void data_zero_message_chain(first_rec)
ushort first_rec;
{
    long rec;
    MSGTEXT t;

    memset(&t, 0, sizeof(t));
    rec = (long)first_rec;

    while (rec >= 0 && rec < data_msgtext_count())
    {
        if (!data_read_msgtext(rec, &t))
            break;
        if (!t.text[0])
            break;

        memset(&t, 0, sizeof(t));
        if (!data_write_msgtext(rec, &t))
            break;
        rec++;
    }
}

/* ------------------------------------------------------------ */
/* summary helpers                                              */
/* ------------------------------------------------------------ */

long data_highest_msg_number(void)
{
    long i, n, hi;
    MSGHEAD h;

    hi = 0L;
    n = data_msg_count();

    for (i = 0L; i < n; i++)
        if (data_read_msghead(i, &h) && h.number > hi)
            hi = h.number;

    return hi;
}

long data_lowest_msg_number(void)
{
    long i, n, lo;
    MSGHEAD h;

    lo = 0L;
    n = data_msg_count();

    for (i = 0L; i < n; i++)
    {
        if (!data_read_msghead(i, &h))
            continue;
        if (!h.number)
            continue;
        if (!lo || h.number < lo)
            lo = h.number;
    }

    return lo;
}

long data_count_visible_msgs(mask)
bits mask;
{
    long i, n, cnt;
    MSGHEAD h;

    cnt = 0L;
    n = data_msg_count();

    for (i = 0L; i < n; i++)
    {
        if (!data_read_msghead(i, &h))
            continue;
        if (!h.number)
            continue;
        if (mask & (1 << h.section))
            cnt++;
    }

    return cnt;
}

long data_count_visible_files(mask)
bits mask;
{
    long i, n, cnt;
    UDHEAD u;

    cnt = 0L;
    n = data_file_count();

    for (i = 0L; i < n; i++)
    {
        if (!data_read_udhead(i, &u))
            continue;
        if (blank_file(&u))
            continue;
        if (mask & (1 << u.section))
            cnt++;
    }

    return cnt;
}

int data_sync_indexes(void)
{
    /* Placeholder: real key maintenance still deferred. */
    return 1;
}