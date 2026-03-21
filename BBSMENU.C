/* BBSMENU.C
 *
 * BBS-PC! 4.21
 *
 * Tightened compiled .MEN loader/executor
 *
 * Supports:
 * - compiled .MEN binary files
 * - fallback interim text format
 * - IRET records
 * - chained menu commands
 * - stack-based menu navigation
 *
 * Tightened:
 * - access control enforced correctly
 * - access_mode 7 prompts only at execution time
 * - return levels/top behave correctly
 * - nested menus do not leak stack/state
 * - opcode 35 explicitly dispatches to do_node_editor()
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "bbsdata.h"
#include "bbsfunc.h"

#ifndef BBSFUNC_INCLUDED
#error "BBSMENU.C requires BBSFUNC.H to be included"
#endif

/* ------------------------------------------------------------ */
/* Opcode sanity checks                                         */
/* ------------------------------------------------------------ */

#if MF_READ_MSGS != 11
#error "Opcode mismatch: MF_READ_MSGS must be 11"
#endif

#if MF_READ_EMAIL != 12
#error "Opcode mismatch: MF_READ_EMAIL must be 12"
#endif

#if MF_SCAN_MSGS != 13
#error "Opcode mismatch: MF_SCAN_MSGS must be 13"
#endif

#if MF_EXIT_SYS != 100
#error "Opcode mismatch: MF_EXIT_SYS must be 100"
#endif

#if MF_SHOW_MENU != 101
#error "Opcode mismatch: MF_SHOW_MENU must be 101"
#endif

#if MF_RET_LEVELS != 102
#error "Opcode mismatch: MF_RET_LEVELS must be 102"
#endif

#if MF_RET_TOP != 103
#error "Opcode mismatch: MF_RET_TOP must be 103"
#endif

#if MF_CALL_MENU != 104
#error "Opcode mismatch: MF_CALL_MENU must be 104"
#endif

#if MF_GOTO_MENU != 105
#error "Opcode mismatch: MF_GOTO_MENU must be 105"
#endif

#if MF_EXEC_EXT != 111
#error "Opcode mismatch: MF_EXEC_EXT must be 111"
#endif

typedef struct {
    ushort total_lines;
    ushort rec_count;
    ushort reserved1;
    ushort reserved2;
    byte   display_option;
    byte   input_mode;
    byte   reserved[38];
} MENHEAD;

typedef struct {
    byte   type;
    byte   key;
    byte   function;
    byte   priv_lo;
    byte   priv_hi;
    byte   boost;
    byte   access_mode;
    ushort section_mask;
    char   parm[13];
} MENREC;

#define MEN_MAX_LINES   64
#define MEN_HDR_SIZE    46
#define MEN_REC_SIZE    22

/* ------------------------------------------------------------ */

static ushort rd_u16_le(p)
unsigned char *p;
{
    return (ushort)(p[0] | (p[1] << 8));
}

static void split_pipe(line, part, maxp, count)
char *line;
char part[][MAX_PATHNAME];
int maxp;
int *count;
{
    char *p;
    int n = 0;

    *count = 0;
    p = strtok(line, "|");
    while (p && n < maxp)
    {
        strncpy(part[n], p, MAX_PATHNAME - 1);
        part[n][MAX_PATHNAME - 1] = 0;
        n++;
        p = strtok(NULL, "|");
    }
    *count = n;
}

static char *dup_cstr_from(buf, buflen, pos)
unsigned char *buf;
long buflen;
long pos;
{
    long end;
    char *s;
    int len;

    if (pos < 0L || pos >= buflen)
        return NULL;

    end = pos;
    while (end < buflen && buf[end] != 0)
        end++;

    len = (int)(end - pos);
    s = (char *)malloc((unsigned)(len + 1));
    if (!s)
        return NULL;

    memcpy(s, buf + pos, len);
    s[len] = 0;
    return s;
}

