/* BBSDATA.C
 *
 * BBS-PC! 4.21
 *
 * Core data/config/path/file wrappers.
 *
 * Ownership:
 * - BBSPATHS.CFG loading
 * - CFGINFO.DAT loading/saving
 * - main datafile open/close
 * - data_* wrappers over ISAM helpers
 * - date/time helpers
 * - path helpers
 * - message text chain storage/load helpers
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

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

FILE *g_usrfp = (FILE *)0;
FILE *g_msgfp = (FILE *)0;
FILE *g_txtfp = (FILE *)0;
FILE *g_udfp  = (FILE *)0;
FILE *g_logfp = (FILE *)0;

/* ------------------------------------------------------------ */
/* local helpers                                                */
/* ------------------------------------------------------------ */

static void data_zero_paths(void)
{
    memset(&g_paths, 0, sizeof(g_paths));
}

static void data_zero_cfg(void)
{
    int i;

    memset(&g_cfg, 0, sizeof(g_cfg));

    strcpy(g_cfg.bbsname, "BBS-PC!");
    strcpy(g_cfg.sysopname, "SYSOP");
    strcpy(g_cfg.sysop_pass, "");

    g_cfg.node = 1;
    g_cfg.min_baud = 300;
    g_cfg.max_baud = 38400;
    g_cfg.page_len = 24;
    g_cfg.max_nodes = 2;

    g_cfg.modem_baud = 2400;
    g_cfg.modem_parity = 'N';
    g_cfg.modem_data_bits = 8;
    g_cfg.modem_stop_bits = 1;

    g_cfg.limit[0] = 30;
    g_cfg.limit[1] = 60;
    g_cfg.priv[0] = 1;
    g_cfg.priv[1] = 10;
    g_cfg.idle_limit = 0;

    for (i = 0; i < NUM_SECT; i++)
    {
        sprintf(g_cfg.sect_name[i], "Section %d", i);
        g_cfg.rd_acc[i] = (bits)(1U << i);
        g_cfg.wr_acc[i] = (bits)(1U << i);
        g_cfg.up_acc[i] = (bits)(1U << i);
        g_cfg.dn_acc[i] = (bits)(1U << i);
    }

    strcpy(g_cfg.term_name[0], "TTY");
    strcpy(g_cfg.term_name[1], "ANSI");
    strcpy(g_cfg.term_name[2], "AVATAR");
    strcpy(g_cfg.term_name[3], "RIP");
    strcpy(g_cfg.term_name[4], "VT52");
    strcpy(g_cfg.term_name[5], "VT100");
    strcpy(g_cfg.term_name[6], "PC8");
    strcpy(g_cfg.term_name[7], "Custom");
}

static void data_trim_spaces(s)
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

static char *data_cfg_value(line)
char *line;
{
    char *p;

    p = strchr(line, '=');
    if (!p)
        return (char *)0;

    *p++ = 0;
    data_trim_spaces(line);
    data_trim_spaces(p);
    return p;
}

static void data_set_default_paths(void)
{
    int i;

    if (!g_paths.msg_path[0]) strcpy(g_paths.msg_path, ".");
    if (!g_paths.usr_path[0]) strcpy(g_paths.usr_path, ".");
    if (!g_paths.ud_path[0])  strcpy(g_paths.ud_path,  ".");
    if (!g_paths.log_path[0]) strcpy(g_paths.log_path, ".");

    for (i = 0; i < NUM_SECT; i++)
        if (!g_paths.updn_path[i][0])
            strcpy(g_paths.updn_path[i], ".");
}

static void data_join_path(out, dir, name)
char *out;
char *dir;
char *name;
{
    int n;

    if (!dir || !dir[0] || !strcmp(dir, "."))
    {
        strcpy(out, name);
        return;
    }

    strcpy(out, dir);
    n = strlen(out);
    if (n > 0 && out[n - 1] != '\\' && out[n - 1] != '/')
        strcat(out, "\\");
    strcat(out, name);
}

static void data_make_user_path(out, name)
char *out;
char *name;
{
    data_join_path(out, g_paths.usr_path, name);
}

static void data_make_ud_path(out, name)
char *out;
char *name;
{
    data_join_path(out, g_paths.ud_path, name);
}

