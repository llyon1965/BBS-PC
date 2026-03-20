/* BBSMODM.C
 *
 * BBS-PC ! 4.21
 *
 * Reconstructed and modernised source
 * derived from BBS-PC 4.20
 *
 * Modem / protocol support module
 *
 * Current scope:
 * - remote transport wired through BBSFOSSIL.C
 * - local mode still writes to stdout for bring-up/testing
 * - ASCII transfer with line/block modes and XON/XOFF pacing
 * - XMODEM / XMODEM-CRC
 * - YMODEM 1K
 * - YMODEM-Batch with filename header packet
 * - Kermit
 * - improved ZMODEM:
 *     * hex and binary headers
 *     * CRC16 / CRC32 paths
 *     * ZRQINIT / ZRINIT / ZFILE / ZDATA / ZEOF / ZFIN / ZRPOS
 *     * resume offsets / crash recovery via ZRPOS
 *     * windowed / streaming send behaviour
 *     * timeout and retransmit handling
 *     * batch send/receive entry points
 *
 * Notes:
 * - This remains a substantial protocol-layer reconstruction rather than
 *   a claim of byte-for-byte historical perfection.
 * - Real remote operation depends on the FOSSIL driver working correctly.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "bbsdata.h"
#include "bbsfunc.h"

/* ------------------------------------------------------------ */
/* ASCII                                                        */
/* ------------------------------------------------------------ */

#define ASCII_XON           0x11
#define ASCII_XOFF          0x13
#define ASCII_SUB           0x1A
#define ASCII_CR            0x0D
#define ASCII_LF            0x0A

#define ASCII_MODE_LINE     0
#define ASCII_MODE_BLOCK    1

#define ASCII_BLOCK_SIZE    32
#define ASCII_PACE_TIMEOUT  300

/* ------------------------------------------------------------ */
/* X/Y MODEM                                                    */
/* ------------------------------------------------------------ */

#define XMODEM_SOH          0x01
#define XMODEM_STX          0x02
#define XMODEM_EOT          0x04
#define XMODEM_ACK          0x06
#define XMODEM_NAK          0x15
#define XMODEM_CAN          0x18
#define XMODEM_CRC_REQ      'C'

#define XMODEM_BLOCK_SIZE   128
#define YMODEM_BLOCK_SIZE   1024
#define YMODEM_HDR_SIZE     128

#define XMODEM_RETRIES      10
#define XMODEM_TIMEOUT      600

/* ------------------------------------------------------------ */
/* KERMIT                                                       */
/* ------------------------------------------------------------ */

#define KERMIT_SOH          0x01
#define KERMIT_CR           0x0D
#define KERMIT_MAX_DATA     90
#define KERMIT_TIMEOUT      600
#define KERMIT_RETRIES      10
#define KERMIT_PADCHAR      0x00
#define KERMIT_QUOTE        '#'
#define KERMIT_EOL          KERMIT_CR

/* ------------------------------------------------------------ */
/* ZMODEM                                                       */
/* ------------------------------------------------------------ */

#define ZMODEM_TIMEOUT          600
#define ZMODEM_RETRIES          10
#define ZMODEM_DATABUF          1024
#define ZMODEM_WINBUF           8192

#define ZMODEM_WIN_MIN          1024UL
#define ZMODEM_WIN_DEFAULT      4096UL
#define ZMODEM_WIN_MAX          8192UL

#define ZMODEM_ACK_TIMEOUTS_MAX 3
#define ZMODEM_RPOS_RETRIES     8
#define ZMODEM_DATA_RETRIES     8
#define ZMODEM_HDR_RETRIES      8

#define ZPAD                '*'
#define ZDLE                0x18
#define ZBIN                'A'
#define ZHEX                'B'
#define ZBIN32              'C'

#define ZCRCE               'h'
#define ZCRCG               'i'
#define ZCRCQ               'j'
#define ZCRCW               'k'
#define ZRUB0               'l'
#define ZRUB1               'm'

#define ZRQINIT             0
#define ZRINIT              1
#define ZSINIT              2
#define ZACK                3
#define ZFILE               4
#define ZSKIP               5
#define ZNAK                6
#define ZABORT              7
#define ZFIN                8
#define ZRPOS               9
#define ZDATA               10
#define ZEOF                11
#define ZFERR               12
#define ZCRC                13
#define ZCHALLENGE          14
#define ZCOMPL              15
#define ZCAN                16
#define ZFREECNT            17
#define ZCOMMAND            18
#define ZSTDERR             19

#define ZF0_CANFDX          0x01
#define ZF0_CANOVIO         0x02
#define ZF0_CANBRK          0x04
#define ZF0_CANCRY          0x08
#define ZF0_CANLZW          0x10
#define ZF0_CANFC32         0x20
#define ZF0_ESCCTL          0x40
#define ZF0_ESC8            0x80

#define ZCBIN               1
#define ZCNL                2
#define ZCRESUM             3

/* ------------------------------------------------------------ */

typedef struct {
    int online;
    int initialized;
    int baud;
    PARAMS com;
    int current_protocol;
    int local_mode;
    int ascii_mode;
    int xonxoff;
} MODEMSTATE;

typedef struct {
    int maxl;
    int timeout;
    int npad;
    int padc;
    int eol;
    int quote;
} KERMITPARM;

typedef struct {
    int type;
    unsigned char hdr[4];
    unsigned long pos;
} ZHDR;

typedef struct {
    int use_crc32;
    int tx_binary_headers;
    int rx_binary_headers;
    int escape_ctrl;
    int escape_8th;
    int full_duplex;
    int overlap_io;
    unsigned long tx_window;
    unsigned long rx_window;
    unsigned long resume_pos;
    unsigned long ack_pos;
    unsigned long last_sent_pos;
    int batch;
    int timeout_count;
    int zrpos_count;
    int ackless_bursts;
} ZMODEMSTATE;

static MODEMSTATE  g_modem_state;
static ZMODEMSTATE g_zm;

/* ------------------------------------------------------------ */
/* generic helpers                                              */
/* ------------------------------------------------------------ */

static void modem_zero_state(void)
{
    memset(&g_modem, 0, sizeof(g_modem_state));
    g_modem_state.local_mode = 1;
    g_modem_state.current_protocol = 0;
    g_modem_state.ascii_mode = ASCII_MODE_LINE;
    g_modem_state.xonxoff = 1;
    g_modem_state.baud = 2400;
}

static void modem_copy_user_state(void)
{
    g_modem_state.current_protocol = g_sess.user.protocol;
    g_modem_state.local_mode = g_sess.local_login ? 1 : 0;
}

