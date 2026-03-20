/* BBSLOGIN.C
 *
 * BBS-PC! 4.21
 *
 * Login and logout handling.
 *
 * Updated:
 * - caller log records now carry node / comm params / in/out times
 * - disconnect reason is written at logout
 * - per-call uploads/downloads/messages are written for BBSINFO-style use
 * - sanity fixes applied for caller-log wrappers and modem field defaults
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include "bbsdata.h"
#include "bbsfunc.h"

#ifndef GUEST_USER_NAME
#define GUEST_USER_NAME "GUEST"
#endif

#ifndef SYSOP_USER_NAME
#define SYSOP_USER_NAME "SYSOP"
#endif

#ifndef DEFAULT_GUEST_MINUTES
#define DEFAULT_GUEST_MINUTES 30
#endif

#ifndef DEFAULT_USER_MINUTES
#define DEFAULT_USER_MINUTES 60
#endif

#ifndef DEFAULT_SYSOP_MINUTES
#define DEFAULT_SYSOP_MINUTES 0
#endif

#ifndef SYSOP_SECLEVEL
#define SYSOP_SECLEVEL 90
#endif

/* ------------------------------------------------------------ */
/* local helpers                                                */
/* ------------------------------------------------------------ */

static long login_caller_count(void)
{
    return data_caller_count();
}

static long login_find_blank_caller(void)
{
    return data_find_blank_caller();
}

static void login_clear_session_user(void)
{
    memset(&g_sess.user, 0, sizeof(g_sess.user));
    g_sess.logged_in = 0;
    g_sess.local_login = 0;
    g_sess.logon_unix = 0L;
    g_sess.last_activity_unix = 0L;
    g_sess.prev_lastdate = 0;
    g_sess.prev_lasttime = 0;
    g_sess.base_minutes_allowed = 0;
    g_sess.bonus_minutes = 0;
    g_sess.ratio_off = 0;
    g_sess.last_time_warning = -1;

    g_sess.caller_log_recno = -1L;
    g_sess.caller_no = 0L;
    g_sess.start_uploads = 0;
    g_sess.start_downloads = 0;
    g_sess.msgs_left = 0;
    g_sess.uploads = 0;
    g_sess.disconnect_reason[0] = 0;
}

static int login_name_is_sysop(name)
char *name;
{
    return data_user_match(name, SYSOP_USER_NAME);
}

static int login_user_locked_out(u)
USRDESC *u;
{
    if (!u)
        return 1;

    return u->seclevel == 0;
}

static void login_show_locked_out(void)
{
    puts("Access denied");
}

static void login_copy_user_into_session(u)
USRDESC *u;
{
    g_sess.user = *u;
    g_sess.expert = g_sess.user.expert ? 1 : 0;
    g_sess.menu_set = g_sess.user.menu_set;
}

static void login_update_session_flags_from_user(void)
{
    g_sess.expert = g_sess.user.expert ? 1 : 0;
    g_sess.menu_set = g_sess.user.menu_set;
}

static void login_prompt_name(out, outlen)
char *out;
int outlen;
{
    term_getline("Name: ", out, outlen);
    data_trim_crlf(out);
}

static int login_prompt_password(out, outlen)
char *out;
int outlen;
{
    term_getline_hidden("Password: ", out, outlen);
    data_trim_crlf(out);
    return out[0] ? 1 : 0;
}

static int login_load_named_user(name, u)
char *name;
USRDESC *u;
{
    return user_load_by_name(name, u);
}

static int login_password_matches(u, input)
USRDESC *u;
char *input;
{
    return data_name_match(u->pwd, input, PWD_LEN);
}

static void login_mark_new_session_defaults(void)
{
    g_sess.menu_set = 0;
    g_sess.logged_in = 0;
    g_sess.page_sysop = 0;
    g_sess.msgs_left = 0;
    g_sess.uploads = 0;
    g_sess.logon_unix = 0L;
    g_sess.last_activity_unix = 0L;
    g_sess.prev_lastdate = 0;
    g_sess.prev_lasttime = 0;
    g_sess.bonus_minutes = 0;
    g_sess.ratio_off = 0;
    g_sess.last_time_warning = -1;
    g_sess.disconnect_reason[0] = 0;
}

static void login_set_local_defaults(void)
{
    g_sess.local_login = 1;
}

