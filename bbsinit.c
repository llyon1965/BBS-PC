/* BBSINIT.C
 *
 * First-pass BBS-PC 4.20 initialization / maintenance utility
 *
 * Updated to use:
 * - node_create_file()
 * - node_set_pseudo_counter_file()
 *
 * Corrected:
 * - writes "UD=%s" instead of "UD =%s" in BBS.P
 *
 * Supports:
 * - interactive parameter-file generation
 * - interactive main datafile creation
 * - interactive node-file creation
 * - -m:x renumber message base
 * - -c:x set highest caller number
 * - -s:x set pseudo filename counter for a node
 * - -u   update U/D paths
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bbsdata.h"
#include "bbsfunc.h"

#define HDRLEN 128

static long opt_msg_base = -1L;
static long opt_call_no  = -1L;
static long opt_pseudo   = -1L;
static int  opt_update_ud = 0;

/* ------------------------------------------------------------ */

static void write_blank_header(fp)
FILE *fp;
{
    unsigned char hdr[HDRLEN];

    memset(hdr, 0, sizeof(hdr));
    fwrite(hdr, sizeof(hdr), 1, fp);
}

static int create_blank_dat(fname)
char *fname;
{
    FILE *fp;

    fp = fopen(fname, "wb");
    if (!fp)
        return 0;

    write_blank_header(fp);
    fclose(fp);
    return 1;
}

static int create_fixed_dat(fname, reclen, count)
char *fname;
int reclen;
int count;
{
    FILE *fp;
    void *buf;
    int i;

    fp = fopen(fname, "wb");
    if (!fp)
        return 0;

    write_blank_header(fp);

    buf = calloc(1, (unsigned)reclen);
    if (!buf)
    {
        fclose(fp);
        return 0;
    }

    for (i = 0; i < count; i++)
        fwrite(buf, (unsigned)reclen, 1, fp);

    free(buf);
    fclose(fp);
    return 1;
}

static void prompt_line(prompt, out, len)
char *prompt;
char *out;
int len;
{
    data_prompt_line(prompt, out, len);
}

static int yesno_prompt(prompt, def_yes)
char *prompt;
int def_yes;
{
    return data_yesno(prompt, def_yes);
}

/* ------------------------------------------------------------ */
/* command-line parsing                                         */
/* ------------------------------------------------------------ */

static void parse_args(argc, argv)
int argc;
char *argv[];
{
    int i;

    for (i = 1; i < argc; i++)
    {
        if (!strnicmp(argv[i], "-m:", 3))
            opt_msg_base = atol(argv[i] + 3);
        else if (!strnicmp(argv[i], "-c:", 3))
            opt_call_no = atol(argv[i] + 3);
        else if (!strnicmp(argv[i], "-s:", 3))
            opt_pseudo = atol(argv[i] + 3);
        else if (!stricmp(argv[i], "-u"))
            opt_update_ud = 1;
    }
}

/* ------------------------------------------------------------ */
/* parameter file                                               */
/* ------------------------------------------------------------ */

static int save_bbs_p(paths)
BBSPATHS *paths;
{
    FILE *fp;
    int i;

    fp = fopen("BBS.P", "wt");
    if (!fp)
        return 0;

    fprintf(fp, "MSG=%s\n", paths->msg_path);
    fprintf(fp, "USR=%s\n", paths->usr_path);
    fprintf(fp, "UD=%s\n", paths->ud_path);
    fprintf(fp, "LOG=%s\n", paths->log_path);

    for (i = 0; i < NUM_SECT; i++)
        if (paths->updn_path[i][0])
            fprintf(fp, "UPDN%02d=%s\n", i, paths->updn_path[i]);

    fclose(fp);
    return 1;
}

static void interactive_parameter_file(void)
{
    int i;
    char line[MAX_PATHNAME];

    memset(&g_paths, 0, sizeof(g_paths));

    prompt_line("Drive/path for message files: ", g_paths.msg_path, sizeof(g_paths.msg_path));
    prompt_line("Drive/path for user files: ", g_paths.usr_path, sizeof(g_paths.usr_path));
    prompt_line("Drive/path for U/D indexes: ", g_paths.ud_path, sizeof(g_paths.ud_path));
    prompt_line("Drive/path for log files: ", g_paths.log_path, sizeof(g_paths.log_path));

    puts("Enter up to 16 paths for U/D files");
    for (i = 0; i < NUM_SECT; i++)
    {
        sprintf(line, "Drive/path #%d: ", i + 1);
        prompt_line(line, g_paths.updn_path[i], sizeof(g_paths.updn_path[i]));
        if (!g_paths.updn_path[i][0])
            break;
    }

    if (save_bbs_p(&g_paths))
        puts("BBS.P written");
    else
        puts("Can't write BBS.P");
}

static void interactive_update_ud_paths(void)
{
    int i;
    char line[MAX_PATHNAME];

    if (!load_bbs_paths("BBS.P"))
        memset(&g_paths, 0, sizeof(g_paths));

    puts("Current U/D paths:");
    for (i = 0; i < NUM_SECT; i++)
        printf("%2d: %s\n", i + 1, g_paths.updn_path[i]);

    puts("");
    puts("Enter new paths and press RETURN when finished.");

    for (i = 0; i < NUM_SECT; i++)
    {
        sprintf(line, "Drive/path #%d: ", i + 1);
        prompt_line(line, g_paths.updn_path[i], sizeof(g_paths.updn_path[i]));
        if (!g_paths.updn_path[i][0])
            break;
    }

    if (save_bbs_p(&g_paths))
        puts("BBS.P updated");
    else
        puts("Can't update BBS.P");
}