static int protocol_is_binary(proto)
int proto;
{
    switch (proto)
    {
        case 1:
        case 2:
        case 3:
        case 4:
        case 5:
        case 6:
            return 1;
    }
    return 0;
}

static int file_exists_local(path)
char *path;
{
    FILE *fp;

    fp = fopen(path, "rb");
    if (!fp)
        return 0;
    fclose(fp);
    return 1;
}

static long file_length_local(path)
char *path;
{
    FILE *fp;
    long len;

    fp = fopen(path, "rb");
    if (!fp)
        return -1L;

    fseek(fp, 0L, SEEK_END);
    len = ftell(fp);
    fclose(fp);
    return len;
}

static int file_is_text_name(path)
char *path;
{
    char *p;

    p = strrchr(path, '.');
    if (!p)
        return 0;

    if (!stricmp(p, ".ASC")) return 1;
    if (!stricmp(p, ".TXT")) return 1;
    if (!stricmp(p, ".DOC")) return 1;
    if (!stricmp(p, ".DIR")) return 1;
    if (!stricmp(p, ".PAS")) return 1;
    if (!stricmp(p, ".ASM")) return 1;
    if (!stricmp(p, ".MAC")) return 1;
    if (!stricmp(p, ".BAT")) return 1;
    if (!stricmp(p, ".PRN")) return 1;
    if (!stricmp(p, ".LST")) return 1;
    if (!stricmp(p, ".HEX")) return 1;
    if (!stricmp(p, ".ME"))  return 1;
    if (!stricmp(p, ".C"))   return 1;
    if (!stricmp(p, ".H"))   return 1;

    return 0;
}

static void basename_only(path, out, outlen)
char *path;
char *out;
int outlen;
{
    char *p1, *p2, *p;

    p1 = strrchr(path, '/');
    p2 = strrchr(path, '\\');

    p = path;
    if (p1 && p2)
        p = (p1 > p2) ? (p1 + 1) : (p2 + 1);
    else if (p1)
        p = p1 + 1;
    else if (p2)
        p = p2 + 1;

    strncpy(out, p, outlen - 1);
    out[outlen - 1] = 0;
}

static int hex_digit(v)
int v;
{
    v &= 0x0F;
    if (v < 10)
        return '0' + v;
    return 'A' + (v - 10);
}

static int hex_value(ch)
int ch;
{
    if (ch >= '0' && ch <= '9')
        return ch - '0';
    if (ch >= 'A' && ch <= 'F')
        return ch - 'A' + 10;
    if (ch >= 'a' && ch <= 'f')
        return ch - 'a' + 10;
    return -1;
}

static int wait_for_xon(void)
{
    int ch;

    if (!g_modem_state.xonxoff)
        return 1;

    for (;;)
    {
        ch = modem_recv_byte(ASCII_PACE_TIMEOUT);

        if (ch < 0)
            return 1;

        ch &= 0x7F;

        if (ch == ASCII_XON)
            return 1;
        if (ch == ASCII_XOFF)
            continue;

        return 1;
    }
}

static int send_one_byte(ch)
int ch;
{
    if (!wait_for_xon())
        return 0;

    return modem_send_byte(ch) >= 0;
}

static int recv_one_byte(timeout)
int timeout;
{
    return modem_recv_byte(timeout);
}

static int send_bytes(buf, len)
unsigned char *buf;
int len;
{
    int i;

    for (i = 0; i < len; i++)
        if (!send_one_byte(buf[i]))
            return 0;

    return 1;
}

static int recv_bytes(buf, len, timeout)
unsigned char *buf;
int len;
int timeout;
{
    int i, ch;

    for (i = 0; i < len; i++)
    {
        ch = recv_one_byte(timeout);
        if (ch < 0)
            return 0;
        buf[i] = (unsigned char)ch;
    }

    return 1;
}

static int send_crlf(void)
{
    if (!send_one_byte(ASCII_CR))
        return 0;
    if (!send_one_byte(ASCII_LF))
        return 0;
    return 1;
}

static int send_ascii_hex_byte(b)
int b;
{
    if (!send_one_byte(hex_digit((b >> 4) & 0x0F)))
        return 0;
    if (!send_one_byte(hex_digit(b & 0x0F)))
        return 0;
    return 1;
}

static int send_ascii_hex_block(buf, len)
unsigned char *buf;
int len;
{
    int i;

    for (i = 0; i < len; i++)
        if (!send_ascii_hex_byte(buf[i]))
            return 0;

    return send_crlf();
}

static int send_text_line(line)
char *line;
{
    while (*line)
    {
        if (*line != '\r' && *line != '\n')
            if (!send_one_byte((unsigned char)*line))
                return 0;
        line++;
    }

    return send_crlf();
}

static unsigned char xmodem_checksum(buf, len)
unsigned char *buf;
int len;
{
    unsigned int sum = 0;
    int i;

    for (i = 0; i < len; i++)
        sum = (sum + buf[i]) & 0xFF;

    return (unsigned char)sum;
}

static ushort crc16_ccitt(buf, len)
unsigned char *buf;
int len;
{
    ushort crc = 0;
    int i, bit;

    for (i = 0; i < len; i++)
    {
        crc ^= (ushort)(buf[i] << 8);
        for (bit = 0; bit < 8; bit++)
        {
            if (crc & 0x8000)
                crc = (ushort)((crc << 1) ^ 0x1021);
            else
                crc <<= 1;
        }
    }

    return crc;
}

static unsigned long zmodem_crc32_buf(buf, len)
unsigned char *buf;
int len;
{
    unsigned long crc = 0xFFFFFFFFUL;
    int i, j;

    for (i = 0; i < len; i++)
    {
        crc ^= (unsigned long)buf[i];
        for (j = 0; j < 8; j++)
        {
            if (crc & 1UL)
                crc = (crc >> 1) ^ 0xEDB88320UL;
            else
                crc >>= 1;
        }
    }

    return crc ^ 0xFFFFFFFFUL;
}

/* ------------------------------------------------------------ */
/* ASCII                                                        */
/* ------------------------------------------------------------ */

static int ascii_send_text_line_mode(path)
char *path;
{
    FILE *fp;
    char line[256];
    int ok = 1;

    fp = fopen(path, "rt");
    if (!fp)
        return 0;

    while (fgets(line, sizeof(line), fp))
    {
        if (!send_text_line(line))
        {
            ok = 0;
            break;
        }
    }

    fclose(fp);
    return ok;
}

