/* BBS.C
 *
 * BBS-PC ! 4.21
 *
 * Reconstructed and modernised source
 * derived from BBS-PC 4.20
 *
 * Runtime entry / startup / shutdown / waiting-for-call loop
 *
 * Command-line switches:
 *   -C      Ignore COM/FOSSIL ports and run local-only
 *   -N:x    Select node number x (default = 1)
 *
 * Ownership in this module:
 * - overall runtime flow
 * - startup / shutdown
 * - waiting-for-call screen
 * - waiting loop and local sysop hotkey handling
 * - connection detection handoff into login
 * - active session loop
 * - local-only caller status bar
 * - command-line switch handling
 *
 * Notes:
 * - The waiting-for-call screen is the first screen shown on startup and
 *   the screen returned to after logout.
 * - During an active remote session, normal BBS output is mirrored through
 *   the normal runtime path, but the top caller status bar is drawn only
 *   on the local screen.
 * - Node number is stored internally as 0-based, but displayed as 1-based.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <conio.h>
#include <dos.h>

#include "bbsdata.h"
#include "bbsfunc.h"

#ifndef BBS_VERSION
#define BBS_VERSION "BBS-PC ! 4.21"
#endif

#ifndef WAIT_MENU_COUNT
#define WAIT_MENU_COUNT 6
#endif

static int g_running = 1;
static int g_wait_menu_sel = 0;
static int g_ignore_com = 0;
static int g_selected_node = 0;     /* 0-based internally */

/* ------------------------------------------------------------ */
/* local helpers                                                */
/* ------------------------------------------------------------ */

static void local_cls(void)
{
    union REGS in, out;

    in.h.ah = 0x00;
    in.h.al = 0x03;
    int86(0x10, &in, &out);
}

static void local_goto_xy(x, y)
int x;
int y;
{
    union REGS in, out;

    in.h.ah = 0x02;
    in.h.bh = 0x00;
    in.h.dh = (unsigned char)(y > 0 ? y - 1 : 0);
    in.h.dl = (unsigned char)(x > 0 ? x - 1 : 0);
    int86(0x10, &in, &out);
}

static void local_puts_xy(x, y, s)
int x;
int y;
char *s;
{
    local_goto_xy(x, y);
    fputs(s, stdout);
}

static void local_put_blank_line(y)
int y;
{
    local_goto_xy(1, y);
    printf("%-79s", "");
}

static void local_draw_box(x1, y1, x2, y2)
int x1;
int y1;
int x2;
int y2;
{
    int x, y;

    local_goto_xy(x1, y1); putchar('+');
    for (x = x1 + 1; x < x2; x++) putchar('-');
    putchar('+');

    for (y = y1 + 1; y < y2; y++)
    {
        local_goto_xy(x1, y); putchar('|');
        local_goto_xy(x2, y); putchar('|');
    }

    local_goto_xy(x1, y2); putchar('+');
    for (x = x1 + 1; x < x2; x++) putchar('-');
    putchar('+');
}

static void local_print_centre(y, s)
int y;
char *s;
{
    int x;
    int len;

    len = strlen(s);
    x = 40 - (len / 2);
    if (x < 1)
        x = 1;

    local_puts_xy(x, y, s);
}

static void bbs_parse_args(argc, argv)
int argc;
char *argv[];
{
    int i;
    char *p;

    g_ignore_com = 0;
    g_selected_node = 0;

    for (i = 1; i < argc; i++)
    {
        p = argv[i];

        if (!p || (*p != '-' && *p != '/'))
            continue;

        p++;

        if (*p == 'C' || *p == 'c')
        {
            g_ignore_com = 1;
            continue;
        }

        if (*p == 'N' || *p == 'n')
        {
            int n = 0;

            p++;
            if (*p == ':')
                p++;

            n = atoi(p);
            if (n < 1)
                n = 1;
            if (n > 99)
                n = 99;

            g_selected_node = n - 1;
            continue;
        }
    }
}

static void bbs_log_startup(void)
{
    char d[32], t[32];

    data_now_strings(d, t);
    printf("[%s %s] %s startup (Node %d%s)\n",
           d, t, BBS_VERSION, g_selected_node + 1,
           g_ignore_com ? ", -C" : "");
}

