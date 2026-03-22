/* BBSMSG.C
 *
 * BBS-PC ! 4.21
 *
 * Reconstructed and modernised source
 * derived from BBS-PC 4.20
 *
 * Message subsystem
 *
 * Updated:
 * - original-style "new" logic uses user.highmsgread
 * - clearer scan vs read behaviour
 * - reply-chain/thread walking improved
 * - full-read paths advance highmsgread
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bbsdata.h"
#include "bbsfunc.h"

#ifndef MAX_MSG_BODY
#define MAX_MSG_BODY 8192
#endif

#define MSGMODE_SCAN   1
#define MSGMODE_READ   2

/* ------------------------------------------------------------ */
/* local helpers                                                */
/* ------------------------------------------------------------ */

static int msg_is_blank(h)
MSGHEAD *h;
{
    if (!h)
        return 1;

    return (h->number == 0L);
}

static int msg_header_valid(h)
MSGHEAD *h;
{
    if (!h)
        return 0;

    if (msg_is_blank(h))
        return 0;

    if (h->number <= 0L)
        return 0;

    return 1;
}

static long msg_count(void)
{
    long n;

    n = data_msg_count();
    if (n < 0L)
        return 0L;

    return n;
}

static long msg_find_recno_by_number(msgno, out)
long msgno;
MSGHEAD *out;
{
    return data_find_msg_by_number(msgno, out);
}

static int msg_reply_slot_used(v)
long v;
{
    return v > 0L;
}

static int msg_has_reply_number(h, replyno)
MSGHEAD *h;
long replyno;
{
    int i;

    for (i = 0; i < MAX_REPLY_LINKS; i++)
        if (h->replys[i] == replyno)
            return 1;

    return 0;
}

static int msg_add_reply_number(h, replyno)
MSGHEAD *h;
long replyno;
{
    int i;

    if (!h || replyno <= 0L)
        return 0;

    if (msg_has_reply_number(h, replyno))
        return 1;

    for (i = 0; i < MAX_REPLY_LINKS; i++)
    {
        if (!msg_reply_slot_used(h->replys[i]))
        {
            h->replys[i] = replyno;
            return 1;
        }
    }

    return 0;
}

static int msg_remove_reply_number(h, replyno)
MSGHEAD *h;
long replyno;
{
    int i, j;

    if (!h || replyno <= 0L)
        return 0;

    for (i = 0; i < MAX_REPLY_LINKS; i++)
    {
        if (h->replys[i] == replyno)
        {
            for (j = i; j < MAX_REPLY_LINKS - 1; j++)
                h->replys[j] = h->replys[j + 1];
            h->replys[MAX_REPLY_LINKS - 1] = 0L;
            return 1;
        }
    }

    return 0;
}

static int msg_reply_target_valid(parentno, childno)
long parentno;
long childno;
{
    if (parentno <= 0L || childno <= 0L)
        return 0;
    if (parentno == childno)
        return 0;
    return 1;
}

static int msg_link_child_to_parent(parentno, childno)
long parentno;
long childno;
{
    long prec;
    MSGHEAD parent;

    if (!msg_reply_target_valid(parentno, childno))
        return 0;

    prec = msg_find_recno_by_number(parentno, &parent);
    if (prec < 0L)
        return 0;

    if (!msg_add_reply_number(&parent, childno))
        return 0;

    return data_write_msghead(prec, &parent);
}

static int msg_unlink_child_from_parent(parentno, childno)
long parentno;
long childno;
{
    long prec;
    MSGHEAD parent;

    if (!msg_reply_target_valid(parentno, childno))
        return 0;

    prec = msg_find_recno_by_number(parentno, &parent);
    if (prec < 0L)
        return 0;

    if (!msg_remove_reply_number(&parent, childno))
        return 1;

    return data_write_msghead(prec, &parent);
}

static int msg_text_chain_valid(first_recno)
ushort first_recno;
{
    long ntext;
    long recno;
    long seen;

    if (first_recno == 0)
        return 1;

    ntext = data_msgtext_count();
    if (ntext <= 0L)
        return 0;

    recno = (long)first_recno;
    if (recno < 0L || recno >= ntext)
        return 0;

    seen = 0L;
    while (recno < ntext)
    {
        MSGTEXT t;

        if (!data_read_msgtext(recno, &t))
            return 0;

        if (!t.text[0])
            return 1;

        recno++;
        seen++;

        if (seen > ntext)
            return 0;
    }

    return 0;
}

