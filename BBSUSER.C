/* BBSUSER.C
 *
 * BBS-PC! 4.21
 *
 * Reconstructed and modernised source
 * derived from BBS-PC 4.20
 *
 * User subsystem.
 *
 * This pass keeps higher-level code working with USRDESC only,
 * while on-disk fidelity is handled underneath by BBSDATA.C
 * and BBSISAM.C through USERREC conversions.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bbsdata.h"
#include "bbsfunc.h"

#ifndef DEFAULT_NEWUSER_SECLEVEL
#define DEFAULT_NEWUSER_SECLEVEL 10
#endif

#ifndef DEFAULT_NEWUSER_PROTOCOL
#define DEFAULT_NEWUSER_PROTOCOL 0
#endif

#ifndef DEFAULT_NEWUSER_MENUSET
#define DEFAULT_NEWUSER_MENUSET 0
#endif

/* ------------------------------------------------------------ */
/* shared helpers used by login/user flows                      */
/* ------------------------------------------------------------ */

void user_zero(u)
USRDESC *u;
{
    if (!u)
        return;

    memset(u, 0, sizeof(*u));
}

static int user_default_member_seclevel(void)
{
    if (g_cfg.priv[1] > 0)
        return (int)g_cfg.priv[1];

    return DEFAULT_NEWUSER_SECLEVEL;
}

static int user_default_member_minutes(void)
{
    if (g_cfg.limit[1] > 0)
        return (int)g_cfg.limit[1];

    return 0;
}

void user_apply_newuser_defaults(u)
USRDESC *u;
{
    int i;

    if (!u)
        return;

    user_zero(u);

    u->phone[0]    = 0;
    u->messages    = 0;
    u->protocol    = DEFAULT_NEWUSER_PROTOCOL;
    u->expert      = 0;
    u->seclevel    = (byte)user_default_member_seclevel();
    u->dnldratio   = 0;
    u->menu_set    = DEFAULT_NEWUSER_MENUSET;
    u->term        = 0;
    u->cls[0]      = 12;
    u->cls[1]      = 0;
    u->cls[2]      = 0;
    u->cls[3]      = 0;
    u->bs[0]       = 8;
    u->bs[1]       = 0;
    u->bs[2]       = 0;
    u->page_width  = 80;
    u->page_len    = 24;
    u->linefeeds   = 1;
    u->nuls        = 0;
    u->time_limit  = (ushort)user_default_member_minutes();
    u->highmsgread = 0L;

    u->rd_acc = 0;
    u->wr_acc = 0;
    u->up_acc = 0;
    u->dn_acc = 0;
    u->sys_acc = 0;
    u->sect_mask = 0;
    u->bull_mask = 0;

    for (i = 0; i < NUM_SECT; i++)
    {
        u->rd_acc |= g_cfg.rd_acc[i];
        u->wr_acc |= g_cfg.wr_acc[i];
        u->up_acc |= g_cfg.up_acc[i];
        u->dn_acc |= g_cfg.dn_acc[i];
    }

    if (u->protocol > 6)
        u->protocol = 0;

    if (u->menu_set >= NUM_MENUSET)
        u->menu_set = 0;
}

/* ------------------------------------------------------------ */
/* local helpers                                                */
/* ------------------------------------------------------------ */

static int user_is_blank(u)
USRDESC *u;
{
    if (!u)
        return 1;

    return (u->name[0] == 0);
}

static void user_apply_runtime_sanity(u)
USRDESC *u;
{
    if (!u)
        return;

    if (u->protocol > 6)
        u->protocol = 0;

    if (u->menu_set >= NUM_MENUSET)
        u->menu_set = 0;
}

static void user_copy_name(dst, src, maxlen)
char *dst;
char *src;
int maxlen;
{
    if (!dst || maxlen <= 0)
        return;

    if (!src)
        src = "";

    strncpy(dst, src, maxlen);
    dst[maxlen] = 0;
}

static int user_prompt_name(prompt, out, outlen)
char *prompt;
char *out;
int outlen;
{
    term_getline(prompt, out, outlen);
    data_trim_crlf(out);
    return out[0] ? 1 : 0;
}

