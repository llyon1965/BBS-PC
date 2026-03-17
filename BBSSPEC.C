/* BBSSPEC.C
 *
 * First-pass BBS-PC 4.20 special/control function module
 *
 * Implements:
 * - phone directory listing/change stubs
 * - dial/connect stubs
 * - direct transfer stubs
 * - DOS gate / external execution stubs
 * - control-function helpers for menu return and menu-set changes
 *
 * Notes:
 * - This is still an early reconstruction.
 * - Real phone directory and modem structures are not yet fully
 *   recovered, so several routines are safe placeholders.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bbsdata.h"
#include "bbsfunc.h"

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

/* ------------------------------------------------------------ */
/* phone directory                                              */
/* ------------------------------------------------------------ */

void do_list_phone_directory(void)
{
    puts("");
    puts("Phone directory");
    puts("--------------");
    puts("Phone directory structure not fully reconstructed yet.");
    puts("This will later read the node/phone listing records.");
    puts("");
    bbs_pause();
}

void do_change_phone_listing(void)
{
    char line[80];

    if (!sysop_ok())
        return;

    puts("");
    puts("Change phone listing");
    puts("--------------------");
    prompt_line("Entry name: ", line, sizeof(line));
    if (!line[0])
        return;

    puts("Phone listing update not implemented yet");
    bbs_pause();
}

/* ------------------------------------------------------------ */
/* dial/connect                                                 */
/* ------------------------------------------------------------ */

void do_dial_connect_remote(void)
{
    char line[80];

    if (!sysop_ok())
        return;

    puts("");
    puts("Dial connect with remote");
    puts("------------------------");
    prompt_line("Directory entry: ", line, sizeof(line));
    if (!line[0])
        return;

    printf("Dial/connect to \"%s\" not implemented yet\n", line);
    bbs_pause();
}

void do_unlisted_dial_connect(void)
{
    char line[80];

    if (!sysop_ok())
        return;

    puts("");
    puts("Unlisted dial/connect");
    puts("---------------------");
    prompt_line("Phone number: ", line, sizeof(line));
    if (!line[0])
        return;

    printf("Unlisted dial/connect to \"%s\" not implemented yet\n", line);
    bbs_pause();
}

/* ------------------------------------------------------------ */
/* direct transfer functions                                    */
/* ------------------------------------------------------------ */

void do_upload_direct(void)
{
    if (!sysop_ok())
        return;

    puts("");
    puts("Upload direct");
    puts("-------------");
    puts("Direct modem upload not implemented yet");
    bbs_pause();
}

void do_download_direct(void)
{
    if (!sysop_ok())
        return;

    puts("");
    puts("Download direct");
    puts("---------------");
    puts("Direct modem download not implemented yet");
    bbs_pause();
}

void do_direct_file_kill(void)
{
    if (!sysop_ok())
        return;

    puts("");
    puts("Direct file kill");
    puts("----------------");
    puts("Direct disk-file deletion not implemented yet");
    bbs_pause();
}

/* ------------------------------------------------------------ */
/* DOS gate / external execution                                */
/* ------------------------------------------------------------ */

void do_dos_gate(void)
{
    char line[128];

    if (!sysop_ok())
        return;

    puts("");
    puts("DOS gate");
    puts("--------");
    prompt_line("DOS command (blank to cancel): ", line, sizeof(line));
    if (!line[0])
        return;

    printf("Would execute DOS command: %s\n", line);
    puts("DOS gate not implemented yet");
    bbs_pause();
}

void do_execute_external_program(cmd)
char *cmd;
{
    if (!sysop_ok())
        return;

    puts("");
    puts("Execute external program");
    puts("------------------------");

    if (!cmd || !*cmd)
    {
        puts("No external program specified");
        bbs_pause();
        return;
    }

    printf("Would execute external program: %s\n", cmd);
    puts("External execution not implemented yet");
    bbs_pause();
}

/* ------------------------------------------------------------ */
/* menu/control helpers                                         */
/* ------------------------------------------------------------ */

void do_return_specified_levels(void)
{
    char line[16];
    int levels;
    int i;

    prompt_line("Return how many menu levels? ", line, sizeof(line));
    if (!line[0])
        return;

    levels = atoi(line);
    if (levels <= 0)
        return;

    for (i = 0; i < levels; i++)
    {
        if (!menu_pop())
            break;
    }

    puts("Menu return complete");
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

    puts("");
    printf("Current menu set: %u\n", (unsigned)g_sess.user.menu);
    prompt_line("New menu set? ", line, sizeof(line));
    if (!line[0])
        return;

    g_sess.user.menu = (byte)atoi(line);
    user_save_current();

    printf("Menu set changed to %u\n", (unsigned)g_sess.user.menu);
    bbs_pause();
}

/* ------------------------------------------------------------ */
/* optional helpers for later direct runtime integration        */
/* ------------------------------------------------------------ */

void do_special_file_command(void)
{
    puts("Special file command not implemented yet");
}

void do_special_phone_command(void)
{
    puts("Special phone command not implemented yet");
}