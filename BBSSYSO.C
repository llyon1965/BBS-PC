/* BBSSYSO.C
 *
 * First-pass BBS-PC 4.20 SYSOP / maintenance / special-function module
 *
 * Implements:
 * - maintenance/sysop opcode stubs from the documented menu table
 * - basic add/delete/change user operations
 * - print user list
 * - simple DOS gate / external program hooks
 *
 * Notes:
 * - This is a first-pass reconstruction intended to make the main
 *   runtime tree compile and run.
 * - True indexed-key maintenance, modem defaults, node defaults,
 *   and full configuration editors remain to be reconstructed.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bbsdata.h"
#include "bbsfunc.h"

#define FILEHDR_LEN 128

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

static void prompt_line(prompt, out, len)
char *prompt;
char *out;
int len;
{
    printf("%s", prompt);
    if (fgets(out, len, stdin) == NULL)
        out[0] = 0;
    trim_crlf_local(out);
}

static int yesno_prompt(prompt)
char *prompt;
{
    char line[16];

    prompt_line(prompt, line, sizeof(line));
    return (line[0] == 'Y' || line[0] == 'y');
}

static int sysop_ok(void)
{
    char pass[PASS_LEN + 2];

    if (g_sess.user.priv >= 100)
        return 1;

    prompt_line("SYSOP password? ", pass, sizeof(pass));

    if (!strcmp(pass, g_cfg.syspass))
        return 1;

    puts("%% Illegal access attempted %%");
    return 0;
}

static long indexed_count(fp, reclen)
FILE *fp;
int reclen;
{
    long sz;

    if (!fp)
        return 0L;

    fseek(fp, 0L, SEEK_END);
    sz = ftell(fp);

    if (sz < FILEHDR_LEN)
        return 0L;

    return (sz - FILEHDR_LEN) / (long)reclen;
}

static long indexed_offset(recno, reclen)
long recno;
int reclen;
{
    return FILEHDR_LEN + recno * (long)reclen;
}

static int read_user_record(recno, u)
long recno;
USRDESC *u;
{
    if (!g_fp_userdesc)
        return 0;

    fseek(g_fp_userdesc, indexed_offset(recno, sizeof(USRDESC)), SEEK_SET);
    return fread(u, sizeof(USRDESC), 1, g_fp_userdesc) == 1;
}

static int write_user_record(recno, u)
long recno;
USRDESC *u;
{
    if (!g_fp_userdesc)
        return 0;

    fseek(g_fp_userdesc, indexed_offset(recno, sizeof(USRDESC)), SEEK_SET);
    return fwrite(u, sizeof(USRDESC), 1, g_fp_userdesc) == 1;
}

static void uppercase_name(dst, src, maxlen)
char *dst;
char *src;
int maxlen;
{
    int i;

    for (i = 0; src[i] && i < maxlen - 1; i++)
    {
        if (src[i] >= 'a' && src[i] <= 'z')
            dst[i] = (char)(src[i] - 32);
        else
            dst[i] = src[i];
    }

    dst[i] = 0;
}

static long find_user_by_name(name, u)
char *name;
USRDESC *u;
{
    long i, nrec;
    char uname[NAME_LEN + 1];
    char input[NAME_LEN + 1];

    if (!g_fp_userdesc)
        return -1L;

    uppercase_name(input, name, sizeof(input));
    nrec = indexed_count(g_fp_userdesc, sizeof(USRDESC));

    for (i = 0L; i < nrec; i++)
    {
        if (!read_user_record(i, u))
            continue;

        if (!u->name[0])
            continue;

        uppercase_name(uname, u->name, sizeof(uname));

        if (!strcmp(uname, input))
            return i;
    }

    return -1L;
}

static long find_empty_user_slot(void)
{
    long i, nrec;
    USRDESC u;

    if (!g_fp_userdesc)
        return -1L;

    nrec = indexed_count(g_fp_userdesc, sizeof(USRDESC));

    for (i = 0L; i < nrec; i++)
    {
        if (!read_user_record(i, &u))
            continue;

        if (!u.name[0])
            return i;
    }

    return -1L;
}

static void apply_member_defaults(u)
USRDESC *u)
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

        prompt_line("Option? ", line, sizeof(line));

        if (line[0] >= 'a' && line[0] <= 'z')
            line[0] -= 32;

        switch (line[0])
        {
            case 'A':
                prompt_line("Name: ", line, sizeof(line));
                if (line[0])
                {
                    strncpy(u->name, line, NAME_LEN - 1);
                    u->name[NAME_LEN - 1] = 0;
                }
                break;

            case 'B':
                prompt_line("Location: ", line, sizeof(line));
                if (line[0])
                {
                    strncpy(u->loc, line, LOC_LEN);
                    u->loc[LOC_LEN] = 0;
                }
                break;

            case 'C':
                prompt_line("Phone: ", line, sizeof(line));
                if (line[0])
                {
                    strncpy(u->phone, line, PHONE_LEN);
                    u->phone[PHONE_LEN] = 0;
                }
                break;

            case 'D':
                prompt_line("Password: ", line, sizeof(line));
                if (line[0])
                {
                    strncpy(u->pass, line, PASS_LEN);
                    u->pass[PASS_LEN] = 0;
                }
                break;

            case 'E':
                prompt_line("Privilege: ", line, sizeof(line));
                if (line[0])
                    u->priv = (byte)atoi(line);
                break;

            case 'F':
                prompt_line("Time limit: ", line, sizeof(line));
                if (line[0])
                    u->limit = (shortint)atoi(line);
                break;

            case 'G':
                prompt_line("Menu set: ", line, sizeof(line));
                if (line[0])
                    u->menu = (byte)atoi(line);
                break;

            case 'H':
                prompt_line("Terminal: ", line, sizeof(line));
                if (line[0])
                    u->term = (byte)atoi(line);
                break;

            case 'I':
                prompt_line("Save section: ", line, sizeof(line));
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

    recno = find_empty_user_slot();
    if (recno < 0L)
    {
        puts("Membership file full");
        return;
    }

    apply_member_defaults(&u);

    prompt_line("Name: ", line, sizeof(line));
    if (!line[0])
        return;
    strncpy(u.name, line, NAME_LEN - 1);
    u.name[NAME_LEN - 1] = 0;

    prompt_line("Location: ", line, sizeof(line));
    strncpy(u.loc, line, LOC_LEN);
    u.loc[LOC_LEN] = 0;

    prompt_line("Phone: ", line, sizeof(line));
    strncpy(u.phone, line, PHONE_LEN);
    u.phone[PHONE_LEN] = 0;

    prompt_line("Password: ", line, sizeof(line));
    strncpy(u.pass, line, PASS_LEN);
    u.pass[PASS_LEN] = 0;

    prompt_line("Privilege: ", line, sizeof(line));
    if (line[0])
        u.priv = (byte)atoi(line);

    if (!write_user_record(recno, &u))
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

    prompt_line("Delete which user? ", name, sizeof(name));
    if (!name[0])
        return;

    recno = find_user_by_name(name, &u);
    if (recno < 0L)
    {
        puts("User not found");
        return;
    }

    show_user_summary(&u);

    if (!yesno_prompt("Delete user (Y/N)? "))
        return;

    memset(&u, 0, sizeof(u));

    if (!write_user_record(recno, &u))
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

    prompt_line("Change which user? ", name, sizeof(name));
    if (!name[0])
        return;

    recno = find_user_by_name(name, &u);
    if (recno < 0L)
    {
        puts("User not found");
        return;
    }

    simple_user_editor(&u);

    if (!write_user_record(recno, &u))
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
    long i, nrec;

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

    nrec = indexed_count(g_fp_userdesc, sizeof(USRDESC));
    for (i = 0L; i < nrec; i++)
    {
        if (!read_user_record(i, &u))
            continue;

        if (!u.name[0])
            continue;

        printf("%-24s  %-24s  %3u  %5d\n",
            u.name,
            u.loc,
            (unsigned)u.priv,
            (int)u.calls_total);
    }

    bbs_pause();
}

void do_reset_bulletin_flags(void)
{
    USRDESC u;
    long i, nrec;

    if (!sysop_ok())
        return;

    if (!g_fp_userdesc)
    {
        puts("Membership file not open");
        return;
    }

    nrec = indexed_count(g_fp_userdesc, sizeof(USRDESC));

    for (i = 0L; i < nrec; i++)
    {
        if (!read_user_record(i, &u))
            continue;

        if (!u.name[0])
            continue;

        u.bul_flg = 0;
        write_user_record(i, &u);
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

        trim_crlf_local(line);
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

void do_modem_defaults(void)
{
    if (!sysop_ok())
        return;

    puts("Modem defaults not implemented yet");
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

void do_change_node_defaults(void)
{
    if (!sysop_ok())
        return;

    puts("Change node defaults not implemented yet");
}

/* ------------------------------------------------------------ */
/* special functions                                            */
/* ------------------------------------------------------------ */

