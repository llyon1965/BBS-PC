/* BBS.C
 *
 * BBS-PC ! 4.21
 *
 * Reconstructed and modernised source
 * derived from BBS-PC 4.20
 *
 * Runtime entry / startup / shutdown / login loop
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bbsdata.h"
#include "bbsfunc.h"

#ifndef BBS_VERSION
#define BBS_VERSION "BBS-PC ! 4.21"
#endif

static int g_running = 1;

/* ------------------------------------------------------------ */
/* local helpers                                                */
/* ------------------------------------------------------------ */

static void bbs_log_startup(void)
{
    char d[32], t[32];

    data_now_strings(d, t);
    printf("[%s %s] %s startup\n", d, t, BBS_VERSION);
}

static void bbs_log_shutdown(void)
{
    char d[32], t[32];

    data_now_strings(d, t);
    printf("[%s %s] %s shutdown\n", d, t, BBS_VERSION);
}

static void bbs_apply_startup_defaults(void)
{
    memset(&g_sess, 0, sizeof(g_sess));
    memset(&g_node, 0, sizeof(g_node));

    g_sess.node = 0;
    g_sess.local_login = 0;
    g_sess.logged_in = 0;
    g_sess.menu_set = 0;
    g_sess.expert = 0;
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
    (void)argc;
    (void)argv;

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
    modem_init();
    node_startup_init();

    bbs_banner();
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
    char d[32], t[32];

    data_now_strings(d, t);

    bbs_cls();
    puts("============================================================");
    puts(BBS_VERSION);
    puts("Reconstructed and modernised source line");
    puts("Derived from BBS-PC 4.20");
    puts("============================================================");
    printf("Date: %s\n", d);
    printf("Time: %s\n", t);
    puts("");
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
/* main loop                                                    */
/* ------------------------------------------------------------ */

void bbs_main_loop(void)
{
    while (g_running)
    {
        bbs_login_sequence();

        if (!g_sess.logged_in)
            break;

        if (!menu_push("MAIN"))
        {
            puts("Unable to load MAIN menu");
            bbs_logout_sequence();
            break;
        }

        while (g_sess.logged_in)
        {
            MENUFILE *m;

            m = menu_current();
            if (!m)
                break;

            menu_display(m);
            if (!menu_execute(m))
                break;
        }

        if (g_sess.logged_in)
            bbs_logout_sequence();
    }
}

/* ------------------------------------------------------------ */
/* login / logout sequence                                      */
/* ------------------------------------------------------------ */

void bbs_login_sequence(void)
{
    int ok;

    modem_begin_session();

    ok = login_user();
    if (!ok)
    {
        modem_end_session();
        return;
    }

    g_sess.logged_in = 1;

    term_apply_user(&g_sess.user);
    term_start_session();
    node_session_begin();

    printf("%s ready.\n", BBS_VERSION);
}

void bbs_logout_sequence(void)
{
    if (!g_sess.logged_in)
        return;

    logout_user();
    node_session_end();
    term_end_session();
    modem_end_session();

    g_sess.logged_in = 0;
    bbs_goodbye();
}