/* ------------------------------------------------------------ */
/* menu frame helpers                                           */
/* ------------------------------------------------------------ */

static void menu_clear_slot(idx)
int idx;
{
    if (idx < 0 || idx >= MAX_MENU_STACK)
        return;

    memset(&g_sess.mstack.menu[idx], 0, sizeof(MENUFILE));
}

static void menu_clear_slots_from(idx)
int idx;
{
    while (idx < MAX_MENU_STACK)
    {
        menu_clear_slot(idx);
        idx++;
    }
}

/* ------------------------------------------------------------ */
/* compiled header/record decoding                              */
/* ------------------------------------------------------------ */

static void decode_header(buf, h)
unsigned char *buf;
MENHEAD *h;
{
    memset(h, 0, sizeof(*h));

    h->total_lines    = rd_u16_le(buf + 0);
    h->rec_count      = (ushort)((h->total_lines >= 2) ? (h->total_lines - 2) : 0);
    h->reserved1      = rd_u16_le(buf + 2);
    h->reserved2      = rd_u16_le(buf + 4);
    h->display_option = buf[8];
    h->input_mode     = buf[9];
    memcpy(h->reserved, buf + 10, 36);
}

static void decode_record(buf, r)
unsigned char *buf;
MENREC *r;
{
    memset(r, 0, sizeof(*r));

    r->type         = buf[0];
    r->key          = buf[1];
    r->function     = buf[2];
    r->priv_lo      = buf[3];
    r->priv_hi      = buf[4];
    r->boost        = buf[5];
    r->access_mode  = buf[6];
    r->section_mask = rd_u16_le(buf + 7);
    memcpy(r->parm, buf + 9, 12);
    r->parm[12] = 0;
}

static void access_spec_from_record(dst, r)
char *dst;
MENREC *r;
{
    char pbuf[32];
    char mbuf[32];

    pbuf[0] = 0;
    mbuf[0] = 0;

    if (r->priv_lo == 0xFF && r->priv_hi == 0x00)
        strcpy(pbuf, "-");
    else if (r->priv_lo == 0xFF)
        sprintf(pbuf, "%u-", (unsigned)r->priv_hi);
    else if (r->priv_hi == 0x00)
        sprintf(pbuf, "-%u", (unsigned)r->priv_lo);
    else
        sprintf(pbuf, "%u-%u", (unsigned)r->priv_lo, (unsigned)r->priv_hi);

    if (r->section_mask == 0x0000)
        sprintf(mbuf, "%u", (unsigned)r->access_mode);
    else
    {
        int bit, found = -1, notfound = -1;

        for (bit = 0; bit < NUM_SECT; bit++)
        {
            if (r->section_mask == (1U << bit))
                found = bit;
            if (r->section_mask == (ushort)(0xFFFFU ^ (1U << bit)))
                notfound = bit;
        }

        if (found >= 0)
            sprintf(mbuf, "%u:%X", (unsigned)r->access_mode, found);
        else if (notfound >= 0)
            sprintf(mbuf, "%u:-%X", (unsigned)r->access_mode, notfound);
        else
            sprintf(mbuf, "%u:%04X", (unsigned)r->access_mode,
                (unsigned)r->section_mask);
    }

    sprintf(dst, "%s,%s", pbuf, mbuf);
}

/* ------------------------------------------------------------ */
/* compiled binary .MEN loader                                  */
/* ------------------------------------------------------------ */

