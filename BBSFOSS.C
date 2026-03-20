/* BBSFOSSIL.C
 *
 * BBS-PC ! 4.21
 *
 * Reconstructed and modernised source
 * derived from BBS-PC 4.20
 *
 * FOSSIL support
 *
 * This pass makes the active FOSSIL port derive from the selected
 * modem/node state:
 *
 *   node 1 -> COM1 -> FOSSIL port 0
 *   node 2 -> COM2 -> FOSSIL port 1
 *
 * The port is taken from g_modem.port when available, falling back
 * to g_sess.node if needed.
 */

#include <stdio.h>
#include <string.h>
#include <dos.h>

#include "bbsdata.h"
#include "bbsfunc.h"

/* ------------------------------------------------------------ */
/* FOSSIL constants                                             */
/* ------------------------------------------------------------ */

#ifndef FOSSIL_INT
#define FOSSIL_INT 0x14
#endif

#ifndef FOSSIL_INIT
#define FOSSIL_INIT 0x04
#endif

#ifndef FOSSIL_DEINIT
#define FOSSIL_DEINIT 0x05
#endif

#ifndef FOSSIL_STAT
#define FOSSIL_STAT 0x03
#endif

#ifndef FOSSIL_PUTC
#define FOSSIL_PUTC 0x01
#endif

#ifndef FOSSIL_GETC
#define FOSSIL_GETC 0x02
#endif

#ifndef FOSSIL_FLUSH_OUT
#define FOSSIL_FLUSH_OUT 0x08
#endif

#ifndef FOSSIL_PURGE_IN
#define FOSSIL_PURGE_IN 0x09
#endif

#ifndef FOSSIL_PURGE_OUT
#define FOSSIL_PURGE_OUT 0x0A
#endif

#ifndef FOSSIL_SET_BAUD
#define FOSSIL_SET_BAUD 0x00
#endif

#ifndef FOSSIL_CURSOR_CTRL
#define FOSSIL_CURSOR_CTRL 0x0F
#endif

#ifndef FOSSIL_WATCHDOG_SLICE_MS
#define FOSSIL_WATCHDOG_SLICE_MS 10
#endif

/* status bits commonly used by FOSSILs */
#ifndef FOSSIL_STAT_RX_READY
#define FOSSIL_STAT_RX_READY   0x0100
#endif

#ifndef FOSSIL_STAT_TX_READY
#define FOSSIL_STAT_TX_READY   0x2000
#endif

#ifndef FOSSIL_STAT_DCD
#define FOSSIL_STAT_DCD        0x0080
#endif

static int g_fossil_available = -1;

/* ------------------------------------------------------------ */
/* local helpers                                                */
/* ------------------------------------------------------------ */

static int fossil_port_number(void)
{
    int port;

    /* Preferred source: configured modem port (1-based COM number) */
    port = g_modem.port;
    if (port >= 1)
        return port - 1;    /* FOSSIL uses 0-based port numbers */

    /* Fallback: selected node (internal node is 0-based) */
    port = g_sess.node;
    if (port < 0)
        port = 0;
    if (port > 1)
        port = 1;

    return port;
}

static int fossil_baud_code(baud)
int baud;
{
    switch (baud)
    {
        case 300:   return 0x40;
        case 600:   return 0x60;
        case 1200:  return 0x80;
        case 2400:  return 0xA0;
        case 4800:  return 0xC0;
        case 9600:  return 0xE0;
#ifdef __TURBOC__
        case 19200: return 0x00;
        case 38400: return 0x20;
#else
        case 19200: return 0x00;
        case 38400: return 0x20;
#endif
    }

    return 0xE0;   /* default 9600 */
}

static unsigned fossil_status_word(void)
{
    union REGS in, out;

    in.h.ah = FOSSIL_STAT;
    in.x.dx = fossil_port_number();
    int86(FOSSIL_INT, &in, &out);

    return (unsigned)out.x.ax;
}

static int fossil_receive_ready(void)
{
    return (fossil_status_word() & FOSSIL_STAT_RX_READY) != 0;
}

static int fossil_transmit_ready(void)
{
    return (fossil_status_word() & FOSSIL_STAT_TX_READY) != 0;
}

static void fossil_purge_input(void)
{
    union REGS in, out;

    in.h.ah = FOSSIL_PURGE_IN;
    in.x.dx = fossil_port_number();
    int86(FOSSIL_INT, &in, &out);
}

