/* BBSNODE.C
 *
 * First-pass BBS-PC 4.20 node-state module
 *
 * Implements:
 * - NODE%02d.DAT open/load/save helpers
 * - basic per-node runtime state
 * - pseudo filename counter support
 * - simple node status display/update
 *
 * Notes:
 * - Exact original NODExx.DAT layout is not fully recovered yet.
 * - This module uses a conservative reconstructed node record that
 *   can be tightened later from BBS.EXE/manual evidence.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "bbsdata.h"
#include "bbsfunc.h"

#define NODE_NAME_LEN   20
#define NODE_BANNER_LEN 40
#define NODE_STATUS_LEN 40

/* ------------------------------------------------------------ */
/* reconstructed node file structure                            */
/* ------------------------------------------------------------ */

typedef struct {
    byte  active;                     /* node in use */
    byte  local;                      /* local console login */
    byte  chat;                       /* sysop chat enabled */
    byte  node_num;                   /* node number */

    longint pseudo_ctr;               /* pseudo filename counter */
    longint caller_no;                /* current caller number */

    char  caller_name[NAME_LEN + 1];  /* current caller name */
    char  status[NODE_STATUS_LEN];    /* status text */
    char  banner[NODE_BANNER_LEN];    /* optional node banner */

    ushort login_date;                /* packed login date */
    ushort login_time;                /* packed login time */

    byte  menu_set;                   /* current menu set */
    byte  term;                       /* current terminal type */
    byte  protocol;                   /* current protocol */
    byte  reserved;

    longint calls_total;              /* node-local counters */
    longint msgs_total;
    longint users_total;
    longint files_total;
} NODEREC;

static NODEREC g_node;
static char g_node_fname[NODE_NAME_LEN] = "";

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

static ushort pack_date_now(void)
{
    time_t t;
    struct tm *tmx;
    ushort y, m, d;

    t = time((time_t *)0);
    tmx = localtime(&t);
    if (!tmx)
        return 0;

    y = (ushort)((tmx->tm_year - 80) & 0x7F);
    m = (ushort)((tmx->tm_mon + 1) & 0x0F);
    d = (ushort)(tmx->tm_mday & 0x1F);

    return (ushort)((y << 9) | (m << 5) | d);
}

static ushort pack_time_now(void)
{
    time_t t;
    struct tm *tmx;
    ushort hh, mm;

    t = time((time_t *)0);
    tmx = localtime(&t);
    if (!tmx)
        return 0;

    hh = (ushort)(tmx->tm_hour & 0x1F);
    mm = (ushort)(tmx->tm_min & 0x3F);

    return (ushort)((hh << 11) | (mm << 5));
}

static void make_node_name(out, node_num)
char *out;
int node_num;
{
    sprintf(out, "NODE%02d.DAT", node_num);
}

static void node_defaults(n)
NODEREC *n;
{
    memset(n, 0, sizeof(*n));

    n->active = 0;
    n->local = 1;
    n->chat = 1;
    n->node_num = (byte)g_sess.node_num;
    n->pseudo_ctr = 0L;
    n->caller_no = 0L;
    strcpy(n->status, "Waiting for caller");
    strcpy(n->banner, "BBS-PC 4.20");
    n->login_date = 0;
    n->login_time = 0;
    n->menu_set = 0;
    n->term = 0;
    n->protocol = 0;
    n->calls_total = 0L;
    n->msgs_total = 0L;
    n->users_total = 0L;
    n->files_total = 0L;
}

/* ------------------------------------------------------------ */
/* public load/save                                             */
/* ------------------------------------------------------------ */

int node_open_current(void)
{
    FILE *fp;

    make_node_name(g_node_fname, g_sess.node_num);

    fp = fopen(g_node_fname, "rb");
    if (!fp)
    {
        node_defaults(&g_node);

        fp = fopen(g_node_fname, "wb");
        if (!fp)
            return 0;

        fwrite(&g_node, sizeof(g_node), 1, fp);
        fclose(fp);
        return 1;
    }

    memset(&g_node, 0, sizeof(g_node));
    fread(&g_node, sizeof(g_node), 1, fp);
    fclose(fp);

    if (g_node.node_num == 0)
        g_node.node_num = (byte)g_sess.node_num;

    return 1;
}

int node_save_current(void)
{
    FILE *fp;

    if (!g_node_fname[0])
        make_node_name(g_node_fname, g_sess.node_num);

    fp = fopen(g_node_fname, "wb");
    if (!fp)
        return 0;

    fwrite(&g_node, sizeof(g_node), 1, fp);
    fclose(fp);
    return 1;
}

void node_close_current(void)
{
    g_node.active = 0;
    strcpy(g_node.status, "Node idle");
    g_node.caller_name[0] = 0;
    g_node.login_date = 0;
    g_node.login_time = 0;
    g_node.caller_no = 0L;
    node_save_current();
}

/* ------------------------------------------------------------ */
/* runtime state sync                                           */
/* ------------------------------------------------------------ */

void node_mark_waiting(void)
{
    g_node.active = 0;
    g_node.local = (byte)(g_sess.local_login ? 1 : 0);
    g_node.chat = (byte)(g_sess.sysop_chat ? 1 : 0);
    g_node.node_num = (byte)g_sess.node_num;
    strcpy(g_node.status, "Waiting for caller");
    g_node.caller_name[0] = 0;
    g_node.login_date = 0;
    g_node.login_time = 0;
    g_node.caller_no = 0L;

    node_save_current();
}

