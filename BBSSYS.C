/* BBSSYS.C
 *
 * BBS-PC ! 4.21
 *
 * Runtime + sysop helper functions
 */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <conio.h>
#include <dos.h>

#include "bbsdata.h"
#include "bbsfunc.h"

#ifndef SYSOP_PAGE_SECONDS
#define SYSOP_PAGE_SECONDS 30
#endif

#ifndef SYSOP_PAGE_BEEP_MS
#define SYSOP_PAGE_BEEP_MS 750
#endif

/* ------------------------------------------------------------ */
/* Session timing helpers (F3 support)                          */
/* ------------------------------------------------------------ */

int session_minutes_used(void)
{
    time_t now;
    long secs;

    if (!g_sess.logon_unix)
        return 0;

    now = time((time_t *)0);
    secs = (long)(now - (time_t)g_sess.logon_unix);
    if (secs < 0L)
        secs = 0L;

    return (int)(secs / 60L);
}

int session_minutes_left(void)
{
    int allowed;
    int left;

    if (g_sess.base_minutes_allowed <= 0)
        return 0x7FFF;   /* treat as unlimited */

    allowed = g_sess.base_minutes_allowed + g_sess.bonus_minutes;
    left = allowed - session_minutes_used();

    if (left < 0)
        left = 0;

    return left;
}

int session_time_expired(void)
{
    if (g_sess.base_minutes_allowed <= 0)
        return 0;

    return session_minutes_left() <= 0;
}

/* ------------------------------------------------------------ */
/* Session idle helpers                                         */
/* ------------------------------------------------------------ */

void session_touch_activity(void)
{
    g_sess.last_activity_unix = (ulong)time((time_t *)0);
}

int session_idle_minutes(void)
{
    time_t now;
    long secs;

    if (!g_sess.last_activity_unix)
        return 0;

    now = time((time_t *)0);
    secs = (long)(now - (time_t)g_sess.last_activity_unix);
    if (secs < 0L)
        secs = 0L;

    return (int)(secs / 60L);
}

int session_idle_expired(void)
{
    if (g_cfg.idle_limit <= 0)
        return 0;

    return session_idle_minutes() >= (int)g_cfg.idle_limit;
}

/* ------------------------------------------------------------ */
/* local paging helpers                                         */
/* ------------------------------------------------------------ */

static void sysop_page_local_bell(void)
{
    bbs_beep();
}

static int sysop_page_wait_for_answer(void)
{
    time_t start;
    time_t now;

    start = time((time_t *)0);
    now = start;

    while ((long)(now - start) < SYSOP_PAGE_SECONDS)
    {
        if (kbhit())
        {
            (void)getch();
            return 1;
        }

        sysop_page_local_bell();
        delay(SYSOP_PAGE_BEEP_MS);

        if (!g_sess.local_login && !term_carrier())
            return 0;

        if (session_time_expired())
            return 0;

        if (session_idle_expired())
            return 0;

        now = time((time_t *)0);
    }

    return 0;
}

/* ------------------------------------------------------------ */
/* User-visible time display                                    */
/* ------------------------------------------------------------ */

void do_time_on_system(void)
{
    int used;
    int left;

    used = session_minutes_used();
    left = session_minutes_left();

    printf("Time used: %d minute(s)\n", used);

    if (g_sess.base_minutes_allowed > 0)
    {
        printf("Time left: %d minute(s)\n", left);

        if (g_sess.bonus_minutes > 0)
            printf("Bonus time: %d minute(s)\n", g_sess.bonus_minutes);
    }
    else
    {
        puts("Time left: Unlimited");

        if (g_sess.bonus_minutes > 0)
            printf("Bonus time: %d minute(s)\n", g_sess.bonus_minutes);
    }

    if (g_cfg.idle_limit > 0)
        printf("Idle limit: %u minute(s), idle now: %d minute(s)\n",
               (unsigned)g_cfg.idle_limit,
               session_idle_minutes());
    else
        puts("Idle limit: Disabled");
}

/* ------------------------------------------------------------ */
/* Sysop / runtime helpers                                      */
/* ------------------------------------------------------------ */

void do_chat_with_sysop(void)
{
    char userbuf[128];
    char sysbuf[128];

    puts("");
    puts("Paging Sysop...");
    puts("");

    if (!sysop_page_wait_for_answer())
    {
        puts("Sysop not available");
        puts("");
        return;
    }

    puts("*** Chat mode - type /EXIT to end ***");
    puts("");

    while (1)
    {
        if (!g_sess.local_login)
        {
            if (!term_carrier())
            {
                puts("Carrier lost");
                break;
            }
        }

        if (session_time_expired())
        {
            puts("Time limit reached");
            break;
        }

        if (session_idle_expired())
        {
            puts("Idle timeout reached");
            break;
        }

        term_getline("> ", userbuf, sizeof(userbuf));
        session_touch_activity();

        if (!userbuf[0])
            continue;

        if (!stricmp(userbuf, "/EXIT"))
            break;

        printf("USER: %s\n", userbuf);

        term_getline("SYSOP> ", sysbuf, sizeof(sysbuf));
        session_touch_activity();

        if (!sysbuf[0])
            continue;

        if (!stricmp(sysbuf, "/EXIT"))
            break;

        printf("SYSOP: %s\n", sysbuf);
    }

    puts("");
    puts("*** Chat ended ***");
    puts("");
}

void do_expert_toggle(void)
{
    g_sess.expert = g_sess.expert ? 0 : 1;
    g_sess.user.expert = g_sess.expert ? 1 : 0;

    puts(g_sess.expert ? "Expert mode ON" : "Expert mode OFF");
}

int sysop_password_prompt(void)
{
    char buf[32];

    term_getline_hidden("Sysop password: ", buf, sizeof(buf));

    if (!buf[0])
        return 0;

    if (!stricmp(buf, g_cfg.sysop_pass))
        return 1;

    puts("Incorrect password.");
    return 0;
}
/* patched time display already handled elsewhere */
