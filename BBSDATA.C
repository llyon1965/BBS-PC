/* BBSDATA.C
 *
 * BBS-PC ! 4.21
 *
 * Reconstructed and modernised source
 * derived from BBS-PC 4.20
 *
 * Data/config/path helpers and runtime-facing wrappers around
 * the fixed-record ISAM layer.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

#include "bbsdata.h"
#include "bbsfunc.h"

/* ------------------------------------------------------------ */
/* globals                                                      */
/* ------------------------------------------------------------ */

CFGINFO    g_cfg;
BBSPATHS   g_paths;
BBSSESSION g_sess;
NODEINFO   g_node;
PARAMS     g_modem;

FILE *g_usrfp = NULL;
FILE *g_msgfp = NULL;
FILE *g_txtfp = NULL;
FILE *g_udfp  = NULL;
FILE *g_logfp = NULL;

/* ------------------------------------------------------------ */
/* local helpers                                                */
/* ------------------------------------------------------------ */

static void data_zero_cfg(void)
{
    memset(&g_cfg, 0, sizeof(g_cfg));
}

static void data_zero_paths(void)
{
    memset(&g_paths, 0, sizeof(g_paths));
}

static void data_trim_right(s)
char *s;
{
    int n;

    if (!s)
        return;

    n = strlen(s);
    while (n > 0)
    {
        if (s[n - 1] == ' ' || s[n - 1] == '\t' ||
            s[n - 1] == '\r' || s[n - 1] == '\n')
        {
            s[n - 1] = 0;
            n--;
        }
        else
            break;
    }
}

static void data_trim_left_inplace(s)
char *s;
{
    char *p;

    if (!s)
        return;

    p = s;
    while (*p == ' ' || *p == '\t')
        p++;

    if (p != s)
        memmove(s, p, strlen(p) + 1);
}

static void data_trim_all(s)
char *s;
{
    data_trim_right(s);
    data_trim_left_inplace(s);
}

static int data_parse_kv_line(line, key, val)
char *line;
char *key;
char *val;
{
    char *eq;

    if (!line || !key || !val)
        return 0;

    eq = strchr(line, '=');
    if (!eq)
        return 0;

    *eq = 0;
    strcpy(key, line);
    strcpy(val, eq + 1);

    data_trim_all(key);
    data_trim_all(val);
    return 1;
}

static void data_fix_path_sep(path)
char *path;
{
    while (*path)
    {
        if (*path == '/')
            *path = '\\';
        path++;
    }
}

static void data_ensure_trailing_backslash(path)
char *path;
{
    int n;

    if (!path || !path[0])
        return;

    n = strlen(path);
    if (path[n - 1] != '\\' && path[n - 1] != '/')
    {
        path[n] = '\\';
        path[n + 1] = 0;
    }
}

static int data_open_one_file(fpp, path)
FILE **fpp;
char *path;
{
    FILE *fp;

    fp = isam_open(path, "r+b");
    if (!fp)
    {
        fp = isam_open(path, "w+b");
        if (!fp)
            return 0;

        if (!isam_blank_header(fp))
        {
            isam_close(fp);
            return 0;
        }
    }

    *fpp = fp;
    return 1;
}

/* ------------------------------------------------------------ */
/* config / paths                                               */
/* ------------------------------------------------------------ */

int load_bbs_paths(fname)
char *fname;
{
    FILE *fp;
    char line[256];
    char key[64];
    char val[192];
    int i;

    data_zero_paths();

    for (i = 0; i < NUM_SECT; i++)
        g_paths.updn_path[i][0] = 0;

    fp = fopen(fname, "rt");
    if (!fp)
        return 0;

    while (fgets(line, sizeof(line), fp))
    {
        data_trim_crlf(line);
        data_trim_all(line);

        if (!line[0])
            continue;
        if (line[0] == ';')
            continue;

        if (!data_parse_kv_line(line, key, val))
            continue;

        if (!stricmp(key, "MSG"))
            strncpy(g_paths.msg_path, val, sizeof(g_paths.msg_path) - 1);
        else if (!stricmp(key, "USR"))
            strncpy(g_paths.usr_path, val, sizeof(g_paths.usr_path) - 1);
        else if (!stricmp(key, "UD"))
            strncpy(g_paths.ud_path, val, sizeof(g_paths.ud_path) - 1);
        else if (!stricmp(key, "LOG"))
            strncpy(g_paths.log_path, val, sizeof(g_paths.log_path) - 1);
        else if (!strnicmp(key, "UPDN", 4))
        {
            i = atoi(key + 4);
            if (i >= 0 && i < NUM_SECT)
                strncpy(g_paths.updn_path[i], val, sizeof(g_paths.updn_path[i]) - 1);
        }
    }

    fclose(fp);

    data_fix_path_sep(g_paths.msg_path);
    data_fix_path_sep(g_paths.usr_path);
    data_fix_path_sep(g_paths.ud_path);
    data_fix_path_sep(g_paths.log_path);

    data_ensure_trailing_backslash(g_paths.msg_path);
    data_ensure_trailing_backslash(g_paths.usr_path);
    data_ensure_trailing_backslash(g_paths.ud_path);
    data_ensure_trailing_backslash(g_paths.log_path);

    for (i = 0; i < NUM_SECT; i++)
    {
        data_fix_path_sep(g_paths.updn_path[i]);
        if (g_paths.updn_path[i][0])
            data_ensure_trailing_backslash(g_paths.updn_path[i]);
    }

    return 1;
}