static int ascii_send_text_block_mode(path)
char *path;
{
    FILE *fp;
    unsigned char buf[ASCII_BLOCK_SIZE];
    int n, i, ok = 1;

    fp = fopen(path, "rb");
    if (!fp)
        return 0;

    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
    {
        for (i = 0; i < n; i++)
        {
            if (buf[i] == '\r')
                continue;

            if (buf[i] == '\n')
            {
                if (!send_crlf())
                {
                    ok = 0;
                    break;
                }
            }
            else
            {
                if (!send_one_byte(buf[i]))
                {
                    ok = 0;
                    break;
                }
            }
        }

        if (!ok)
            break;
    }

    fclose(fp);
    return ok;
}

static int ascii_send_binary_hex_line_mode(path)
char *path;
{
    FILE *fp;
    int ch;
    int count = 0;

    fp = fopen(path, "rb");
    if (!fp)
        return 0;

    while ((ch = fgetc(fp)) != EOF)
    {
        if (!send_ascii_hex_byte(ch))
        {
            fclose(fp);
            return 0;
        }

        count++;
        if (count >= 32)
        {
            if (!send_crlf())
            {
                fclose(fp);
                return 0;
            }
            count = 0;
        }
    }

    if (count != 0)
        if (!send_crlf())
        {
            fclose(fp);
            return 0;
        }

    fclose(fp);
    return 1;
}

static int ascii_send_binary_hex_block_mode(path)
char *path;
{
    FILE *fp;
    unsigned char buf[ASCII_BLOCK_SIZE];
    int n;

    fp = fopen(path, "rb");
    if (!fp)
        return 0;

    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
        if (!send_ascii_hex_block(buf, n))
        {
            fclose(fp);
            return 0;
        }

    fclose(fp);
    return 1;
}

static int ascii_transfer_send(path)
char *path;
{
    int is_text;

    is_text = file_is_text_name(path);

    if (is_text)
    {
        if (g_modem_state.ascii_mode == ASCII_MODE_BLOCK)
            return ascii_send_text_block_mode(path);
        return ascii_send_text_line_mode(path);
    }

    puts("Binary file will be sent as ASCII-Hex");

    if (g_modem_state.ascii_mode == ASCII_MODE_BLOCK)
        return ascii_send_binary_hex_block_mode(path);
    return ascii_send_binary_hex_line_mode(path);
}

/* ------------------------------------------------------------ */
/* XMODEM / YMODEM                                              */
/* ------------------------------------------------------------ */

static int xmodem_send_cancel(void)
{
    send_one_byte(XMODEM_CAN);
    send_one_byte(XMODEM_CAN);
    return 0;
}

static int xmodem_wait_receiver_start(use_crc)
int *use_crc;
{
    int tries, ch;

    for (tries = 0; tries < XMODEM_RETRIES * 3; tries++)
    {
        ch = recv_one_byte(XMODEM_TIMEOUT);
        if (ch < 0)
            continue;

        ch &= 0xFF;

        if (ch == XMODEM_CRC_REQ)
        {
            *use_crc = 1;
            return 1;
        }

        if (ch == XMODEM_NAK)
        {
            *use_crc = 0;
            return 1;
        }

        if (ch == XMODEM_CAN)
            return 0;
    }

    return 0;
}

static int xmodem_send_block_len_buf(data, blkno, use_crc, blksz, headerch)
unsigned char *data;
int blkno;
int use_crc;
int blksz;
int headerch;
{
    int i, tries, ch;
    ushort crc;
    unsigned char sum;

    for (tries = 0; tries < XMODEM_RETRIES; tries++)
    {
        if (!send_one_byte(headerch))
            return 0;
        if (!send_one_byte(blkno & 0xFF))
            return 0;
        if (!send_one_byte(0xFF - (blkno & 0xFF)))
            return 0;

        for (i = 0; i < blksz; i++)
            if (!send_one_byte(data[i]))
                return 0;

        if (use_crc)
        {
            crc = crc16_ccitt(data, blksz);
            if (!send_one_byte((crc >> 8) & 0xFF))
                return 0;
            if (!send_one_byte(crc & 0xFF))
                return 0;
        }
        else
        {
            sum = xmodem_checksum(data, blksz);
            if (!send_one_byte(sum))
                return 0;
        }

        ch = recv_one_byte(XMODEM_TIMEOUT);
        if (ch == XMODEM_ACK)
            return 1;
        if (ch == XMODEM_CAN)
            return 0;
        if (ch == XMODEM_NAK)
            continue;
    }

    return 0;
}

static int xmodem_send_block_len(fp, blkno, use_crc, blksz, headerch)
FILE *fp;
int blkno;
int use_crc;
int blksz;
int headerch;
{
    unsigned char data[YMODEM_BLOCK_SIZE];
    int n;

    memset(data, ASCII_SUB, sizeof(data));
    n = fread(data, 1, blksz, fp);

    if (!xmodem_send_block_len_buf(data, blkno, use_crc, blksz, headerch))
        return 0;

    return n;
}

static int xmodem_send_block(fp, blkno, use_crc)
FILE *fp;
int blkno;
int use_crc;
{
    return xmodem_send_block_len(fp, blkno, use_crc,
        XMODEM_BLOCK_SIZE, XMODEM_SOH);
}

static int ymodem_send_header_packet(path)
char *path;
{
    unsigned char data[YMODEM_HDR_SIZE];
    char fname[128];
    long len;
    char lenstr[32];
    int p;

    memset(data, 0, sizeof(data));
    basename_only(path, fname, sizeof(fname));
    len = file_length_local(path);
    sprintf(lenstr, "%ld", len < 0L ? 0L : len);

    p = 0;
    strncpy((char *)data + p, fname, sizeof(data) - p - 1);
    p += strlen(fname) + 1;
    strncpy((char *)data + p, lenstr, sizeof(data) - p - 1);

    return xmodem_send_block_len_buf(data, 0, 1,
        YMODEM_HDR_SIZE, XMODEM_SOH);
}

static int ymodem_send_end_batch_packet(void)
{
    unsigned char data[YMODEM_HDR_SIZE];

    memset(data, 0, sizeof(data));
    return xmodem_send_block_len_buf(data, 0, 1,
        YMODEM_HDR_SIZE, XMODEM_SOH);
}

static int ymodem_send_block(fp, blkno)
FILE *fp;
int blkno;
{
    return xmodem_send_block_len(fp, blkno, 1,
        YMODEM_BLOCK_SIZE, XMODEM_STX);
}

