/* BBSINIT.C
 *
 * BBS-PC ! 4.21
 *
 * Reconstructed and modernised source
 * derived from BBS-PC 4.20
 *
 * Initial configuration utility.
 *
 * Ownership added here:
 * - writes legacy configuration files
 * - writes BBSPATHS.CFG at the same time
 * - creates core fixed-record data files with safe headers
 * - initialises node files
 *
 * Notes:
 * - BBSPATHS.CFG is treated as the modern path-ownership file for the
 *   reconstructed tree.
 * - Legacy packed/on-disk formats are preserved where required.
 * - This utility now owns generation of BBSPATHS.CFG rather than leaving
 *   it as an implicit external dependency.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "bbsdata.h"
#include "bbsfunc.h"

#ifndef BBS_VERSION
#define BBS_VERSION "BBS-PC ! 4.21"
#endif

#ifndef HDRLEN
#define HDRLEN 128L
#endif

#ifndef MAX_PATHNAME
#define MAX_PATHNAME 128
#endif

/* ------------------------------------------------------------ */
/* local helpers                                                */
/* ------------------------------------------------------------ */

static void init_zero_paths(paths)
BBSPATHS *paths;
{
    memset(paths, 0, sizeof(*paths));
}

static void init_zero_cfg(cfg)
CFGINFO *cfg;
{
    memset(cfg, 0, sizeof(*cfg));
}

static void trim_crlf(s)
char *s;
{
    while (*s)
    {
        if (*s == '\r' || *s == '\n')
        {
            *s = 0;
            return;
        }
        s++;
    }
}

static void ensure_trailing_slash(path)
char *path;
{
    int n;

    if (!path || !path[0])
        return;

    n = strlen(path);
    if (n <= 0)
        return;

    if (path[n - 1] != '\\' && path[n - 1] != '/')
        strcat(path, "\\");
}

static void join_path(dst, dir, name)
char *dst;
char *dir;
char *name;
{
    strcpy(dst, dir);
    ensure_trailing_slash(dst);
    strcat(dst, name);
}

static int prompt_yesno(prompt, def_yes)
char *prompt;
int def_yes;
{
    char line[16];

    printf("%s", prompt);
    if (!fgets(line, sizeof(line), stdin))
        return def_yes;

    trim_crlf(line);
    if (!line[0])
        return def_yes;

    return (line[0] == 'Y' || line[0] == 'y');
}

static void prompt_line(prompt, out, outlen, defval)
char *prompt;
char *out;
int outlen;
char *defval;
{
    char line[256];

    printf("%s", prompt);
    if (defval && defval[0])
        printf(" [%s]", defval);
    printf(": ");

    if (!fgets(line, sizeof(line), stdin))
    {
        if (defval)
            strncpy(out, defval, outlen - 1);
        else
            out[0] = 0;
        out[outlen - 1] = 0;
        return;
    }

    trim_crlf(line);
    if (!line[0] && defval)
        strncpy(out, defval, outlen - 1);
    else
        strncpy(out, line, outlen - 1);

    out[outlen - 1] = 0;
}

static void default_section_paths(paths, base)
BBSPATHS *paths;
char *base;
{
    int i;
    char tmp[MAX_PATHNAME];

    for (i = 0; i < NUM_SECT; i++)
    {
        sprintf(tmp, "%sUPDN%02d", base, i);
        strncpy(paths->updn_path[i], tmp, sizeof(paths->updn_path[i]) - 1);
        paths->updn_path[i][sizeof(paths->updn_path[i]) - 1] = 0;
        ensure_trailing_slash(paths->updn_path[i]);
    }
}

