/* BBSUSER.C
 *
 * First-pass BBS-PC 4.20 user record module
 *
 * Refactored to use BBSDATA.C / BBSISAM.C helpers:
 * - data_prompt_line()
 * - data_yesno()
 * - data_find_user_by_name()
 * - data_read_user() / data_write_user()
 * - data_first_caller() / data_next_caller()
 * - data_user_match()
 *
 * Implements:
 * - change/examine current user record
 * - simple user statistics display
 * - section-name display
 * - section mask editing
 * - expert toggle
 * - caller-log printing
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bbsdata.h"
#include "bbsfunc.h"

/* ------------------------------------------------------------ */

static long find_current_user_recno(void)
{
    USRDESC u;
    return data_find_user_by_name(g_sess.user.name, &u);
}

static void save_current_user(void)
{
    long recno;

    recno = find_current_user_recno();
    if (recno < 0L)
    {
        puts("Can't locate current user record");
        return;
    }

    if (!data_write_user(recno, &g_sess.user))
        puts("Can't write membership file");
}

static void show_term_name(term)
int term;
{
    if (term >= 0 && term < NUM_TERM && g_cfg.trmnl[term].name[0])
        printf("%s", g_cfg.trmnl[term].name);
    else
        printf("Terminal %d", term);
}

static void show_access_bits(label, mask)
char *label;
bits mask;
{
    int i;

    printf("%-12s ", label);
    for (i = 0; i < NUM_SECT; i++)
        putchar((mask & (1 << i)) ? ('A' + i) : '.');
    putchar('\n');
}

/* ------------------------------------------------------------ */
/* displayed information                                        */
/* ------------------------------------------------------------ */

void do_user_statistics(void)
{
    puts("");
    puts("User statistics:");
    printf("Name            %s\n", g_sess.user.name);
    printf("Location        %s\n", g_sess.user.loc);
    printf("Privilege       %u\n", (unsigned)g_sess.user.priv);
    printf("Calls total     %d\n", (int)g_sess.user.calls_total);
    printf("Messages left   %d\n", (int)g_sess.user.msg_total);
    printf("Uploads total   %d\n", (int)g_sess.user.upl_total);
    printf("Downloads total %d\n", (int)g_sess.user.dnl_total);
    printf("High msg read   %ld\n", (long)g_sess.user.high_msg);
    printf("Menu set        %u\n", (unsigned)g_sess.user.menu);
    printf("Saved section   %u\n", (unsigned)g_sess.user.sav_sec);
    printf("Protocol        %u\n", (unsigned)g_sess.user.protocol);
    printf("Width           %u\n", (unsigned)g_sess.user.width);
    printf("Page length     %u\n", (unsigned)g_sess.user.length);
    printf("NULS            %u\n", (unsigned)g_sess.user.nuls);
    printf("Time remaining  %d\n", (int)g_sess.time_left);

    printf("Terminal        ");
    show_term_name(g_sess.user.term);
    putchar('\n');

    printf("Expert          %s\n", g_sess.user.expert ? "Yes" : "No");
    printf("Linefeeds       %s\n", g_sess.user.linefeed ? "Yes" : "No");
    printf("Callback        %s\n", g_sess.user.callback ? "Yes" : "No");
    printf("Guest           %s\n", g_sess.user.guest ? "Yes" : "No");

    show_access_bits("Read access", g_sess.user.rd_acc);
    show_access_bits("Write access", g_sess.user.wr_acc);
    show_access_bits("Upload access", g_sess.user.up_acc);
    show_access_bits("Down access", g_sess.user.dn_acc);

    bbs_pause();
}

void do_section_names(void)
{
    int i;

    puts("");
    puts("Section names:");
    for (i = 0; i < NUM_SECT; i++)
        printf("%c: %s\n",
            'A' + i,
            g_cfg.sec_name[i][0] ? g_cfg.sec_name[i] : "(unnamed)");

    bbs_pause();
}

/* ------------------------------------------------------------ */
/* section mask editing                                         */
/* ------------------------------------------------------------ */

static void show_mask_editor(mask)
bits mask;
{
    int i;

    puts("");
    puts("Current section mask:");
    for (i = 0; i < NUM_SECT; i++)
        printf("%c %-20s %s\n",
            'A' + i,
            g_cfg.sec_name[i][0] ? g_cfg.sec_name[i] : "(unnamed)",
            (mask & (1 << i)) ? "ON" : "OFF");
    puts("");
    puts("Enter section letter to toggle, RETURN to finish.");
}

