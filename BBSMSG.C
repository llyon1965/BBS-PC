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
 * - stronger message/thread traversal
 * - safe reply-chain walking
 * - broken-link detection
 * - orphaned text-chain protection
 * - conservative purge logic
 *
 * Notes:
 * - This pass keeps the legacy on-disk structures intact.
 * - It does not attempt to redesign message storage.
 * - It assumes:
 *      MSGHEAD.number      = public message number
 *      MSGHEAD.replyto     = parent message number or 0
 *      MSGHEAD.replys[]    = reply message numbers, 0-terminated/unused
 *      MSGHEAD.msgptr      = first MSGTEXT record
 * - Where historical field names differ slightly in your tree, align the
 *   field references in the obvious places.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "bbsdata.h"
#include "bbsfunc.h"

#ifndef MAX_MSG_SCAN
#define MAX_MSG_SCAN 32767L
#endif

#ifndef MAX_REPLY_LINKS
#define MAX_REPLY_LINKS 16
#endif

#ifndef MAX_MSG_BODY
#define MAX_MSG_BODY 8192
#endif

/* ------------------------------------------------------------ */
/* local helpers                                                */
/* ------------------------------------------------------------ */

static int msg_is_blank(h)
MSGHEAD *h;
{
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

static int msg_load_by_recno(recno, h)
long recno;
MSGHEAD *h;
{
    if (recno < 0L || recno >= msg_count())
        return 0;

    if (!data_read_msghead(recno, h))
        return 0;

    return msg_header_valid(h);
}

static long msg_find_recno_by_number(msgno, out)
long msgno;
MSGHEAD *out;
{
    return data_find_msg_by_number(msgno, out);
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

static int msg_reply_link_valid(h, idx)
MSGHEAD *h;
int idx;
{
    MSGHEAD child;
    long crec;
    long childno;

    if (!h || idx < 0 || idx >= MAX_REPLY_LINKS)
        return 0;

    childno = h->replys[idx];
    if (!msg_reply_slot_used(childno))
        return 1;

    if (childno == h->number)
        return 0;

    crec = msg_find_recno_by_number(childno, &child);
    if (crec < 0L)
        return 0;

    if (child.replyto != h->number)
        return 0;

    return 1;
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

static void msg_show_header(h)
MSGHEAD *h;
{
    char d[32], t[32];

    if (!h)
        return;

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

static int msg_read_one_by_number(msgno)
long msgno;
{
    MSGHEAD h;
    long recno;

    recno = msg_find_recno_by_number(msgno, &h);
    if (recno < 0L)
        return 0;

    (void)msg_validate_and_repair_header(recno, &h);

    msg_show_header(&h);
    msg_show_body(&h);
    return 1;
}

static long msg_next_visible_after(cur_msgno)
long cur_msgno;
{
    long i, n;
    MSGHEAD h;
    long best;

    n = msg_count();
    best = 0L;

    for (i = 0L; i < n; i++)
    {
        if (!data_read_msghead(i, &h))
            continue;
        if (!msg_header_valid(&h))
            continue;

        if (h.number > cur_msgno)
        {
            if (!best || h.number < best)
                best = h.number;
        }
    }

    return best;
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

static long msg_next_number(void)
{
    return data_highest_msg_number() + 1L;
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

static int msg_reply_chain_walk(startno, level)
long startno;
int level;
{
    MSGHEAD h;
    long recno;
    int i;

    recno = msg_find_recno_by_number(startno, &h);
    if (recno < 0L)
        return 0;

    for (i = 0; i < level; i++)
        printf("  ");
    printf("#%ld %s -> %s : %s\n", h.number, h.from, h.to, h.subject);

    for (i = 0; i < MAX_REPLY_LINKS; i++)
        if (h.replys[i] > 0L)
            (void)msg_reply_chain_walk(h.replys[i], level + 1);

    return 1;
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

    puts("Message saved");
}

void do_leave_message_to_sysop(void)
{
    MSGHEAD h;
    long recno;

    memset(&h, 0, sizeof(h));
    h.number = msg_next_number();
    h.date = data_pack_date_now();
    h.time = data_pack_time_now();

    strncpy(h.from, g_sess.user.name, NAME_LEN);
    h.from[NAME_LEN] = 0;
    strncpy(h.to, "SYSOP", NAME_LEN);
    h.to[NAME_LEN] = 0;

    term_getline("Subject: ", h.subject, sizeof(h.subject));

    {
        char body[MAX_MSG_BODY];
        ushort firstptr;

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
    }

    if (!msg_append_new_header(&h, &recno))
    {
        puts("Unable to save message");
        return;
    }

    puts("Message left for SYSOP");
}

void do_leave_message_to_name(void)
{
    do_leave_message();
}

void do_read_messages(void)
{
    long msgno;

    if (!msg_repair_all_threads())
        puts("Warning: some message links could not be repaired");

    msgno = 0L;
    for (;;)
    {
        msgno = msg_next_visible_after(msgno);
        if (!msgno)
            break;

        if (!msg_read_one_by_number(msgno))
            continue;

        if (term_yesno("Continue (Y/N)? ", 1))
            continue;
        break;
    }
}

void do_read_email(void)
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
        if (!data_user_match(h.to, g_sess.user.name))
            continue;

        (void)msg_validate_and_repair_header(i, &h);
        msg_show_header(&h);
        msg_show_body(&h);

        if (!term_yesno("Continue (Y/N)? ", 1))
            break;
    }
}

void do_scan_messages(void)
{
    long i, n;
    MSGHEAD h;

    if (!msg_repair_all_threads())
        puts("Warning: some message links could not be repaired");

    n = msg_count();
    for (i = 0L; i < n; i++)
    {
        if (!data_read_msghead(i, &h))
            continue;
        if (!msg_header_valid(&h))
            continue;

        printf("#%ld  %-20s -> %-20s  %s\n",
               h.number, h.from, h.to, h.subject);
    }
}

void do_reply_message(void)
{
    char line[32];
    long parentno;
    long parentrec;
    long childrec;
    MSGHEAD parent;
    MSGHEAD child;

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

    memset(&child, 0, sizeof(child));
    child.number = msg_next_number();
    child.date = data_pack_date_now();
    child.time = data_pack_time_now();
    child.replyto = parent.number;

    strncpy(child.from, g_sess.user.name, NAME_LEN);
    child.from[NAME_LEN] = 0;
    strncpy(child.to, parent.from, NAME_LEN);
    child.to[NAME_LEN] = 0;

    {
        char subj[SUBJ_LEN + 8];
        if (!strnicmp(parent.subject, "Re:", 3))
            strncpy(subj, parent.subject, sizeof(subj) - 1);
        else
            sprintf(subj, "Re: %s", parent.subject);

        subj[sizeof(subj) - 1] = 0;
        strncpy(child.subject, subj, SUBJ_LEN);
        child.subject[SUBJ_LEN] = 0;
    }

    {
        char body[MAX_MSG_BODY];
        ushort firstptr;

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
    }

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