/* BBSMAINT.C
 *
 * First-pass BBS-PC 4.20 maintenance module
 *
 * Ownership cleanup:
 * - do_modem_defaults() lives in BBSMODM.C
 * - do_purge_messages() lives in BBSMSG.C
 * - opcode 35 (change node defaults) lives in BBSNODEED.C
 *
 * Refactored to use BBSDATA.C / BBSISAM.C helpers:
 * - data_prompt_line()
 * - data_yesno()
 * - data_find_user_by_name()
 * - data_find_blank_user()
 * - data_read_user() / data_write_user()
 * - data_first_user() / data_next_user()
 * - data_trim_crlf()
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bbsdata.h"
#include "bbsfunc.h"

/* ------------------------------------------------------------ */

static int sysop_ok(void)
{
    return sysop_password_prompt();
}

static void apply_member_defaults(u)
USRDESC *u;
{
    memset(u, 0, sizeof(*u));

    u->limit     = g_cfg.limit[1];
    u->time_rem  = g_cfg.limit[1];
    u->priv      = g_cfg.priv[1];
    u->rd_acc    = g_cfg.rd_acc[1];
    u->wr_acc    = g_cfg.wr_acc[1];
    u->up_acc    = g_cfg.up_acc[1];
    u->dn_acc    = g_cfg.dn_acc[1];
    u->sav_sec   = g_cfg.sav_sec[1];
    u->menu      = g_cfg.menu[1];
    u->term      = 0;
    u->protocol  = 0;
    u->width     = 80;
    u->length    = 24;
    u->nuls      = 0;
}

static void show_user_summary(u)
USRDESC *u;
{
    puts("");
    printf("Name      %s\n", u->name);
    printf("Location  %s\n", u->loc);
    printf("Phone     %s\n", u->phone);
    printf("Priv      %u\n", (unsigned)u->priv);
    printf("Limit     %d\n", (int)u->limit);
    printf("Menu      %u\n", (unsigned)u->menu);
    printf("Term      %u\n", (unsigned)u->term);
    printf("Calls     %d\n", (int)u->calls_total);
    printf("Msgs      %d\n", (int)u->msg_total);
    printf("Uploads   %d\n", (int)u->upl_total);
    printf("Downloads %d\n", (int)u->dnl_total);
}

static void simple_user_editor(u)
USRDESC *u;
{
    char line[80];

    for (;;)
    {
        show_user_summary(u);
        puts("");
        puts("A: Name");
        puts("B: Location");
        puts("C: Phone");
        puts("D: Password");
        puts("E: Privilege");
        puts("F: Time limit");
        puts("G: Menu set");
        puts("H: Terminal");
        puts("I: Save section");
        puts("Q: Quit");
        puts("");

        data_prompt_line("Option? ", line, sizeof(line));

        if (line[0] >= 'a' && line[0] <= 'z')
            line[0] -= 32;

        switch (line[0])
        {
            case 'A':
                data_prompt_line("Name: ", line, sizeof(line));
                if (line[0])
                {
                    strncpy(u->name, line, NAME_LEN - 1);
                    u->name[NAME_LEN - 1] = 0;
                }
                break;

            case 'B':
                data_prompt_line("Location: ", line, sizeof(line));
                if (line[0])
                {
                    strncpy(u->loc, line, LOC_LEN);
                    u->loc[LOC_LEN] = 0;
                }
                break;

            case 'C':
                data_prompt_line("Phone: ", line, sizeof(line));
                if (line[0])
                {
                    strncpy(u->phone, line, PHONE_LEN);
                    u->phone[PHONE_LEN] = 0;
                }
                break;

            case 'D':
                data_prompt_line("Password: ", line, sizeof(line));
                if (line[0])
                {
                    strncpy(u->pass, line, PASS_LEN);
                    u->pass[PASS_LEN] = 0;
                }
                break;

            case 'E':
                data_prompt_line("Privilege: ", line, sizeof(line));
                if (line[0])
                    u->priv = (byte)atoi(line);
                break;

            case 'F':
                data_prompt_line("Time limit: ", line, sizeof(line));
                if (line[0])
                    u->limit = (shortint)atoi(line);
                break;

            case 'G':
                data_prompt_line("Menu set: ", line, sizeof(line));
                if (line[0])
                    u->menu = (byte)atoi(line);
                break;

            case 'H':
                data_prompt_line("Terminal: ", line, sizeof(line));
                if (line[0])
                    u->term = (byte)atoi(line);
                break;

            case 'I':
                data_prompt_line("Save section: ", line, sizeof(line));
                if (line[0])
                    u->sav_sec = (byte)atoi(line);
                break;

            case 'Q':
                return;

            default:
                break;
        }
    }
}

/* ------------------------------------------------------------ */
/* maintenance functions                                        */
/* ------------------------------------------------------------ */

