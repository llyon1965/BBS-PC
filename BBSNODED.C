/* BBSNODED.C
 *
 * BBS-PC! 4.21
 *
 * Node editor / inspector
 *
 * DOS 8.3 rename of older BBSNODEED.C-style functionality.
 *
 * Works directly with:
 *   NODE01.DAT
 *   NODE02.DAT
 *   ...
 *
 * using the on-disk NODEREC layout.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bbsdata.h"
#include "bbsfunc.h"

#ifndef NODE_STATUS_WAITING
#define NODE_STATUS_WAITING   0
#endif

#ifndef NODE_STATUS_LOGGED_IN
#define NODE_STATUS_LOGGED_IN 1
#endif

#ifndef NODE_STATUS_BUSY
#define NODE_STATUS_BUSY      2
#endif

/* ------------------------------------------------------------ */
/* local helpers                                                */
/* ------------------------------------------------------------ */

static void noded_make_filename(out, node_no)
char *out;
int node_no;
{
    sprintf(out, "NODE%02d.DAT", node_no);
}

static void noded_make_path(out, node_no)
char *out;
int node_no;
{
    char fname[32];

    noded_make_filename(fname, node_no);
    data_make_data_path(out, fname);
}

static int noded_valid_node_number(node_no)
int node_no;
{
    if (node_no < 1)
        return 0;

    if (g_cfg.max_nodes > 0 && node_no > g_cfg.max_nodes)
        return 0;

    if (node_no > 99)
        return 0;

    return 1;
}

static void noded_zero_record(r)
NODEREC *r;
{
    memset(r, 0, sizeof(*r));
    r->status = NODE_STATUS_WAITING;
}

static int noded_ensure_file(node_no)
int node_no;
{
    return node_create_file(node_no);
}

static int noded_read_record(node_no, r)
int node_no;
NODEREC *r;
{
    FILE *fp;
    char path[MAX_PATHNAME * 2];
    char hdr[HDRLEN];

    if (!noded_valid_node_number(node_no) || !r)
        return 0;

    if (!noded_ensure_file(node_no))
        return 0;

    noded_make_path(path, node_no);

    fp = fopen(path, "rb");
    if (!fp)
        return 0;

    if (fread(hdr, sizeof(hdr), 1, fp) != 1)
    {
        fclose(fp);
        return 0;
    }

    if (fread(r, sizeof(*r), 1, fp) != 1)
    {
        fclose(fp);
        return 0;
    }

    fclose(fp);
    return 1;
}

static int noded_write_record(node_no, r)
int node_no;
NODEREC *r;
{
    FILE *fp;
    char path[MAX_PATHNAME * 2];
    char hdr[HDRLEN];

    if (!noded_valid_node_number(node_no) || !r)
        return 0;

    if (!noded_ensure_file(node_no))
        return 0;

    noded_make_path(path, node_no);

    fp = fopen(path, "r+b");
    if (!fp)
        return 0;

    memset(hdr, 0, sizeof(hdr));

    if (fwrite(hdr, sizeof(hdr), 1, fp) != 1)
    {
        fclose(fp);
        return 0;
    }

    if (fwrite(r, sizeof(*r), 1, fp) != 1)
    {
        fclose(fp);
        return 0;
    }

    fflush(fp);
    fclose(fp);
    return 1;
}

static char *noded_status_text(v)
int v;
{
    switch (v)
    {
        case NODE_STATUS_WAITING:   return "Waiting";
        case NODE_STATUS_LOGGED_IN: return "Logged In";
        case NODE_STATUS_BUSY:      return "Busy";
    }

    return "Unknown";
}

static void noded_show_record(node_no, r)
int node_no;
NODEREC *r;
{
    printf("\n");
    printf("Node #%d\n", node_no);
    printf("-------\n");
    printf("Status     : %u (%s)\n", (unsigned)r->status, noded_status_text(r->status));
    printf("Want Chat  : %u\n", (unsigned)r->wantchat);
    printf("Baud       : %u\n", (unsigned)r->baud);
    printf("Page Sysop : %u\n", (unsigned)r->page_sysop);
    printf("User Name  : %s\n", r->user_name[0] ? r->user_name : "(none)");
    printf("User City  : %s\n", r->user_city[0] ? r->user_city : "(none)");
    printf("Menu       : %s\n", r->current_menu[0] ? r->current_menu : "(none)");
    printf("\n");
}

static int noded_prompt_node_number(void)
{
    char line[16];
    int node_no;

    term_getline("Node number: ", line, sizeof(line));
    data_trim_crlf(line);

    node_no = atoi(line);
    return node_no;
}

static int noded_prompt_numeric(prompt, current)
char *prompt;
int current;
{
    char line[32];

    term_getline(prompt, line, sizeof(line));
    data_trim_crlf(line);

    if (!line[0])
        return current;

    return atoi(line);
}

static void noded_prompt_string(prompt, dst, maxlen)
char *prompt;
char *dst;
int maxlen;
{
    char line[128];

    term_getline(prompt, line, sizeof(line));
    data_trim_crlf(line);

    if (!line[0])
        return;

    strncpy(dst, line, maxlen);
    dst[maxlen] = 0;
}

static void noded_edit_record(r)
NODEREC *r;
{
    r->status =
        (byte)noded_prompt_numeric("Status (0=Waiting,1=Logged In,2=Busy): ",
                                   (int)r->status);

    r->wantchat =
        (byte)noded_prompt_numeric("Want chat (0/1): ",
                                   (int)r->wantchat);

    r->baud =
        (ushort)noded_prompt_numeric("Baud: ",
                                     (int)r->baud);

    r->page_sysop =
        (byte)noded_prompt_numeric("Page sysop (0/1): ",
                                   (int)r->page_sysop);

    noded_prompt_string("User name: ", r->user_name, DISK_NAME_LEN);
    noded_prompt_string("User city: ", r->user_city, DISK_CITY_LEN);
    noded_prompt_string("Current menu: ", r->current_menu, NODE_MENU_LEN);
}

static void noded_reset_record(r)
NODEREC *r;
{
    noded_zero_record(r);
}

/* ------------------------------------------------------------ */
/* public entry point                                           */
/* ------------------------------------------------------------ */

void do_node_editor(void)
{
    int node_no;
    NODEREC r;
    char line[8];

    node_no = noded_prompt_node_number();
    if (!noded_valid_node_number(node_no))
    {
        puts("Invalid node number");
        return;
    }

    if (!noded_read_record(node_no, &r))
    {
        puts("Unable to read node file");
        return;
    }

    for (;;)
    {
        noded_show_record(node_no, &r);

        puts("E) Edit node");
        puts("R) Reset node to waiting");
        puts("S) Save changes");
        puts("Q) Quit");
        puts("");

        term_getline("Choice: ", line, sizeof(line));
        data_trim_crlf(line);

        switch (toupper((unsigned char)line[0]))
        {
            case 'E':
                noded_edit_record(&r);
                break;

            case 'R':
                noded_reset_record(&r);
                break;

            case 'S':
                if (!noded_write_record(node_no, &r))
                    puts("Save failed");
                else
                    puts("Node file updated");
                break;

            case 'Q':
                return;
        }
    }
}