/* BBSNODEED.C
 *
 * BBS-PC 4.20 node editor / NODE%02d.DAT maintenance path
 *
 * Purpose:
 * - provide a real editor path for maintenance opcode 35
 * - centralize NODE%02d.DAT load/save/edit logic
 * - improve alignment with BBSINIT / V41TOV42 reconstruction work
 *
 * Notes:
 * - Exact historical NODE%02d.DAT layout is still not fully proven.
 * - This editor works with the reconstructed NODEREC layout currently
 *   used by BBSNODE.C.
 * - Once more binary evidence is recovered, this file can be tightened
 *   without changing the rest of the runtime.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bbsdata.h"
#include "bbsfunc.h"

#define NODE_NAME_LEN   20
#define NODE_BANNER_LEN 40
#define NODE_STATUS_LEN 40

typedef struct {
    byte  active;
    byte  local;
    byte  chat;
    byte  node_num;

    longint pseudo_ctr;
    longint caller_no;

    char  caller_name[NAME_LEN + 1];
    char  status[NODE_STATUS_LEN];
    char  banner[NODE_BANNER_LEN];

    ushort login_date;
    ushort login_time;

    byte  menu_set;
    byte  term;
    byte  protocol;
    byte  reserved;

    longint calls_total;
    longint msgs_total;
    longint users_total;
    longint files_total;
} NODEREC;

/* ------------------------------------------------------------ */

static void nodeed_defaults(n, node_num)
NODEREC *n;
int node_num;
{
    memset(n, 0, sizeof(*n));

    n->active = 0;
    n->local = 1;
    n->chat = 1;
    n->node_num = (byte)node_num;
    n->pseudo_ctr = 0L;
    n->caller_no = 0L;
    strcpy(n->caller_name, "");
    strcpy(n->status, "Waiting for caller");
    strcpy(n->banner, "BBS-PC 4.20");
    n->login_date = 0;
    n->login_time = 0;
    n->menu_set = 0;
    n->term = 0;
    n->protocol = 0;
    n->reserved = 0;
    n->calls_total = 0L;
    n->msgs_total = 0L;
    n->users_total = 0L;
    n->files_total = 0L;
}

static int nodeed_load(node_num, n)
int node_num;
NODEREC *n;
{
    char path[NODE_NAME_LEN];
    FILE *fp;

    data_make_node_name(path, node_num);

    fp = fopen(path, "rb");
    if (!fp)
        return 0;

    memset(n, 0, sizeof(*n));
    if (fread(n, sizeof(*n), 1, fp) != 1)
    {
        fclose(fp);
        return 0;
    }

    fclose(fp);

    if (n->node_num == 0)
        n->node_num = (byte)node_num;

    return 1;
}

static int nodeed_save(node_num, n)
int node_num;
NODEREC *n;
{
    char path[NODE_NAME_LEN];
    FILE *fp;

    data_make_node_name(path, node_num);

    fp = fopen(path, "wb");
    if (!fp)
        return 0;

    if (fwrite(n, sizeof(*n), 1, fp) != 1)
    {
        fclose(fp);
        return 0;
    }

    fclose(fp);
    return 1;
}

static void nodeed_show(node_num, n)
int node_num;
NODEREC *n;
{
    char d[20], t[20];

    if (n->login_date)
        data_unpack_date(n->login_date, d);
    else
        strcpy(d, "--");

    if (n->login_time)
        data_unpack_time(n->login_time, t);
    else
        strcpy(t, "--");

    puts("");
    printf("NODE%02d.DAT\n", node_num);
    puts("----------------------------------------");
    printf("Node number       %u\n", (unsigned)n->node_num);
    printf("Active            %s\n", n->active ? "Yes" : "No");
    printf("Local             %s\n", n->local ? "Yes" : "No");
    printf("Chat              %s\n", n->chat ? "Yes" : "No");
    printf("Pseudo counter    %ld\n", (long)n->pseudo_ctr);
    printf("Caller number     %ld\n", (long)n->caller_no);
    printf("Caller name       %s\n", n->caller_name);
    printf("Status            %s\n", n->status);
    printf("Banner            %s\n", n->banner);
    printf("Login date        %s\n", d);
    printf("Login time        %s\n", t);
    printf("Menu set          %u\n", (unsigned)n->menu_set);
    printf("Terminal          %u\n", (unsigned)n->term);
    printf("Protocol          %u\n", (unsigned)n->protocol);
    printf("Calls total       %ld\n", (long)n->calls_total);
    printf("Msgs total        %ld\n", (long)n->msgs_total);
    printf("Users total       %ld\n", (long)n->users_total);
    printf("Files total       %ld\n", (long)n->files_total);
}

static void nodeed_edit_numeric_byte(prompt, v)
char *prompt;
byte *v;
{
    char line[32];

    data_prompt_line(prompt, line, sizeof(line));
    if (line[0])
        *v = (byte)atoi(line);
}

static void nodeed_edit_numeric_long(prompt, v)
char *prompt;
longint *v;
{
    char line[32];

    data_prompt_line(prompt, line, sizeof(line));
    if (line[0])
        *v = atol(line);
}

static void nodeed_edit_string(prompt, s, maxlen)
char *prompt;
char *s;
int maxlen;
{
    char line[128];

    data_prompt_line(prompt, line, sizeof(line));
    if (line[0])
    {
        strncpy(s, line, maxlen - 1);
        s[maxlen - 1] = 0;
    }
}

static void nodeed_toggle(prompt, v)
char *prompt;
byte *v;
{
    *v = data_yesno(prompt, *v ? 1 : 0) ? 1 : 0;
}