void do_add_user(void)
{
    USRDESC u;
    long recno;
    char line[80];

    if (!sysop_ok())
        return;

    recno = data_find_blank_user();
    if (recno < 0L)
    {
        puts("Membership file full");
        return;
    }

    apply_member_defaults(&u);

    data_prompt_line("Name: ", line, sizeof(line));
    if (!line[0])
        return;
    strncpy(u.name, line, NAME_LEN - 1);
    u.name[NAME_LEN - 1] = 0;

    data_prompt_line("Location: ", line, sizeof(line));
    strncpy(u.loc, line, LOC_LEN);
    u.loc[LOC_LEN] = 0;

    data_prompt_line("Phone: ", line, sizeof(line));
    strncpy(u.phone, line, PHONE_LEN);
    u.phone[PHONE_LEN] = 0;

    data_prompt_line("Password: ", line, sizeof(line));
    strncpy(u.pass, line, PASS_LEN);
    u.pass[PASS_LEN] = 0;

    data_prompt_line("Privilege: ", line, sizeof(line));
    if (line[0])
        u.priv = (byte)atoi(line);

    if (!data_write_user(recno, &u))
    {
        puts("Can't write membership file");
        return;
    }

    puts("User added");
}

void do_delete_user(void)
{
    USRDESC u;
    long recno;
    char name[NAME_LEN + 2];

    if (!sysop_ok())
        return;

    data_prompt_line("Delete which user? ", name, sizeof(name));
    if (!name[0])
        return;

    recno = data_find_user_by_name(name, &u);
    if (recno < 0L)
    {
        puts("User not found");
        return;
    }

    show_user_summary(&u);

    if (!data_yesno("Delete user (Y/N)? ", 0))
        return;

    memset(&u, 0, sizeof(u));

    if (!data_write_user(recno, &u))
    {
        puts("Can't delete user");
        return;
    }

    puts("Killed");
}

void do_change_user(void)
{
    USRDESC u;
    long recno;
    char name[NAME_LEN + 2];

    if (!sysop_ok())
        return;

    data_prompt_line("Change which user? ", name, sizeof(name));
    if (!name[0])
        return;

    recno = data_find_user_by_name(name, &u);
    if (recno < 0L)
    {
        puts("User not found");
        return;
    }

    simple_user_editor(&u);

    if (!data_write_user(recno, &u))
    {
        puts("Can't write membership file");
        return;
    }

    puts("User changed");
}

void do_purge_inactive_users(void)
{
    if (!sysop_ok())
        return;

    puts("Purge inactive users not implemented yet");
}

void do_print_user_list(void)
{
    USRDESC u;
    long recno;

    if (!sysop_ok())
        return;

    if (!g_fp_userdesc)
    {
        puts("Membership file not open");
        return;
    }

    puts("");
    puts("User list:");
    puts("");

    recno = data_first_user(&u);
    while (recno >= 0L)
    {
        printf("%-24s  %-24s  %3u  %5d\n",
            u.name,
            u.loc,
            (unsigned)u.priv,
            (int)u.calls_total);

        recno = data_next_user(recno, &u);
    }

    bbs_pause();
}

void do_reset_bulletin_flags(void)
{
    USRDESC u;
    long recno;

    if (!sysop_ok())
        return;

    if (!g_fp_userdesc)
    {
        puts("Membership file not open");
        return;
    }

    recno = data_first_user(&u);
    while (recno >= 0L)
    {
        u.bul_flg = 0;
        data_write_user(recno, &u);
        recno = data_next_user(recno, &u);
    }

    puts("Bulletin flags reset");
}

void do_update_user_defaults(void)
{
    if (!sysop_ok())
        return;

    puts("Update user defaults not implemented yet");
}

void do_define_section_names(void)
{
    char line[SECT_LEN + 2];
    int i;

    if (!sysop_ok())
        return;

    for (i = 0; i < NUM_SECT; i++)
    {
        printf("%c [%s]: ",
            'A' + i,
            g_cfg.sec_name[i][0] ? g_cfg.sec_name[i] : "");

        if (fgets(line, sizeof(line), stdin) == NULL)
            break;

        data_trim_crlf(line);
        if (line[0])
        {
            strncpy(g_cfg.sec_name[i], line, SECT_LEN);
            g_cfg.sec_name[i][SECT_LEN] = 0;
        }
    }

    puts("Section names updated in memory");
}

void do_define_terminal_types(void)
{
    if (!sysop_ok())
        return;

    puts("Define terminal types not implemented yet");
}

void do_user_defaults(void)
{
    if (!sysop_ok())
        return;

    puts("User defaults not implemented yet");
}

void do_system_defaults(void)
{
    if (!sysop_ok())
        return;

    puts("System defaults not implemented yet");
}

/* opcode 35 moved to BBSNODEED.C */
/* do_modem_defaults() lives in BBSMODM.C */
/* do_purge_messages() lives in BBSMSG.C */