static void data_make_log_path(out, name)
char *out;
char *name;
{
    data_join_path(out, g_paths.log_path, name);
}

static int data_ensure_header_file(path)
char *path;
{
    FILE *fp;
    char hdr[HDRLEN];

    fp = fopen(path, "r+b");
    if (fp)
    {
        fclose(fp);
        return 1;
    }

    fp = fopen(path, "w+b");
    if (!fp)
        return 0;

    memset(hdr, 0, sizeof(hdr));
    if (fwrite(hdr, sizeof(hdr), 1, fp) != 1)
    {
        fclose(fp);
        return 0;
    }

    fclose(fp);
    return 1;
}

static void data_unpack_date_time_parts(packed, year, month, day)
ushort packed;
int *year;
int *month;
int *day;
{
    if (year)  *year  = 1980 + ((packed >> 9) & 0x7F);
    if (month) *month = (packed >> 5) & 0x0F;
    if (day)   *day   = packed & 0x1F;
}

static void data_unpack_clock_parts(packed, hour, minute, second)
ushort packed;
int *hour;
int *minute;
int *second;
{
    if (hour)   *hour   = (packed >> 11) & 0x1F;
    if (minute) *minute = (packed >> 5) & 0x3F;
    if (second) *second = (packed & 0x1F) * 2;
}

static int data_stricmp(a, b)
char *a;
char *b;
{
    int ca, cb;

    if (!a) a = "";
    if (!b) b = "";

    while (*a || *b)
    {
        ca = toupper((unsigned char)*a);
        cb = toupper((unsigned char)*b);

        if (ca != cb)
            return ca - cb;

        if (*a) a++;
        if (*b) b++;
    }

    return 0;
}

static int data_strnicmp(a, b, maxlen)
char *a;
char *b;
int maxlen;
{
    int ca, cb;
    int i;

    if (!a) a = "";
    if (!b) b = "";

    for (i = 0; i < maxlen; i++)
    {
        ca = toupper((unsigned char)a[i]);
        cb = toupper((unsigned char)b[i]);

        if (ca != cb)
            return ca - cb;

        if (!a[i] || !b[i])
            break;
    }

    return 0;
}

/* ------------------------------------------------------------ */
/* paths/config                                                 */
/* ------------------------------------------------------------ */

int load_bbs_paths(fname)
char *fname;
{
    FILE *fp;
    char line[256];
    int sec;

    data_zero_paths();
    data_set_default_paths();

    fp = fopen(fname, "rt");
    if (!fp)
        return 0;

    while (fgets(line, sizeof(line), fp))
    {
        char *val;
        char key[64];

        data_trim_crlf(line);
        data_trim_spaces(line);

        if (!line[0] || line[0] == ';' || line[0] == '#')
            continue;

        strcpy(key, line);
        val = data_cfg_value(key);
        if (!val)
            continue;

        if (!data_stricmp(key, "MSG_PATH"))
            strncpy(g_paths.msg_path, val, MAX_PATHNAME - 1);
        else if (!data_stricmp(key, "USR_PATH"))
            strncpy(g_paths.usr_path, val, MAX_PATHNAME - 1);
        else if (!data_stricmp(key, "UD_PATH"))
            strncpy(g_paths.ud_path, val, MAX_PATHNAME - 1);
        else if (!data_stricmp(key, "LOG_PATH"))
            strncpy(g_paths.log_path, val, MAX_PATHNAME - 1);
        else if (!data_strnicmp(key, "UPDN_PATH", 9))
        {
            sec = atoi(key + 9);
            if (sec >= 0 && sec < NUM_SECT)
                strncpy(g_paths.updn_path[sec], val, MAX_PATHNAME - 1);
        }
    }

    fclose(fp);
    data_set_default_paths();
    return 1;
}

int load_cfginfo(fname)
char *fname;
{
    FILE *fp;
    long len;

    data_zero_cfg();

    fp = fopen(fname, "rb");
    if (!fp)
    {
        (void)save_cfginfo(fname);
        return 1;
    }

    fseek(fp, 0L, SEEK_END);
    len = ftell(fp);
    fseek(fp, 0L, SEEK_SET);

    if (len < (long)sizeof(CFGINFO))
    {
        fclose(fp);
        (void)save_cfginfo(fname);
        return 1;
    }

    if (fread(&g_cfg, sizeof(g_cfg), 1, fp) != 1)
    {
        fclose(fp);
        data_zero_cfg();
        return 0;
    }

    fclose(fp);
    return 1;
}