int load_cfginfo(fname)
char *fname;
{
    FILE *fp;
    char line[256];
    char key[64];
    char val[192];

    data_zero_cfg();

    fp = fopen(fname, "rb");
    if (fp)
    {
        size_t nread;

        nread = fread(&g_cfg, 1, sizeof(g_cfg), fp);
        fclose(fp);

        if (nread == sizeof(g_cfg))
            return 1;
    }

    fp = fopen(fname, "rt");
    if (!fp)
        return 0;

    while (fgets(line, sizeof(line), fp))
    {
        data_trim_crlf(line);
        data_trim_all(line);

        if (!line[0])
            continue;
        if (line[0] == ';')
            continue;

        if (!data_parse_kv_line(line, key, val))
            continue;

        if (!stricmp(key, "BBSNAME"))
            strncpy(g_cfg.bbsname, val, sizeof(g_cfg.bbsname) - 1);
        else if (!stricmp(key, "SYSOPNAME"))
            strncpy(g_cfg.sysopname, val, sizeof(g_cfg.sysopname) - 1);
        else if (!stricmp(key, "SYSPASS"))
            strncpy(g_cfg.sysop_pass, val, sizeof(g_cfg.sysop_pass) - 1);
        else if (!stricmp(key, "NODE"))
            g_cfg.node = atoi(val);
        else if (!stricmp(key, "MINBAUD"))
            g_cfg.min_baud = atoi(val);
        else if (!stricmp(key, "MAXBAUD"))
            g_cfg.max_baud = atoi(val);
        else if (!stricmp(key, "PAGELEN"))
            g_cfg.page_len = atoi(val);
        else if (!stricmp(key, "MAXNODES"))
            g_cfg.max_nodes = atoi(val);
        else if (!stricmp(key, "GUESTLIMIT"))
            g_cfg.limit[0] = (ushort)atoi(val);
        else if (!stricmp(key, "USERLIMIT"))
            g_cfg.limit[1] = (ushort)atoi(val);
        else if (!stricmp(key, "GUESTPRIV"))
            g_cfg.priv[0] = (byte)atoi(val);
        else if (!stricmp(key, "USERPRIV"))
            g_cfg.priv[1] = (byte)atoi(val);
        else if (!stricmp(key, "IDLELIMIT"))
            g_cfg.idle_limit = (ushort)atoi(val);
    }

    fclose(fp);
    return 1;
}

/* ------------------------------------------------------------ */
/* data file open/close                                         */
/* ------------------------------------------------------------ */

int open_main_datafiles(void)
{
    char path[MAX_PATHNAME * 2];

    data_make_data_path(path, "USERDESC.DAT");
    if (!data_open_one_file(&g_usrfp, path))
        return 0;

    data_make_data_path(path, "MSGHEAD.DAT");
    if (!data_open_one_file(&g_msgfp, path))
        return 0;

    data_make_data_path(path, "MSGTEXT.DAT");
    if (!data_open_one_file(&g_txtfp, path))
        return 0;

    data_make_data_path(path, "UDHEAD.DAT");
    if (!data_open_one_file(&g_udfp, path))
        return 0;

    data_make_data_path(path, "CALLER.DAT");
    if (!data_open_one_file(&g_logfp, path))
        return 0;

    return 1;
}

void close_main_datafiles(void)
{
    if (g_usrfp) { isam_close(g_usrfp); g_usrfp = NULL; }
    if (g_msgfp) { isam_close(g_msgfp); g_msgfp = NULL; }
    if (g_txtfp) { isam_close(g_txtfp); g_txtfp = NULL; }
    if (g_udfp)  { isam_close(g_udfp);  g_udfp  = NULL; }
    if (g_logfp) { isam_close(g_logfp); g_logfp = NULL; }
}

/* ------------------------------------------------------------ */
/* utility/string/date helpers                                  */
/* ------------------------------------------------------------ */

