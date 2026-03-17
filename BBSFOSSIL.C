/* BBSFOSSIL.C
 *
 * BBS-PC 4.20 FOSSIL interface
 *
 * Purpose:
 * - provide a real serial backend for modem_send_byte()/modem_recv_byte()
 * - expose enough FOSSIL services for current transfer protocols
 * - keep the rest of the reconstructed codebase insulated from INT 14h
 *
 * Supported:
 * - initialize / deinitialize driver
 * - set baud/parity/word/stop parameters
 * - transmit character
 * - receive character with timeout polling
 * - carrier detect
 * - DTR drop / hangup
 * - buffer flush
 * - simple status query
 *
 * Notes:
 * - This is written for 16-bit DOS C compilers using int86/int86x.
 * - Function usage is aimed at common FOSSIL implementations
 *   (X00 / BNU / OpusComm style).
 * - If your target compiler uses a different interrupt API, only the
 *   fossil_int() wrapper should need adjustment.
 */

#include <stdio.h>
#include <string.h>
#include <dos.h>

#include "bbsdata.h"
#include "bbsfunc.h"

#define FOSSIL_INTNO        0x14

#define FOSSIL_SET_BAUD     0x00
#define FOSSIL_TX_CHAR      0x01
#define FOSSIL_RX_CHAR      0x02
#define FOSSIL_STATUS       0x03
#define FOSSIL_INIT         0x04
#define FOSSIL_DEINIT       0x05
#define FOSSIL_DTR          0x06
#define FOSSIL_FLUSH_OUT    0x08
#define FOSSIL_PURGE_OUT    0x09
#define FOSSIL_PURGE_IN     0x0A
#define FOSSIL_PEEK         0x0C
#define FOSSIL_DRIVER_INFO  0x1B

#define FOSSIL_STAT_RXRDY   0x0100
#define FOSSIL_STAT_TXRDY   0x2000
#define FOSSIL_STAT_CD      0x0080

#define FOSSIL_DTR_ON       1
#define FOSSIL_DTR_OFF      0

typedef struct {
    int installed;
    int initialized;
    int port;
    int baud;
    int word;
    int parity;
    int stops;
} FOSSILSTATE;

static FOSSILSTATE g_fossil;

/* ------------------------------------------------------------ */

static int fossil_port_number(void)
{
    /*
     * First-pass mapping:
     * node/CFG can later choose exact COM port.
     * Default COM1 => port 0 for FOSSIL API.
     */
    return 0;
}

static void fossil_zero_state(void)
{
    memset(&g_fossil, 0, sizeof(g_fossil));
    g_fossil.port = fossil_port_number();
    g_fossil.baud = 2400;
    g_fossil.word = 8;
    g_fossil.parity = 0;
    g_fossil.stops = 1;
}

static void fossil_int(in, out)
union REGS *in;
union REGS *out;
{
    int86(FOSSIL_INTNO, in, out);
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
#else
        case 19200: return 0x00;
#endif
    }

    return 0xA0; /* 2400 default */
}

static int fossil_word_code(word)
int word;
{
    switch (word)
    {
        case 5: return 0x00;
        case 6: return 0x01;
        case 7: return 0x02;
        case 8: return 0x03;
    }

    return 0x03;
}

static int fossil_stop_code(stops)
int stops;
{
    if (stops == 2)
        return 0x04;
    return 0x00;
}

static int fossil_parity_code(parity)
int parity;
{
    switch (parity)
    {
        case 0: return 0x00; /* none */
        case 1: return 0x08; /* odd */
        case 2: return 0x18; /* even */
        case 3: return 0x28; /* mark */
        case 4: return 0x38; /* space */
    }

    return 0x00;
}

static int fossil_line_word(baud, parity, word, stops)
int baud, parity, word, stops;
{
    return fossil_baud_code(baud) |
           fossil_parity_code(parity) |
           fossil_stop_code(stops) |
           fossil_word_code(word);
}

static long fossil_ticks_now(void)
{
    union REGS in, out;

    in.h.ah = 0x00;
    int86(0x1A, &in, &out);

    return ((long)out.x.cx << 16) | (unsigned)out.x.dx;
}

static long fossil_timeout_ticks(timeout_ticks)
int timeout_ticks;
{
    long now = fossil_ticks_now();
    return now + (long)timeout_ticks;
}

/* ------------------------------------------------------------ */
/* public driver detection/init                                 */
/* ------------------------------------------------------------ */

int fossil_driver_installed(void)
{
    union REGS in, out;

    fossil_zero_state();

    in.h.ah = FOSSIL_DRIVER_INFO;
    in.x.dx = g_fossil.port;
    fossil_int(&in, &out);

    /*
     * Many FOSSILs return AX=1954h or nonzero success-like values here.
     * Use a permissive first-pass test.
     */
    if (out.x.ax == 0)
        return 0;

    g_fossil.installed = 1;
    return 1;
}

int fossil_init(void)
{
    union REGS in, out;

    if (!g_fossil.installed && !fossil_driver_installed())
        return 0;

    in.h.ah = FOSSIL_INIT;
    in.x.dx = g_fossil.port;
    fossil_int(&in, &out);

    g_fossil.initialized = 1;
    return 1;
}

void fossil_deinit(void)
{
    union REGS in, out;

    if (!g_fossil.initialized)
        return;

    in.h.ah = FOSSIL_DEINIT;
    in.x.dx = g_fossil.port;
    fossil_int(&in, &out);

    g_fossil.initialized = 0;
}