static int msg_parent_link_consistent(h)
MSGHEAD *h;
{
    MSGHEAD parent;
    long prec;

    if (!h)
        return 0;

    if (h->replyto == 0L)
        return 1;

    if (h->replyto == h->number)
        return 0;

    prec = msg_find_recno_by_number(h->replyto, &parent);
    if (prec < 0L)
        return 0;

    return msg_has_reply_number(&parent, h->number);
}

static int msg_repair_parent_link(h)
MSGHEAD *h;
{
    if (!h)
        return 0;

    if (h->replyto == 0L)
        return 1;

    if (h->replyto == h->number)
    {
        h->replyto = 0L;
        return 1;
    }

    return msg_link_child_to_parent(h->replyto, h->number);
}

static int msg_repair_reply_links(h)
MSGHEAD *h;
{
    int i, j, changed;
    MSGHEAD child;
    long crec;

    if (!h)
        return 0;

    changed = 0;

    for (i = 0; i < MAX_REPLY_LINKS; i++)
    {
        if (!msg_reply_slot_used(h->replys[i]))
            continue;

        if (h->replys[i] == h->number)
        {
            msg_remove_reply_number(h, h->replys[i]);
            changed = 1;
            i--;
            continue;
        }

        crec = msg_find_recno_by_number(h->replys[i], &child);
        if (crec < 0L || child.replyto != h->number)
        {
            msg_remove_reply_number(h, h->replys[i]);
            changed = 1;
            i--;
        }
    }

    for (i = 0; i < MAX_REPLY_LINKS; i++)
    {
        for (j = i + 1; j < MAX_REPLY_LINKS; j++)
        {
            if (h->replys[i] > 0L && h->replys[i] == h->replys[j])
            {
                msg_remove_reply_number(h, h->replys[j]);
                changed = 1;
                j--;
            }
        }
    }

    return changed;
}

static int msg_validate_and_repair_header(recno, h)
long recno;
MSGHEAD *h;
{
    int changed;

    changed = 0;

    if (!msg_text_chain_valid(h->msgptr))
    {
        h->msgptr = 0;
        changed = 1;
    }

    if (!msg_parent_link_consistent(h))
    {
        if (msg_repair_parent_link(h))
            changed = 1;
        else if (h->replyto != 0L)
        {
            h->replyto = 0L;
            changed = 1;
        }
    }

    if (msg_repair_reply_links(h))
        changed = 1;

    if (changed)
        return data_write_msghead(recno, h);

    return 1;
}

static int msg_is_new_for_user(h)
MSGHEAD *h;
{
    if (!h)
        return 0;

    return h->number > g_sess.user.highmsgread;
}

static void msg_mark_read(h)
MSGHEAD *h;
{
    if (!h)
        return;

    if (h->number > g_sess.user.highmsgread)
    {
        g_sess.user.highmsgread = h->number;
        (void)user_save_current();
    }
}

static int msg_is_email_to_current_user(h)
MSGHEAD *h;
{
    if (!h)
        return 0;

    return data_user_match(h->to, g_sess.user.name);
}

static int msg_is_thread_root(h)
MSGHEAD *h;
{
    if (!h)
        return 0;

    if (h->replyto <= 0L)
        return 1;

    if (h->replyto == h->number)
        return 1;

    return 0;
}

static long msg_find_thread_root(msgno)
long msgno;
{
    MSGHEAD h;
    long seen;
    long current;

    current = msgno;
    seen = 0L;

    while (seen < msg_count())
    {
        if (msg_find_recno_by_number(current, &h) < 0L)
            return msgno;

        if (h.replyto <= 0L || h.replyto == h.number)
            return h.number;

        current = h.replyto;
        seen++;
    }

    return msgno;
}

static int msg_any_new_in_thread(msgno)
long msgno;
{
    MSGHEAD h;
    long recno;
    int i;

    recno = msg_find_recno_by_number(msgno, &h);
    if (recno < 0L)
        return 0;

    if (msg_is_new_for_user(&h))
        return 1;

    for (i = 0; i < MAX_REPLY_LINKS; i++)
        if (h.replys[i] > 0L && msg_any_new_in_thread(h.replys[i]))
            return 1;

    return 0;
}

