/* BBSMSG.C
 *
 * First-pass BBS-PC 4.20 message base module
 *
 * Refactored to use BBSDATA.C / BBSISAM.C helpers:
 * - data_prompt_line()
 * - data_yesno()
 * - data_pack_date_now()
 * - data_pack_time_now()
 * - data_unpack_date()
 * - data_unpack_time()
 * - data_find_blank_msghead()
 * - data_find_msg_by_number()
 * - data_highest_msg_number()
 * - data_lowest_msg_number()
 * - data_count_visible_msgs()
 * - data_load_message_text()
 * - data_store_message_text()
 * - data_zero_message_chain()
 * - data_read_msghead() / data_write_msghead()
 *
 * Implements:
 * - leave a message
 * - leave a message to SYSOP
 * - read messages
 * - read EMAIL
 * - scan message headers
 * - delete a message
 *
 * Notes:
 * - Thread/reply maintenance is still minimal in this pass.
 * - Message text chaining remains the current fixed-record model.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bbsdata.h"
#include "bbsfunc.h"

/* ------------------------------------------------------------ */
/* local helpers                                                */
/* ------------------------------------------------------------ */

static int capture_message_text(out, maxlen)
char *out;
int maxlen;
{
    char line[160];
    int pos = 0;
    int n;

    out[0] = 0;

    puts("");
    puts("Enter your message text");
    puts("Blank line to end message");
    puts("");

    for (;;)
    {
        if (fgets(line, sizeof(line), stdin) == NULL)
            break;

        data_trim_crlf(line);

        if (!line[0])
            break;

        n = (int)strlen(line);
        if (pos + n + 2 >= maxlen)
        {
            puts("Message file full");
            return 0;
        }

        memcpy(out + pos, line, n);
        pos += n;
        out[pos++] = '\n';
        out[pos] = 0;
    }

    return 1;
}

static int current_user_write_section(sec)
int sec;
{
    if (sec < 0 || sec >= NUM_SECT)
        return 0;

    return (g_sess.user.wr_acc & (1 << sec)) != 0;
}

static int current_user_read_section(sec)
int sec;
{
    if (sec < 0 || sec >= NUM_SECT)
        return 0;

    return (g_sess.user.rd_acc & (1 << sec)) != 0;
}

static int select_section_for_posting(void)
{
    char line[16];
    int sec;

    puts("");
    puts("Sections:");
    do_section_names();

    data_prompt_line("Section (A-P)? ", line, sizeof(line));
    if (!line[0])
        return -1;

    if (line[0] >= 'a' && line[0] <= 'z')
        line[0] -= 32;

    if (line[0] < 'A' || line[0] >= ('A' + NUM_SECT))
        return -1;

    sec = line[0] - 'A';

    if (!current_user_write_section(sec))
    {
        puts("Illegal access");
        return -1;
    }

    return sec;
}

static int create_message_to(to_name)
char *to_name;
{
    MSGHEAD h;
    long head_rec;
    ushort first_text;
    char subj[SUBJ_LEN + 2];
    char text[MSG_LEN + 1];
    int sec;

    if (!g_fp_msghead || !g_fp_msgtext)
    {
        puts("Message files not open");
        return 0;
    }

    sec = select_section_for_posting();
    if (sec < 0)
        return 0;

    data_prompt_line("Subject: ", subj, sizeof(subj));
    if (!capture_message_text(text, sizeof(text)))
        return 0;

    head_rec = data_find_blank_msghead();
    if (head_rec < 0L)
    {
        puts("Message file full");
        return 0;
    }

    if (!data_store_message_text(text, &first_text))
    {
        puts("Message file full");
        return 0;
    }

    memset(&h, 0, sizeof(h));
    h.type = 0;
    h.number = data_highest_msg_number() + 1L;
    h.section = (byte)sec;

    strncpy(h.from, g_sess.user.name, FROM_LEN);
    h.from[FROM_LEN] = 0;

    strncpy(h.to, to_name, TO_LEN);
    h.to[TO_LEN] = 0;

    strncpy(h.subj, subj, SUBJ_LEN);
    h.subj[SUBJ_LEN] = 0;

    h.recno = first_text;
    h.date = data_pack_date_now();
    h.time = data_pack_time_now();
    h.origin = 0L;

    if (data_yesno("Unformatted? ", 0))
        h.unformat = 1;
    if (data_yesno("Private? ", 0))
        h.personal = 1;
    if (data_yesno("Locked? ", 0))
        h.locked = 1;

    if (!data_write_msghead(head_rec, &h))
    {
        puts("Can't write message");
        return 0;
    }

    g_sess.user.msg_total++;
    user_save_current();

    printf("Message #%ld stored\n", h.number);
    return 1;
}

/* ------------------------------------------------------------ */
/* display                                                      */
/* ------------------------------------------------------------ */