static void login_set_remote_defaults(void)
{
    g_sess.local_login = 0;
}

static void login_set_user_runtime_defaults(void)
{
    if (g_sess.user.protocol > 6)
        g_sess.user.protocol = 0;

    login_update_session_flags_from_user();
}

static int login_cfg_default_minutes(is_guest)
int is_guest;
{
    if (is_guest)
    {
        if (g_cfg.limit[0] > 0)
            return (int)g_cfg.limit[0];
        return DEFAULT_GUEST_MINUTES;
    }

    if (g_cfg.limit[1] > 0)
        return (int)g_cfg.limit[1];

    return DEFAULT_USER_MINUTES;
}

static int login_compute_time_limit(is_guest, is_local_sysop)
int is_guest;
int is_local_sysop;
{
    int mins;

    if (is_local_sysop)
        return DEFAULT_SYSOP_MINUTES;

    if (g_sess.user.seclevel >= SYSOP_SECLEVEL)
        return 0;

    if (g_sess.user.time_limit > 0)
        return g_sess.user.time_limit;

    mins = login_cfg_default_minutes(is_guest);
    if (mins < 0)
        mins = 0;

    return mins;
}

static void login_apply_session_policy(is_guest, is_local_sysop)
int is_guest;
int is_local_sysop;
{
    g_sess.base_minutes_allowed =
        login_compute_time_limit(is_guest, is_local_sysop);

    if (g_sess.base_minutes_allowed < 0)
        g_sess.base_minutes_allowed = 0;
}

static void login_preserve_previous_lastcall(void)
{
    g_sess.prev_lastdate = g_sess.user.lastdate;
    g_sess.prev_lasttime = g_sess.user.lasttime;
}

static void login_bump_user_stats_on_entry(void)
{
    g_sess.user.calls++;
    g_sess.user.lastdate = data_pack_date_now();
    g_sess.user.lasttime = data_pack_time_now();
    user_save_current();
}

static void login_show_failure(void)
{
    puts("Login failed");
}

static void login_show_success(name)
char *name;
{
    printf("Welcome, %s\n", name);
}

static void login_show_guest(void)
{
    puts("Guest access granted");
}

static void login_show_local(void)
{
    puts("Local login granted");
}

static void login_seed_session_counters(void)
{
    g_sess.start_uploads = g_sess.user.uploads;
    g_sess.start_downloads = g_sess.user.downloads;
    g_sess.msgs_left = 0;
    g_sess.uploads = 0;
    g_sess.disconnect_reason[0] = 0;
}

static void login_fill_call_record_base(c)
USRLOG *c;
{
    memset(c, 0, sizeof(*c));

    c->caller_no = (ulong)(login_caller_count() + 1L);
    c->node_no = (byte)(g_sess.node + 1);
    c->baud = (ushort)((g_modem.baud > 0) ? g_modem.baud : 0);
    c->parity = (char)(g_modem.parity ? g_modem.parity : 'N');
    c->data_bits = (byte)((g_modem.data_bits > 0) ? g_modem.data_bits : 8);
    c->stop_bits = (byte)((g_modem.stop_bits > 0) ? g_modem.stop_bits : 1);

    strncpy(c->name, g_sess.user.name, DISK_NAME_LEN);
    c->name[DISK_NAME_LEN] = 0;

    strncpy(c->city, g_sess.user.city, DISK_CITY_LEN);
    c->city[DISK_CITY_LEN] = 0;

    c->in_date = data_pack_date_now();
    c->in_time = data_pack_time_now();
}

static void login_begin_caller_log(void)
{
    USRLOG c;
    long recno;

    login_fill_call_record_base(&c);

    recno = login_find_blank_caller();
    if (recno < 0L)
        recno = login_caller_count();

    if (!data_write_caller(recno, &c))
    {
        g_sess.caller_log_recno = -1L;
        g_sess.caller_no = 0L;
        return;
    }

    g_sess.caller_log_recno = recno;
    g_sess.caller_no = c.caller_no;
}

static ushort login_session_downloads_this_call(void)
{
    if (g_sess.user.downloads >= g_sess.start_downloads)
        return (ushort)(g_sess.user.downloads - g_sess.start_downloads);

    return 0;
}

