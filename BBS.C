/* BBS.C
 *
 * BBS-PC! 4.21
 *
 * Main runtime loop
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "bbsdata.h"
#include "bbsfunc.h"

/* ------------------------------------------------------------ */
/* internal state                                               */
/* ------------------------------------------------------------ */

static int g_ignore_com = 0;

/* sysop runtime flags */
static int g_sysop_force_disconnect = 0;
static int g_sysop_force_chat = 0;
static int g_saved_seclevel = 0;
static int g_saved_seclevel_valid = 0;

/* ------------------------------------------------------------ */
/* forward                                                      */
/* ------------------------------------------------------------ */

static void bbs_parse_args(int argc, char *argv[]);
static int  bbs_current_com_port(void);
static int  bbs_wait_for_enter(int timeout_secs);
static void bbs_reset_session_overrides(void);
static int  bbs_session_ready_from_login(int ok);
static void bbs_handle_online_sysop_key(int key);
static void bbs_update_local_status_bar(void);

/* ------------------------------------------------------------ */
/* argument parsing                                             */
/* ------------------------------------------------------------ */

static void bbs_parse_args(argc, argv)
int argc;
char *argv[];
{
    int i;

    for (i = 1; i < argc; i++)
    {
        if (!strncmp(argv[i], "-C:", 3))
        {
            g_ignore_com = 1;
        }
        else if (!strncmp(argv[i], "-N:", 3))
        {
            g_sess.node = atoi(argv[i] + 3);
            if (g_sess.node <= 0)
                g_sess.node = 1;
        }
    }

    if (g_sess.node <= 0)
        g_sess.node = 1;
}

/* ------------------------------------------------------------ */
/* node → COM mapping                                           */
/* ------------------------------------------------------------ */

static int bbs_current_com_port(void)
{
    /* node 1 = COM1, node 2 = COM2 */
    if (g_sess.node == 2)
        return 2;

    return 1;
}

/* ------------------------------------------------------------ */
/* startup/shutdown                                             */
/* ------------------------------------------------------------ */

void bbs_startup(argc, argv)
int argc;
char *argv[];
{
    memset(&g_sess, 0, sizeof(g_sess));

    bbs_parse_args(argc, argv);

    if (!load_bbs_paths("BBSPATHS.CFG"))
        puts("Warning: BBSPATHS.CFG not found");

    if (!load_cfginfo("CFGINFO.DAT"))
        puts("Warning: CFGINFO.DAT not found");

    if (!open_main_datafiles())
    {
        puts("Unable to open data files");
        exit(1);
    }

    node_startup_init();

    term_init_defaults();

    if (!g_ignore_com)
    {
        g_modem.port = bbs_current_com_port();
        g_modem.baud = g_cfg.modem_baud;
        g_modem.parity = g_cfg.modem_parity;
        g_modem.data_bits = g_cfg.modem_data_bits;
        g_modem.stop_bits = g_cfg.modem_stop_bits;

        modem_init();
    }
}

void bbs_shutdown(void)
{
    node_session_end();
    term_end_session();
    close_main_datafiles();
}

/* ------------------------------------------------------------ */
/* waiting for call                                             */
/* ------------------------------------------------------------ */

void bbs_waiting_for_call(void)
{
    term_cls();
    bbs_print_center("BBS-PC! 4.21");
    puts("");
    puts("Waiting for call...");
}

/* ------------------------------------------------------------ */
/* enter key wait                                               */
/* ------------------------------------------------------------ */

static int bbs_wait_for_enter(timeout_secs)
int timeout_secs;
{
    time_t start;
    int ch;

    start = time((time_t *)0);

    puts("Press [ENTER]");

    for (;;)
    {
        if (term_carrier())
        {
            ch = getchar();
            if (ch == '\n' || ch == '\r')
                return 1;
        }

        if ((time((time_t *)0) - start) >= timeout_secs)
            return 0;
    }
}

/* ------------------------------------------------------------ */
/* session control                                              */
/* ------------------------------------------------------------ */

static void bbs_reset_session_overrides(void)
{
    g_sysop_force_disconnect = 0;
    g_sysop_force_chat = 0;
    g_saved_seclevel = 0;
    g_saved_seclevel_valid = 0;

    g_sess.bonus_minutes = 0;
    g_sess.ratio_off = 0;
    g_sess.last_time_warning = -1;
}