static void nodeed_reset_runtime(n, node_num)
NODEREC *n;
int node_num;
{
    nodeed_defaults(n, node_num);
    strcpy(n->status, "Node reset");
}

static void nodeed_set_now(n)
NODEREC *n;
{
    n->login_date = data_pack_date_now();
    n->login_time = data_pack_time_now();
}

static void nodeed_menu(node_num, n)
int node_num;
NODEREC *n;
{
    char line[16];
    int done = 0;

    while (!done)
    {
        nodeed_show(node_num, n);
        puts("");
        puts("A: Active");
        puts("B: Local");
        puts("C: Chat");
        puts("D: Pseudo counter");
        puts("E: Caller number");
        puts("F: Caller name");
        puts("G: Status");
        puts("H: Banner");
        puts("I: Login date/time = now");
        puts("J: Menu set");
        puts("K: Terminal");
        puts("L: Protocol");
        puts("M: Calls total");
        puts("N: Msgs total");
        puts("O: Users total");
        puts("P: Files total");
        puts("R: Reset node");
        puts("S: Save");
        puts("Q: Quit");
        puts("");

        data_prompt_line("Option? ", line, sizeof(line));
        if (line[0] >= 'a' && line[0] <= 'z')
            line[0] -= 32;

        switch (line[0])
        {
            case 'A':
                nodeed_toggle("Active (Y/N)? ", &n->active);
                break;

            case 'B':
                nodeed_toggle("Local (Y/N)? ", &n->local);
                break;

            case 'C':
                nodeed_toggle("Chat (Y/N)? ", &n->chat);
                break;

            case 'D':
                nodeed_edit_numeric_long("Pseudo counter: ", &n->pseudo_ctr);
                break;

            case 'E':
                nodeed_edit_numeric_long("Caller number: ", &n->caller_no);
                break;

            case 'F':
                nodeed_edit_string("Caller name: ", n->caller_name,
                    sizeof(n->caller_name));
                break;

            case 'G':
                nodeed_edit_string("Status: ", n->status, sizeof(n->status));
                break;

            case 'H':
                nodeed_edit_string("Banner: ", n->banner, sizeof(n->banner));
                break;

            case 'I':
                nodeed_set_now(n);
                break;

            case 'J':
                nodeed_edit_numeric_byte("Menu set: ", &n->menu_set);
                break;

            case 'K':
                nodeed_edit_numeric_byte("Terminal: ", &n->term);
                break;

            case 'L':
                nodeed_edit_numeric_byte("Protocol: ", &n->protocol);
                break;

            case 'M':
                nodeed_edit_numeric_long("Calls total: ", &n->calls_total);
                break;

            case 'N':
                nodeed_edit_numeric_long("Msgs total: ", &n->msgs_total);
                break;

            case 'O':
                nodeed_edit_numeric_long("Users total: ", &n->users_total);
                break;

            case 'P':
                nodeed_edit_numeric_long("Files total: ", &n->files_total);
                break;

            case 'R':
                if (data_yesno("Reset node to defaults (Y/N)? ", 0))
                    nodeed_reset_runtime(n, node_num);
                break;

            case 'S':
                if (nodeed_save(node_num, n))
                    puts("Node file saved");
                else
                    puts("Can't save node file");
                bbs_pause();
                break;

            case 'Q':
                done = 1;
                break;

            default:
                break;
        }
    }
}

/* ------------------------------------------------------------ */
/* public maintenance entry points                              */
/* ------------------------------------------------------------ */

void node_edit_file(node_num)
int node_num;
{
    NODEREC n;

    if (node_num < 1 || node_num > 99)
    {
        puts("Illegal node number");
        return;
    }

    if (!nodeed_load(node_num, &n))
        nodeed_defaults(&n, node_num);

    nodeed_menu(node_num, &n);
}

void node_edit_current(void)
{
    node_edit_file(g_sess.node_num);
}

void do_change_node_defaults(void)
{
    char line[16];
    int node_num;

    if (!sysop_password_prompt())
        return;

    data_prompt_line("Which node (1-99)? ", line, sizeof(line));
    if (!line[0])
        return;

    node_num = atoi(line);
    node_edit_file(node_num);
}

void node_create_file(node_num)
int node_num;
{
    NODEREC n;

    if (node_num < 1 || node_num > 99)
        return;

    nodeed_defaults(&n, node_num);
    nodeed_save(node_num, &n);
}

void node_convert_file(node_num)
int node_num;
{
    NODEREC n;

    if (node_num < 1 || node_num > 99)
        return;

    if (!nodeed_load(node_num, &n))
    {
        nodeed_defaults(&n, node_num);
        nodeed_save(node_num, &n);
        return;
    }

    if (n.node_num == 0)
        n.node_num = (byte)node_num;

    nodeed_save(node_num, &n);
}

/* ------------------------------------------------------------ */
/* BBSINIT / V41TOV42-friendly helpers                          */
/* ------------------------------------------------------------ */

void node_set_pseudo_counter_file(node_num, v)
int node_num;
long v;
{
    NODEREC n;

    if (node_num < 1 || node_num > 99)
        return;

    if (!nodeed_load(node_num, &n))
        nodeed_defaults(&n, node_num);

    if (v < 0L)
        v = 0L;

    n.pseudo_ctr = v;
    nodeed_save(node_num, &n);
}

long node_get_pseudo_counter_file(node_num)
int node_num;
{
    NODEREC n;

    if (node_num < 1 || node_num > 99)
        return 0L;

    if (!nodeed_load(node_num, &n))
        return 0L;

    return n.pseudo_ctr;
}