static void show_msg_header(h)
MSGHEAD *h;
{
    char d[20], t[20];

    data_unpack_date(h->date, d);
    data_unpack_time(h->time, t);

    puts("");
    printf("Msg: #%ld", (long)h->number);
    if (h->personal) printf("  * Private *");
    if (h->locked)   printf("  * Locked *");
    if (h->unformat) printf("  * Unformatted *");
    putchar('\n');

    printf("Sec: %c - %s\n",
        'A' + h->section,
        g_cfg.sec_name[h->section][0] ? g_cfg.sec_name[h->section] : "(unnamed)");

    printf("Date: %s  Time: %s\n", d, t);
    printf("From: %s\n", h->from);
    printf("To:   %s\n", h->to);
    printf("Subj: %s\n", h->subj);
    puts("");
}

static void show_msg_text(h)
MSGHEAD *h;
{
    char text[MSG_LEN + 1];

    if (!data_load_message_text(h->recno, text, sizeof(text)))
    {
        puts("Can't read message text");
        return;
    }

    puts(text);
}

static void display_message(h)
MSGHEAD *h;
{
    show_msg_header(h);
    show_msg_text(h);
    bbs_pause();
}

/* ------------------------------------------------------------ */
/* scan/read                                                    */
/* ------------------------------------------------------------ */

static void scan_one_header(h)
MSGHEAD *h;
{
    char d[20], t[20];

    data_unpack_date(h->date, d);
    data_unpack_time(h->time, t);

    printf("#%-6ld %c %-24s %-24s %-24s %s %s\n",
        (long)h->number,
        'A' + h->section,
        h->from,
        h->to,
        h->subj,
        d,
        t);
}

void do_scan_messages(void)
{
    long recno;
    MSGHEAD h;

    if (!g_fp_msghead)
    {
        puts("Message file not open");
        bbs_pause();
        return;
    }

    puts("");
    puts("Message headers:");
    puts("");

    recno = data_first_msg(&h);
    while (recno >= 0L)
    {
        if (current_user_read_section(h.section))
            scan_one_header(&h);

        recno = data_next_msg(recno, &h);
    }

    bbs_pause();
}

void do_read_messages(void)
{
    MSGHEAD h;
    char line[32];
    long msgno, recno, first, last, visible;

    if (!g_fp_msghead)
    {
        puts("Message file not open");
        bbs_pause();
        return;
    }

    first = data_lowest_msg_number();
    last = data_highest_msg_number();
    visible = data_count_visible_msgs(g_sess.user.rd_acc);

    printf("System contains %ld msgs (%ld-%ld)\n", visible, first, last);

    for (;;)
    {
        data_prompt_line("Message number (0 to end): ", line, sizeof(line));
        msgno = atol(line);

        if (msgno == 0L)
            break;

        recno = data_find_msg_by_number(msgno, &h);
        if (recno < 0L)
        {
            puts("Message not found");
            continue;
        }

        if (!current_user_read_section(h.section))
        {
            puts("Illegal access");
            continue;
        }

        display_message(&h);
    }
}

/* ------------------------------------------------------------ */
/* delete                                                       */
/* ------------------------------------------------------------ */

static int can_delete_message(h)
MSGHEAD *h;
{
    if (h->locked && g_sess.user.priv < 100)
        return 0;

    if (data_user_match(h->from, g_sess.user.name))
        return 1;

    if (g_sess.user.priv >= 100)
        return 1;

    return 0;
}

void do_delete_message(void)
{
    MSGHEAD h;
    char line[32];
    long msgno, recno;

    data_prompt_line("Message number to delete: ", line, sizeof(line));
    msgno = atol(line);
    if (msgno <= 0L)
        return;

    recno = data_find_msg_by_number(msgno, &h);
    if (recno < 0L)
    {
        puts("Message not found");
        return;
    }

    if (!can_delete_message(&h))
    {
        puts("Illegal access");
        return;
    }

    data_zero_message_chain(h.recno);
    memset(&h, 0, sizeof(h));

    if (!data_write_msghead(recno, &h))
    {
        puts("Can't delete message");
        return;
    }

    puts("Killed");
}

/* ------------------------------------------------------------ */
/* public message ops                                           */
/* ------------------------------------------------------------ */

void do_leave_message(void)
{
    create_message_to("ALL");
}

void do_leave_message_to_sysop(void)
{
    create_message_to("SYSOP");
}

void do_read_email(void)
{
    long recno;
    MSGHEAD h;

    if (!g_fp_msghead)
    {
        puts("Message file not open");
        bbs_pause();
        return;
    }

    puts("");
    puts("EMAIL:");
    puts("");

    recno = data_first_msg(&h);
    while (recno >= 0L)
    {
        if (current_user_read_section(h.section) &&
            data_user_match(h.to, g_sess.user.name))
            display_message(&h);

        recno = data_next_msg(recno, &h);
    }
}

void do_leave_message_to_name(void)
{
    char name[TO_LEN + 2];

    data_prompt_line("To: ", name, sizeof(name));
    if (!name[0])
        return;

    create_message_to(name);
}

/* ------------------------------------------------------------ */
/* future hooks                                                 */
/* ------------------------------------------------------------ */

void do_reply_message(void)
{
    puts("Reply message not implemented yet");
}

void do_purge_messages(void)
{
    puts("Purge messages not implemented yet");
}