static void msg_show_header(h)
MSGHEAD *h;
{
    char d[32], t[32];

    data_unpack_date(h->date, d);
    data_unpack_time(h->time, t);

    printf("Msg #%ld\n", h->number);
    printf("From: %s\n", h->from);
    printf("To  : %s\n", h->to);
    printf("Subj: %s\n", h->subject);
    printf("Date: %s %s\n", d, t);

    if (h->replyto > 0L)
        printf("Reply to: %ld\n", h->replyto);

    puts("");
}

static void msg_show_body(h)
MSGHEAD *h;
{
    char body[MAX_MSG_BODY];

    body[0] = 0;
    if (h->msgptr && data_load_message_text(h->msgptr, body, sizeof(body)))
        puts(body);
    else
        puts("(No message text)");
}

static void msg_show_thread_summary(h, depth)
MSGHEAD *h;
int depth;
{
    char d[32], t[32];
    int i;

    data_unpack_date(h->date, d);
    data_unpack_time(h->time, t);

    for (i = 0; i < depth; i++)
        printf("  ");

    printf("#%ld %-12s -> %-12s %-24s %s %s",
           h->number, h->from, h->to, h->subject, d, t);

    if (msg_is_new_for_user(h))
        printf(" [NEW]");

    puts("");
}

static int msg_continue_prompt(void)
{
    return term_yesno("Continue (Y/N)? ", 1);
}

static int msg_show_one(h, mode, depth)
MSGHEAD *h;
int mode;
int depth;
{
    if (mode == MSGMODE_SCAN)
    {
        msg_show_thread_summary(h, depth);
        return 1;
    }

    msg_show_header(h);
    msg_show_body(h);
    puts("");

    msg_mark_read(h);

    return msg_continue_prompt();
}

static int msg_display_thread_from_number(msgno, mode, depth, new_only)
long msgno;
int mode;
int depth;
int new_only;
{
    MSGHEAD h;
    long recno;
    int i;
    int show_this;

    recno = msg_find_recno_by_number(msgno, &h);
    if (recno < 0L)
        return 1;

    (void)msg_validate_and_repair_header(recno, &h);

    show_this = (!new_only || msg_is_new_for_user(&h));

    if (show_this)
    {
        if (!msg_show_one(&h, mode, depth))
            return 0;
    }

    for (i = 0; i < MAX_REPLY_LINKS; i++)
    {
        if (h.replys[i] > 0L)
        {
            if (!msg_display_thread_from_number(h.replys[i],
                                                mode,
                                                show_this ? depth + 1 : depth,
                                                new_only))
                return 0;
        }
    }

    return 1;
}

static int msg_display_roots(mode, new_only)
int mode;
int new_only;
{
    long i, n;
    MSGHEAD h;

    n = msg_count();
    for (i = 0L; i < n; i++)
    {
        if (!data_read_msghead(i, &h))
            continue;
        if (!msg_header_valid(&h))
            continue;
        if (!msg_is_thread_root(&h))
            continue;

        if (new_only && !msg_any_new_in_thread(h.number))
            continue;

        if (!msg_display_thread_from_number(h.number, mode, 0, new_only))
            return 0;
    }

    return 1;
}

static int msg_display_email(mode, new_only)
int mode;
int new_only;
{
    long i, n;
    MSGHEAD h;

    n = msg_count();
    for (i = 0L; i < n; i++)
    {
        if (!data_read_msghead(i, &h))
            continue;
        if (!msg_header_valid(&h))
            continue;
        if (!msg_is_email_to_current_user(&h))
            continue;
        if (new_only && !msg_is_new_for_user(&h))
            continue;

        if (!msg_show_one(&h, mode, 0))
            return 0;
    }

    return 1;
}

static long msg_next_number(void)
{
    return data_highest_msg_number() + 1L;
}

static int msg_append_new_header(h, out_recno)
MSGHEAD *h;
long *out_recno;
{
    long recno;

    recno = data_find_blank_msghead();
    if (recno < 0L)
        recno = data_msg_count();

    if (!data_write_msghead(recno, h))
        return 0;

    if (out_recno)
        *out_recno = recno;

    return 1;
}