void do_change_section_mask(void)
{
    char line[16];
    int sec;

    for (;;)
    {
        show_mask_editor(g_sess.user.rd_acc);
        data_prompt_line("Section? ", line, sizeof(line));

        if (!line[0])
            break;

        if (line[0] >= 'a' && line[0] <= 'z')
            line[0] -= 32;

        if (line[0] < 'A' || line[0] >= ('A' + NUM_SECT))
            continue;

        sec = line[0] - 'A';
        g_sess.user.rd_acc ^= (1 << sec);
    }

    save_current_user();
}

/* ------------------------------------------------------------ */
/* user record editor                                           */
/* ------------------------------------------------------------ */

static void copy_term_defaults_to_user(u)
USRDESC *u;
{
    int t;

    t = u->term;
    if (t < 0 || t >= NUM_TERM)
        t = 0;

    u->nuls = g_cfg.trmnl[t].nuls;
    u->protocol = g_cfg.trmnl[t].protocol;
    u->width = 80;
    u->length = g_cfg.trmnl[t].page[0] ? g_cfg.trmnl[t].page[0] : 24;
    memcpy(u->cls, g_cfg.trmnl[t].cls, 3);
    memcpy(u->bs, g_cfg.trmnl[t].bs, 3);
    u->linefeed = g_cfg.trmnl[t].linefeed ? 1 : 0;
}

static void user_edit_name(void)
{
    char line[NAME_LEN + 2];

    data_prompt_line("Your name: ", line, sizeof(line));
    if (line[0])
    {
        strncpy(g_sess.user.name, line, NAME_LEN - 1);
        g_sess.user.name[NAME_LEN - 1] = 0;
    }
}

static void user_edit_location(void)
{
    char line[LOC_LEN + 2];

    data_prompt_line("City, State: ", line, sizeof(line));
    if (line[0])
    {
        strncpy(g_sess.user.loc, line, LOC_LEN);
        g_sess.user.loc[LOC_LEN] = 0;
    }
}

static void user_edit_phone(void)
{
    char line[PHONE_LEN + 2];

    data_prompt_line("Phone number: ", line, sizeof(line));
    if (line[0])
    {
        strncpy(g_sess.user.phone, line, PHONE_LEN);
        g_sess.user.phone[PHONE_LEN] = 0;
    }
}

static void user_edit_password(void)
{
    char line[PASS_LEN + 2];

    data_prompt_line("Password: ", line, sizeof(line));
    if (line[0])
    {
        strncpy(g_sess.user.pass, line, PASS_LEN);
        g_sess.user.pass[PASS_LEN] = 0;
    }
}

static void user_edit_terminal(void)
{
    char line[16];
    int i, t;

    puts("");
    puts("Terminal types:");
    for (i = 0; i < NUM_TERM; i++)
        if (g_cfg.trmnl[i].name[0])
            printf("%d: %s\n", i, g_cfg.trmnl[i].name);

    data_prompt_line("Terminal? ", line, sizeof(line));
    if (!line[0])
        return;

    t = atoi(line);
    if (t < 0 || t >= NUM_TERM)
        return;

    g_sess.user.term = (byte)t;
    copy_term_defaults_to_user(&g_sess.user);
}

static void user_edit_protocol(void)
{
    char line[16];

    data_prompt_line("Protocol number? ", line, sizeof(line));
    if (line[0])
        g_sess.user.protocol = (byte)atoi(line);
}

static void user_edit_menu_set(void)
{
    char line[16];

    data_prompt_line("Menu set? ", line, sizeof(line));
    if (line[0])
        g_sess.user.menu = (byte)atoi(line);
}

static void user_edit_saved_section(void)
{
    char line[16];

    data_prompt_line("Saved section? ", line, sizeof(line));
    if (line[0])
        g_sess.user.sav_sec = (byte)atoi(line);
}

static void user_edit_width(void)
{
    char line[16];

    data_prompt_line("Display width? ", line, sizeof(line));
    if (line[0])
        g_sess.user.width = (byte)atoi(line);
}

static void user_edit_length(void)
{
    char line[16];

    data_prompt_line("Page length? ", line, sizeof(line));
    if (line[0])
        g_sess.user.length = (byte)atoi(line);
}