void node_mark_logged_in(void)
{
    g_node.active = 1;
    g_node.local = (byte)(g_sess.local_login ? 1 : 0);
    g_node.chat = (byte)(g_sess.sysop_chat ? 1 : 0);
    g_node.node_num = (byte)g_sess.node_num;
    g_node.caller_no = (longint)g_sess.caller_no;
    strncpy(g_node.caller_name, g_sess.user.name, NAME_LEN);
    g_node.caller_name[NAME_LEN] = 0;
    strcpy(g_node.status, "Logged in");
    g_node.login_date = pack_date_now();
    g_node.login_time = pack_time_now();
    g_node.menu_set = g_sess.user.menu;
    g_node.term = g_sess.user.term;
    g_node.protocol = g_sess.user.protocol;
    g_node.calls_total++;

    node_save_current();
}

void node_mark_menu(menu_name)
char *menu_name;
{
    g_node.active = 1;

    if (menu_name && *menu_name)
    {
        strncpy(g_node.status, menu_name, NODE_STATUS_LEN - 1);
        g_node.status[NODE_STATUS_LEN - 1] = 0;
    }
    else
        strcpy(g_node.status, "Menu");

    node_save_current();
}

void node_mark_message_activity(void)
{
    g_node.msgs_total++;
    strcpy(g_node.status, "Messages");
    node_save_current();
}

void node_mark_file_activity(void)
{
    g_node.files_total++;
    strcpy(g_node.status, "Files");
    node_save_current();
}

void node_mark_user_activity(void)
{
    g_node.users_total++;
    strcpy(g_node.status, "User maintenance");
    node_save_current();
}

void node_set_chat(flag)
int flag;
{
    g_node.chat = (byte)(flag ? 1 : 0);
    node_save_current();
}

void node_set_status(text)
char *text;
{
    if (text && *text)
    {
        strncpy(g_node.status, text, NODE_STATUS_LEN - 1);
        g_node.status[NODE_STATUS_LEN - 1] = 0;
    }
    else
        g_node.status[0] = 0;

    node_save_current();
}

/* ------------------------------------------------------------ */
/* pseudo filename counter                                      */
/* ------------------------------------------------------------ */

long node_get_pseudo_counter(void)
{
    return g_node.pseudo_ctr;
}

void node_set_pseudo_counter(v)
long v;
{
    if (v < 0L)
        v = 0L;

    g_node.pseudo_ctr = v;
    node_save_current();
}

void node_bump_pseudo_counter(void)
{
    g_node.pseudo_ctr++;
    node_save_current();
}

void node_make_pseudo_name(out, ext_num)
char *out;
int ext_num;
{
    unsigned n;

    n = (unsigned)(g_node.pseudo_ctr & 0xFFFFU);
    sprintf(out, "FILE%04u.U%d", n, ext_num);
}

/* ------------------------------------------------------------ */
/* display                                                      */
/* ------------------------------------------------------------ */

void node_show_status(void)
{
    puts("");
    printf("BBS-PC 4.20 Node #%d\n", g_sess.node_num);
    printf("Chat:   %s\n", g_node.chat ? "ON" : "OFF");
    printf("Mode:   %s\n", g_node.local ? "Local" : "Remote");
    printf("Calls:  %ld\n", (long)g_node.calls_total);
    printf("Msgs:   %ld\n", (long)g_node.msgs_total);
    printf("Users:  %ld\n", (long)g_node.users_total);
    printf("Files:  %ld\n", (long)g_node.files_total);
    printf("Pseudo: %ld\n", (long)g_node.pseudo_ctr);
    printf("State:  %s\n", g_node.status);
    if (g_node.caller_name[0])
        printf("Caller: %s\n", g_node.caller_name);
    puts("");
}

void node_show_brief_line(void)
{
    printf("Node %02d  Chat:%s  Calls:%ld  Msgs:%ld  Users:%ld  Files:%ld  %s\n",
        g_sess.node_num,
        g_node.chat ? "Y" : "N",
        (long)g_node.calls_total,
        (long)g_node.msgs_total,
        (long)g_node.users_total,
        (long)g_node.files_total,
        g_node.status);
}

/* ------------------------------------------------------------ */
/* simple administrative entry points                           */
/* ------------------------------------------------------------ */

void node_reset_current(void)
{
    node_defaults(&g_node);
    g_node.node_num = (byte)g_sess.node_num;
    strcpy(g_node.status, "Node reset");
    node_save_current();
}

void node_change_banner(void)
{
    char line[80];

    printf("Current banner: %s\n", g_node.banner);
    printf("New banner: ");

    if (fgets(line, sizeof(line), stdin) == NULL)
        return;

    trim_crlf_local(line);
    if (!line[0])
        return;

    strncpy(g_node.banner, line, NODE_BANNER_LEN - 1);
    g_node.banner[NODE_BANNER_LEN - 1] = 0;
    node_save_current();
}

/* ------------------------------------------------------------ */
/* convenience hooks for startup/shutdown integration           */
/* ------------------------------------------------------------ */

void node_startup_init(void)
{
    if (!node_open_current())
    {
        puts("Can't open node file");
        node_defaults(&g_node);
        make_node_name(g_node_fname, g_sess.node_num);
    }

    node_mark_waiting();
}

void node_session_begin(void)
{
    node_mark_logged_in();
}

void node_session_end(void)
{
    node_mark_waiting();
}