static int menu_load_binary(fname, m)
char *fname;
MENUFILE *m;
{
    FILE *fp;
    unsigned char *buf;
    long buflen;
    MENHEAD h;
    long str_off;
    char *strings[MEN_MAX_LINES + 8];
    int scount = 0;
    int si, i, extra;
    MENUITEM *it;

    memset(strings, 0, sizeof(strings));

    fp = fopen(fname, "rb");
    if (!fp)
        return 0;

    fseek(fp, 0L, SEEK_END);
    buflen = ftell(fp);
    fseek(fp, 0L, SEEK_SET);

    if (buflen < MEN_HDR_SIZE)
    {
        fclose(fp);
        return 0;
    }

    buf = (unsigned char *)malloc((unsigned)buflen);
    if (!buf)
    {
        fclose(fp);
        return 0;
    }

    if (fread(buf, 1, (unsigned)buflen, fp) != (unsigned)buflen)
    {
        free(buf);
        fclose(fp);
        return 0;
    }
    fclose(fp);

    decode_header(buf, &h);

    if (h.rec_count <= 0 || h.rec_count > MEN_MAX_LINES)
    {
        free(buf);
        return 0;
    }

    str_off = MEN_HDR_SIZE + (long)h.rec_count * MEN_REC_SIZE;
    if (str_off >= buflen)
    {
        free(buf);
        return 0;
    }

    memset(m, 0, sizeof(*m));
    strncpy(m->filename, fname, sizeof(m->filename) - 1);
    m->display_option = h.display_option;
    m->input_mode = h.input_mode;

    {
        long pos = str_off;
        while (pos < buflen && scount < (MEN_MAX_LINES + 8))
        {
            strings[scount++] = dup_cstr_from(buf, buflen, pos);
            while (pos < buflen && buf[pos] != 0)
                pos++;
            pos++;
        }
    }

    si = 0;
    while (si < scount)
    {
        if (strings[si] && strings[si][0])
        {
            strncpy(m->prompt, strings[si], sizeof(m->prompt) - 1);
            si++;
            break;
        }
        si++;
    }

    extra = (scount - si) - (int)h.rec_count;
    while (extra > 0 && m->item_count < MAX_MENU_ITEMS && si < scount)
    {
        it = &m->item[m->item_count];
        memset(it, 0, sizeof(*it));

        it->key = 0;
        if (strings[si])
            strncpy(it->text, strings[si], sizeof(it->text) - 1);

        it->in_use = 1;
        it->rec_type = 0;
        it->is_iret = 0;

        m->item_count++;
        si++;
        extra--;
    }

    for (i = 0; i < (int)h.rec_count && m->item_count < MAX_MENU_ITEMS; i++)
    {
        MENREC r;
        long rp;

        rp = MEN_HDR_SIZE + (long)i * MEN_REC_SIZE;
        decode_record(buf + rp, &r);

        it = &m->item[m->item_count];
        memset(it, 0, sizeof(*it));

        if (r.type == 0)
            it->key = 0;
        else
            it->key = (char)r.key;

        it->function = r.function;
        it->access_mode = r.access_mode;
        access_spec_from_record(it->access_spec, &r);

        if (strings[si])
            strncpy(it->text, strings[si], sizeof(it->text) - 1);
        si++;

        if (r.parm[0])
            strncpy(it->parm, r.parm, sizeof(it->parm) - 1);
        else if (r.boost)
            sprintf(it->parm, "%u", (unsigned)r.boost);

        it->in_use = 1;
        it->rec_type = r.type;
        it->is_iret = (r.type == 2) ? 1 : 0;
        it->boost = r.boost;
        it->section_mask = r.section_mask;

        if (r.priv_lo != 0xFF)
        {
            it->has_lo = 1;
            it->priv_lo = r.priv_lo;
        }
        if (r.priv_hi != 0x00)
        {
            it->has_hi = 1;
            it->priv_hi = r.priv_hi;
        }

        m->item_count++;
    }

    for (i = 0; i < scount; i++)
        if (strings[i]) free(strings[i]);
    free(buf);
    return 1;
}

/* ------------------------------------------------------------ */
/* fallback interim text loader                                 */
/* ------------------------------------------------------------ */