static void init_default_paths(paths)
BBSPATHS *paths;
{
    init_zero_paths(paths);

    strcpy(paths->msg_path,  "MSG");
    strcpy(paths->usr_path,  "USER");
    strcpy(paths->ud_path,   "UD");
    strcpy(paths->log_path,  "LOG");

    ensure_trailing_slash(paths->msg_path);
    ensure_trailing_slash(paths->usr_path);
    ensure_trailing_slash(paths->ud_path);
    ensure_trailing_slash(paths->log_path);

    default_section_paths(paths, "");
}

static void init_default_cfg(cfg)
CFGINFO *cfg;
{
    init_zero_cfg(cfg);

    /* Conservative defaults.
     * Only fields known to exist in the reconstructed tree should be
     * touched here.
     */
    strncpy(cfg->bbsname, "BBS-PC ! 4.21", sizeof(cfg->bbsname) - 1);
    cfg->bbsname[sizeof(cfg->bbsname) - 1] = 0;

    strncpy(cfg->sysopname, "SYSOP", sizeof(cfg->sysopname) - 1);
    cfg->sysopname[sizeof(cfg->sysopname) - 1] = 0;

    cfg->node = 0;
    cfg->min_baud = 300;
    cfg->max_baud = 2400;
    cfg->page_len = 24;
}

static int make_dir_if_needed(path)
char *path;
{
    char cmd[256];

    if (!path || !path[0])
        return 1;

#if defined(__TURBOC__) || defined(__BORLANDC__) || defined(_MSC_VER)
    sprintf(cmd, "IF NOT EXIST %s MD %s", path, path);
    return system(cmd) == 0;
#else
    sprintf(cmd, "mkdir -p %s", path);
    return system(cmd) == 0;
#endif
}

static int ensure_runtime_dirs(paths)
BBSPATHS *paths;
{
    int i;
    int ok = 1;
    char tmp[MAX_PATHNAME];

    strcpy(tmp, paths->msg_path);
    if (tmp[0] && (tmp[strlen(tmp) - 1] == '\\' || tmp[strlen(tmp) - 1] == '/'))
        tmp[strlen(tmp) - 1] = 0;
    ok &= make_dir_if_needed(tmp);

    strcpy(tmp, paths->usr_path);
    if (tmp[0] && (tmp[strlen(tmp) - 1] == '\\' || tmp[strlen(tmp) - 1] == '/'))
        tmp[strlen(tmp) - 1] = 0;
    ok &= make_dir_if_needed(tmp);

    strcpy(tmp, paths->ud_path);
    if (tmp[0] && (tmp[strlen(tmp) - 1] == '\\' || tmp[strlen(tmp) - 1] == '/'))
        tmp[strlen(tmp) - 1] = 0;
    ok &= make_dir_if_needed(tmp);

    strcpy(tmp, paths->log_path);
    if (tmp[0] && (tmp[strlen(tmp) - 1] == '\\' || tmp[strlen(tmp) - 1] == '/'))
        tmp[strlen(tmp) - 1] = 0;
    ok &= make_dir_if_needed(tmp);

    for (i = 0; i < NUM_SECT; i++)
    {
        strcpy(tmp, paths->updn_path[i]);
        if (tmp[0] && (tmp[strlen(tmp) - 1] == '\\' || tmp[strlen(tmp) - 1] == '/'))
            tmp[strlen(tmp) - 1] = 0;
        ok &= make_dir_if_needed(tmp);
    }

    return ok;
}

static int write_cfginfo_dat(cfg)
CFGINFO *cfg;
{
    FILE *fp;

    fp = fopen("CFGINFO.DAT", "wb");
    if (!fp)
        return 0;

    if (fwrite(cfg, sizeof(*cfg), 1, fp) != 1)
    {
        fclose(fp);
        return 0;
    }

    fclose(fp);
    return 1;
}