static ushort login_session_uploads_this_call(void)
{
    if (g_sess.user.uploads >= g_sess.start_uploads)
        return (ushort)(g_sess.user.uploads - g_sess.start_uploads);

    return 0;
}

static void logout_finish_caller_log(void)
{
    USRLOG c;

    if (g_sess.caller_log_recno < 0L)
        return;

    if (!data_read_caller(g_sess.caller_log_recno, &c))
        return;

    c.out_date = data_pack_date_now();
    c.out_time = data_pack_time_now();

    if (g_sess.disconnect_reason[0])
    {
        strncpy(c.disc_reason, g_sess.disconnect_reason, DISC_REASON_LEN);
        c.disc_reason[DISC_REASON_LEN] = 0;
    }

    c.msgs_left = (ushort)g_sess.msgs_left;
    c.uploads = login_session_uploads_this_call();
    c.downloads = login_session_downloads_this_call();

    (void)data_write_caller(g_sess.caller_log_recno, &c);
}

static void logout_update_user_stats(void)
{
    g_sess.user.lastdate = data_pack_date_now();
    g_sess.user.lasttime = data_pack_time_now();
    user_save_current();
}

static int login_try_existing_user(name)
char *name;
{
    USRDESC u;

    if (!login_load_named_user(name, &u))
        return 0;

    if (login_user_locked_out(&u))
    {
        login_show_locked_out();
        return 0;
    }

    if (!password_prompt(&u))
        return 0;

    login_copy_user_into_session(&u);
    login_preserve_previous_lastcall();
    login_set_remote_defaults();
    login_set_user_runtime_defaults();
    login_apply_session_policy(0, 0);
    login_bump_user_stats_on_entry();
    login_seed_session_counters();
    login_begin_caller_log();
    login_show_success(g_sess.user.name);
    return 1;
}

static int login_try_guest(void)
{
    USRDESC u;
    int has_guest_record;

    has_guest_record = login_load_named_user(GUEST_USER_NAME, &u);

    if (has_guest_record)
    {
        if (login_user_locked_out(&u))
        {
            login_show_locked_out();
            return 0;
        }

        login_copy_user_into_session(&u);
        login_preserve_previous_lastcall();
    }
    else
    {
        memset(&u, 0, sizeof(u));
        strncpy(u.name, GUEST_USER_NAME, NAME_LEN);
        u.name[NAME_LEN] = 0;
        u.protocol = 0;
        u.expert = 0;
        u.seclevel = (g_cfg.priv[0] > 0) ? g_cfg.priv[0] : 1;
        u.menu_set = 0;
        login_copy_user_into_session(&u);
        g_sess.prev_lastdate = 0;
        g_sess.prev_lasttime = 0;
    }

    login_set_remote_defaults();
    login_set_user_runtime_defaults();
    login_apply_session_policy(1, 0);

    if (has_guest_record)
        login_bump_user_stats_on_entry();

    login_seed_session_counters();
    login_begin_caller_log();
    login_show_guest();
    return 1;
}

static int login_try_local_sysop(void)
{
    USRDESC u;
    int has_sysop_record;

    has_sysop_record = login_load_named_user(SYSOP_USER_NAME, &u);

    if (has_sysop_record)
    {
        if (login_user_locked_out(&u))
        {
            login_show_locked_out();
            return 0;
        }

        login_copy_user_into_session(&u);
        login_preserve_previous_lastcall();
    }
    else
    {
        memset(&u, 0, sizeof(u));
        strncpy(u.name, SYSOP_USER_NAME, NAME_LEN);
        u.name[NAME_LEN] = 0;
        u.protocol = 0;
        u.expert = 1;
        u.seclevel = SYSOP_SECLEVEL;
        u.menu_set = 0;
        login_copy_user_into_session(&u);
        g_sess.prev_lastdate = 0;
        g_sess.prev_lasttime = 0;
    }

    login_set_local_defaults();
    login_set_user_runtime_defaults();
    login_apply_session_policy(0, 1);

    if (has_sysop_record)
        login_bump_user_stats_on_entry();

    login_seed_session_counters();
    login_begin_caller_log();
    login_show_local();
    return 1;
}

static int login_handle_special_commands(name)
char *name;
{
    if (!stricmp(name, "GUEST"))
        return login_guest();

    if (!stricmp(name, "NEW"))
        return user_register();

    if (!stricmp(name, "LOCAL"))
        return login_local();

    return -1;
}

