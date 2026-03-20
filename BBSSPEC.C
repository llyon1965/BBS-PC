/* BBSSPEC.C
 *
 * BBS-PC ! 4.21
 *
 * Special/menu support functions.
 *
 * Updated:
 * - do_print_caller_log() now walks the full caller log in reverse order
 *   with paging, instead of showing an arbitrary fixed count.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "bbsdata.h"
#include "bbsfunc.h"

#ifndef CALLER_LINES_PER_ENTRY
#define CALLER_LINES_PER_ENTRY 6
#endif

/* ------------------------------------------------------------ */
/* local helpers                                                */
/* ------------------------------------------------------------ */

static int spec_pause_if_needed(lines_used)
int *lines_used;
{
    int page_len;

    if (!lines_used)
        return 1;

    page_len = g_cfg.page_len;
    if (page_len <= 0)
        page_len = 24;

    if (*lines_used < page_len)
        return 1;

    if (!term_yesno("Continue (Y/N)? ", 1))
        return 0;

    *lines_used = 0;
    return 1;
}

static void spec_make_comm_string(c, out, outlen)
USRLOG *c;
char *out;
int outlen;
{
    char parity;

    parity = c->parity ? c->parity : 'N';

    sprintf(out, "%u:%u%c%u",
            (unsigned)c->baud,
            (unsigned)c->data_bits,
            parity,
            (unsigned)c->stop_bits);

    out[outlen - 1] = 0;
}

static void spec_make_call_summary(c, out, outlen)
USRLOG *c;
char *out;
int outlen;
{
    out[0] = 0;

    if (c->msgs_left > 0)
        sprintf(out + strlen(out), "%u Message%s",
                (unsigned)c->msgs_left,
                (c->msgs_left == 1) ? "" : "s");

    if (c->downloads > 0)
    {
        if (out[0])
            strcat(out, ", ");
        sprintf(out + strlen(out), "%u Download%s",
                (unsigned)c->downloads,
                (c->downloads == 1) ? "" : "s");
    }

    if (c->uploads > 0)
    {
        if (out[0])
            strcat(out, ", ");
        sprintf(out + strlen(out), "%u Upload%s",
                (unsigned)c->uploads,
                (c->uploads == 1) ? "" : "s");
    }

    if (!out[0])
        strcpy(out, "No activity");
}

static void spec_print_one_caller(c)
USRLOG *c;
{
    char in_date[32], in_time[32];
    char out_date[32], out_time[32];
    char comm[24];
    char summary[96];

    data_unpack_date(c->in_date, in_date);
    data_unpack_time(c->in_time, in_time);

    if (c->out_date)
        data_unpack_date(c->out_date, out_date);
    else
        strcpy(out_date, "-");

    if (c->out_time)
        data_unpack_time(c->out_time, out_time);
    else
        strcpy(out_time, "-");

    spec_make_comm_string(c, comm, sizeof(comm));
    spec_make_call_summary(c, summary, sizeof(summary));

    printf("#%lu - Node #%u %s\n",
           c->caller_no,
           (unsigned)c->node_no,
           comm);

    printf("  %s\n", c->name);

    if (c->city[0])
        printf("  %s\n", c->city);
    else
        printf("  Unknown\n");

    printf("  In : %s %s\n", in_date, in_time);

    if (c->out_date || c->out_time)
    {
        if (c->disc_reason[0])
            printf("  Out: %s %s  %s\n", out_date, out_time, c->disc_reason);
        else
            printf("  Out: %s %s\n", out_date, out_time);
    }
    else
    {
        printf("  Out: -\n");
    }

    printf("  Sum: %s\n", summary);
    puts("");
}

/* ------------------------------------------------------------ */
/* public special functions                                     */
/* ------------------------------------------------------------ */

void do_execute_external_program(parm)
char *parm;
{
    if (!parm || !parm[0])
    {
        puts("No external program specified");
        return;
    }

    printf("Executing external program: %s\n", parm);
    system(parm);
}

void do_dos_gate(void)
{
    puts("DOS gate not yet expanded");
}

void do_section_names(void)
{
    int i;

    puts("Sections:");
    for (i = 0; i < NUM_SECT; i++)
        printf("  Section %d\n", i);
}

void do_change_section_mask(void)
{
    puts("Section mask change not yet expanded");
}

void cls_type_text_file(fname, stop_flag)
char *fname;
int stop_flag;
{
    bbs_type_file(fname, 1, stop_flag);
}

void type_text_file(fname, stop_flag)
char *fname;
int stop_flag;
{
    bbs_type_file(fname, 0, stop_flag);
}

void do_change_menu_sets(void)
{
    puts("Menu-set change not yet expanded");
}

void do_print_caller_log(void)
{
    long n;
    long i;
    int lines_used;
    USRLOG c;

    n = data_caller_count();
    if (n <= 0L)
    {
        puts("No caller log entries");
        return;
    }

    lines_used = 0;

    puts("");
    puts("Caller Log");
    puts("----------");
    puts("");
    lines_used += 3;

    for (i = n - 1L; i >= 0L; i--)
    {
        if (!data_read_caller(i, &c))
            continue;

        if (!c.name[0])
            continue;

        if ((lines_used + CALLER_LINES_PER_ENTRY) >=
            ((g_cfg.page_len > 0) ? g_cfg.page_len : 24))
        {
            if (!spec_pause_if_needed(&lines_used))
                return;
        }

        spec_print_one_caller(&c);
        lines_used += CALLER_LINES_PER_ENTRY;

        if (i == 0L)
            break;
    }
}

void do_list_phone_directory(void)
{
    puts("Phone directory is not implemented");
}

void do_change_phone_listing(void)
{
    puts("Phone listing change is not implemented");
}

void do_dial_connect_remote(void)
{
    puts("Dial/connect remote is not implemented");
}

void do_unlisted_dial_connect(void)
{
    puts("Unlisted dial/connect is not implemented");
}

void do_return_specified_levels(void)
{
    if (!menu_pop())
        return;
}

void do_return_top_level(void)
{
    while (menu_pop())
        ;
}