static int user_prompt_password(prompt, out, outlen)
char *prompt;
char *out;
int outlen;
{
    term_getline_hidden(prompt, out, outlen);
    data_trim_crlf(out);
    return out[0] ? 1 : 0;
}

static void user_prompt_city(prompt, out, outlen)
char *prompt;
char *out;
int outlen;
{
    term_getline(prompt, out, outlen);
    data_trim_crlf(out);
}

static int user_find_recno_by_name(name, out)
char *name;
USRDESC *out;
{
    return data_find_user_by_name(name, out) >= 0L;
}

static long user_find_recno(name, out)
char *name;
USRDESC *out;
{
    return data_find_user_by_name(name, out);
}

static int user_write_at(recno, u)
long recno;
USRDESC *u;
{
    user_apply_runtime_sanity(u);
    return data_write_user(recno, u);
}

static int user_can_edit_target(u)
USRDESC *u;
{
    if (!u)
        return 0;

    if (u->name[0] == 0)
        return 0;

    return 1;
}

static void user_show_summary(u)
USRDESC *u;
{
    char d[32], t[32];

    if (!u)
        return;

    data_unpack_date(u->lastdate, d);
    data_unpack_time(u->lasttime, t);

    printf("Name      : %s\n", u->name);
    printf("City      : %s\n", u->city);
    printf("Seclevel  : %u\n", (unsigned)u->seclevel);
    printf("Calls     : %u\n", (unsigned)u->calls);
    printf("Uploads   : %u\n", (unsigned)u->uploads);
    printf("Downloads : %u\n", (unsigned)u->downloads);
    printf("High read : %ld\n", u->highmsgread);
    printf("Last call : %s %s\n", d, t);
    printf("Menu set  : %u\n", (unsigned)u->menu_set);
    printf("Protocol  : %u\n", (unsigned)u->protocol);
    printf("Expert    : %s\n", u->expert ? "Yes" : "No");
    printf("Ratio     : %u\n", (unsigned)u->dnldratio);
    printf("Bull mask : %04X\n", (unsigned)u->bull_mask);
}

static int user_prompt_edit_numeric(prompt, current)
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

static void user_edit_fields(u)
USRDESC *u;
{
    char line[128];

    if (!u)
        return;

    printf("Editing user: %s\n", u->name);

    term_getline("New city (blank = keep): ", line, sizeof(line));
    data_trim_crlf(line);
    if (line[0])
        user_copy_name(u->city, line, CITY_LEN);

    term_getline_hidden("New password (blank = keep): ", line, sizeof(line));
    data_trim_crlf(line);
    if (line[0])
        user_copy_name(u->pwd, line, PWD_LEN);

    u->seclevel =
        (byte)user_prompt_edit_numeric("New seclevel (blank = keep): ",
                                       (int)u->seclevel);

    u->menu_set =
        (byte)user_prompt_edit_numeric("New menu set (blank = keep): ",
                                       (int)u->menu_set);

    u->protocol =
        (byte)user_prompt_edit_numeric("New protocol (blank = keep): ",
                                       (int)u->protocol);

    u->dnldratio =
        (byte)user_prompt_edit_numeric("New ratio (blank = keep): ",
                                       (int)u->dnldratio);

    u->time_limit =
        (ushort)user_prompt_edit_numeric("New time limit (blank = keep): ",
                                         (int)u->time_limit);

    u->expert =
        (byte)user_prompt_edit_numeric("Expert 0/1 (blank = keep): ",
                                       (int)u->expert);

    u->bull_mask =
        (bits)user_prompt_edit_numeric("Bulletin mask (blank = keep): ",
                                       (int)u->bull_mask);

    user_apply_runtime_sanity(u);
}

static void user_make_printable_line(out, u)
char *out;
USRDESC *u;
{
    sprintf(out, "%-35s %-24s Sec:%-3u Calls:%-5u",
            u->name,
            u->city,
            (unsigned)u->seclevel,
            (unsigned)u->calls);
}


