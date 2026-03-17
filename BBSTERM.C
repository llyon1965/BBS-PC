/* BBSTERM.C
 *
 * First-pass BBS-PC 4.20 terminal/modem I/O module
 *
 * Implements:
 * - local console terminal I/O
 * - terminal parameter handling from CFGINFO.DAT / user record
 * - paging support
 * - simple line input
 * - basic output translation for linefeeds / nuls
 * - protocol-name helpers
 *
 * Notes:
 * - This is a safe reconstruction scaffold.
 * - Real modem/FOSSIL/UART handling is not yet implemented.
 * - Current build targets local-console operation first.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "bbsdata.h"
#include "bbsfunc.h"

#define TERM_INBUF 160

typedef struct {
    int local_mode;
    int page_line;
    int page_len;
    int width;
    int linefeed;
    int nuls;
    int protocol;
    int echo;
    int stopped;
} TERMSTATE;

static TERMSTATE g_term;

/* ------------------------------------------------------------ */

static void trim_crlf_local(s)
char *s;
{
    char *p;

    p = strchr(s, '\r');
    if (p) *p = 0;

    p = strchr(s, '\n');
    if (p) *p = 0;
}

static void put_nuls(n)
int n;
{
    while (n-- > 0)
        putchar('\0');
}

/* ------------------------------------------------------------ */
/* initialization                                               */
/* ------------------------------------------------------------ */

void term_init_defaults(void)
{
    memset(&g_term, 0, sizeof(g_term));

    g_term.local_mode = 1;
    g_term.page_line  = 0;
    g_term.page_len   = 24;
    g_term.width      = 80;
    g_term.linefeed   = 0;
    g_term.nuls       = 0;
    g_term.protocol   = 0;
    g_term.echo       = 1;
    g_term.stopped    = 0;
}

void term_apply_user(u)
USRDESC *u;
{
    if (!u)
        return;

    g_term.page_len = u->length ? u->length : 24;
    g_term.width    = u->width ? u->width : 80;
    g_term.linefeed = u->linefeed ? 1 : 0;
    g_term.nuls     = u->nuls;
    g_term.protocol = u->protocol;
}

void term_apply_cfg_terminal(termno)
int termno;
{
    if (termno < 0 || termno >= NUM_TERM)
        termno = 0;

    if (g_cfg.trmnl[termno].page[0])
        g_term.page_len = g_cfg.trmnl[termno].page[0];

    g_term.linefeed = g_cfg.trmnl[termno].linefeed ? 1 : 0;
    g_term.nuls = g_cfg.trmnl[termno].nuls;
    g_term.protocol = g_cfg.trmnl[termno].protocol;
}

void term_start_session(void)
{
    term_init_defaults();
    term_apply_cfg_terminal(g_sess.user.term);
    term_apply_user(&g_sess.user);
}

void term_end_session(void)
{
    fflush(stdout);
}

/* ------------------------------------------------------------ */
/* output                                                       */
/* ------------------------------------------------------------ */

void term_putc(ch)
int ch;
{
    putchar(ch);

    if (ch == '\n')
    {
        if (g_term.linefeed)
            putchar('\r');

        put_nuls(g_term.nuls);
        g_term.page_line++;
    }
}

void term_puts(s)
char *s;
{
    while (*s)
        term_putc((unsigned char)*s++);
}

void term_newline(void)
{
    term_putc('\n');
}

void term_crlf(void)
{
    putchar('\r');
    putchar('\n');
    put_nuls(g_term.nuls);
    g_term.page_line++;
}

void term_beep(void)
{
    putchar('\a');
}

void term_cls(void)
{
    int i;

    /* first-pass local console clear */
    for (i = 0; i < 25; i++)
        puts("");

    g_term.page_line = 0;
}

void term_reset_pager(void)
{
    g_term.page_line = 0;
}

void term_pause(void)
{
    char line[8];

    term_crlf();
    fputs("Press [RETURN]", stdout);
    fflush(stdout);

    fgets(line, sizeof(line), stdin);
    term_crlf();
    g_term.page_line = 0;
}

int term_more_needed(void)
{
    if (g_term.page_len <= 0)
        return 0;

    return (g_term.page_line >= (g_term.page_len - 1));
}

void term_page_check(void)
{
    if (term_more_needed())
        term_pause();
}

void term_type_text(s, stop_flag)
char *s;
int stop_flag;
{
    while (*s)
    {
        term_putc((unsigned char)*s++);

        if (stop_flag && *s && term_more_needed())
            term_pause();
    }
}

void term_print_center(s)
char *s;
{
    int len, pad, i;

    len = (int)strlen(s);
    if (len >= g_term.width)
    {
        term_puts(s);
        term_newline();
        return;
    }

    pad = (g_term.width - len) / 2;
    for (i = 0; i < pad; i++)
        term_putc(' ');

    term_puts(s);
    term_newline();
}

void term_print_status_line(s)
char *s;
{
    term_puts(s);
    term_newline();
}

/* ------------------------------------------------------------ */
/* input                                                        */
/* ------------------------------------------------------------ */

int term_getc(void)
{
    return getchar();
}

int term_getkey(void)
{
    int ch;

    ch = getchar();
    if (ch == '\r')
        ch = '\n';

    return ch;
}

void term_getline(prompt, out, len)
char *prompt;
char *out;
int len;
{
    if (prompt && *prompt)
        fputs(prompt, stdout);

    if (fgets(out, len, stdin) == NULL)
        out[0] = 0;

    trim_crlf_local(out);
}