static int menu_load_text(fname, m)
char *fname;
MENUFILE *m;
{
    FILE *fp;
    char line[256];
    char raw[256];
    char part[8][MAX_PATHNAME];
    int count;
    MENUITEM *it;

    memset(m, 0, sizeof(*m));
    strncpy(m->filename, fname, sizeof(m->filename) - 1);

    fp = fopen(fname, "rt");
    if (!fp)
        return 0;

    while (fgets(line, sizeof(line), fp))
    {
        char *p;

        p = strchr(line, '\n'); if (p) *p = 0;
        p = strchr(line, '\r'); if (p) *p = 0;
        if (!line[0])
            continue;

        strcpy(raw, line);
        split_pipe(raw, part, 8, &count);

        if (count <= 0)
            continue;

        if (!stricmp(part[0], "MENU"))
        {
            if (count >= 4)
            {
                strncpy(m->prompt, part[1], sizeof(m->prompt) - 1);
                m->display_option = atoi(part[2]);
                m->input_mode = atoi(part[3]);
            }
        }
        else if (!stricmp(part[0], "TITLE"))
        {
            if (m->item_count >= MAX_MENU_ITEMS)
                break;

            it = &m->item[m->item_count];
            memset(it, 0, sizeof(*it));

            it->key = 0;
            if (count >= 2)
                strncpy(it->text, part[1], sizeof(it->text) - 1);

            it->in_use = 1;
            it->rec_type = 0;
            it->is_iret = 0;

            m->item_count++;
        }
        else if (!stricmp(part[0], "ITEM") || !stricmp(part[0], "IRET"))
        {
            if (m->item_count >= MAX_MENU_ITEMS)
                break;

            it = &m->item[m->item_count];
            memset(it, 0, sizeof(*it));

            if (count >= 6)
            {
                it->key = part[1][0];
                strncpy(it->text, part[2], sizeof(it->text) - 1);
                strncpy(it->access_spec, part[3], sizeof(it->access_spec) - 1);
                it->access_mode = (byte)atoi(part[4]);
                it->function = (byte)atoi(part[5]);
                if (count >= 7)
                    strncpy(it->parm, part[6], sizeof(it->parm) - 1);

                it->in_use = 1;
                it->rec_type = !stricmp(part[0], "IRET") ? 2 : 1;
                it->is_iret = !stricmp(part[0], "IRET") ? 1 : 0;

                m->item_count++;
            }
        }
    }

    fclose(fp);
    return 1;
}

/* ------------------------------------------------------------ */

int menu_load(fname, m)
char *fname;
MENUFILE *m;
{
    if (menu_load_binary(fname, m))
        return 1;

    return menu_load_text(fname, m);
}

MENUFILE *menu_current(void)
{
    if (g_sess.mstack.sp <= 0)
        return NULL;

    return &g_sess.mstack.menu[g_sess.mstack.sp - 1];
}

int menu_push(fname)
char *fname;
{
    MENUFILE *m;

    if (g_sess.mstack.sp >= MAX_MENU_STACK)
    {
        puts("% Menu stack overflow %");
        return 0;
    }

    m = &g_sess.mstack.menu[g_sess.mstack.sp];
    menu_clear_slot(g_sess.mstack.sp);

    if (!menu_load(fname, m))
        return 0;

    g_sess.mstack.sp++;
    node_mark_menu(fname);
    return 1;
}

int menu_pop(void)
{
    if (g_sess.mstack.sp <= 1)
        return 0;

    g_sess.mstack.sp--;
    menu_clear_slot(g_sess.mstack.sp);

    if (g_sess.mstack.sp > 0)
        node_mark_menu(g_sess.mstack.menu[g_sess.mstack.sp - 1].filename);

    return 1;
}

/* ------------------------------------------------------------ */
/* explicit stack helpers                                       */
/* ------------------------------------------------------------ */

static int menu_replace_current(fname)
char *fname;
{
    MENUFILE m;

    if (g_sess.mstack.sp <= 0)
        return menu_push(fname);

    memset(&m, 0, sizeof(m));
    if (!menu_load(fname, &m))
        return 0;

    g_sess.mstack.menu[g_sess.mstack.sp - 1] = m;
    node_mark_menu(fname);
    return 1;
}

