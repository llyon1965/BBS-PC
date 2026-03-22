/* BBSXTRN.C
 *
 * BBS-PC! 4.21
 *
 * External execution runtime layer.
 *
 * Owns:
 * - safe suspend/restore of the active BBS session
 * - DOS drop
 * - external program execution
 *
 * Future:
 * - DOOR.SYS / DORINFOx.DEF generation
 * - batch/door execution helpers
 */

#include <stdio.h>
#include <stdlib.h>

#include "bbsdata.h"
#include "bbsfunc.h"

static void xtrn_suspend_runtime(void)
{
    term_end_session();

    if (!g_sess.local_login)
        modem_end_session();
}

static void xtrn_restore_runtime(void)
{
    if (!g_sess.local_login)
    {
        g_modem.port = (g_sess.node == 2) ? 2 : 1;
        g_modem.baud = g_cfg.modem_baud;
        g_modem.parity = g_cfg.modem_parity;
        g_modem.data_bits = g_cfg.modem_data_bits;
        g_modem.stop_bits = g_cfg.modem_stop_bits;

        modem_init();
        modem_begin_session();
    }

    term_start_session();
    term_apply_user(&g_sess.user);
}

int xtrn_run_command(cmd)
char *cmd;
{
    if (!cmd || !cmd[0])
        return 0;

    xtrn_suspend_runtime();
    system(cmd);
    xtrn_restore_runtime();

    return 1;
}

int xtrn_drop_to_dos(void)
{
    xtrn_suspend_runtime();
    system("");
    xtrn_restore_runtime();

    return 1;
}