int fossil_configure(baud, parity, word, stops)
int baud, parity, word, stops;
{
    union REGS in, out;
    int line;

    if (!g_fossil.initialized && !fossil_init())
        return 0;

    line = fossil_line_word(baud, parity, word, stops);

    in.h.ah = FOSSIL_SET_BAUD;
    in.h.al = line & 0xFF;
    in.x.dx = g_fossil.port;
    fossil_int(&in, &out);

    g_fossil.baud = baud;
    g_fossil.parity = parity;
    g_fossil.word = word;
    g_fossil.stops = stops;

    return 1;
}

/* ------------------------------------------------------------ */
/* status / control                                             */
/* ------------------------------------------------------------ */

unsigned fossil_status_word(void)
{
    union REGS in, out;

    if (!g_fossil.initialized)
        return 0;

    in.h.ah = FOSSIL_STATUS;
    in.x.dx = g_fossil.port;
    fossil_int(&in, &out);

    return (unsigned)out.x.ax;
}

int fossil_carrier(void)
{
    return (fossil_status_word() & FOSSIL_STAT_CD) ? 1 : 0;
}

int fossil_rx_ready(void)
{
    return (fossil_status_word() & FOSSIL_STAT_RXRDY) ? 1 : 0;
}

int fossil_tx_ready(void)
{
    return (fossil_status_word() & FOSSIL_STAT_TXRDY) ? 1 : 0;
}

void fossil_set_dtr(on)
int on;
{
    union REGS in, out;

    if (!g_fossil.initialized)
        return;

    in.h.ah = FOSSIL_DTR;
    in.h.al = on ? FOSSIL_DTR_ON : FOSSIL_DTR_OFF;
    in.x.dx = g_fossil.port;
    fossil_int(&in, &out);
}

void fossil_drop_dtr(void)
{
    fossil_set_dtr(0);
}

void fossil_raise_dtr(void)
{
    fossil_set_dtr(1);
}

void fossil_purge_input(void)
{
    union REGS in, out;

    if (!g_fossil.initialized)
        return;

    in.h.ah = FOSSIL_PURGE_IN;
    in.x.dx = g_fossil.port;
    fossil_int(&in, &out);
}

void fossil_purge_output(void)
{
    union REGS in, out;

    if (!g_fossil.initialized)
        return;

    in.h.ah = FOSSIL_PURGE_OUT;
    in.x.dx = g_fossil.port;
    fossil_int(&in, &out);
}

void fossil_flush_output(void)
{
    union REGS in, out;

    if (!g_fossil.initialized)
        return;

    in.h.ah = FOSSIL_FLUSH_OUT;
    in.x.dx = g_fossil.port;
    fossil_int(&in, &out);
}

/* ------------------------------------------------------------ */
/* byte I/O                                                     */
/* ------------------------------------------------------------ */

int fossil_send_char(ch)
int ch;
{
    union REGS in, out;

    if (!g_fossil.initialized && !fossil_init())
        return -1;

    in.h.ah = FOSSIL_TX_CHAR;
    in.h.al = ch & 0xFF;
    in.x.dx = g_fossil.port;
    fossil_int(&in, &out);

    return 0;
}

int fossil_recv_char(timeout_ticks)
int timeout_ticks;
{
    union REGS in, out;
    long deadline;

    if (!g_fossil.initialized && !fossil_init())
        return -1;

    deadline = fossil_timeout_ticks(timeout_ticks);

    while (fossil_ticks_now() <= deadline)
    {
        if (fossil_rx_ready())
        {
            in.h.ah = FOSSIL_RX_CHAR;
            in.x.dx = g_fossil.port;
            fossil_int(&in, &out);
            return out.h.al & 0xFF;
        }
    }

    return -1;
}

int fossil_peek_char(void)
{
    union REGS in, out;

    if (!g_fossil.initialized && !fossil_init())
        return -1;

    in.h.ah = FOSSIL_PEEK;
    in.x.dx = g_fossil.port;
    fossil_int(&in, &out);

    /*
     * Common convention: AX = FFFFh means none available.
     */
    if (out.x.ax == 0xFFFF)
        return -1;

    return out.h.al & 0xFF;
}

/* ------------------------------------------------------------ */
/* BBS-facing wrappers                                          */
/* ------------------------------------------------------------ */

void fossil_start_session(void)
{
    PARAMS p;
    int baud, parity, word, stops;

    if (!fossil_init())
        return;

    baud = 2400;
    parity = 0;
    word = 8;
    stops = 1;

    /*
     * Pull from current session if available.
     * PARAMS layout in reconstruction:
     * parity/even/stops/word are bitfields.
     */
    memset(&p, 0, sizeof(p));

    if (g_sess.user.term < NUM_TERM)
    {
        baud = 2400;
    }

    fossil_configure(baud, parity, word, stops);
    fossil_raise_dtr();
    fossil_purge_input();
    fossil_purge_output();
}

void fossil_end_session(void)
{
    fossil_flush_output();
    fossil_deinit();
}

void fossil_show_status(void)
{
    puts("");
    printf("FOSSIL installed    %s\n", g_fossil.installed ? "Yes" : "No");
    printf("FOSSIL initialized  %s\n", g_fossil.initialized ? "Yes" : "No");
    printf("Port                COM%d\n", g_fossil.port + 1);
    printf("Baud                %d\n", g_fossil.baud);
    printf("Carrier             %s\n", fossil_carrier() ? "Yes" : "No");
    puts("");
}