static void menu_return_top_level_stack(void)
{
    if (g_sess.mstack.sp <= 0)
        return;

    menu_clear_slots_from(1);
    g_sess.mstack.sp = 1;
    node_mark_menu(g_sess.mstack.menu[0].filename);
}

/* ------------------------------------------------------------ */
/* display/access helpers                                       */
/* ------------------------------------------------------------ */

static bits menu_all_sections_mask(void)
{
    return (bits)0xFFFFU;
}

static bits menu_item_mask(it)
MENUITEM *it;
{
    if (!it)
        return 0;

    if (it->section_mask == 0)
        return menu_all_sections_mask();

    return it->section_mask;
}

static int menu_privilege_allows(it)
MENUITEM *it;
{
    int p;

    if (!it)
        return 0;

    p = (int)g_sess.user.seclevel;

    if (it->has_lo && p < it->priv_lo)
        return 0;
    if (it->has_hi && p > it->priv_hi)
        return 0;

    return 1;
}

static bits menu_visible_mask_for_mode(mode)
int mode;
{
    switch (mode)
    {
        case 0:
            return (g_sess.user.rd_acc |
                    g_sess.user.wr_acc |
                    g_sess.user.up_acc |
                    g_sess.user.dn_acc |
                    g_sess.user.sys_acc);

        case 1:
            return ((g_sess.user.rd_acc |
                     g_sess.user.wr_acc |
                     g_sess.user.up_acc |
                     g_sess.user.dn_acc |
                     g_sess.user.sys_acc) & (~g_sess.user.sect_mask));

        case 2:
            return (g_sess.user.rd_acc & (~g_sess.user.sect_mask));

        case 3:
            return (g_sess.user.wr_acc & (~g_sess.user.sect_mask));

        case 4:
            return (g_sess.user.dn_acc & (~g_sess.user.sect_mask));

        case 5:
            return (g_sess.user.up_acc & (~g_sess.user.sect_mask));

        case 6:
            return menu_all_sections_mask();

        case 7:
            return g_sess.user.sys_acc;

        default:
            return 0;
    }
}

static int menu_access_visible(it)
MENUITEM *it;
{
    bits mask;
    bits visible;

    if (!it)
        return 0;

    mask = menu_item_mask(it);
    visible = menu_visible_mask_for_mode(it->access_mode);

    return ((visible & mask) != 0);
}

static int menu_access_execute(it)
MENUITEM *it;
{
    bits mask;

    if (!it)
        return 0;

    mask = menu_item_mask(it);

    switch (it->access_mode)
    {
        case 6:
            return 1;

        case 7:
            if ((g_sess.user.sys_acc & mask) == 0)
                return 0;
            return sysop_password_prompt();

        default:
            return menu_access_visible(it);
    }
}

static int item_visible(m, idx)
MENUFILE *m;
int idx;
{
    MENUITEM *it;

    it = &m->item[idx];
    if (!it->in_use)
        return 1;

    if (it->rec_type == 0)
        return 1;

    if (!menu_privilege_allows(it))
        return 0;

    return menu_access_visible(it);
}

void menu_display(m)
MENUFILE *m;
{
    int i;

    puts("");
    for (i = 0; i < m->item_count; i++)
    {
        if (!item_visible(m, i))
            continue;

        if (m->item[i].text[0])
            puts(m->item[i].text);
    }
    puts("");
    printf("%s", m->prompt[0] ? m->prompt : "Command: ");
}

static MENUITEM *menu_find_key(m, ch, out_idx)
MENUFILE *m;
int ch;
int *out_idx;
{
    int i;
    char c = (char)toupper((unsigned char)ch);

    for (i = 0; i < m->item_count; i++)
    {
        if (!item_visible(m, i))
            continue;

        if (m->item[i].key &&
            toupper((unsigned char)m->item[i].key) == c)
        {
            if (out_idx)
                *out_idx = i;
            return &m->item[i];
        }
    }

    return NULL;
}