/* ------------------------------------------------------------ */
/* main BBS files                                               */
/* ------------------------------------------------------------ */

static void create_main_bbs_files(void)
{
    int max_msg, max_user, max_log, max_ud;

    if (!load_cfginfo("CFGINFO.DAT"))
        memset(&g_cfg, 0, sizeof(g_cfg));

    max_msg  = g_cfg.max_msg  ? g_cfg.max_msg  : 512;
    max_user = g_cfg.max_user ? g_cfg.max_user : 256;
    max_log  = g_cfg.max_log  ? g_cfg.max_log  : 256;
    max_ud   = g_cfg.max_ud   ? g_cfg.max_ud   : 512;

    puts("Creating main BBS datafiles...");

    create_fixed_dat("MSGHEAD.DAT", sizeof(MSGHEAD), max_msg);
    create_fixed_dat("MSGTEXT.DAT", sizeof(MSGTEXT), max_msg * 4);
    create_blank_dat("MSGKEY.DAT");

    create_fixed_dat("USERDESC.DAT", sizeof(USRDESC), max_user);
    create_blank_dat("USERKEY.DAT");

    create_fixed_dat("UDHEAD.DAT", sizeof(UDHEAD), max_ud);
    create_blank_dat("UDKEY1.DAT");
    create_blank_dat("UDKEY2.DAT");
    create_blank_dat("UDKEY3.DAT");

    create_fixed_dat("CALLER.DAT", sizeof(USRLOG), max_log);
    create_blank_dat("CALLKEY.DAT");

    puts("Main BBS files created");
}

/* ------------------------------------------------------------ */
/* node file creation/update                                    */
/* ------------------------------------------------------------ */

static void interactive_create_node_file(void)
{
    char line[16];
    int node_num;

    prompt_line("Which node (1-99)? ", line, sizeof(line));
    if (!line[0])
        return;

    node_num = atoi(line);
    if (node_num < 1 || node_num > 99)
    {
        puts("Illegal node number");
        return;
    }

    node_create_file(node_num);
    printf("NODE%02d.DAT created\n", node_num);
}

static void apply_pseudo_counter_change(void)
{
    char line[16];
    int node_num;

    if (opt_pseudo < 0L)
        return;

    prompt_line("Which node (1-99)? ", line, sizeof(line));
    if (!line[0])
        return;

    node_num = atoi(line);
    if (node_num < 1 || node_num > 99)
    {
        puts("Illegal node number");
        return;
    }

    node_set_pseudo_counter_file(node_num, opt_pseudo);
    printf("NODE%02d.DAT pseudo counter set to %ld\n", node_num, opt_pseudo);
}

/* ------------------------------------------------------------ */
/* message / caller maintenance                                 */
/* ------------------------------------------------------------ */

static void renumber_message_base(new_low)
long new_low;
{
    long recno;
    long next_no;
    MSGHEAD h;
    USRDESC u;

    if (new_low < 1L)
        return;

    if (!open_main_datafiles())
    {
        puts("Can't open datafiles");
        return;
    }

    next_no = new_low;
    recno = data_first_msg(&h);
    while (recno >= 0L)
    {
        h.number = next_no++;
        data_write_msghead(recno, &h);
        recno = data_next_msg(recno, &h);
    }

    recno = data_first_user(&u);
    while (recno >= 0L)
    {
        u.high_msg = 0L;
        data_write_user(recno, &u);
        recno = data_next_user(recno, &u);
    }

    close_main_datafiles();
    printf("Message base renumbered from %ld\n", new_low);
}

static void set_highest_caller_number(new_no)
long new_no;
{
    printf("Highest caller number set request: %ld\n", new_no);
    puts("Persistent caller-number header field not yet reconstructed");
}

/* ------------------------------------------------------------ */
/* interactive main                                             */
/* ------------------------------------------------------------ */

static void interactive_main(void)
{
    if (yesno_prompt("Generate parameter file? ", 0))
        interactive_parameter_file();

    if (yesno_prompt("Generate main BBS files? ", 0))
        create_main_bbs_files();

    if (yesno_prompt("Initialize BBS node file? ", 0))
        interactive_create_node_file();
}

int main(argc, argv)
int argc;
char *argv[];
{
    parse_args(argc, argv);

    puts("BBSINIT - BBS-PC! Initialization - 4.20");
    puts("Copyright (c) 1985,86,87 Micro-Systems Software Inc.");
    puts("");

    if (opt_update_ud)
        interactive_update_ud_paths();

    if (opt_msg_base >= 0L)
        renumber_message_base(opt_msg_base);

    if (opt_call_no >= 0L)
        set_highest_caller_number(opt_call_no);

    if (opt_pseudo >= 0L)
        apply_pseudo_counter_change();

    if (!opt_update_ud && opt_msg_base < 0L && opt_call_no < 0L && opt_pseudo < 0L)
        interactive_main();

    return 0;
}