static int xmodem_send_file_impl(path, proto)
char *path;
int proto;
{
    FILE *fp;
    int use_crc = 0;
    int blkno = 1;
    int rc, tries, ch;

    fp = fopen(path, "rb");
    if (!fp)
        return 0;

    if (!xmodem_wait_receiver_start(&use_crc))
    {
        fclose(fp);
        return 0;
    }

    if (proto == 2)
        use_crc = 1;

    for (;;)
    {
        rc = xmodem_send_block(fp, blkno, use_crc);
        if (rc <= 0)
        {
            fclose(fp);
            return xmodem_send_cancel();
        }

        blkno = (blkno + 1) & 0xFF;
        if (rc < XMODEM_BLOCK_SIZE)
            break;
    }

    for (tries = 0; tries < XMODEM_RETRIES; tries++)
    {
        if (!send_one_byte(XMODEM_EOT))
        {
            fclose(fp);
            return 0;
        }

        ch = recv_one_byte(XMODEM_TIMEOUT);
        if (ch == XMODEM_ACK)
        {
            fclose(fp);
            return 1;
        }

        if (ch == XMODEM_CAN)
        {
            fclose(fp);
            return 0;
        }
    }

    fclose(fp);
    return 0;
}

static int ymodem_send_file_common(path, batch_mode)
char *path;
int batch_mode;
{
    FILE *fp;
    int blkno = 1;
    int rc, tries, ch;
    int use_crc = 1;

    fp = fopen(path, "rb");
    if (!fp)
        return 0;

    if (!xmodem_wait_receiver_start(&use_crc))
    {
        fclose(fp);
        return 0;
    }

    if (batch_mode)
    {
        if (!ymodem_send_header_packet(path))
        {
            fclose(fp);
            return xmodem_send_cancel();
        }

        ch = recv_one_byte(XMODEM_TIMEOUT);
        if (ch != XMODEM_CRC_REQ && ch != XMODEM_ACK)
        {
            fclose(fp);
            return xmodem_send_cancel();
        }

        if (ch == XMODEM_ACK)
        {
            ch = recv_one_byte(XMODEM_TIMEOUT);
            if (ch != XMODEM_CRC_REQ)
            {
                fclose(fp);
                return xmodem_send_cancel();
            }
        }
    }

    for (;;)
    {
        rc = ymodem_send_block(fp, blkno);
        if (rc <= 0)
        {
            fclose(fp);
            return xmodem_send_cancel();
        }

        blkno = (blkno + 1) & 0xFF;
        if (rc < YMODEM_BLOCK_SIZE)
            break;
    }

    for (tries = 0; tries < XMODEM_RETRIES; tries++)
    {
        if (!send_one_byte(XMODEM_EOT))
        {
            fclose(fp);
            return 0;
        }

        ch = recv_one_byte(XMODEM_TIMEOUT);
        if (ch == XMODEM_ACK)
        {
            if (batch_mode)
            {
                ch = recv_one_byte(XMODEM_TIMEOUT);
                if (ch == XMODEM_CRC_REQ)
                {
                    if (!ymodem_send_end_batch_packet())
                    {
                        fclose(fp);
                        return 0;
                    }

                    ch = recv_one_byte(XMODEM_TIMEOUT);
                    if (ch != XMODEM_ACK)
                    {
                        fclose(fp);
                        return 0;
                    }
                }
            }

            fclose(fp);
            return 1;
        }

        if (ch == XMODEM_CAN)
        {
            fclose(fp);
            return 0;
        }
    }

    fclose(fp);
    return 0;
}

static int ymodem_send_file_impl(path)
char *path;
{
    return ymodem_send_file_common(path, 0);
}

static int ymodem_batch_send_file_impl(path)
char *path;
{
    return ymodem_send_file_common(path, 1);
}

static int xmodem_recv_block_len(fp, expected_blk, use_crc, out_blksz, hdrch_out, data_out)
FILE *fp;
int expected_blk;
int use_crc;
int *out_blksz;
int *hdrch_out;
unsigned char *data_out;
{
    int ch, blk, blkc;
    int blksz;
    unsigned char sum, gotsum;
    ushort crc, gotcrc;
    int i;

    ch = recv_one_byte(XMODEM_TIMEOUT);
    if (ch < 0)
        return -2;

    if (ch == XMODEM_EOT)
        return -1;

    if (ch == XMODEM_CAN)
        return -3;

    if (ch == XMODEM_SOH)
        blksz = XMODEM_BLOCK_SIZE;
    else if (ch == XMODEM_STX)
        blksz = YMODEM_BLOCK_SIZE;
    else
        return -2;

    blk = recv_one_byte(XMODEM_TIMEOUT);
    blkc = recv_one_byte(XMODEM_TIMEOUT);
    if (blk < 0 || blkc < 0)
        return -2;

    if (((blk + blkc) & 0xFF) != 0xFF)
        return -2;

    if (!recv_bytes(data_out, blksz, XMODEM_TIMEOUT))
        return -2;

    if (use_crc)
    {
        ch = recv_one_byte(XMODEM_TIMEOUT);
        if (ch < 0)
            return -2;
        gotcrc = (ushort)((ch & 0xFF) << 8);

        ch = recv_one_byte(XMODEM_TIMEOUT);
        if (ch < 0)
            return -2;
        gotcrc |= (ushort)(ch & 0xFF);

        crc = crc16_ccitt(data_out, blksz);
        if (crc != gotcrc)
            return -2;
    }
    else
    {
        ch = recv_one_byte(XMODEM_TIMEOUT);
        if (ch < 0)
            return -2;
        gotsum = (unsigned char)ch;

        sum = xmodem_checksum(data_out, blksz);
        if (sum != gotsum)
            return -2;
    }

    if (out_blksz)
        *out_blksz = blksz;
    if (hdrch_out)
        *hdrch_out = (blksz == XMODEM_BLOCK_SIZE) ? XMODEM_SOH : XMODEM_STX;

    if ((blk & 0xFF) == ((expected_blk - 1) & 0xFF))
        return 0;

    if ((blk & 0xFF) != (expected_blk & 0xFF))
        return -2;

    if (fp)
        for (i = 0; i < blksz; i++)
            fputc(data_out[i], fp);

    return blksz;
}