void term_getline_upper(prompt, out, len)
char *prompt;
char *out;
int len;
{
    int i;

    term_getline(prompt, out, len);

    for (i = 0; out[i]; i++)
        out[i] = (char)toupper((unsigned char)out[i]);
}

void term_getline_hidden(prompt, out, len)
char *prompt;
char *out;
int len;
{
    /* first-pass fallback: visible input */
    term_getline(prompt, out, len);
}

int term_yesno(prompt, def_yes)
char *prompt;
int def_yes;
{
    char line[8];

    term_getline(prompt, line, sizeof(line));

    if (!line[0])
        return def_yes ? 1 : 0;

    return (line[0] == 'Y' || line[0] == 'y');
}

/* ------------------------------------------------------------ */
/* terminal/user helpers                                        */
/* ------------------------------------------------------------ */

char *term_protocol_name(proto)
int proto;
{
    switch (proto)
    {
        case 0:  return "Text";
        case 1:  return "XMODEM";
        case 2:  return "XMODEM-CRC";
        case 3:  return "YMODEM";
        case 4:  return "YMODEM-Batch";
        case 5:  return "Kermit";
        case 6:  return "ZMODEM";
        default: return "Unknown";
    }
}

char *term_type_name(termno)
int termno;
{
    if (termno >= 0 && termno < NUM_TERM && g_cfg.trmnl[termno].name[0])
        return g_cfg.trmnl[termno].name;

    return "ASCII Terminal";
}

void term_show_current(void)
{
    term_newline();
    printf("Terminal: %s\n", term_type_name(g_sess.user.term));
    printf("Protocol: %s\n", term_protocol_name(g_term.protocol));
    printf("Width: %d\n", g_term.width);
    printf("Page length: %d\n", g_term.page_len);
    printf("Linefeeds: %s\n", g_term.linefeed ? "Yes" : "No");
    printf("NULS: %d\n", g_term.nuls);
    term_newline();
}

void term_set_protocol(proto)
int proto;
{
    g_term.protocol = proto;
    g_sess.user.protocol = (byte)proto;
    user_save_current();
}

void term_set_page_length(n)
int n;
{
    if (n < 0)
        n = 0;

    g_term.page_len = n;
    g_sess.user.length = (byte)n;
    user_save_current();
}

void term_set_width(n)
int n;
{
    if (n <= 0)
        n = 80;

    g_term.width = n;
    g_sess.user.width = (byte)n;
    user_save_current();
}

void term_set_linefeeds(flag)
int flag;
{
    g_term.linefeed = flag ? 1 : 0;
    g_sess.user.linefeed = flag ? 1 : 0;
    user_save_current();
}

void term_set_nuls(n)
int n;
{
    if (n < 0)
        n = 0;

    g_term.nuls = n;
    g_sess.user.nuls = (byte)n;
    user_save_current();
}

/* ------------------------------------------------------------ */
/* file typing helpers                                          */
/* ------------------------------------------------------------ */

int term_type_file(fname, cls_flag, stop_flag)
char *fname;
int cls_flag;
int stop_flag;
{
    FILE *fp;
    char line[TERM_INBUF];

    if (cls_flag)
        term_cls();

    fp = fopen(fname, "rt");
    if (!fp)
        return 0;

    term_reset_pager();

    while (fgets(line, sizeof(line), fp))
    {
        term_type_text(line, 0);

        if (stop_flag)
            term_page_check();
    }

    fclose(fp);
    return 1;
}

/* ------------------------------------------------------------ */
/* modem / remote placeholders                                  */
/* ------------------------------------------------------------ */

int term_carrier(void)
{
    /* local-first reconstruction */
    return 1;
}

void term_drop_dtr(void)
{
    /* not yet implemented */
}

void term_toggle_chat(flag)
int flag;
{
    g_sess.sysop_chat = flag ? 1 : 0;
    node_set_chat(g_sess.sysop_chat);
}

void term_switch_8n1(label)
char *label;
{
    printf("Switching to 8N1 for %s\n", label ? label : "transfer");
}

void term_restore_mode(void)
{
    /* placeholder */
}

/* ------------------------------------------------------------ */
/* direct convenience wrappers expected by higher layers        */
/* ------------------------------------------------------------ */

int type_text_file(fname, pause_flag)
char *fname;
int pause_flag;
{
    int ok;

    ok = term_type_file(fname, 0, pause_flag);
    if (!ok)
        printf("Can't open %s\n", fname);

    return ok;
}

int cls_type_text_file(fname, pause_flag)
char *fname;
int pause_flag;
{
    int ok;

    ok = term_type_file(fname, 1, pause_flag);
    if (!ok)
        printf("Can't open %s\n", fname);

    return ok;
}

void bbs_cls(void)
{
    term_cls();
}

void bbs_pause(void)
{
    term_pause();
}

void bbs_beep(void)
{
    term_beep();
}

void bbs_press_enter(void)
{
    term_pause();
}

void bbs_print_center(s)
char *s;
{
    term_print_center(s);
}

void bbs_print_file(fname)
char *fname;
{
    term_type_file(fname, 0, 1);
}

void bbs_print_file_nostop(fname)
char *fname;
{
    term_type_file(fname, 0, 0);
}

void bbs_type_file(fname, cls_flag, stop_flag)
char *fname;
int cls_flag;
int stop_flag;
{
    term_type_file(fname, cls_flag, stop_flag);
}