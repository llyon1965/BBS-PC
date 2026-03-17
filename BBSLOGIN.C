/* BBSLOGIN.C
 *
 * BBS-PC ! 4.21
 *
 * Reconstructed and modernised source
 * derived from BBS-PC 4.20
 *
 * Login and logout handling.
 *
 * Ownership rules for this module:
 * - owns authentication and account-entry flow
 * - owns caller/user bookkeeping related to login/logout
 * - does not own session orchestration
 *
 * In particular, this module does NOT call:
 * - modem_begin_session()
 * - modem_end_session()
 * - term_start_session()
 * - term_end_session()
 * - node_session_begin()
 * - node_session_end()
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "bbsdata.h"
#include "bbsfunc.h"

#ifndef GUEST_USER_NAME
#define GUEST_USER_NAME "GUEST"
#endif

#ifndef SYSOP_USER_NAME
#define SYSOP_USER_NAME "SYSOP"
#endif

/* ------------------------------------------------------------ */
/* local helpers                                                */
/* ------------------------------------------------------------ */

static void login_clear_session_user(void)
{
    memset(&g_sess.user, 0, sizeof(g_sess.user));
    g_sess.logged_in = 0;
    g_sess.local_login = 0;
}

static int login_name_is_guest(name)
char *name;
{
    return data_user_match(name, GUEST_USER_NAME);
}

static int login_name_is_sysop(name)
char *name;
{
    return data_user_match(name, SYSOP_USER_NAME);
}

static void login_copy_user_into_session(u)
USRDESC *u;
{
    g_sess.user = *u;

    if (g_sess.user.expert)
        g_sess.expert = 1;
    else
        g_sess.expert = 0;
}

static void login_update_session_flags_from_user(void)
{
    g_sess.expert = g_sess.user.expert ? 1 : 0;
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
    /* Legacy-compatible first pass:
     * direct case-insensitive compare against stored password field.
     */
    return data_name_match(u->pwd, input, PWD_LEN);
}

static void login_mark_new_session_defaults(void)
{
    g_sess.menu_set = 0;
    g_sess.logged_in = 0;
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

static void login_bump_user_stats_on_entry(void)
{
    g_sess.user.calls++;
    g_sess.user.lastdate = data_pack_date_now();
    g_sess.user.lasttime = data_pack_time_now();
    user_save_current();
}

static void logout_update_user_stats(void)
{
    /* Conservative first pass:
     * persist the current user record as-is on logout.
     * Time-on-system accumulation can be refined later.
     */
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

static int login_try_existing_user(name)
char *name;
{
    USRDESC u;
    char pwd[PWD_LEN + 4];

    if (!login_load_named_user(name, &u))
        return 0;

    if (!password_prompt(&u))
        return 0;

    login_copy_user_into_session(&u);
    login_set_remote_defaults();
    login_set_user_runtime_defaults();
    login_bump_user_stats_on_entry();
    login_record_call();
    login_update_lastcall(&g_sess.user);
    login_show_success(g_sess.user.name);
    return 1;
}

static int login_try_existing_user_manual(name)
char *name;
{
    USRDESC u;
    char pwd[PWD_LEN + 4];

    if (!login_load_named_user(name, &u))
        return 0;

    if (!login_prompt_password(pwd, sizeof(pwd)))
        return 0;

    if (!login_password_matches(&u, pwd))
        return 0;

    login_copy_user_into_session(&u);
    login_set_remote_defaults();
    login_set_user_runtime_defaults();
    login_bump_user_stats_on_entry();
    login_record_call();
    login_update_lastcall(&g_sess.user);
    login_show_success(g_sess.user.name);
    return 1;
}

static int login_try_guest(void)
{
    USRDESC u;

    if (login_load_named_user(GUEST_USER_NAME, &u))
    {
        login_copy_user_into_session(&u);
    }
    else
    {
        memset(&u, 0, sizeof(u));
        strncpy(u.name, GUEST_USER_NAME, NAME_LEN);
        u.name[NAME_LEN] = 0;
        u.protocol = 0;
        u.expert = 0;
        login_copy_user_into_session(&u);
    }

    login_set_remote_defaults();
    login_set_user_runtime_defaults();
    login_record_call();
    login_show_guest();
    return 1;
}

static int login_try_local_sysop(void)
{
    USRDESC u;

    if (login_load_named_user(SYSOP_USER_NAME, &u))
    {
        login_copy_user_into_session(&u);
    }
    else
    {
        memset(&u, 0, sizeof(u));
        strncpy(u.name, SYSOP_USER_NAME, NAME_LEN);
        u.name[NAME_LEN] = 0;
        u.protocol = 0;
        u.expert = 1;
        login_copy_user_into_session(&u);
    }

    login_set_local_defaults();
    login_set_user_runtime_defaults();
    login_record_call();
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
    /* Conservative first pass:
     * local sysop-style entry path.
     */
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

    memset(&u, 0, sizeof(u));

    term_getline("New user name: ", line, sizeof(line));
    data_trim_crlf(line);
    if (!line[0])
        return 0;

    if (data_find_user_by_name(line, &u) >= 0L)
    {
        puts("User already exists");
        return 0;
    }

    memset(&u, 0, sizeof(u));
    strncpy(u.name, line, NAME_LEN);
    u.name[NAME_LEN] = 0;

    term_getline_hidden("New password: ", line, sizeof(line));
    data_trim_crlf(line);
    strncpy(u.pwd, line, PWD_LEN);
    u.pwd[PWD_LEN] = 0;

    u.lastdate = data_pack_date_now();
    u.lasttime = data_pack_time_now();
    u.protocol = 0;
    u.expert = 0;
    u.calls = 1;

    recno = data_find_blank_user();
    if (recno < 0L)
        recno = data_user_count();

    if (!data_write_user(recno, &u))
        return 0;

    login_copy_user_into_session(&u);
    login_set_remote_defaults();
    login_set_user_runtime_defaults();
    login_record_call();
    login_update_lastcall(&g_sess.user);

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
    USRLOG c;
    long recno;
    char d[32], t[32];

    memset(&c, 0, sizeof(c));

    strncpy(c.name, g_sess.user.name, NAME_LEN);
    c.name[NAME_LEN] = 0;

    data_now_strings(d, t);
    strncpy(c.date, d, sizeof(c.date) - 1);
    c.date[sizeof(c.date) - 1] = 0;
    strncpy(c.time, t, sizeof(c.time) - 1);
    c.time[sizeof(c.time) - 1] = 0;

    c.when = (ulong)time((time_t *)0);

    recno = data_find_blank_caller();
    if (recno < 0L)
        recno = data_caller_count();

    (void)data_write_caller(recno, &c);
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
}

void do_register_user(void)
{
    (void)user_register();
}

void do_callback_user(void)
{
    puts("Callback not yet implemented");
}

void do_lockout_user(void)
{
    puts("Lockout not yet implemented");
}