static void bbs_log_shutdown(void)
{
    char d[32], t[32];

    data_now_strings(d, t);
    printf("[%s %s] %s shutdown (Node %d)\n",
           d, t, BBS_VERSION, g_selected_node + 1);
}

static void bbs_apply_startup_defaults(void)
{
    memset(&g_sess, 0, sizeof(g_sess));
    memset(&g_node, 0, sizeof(g_node));

    g_sess.node = g_selected_node;
    g_sess.local_login = 0;
    g_sess.logged_in = 0;
    g_sess.menu_set = 0;
    g_sess.expert = 0;
}

static char *bbs_wait_menu_text(idx)
int idx;
{
    switch (idx)
    {
        case 0: return "Exit program to DOS";
        case 1: return "Toggle Chat option";
        case 2: return "Local BBS Log-in";
        case 3: return "Upload/download module";
        case 4: return "Terminal module";
        case 5: return "Maintenance module";
    }

    return "";
}

static void bbs_wait_draw_lightbar_item(y, selected, text)
int y;
int selected;
char *text;
{
    char buf[64];

    if (selected)
        sprintf(buf, "> %-28s <", text);
    else
        sprintf(buf, "  %-28s  ", text);

    local_print_centre(y, buf);
}

static void bbs_wait_get_fossil_line1(out)
char *out;
{
    if (g_ignore_com)
    {
        strcpy(out, "FOSSIL: Disabled (-C)   Local-only mode");
        return;
    }

    if (fossil_driver_installed())
        strcpy(out, "FOSSIL: Active   Driver: Compatible");
    else
        strcpy(out, "FOSSIL: Not detected   Local console available");
}

static void bbs_wait_get_fossil_line2(out)
char *out;
{
    int com_port;

    com_port = g_sess.node + 1;
    if (com_port < 1)
        com_port = 1;
    if (com_port > 2)
        com_port = 2;

    if (g_ignore_com)
    {
        strcpy(out, "Port: Disabled   Carrier: N/A   Session: Local");
        return;
    }

    if (fossil_driver_installed())
        sprintf(out, "Port: COM%d   Carrier: %s   Session: Waiting",
                com_port, fossil_carrier() ? "Present" : "Waiting");
    else
        sprintf(out, "Port: COM%d   Carrier: N/A   Session: Waiting", com_port);
}

static void bbs_wait_get_dir0_text(out, outlen)
char *out;
int outlen;
{
    if (data_file_count() <= 0L)
        strncpy(out, "Empty", outlen - 1);
    else
        sprintf(out, "%ld file(s)", data_file_count());

    out[outlen - 1] = 0;
}

static void bbs_waiting_screen_update_stats(void)
{
    char d[32], t[32];
    char fossil1[80], fossil2[80];
    char dir0[40];

    data_now_strings(d, t);
    bbs_wait_get_fossil_line1(fossil1);
    bbs_wait_get_fossil_line2(fossil2);
    bbs_wait_get_dir0_text(dir0, sizeof(dir0));

    local_print_centre(3, fossil1);
    local_print_centre(4, fossil2);

    local_goto_xy(21, 20);
    printf("High: %-6ld", data_highest_msg_number());

    local_goto_xy(21, 21);
    printf("Msgs: %-6ld", data_msg_count());

    local_goto_xy(39, 20);
    printf("Calls: %-8ld", data_caller_count());

    local_goto_xy(39, 21);
    printf("Users: %-8ld", data_user_count());

    local_goto_xy(58, 20);
    printf("Dir/0: %-14s", dir0);

    local_goto_xy(58, 21);
    printf("Files: %-14ld", data_file_count());

    local_print_centre(25, t);
    local_print_centre(26, d);

    fflush(stdout);
}

static void bbs_draw_waiting_screen(highlight)
int highlight;
{
    int i;
    char title[64];

    local_cls();

    sprintf(title, "%s Node #%d", BBS_VERSION, g_sess.node + 1);

    bbs_waiting_screen_update_stats();

    local_print_centre(6, title);
    local_draw_box(19, 7, 70, 23);

    for (i = 0; i < WAIT_MENU_COUNT; i++)
        bbs_wait_draw_lightbar_item(9 + i, (i == highlight),
                                    bbs_wait_menu_text(i));

    local_goto_xy(37, 16);
    printf("Chat: %s", g_node.wantchat ? "ON " : "OFF");

    local_goto_xy(35, 17);
    if (g_ignore_com)
        printf("Port: Disabled");
    else
        printf("Port: COM%d", ((g_sess.node + 1) > 2) ? 2 : (g_sess.node + 1));

    fflush(stdout);
}