int save_cfginfo(fname)
char *fname;
{
    FILE *fp;

    fp = fopen(fname, "wb");
    if (!fp)
        return 0;

    if (fwrite(&g_cfg, sizeof(g_cfg), 1, fp) != 1)
    {
        fclose(fp);
        return 0;
    }

    fclose(fp);
    return 1;
}

int open_main_datafiles(void)
{
    char path[MAX_PATHNAME * 2];

    data_make_user_path(path, "USERDESC.DAT");
    if (!data_ensure_header_file(path))
        return 0;
    g_usrfp = fopen(path, "r+b");
    if (!g_usrfp)
        return 0;

    data_make_data_path(path, "MSGHEAD.DAT");
    if (!data_ensure_header_file(path))
        return 0;
    g_msgfp = fopen(path, "r+b");
    if (!g_msgfp)
        return 0;

    data_make_data_path(path, "MSGTEXT.DAT");
    if (!data_ensure_header_file(path))
        return 0;
    g_txtfp = fopen(path, "r+b");
    if (!g_txtfp)
        return 0;

    data_make_ud_path(path, "UDHEAD.DAT");
    if (!data_ensure_header_file(path))
        return 0;
    g_udfp = fopen(path, "r+b");
    if (!g_udfp)
        return 0;

    data_make_log_path(path, "CALLER.DAT");
    if (!data_ensure_header_file(path))
        return 0;
    g_logfp = fopen(path, "r+b");
    if (!g_logfp)
        return 0;

    return 1;
}

void close_main_datafiles(void)
{
    if (g_usrfp) { fclose(g_usrfp); g_usrfp = (FILE *)0; }
    if (g_msgfp) { fclose(g_msgfp); g_msgfp = (FILE *)0; }
    if (g_txtfp) { fclose(g_txtfp); g_txtfp = (FILE *)0; }
    if (g_udfp)  { fclose(g_udfp);  g_udfp  = (FILE *)0; }
    if (g_logfp) { fclose(g_logfp); g_logfp = (FILE *)0; }
}

/* ------------------------------------------------------------ */
/* text/date/time helpers                                       */
/* ------------------------------------------------------------ */

void data_trim_crlf(s)
char *s;
{
    char *p;

    if (!s)
        return;

    p = strchr(s, '\n');
    if (p) *p = 0;
    p = strchr(s, '\r');
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
        sprintf(d, "%02d/%02d/%04d",
                tmv->tm_mday,
                tmv->tm_mon + 1,
                tmv->tm_year + 1900);

    if (t)
        sprintf(t, "%02d:%02d:%02d",
                tmv->tm_hour,
                tmv->tm_min,
                tmv->tm_sec);
}

void data_prompt_line(prompt, buf, buflen)
char *prompt;
char *buf;
int buflen;
{
    if (prompt)
        fputs(prompt, stdout);

    if (fgets(buf, buflen, stdin))
        data_trim_crlf(buf);
    else if (buflen > 0)
        buf[0] = 0;
}

ushort data_pack_date_now(void)
{
    time_t now;
    struct tm *tmv;
    ushort y, m, d;

    now = time((time_t *)0);
    tmv = localtime(&now);

    y = (ushort)((tmv->tm_year + 1900) - 1980);
    m = (ushort)(tmv->tm_mon + 1);
    d = (ushort)tmv->tm_mday;

    return (ushort)((y << 9) | (m << 5) | d);
}

ushort data_pack_time_now(void)
{
    time_t now;
    struct tm *tmv;
    ushort h, m, s;

    now = time((time_t *)0);
    tmv = localtime(&now);

    h = (ushort)tmv->tm_hour;
    m = (ushort)tmv->tm_min;
    s = (ushort)(tmv->tm_sec / 2);

    return (ushort)((h << 11) | (m << 5) | s);
}