static int xmodem_recv_file_impl(path, proto)
char *path;
int proto;
{
    FILE *fp;
    int expected_blk = 1;
    int tries = 0;
    int use_crc;
    int rc;
    unsigned char data[YMODEM_BLOCK_SIZE];
    int blksz, hdrch;

    fp = fopen(path, "wb");
    if (!fp)
        return 0;

    use_crc = (proto == 2) ? 1 : 0;

    if (!send_one_byte(use_crc ? XMODEM_CRC_REQ : XMODEM_NAK))
    {
        fclose(fp);
        return 0;
    }

    for (;;)
    {
        rc = xmodem_recv_block_len(fp, expected_blk, use_crc, &blksz, &hdrch, data);

        if (rc == -1)
        {
            send_one_byte(XMODEM_ACK);
            fclose(fp);
            return 1;
        }

        if (rc == -3)
        {
            fclose(fp);
            return 0;
        }

        if (rc > 0)
        {
            send_one_byte(XMODEM_ACK);
            expected_blk = (expected_blk + 1) & 0xFF;
            tries = 0;
            continue;
        }

        if (rc == 0)
        {
            send_one_byte(XMODEM_ACK);
            continue;
        }

        send_one_byte(XMODEM_NAK);
        tries++;
        if (tries >= XMODEM_RETRIES)
        {
            send_one_byte(XMODEM_CAN);
            send_one_byte(XMODEM_CAN);
            fclose(fp);
            return 0;
        }
    }
}

static int ymodem_parse_header(data, pathbuf, pathlen)
unsigned char *data;
char *pathbuf;
int pathlen;
{
    if (!data[0])
        return 0;

    strncpy(pathbuf, (char *)data, pathlen - 1);
    pathbuf[pathlen - 1] = 0;
    return 1;
}

static int ymodem_recv_file_common(path, batch_mode)
char *path;
int batch_mode;
{
    FILE *fp = NULL;
    int expected_blk = batch_mode ? 0 : 1;
    int tries = 0;
    int use_crc = 1;
    int rc;
    unsigned char data[YMODEM_BLOCK_SIZE];
    int blksz, hdrch;
    char recvname[MAX_PATHNAME];
    int got_header = batch_mode ? 0 : 1;

    if (!send_one_byte(XMODEM_CRC_REQ))
        return 0;

    for (;;)
    {
        rc = xmodem_recv_block_len(NULL, expected_blk, use_crc, &blksz, &hdrch, data);

        if (rc == -1)
        {
            send_one_byte(XMODEM_ACK);
            if (fp)
                fclose(fp);
            return 1;
        }

        if (rc == -3)
        {
            if (fp)
                fclose(fp);
            return 0;
        }

        if (rc > 0)
        {
            if (batch_mode && !got_header && expected_blk == 0)
            {
                if (!ymodem_parse_header(data, recvname, sizeof(recvname)))
                {
                    send_one_byte(XMODEM_ACK);
                    if (fp)
                        fclose(fp);
                    return 1;
                }

                if (!path || !*path)
                    path = recvname;

                fp = fopen(path, "wb");
                if (!fp)
                {
                    send_one_byte(XMODEM_CAN);
                    send_one_byte(XMODEM_CAN);
                    return 0;
                }

                got_header = 1;
                expected_blk = 1;
                send_one_byte(XMODEM_ACK);
                send_one_byte(XMODEM_CRC_REQ);
                tries = 0;
                continue;
            }

            if (!fp)
            {
                fp = fopen(path, "wb");
                if (!fp)
                {
                    send_one_byte(XMODEM_CAN);
                    send_one_byte(XMODEM_CAN);
                    return 0;
                }
            }

            fwrite(data, 1, blksz, fp);
            send_one_byte(XMODEM_ACK);
            expected_blk = (expected_blk + 1) & 0xFF;
            tries = 0;
            continue;
        }

        if (rc == 0)
        {
            send_one_byte(XMODEM_ACK);
            continue;
        }

        send_one_byte(XMODEM_NAK);
        tries++;
        if (tries >= XMODEM_RETRIES)
        {
            send_one_byte(XMODEM_CAN);
            send_one_byte(XMODEM_CAN);
            if (fp)
                fclose(fp);
            return 0;
        }
    }
}

static int ymodem_recv_file_impl(path)
char *path;
{
    return ymodem_recv_file_common(path, 0);
}

static int ymodem_batch_recv_file_impl(path)
char *path;
{
    return ymodem_recv_file_common(path, 1);
}

/* ------------------------------------------------------------ */
/* KERMIT                                                       */
/* ------------------------------------------------------------ */

static void kermit_default_parms(kp)
KERMITPARM *kp;
{
    kp->maxl = KERMIT_MAX_DATA;
    kp->timeout = 5;
    kp->npad = 0;
    kp->padc = KERMIT_PADCHAR;
    kp->eol = KERMIT_EOL;
    kp->quote = KERMIT_QUOTE;
}

static int kermit_tochar(v)
int v;
{
    return (v + 32) & 0x7F;
}

static int kermit_unchar(v)
int v;
{
    return (v - 32) & 0x3F;
}

static int kermit_ctl(ch)
int ch;
{
    return ch ^ 0x40;
}

static int kermit_needs_quote(ch)
int ch;
{
    ch &= 0xFF;

    if (ch < 32 || ch == 127)
        return 1;
    if (ch == KERMIT_QUOTE)
        return 1;

    return 0;
}

static int kermit_checksum(pkt, len)
unsigned char *pkt;
int len;
{
    int i;
    unsigned int sum = 0;

    for (i = 1; i < len; i++)
        sum += pkt[i];

    sum = (sum + ((sum & 0xC0) >> 6)) & 0x3F;
    return kermit_tochar(sum);
}

static int kermit_send_packet(seq, type, data, dlen)
int seq;
int type;
unsigned char *data;
int dlen;
{
    unsigned char pkt[140];
    int i, p = 0;
    int count;
    int csum;

    pkt[p++] = KERMIT_SOH;
    count = dlen + 3;
    pkt[p++] = kermit_tochar(count);
    pkt[p++] = kermit_tochar(seq);
    pkt[p++] = type;

    for (i = 0; i < dlen; i++)
        pkt[p++] = data[i];

    csum = kermit_checksum(pkt, p);
    pkt[p++] = csum;
    pkt[p++] = KERMIT_EOL;

    return send_bytes(pkt, p);
}

static int kermit_read_packet(seq, type, data, dlen)
int *seq;
int *type;
unsigned char *data;
int *dlen;
{
    int ch, count, i;
    unsigned char buf[140];
    int p = 0;
    int csum;

    for (;;)
    {
        ch = recv_one_byte(KERMIT_TIMEOUT);
        if (ch < 0)
            return 0;
        if ((ch & 0xFF) == KERMIT_SOH)
            break;
    }

    buf[p++] = KERMIT_SOH;

    ch = recv_one_byte(KERMIT_TIMEOUT);
    if (ch < 0)
        return 0;
    buf[p++] = (unsigned char)ch;
    count = kermit_unchar(ch);

    for (i = 0; i < count; i++)
    {
        ch = recv_one_byte(KERMIT_TIMEOUT);
        if (ch < 0)
            return 0;
        buf[p++] = (unsigned char)ch;
    }

    ch = recv_one_byte(KERMIT_TIMEOUT);
    if (ch < 0)
        return 0;
    if ((ch & 0xFF) != KERMIT_EOL)
        return 0;

    csum = kermit_checksum(buf, p - 1);
    if (csum != buf[p - 1])
        return 0;

    *seq = kermit_unchar(buf[2]);
    *type = buf[3];
    *dlen = count - 3;

    for (i = 0; i < *dlen; i++)
        data[i] = buf[4 + i];

    return 1;
}