static void fossil_purge_output(void)
{
    union REGS in, out;

    in.h.ah = FOSSIL_PURGE_OUT;
    in.x.dx = fossil_port_number();
    int86(FOSSIL_INT, &in, &out);
}

static void fossil_flush_output(void)
{
    union REGS in, out;

    in.h.ah = FOSSIL_FLUSH_OUT;
    in.x.dx = fossil_port_number();
    int86(FOSSIL_INT, &in, &out);
}

static void fossil_set_line_speed(baud)
int baud;
{
    union REGS in, out;

    in.h.ah = FOSSIL_SET_BAUD;
    in.h.al = (unsigned char)fossil_baud_code(baud);
    in.x.dx = fossil_port_number();
    int86(FOSSIL_INT, &in, &out);
}

static void fossil_set_cursor(enabled)
int enabled;
{
    union REGS in, out;

    in.h.ah = FOSSIL_CURSOR_CTRL;
    in.h.al = (unsigned char)(enabled ? 1 : 0);
    int86(FOSSIL_INT, &in, &out);
}

/* ------------------------------------------------------------ */
/* public FOSSIL helpers                                        */
/* ------------------------------------------------------------ */

int fossil_driver_installed(void)
{
    union REGS in, out;

    if (g_fossil_available >= 0)
        return g_fossil_available;

    memset(&in, 0, sizeof(in));
    in.h.ah = FOSSIL_INIT;
    in.x.dx = fossil_port_number();
    int86(FOSSIL_INT, &in, &out);

    /* Most FOSSILs return AX = 0x1954 when present after init,
       but implementations vary. Treat nonzero response as usable,
       then deinit cleanly. */
    g_fossil_available = (out.x.ax != 0 || out.x.bx != 0) ? 1 : 0;

    if (g_fossil_available)
    {
        in.h.ah = FOSSIL_DEINIT;
        in.x.dx = fossil_port_number();
        int86(FOSSIL_INT, &in, &out);
    }

    return g_fossil_available;
}

int fossil_carrier(void)
{
    if (!fossil_driver_installed())
        return 0;

    return (fossil_status_word() & FOSSIL_STAT_DCD) != 0;
}

void modem_init(void)
{
    union REGS in, out;

    if (g_modem.port < 1)
        g_modem.port = (g_sess.node >= 0 ? g_sess.node + 1 : 1);

    if (g_modem.baud <= 0)
        g_modem.baud = 2400;

    memset(&in, 0, sizeof(in));
    in.h.ah = FOSSIL_INIT;
    in.x.dx = fossil_port_number();
    int86(FOSSIL_INT, &in, &out);

    fossil_set_line_speed(g_modem.baud);
    fossil_purge_input();
    fossil_purge_output();
}

void modem_begin_session(void)
{
    fossil_purge_input();
    fossil_purge_output();
    fossil_set_cursor(0);
}

void modem_end_session(void)
{
    union REGS in, out;

    fossil_flush_output();
    fossil_purge_input();
    fossil_set_cursor(1);

    in.h.ah = FOSSIL_DEINIT;
    in.x.dx = fossil_port_number();
    int86(FOSSIL_INT, &in, &out);
}

int modem_send_byte(ch)
int ch;
{
    union REGS in, out;
    long guard;

    if (!fossil_driver_installed())
        return -1;

    guard = 0L;
    while (!fossil_transmit_ready())
    {
        delay(FOSSIL_WATCHDOG_SLICE_MS);
        guard += FOSSIL_WATCHDOG_SLICE_MS;
        if (guard >= 5000L)
            return -1;
    }

    in.h.ah = FOSSIL_PUTC;
    in.h.al = (unsigned char)(ch & 0xFF);
    in.x.dx = fossil_port_number();
    int86(FOSSIL_INT, &in, &out);

    return ch & 0xFF;
}

int modem_recv_byte(timeout)
int timeout;
{
    union REGS in, out;
    long waited;

    if (!fossil_driver_installed())
        return -1;

    waited = 0L;

    for (;;)
    {
        if (fossil_receive_ready())
        {
            in.h.ah = FOSSIL_GETC;
            in.x.dx = fossil_port_number();
            int86(FOSSIL_INT, &in, &out);
            return out.h.al;
        }

        if (timeout == 0)
            return -1;

        delay(FOSSIL_WATCHDOG_SLICE_MS);
        waited += FOSSIL_WATCHDOG_SLICE_MS;

        if (timeout > 0 && waited >= (long)timeout)
            return -1;
    }
}

void modem_direct_upload(void)
{
    puts("Direct upload mode not yet expanded");
}