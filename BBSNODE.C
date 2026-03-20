/* BBSNODE.C
 *
 * BBS-PC ! 4.21
 *
 * Reconstructed and modernised source
 * derived from BBS-PC 4.20
 *
 * Node status handling.
 *
 * This pass makes node number drive:
 *   node 1 -> NODE01.DAT
 *   node 2 -> NODE02.DAT
 *
 * The on-disk node file uses NODEREC.
 * Runtime code continues to use NODEINFO.
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

static int node_selected_number(void)
{
    int node_no;

    node_no = g_sess.node + 1;
    if (node_no < 1)
        node_no = 1;
    if (node_no > 99)
        node_no = 99;

    return node_no;
}

static void node_make_filename(out, node_no)
char *out;
int node_no;
{
    sprintf(out, "NODE%02d.DAT", node_no);
}

static void node_make_path(out, node_no)
char *out;
int node_no;
{
    char fname[32];

    node_make_filename(fname, node_no);
    data_make_data_path(out, fname);
}

static void nodeinfo_to_noderec(dst, src)
NODEREC *dst;
NODEINFO *src;
{
    memset(dst, 0, sizeof(*dst));

    dst->status     = (byte)src->status;
    dst->wantchat   = (byte)src->wantchat;
    dst->baud       = (ushort)((g_modem.baud > 0) ? g_modem.baud : src->baud);
    dst->page_sysop = (byte)src->page_sysop;

    strncpy(dst->user_name, src->user_name, DISK_NAME_LEN);
    dst->user_name[DISK_NAME_LEN] = 0;

    strncpy(dst->user_city, src->user_city, DISK_CITY_LEN);
    dst->user_city[DISK_CITY_LEN] = 0;

    strncpy(dst->current_menu, src->current_menu, NODE_MENU_LEN);
    dst->current_menu[NODE_MENU_LEN] = 0;
}

static void noderec_to_nodeinfo(dst, src)
NODEINFO *dst;
NODEREC *src;
{
    memset(dst, 0, sizeof(*dst));

    dst->status     = (int)src->status;
    dst->wantchat   = (int)src->wantchat;
    dst->baud       = (int)src->baud;
    dst->page_sysop = (int)src->page_sysop;

    strncpy(dst->user_name, src->user_name, NAME_LEN);
    dst->user_name[NAME_LEN] = 0;

    strncpy(dst->user_city, src->user_city, CITY_LEN);
    dst->user_city[CITY_LEN] = 0;

    strncpy(dst->current_menu, src->current_menu, NODE_MENU_LEN);
    dst->current_menu[NODE_MENU_LEN] = 0;
}

static int node_open_selected(fp, mode)
FILE **fp;
char *mode;
{
    char path[MAX_PATHNAME * 2];

    node_make_path(path, node_selected_number());
    *fp = fopen(path, mode);
    return (*fp != NULL);
}

static int node_read_selected(rec)
NODEREC *rec;
{
    FILE *fp;
    char hdr[HDRLEN];

    if (!rec)
        return 0;

    if (!node_open_selected(&fp, "rb"))
        return 0;

    if (fread(hdr, sizeof(hdr), 1, fp) != 1)
    {
        fclose(fp);
        return 0;
    }

    if (fread(rec, sizeof(*rec), 1, fp) != 1)
    {
        fclose(fp);
        return 0;
    }

    fclose(fp);
    return 1;
}

static int node_write_selected(rec)
NODEREC *rec;
{
    FILE *fp;
    char hdr[HDRLEN];

    if (!rec)
        return 0;

    if (!node_open_selected(&fp, "r+b"))
    {
        if (!node_create_file(node_selected_number()))
            return 0;

        if (!node_open_selected(&fp, "r+b"))
            return 0;
    }

    memset(hdr, 0, sizeof(hdr));

    if (fwrite(hdr, sizeof(hdr), 1, fp) != 1)
    {
        fclose(fp);
        return 0;
    }

    if (fwrite(rec, sizeof(*rec), 1, fp) != 1)
    {
        fclose(fp);
        return 0;
    }

    fflush(fp);
    fclose(fp);
    return 1;
}

static void node_sync_runtime_from_session(void)
{
    g_node.wantchat = g_node.wantchat ? 1 : 0;
    g_node.page_sysop = g_sess.page_sysop ? 1 : 0;
    g_node.baud = (g_modem.baud > 0) ? g_modem.baud : 0;

    if (g_sess.logged_in)
    {
        g_node.status = NODE_STATUS_LOGGED_IN;

        strncpy(g_node.user_name, g_sess.user.name, NAME_LEN);
        g_node.user_name[NAME_LEN] = 0;

        strncpy(g_node.user_city, g_sess.user.city, CITY_LEN);
        g_node.user_city[CITY_LEN] = 0;

        if (g_sess.mstack.sp > 0)
        {
            strncpy(g_node.current_menu,
                    g_sess.mstack.menu[g_sess.mstack.sp - 1].filename,
                    NODE_MENU_LEN);
            g_node.current_menu[NODE_MENU_LEN] = 0;
        }
        else
            g_node.current_menu[0] = 0;
    }
    else
    {
        g_node.status = NODE_STATUS_WAITING;
        g_node.user_name[0] = 0;
        g_node.user_city[0] = 0;
        g_node.current_menu[0] = 0;
    }
}

static void node_flush_selected(void)
{
    NODEREC rec;

    node_sync_runtime_from_session();
    nodeinfo_to_noderec(&rec, &g_node);
    (void)node_write_selected(&rec);
}

/* ------------------------------------------------------------ */
/* public node file helpers                                     */
/* ------------------------------------------------------------ */

