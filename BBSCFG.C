/* BBSCFG.C
 *
 * Configuration/path handling
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "bbsdata.h"
#include "bbsfunc.h"

void make_data_path(dst, dir, name)
char *dst, *dir, *name;
{
    dst[0] = 0;

    if (dir && *dir)
    {
        strcpy(dst, dir);
        if (dst[strlen(dst) - 1] != '\\' && dst[strlen(dst) - 1] != '/')
            strcat(dst, "\\");
    }

    strcat(dst, name);
}

int load_bbs_paths(fname)
char *fname;
{
    FILE *fp;
    char line[128];
    int section = 0;

    fp = fopen(fname, "rt");
    if (!fp)
        return 0;

    while (fgets(line, sizeof(line), fp))
    {
        char *p;
        char *key;
        char *val;

        p = strchr(line, '\n');
        if (p) *p = 0;
        p = strchr(line, '\r');
        if (p) *p = 0;

        if (!line[0])
            continue;

        p = strchr(line, '=');
        if (!p)
            continue;

        *p = 0;
        key = line;
        val = p + 1;

        while (*key == ' ' || *key == '\t') key++;
        while (*val == ' ' || *val == '\t') val++;

        if (!stricmp(key, "MSGPATH"))
            strncpy(g_paths.msg_path, val, sizeof(g_paths.msg_path)-1);
        else if (!stricmp(key, "USERPATH"))
            strncpy(g_paths.usr_path, val, sizeof(g_paths.usr_path)-1);
        else if (!stricmp(key, "UDPATH"))
            strncpy(g_paths.ud_path, val, sizeof(g_paths.ud_path)-1);
        else if (!stricmp(key, "LOGPATH"))
            strncpy(g_paths.log_path, val, sizeof(g_paths.log_path)-1);
        else if (!strnicmp(key, "UPDN", 4))
        {
            section = atoi(key + 4);
            if (section >= 0 && section < NUM_SECT)
                strncpy(g_paths.updn_path[section], val,
                    sizeof(g_paths.updn_path[section])-1);
        }
    }

    fclose(fp);
    return 1;
}

int load_cfginfo(fname)
char *fname;
{
    FILE *fp;

    fp = fopen(fname, "rb");
    if (!fp)
        return 0;

    memset(&g_cfg, 0, sizeof(g_cfg));

    fread(&g_cfg, 1,
        sizeof(g_cfg) < 2048 ? sizeof(g_cfg) : 2048,
        fp);

    fclose(fp);
    return 1;
}

int open_main_datafiles(void)
{
    char path[MAX_PATHNAME];

    make_data_path(path, g_paths.msg_path, "MSGHEAD.DAT");
    g_fp_msghead = fopen(path, "rb+");
    if (!g_fp_msghead)
        g_fp_msghead = fopen(path, "rb");

    make_data_path(path, g_paths.msg_path, "MSGTEXT.DAT");
    g_fp_msgtext = fopen(path, "rb+");
    if (!g_fp_msgtext)
        g_fp_msgtext = fopen(path, "rb");

    make_data_path(path, g_paths.usr_path, "USERDESC.DAT");
    g_fp_userdesc = fopen(path, "rb+");
    if (!g_fp_userdesc)
        g_fp_userdesc = fopen(path, "rb");

    make_data_path(path, g_paths.ud_path, "UDHEAD.DAT");
    g_fp_udhead = fopen(path, "rb+");
    if (!g_fp_udhead)
        g_fp_udhead = fopen(path, "rb");

    make_data_path(path, g_paths.log_path, "CALLER.DAT");
    g_fp_caller = fopen(path, "rb+");
    if (!g_fp_caller)
        g_fp_caller = fopen(path, "rb");

    return 1;
}

void close_main_datafiles(void)
{
    if (g_fp_msghead)  fclose(g_fp_msghead);
    if (g_fp_msgtext)  fclose(g_fp_msgtext);
    if (g_fp_userdesc) fclose(g_fp_userdesc);
    if (g_fp_udhead)   fclose(g_fp_udhead);
    if (g_fp_caller)   fclose(g_fp_caller);

    g_fp_msghead  = NULL;
    g_fp_msgtext  = NULL;
    g_fp_userdesc = NULL;
    g_fp_udhead   = NULL;
    g_fp_caller   = NULL;
}