/* ------------------------------------------------------------ */
/* chaining / return helpers                                    */
/* ------------------------------------------------------------ */

static int menu_execute_key(m, ch)
MENUFILE *m;
int ch;

static void apply_boost(it)
MENUITEM *it;
{
    if (!it || !it->boost)
        return;

    g_sess.user.seclevel = it->boost;
    user_save_current();
}

static int do_return_levels_from_item(it)
MENUITEM *it;
{
    int levels;
    int i;

    levels = atoi(it->parm);
    if (levels <= 0)
        levels = 1;

    for (i = 0; i < levels; i++)
    {
        if (!menu_pop())
            break;
    }

    return 1;
}

static int execute_chain(chain)
char *chain;
{
    char local[128];
    char *p;

    if (!chain || !*chain)
        return 1;

    strncpy(local, chain, sizeof(local) - 1);
    local[sizeof(local) - 1] = 0;

    p = strchr(local, ';');
    while (p)
    {
        char cmd = *(p + 1);
        if (cmd)
        {
            if (!menu_execute_key(menu_current(), cmd))
                return 0;
        }
        p = strchr(p + 1, ';');
    }

    return 1;
}

/* ------------------------------------------------------------ */
/* dispatch                                                     */
/* ------------------------------------------------------------ */

static int menu_dispatch_index(m, idx)
MENUFILE *m;
int idx;
{
    MENUITEM *it;
    int ok = 1;

    it = &m->item[idx];

    if (!menu_privilege_allows(it))
    {
        puts("Access denied");
        return 1;
    }

    if (!menu_access_execute(it))
    {
        puts("Access denied");
        return 1;
    }

    switch (it->function)
    {
        case MF_EXIT_SYS:
            g_sess.running = 0;
            break;

        case MF_SHOW_MENU:
            break;

        case MF_RET_LEVELS:
            ok = do_return_levels_from_item(it);
            break;

        case MF_RET_TOP:
            menu_return_top_level_stack();
            break;

        case MF_CALL_MENU:
            if (!menu_push(it->parm))
                puts("Can't call menu");
            break;

        case MF_GOTO_MENU:
            if (!menu_replace_current(it->parm))
                puts("Can't goto menu");
            break;

        case MF_CLS_TYPE:
            cls_type_text_file(it->parm, 1);
            break;

        case MF_TYPE_FILE:
            type_text_file(it->parm, 1);
            break;

        case MF_CLS_TYPE_NS:
            cls_type_text_file(it->parm, 0);
            break;

        case MF_TYPE_FILE_NS:
            type_text_file(it->parm, 0);
            break;

        case MF_CHG_MENUSET:
            do_change_menu_sets();
            break;

        case MF_EXEC_EXT:
            do_execute_external_program(it->parm);
            break;

        case MF_USER_STATS:
            do_user_statistics();
            break;

        case MF_SECTION_NAMES:
            do_section_names();
            break;

        case MF_CHANGE_MASK:
            do_change_section_mask();
            break;

        case MF_REGISTER:
            do_register_user();
            break;

        case MF_USER_EDIT:
            do_user_edit();
            break;

        case MF_CALLER_LOG:
            do_print_caller_log();
            break;

        case MF_TIME_ON_SYS:
            do_time_on_system();
            break;

        case MF_CHAT_SYSOP:
            do_chat_with_sysop();
            break;

        case MF_EXPERT_TOGGLE:
            do_expert_toggle();
            break;

        case MF_LEAVE_MSG:
            node_mark_message_activity();
            do_leave_message();
            break;

        case MF_LEAVE_SYSOP:
            node_mark_message_activity();
            do_leave_message_to_sysop();
            break;

        case MF_READ_MSGS:
            do_read_messages();
            break;

        case MF_READ_NEW_MSGS:
            do_read_new_messages();
            break;

        case MF_READ_EMAIL:
            do_read_email();
            break;

        case MF_READ_NEW_EMAIL:
            do_read_new_email();
            break;

        case MF_SCAN_MSGS:
            do_scan_messages();
            break;

        case MF_SCAN_NEW_MSGS:
            do_scan_new_messages();
            break;

        case MF_DELETE_MSG:
            node_mark_message_activity();
            do_delete_message();
            break;

        case MF_PRINT_CAT:
            do_print_catalog();
            break;

        case MF_BROWSE_FILES:
            do_browse_files();
            break;

        case MF_UPLOAD_FILE:
            node_mark_file_activity();
            do_upload_file();
            break;

        case MF_UPLOAD_LOCAL:
            node_mark_file_activity();
            do_upload_local();
            break;

        case MF_DOWNLOAD_FILE:
            node_mark_file_activity();
            do_download_file();
            break;

        case MF_READ_FILE:
            do_read_file();
            break;

        case MF_KILL_FILE:
            node_mark_file_activity();
            do_kill_file();
            break;

        case MF_ADD_USER:
            node_mark_user_activity();
            do_add_user();
            break;

        case MF_DEL_USER:
            node_mark_user_activity();
            do_delete_user();
            break;

        case MF_CHG_USER:
            node_mark_user_activity();
            do_change_user();
            break;

        case MF_PURGE_USERS:
            node_mark_user_activity();
            do_purge_inactive_users();
            break;

        case MF_PURGE_MSGS:
            node_mark_message_activity();
            do_purge_messages();
            break;

        case MF_PRINT_USERS:
            do_print_user_list();
            break;

        case MF_RESET_BULL:
            do_reset_bulletin_flags();
            break;

        case MF_UPDATE_DEF:
            do_update_user_defaults();
            break;

        case MF_DEF_SECTIONS:
            do_define_section_names();
            break;

        case MF_DEF_TERMS:
            do_define_terminal_types();
            break;

        case MF_MODEM_DEF:
            do_modem_defaults();
            break;

        case MF_USER_DEF:
            do_user_defaults();
            break;

        case MF_SYS_DEF:
            do_system_defaults();
            break;

        case MF_NODE_DEF:
            do_node_editor();
            break;

        case MF_LIST_PHONE:
            do_list_phone_directory();
            break;

        case MF_CHG_PHONE:
            do_change_phone_listing();
            break;

        case MF_DIAL_REMOTE:
            do_dial_connect_remote();
            break;

        case MF_DIAL_UNLISTED:
            do_unlisted_dial_connect();
            break;

        case MF_UPLOAD_DIRECT:
            do_upload_direct();
            break;

        case MF_DNL_DIRECT:
            do_download_direct();
            break;

        case MF_KILL_DIRECT:
            do_direct_file_kill();
            break;

        case MF_DOS_GATE:
            do_dos_gate();
            break;

        case MF_SRCH_CATDESC:
            do_search_catdesc();
            break;

        case MF_NEW_FILES:
            do_new_files();
            break;

        default:
            printf("Function %d not implemented yet\n", it->function);
            break;
    }

    apply_boost(it);

    if (it->parm[0] && strchr(it->parm, ';'))
        ok = execute_chain(it->parm);

    if (it->is_iret)
    {
        if (!menu_pop())
            g_sess.running = 0;
    }

    return ok;
}

static int menu_execute_key(m, ch)
MENUFILE *m;
int ch;
{
    int idx;
    MENUITEM *it;

    it = menu_find_key(m, ch, &idx);
    if (!it)
    {
        puts("?");
        return 1;
    }

    return menu_dispatch_index(m, idx);
}

int menu_execute(m)
MENUFILE *m;
{
    int ch;
    int flushch;

    ch = getchar();
    while (ch == '\r' || ch == '\n')
        ch = getchar();

    do
    {
        flushch = getchar();
    } while (flushch != '\n' && flushch != EOF);

    return menu_execute_key(m, ch);
}