void data_trim_crlf(s)
char *s;
{
    char *p;

    if (!s)
        return;

    p = strchr(s, '\r');
    if (p) *p = 0;
    p = strchr(s, '\n');
    if (p) *p = 0;
}

void data_now_strings(d, t)
char *d;
char *t;
{
    time_t now;
    struct tm *tmv;

    now = time((time_t *)0);
    tmv = localtime(&now);

    if (d)
        sprintf(d, "%04d-%02d-%02d",
                tmv->tm_year + 1900, tmv->tm_mon + 1, tmv->tm_mday);

    if (t)
        sprintf(t, "%02d:%02d:%02d",
                tmv->tm_hour, tmv->tm_min, tmv->tm_sec);
}

void data_prompt_line(prompt, buf, buflen)
char *prompt;
char *buf;
int buflen;
{
    if (prompt)
        fputs(prompt, stdout);

    if (!fgets(buf, buflen, stdin))
    {
        if (buflen > 0)
            buf[0] = 0;
        return;
    }

    data_trim_crlf(buf);
}

ushort data_pack_date_now(void)
{
    time_t now;
    struct tm *tmv;
    int year;

    now = time((time_t *)0);
    tmv = localtime(&now);

    year = (tmv->tm_year + 1900) - 1980;
    if (year < 0)
        year = 0;
    if (year > 127)
        year = 127;

    return (ushort)((year << 9) | ((tmv->tm_mon + 1) << 5) | tmv->tm_mday);
}

ushort data_pack_time_now(void)
{
    time_t now;
    struct tm *tmv;

    now = time((time_t *)0);
    tmv = localtime(&now);

    return (ushort)((tmv->tm_hour << 11) | (tmv->tm_min << 5) | (tmv->tm_sec / 2));
}

void data_unpack_date(p, out)
ushort p;
char *out;
{
    int year, mon, day;

    year = 1980 + ((p >> 9) & 0x7F);
    mon  = (p >> 5) & 0x0F;
    day  = p & 0x1F;

    sprintf(out, "%04d-%02d-%02d", year, mon, day);
}

void data_unpack_time(p, out)
ushort p;
char *out;
{
    int hour, min, sec;

    hour = (p >> 11) & 0x1F;
    min  = (p >> 5) & 0x3F;
    sec  = (p & 0x1F) * 2;

    sprintf(out, "%02d:%02d:%02d", hour, min, sec);
}

int data_user_match(a, b)
char *a;
char *b;
{
    return !stricmp(a ? a : "", b ? b : "");
}

int data_name_match(a, b, maxlen)
char *a;
char *b;
int maxlen;
{
    return !strnicmp(a ? a : "", b ? b : "", maxlen);
}

int data_cat_match(a, b)
char *a;
char *b;
{
    return !stricmp(a ? a : "", b ? b : "");
}

/* ------------------------------------------------------------ */
/* counters / finders / wrappers                                */
/* ------------------------------------------------------------ */

long data_user_count(void)
{
    return isam_count_users();
}

long data_msg_count(void)
{
    return isam_count_msgs();
}

long data_msgtext_count(void)
{
    return isam_count_msgtext();
}

long data_file_count(void)
{
    return isam_count_files();
}

long data_caller_count(void)
{
    return isam_count_callers();
}

long data_highest_msg_number(void)
{
    long i, n;
    long high;
    MSGHEAD h;

    n = data_msg_count();
    high = 0L;

    for (i = 0L; i < n; i++)
    {
        if (!data_read_msghead(i, &h))
            continue;
        if (h.number > high)
            high = h.number;
    }

    return high;
}

long data_find_user_by_name(name, u)
char *name;
USRDESC *u;
{
    return isam_find_user_by_name(name, u);
}

long data_find_msg_by_number(msgno, h)
long msgno;
MSGHEAD *h;
{
    return isam_find_msg_by_number(msgno, h);
}

long data_find_file_by_catalog(name, u)
char *name;
UDHEAD *u;
{
    return isam_find_file_by_catalog(name, u);
}

long data_find_blank_user(void)
{
    return isam_find_blank_user();
}

long data_find_blank_msghead(void)
{
    return isam_find_blank_msghead();
}

long data_find_blank_msgtext(void)
{
    return isam_find_blank_msgtext();
}

long data_find_blank_udhead(void)
{
    return isam_find_blank_udhead();
}

long data_find_blank_caller(void)
{
    return isam_find_blank_caller();
}

int data_write_user(recno, u)
long recno;
USRDESC *u;
{
    return isam_write_user(recno, u);
}

int data_write_msghead(recno, h)
long recno;
MSGHEAD *h;
{
    return isam_write_msghead(recno, h);
}