void data_unpack_date(p, out)
ushort p;
char *out;
{
    int y, m, d;

    data_unpack_date_time_parts(p, &y, &m, &d);
    sprintf(out, "%02d/%02d/%04d", d, m, y);
}

void data_unpack_time(p, out)
ushort p;
char *out;
{
    int h, m, s;

    data_unpack_clock_parts(p, &h, &m, &s);
    sprintf(out, "%02d:%02d:%02d", h, m, s);
}

/* ------------------------------------------------------------ */
/* compares                                                     */
/* ------------------------------------------------------------ */

int data_user_match(a, b)
char *a;
char *b;
{
    return data_stricmp(a, b) == 0;
}

int data_name_match(a, b, maxlen)
char *a;
char *b;
int maxlen;
{
    return data_strnicmp(a, b, maxlen) == 0;
}

int data_cat_match(a, b)
char *a;
char *b;
{
    return data_stricmp(a, b) == 0;
}

/* ------------------------------------------------------------ */
/* counts                                                       */
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
    long i, n, high;
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

/* ------------------------------------------------------------ */
/* finders                                                      */
/* ------------------------------------------------------------ */

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

/* ------------------------------------------------------------ */
/* record read/write wrappers                                   */
/* ------------------------------------------------------------ */

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
/* message text chains                                          */
/* ------------------------------------------------------------ */

int data_store_message_text(body, firstptr)
char *body;
ushort *firstptr;
{
    long start_rec;
    long recno;
    long newrec;
    MSGTEXT t;
    int len;
    int pos;

    if (!body || !firstptr)
        return 0;

    start_rec = data_msgtext_count();
    if (start_rec < 0L)
        return 0;

    *firstptr = (ushort)start_rec;

    len = strlen(body);
    pos = 0;
    recno = start_rec;

    while (pos < len)
    {
        int chunk;

        memset(&t, 0, sizeof(t));
        chunk = len - pos;
        if (chunk > (TEXT_LEN - 1))
            chunk = TEXT_LEN - 1;

        memcpy(t.text, body + pos, chunk);
        t.text[chunk] = 0;

        newrec = isam_append_blank_record(g_txtfp, MSGTEXT_SIZE);
        if (newrec < 0L || newrec != recno)
            return 0;

        if (!data_write_msgtext(recno, &t))
            return 0;

        pos += chunk;
        recno++;
    }

    memset(&t, 0, sizeof(t));
    newrec = isam_append_blank_record(g_txtfp, MSGTEXT_SIZE);
    if (newrec < 0L || newrec != recno)
        return 0;

    if (!data_write_msgtext(recno, &t))
        return 0;

    return 1;
}

int data_load_message_text(firstptr, out, outlen)
ushort firstptr;
char *out;
int outlen;
{
    long recno;
    int used;
    MSGTEXT t;

    if (!out || outlen <= 0)
        return 0;

    out[0] = 0;

    if (firstptr == 0)
        return 1;

    recno = (long)firstptr;
    used = 0;

    while (data_read_msgtext(recno, &t))
    {
        int chunk;

        if (!t.text[0])
            break;

        chunk = strlen(t.text);
        if ((used + chunk + 1) >= outlen)
            chunk = outlen - used - 1;

        if (chunk <= 0)
            break;

        memcpy(out + used, t.text, chunk);
        used += chunk;
        out[used] = 0;

        recno++;
    }

    return 1;
}

void data_zero_message_chain(firstptr)
ushort firstptr;
{
    long recno;
    MSGTEXT t;
    int was_empty;

    if (firstptr == 0)
        return;

    recno = (long)firstptr;

    while (data_read_msgtext(recno, &t))
    {
        was_empty = (t.text[0] == 0);

        memset(&t, 0, sizeof(t));
        (void)data_write_msgtext(recno, &t);

        if (was_empty)
            break;

        recno++;
    }
}

/* ------------------------------------------------------------ */
/* path/disk helpers                                            */
/* ------------------------------------------------------------ */

void data_make_data_path(out, name)
char *out;
char *name;
{
    data_join_path(out, g_paths.msg_path, name);
}

void data_make_updn_path(out, section, name)
char *out;
int section;
char *name;
{
    if (section < 0 || section >= NUM_SECT)
        section = 0;

    data_join_path(out, g_paths.updn_path[section], name);
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