static int msg_compose_common(h)
MSGHEAD *h;
{
    char line[256];
    char body[MAX_MSG_BODY];
    ushort firstptr;

    memset(h, 0, sizeof(*h));

    h->number = msg_next_number();
    h->date = data_pack_date_now();
    h->time = data_pack_time_now();

    strncpy(h->from, g_sess.user.name, NAME_LEN);
    h->from[NAME_LEN] = 0;

    term_getline("To: ", line, sizeof(line));
    if (!line[0])
        return 0;
    strncpy(h->to, line, NAME_LEN);
    h->to[NAME_LEN] = 0;

    term_getline("Subject: ", line, sizeof(line));
    strncpy(h->subject, line, SUBJ_LEN);
    h->subject[SUBJ_LEN] = 0;

    puts("Enter message text. End with a single '.' line.");
    body[0] = 0;

    for (;;)
    {
        char tmp[256];

        term_getline("", tmp, sizeof(tmp));
        if (!strcmp(tmp, "."))
            break;

        if ((strlen(body) + strlen(tmp) + 3) >= sizeof(body))
            break;

        strcat(body, tmp);
        strcat(body, "\n");
    }

    if (!data_store_message_text(body, &firstptr))
        return 0;

    h->msgptr = firstptr;
    return 1;
}

static int msg_delete_by_number(msgno)
long msgno;
{
    long recno;
    MSGHEAD h;
    int i;

    recno = msg_find_recno_by_number(msgno, &h);
    if (recno < 0L)
        return 0;

    if (h.replyto > 0L)
        (void)msg_unlink_child_from_parent(h.replyto, h.number);

    for (i = 0; i < MAX_REPLY_LINKS; i++)
    {
        if (h.replys[i] > 0L)
        {
            MSGHEAD child;
            long crec;

            crec = msg_find_recno_by_number(h.replys[i], &child);
            if (crec >= 0L)
            {
                child.replyto = 0L;
                (void)data_write_msghead(crec, &child);
            }
        }
    }

    if (h.msgptr > 0)
        data_zero_message_chain(h.msgptr);

    memset(&h, 0, sizeof(h));
    return data_write_msghead(recno, &h);
}

static int msg_repair_all_threads(void)
{
    long i, n;
    MSGHEAD h;
    int ok;

    n = msg_count();
    ok = 1;

    for (i = 0L; i < n; i++)
    {
        if (!data_read_msghead(i, &h))
            continue;
        if (!msg_header_valid(&h))
            continue;

        if (!msg_validate_and_repair_header(i, &h))
            ok = 0;
    }

    return ok;
}

/* ------------------------------------------------------------ */
/* public message actions                                       */
/* ------------------------------------------------------------ */

void do_leave_message(void)
{
    MSGHEAD h;
    long recno;

    if (!msg_compose_common(&h))
    {
        puts("Message not saved");
        return;
    }

    if (!msg_append_new_header(&h, &recno))
    {
        puts("Unable to save message");
        return;
    }

    g_sess.user.messages++;
    g_sess.msgs_left++;
    (void)user_save_current();

    g_sess.user.messages++;
    g_sess.msgs_left++;
    (void)user_save_current();

    puts("Message saved");
}

void do_leave_message_to_sysop(void)
{
    MSGHEAD h;
    long recno;
    char body[MAX_MSG_BODY];
    ushort firstptr;

    memset(&h, 0, sizeof(h));
    h.number = msg_next_number();
    h.date = data_pack_date_now();
    h.time = data_pack_time_now();

    strncpy(h.from, g_sess.user.name, NAME_LEN);
    h.from[NAME_LEN] = 0;
    strncpy(h.to, "SYSOP", NAME_LEN);
    h.to[NAME_LEN] = 0;

    term_getline("Subject: ", h.subject, sizeof(h.subject));

    puts("Enter message text. End with a single '.' line.");
    body[0] = 0;

    for (;;)
    {
        char tmp[256];
        term_getline("", tmp, sizeof(tmp));
        if (!strcmp(tmp, "."))
            break;
        if ((strlen(body) + strlen(tmp) + 3) >= sizeof(body))
            break;
        strcat(body, tmp);
        strcat(body, "\n");
    }

    if (!data_store_message_text(body, &firstptr))
    {
        puts("Unable to store message text");
        return;
    }

    h.msgptr = firstptr;

    if (!msg_append_new_header(&h, &recno))
    {
        puts("Unable to save message");
        return;
    }

    g_sess.user.messages++;
    g_sess.msgs_left++;
    (void)user_save_current();

    puts("Message left for SYSOP");
}