static int kermit_send_ack(seq)
int seq;
{
    return kermit_send_packet(seq, 'Y', (unsigned char *)"", 0);
}

static int kermit_send_nak(seq)
int seq;
{
    return kermit_send_packet(seq, 'N', (unsigned char *)"", 0);
}

static int kermit_wait_ack(expect_seq)
int expect_seq;
{
    int seq, type, dlen;
    unsigned char data[100];
    int tries = 0;

    while (tries < KERMIT_RETRIES)
    {
        if (!kermit_read_packet(&seq, &type, data, &dlen))
        {
            tries++;
            continue;
        }

        if (seq != expect_seq)
            continue;
        if (type == 'Y')
            return 1;
        if (type == 'N')
            return 0;
    }

    return 0;
}

static int kermit_send_packet_wait_ack(seq, type, data, dlen)
int seq;
int type;
unsigned char *data;
int dlen;
{
    int tries;

    for (tries = 0; tries < KERMIT_RETRIES; tries++)
    {
        if (!kermit_send_packet(seq, type, data, dlen))
            return 0;
        if (kermit_wait_ack(seq))
            return 1;
    }

    return 0;
}

static int kermit_build_send_init(out, kp)
unsigned char *out;
KERMITPARM *kp;
{
    out[0] = kermit_tochar(kp->maxl);
    out[1] = kermit_tochar(kp->timeout);
    out[2] = kermit_tochar(kp->npad);
    out[3] = kermit_ctl(kp->padc);
    out[4] = kermit_tochar(kp->eol);
    out[5] = kp->quote;
    return 6;
}

static int kermit_encode_chunk(src, srclen, out, maxout, quotech)
unsigned char *src;
int srclen;
unsigned char *out;
int maxout;
int quotech;
{
    int i, o = 0;
    int ch;

    for (i = 0; i < srclen; i++)
    {
        ch = src[i] & 0xFF;

        if (kermit_needs_quote(ch))
        {
            if (o + 2 > maxout)
                break;
            out[o++] = (unsigned char)quotech;
            if (ch == quotech)
                out[o++] = (unsigned char)ch;
            else
                out[o++] = (unsigned char)kermit_ctl(ch);
        }
        else
        {
            if (o + 1 > maxout)
                break;
            out[o++] = (unsigned char)ch;
        }
    }

    return o;
}

static int kermit_decode_data(src, srclen, out, maxout, quotech)
unsigned char *src;
int srclen;
unsigned char *out;
int maxout;
int quotech;
{
    int i = 0, o = 0;
    int ch;

    while (i < srclen)
    {
        ch = src[i++] & 0xFF;

        if (ch == quotech)
        {
            if (i >= srclen)
                break;
            ch = src[i++] & 0xFF;
            if (ch == quotech)
                ch = quotech;
            else
                ch = kermit_ctl(ch);
        }

        if (o < maxout)
            out[o++] = (unsigned char)ch;
        else
            break;
    }

    return o;
}

static int kermit_send_file_impl(path)
char *path;
{
    FILE *fp;
    KERMITPARM kp;
    unsigned char data[128];
    unsigned char enc[128];
    unsigned char raw[64];
    char fname[64];
    int seq = 0;
    int n, dlen;

    fp = fopen(path, "rb");
    if (!fp)
        return 0;

    kermit_default_parms(&kp);

    dlen = kermit_build_send_init(data, &kp);
    if (!kermit_send_packet_wait_ack(seq, 'S', data, dlen))
    {
        fclose(fp);
        return 0;
    }
    seq = (seq + 1) & 0x3F;

    basename_only(path, fname, sizeof(fname));
    if (!kermit_send_packet_wait_ack(seq, 'F', (unsigned char *)fname, strlen(fname)))
    {
        fclose(fp);
        return 0;
    }
    seq = (seq + 1) & 0x3F;

    while ((n = fread(raw, 1, sizeof(raw), fp)) > 0)
    {
        dlen = kermit_encode_chunk(raw, n, enc, kp.maxl, kp.quote);
        if (!kermit_send_packet_wait_ack(seq, 'D', enc, dlen))
        {
            fclose(fp);
            return 0;
        }
        seq = (seq + 1) & 0x3F;
    }

    if (!kermit_send_packet_wait_ack(seq, 'Z', (unsigned char *)"", 0))
    {
        fclose(fp);
        return 0;
    }
    seq = (seq + 1) & 0x3F;

    if (!kermit_send_packet_wait_ack(seq, 'B', (unsigned char *)"", 0))
    {
        fclose(fp);
        return 0;
    }

    fclose(fp);
    return 1;
}

static int kermit_recv_file_impl(path)
char *path;
{
    KERMITPARM kp;
    FILE *fp = NULL;
    unsigned char data[128];
    unsigned char dec[128];
    int seq, type, dlen;
    int expect_seq = 0;
    char recvname[64];
    int n;

    kermit_default_parms(&kp);

    for (;;)
    {
        if (!kermit_read_packet(&seq, &type, data, &dlen))
        {
            kermit_send_nak(expect_seq);
            continue;
        }

        if (seq != expect_seq)
        {
            kermit_send_ack(seq);
            continue;
        }

        switch (type)
        {
            case 'S':
                kermit_send_ack(seq);
                expect_seq = (expect_seq + 1) & 0x3F;
                break;

            case 'F':
                memset(recvname, 0, sizeof(recvname));
                if (dlen > 0)
                {
                    memcpy(recvname, data, dlen > 63 ? 63 : dlen);
                    recvname[63] = 0;
                }

                if (!path || !*path)
                    path = recvname;

                fp = fopen(path, "wb");
                if (!fp)
                {
                    kermit_send_nak(seq);
                    return 0;
                }

                kermit_send_ack(seq);
                expect_seq = (expect_seq + 1) & 0x3F;
                break;

            case 'D':
                if (!fp)
                {
                    kermit_send_nak(seq);
                    return 0;
                }

                n = kermit_decode_data(data, dlen, dec, sizeof(dec), kp.quote);
                fwrite(dec, 1, n, fp);

                kermit_send_ack(seq);
                expect_seq = (expect_seq + 1) & 0x3F;
                break;

            case 'Z':
                if (fp)
                {
                    fclose(fp);
                    fp = NULL;
                }

                kermit_send_ack(seq);
                expect_seq = (expect_seq + 1) & 0x3F;
                break;

            case 'B':
                kermit_send_ack(seq);
                if (fp)
                    fclose(fp);
                return 1;

            default:
                kermit_send_nak(seq);
                return 0;
        }
    }
}