static int bbs_session_ready_from_login(ok)
int ok;
{
    if (!ok)
        return 0;

    bbs_reset_session_overrides();

    g_sess.logon_unix = (ulong)time((time_t *)0);

    if (g_sess.base_minutes_allowed < 0)
        g_sess.base_minutes_allowed = 0;

    g_sess.logged_in = 1;

    term_apply_user(&g_sess.user);
    term_start_session();
    node_session_begin();

    return 1;
}

/* ------------------------------------------------------------ */
/* sysop keys                                                   */
/* ------------------------------------------------------------ */

static void bbs_handle_online_sysop_key(key)
int key;
{
    switch (key)
    {
        case 1001: /* F1 - force chat */
            g_sysop_force_chat = 1;
            break;

        case 1002: /* F2 - disconnect */
            g_sysop_force_disconnect = 1;
            break;

        case 1003: /* F3 - +15 mins */
            g_sess.bonus_minutes += 15;
            break;

        case 1004: /* F4 - promote */
            if (!g_saved_seclevel_valid)
            {
                g_saved_seclevel = g_sess.user.seclevel;
                g_saved_seclevel_valid = 1;
            }
            g_sess.user.seclevel = 10;
            break;

        case 1005: /* F5 - toggle ratio */
            g_sess.ratio_off = g_sess.ratio_off ? 0 : 1;
            break;
    }
}

/* ------------------------------------------------------------ */
/* status bar                                                   */
/* ------------------------------------------------------------ */

static void bbs_update_local_status_bar(void)
{
    char rbuf[8];

    strcpy(rbuf, g_sess.ratio_off ? "Off" : "On ");

    printf("[User:%s] [Sec:%d] [Time+:%d] [Ratio:%s]\n",
           g_sess.user.name,
           g_sess.user.seclevel,
           g_sess.bonus_minutes,
           rbuf);
}

/* ------------------------------------------------------------ */
/* session loop                                                 */
/* ------------------------------------------------------------ */

void bbs_session_loop(void)
{
    MENUFILE *m;
    int key;

    while (g_sess.logged_in)
    {
        if (session_time_expired())
        {
            g_sysop_force_disconnect = 1;
            break;
        }

        if (session_idle_expired())
        {
            puts("Idle timeout");
            break;
        }

        if (g_sysop_force_disconnect)
            break;

        m = menu_current();
        if (!m)
            break;

        bbs_update_local_status_bar();

        menu_display(m);

        if (!menu_execute(m))
            break;

        if (g_sysop_force_chat)
        {
            do_chat_with_sysop();
            g_sysop_force_chat = 0;
        }
    }
}

/* ------------------------------------------------------------ */
/* main loop                                                    */
/* ------------------------------------------------------------ */

void bbs_main_loop(void)
{
    for (;;)
    {
        g_sess.logged_in = 0;

        bbs_waiting_for_call();

        if (!bbs_wait_for_enter(60))
            continue;

        bbs_login_sequence();

        if (!bbs_session_ready_from_login(g_sess.logged_in))
            continue;

        bbs_session_loop();

        bbs_logout_sequence();
    }
}

/* ------------------------------------------------------------ */
/* wrappers                                                     */
/* ------------------------------------------------------------ */

void bbs_run(void)
{
    bbs_main_loop();
}

void bbs_login_sequence(void)
{
    if (login_user())
        g_sess.logged_in = 1;
}

void bbs_logout_sequence(void)
{
    logout_user();
    term_end_session();
    node_session_end();
}

void bbs_banner(void)
{
    puts("BBS-PC! 4.21");
}

void bbs_goodbye(void)
{
    puts("Goodbye");
}

void bbs_pause(void)
{
    puts("Press ENTER...");
    getchar();
}

void bbs_cls(void)
{
    term_cls();
}

void bbs_beep(void)
{
    term_beep();
}

void bbs_press_enter(void)
{
    puts("Press ENTER...");
    getchar();
}

void bbs_print_center(s)
char *s;
{
    term_print_center(s);
}

void bbs_print_file(fname)
char *fname;
{
    term_type_file(fname, 1, 1);
}

void bbs_print_file_nostop(fname)
char *fname;
{
    term_type_file(fname, 1, 0);
}

void bbs_type_file(fname, cls_flag, stop_flag)
char *fname;
int cls_flag;
int stop_flag;
{
    term_type_file(fname, cls_flag, stop_flag);
}