static int user_prompt_termno(current)
int current;
{
    char line[32];
    int i, v;

    puts("");
    puts("Terminals:");
    puts("");

    for (i = 0; i < NUM_TERM_TYPES; i++)
    {
        if (g_cfg.term_name[i][0])
            printf("%d: %s\n", i, g_cfg.term_name[i]);
    }

    puts("");
    term_getline("Terminal type: ", line, sizeof(line));
    data_trim_crlf(line);

    if (!line[0])
        return current;

    v = atoi(line);
    if (v < 0 || v >= NUM_TERM_TYPES)
        return current;

    return v;
}

static int user_prompt_protocol(current)
int current;
{
    char line[32];

    puts("");
    puts("Protocols:");
    puts("T: ASCII Text");
    puts("X: XMODEM");
    puts("C: XMODEM-CRC");
    puts("Y: YMODEM");
    puts("B: YMODEM-Batch");
    puts("K: Kermit");
    puts("Z: Zmodem");
    puts("");

    term_getline("U/D protocol: ", line, sizeof(line));
    data_trim_crlf(line);

    if (!line[0])
        return current;

    switch (toupper((unsigned char)line[0]))
    {
        case 'T': return 0;
        case 'X': return 1;
        case 'C': return 2;
        case 'Y': return 3;
        case 'B': return 4;
        case 'K': return 5;
        case 'Z': return 6;
    }

    return current;
}

static void user_prompt_cls_codes(u)
USRDESC *u;
{
    int i;
    char prompt[32];
    char line[32];
    int v;

    for (i = 0; i < 4; i++)
    {
        sprintf(prompt, "CLS code%d: ", i + 1);
        term_getline(prompt, line, sizeof(line));
        data_trim_crlf(line);
        if (!line[0])
            continue;
        v = atoi(line);
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        u->cls[i] = (byte)v;
    }
}

static void user_prompt_bs_codes(u)
USRDESC *u;
{
    int i;
    char prompt[32];
    char line[32];
    int v;

    for (i = 0; i < 3; i++)
    {
        sprintf(prompt, "BS code%d: ", i + 1);
        term_getline(prompt, line, sizeof(line));
        data_trim_crlf(line);
        if (!line[0])
            continue;
        v = atoi(line);
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        u->bs[i] = (byte)v;
    }
}

static void user_prompt_page_size(u)
USRDESC *u;
{
    char line[32];
    int v;

    term_getline("Page width: ", line, sizeof(line));
    data_trim_crlf(line);
    if (line[0])
    {
        v = atoi(line);
        if (v < 32) v = 32;
        if (v > 132) v = 132;
        u->page_width = (ushort)v;
    }

    term_getline("Page len: ", line, sizeof(line));
    data_trim_crlf(line);
    if (line[0])
    {
        v = atoi(line);
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        u->page_len = (ushort)v;
    }
}

static void user_show_pref_editor(u)
USRDESC *u;
{
    char sectbuf[32];
    bits visible;
    int sec;
    int first;

    visible = (u->rd_acc | u->wr_acc | u->up_acc | u->dn_acc | u->sys_acc) & (~u->sect_mask);
    sectbuf[0] = 0;
    first = 1;
    for (sec = 0; sec < NUM_SECT; sec++)
    {
        if (visible & (bits)(1U << sec))
        {
            char tmp[4];
            if (!first)
                strcat(sectbuf, "");
            if (sec < 10)
                sprintf(tmp, "%d", sec);
            else
                sprintf(tmp, "%c", 'A' + (sec - 10));
            strcat(sectbuf, tmp);
            first = 0;
        }
    }

    puts("");
    puts(u->name);
    puts(u->city);
    if (u->phone[0])
        puts(u->phone);

    printf("1: Terminal : %s\n", g_cfg.term_name[u->term][0] ? g_cfg.term_name[u->term] : "Standard");
    printf("2: Sections : %s\n", sectbuf[0] ? sectbuf : "-");
    printf("3: Password : %s\n", u->pwd);
    printf("4: Status   : %s\n", u->expert ? "Expert" : "Novice");
    printf("5: Protocol : %u\n", (unsigned)u->protocol);
    printf("6: CLS codes: %u %u %u %u\n", (unsigned)u->cls[0], (unsigned)u->cls[1], (unsigned)u->cls[2], (unsigned)u->cls[3]);
    printf("7: BS codes : %u %u %u\n", (unsigned)u->bs[0], (unsigned)u->bs[1], (unsigned)u->bs[2]);
    printf("8: Page size: %u x %u\n", (unsigned)u->page_width, (unsigned)u->page_len);
    printf("9: Linefeeds: %s\n", u->linefeeds ? "Yes" : "No");
    printf("0: NULS     : %u\n", (unsigned)u->nuls);
}