static int bbs_check_for_incoming_call(void)
{
    if (g_ignore_com)
        return 0;

    if (g_sess.local_login)
        return 0;

    return term_carrier();
}

static int bbs_wait_key_available(void)
{
    return kbhit();
}

static int bbs_wait_read_key(void)
{
    int ch;

    ch = getch();

    if (ch == 0 || ch == 0xE0)
    {
        ch = getch();

        switch (ch)
        {
            case 72:
                return 'U';
            case 80:
                return 'D';
            default:
                return -1;
        }
    }

    if (ch == 13)
        return 13;

    if (ch == 27)
        return 27;

    return ch;
}

static void bbs_waiting_screen_handle_local_key(key)
int key;
{
    switch (key)
    {
        case 'U':
            if (g_wait_menu_sel > 0)
                g_wait_menu_sel--;
            else
                g_wait_menu_sel = WAIT_MENU_COUNT - 1;
            break;

        case 'D':
            if (g_wait_menu_sel < (WAIT_MENU_COUNT - 1))
                g_wait_menu_sel++;
            else
                g_wait_menu_sel = 0;
            break;

        case 13:
            switch (g_wait_menu_sel)
            {
                case 0:
                    g_running = 0;
                    break;

                case 1:
                    g_node.wantchat = g_node.wantchat ? 0 : 1;
                    node_set_chat(g_node.wantchat);
                    break;

                case 2:
                    if (login_local())
                    {
                        g_sess.logged_in = 1;
                        term_apply_user(&g_sess.user);
                        term_start_session();
                        node_session_begin();
                    }
                    break;

                case 3:
                    modem_direct_upload();
                    break;

                case 4:
                    term_show_current();
                    bbs_pause();
                    break;

                case 5:
                    do_system_defaults();
                    break;
            }
            break;

        case 27:
            g_running = 0;
            break;
    }
}

static void bbs_clear_local_status_bar(void)
{
    local_put_blank_line(1);
    local_put_blank_line(2);
    fflush(stdout);
}

static void bbs_draw_local_status_bar(void)
{
    char line1[128];
    char line2[128];
    char name[NAME_LEN + 2];
    char location[40];
    char speed[16];

    strncpy(name, g_sess.user.name, NAME_LEN + 1);
    name[NAME_LEN + 1] = 0;

    if (g_sess.user.city[0])
        strncpy(location, g_sess.user.city, sizeof(location) - 1);
    else
        strcpy(location, "Unknown");
    location[sizeof(location) - 1] = 0;

    sprintf(speed, "%d", g_modem.baud > 0 ? g_modem.baud : 0);

    sprintf(line1,
        " %-18.18s %-16.16s Calls:%-4u Sec:%-3u Baud:%-6s ",
        name,
        location,
        (unsigned)g_sess.user.calls,
        (unsigned)g_sess.user.seclevel,
        speed);

    sprintf(line2,
        " Paged:%s Msgs left:%-4u Uploads:%-4u ",
        g_sess.page_sysop ? "Yes" : "No ",
        (unsigned)g_sess.msgs_left,
        (unsigned)g_sess.uploads);

    local_goto_xy(1, 1);
    printf("%-79s", line1);
    local_goto_xy(1, 2);
    printf("%-79s", line2);
    fflush(stdout);
}

static void bbs_update_local_status_bar(void)
{
    bbs_draw_local_status_bar();
}

static int bbs_session_ready_from_login(ok)
int ok;
{
    if (!ok)
        return 0;

    g_sess.logged_in = 1;
    term_apply_user(&g_sess.user);
    term_start_session();
    node_session_begin();
    return 1;
}

/* ------------------------------------------------------------ */
/* entry                                                        */
/* ------------------------------------------------------------ */

int main(argc, argv)
int argc;
char *argv[];
{
    bbs_startup(argc, argv);
    bbs_run();
    bbs_shutdown();
    return 0;
}

void bbs_run(void)
{
    bbs_main_loop();
}

/* ------------------------------------------------------------ */
/* startup / shutdown                                           */
/* ------------------------------------------------------------ */