void do_leave_message_to_name(void)
{
    do_leave_message();
}

void do_read_messages(void)
{
    if (!msg_repair_all_threads())
        puts("Warning: some message links could not be repaired");

    (void)msg_display_roots(MSGMODE_READ, 0);
}

void do_read_new_messages(void)
{
    if (!msg_repair_all_threads())
        puts("Warning: some message links could not be repaired");

    (void)msg_display_roots(MSGMODE_READ, 1);
}

void do_read_email(void)
{
    (void)msg_display_email(MSGMODE_READ, 0);
}

void do_read_new_email(void)
{
    (void)msg_display_email(MSGMODE_READ, 1);
}

void do_scan_messages(void)
{
    if (!msg_repair_all_threads())
        puts("Warning: some message links could not be repaired");

    (void)msg_display_roots(MSGMODE_SCAN, 0);
}

void do_scan_new_messages(void)
{
    if (!msg_repair_all_threads())
        puts("Warning: some message links could not be repaired");

    (void)msg_display_roots(MSGMODE_SCAN, 1);
}

void do_reply_message(void)
{
    char line[32];
    long parentno;
    long parentrec;
    long childrec;
    long rootno;
    MSGHEAD parent;
    MSGHEAD child;
    char body[MAX_MSG_BODY];
    ushort firstptr;

    term_getline("Reply to message number: ", line, sizeof(line));
    parentno = atol(line);
    if (parentno <= 0L)
        return;

    parentrec = msg_find_recno_by_number(parentno, &parent);
    if (parentrec < 0L)
    {
        puts("Message not found");
        return;
    }

    rootno = msg_find_thread_root(parent.number);
    if (rootno <= 0L)
        rootno = parent.number;

    memset(&child, 0, sizeof(child));
    child.number = msg_next_number();
    child.date = data_pack_date_now();
    child.time = data_pack_time_now();
    child.replyto = parent.number;

    strncpy(child.from, g_sess.user.name, NAME_LEN);
    child.from[NAME_LEN] = 0;
    strncpy(child.to, parent.from, NAME_LEN);
    child.to[NAME_LEN] = 0;

    if (!strnicmp(parent.subject, "Re:", 3))
        strncpy(child.subject, parent.subject, SUBJ_LEN);
    else
        sprintf(child.subject, "Re: %s", parent.subject);
    child.subject[SUBJ_LEN] = 0;

    puts("Enter reply text. End with a single '.' line.");
    body[0] = 0;

    for (;;)
    {
        char tmp[256];
        term_getline("", tmp, sizeof(tmp));
        if (!strcmp(tmp, "."))
            break;
        if ((strlen(body) + strlen(tmp) + 3) >= sizeof(body))
            break;
        strcat(body, tmp);
        strcat(body, "\n");
    }

    if (!data_store_message_text(body, &firstptr))
    {
        puts("Unable to store reply text");
        return;
    }

    child.msgptr = firstptr;

    if (!msg_append_new_header(&child, &childrec))
    {
        puts("Unable to save reply");
        return;
    }

    if (!msg_add_reply_number(&parent, child.number))
    {
        puts("Parent reply list full; reply saved but not linked");
        return;
    }

    if (!data_write_msghead(parentrec, &parent))
    {
        puts("Reply saved but parent link update failed");
        return;
    }

    puts("Reply saved");
}

void do_delete_message(void)
{
    char line[32];
    long msgno;

    term_getline("Delete message number: ", line, sizeof(line));
    msgno = atol(line);
    if (msgno <= 0L)
        return;

    if (!sysop_password_prompt())
        return;

    if (!msg_delete_by_number(msgno))
    {
        puts("Delete failed");
        return;
    }

    puts("Message deleted");
}

void do_purge_messages(void)
{
    long i, n;
    MSGHEAD h;
    int purged;

    if (!sysop_password_prompt())
        return;

    purged = 0;
    n = msg_count();

    for (i = 0L; i < n; i++)
    {
        if (!data_read_msghead(i, &h))
            continue;
        if (!msg_header_valid(&h))
            continue;

        if (!msg_text_chain_valid(h.msgptr))
        {
            (void)msg_delete_by_number(h.number);
            purged++;
            continue;
        }

        if (!msg_validate_and_repair_header(i, &h))
            continue;
    }

    printf("%d damaged/orphaned message(s) purged\n", purged);
}