/* ------------------------------------------------------------ */
/* public user helpers                                          */
/* ------------------------------------------------------------ */

int user_load_by_name(name, u)
char *name;
USRDESC *u;
{
    if (!name || !u)
        return 0;

    return (data_find_user_by_name(name, u) >= 0L);
}

int user_save_current(void)
{
    long recno;
    USRDESC tmp;

    if (!g_sess.user.name[0])
        return 0;

    recno = user_find_recno(g_sess.user.name, &tmp);
    if (recno < 0L)
        return 0;

    return user_write_at(recno, &g_sess.user);
}

/* ------------------------------------------------------------ */
/* maintenance actions                                          */
/* ------------------------------------------------------------ */

void do_add_user(void)
{
    USRDESC u;
    char name[NAME_LEN + 4];
    char pwd[PWD_LEN + 4];
    long recno;

    if (!sysop_password_prompt())
        return;

    if (!user_prompt_name("Add user name: ", name, sizeof(name)))
        return;

    if (user_find_recno_by_name(name, (USRDESC *)0))
    {
        puts("User already exists");
        return;
    }

    user_apply_newuser_defaults(&u);
    user_copy_name(u.name, name, NAME_LEN);

    if (user_prompt_password("Password: ", pwd, sizeof(pwd)))
        user_copy_name(u.pwd, pwd, PWD_LEN);

    user_prompt_city("City: ", u.city, sizeof(u.city));

    u.lastdate = 0;
    u.lasttime = 0;
    u.calls = 0;
    u.uploads = 0;
    u.downloads = 0;
    u.highmsgread = 0L;
    u.bull_mask = 0;

    recno = data_find_blank_user();
    if (recno < 0L)
        recno = data_user_count();

    if (!user_write_at(recno, &u))
    {
        puts("Unable to add user");
        return;
    }

    puts("User added");
}

void do_delete_user(void)
{
    char name[NAME_LEN + 4];
    USRDESC u;
    long recno;

    if (!sysop_password_prompt())
        return;

    if (!user_prompt_name("Delete user name: ", name, sizeof(name)))
        return;

    recno = user_find_recno(name, &u);
    if (recno < 0L)
    {
        puts("User not found");
        return;
    }

    user_zero(&u);

    if (!user_write_at(recno, &u))
    {
        puts("Delete failed");
        return;
    }

    puts("User deleted");
}

void do_change_user(void)
{
    char name[NAME_LEN + 4];
    USRDESC u;
    long recno;

    if (!sysop_password_prompt())
        return;

    if (!user_prompt_name("Change user name: ", name, sizeof(name)))
        return;

    recno = user_find_recno(name, &u);
    if (recno < 0L)
    {
        puts("User not found");
        return;
    }

    if (!user_can_edit_target(&u))
    {
        puts("User cannot be edited");
        return;
    }

    user_show_summary(&u);
    puts("");

    user_edit_fields(&u);

    if (!user_write_at(recno, &u))
    {
        puts("Update failed");
        return;
    }

    puts("User updated");
}

void do_purge_inactive_users(void)
{
    long i, n;
    USRDESC u;
    int purged;

    if (!sysop_password_prompt())
        return;

    n = data_user_count();
    purged = 0;

    for (i = 0L; i < n; i++)
    {
        if (!data_read_user(i, &u))
            continue;

        if (user_is_blank(&u))
            continue;

        if (u.seclevel == 0)
        {
            user_zero(&u);
            if (data_write_user(i, &u))
                purged++;
        }
    }

    printf("%d locked-out user(s) purged\n", purged);
}