void bbs_startup(argc, argv)
int argc;
char *argv[];
{
    bbs_parse_args(argc, argv);
    bbs_apply_startup_defaults();
    bbs_log_startup();

    if (!load_bbs_paths("BBSPATHS.CFG"))
    {
        puts("Unable to load BBSPATHS.CFG");
        exit(1);
    }

    if (!load_cfginfo("CFGINFO.DAT"))
    {
        puts("Unable to load CFGINFO.DAT");
        exit(1);
    }

    if (!open_main_datafiles())
    {
        puts("Unable to open main datafiles");
        exit(1);
    }

    term_init_defaults();

    if (g_ignore_com)
    {
        g_sess.local_login = 1;
        printf("COM ports disabled (-C switch)\n");
    }
    else
    {
        modem_init();
    }

    node_startup_init();
}

void bbs_shutdown(void)
{
    if (g_sess.logged_in)
        bbs_logout_sequence();

    close_main_datafiles();
    bbs_log_shutdown();
}

/* ------------------------------------------------------------ */
/* banner / ui                                                  */
/* ------------------------------------------------------------ */

void bbs_banner(void)
{
    bbs_draw_waiting_screen(g_wait_menu_sel);
}

void bbs_goodbye(void)
{
    puts("");
    puts("Thank you for calling.");
    puts(BBS_VERSION);
    puts("");
}

void bbs_pause(void)
{
    term_pause();
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
    char line[8];
    data_prompt_line("Press ENTER: ", line, sizeof(line));
}

void bbs_print_center(s)
char *s;
{
    term_print_center(s);
}

void bbs_print_file(fname)
char *fname;
{
    (void)term_type_file(fname, 0, 1);
}

void bbs_print_file_nostop(fname)
char *fname;
{
    (void)term_type_file(fname, 0, 0);
}

void bbs_type_file(fname, cls_flag, stop_flag)
char *fname;
int cls_flag;
int stop_flag;
{
    (void)term_type_file(fname, cls_flag, stop_flag);
}

/* ------------------------------------------------------------ */
/* waiting-for-call / main loop                                 */
/* ------------------------------------------------------------ */

void bbs_waiting_for_call(void)
{
    int key;
    int login_ok;

    g_sess.logged_in = 0;
    bbs_draw_waiting_screen(g_wait_menu_sel);

    for (;;)
    {
        if (!g_running)
            return;

        bbs_waiting_screen_update_stats();

        if (bbs_wait_key_available())
        {
            key = bbs_wait_read_key();
            bbs_waiting_screen_handle_local_key(key);

            if (g_sess.logged_in)
                return;

            bbs_draw_waiting_screen(g_wait_menu_sel);
        }

        if (bbs_check_for_incoming_call())
        {
            modem_begin_session();
            login_ok = login_user();

            if (bbs_session_ready_from_login(login_ok))
                return;

            modem_end_session();
            bbs_draw_waiting_screen(g_wait_menu_sel);
        }

        delay(50);
    }
}

void bbs_main_loop(void)
{
    while (g_running)
    {
        bbs_waiting_for_call();

        if (!g_running)
            break;

        if (!g_sess.logged_in)
            continue;

        bbs_session_loop();

        if (g_sess.logged_in)
            bbs_logout_sequence();
    }
}

void bbs_session_loop(void)
{
    MENUFILE *m;

    if (!menu_push("MAIN"))
    {
        puts("Unable to load MAIN menu");
        return;
    }

    bbs_draw_local_status_bar();

    while (g_sess.logged_in)
    {
        m = menu_current();
        if (!m)
            break;

        bbs_update_local_status_bar();
        menu_display(m);
        if (!menu_execute(m))
            break;
    }
}

/* ------------------------------------------------------------ */
/* login / logout sequence                                      */
/* ------------------------------------------------------------ */

void bbs_login_sequence(void)
{
    int ok;

    if (g_ignore_com)
        return;

    modem_begin_session();
    ok = login_user();
    (void)bbs_session_ready_from_login(ok);

    if (!g_sess.logged_in)
        modem_end_session();
}

void bbs_logout_sequence(void)
{
    if (!g_sess.logged_in)
        return;

    logout_user();
    node_session_end();
    term_end_session();

    if (!g_ignore_com)
        modem_end_session();

    g_sess.logged_in = 0;
    bbs_clear_local_status_bar();
}