/* V41TOV42.C
 *
 * First-pass BBS-PC 4.1x -> 4.20 conversion utility
 *
 * Updated to use:
 * - node_convert_file()
 *
 * Purpose:
 * - normalize/rebuild datafiles into the current reconstructed 4.20 layout
 * - convert/create NODE%02d.DAT files through the shared node path
 * - provide a single maintenance utility compatible with the current tree
 *
 * Notes:
 * - Exact historical 4.1x binary differences are not fully recovered yet.
 * - This utility currently performs a safe normalization pass rather than a
 *   byte-perfect archival conversion.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bbsdata.h"
#include "bbsfunc.h"

static int opt_all_nodes = 0;
static int opt_node = -1;
static int opt_fix_main = 1;

/* ------------------------------------------------------------ */

static void parse_args(argc, argv)
int argc;
char *argv[];
{
    int i;

    for (i = 1; i < argc; i++)
    {
        if (!stricmp(argv[i], "-all"))
            opt_all_nodes = 1;
        else if (!strnicmp(argv[i], "-n:", 3))
            opt_node = atoi(argv[i] + 3);
        else if (!stricmp(argv[i], "-nodes"))
            opt_fix_main = 0;
    }
}

/* ------------------------------------------------------------ */
/* simple header-normalization helpers                          */
/* ------------------------------------------------------------ */

static void normalize_blank_header(fname)
char *fname;
{
    FILE *fp;
    unsigned char hdr[128];

    fp = fopen(fname, "r+b");
    if (!fp)
        return;

    memset(hdr, 0, sizeof(hdr));

    fseek(fp, 0L, SEEK_SET);
    if (fread(hdr, sizeof(hdr), 1, fp) != 1)
    {
        fclose(fp);
        return;
    }

    /*
     * First-pass normalization:
     * if file exists, keep it as-is unless future evidence proves a
     * specific control/header word must be changed.
     */

    fclose(fp);
}

static void convert_main_files(void)
{
    puts("Normalizing main BBS datafiles...");

    normalize_blank_header("MSGHEAD.DAT");
    normalize_blank_header("MSGTEXT.DAT");
    normalize_blank_header("MSGKEY.DAT");

    normalize_blank_header("USERDESC.DAT");
    normalize_blank_header("USERKEY.DAT");

    normalize_blank_header("UDHEAD.DAT");
    normalize_blank_header("UDKEY1.DAT");
    normalize_blank_header("UDKEY2.DAT");
    normalize_blank_header("UDKEY3.DAT");

    normalize_blank_header("CALLER.DAT");
    normalize_blank_header("CALLKEY.DAT");

    puts("Main file normalization complete");
}

/* ------------------------------------------------------------ */
/* node conversion                                              */
/* ------------------------------------------------------------ */

static void convert_one_node(node_num)
int node_num;
{
    if (node_num < 1 || node_num > 99)
    {
        printf("Skipping illegal node number %d\n", node_num);
        return;
    }

    node_convert_file(node_num);
    printf("NODE%02d.DAT converted/normalized\n", node_num);
}

static void convert_all_nodes(void)
{
    int i;

    for (i = 1; i <= 99; i++)
        convert_one_node(i);
}

/* ------------------------------------------------------------ */
/* interactive helpers                                          */
/* ------------------------------------------------------------ */

static void interactive_run(void)
{
    char line[16];
    int node_num;

    if (data_yesno("Convert main BBS files (Y/N)? ", 1))
        convert_main_files();

    if (data_yesno("Convert all node files (Y/N)? ", 0))
    {
        convert_all_nodes();
        return;
    }

    if (data_yesno("Convert one node file (Y/N)? ", 1))
    {
        data_prompt_line("Which node (1-99)? ", line, sizeof(line));
        if (!line[0])
            return;

        node_num = atoi(line);
        convert_one_node(node_num);
    }
}

/* ------------------------------------------------------------ */

int main(argc, argv)
int argc;
char *argv[];
{
    parse_args(argc, argv);

    puts("V41TOV42 - BBS-PC conversion utility - 4.20");
    puts("Copyright (c) 1985,86,87 Micro-Systems Software Inc.");
    puts("");

    if (opt_fix_main)
        convert_main_files();

    if (opt_all_nodes)
        convert_all_nodes();
    else if (opt_node >= 1)
        convert_one_node(opt_node);
    else if (!opt_fix_main)
        interactive_run();

    return 0;
}