static int write_bbspaths_cfg(paths)
BBSPATHS *paths;
{
    FILE *fp;
    int i;

    fp = fopen("BBSPATHS.CFG", "wt");
    if (!fp)
        return 0;

    fprintf(fp, "MSG=%s\n", paths->msg_path);
    fprintf(fp, "USER=%s\n", paths->usr_path);
    fprintf(fp, "UD=%s\n", paths->ud_path);
    fprintf(fp, "LOG=%s\n", paths->log_path);

    for (i = 0; i < NUM_SECT; i++)
        fprintf(fp, "UPDN%02d=%s\n", i, paths->updn_path[i]);

    fclose(fp);
    return 1;
}

static int create_fixed_file(path, reclen)
char *path;
int reclen;
{
    FILE *fp;
    unsigned char hdr[HDRLEN];

    fp = fopen(path, "rb");
    if (fp)
    {
        fclose(fp);
        return 1;
    }

    fp = fopen(path, "wb");
    if (!fp)
        return 0;

    memset(hdr, 0, sizeof(hdr));
    hdr[0] = (unsigned char)(reclen & 0xFF);
    hdr[1] = (unsigned char)((reclen >> 8) & 0xFF);

    if (fwrite(hdr, 1, sizeof(hdr), fp) != sizeof(hdr))
    {
        fclose(fp);
        return 0;
    }

    fclose(fp);
    return 1;
}

static int create_core_datafiles(paths)
BBSPATHS *paths;
{
    char path[MAX_PATHNAME];
    int ok = 1;

    join_path(path, paths->msg_path, "MSGHEAD.DAT");
    ok &= create_fixed_file(path, sizeof(MSGHEAD));

    join_path(path, paths->msg_path, "MSGTEXT.DAT");
    ok &= create_fixed_file(path, sizeof(MSGTEXT));

    join_path(path, paths->usr_path, "USERDESC.DAT");
    ok &= create_fixed_file(path, sizeof(USRDESC));

    join_path(path, paths->ud_path, "UDHEAD.DAT");
    ok &= create_fixed_file(path, sizeof(UDHEAD));

    join_path(path, paths->log_path, "CALLER.DAT");
    ok &= create_fixed_file(path, sizeof(USRLOG));

    return ok;
}

static int create_initial_node_files(cfg)
CFGINFO *cfg;
{
    int i;
    int max_nodes;

    max_nodes = cfg->max_nodes;
    if (max_nodes <= 0)
        max_nodes = 1;
    if (max_nodes > 99)
        max_nodes = 99;

    for (i = 0; i < max_nodes; i++)
    {
        node_create_file(i);
        node_set_pseudo_counter_file(i, 0L);
    }

    return 1;
}

static void prompt_paths(paths)
BBSPATHS *paths;
{
    char tmp[MAX_PATHNAME];
    int i;
    char prompt[64];

    strcpy(tmp, paths->msg_path);
    prompt_line("Message path", paths->msg_path, sizeof(paths->msg_path), tmp);
    ensure_trailing_slash(paths->msg_path);

    strcpy(tmp, paths->usr_path);
    prompt_line("User path", paths->usr_path, sizeof(paths->usr_path), tmp);
    ensure_trailing_slash(paths->usr_path);

    strcpy(tmp, paths->ud_path);
    prompt_line("Upload/download data path", paths->ud_path, sizeof(paths->ud_path), tmp);
    ensure_trailing_slash(paths->ud_path);

    strcpy(tmp, paths->log_path);
    prompt_line("Log path", paths->log_path, sizeof(paths->log_path), tmp);
    ensure_trailing_slash(paths->log_path);

    for (i = 0; i < NUM_SECT; i++)
    {
        strcpy(tmp, paths->updn_path[i]);
        sprintf(prompt, "Section %02d upload path", i);
        prompt_line(prompt, paths->updn_path[i], sizeof(paths->updn_path[i]), tmp);
        ensure_trailing_slash(paths->updn_path[i]);
    }
}

