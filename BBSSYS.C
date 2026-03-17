/* BBSSYS.C
 *
 * BBS-PC 4.20 SYSOP / runtime helper module
 *
 * Provides:
 * - sysop_password_prompt()
 * - do_time_on_system()
 * - do_chat_with_sysop()
 * - do_expert_toggle()
 * - bbs_goodbye()
 * - bbs_run()
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bbsdata.h"
#include "bbsfunc.h"

/* ------------------------------------------------------------ */

static void format_hhmm(mins, out)
int mins;
char *out;
{
    int h, m;

    if (mins < 0)
        mins = 0;

    h = mins / 60;
    m = mins % 60;

    sprintf(out, "%02d:%02d", h, m);
}

static int current_time_used(void)
{
    int used;

    used = g_sess.user.limit - g_sess.time_left;
    if (used < 0)
        used = 0;

    return used;
}

/* ------------------------------------------------------------ */
/* shared SYSOP helper                                          */
/* ------------------------------------------------------------ */

int sysop_password_prompt(void)
{
    char line[PASS_LEN + 2];

    if (g_sess.user.priv >= 100)
        return 1;

    data_prompt_hidden("SYSOP password? ", line, sizeof(line));

    if (!strcmp(line, g_cfg.syspass))
        return 1;

    puts("%% Illegal access attempted %%");
    return 0;
}

/* ------------------------------------------------------------ */
/* general runtime/menu helpers                                 */
/* ------------------------------------------------------------ */

void do_time_on_system(void)
{
    char used[16];
    char left[16];

    format_hhmm(current_time_used(), used);
    format_hhmm(g_sess.time_left, left);

    puts("");
    printf("Time used this call  %s\n", used);
    printf("Time remaining       %s\n", left);
    bbs_pause();
}

void do_chat_with_sysop(void)
{
    char line[160];
    MENUFILE *m;

    if (!g_sess.sysop_chat)
    {
        puts("SYSOP chat unavailable");
        bbs_pause();
        return;
    }

    node_set_status("Chat with SYSOP");

    puts("");
    puts("Chat with SYSOP");
    puts("---------------");
    puts("Enter a blank line to end chat.");
    puts("");

    for (;;)
    {
        data_prompt_line("You> ", line, sizeof(line));
        if (!line[0])
            break;

        printf("SYSOP> %s\n", "(chat placeholder)");
    }

    m = menu_current();
    node_mark_menu(m ? m->filename : "Menu");
}

void do_expert_toggle(void)
{
    g_sess.user.expert = g_sess.user.expert ? 0 : 1;
    g_sess.expert = g_sess.user.expert ? 1 : 0;
    user_save_current();

    printf("Expert menus %s\n", g_sess.user.expert ? "ON" : "OFF");
    bbs_pause();
}

void bbs_goodbye(void)
{
    puts("");
    puts("Thanks for calling.");
    puts("Goodbye.");
}

void bbs_run(void)
{
    bbs_startup(0, (char **)0);

    if (g_sess.running)
        bbs_main_loop();

    bbs_shutdown();
}