int node_create_file(node_no)
int node_no;
{
    char path[MAX_PATHNAME * 2];
    FILE *fp;
    char hdr[HDRLEN];
    NODEREC rec;

    node_make_path(path, node_no);

    fp = fopen(path, "rb");
    if (fp)
    {
        fclose(fp);
        return 1;
    }

    fp = fopen(path, "w+b");
    if (!fp)
        return 0;

    memset(hdr, 0, sizeof(hdr));
    memset(&rec, 0, sizeof(rec));

    rec.status = NODE_STATUS_WAITING;
    rec.wantchat = 0;
    rec.baud = 0;
    rec.page_sysop = 0;

    if (fwrite(hdr, sizeof(hdr), 1, fp) != 1)
    {
        fclose(fp);
        return 0;
    }

    if (fwrite(&rec, sizeof(rec), 1, fp) != 1)
    {
        fclose(fp);
        return 0;
    }

    fflush(fp);
    fclose(fp);
    return 1;
}

int node_set_pseudo_counter_file(node_no, value)
int node_no;
long value;
{
    char path[MAX_PATHNAME * 2];
    FILE *fp;

    node_make_path(path, node_no);

    fp = fopen(path, "ab");
    if (!fp)
        return 0;

    fprintf(fp, "COUNTER=%ld\n", value);
    fclose(fp);
    return 1;
}

/* ------------------------------------------------------------ */
/* runtime node state                                           */
/* ------------------------------------------------------------ */

void node_startup_init(void)
{
    NODEREC rec;

    memset(&g_node, 0, sizeof(g_node));
    g_node.status = NODE_STATUS_WAITING;

    (void)node_create_file(node_selected_number());

    if (node_read_selected(&rec))
        noderec_to_nodeinfo(&g_node, &rec);

    g_node.status = NODE_STATUS_WAITING;
    g_node.wantchat = 0;
    g_node.page_sysop = 0;
    g_node.baud = (g_modem.baud > 0) ? g_modem.baud : 0;
    g_node.user_name[0] = 0;
    g_node.user_city[0] = 0;
    g_node.current_menu[0] = 0;

    node_flush_selected();
}

void node_session_begin(void)
{
    g_node.status = NODE_STATUS_LOGGED_IN;
    g_node.wantchat = g_node.wantchat ? 1 : 0;
    g_node.page_sysop = 0;
    g_node.baud = (g_modem.baud > 0) ? g_modem.baud : 0;

    strncpy(g_node.user_name, g_sess.user.name, NAME_LEN);
    g_node.user_name[NAME_LEN] = 0;

    strncpy(g_node.user_city, g_sess.user.city, CITY_LEN);
    g_node.user_city[CITY_LEN] = 0;

    if (g_sess.mstack.sp > 0)
    {
        strncpy(g_node.current_menu,
                g_sess.mstack.menu[g_sess.mstack.sp - 1].filename,
                NODE_MENU_LEN);
        g_node.current_menu[NODE_MENU_LEN] = 0;
    }
    else
        g_node.current_menu[0] = 0;

    node_flush_selected();
}

void node_session_end(void)
{
    g_node.status = NODE_STATUS_WAITING;
    g_node.page_sysop = 0;
    g_node.baud = (g_modem.baud > 0) ? g_modem.baud : 0;
    g_node.user_name[0] = 0;
    g_node.user_city[0] = 0;
    g_node.current_menu[0] = 0;

    node_flush_selected();
}

void node_mark_menu(fname)
char *fname;
{
    if (!fname)
        fname = "";

    strncpy(g_node.current_menu, fname, NODE_MENU_LEN);
    g_node.current_menu[NODE_MENU_LEN] = 0;

    if (g_sess.logged_in)
        g_node.status = NODE_STATUS_LOGGED_IN;

    node_flush_selected();
}

void node_mark_message_activity(void)
{
    if (g_sess.logged_in)
        g_node.status = NODE_STATUS_BUSY;

    node_flush_selected();

    if (g_sess.logged_in)
    {
        g_node.status = NODE_STATUS_LOGGED_IN;
        node_flush_selected();
    }
}

void node_mark_file_activity(void)
{
    if (g_sess.logged_in)
        g_node.status = NODE_STATUS_BUSY;

    node_flush_selected();

    if (g_sess.logged_in)
    {
        g_node.status = NODE_STATUS_LOGGED_IN;
        node_flush_selected();
    }
}

void node_mark_user_activity(void)
{
    if (g_sess.logged_in)
        g_node.status = NODE_STATUS_BUSY;

    node_flush_selected();

    if (g_sess.logged_in)
    {
        g_node.status = NODE_STATUS_LOGGED_IN;
        node_flush_selected();
    }
}

void node_set_chat(on)
int on;
{
    g_node.wantchat = on ? 1 : 0;
    node_flush_selected();
}

void do_change_node_defaults(void)
{
    puts("Node defaults editor not yet expanded");
}