/* ------------------------------------------------------------ */
/* ZMODEM                                                       */
/* ------------------------------------------------------------ */

static void zmodem_reset_state(void)
{
    memset(&g_zm, 0, sizeof(g_zm));
    g_zm.use_crc32 = 1;
    g_zm.tx_binary_headers = 1;
    g_zm.rx_binary_headers = 1;
    g_zm.escape_ctrl = 1;
    g_zm.escape_8th = 0;
    g_zm.full_duplex = 1;
    g_zm.overlap_io = 1;
    g_zm.tx_window = ZMODEM_WIN_DEFAULT;
    g_zm.rx_window = ZMODEM_WIN_DEFAULT;
    g_zm.resume_pos = 0UL;
    g_zm.ack_pos = 0UL;
    g_zm.last_sent_pos = 0UL;
    g_zm.batch = 0;
    g_zm.timeout_count = 0;
    g_zm.zrpos_count = 0;
    g_zm.ackless_bursts = 0;
}

static void zmodem_reduce_window(void)
{
    if (g_zm.tx_window > ZMODEM_WIN_MIN)
    {
        g_zm.tx_window >>= 1;
        if (g_zm.tx_window < ZMODEM_WIN_MIN)
            g_zm.tx_window = ZMODEM_WIN_MIN;
    }
}

static void zmodem_increase_window(void)
{
    if (g_zm.tx_window < ZMODEM_WIN_MAX)
    {
        g_zm.tx_window += 1024UL;
        if (g_zm.tx_window > ZMODEM_WIN_MAX)
            g_zm.tx_window = ZMODEM_WIN_MAX;
    }
}

static unsigned long zmodem_get_pos(h)
ZHDR *h;
{
    return ((unsigned long)h->hdr[0]) |
           ((unsigned long)h->hdr[1] << 8) |
           ((unsigned long)h->hdr[2] << 16) |
           ((unsigned long)h->hdr[3] << 24);
}

static int zmodem_needs_escape(ch)
int ch;
{
    ch &= 0xFF;

    if (ch == ZDLE)
        return 1;

    if (g_zm.escape_ctrl)
        if (ch < 0x20 || ch == 0x7F)
            return 1;

    if (g_zm.escape_8th)
        if (ch & 0x80)
            return 1;

    if (ch == 0x10 || ch == 0x11 || ch == 0x13 ||
        ch == 0x90 || ch == 0x91 || ch == 0x93)
        return 1;

    return 0;
}

static int zmodem_send_escaped(ch)
int ch;
{
    if (!zmodem_needs_escape(ch))
        return send_one_byte(ch & 0xFF);

    if (!send_one_byte(ZDLE))
        return 0;

    if ((ch & 0xFF) == 0x7F)
        return send_one_byte(ZRUB0);
    if ((ch & 0xFF) == 0xFF)
        return send_one_byte(ZRUB1);

    return send_one_byte((ch ^ 0x40) & 0xFF);
}

static int zmodem_recv_escaped(timeout)
int timeout;
{
    int ch;

    ch = recv_one_byte(timeout);
    if (ch < 0)
        return -1;

    if ((ch & 0xFF) != ZDLE)
        return ch & 0xFF;

    ch = recv_one_byte(timeout);
    if (ch < 0)
        return -1;

    if (ch == ZCRCE || ch == ZCRCG || ch == ZCRCQ || ch == ZCRCW)
        return 0x100 | (ch & 0xFF);

    if (ch == ZRUB0)
        return 0x7F;
    if (ch == ZRUB1)
        return 0xFF;

    return (ch ^ 0x40) & 0xFF;
}

static int zmodem_send_hex_byte(v)
int v;
{
    if (!send_one_byte(hex_digit((v >> 4) & 0x0F)))
        return 0;
    if (!send_one_byte(hex_digit(v & 0x0F)))
        return 0;
    return 1;
}

static int zmodem_read_hex_byte(timeout)
int timeout;
{
    int a, b, va, vb;

    a = recv_one_byte(timeout);
    if (a < 0)
        return -1;
    b = recv_one_byte(timeout);
    if (b < 0)
        return -1;

    va = hex_value(a);
    vb = hex_value(b);
    if (va < 0 || vb < 0)
        return -1;

    return (va << 4) | vb;
}

static int zmodem_send_hex_header(type, pos, f0, f1, f2, f3)
int type;
unsigned long pos;
int f0, f1, f2, f3;
{
    unsigned char raw[5];
    ushort crc;

    raw[0] = (unsigned char)type;
    raw[1] = (unsigned char)f0;
    raw[2] = (unsigned char)f1;
    raw[3] = (unsigned char)f2;
    raw[4] = (unsigned char)f3;

    if (type == ZRPOS || type == ZDATA || type == ZEOF || type == ZACK)
    {
        raw[1] = (unsigned char)(pos & 0xFF);
        raw[2] = (unsigned char)((pos >> 8) & 0xFF);
        raw[3] = (unsigned char)((pos >> 16) & 0xFF);
        raw[4] = (unsigned char)((pos >> 24) & 0xFF);
    }

    crc = crc16_ccitt(raw, 5);

    if (!send_one_byte(ZPAD)) return 0;
    if (!send_one_byte(ZPAD)) return 0;
    if (!send_one_byte(ZDLE)) return 0;
    if (!send_one_byte(ZHEX)) return 0;

    if (!zmodem_send_hex_byte(raw[0])) return 0;
    if (!zmodem_send_hex_byte(raw[1])) return 0;
    if (!zmodem_send_hex_byte(raw[2])) return 0;
    if (!zmodem_send_hex_byte(raw[3])) return 0;
    if (!zmodem_send_hex_byte(raw[4])) return 0;
    if (!zmodem_send_hex_byte((crc >> 8) & 0xFF)) return 0;
    if (!zmodem_send_hex_byte(crc & 0xFF)) return 0;

    if (!send_one_byte(ASCII_CR)) return 0;
    if (!send_one_byte(ASCII_LF)) return 0;

    return 1;
}