void do_list_phone_directory(void)
{
    puts("List phone directory not implemented yet");
}

void do_change_phone_listing(void)
{
    if (!sysop_ok())
        return;

    puts("Change phone listing not implemented yet");
}

void do_dial_connect_remote(void)
{
    if (!sysop_ok())
        return;

    puts("Dial connect with remote not implemented yet");
}

void do_unlisted_dial_connect(void)
{
    if (!sysop_ok())
        return;

    puts("Unlisted dial/connect not implemented yet");
}

void do_upload_direct(void)
{
    if (!sysop_ok())
        return;

    puts("Upload direct not implemented yet");
}

void do_download_direct(void)
{
    if (!sysop_ok())
        return;

    puts("Download direct not implemented yet");
}

void do_direct_file_kill(void)
{
    if (!sysop_ok())
        return;

    puts("Direct file kill not implemented yet");
}

void do_dos_gate(void)
{
    if (!sysop_ok())
        return;

    puts("DOS gate not implemented yet");
}

/* ------------------------------------------------------------ */
/* control-function helpers                                     */
/* ------------------------------------------------------------ */

void do_return_specified_levels(void)
{
    puts("Return specified levels not implemented yet");
}

void do_return_top_level(void)
{
    while (menu_pop())
        ;

    puts("Returned to top menu");
}

void do_change_menu_sets(void)
{
    char line[16];

    prompt_line("New menu set? ", line, sizeof(line));
    if (!line[0])
        return;

    g_sess.user.menu = (byte)atoi(line);
    user_save_current();
    puts("Menu set changed");
}

void do_execute_external_program(cmd)
char *cmd;
{
    if (!sysop_ok())
        return;

    if (!cmd || !*cmd)
    {
        puts("No external program specified");
        return;
    }

    printf("Execute external program: %s\n", cmd);
    puts("External execution not implemented yet");
}