/* ------------------------------------------------------------ */
/* public login entry points                                    */
/* ------------------------------------------------------------ */

int login_user(void)
{
    char name[NAME_LEN + 4];
    int rc;

    login_clear_session_user();
    login_mark_new_session_defaults();

    login_prompt_name(name, sizeof(name));
    if (!name[0])
        return 0;

    rc = login_handle_special_commands(name);
    if (rc >= 0)
        return rc;

    if (login_try_existing_user(name))
        return 1;

    login_show_failure();
    return 0;
}

int login_wizard(void)
{
    return login_local();
}

int login_guest(void)
{
    login_clear_session_user();
    login_mark_new_session_defaults();
    return login_try_guest();
}

int login_local(void)
{
    login_clear_session_user();
    login_mark_new_session_defaults();
    return login_try_local_sysop();
}

int user_register(void)
{
    char line[NAME_LEN + 4];
    USRDESC u;
    long recno;

    user_zero(&u);

    term_getline("New user name: ", line, sizeof(line));
    data_trim_crlf(line);
    if (!line[0])
        return 0;

    if (data_find_user_by_name(line, &u) >= 0L)
    {
        puts("User already exists");
        return 0;
    }

    user_apply_newuser_defaults(&u);
    strncpy(u.name, line, NAME_LEN);
    u.name[NAME_LEN] = 0;

    term_getline_hidden("New password: ", line, sizeof(line));
    data_trim_crlf(line);
    strncpy(u.pwd, line, PWD_LEN);
    u.pwd[PWD_LEN] = 0;

    term_getline("City: ", line, sizeof(line));
    data_trim_crlf(line);
    strncpy(u.city, line, CITY_LEN);
    u.city[CITY_LEN] = 0;

    u.calls = 0;
    u.lastdate = 0;
    u.lasttime = 0;

    recno = data_find_blank_user();
    if (recno < 0L)
        recno = data_user_count();

    if (!data_write_user(recno, &u))
        return 0;

    login_copy_user_into_session(&u);
    g_sess.prev_lastdate = 0;
    g_sess.prev_lasttime = 0;
    login_set_remote_defaults();
    login_set_user_runtime_defaults();
    login_apply_session_policy(0, 0);
    login_bump_user_stats_on_entry();
    login_seed_session_counters();
    login_begin_caller_log();

    puts("Registration complete");
    return 1;
}

int password_prompt(u)
USRDESC *u;
{
    char pwd[PWD_LEN + 4];

    if (!u)
        return 0;

    if (!login_prompt_password(pwd, sizeof(pwd)))
        return 0;

    return login_password_matches(u, pwd);
}

void login_record_call(void)
{
    if (g_sess.caller_log_recno < 0L)
        login_begin_caller_log();
}

void login_update_lastcall(u)
USRDESC *u;
{
    if (!u)
        return;

    u->lastdate = data_pack_date_now();
    u->lasttime = data_pack_time_now();
    user_save_current();
}

/* ------------------------------------------------------------ */
/* logout / maintenance-related entry points                    */
/* ------------------------------------------------------------ */

void logout_user(void)
{
    if (!g_sess.user.name[0])
        return;

    logout_update_user_stats();
    logout_finish_caller_log();

    g_sess.caller_log_recno = -1L;
    g_sess.caller_no = 0L;
    g_sess.disconnect_reason[0] = 0;
}

void do_register_user(void)
{
    (void)user_register();
}

void do_callback_user(void)
{
    puts("Callback verification is not available on this system");
}

void do_lockout_user(void)
{
    char name[NAME_LEN + 4];
    USRDESC u;
    long recno;

    if (!sysop_password_prompt())
        return;

    term_getline("Lock out user: ", name, sizeof(name));
    data_trim_crlf(name);
    if (!name[0])
        return;

    recno = data_find_user_by_name(name, &u);
    if (recno < 0L)
    {
        puts("User not found");
        return;
    }

    if (login_name_is_sysop(u.name))
    {
        puts("Cannot lock out SYSOP");
        return;
    }

    u.seclevel = 0;

    if (!data_write_user(recno, &u))
    {
        puts("Lockout failed");
        return;
    }

    puts("User locked out");
}