static void user_edit_nuls(void)
{
    char line[16];

    data_prompt_line("Required NULS? ", line, sizeof(line));
    if (line[0])
        g_sess.user.nuls = (byte)atoi(line);
}

static void user_edit_linefeeds(void)
{
    g_sess.user.linefeed = data_yesno("Linefeeds (Y/N)? ", 0) ? 1 : 0;
}

static void user_edit_expert(void)
{
    g_sess.user.expert = data_yesno("Expert menus (Y/N)? ", 0) ? 1 : 0;
    g_sess.expert = g_sess.user.expert ? 1 : 0;
}

static void user_edit_cls_codes(void)
{
    char line[32];
    int a, b, c;

    data_prompt_line("CLS codes (a,b,c)? ", line, sizeof(line));
    if (sscanf(line, "%d,%d,%d", &a, &b, &c) == 3)
    {
        g_sess.user.cls[0] = (byte)a;
        g_sess.user.cls[1] = (byte)b;
        g_sess.user.cls[2] = (byte)c;
    }
}

static void user_edit_bs_codes(void)
{
    char line[32];
    int a, b, c;

    data_prompt_line("BS codes (a,b,c)? ", line, sizeof(line));
    if (sscanf(line, "%d,%d,%d", &a, &b, &c) == 3)
    {
        g_sess.user.bs[0] = (byte)a;
        g_sess.user.bs[1] = (byte)b;
        g_sess.user.bs[2] = (byte)c;
    }
}

static void show_user_editor_menu(void)
{
    puts("");
    puts("View/Edit your user options:");
    puts("A: Name");
    puts("B: Location");
    puts("C: Phone number");
    puts("D: Password");
    puts("E: Terminal type");
    puts("F: Protocol");
    puts("G: Menu set");
    puts("H: Saved section");
    puts("I: Display width");
    puts("J: Page length");
    puts("K: NULS");
    puts("L: Linefeeds");
    puts("M: Expert menus");
    puts("N: CLS codes");
    puts("O: BS codes");
    puts("Q: Quit");
    puts("");
}

void do_user_edit(void)
{
    char line[16];
    int done = 0;

    while (!done)
    {
        show_user_editor_menu();
        data_prompt_line("Option? ", line, sizeof(line));

        if (line[0] >= 'a' && line[0] <= 'z')
            line[0] -= 32;

        switch (line[0])
        {
            case 'A': user_edit_name(); break;
            case 'B': user_edit_location(); break;
            case 'C': user_edit_phone(); break;
            case 'D': user_edit_password(); break;
            case 'E': user_edit_terminal(); break;
            case 'F': user_edit_protocol(); break;
            case 'G': user_edit_menu_set(); break;
            case 'H': user_edit_saved_section(); break;
            case 'I': user_edit_width(); break;
            case 'J': user_edit_length(); break;
            case 'K': user_edit_nuls(); break;
            case 'L': user_edit_linefeeds(); break;
            case 'M': user_edit_expert(); break;
            case 'N': user_edit_cls_codes(); break;
            case 'O': user_edit_bs_codes(); break;
            case 'Q': done = 1; break;
            default:  break;
        }
    }

    save_current_user();
}

/* ------------------------------------------------------------ */
/* caller log display                                           */
/* ------------------------------------------------------------ */

void do_print_caller_log(void)
{
    USRLOG logrec;
    long recno;

    if (!g_fp_caller)
    {
        puts("Caller log not open");
        bbs_pause();
        return;
    }

    puts("");
    puts("Recent callers:");
    puts("");

    recno = data_first_caller(&logrec);
    while (recno >= 0L)
    {
        printf("%6ld  %-24s  %-24s  %5d\n",
            (long)logrec.number,
            logrec.name,
            logrec.loc,
            (int)logrec.baud);

        recno = data_next_caller(recno, &logrec);
    }

    bbs_pause();
}

/* ------------------------------------------------------------ */
/* simple helpers for future maintenance module                 */
/* ------------------------------------------------------------ */

long user_find_by_name(name, u)
char *name;
USRDESC *u;
{
    return data_find_user_by_name(name, u);
}

int user_load_by_name(name, u)
char *name;
USRDESC *u;
{
    return (data_find_user_by_name(name, u) >= 0L);
}

int user_save_current(void)
{
    long recno;

    recno = find_current_user_recno();
    if (recno < 0L)
        return 0;

    return data_write_user(recno, &g_sess.user);
}