static void prompt_cfg(cfg)
CFGINFO *cfg)
{
    char tmp[64];

    prompt_line("BBS name", cfg->bbsname, sizeof(cfg->bbsname), cfg->bbsname);
    prompt_line("Sysop name", cfg->sysopname, sizeof(cfg->sysopname), cfg->sysopname);

    sprintf(tmp, "%d", cfg->max_nodes > 0 ? cfg->max_nodes : 1);
    prompt_line("Maximum nodes", tmp, sizeof(tmp), tmp);
    cfg->max_nodes = atoi(tmp);
    if (cfg->max_nodes <= 0)
        cfg->max_nodes = 1;
    if (cfg->max_nodes > 99)
        cfg->max_nodes = 99;

    sprintf(tmp, "%d", cfg->min_baud > 0 ? cfg->min_baud : 300);
    prompt_line("Minimum baud", tmp, sizeof(tmp), tmp);
    cfg->min_baud = atoi(tmp);

    sprintf(tmp, "%d", cfg->max_baud > 0 ? cfg->max_baud : 2400);
    prompt_line("Maximum baud", tmp, sizeof(tmp), tmp);
    cfg->max_baud = atoi(tmp);

    sprintf(tmp, "%d", cfg->page_len > 0 ? cfg->page_len : 24);
    prompt_line("Default page length", tmp, sizeof(tmp), tmp);
    cfg->page_len = atoi(tmp);
    if (cfg->page_len <= 0)
        cfg->page_len = 24;
}

static void show_summary(paths, cfg)
BBSPATHS *paths;
CFGINFO *cfg;
{
    int i;

    puts("");
    puts(BBS_VERSION);
    puts("Initialisation summary");
    puts("----------------------");
    printf("BBS name      : %s\n", cfg->bbsname);
    printf("Sysop name    : %s\n", cfg->sysopname);
    printf("Max nodes     : %d\n", cfg->max_nodes);
    printf("Min baud      : %d\n", cfg->min_baud);
    printf("Max baud      : %d\n", cfg->max_baud);
    printf("Page length   : %d\n", cfg->page_len);
    printf("MSG path      : %s\n", paths->msg_path);
    printf("USER path     : %s\n", paths->usr_path);
    printf("UD path       : %s\n", paths->ud_path);
    printf("LOG path      : %s\n", paths->log_path);

    for (i = 0; i < NUM_SECT; i++)
        printf("UPDN%02d        : %s\n", i, paths->updn_path[i]);

    puts("");
}

/* ------------------------------------------------------------ */
/* entry                                                        */
/* ------------------------------------------------------------ */

int main(argc, argv)
int argc;
char *argv[];
{
    BBSPATHS paths;
    CFGINFO cfg;

    (void)argc;
    (void)argv;

    puts(BBS_VERSION);
    puts("System initialisation");
    puts("");

    init_default_paths(&paths);
    init_default_cfg(&cfg);

    if (prompt_yesno("Edit default paths", 1))
        prompt_paths(&paths);

    if (prompt_yesno("Edit system configuration", 1))
        prompt_cfg(&cfg);

    show_summary(&paths, &cfg);

    if (!prompt_yesno("Write configuration files", 1))
    {
        puts("Aborted");
        return 1;
    }

    if (!ensure_runtime_dirs(&paths))
    {
        puts("Unable to create one or more runtime directories");
        return 1;
    }

    if (!write_cfginfo_dat(&cfg))
    {
        puts("Unable to write CFGINFO.DAT");
        return 1;
    }

    if (!write_bbspaths_cfg(&paths))
    {
        puts("Unable to write BBSPATHS.CFG");
        return 1;
    }

    if (!create_core_datafiles(&paths))
    {
        puts("Unable to create one or more core data files");
        return 1;
    }

    if (!create_initial_node_files(&cfg))
    {
        puts("Unable to create one or more node files");
        return 1;
    }

    puts("");
    puts("Initialisation complete");
    puts("Legacy configuration files written");
    puts("BBSPATHS.CFG written");
    puts("");

    return 0;
}