void do_print_user_list(void)
{
    long i, n;
    USRDESC u;
    char line[128];

    n = data_user_count();
    if (n <= 0L)
    {
        puts("No users");
        return;
    }

    for (i = 0L; i < n; i++)
    {
        if (!data_read_user(i, &u))
            continue;
        if (user_is_blank(&u))
            continue;

        user_make_printable_line(line, &u);
        puts(line);
    }
}

void do_update_user_defaults(void)
{
    puts("User defaults editor not yet expanded");
}

void do_user_edit(void)
{
    char line[32];

    if (!g_sess.user.name[0])
    {
        puts("No active user");
        return;
    }

    for (;;)
    {
        user_show_pref_editor(&g_sess.user);
        term_getline("Enter line to change: ", line, sizeof(line));
        data_trim_crlf(line);

        if (!line[0])
            break;

        switch (line[0])
        {
            case '1':
                g_sess.user.term = (byte)user_prompt_termno((int)g_sess.user.term);
                term_apply_user(&g_sess.user);
                (void)user_save_current();
                break;

            case '2':
                do_change_section_mask();
                break;

            case '3':
                term_getline_hidden("New password: ", line, sizeof(line));
                data_trim_crlf(line);
                if (line[0])
                {
                    user_copy_name(g_sess.user.pwd, line, PWD_LEN);
                    (void)user_save_current();
                }
                break;

            case '4':
                g_sess.user.expert = term_yesno("Expert user (Y/N): ", g_sess.user.expert ? 1 : 0) ? 1 : 0;
                g_sess.expert = g_sess.user.expert ? 1 : 0;
                (void)user_save_current();
                break;

            case '5':
                g_sess.user.protocol = (byte)user_prompt_protocol((int)g_sess.user.protocol);
                term_apply_user(&g_sess.user);
                (void)user_save_current();
                break;

            case '6':
                user_prompt_cls_codes(&g_sess.user);
                term_apply_user(&g_sess.user);
                (void)user_save_current();
                break;

            case '7':
                user_prompt_bs_codes(&g_sess.user);
                term_apply_user(&g_sess.user);
                (void)user_save_current();
                break;

            case '8':
                user_prompt_page_size(&g_sess.user);
                term_apply_user(&g_sess.user);
                (void)user_save_current();
                break;

            case '9':
                g_sess.user.linefeeds = term_yesno("Linefeeds (Y/N): ", g_sess.user.linefeeds ? 1 : 0) ? 1 : 0;
                term_apply_user(&g_sess.user);
                (void)user_save_current();
                break;

            case '0':
                term_getline("NULS: ", line, sizeof(line));
                data_trim_crlf(line);
                if (line[0])
                {
                    int n = atoi(line);
                    if (n < 0) n = 0;
                    if (n > 50) n = 50;
                    g_sess.user.nuls = (byte)n;
                    term_apply_user(&g_sess.user);
                    (void)user_save_current();
                }
                break;
        }
    }
}


void do_user_statistics(void)
{
    long n;
    long active;
    long locked;
    long i;
    USRDESC u;

    n = data_user_count();
    active = 0L;
    locked = 0L;

    for (i = 0L; i < n; i++)
    {
        if (!data_read_user(i, &u))
            continue;
        if (user_is_blank(&u))
            continue;

        active++;
        if (u.seclevel == 0)
            locked++;
    }

    printf("Total records : %ld\n", n);
    printf("Active users  : %ld\n", active);
    printf("Locked users  : %ld\n", locked);
}

void do_expert_toggle(void)
{
    g_sess.user.expert = g_sess.user.expert ? 0 : 1;
    g_sess.expert = g_sess.user.expert ? 1 : 0;
    (void)user_save_current();
    printf("Expert mode %s\n", g_sess.user.expert ? "ON" : "OFF");
}

void do_reset_bulletin_flags(void)
{
    long i, n;
    USRDESC u;

    if (!sysop_password_prompt())
        return;

    n = data_user_count();

    for (i = 0L; i < n; i++)
    {
        if (!data_read_user(i, &u))
            continue;

        if (!u.name[0])
            continue;

        u.bull_mask = 0;
        (void)data_write_user(i, &u);
    }

    puts("Bulletin flags reset");
}