int data_write_msgtext(recno, t)
long recno;
MSGTEXT *t;
{
    return isam_write_msgtext(recno, t);
}

int data_write_udhead(recno, u)
long recno;
UDHEAD *u;
{
    return isam_write_udhead(recno, u);
}

int data_write_caller(recno, c)
long recno;
USRLOG *c;
{
    return isam_write_caller(recno, c);
}

int data_read_user(recno, u)
long recno;
USRDESC *u;
{
    return isam_read_user(recno, u);
}

int data_read_msghead(recno, h)
long recno;
MSGHEAD *h;
{
    return isam_read_msghead(recno, h);
}

int data_read_msgtext(recno, t)
long recno;
MSGTEXT *t;
{
    return isam_read_msgtext(recno, t);
}

int data_read_udhead(recno, u)
long recno;
UDHEAD *u;
{
    return isam_read_udhead(recno, u);
}

int data_read_caller(recno, c)
long recno;
USRLOG *c;
{
    return isam_read_caller(recno, c);
}

/* ------------------------------------------------------------ */
/* message text helpers                                         */
/* ------------------------------------------------------------ */

int data_store_message_text(body, firstptr)
char *body;
ushort *firstptr;
{
    long startrec;
    long currec;
    MSGTEXT t;
    int len;
    int pos;

    if (!body || !firstptr)
        return 0;

    startrec = data_find_blank_msgtext();
    if (startrec < 0L)
        startrec = data_msgtext_count();

    *firstptr = (ushort)startrec;

    len = strlen(body);
    pos = 0;
    currec = startrec;

    if (len == 0)
    {
        memset(&t, 0, sizeof(t));
        return data_write_msgtext(currec, &t);
    }

    while (pos < len)
    {
        int chunk;

        memset(&t, 0, sizeof(t));
        chunk = len - pos;
        if (chunk > (TEXT_LEN - 1))
            chunk = TEXT_LEN - 1;

        memcpy(t.text, body + pos, chunk);
        t.text[chunk] = 0;

        if (currec >= data_msgtext_count())
        {
            if (isam_append_blank_record(g_txtfp, MSGTEXT_SIZE) < 0L)
                return 0;
        }

        if (!data_write_msgtext(currec, &t))
            return 0;

        pos += chunk;
        currec++;
    }

    memset(&t, 0, sizeof(t));
    if (currec >= data_msgtext_count())
    {
        if (isam_append_blank_record(g_txtfp, MSGTEXT_SIZE) < 0L)
            return 0;
    }

    return data_write_msgtext(currec, &t);
}

int data_load_message_text(firstptr, out, outlen)
ushort firstptr;
char *out;
int outlen;
{
    long recno;
    long ntext;
    int used;
    MSGTEXT t;

    if (!out || outlen <= 0)
        return 0;

    out[0] = 0;

    if (firstptr == 0)
        return 1;

    ntext = data_msgtext_count();
    recno = (long)firstptr;
    used = 0;

    while (recno < ntext)
    {
        if (!data_read_msgtext(recno, &t))
            return 0;

        if (!t.text[0])
            break;

        if ((used + strlen(t.text)) >= (unsigned)(outlen - 1))
            break;

        strcat(out, t.text);
        used += strlen(t.text);
        recno++;
    }

    return 1;
}

void data_zero_message_chain(firstptr)
ushort firstptr;
{
    long recno;
    long ntext;
    MSGTEXT t;

    if (firstptr == 0)
        return;

    ntext = data_msgtext_count();
    recno = (long)firstptr;

    while (recno < ntext)
    {
        if (!data_read_msgtext(recno, &t))
            break;

        if (!t.text[0])
            break;

        memset(&t, 0, sizeof(t));
        (void)data_write_msgtext(recno, &t);
        recno++;
    }

    memset(&t, 0, sizeof(t));
    if (recno < ntext)
        (void)data_write_msgtext(recno, &t);
}

/* ------------------------------------------------------------ */
/* path / file helpers                                          */
/* ------------------------------------------------------------ */

void data_make_data_path(out, name)
char *out;
char *name;
{
    if (!out)
        return;

    if (!name)
        name = "";

    sprintf(out, "%s%s", g_paths.msg_path, name);
}

void data_make_updn_path(out, section, name)
char *out;
int section;
char *name;
{
    char *base;

    if (!out)
        return;

    if (section >= 0 && section < NUM_SECT && g_paths.updn_path[section][0])
        base = g_paths.updn_path[section];
    else
        base = g_paths.ud_path;

    sprintf(out, "%s%s", base, name ? name : "");
}

int data_disk_file_exists(path)
char *path;
{
    FILE *fp;

    fp = fopen(path, "rb");
    if (!fp)
        return 0;

    fclose(fp);
    return 1;
}

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