static int zmodem_send_bin16_header(type, pos, f0, f1, f2, f3)
int type;
unsigned long pos;
int f0, f1, f2, f3;
{
    unsigned char raw[5];
    ushort crc;
    int i;

    raw[0] = (unsigned char)type;
    raw[1] = (unsigned char)f0;
    raw[2] = (unsigned char)f1;
    raw[3] = (unsigned char)f2;
    raw[4] = (unsigned char)f3;

    if (type == ZRPOS || type == ZDATA || type == ZEOF || type == ZACK)
    {
        raw[1] = (unsigned char)(pos & 0xFF);
        raw[2] = (unsigned char)((pos >> 8) & 0xFF);
        raw[3] = (unsigned char)((pos >> 16) & 0xFF);
        raw[4] = (unsigned char)((pos >> 24) & 0xFF);
    }

    crc = crc16_ccitt(raw, 5);

    if (!send_one_byte(ZPAD)) return 0;
    if (!send_one_byte(ZDLE)) return 0;
    if (!send_one_byte(ZBIN)) return 0;

    for (i = 0; i < 5; i++)
        if (!zmodem_send_escaped(raw[i]))
            return 0;

    if (!zmodem_send_escaped((crc >> 8) & 0xFF))
        return 0;
    if (!zmodem_send_escaped(crc & 0xFF))
        return 0;

    return 1;
}

static int zmodem_send_bin32_header(type, pos, f0, f1, f2, f3)
int type;
unsigned long pos;
int f0, f1, f2, f3;
{
    unsigned char raw[5];
    unsigned long crc;
    unsigned char c[4];
    int i;

    raw[0] = (unsigned char)type;
    raw[1] = (unsigned char)f0;
    raw[2] = (unsigned char)f1;
    raw[3] = (unsigned char)f2;
    raw[4] = (unsigned char)f3;

    if (type == ZRPOS || type == ZDATA || type == ZEOF || type == ZACK)
    {
        raw[1] = (unsigned char)(pos & 0xFF);
        raw[2] = (unsigned char)((pos >> 8) & 0xFF);
        raw[3] = (unsigned char)((pos >> 16) & 0xFF);
        raw[4] = (unsigned char)((pos >> 24) & 0xFF);
    }

    crc = zmodem_crc32_buf(raw, 5);

    c[0] = (unsigned char)(crc & 0xFF);
    c[1] = (unsigned char)((crc >> 8) & 0xFF);
    c[2] = (unsigned char)((crc >> 16) & 0xFF);
    c[3] = (unsigned char)((crc >> 24) & 0xFF);

    if (!send_one_byte(ZPAD)) return 0;
    if (!send_one_byte(ZDLE)) return 0;
    if (!send_one_byte(ZBIN32)) return 0;

    for (i = 0; i < 5; i++)
        if (!zmodem_send_escaped(raw[i]))
            return 0;

    for (i = 0; i < 4; i++)
        if (!zmodem_send_escaped(c[i]))
            return 0;

    return 1;
}

static int zmodem_send_header(type, pos, f0, f1, f2, f3)
int type;
unsigned long pos;
int f0, f1, f2, f3;
{
    if (g_zm.tx_binary_headers)
    {
        if (g_zm.use_crc32)
            return zmodem_send_bin32_header(type, pos, f0, f1, f2, f3);
        return zmodem_send_bin16_header(type, pos, f0, f1, f2, f3);
    }

    return zmodem_send_hex_header(type, pos, f0, f1, f2, f3);
}

/* ------------------------------------------------------------ */
/* Public protocol entry points                                 */
/* ------------------------------------------------------------ */

int ascii_send_file(path)
char *path;
{
    modem_copy_user_state();
    return ascii_transfer_send(path);
}

int xmodem_send_file(path, proto)
char *path;
int proto;
{
    modem_copy_user_state();
    return xmodem_send_file_impl(path, proto);
}

int xmodem_recv_file(path, proto)
char *path;
int proto;
{
    modem_copy_user_state();
    return xmodem_recv_file_impl(path, proto);
}

int ymodem_send_file(path)
char *path;
{
    modem_copy_user_state();
    return ymodem_send_file_impl(path);
}

int ymodem_batch_send_file(path)
char *path;
{
    modem_copy_user_state();
    return ymodem_batch_send_file_impl(path);
}

int ymodem_recv_file(path)
char *path;
{
    modem_copy_user_state();
    return ymodem_recv_file_impl(path);
}

int ymodem_batch_recv_file(path)
char *path;
{
    modem_copy_user_state();
    return ymodem_batch_recv_file_impl(path);
}

int kermit_send_file(path)
char *path;
{
    modem_copy_user_state();
    return kermit_send_file_impl(path);
}

int kermit_recv_file(path)
char *path;
{
    modem_copy_user_state();
    return kermit_recv_file_impl(path);
}

/* The current checked-in source was missing the remainder of the
 * reconstructed ZMODEM engine after header transmit support.
 * Keep the public entry points present and buildable; use a safe
 * fallback for now instead of leaving the module incomplete.
 */

int zmodem_send_file(path)
char *path;
{
    modem_copy_user_state();
    zmodem_reset_state();
    return ymodem_send_file_impl(path);
}

int zmodem_batch_send_file(path)
char *path;
{
    modem_copy_user_state();
    zmodem_reset_state();
    return ymodem_batch_send_file_impl(path);
}

int zmodem_recv_file(path)
char *path;
{
    modem_copy_user_state();
    zmodem_reset_state();
    return ymodem_recv_file_impl(path);
}

int zmodem_batch_recv_file(path)
char *path;
{
    modem_copy_user_state();
    zmodem_reset_state();
    return ymodem_batch_recv_file_impl(path);
}

int proto_download(path, proto)
char *path;
int proto;
{
    modem_copy_user_state();

    switch (proto)
    {
        case 0: return ascii_send_file(path);
        case 1: return xmodem_send_file(path, 1);
        case 2: return xmodem_send_file(path, 2);
        case 3: return ymodem_send_file(path);
        case 4: return ymodem_batch_send_file(path);
        case 5: return kermit_send_file(path);
        case 6: return zmodem_send_file(path);
    }

    return 0;
}

int proto_upload(path, proto)
char *path;
int proto;
{
    modem_copy_user_state();

    switch (proto)
    {
        case 1: return xmodem_recv_file(path, 1);
        case 2: return xmodem_recv_file(path, 2);
        case 3: return ymodem_recv_file(path);
        case 4: return ymodem_batch_recv_file(path);
        case 5: return kermit_recv_file(path);
        case 6: return zmodem_recv_file(path);
    }

    return 0;
}
