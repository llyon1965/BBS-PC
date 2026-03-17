/* BBSMODM.C
 *
 * BBS-PC 4.20 modem / protocol support module
 *
 * Rebuilt with the current suggested changes applied:
 * - remote transport wired through BBSFOSSIL.C
 * - local mode still writes to stdout for bring-up/testing
 * - ASCII transfer with line/block modes and XON/XOFF pacing
 * - XMODEM / XMODEM-CRC
 * - YMODEM 1K
 * - YMODEM-Batch with filename header packet
 * - Kermit
 * - second-pass ZMODEM:
 *      * hex and binary headers
 *      * CRC16 / CRC32 paths
 *      * ZRQINIT / ZRINIT / ZFILE / ZDATA / ZEOF / ZFIN / ZRPOS
 *      * resume offsets / crash recovery via ZRPOS
 *      * windowed / streaming send behavior
 *      * batch send/receive entry points
 *
 * Notes:
 * - This is a substantial protocol-layer reconstruction, not a claim of
 *   byte-for-byte historical perfection.
 * - Real end-to-end remote operation depends on the FOSSIL driver and the
 *   rest of the runtime being wired consistently.
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

#define ZMODEM_TIMEOUT      600
#define ZMODEM_RETRIES      10
#define ZMODEM_DATABUF      1024
#define ZMODEM_WINBUF       8192

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
    int batch;
} ZMODEMSTATE;

static MODEMSTATE  g_modem;
static ZMODEMSTATE g_zm;

/* ------------------------------------------------------------ */
/* generic helpers                                              */
/* ------------------------------------------------------------ */

static void modem_zero_state(void)
{
    memset(&g_modem, 0, sizeof(g_modem));
    g_modem.local_mode = 1;
    g_modem.current_protocol = 0;
    g_modem.ascii_mode = ASCII_MODE_LINE;
    g_modem.xonxoff = 1;
    g_modem.baud = 2400;
}

static void modem_copy_user_state(void)
{
    g_modem.current_protocol = g_sess.user.protocol;
    g_modem.local_mode = g_sess.local_login ? 1 : 0;
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

    if (!g_modem.xonxoff)
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

    return modem_send_byte(ch) == 0;
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
        if (g_modem.ascii_mode == ASCII_MODE_BLOCK)
            return ascii_send_text_block_mode(path);
        return ascii_send_text_line_mode(path);
    }

    puts("Binary file will be sent as ASCII-Hex");

    if (g_modem.ascii_mode == ASCII_MODE_BLOCK)
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

static int xmodem_send_file(path, proto)
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

static int ymodem_send_file(path)
char *path;
{
    return ymodem_send_file_common(path, 0);
}

static int ymodem_batch_send_file(path)
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

static int xmodem_recv_file(path, proto)
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

static int ymodem_recv_file(path)
char *path;
{
    return ymodem_recv_file_common(path, 0);
}

static int ymodem_batch_recv_file(path)
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

static int kermit_send_file(path)
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

static int kermit_recv_file(path)
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
    g_zm.tx_window = 4096UL;
    g_zm.rx_window = 4096UL;
    g_zm.resume_pos = 0UL;
    g_zm.ack_pos = 0UL;
    g_zm.batch = 0;
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

static int zmodem_read_hex_header(h)
ZHDR *h;
{
    int ch;
    unsigned char raw[5];
    ushort gotcrc, calc;

    ch = zmodem_read_hex_byte(ZMODEM_TIMEOUT); if (ch < 0) return 0; raw[0] = (unsigned char)ch;
    ch = zmodem_read_hex_byte(ZMODEM_TIMEOUT); if (ch < 0) return 0; raw[1] = (unsigned char)ch;
    ch = zmodem_read_hex_byte(ZMODEM_TIMEOUT); if (ch < 0) return 0; raw[2] = (unsigned char)ch;
    ch = zmodem_read_hex_byte(ZMODEM_TIMEOUT); if (ch < 0) return 0; raw[3] = (unsigned char)ch;
    ch = zmodem_read_hex_byte(ZMODEM_TIMEOUT); if (ch < 0) return 0; raw[4] = (unsigned char)ch;

    ch = zmodem_read_hex_byte(ZMODEM_TIMEOUT); if (ch < 0) return 0;
    gotcrc = (ushort)(ch << 8);
    ch = zmodem_read_hex_byte(ZMODEM_TIMEOUT); if (ch < 0) return 0;
    gotcrc |= (ushort)ch;

    calc = crc16_ccitt(raw, 5);
    if (calc != gotcrc)
        return 0;

    h->type = raw[0];
    h->hdr[0] = raw[1];
    h->hdr[1] = raw[2];
    h->hdr[2] = raw[3];
    h->hdr[3] = raw[4];
    h->pos = zmodem_get_pos(h);
    return 1;
}

static int zmodem_read_bin16_header(h)
ZHDR *h;
{
    int ch, i;
    unsigned char raw[5];
    ushort gotcrc, calc;

    for (i = 0; i < 5; i++)
    {
        ch = zmodem_recv_escaped(ZMODEM_TIMEOUT);
        if (ch < 0)
            return 0;
        raw[i] = (unsigned char)ch;
    }

    ch = zmodem_recv_escaped(ZMODEM_TIMEOUT);
    if (ch < 0)
        return 0;
    gotcrc = (ushort)((ch & 0xFF) << 8);

    ch = zmodem_recv_escaped(ZMODEM_TIMEOUT);
    if (ch < 0)
        return 0;
    gotcrc |= (ushort)(ch & 0xFF);

    calc = crc16_ccitt(raw, 5);
    if (calc != gotcrc)
        return 0;

    h->type = raw[0];
    h->hdr[0] = raw[1];
    h->hdr[1] = raw[2];
    h->hdr[2] = raw[3];
    h->hdr[3] = raw[4];
    h->pos = zmodem_get_pos(h);
    return 1;
}

static int zmodem_read_bin32_header(h)
ZHDR *h;
{
    int ch, i;
    unsigned char raw[5];
    unsigned char c[4];
    unsigned long gotcrc, calc;

    for (i = 0; i < 5; i++)
    {
        ch = zmodem_recv_escaped(ZMODEM_TIMEOUT);
        if (ch < 0)
            return 0;
        raw[i] = (unsigned char)ch;
    }

    for (i = 0; i < 4; i++)
    {
        ch = zmodem_recv_escaped(ZMODEM_TIMEOUT);
        if (ch < 0)
            return 0;
        c[i] = (unsigned char)ch;
    }

    gotcrc = ((unsigned long)c[0]) |
             ((unsigned long)c[1] << 8) |
             ((unsigned long)c[2] << 16) |
             ((unsigned long)c[3] << 24);

    calc = zmodem_crc32_buf(raw, 5);
    if (calc != gotcrc)
        return 0;

    h->type = raw[0];
    h->hdr[0] = raw[1];
    h->hdr[1] = raw[2];
    h->hdr[2] = raw[3];
    h->hdr[3] = raw[4];
    h->pos = zmodem_get_pos(h);
    return 1;
}

static int zmodem_read_header(h)
ZHDR *h;
{
    int ch;

    for (;;)
    {
        ch = recv_one_byte(ZMODEM_TIMEOUT);
        if (ch < 0)
            return 0;
        if ((ch & 0xFF) == ZPAD)
            break;
    }

    do
    {
        ch = recv_one_byte(ZMODEM_TIMEOUT);
        if (ch < 0)
            return 0;
    } while ((ch & 0xFF) == ZPAD);

    if ((ch & 0xFF) != ZDLE)
        return 0;

    ch = recv_one_byte(ZMODEM_TIMEOUT);
    if (ch < 0)
        return 0;

    if ((ch & 0xFF) == ZHEX)
        return zmodem_read_hex_header(h);
    if ((ch & 0xFF) == ZBIN)
        return zmodem_read_bin16_header(h);
    if ((ch & 0xFF) == ZBIN32)
        return zmodem_read_bin32_header(h);

    return 0;
}

static int zmodem_send_data_subpacket(buf, len, frameend)
unsigned char *buf;
int len;
int frameend;
{
    int i;
    unsigned char crcbuf[ZMODEM_DATABUF + 1];
    ushort crc16;
    unsigned long crc32;
    unsigned char tail[4];

    for (i = 0; i < len; i++)
        if (!zmodem_send_escaped(buf[i]))
            return 0;

    if (!send_one_byte(ZDLE))
        return 0;
    if (!send_one_byte(frameend))
        return 0;

    memcpy(crcbuf, buf, len);
    crcbuf[len] = (unsigned char)frameend;

    if (g_zm.use_crc32)
    {
        crc32 = zmodem_crc32_buf(crcbuf, len + 1);
        tail[0] = (unsigned char)(crc32 & 0xFF);
        tail[1] = (unsigned char)((crc32 >> 8) & 0xFF);
        tail[2] = (unsigned char)((crc32 >> 16) & 0xFF);
        tail[3] = (unsigned char)((crc32 >> 24) & 0xFF);

        for (i = 0; i < 4; i++)
            if (!zmodem_send_escaped(tail[i]))
                return 0;
    }
    else
    {
        crc16 = crc16_ccitt(crcbuf, len + 1);
        if (!zmodem_send_escaped((crc16 >> 8) & 0xFF))
            return 0;
        if (!zmodem_send_escaped(crc16 & 0xFF))
            return 0;
    }

    return 1;
}

static int zmodem_recv_data_subpacket(buf, maxlen, gotlen, frameend)
unsigned char *buf;
int maxlen;
int *gotlen;
int *frameend;
{
    int ch;
    int n = 0;
    int i;
    unsigned char crcbuf[ZMODEM_DATABUF + 1];
    unsigned char tail[4];
    ushort got16, calc16;
    unsigned long got32, calc32;

    for (;;)
    {
        ch = zmodem_recv_escaped(ZMODEM_TIMEOUT);
        if (ch < 0)
            return 0;

        if (ch & 0x100)
        {
            *frameend = ch & 0xFF;
            break;
        }

        if (n >= maxlen)
            return 0;

        buf[n++] = (unsigned char)ch;
    }

    memcpy(crcbuf, buf, n);
    crcbuf[n] = (unsigned char)(*frameend);

    if (g_zm.use_crc32)
    {
        for (i = 0; i < 4; i++)
        {
            ch = zmodem_recv_escaped(ZMODEM_TIMEOUT);
            if (ch < 0)
                return 0;
            tail[i] = (unsigned char)ch;
        }

        got32 = ((unsigned long)tail[0]) |
                ((unsigned long)tail[1] << 8) |
                ((unsigned long)tail[2] << 16) |
                ((unsigned long)tail[3] << 24);

        calc32 = zmodem_crc32_buf(crcbuf, n + 1);
        if (got32 != calc32)
            return 0;
    }
    else
    {
        ch = zmodem_recv_escaped(ZMODEM_TIMEOUT);
        if (ch < 0)
            return 0;
        tail[0] = (unsigned char)ch;

        ch = zmodem_recv_escaped(ZMODEM_TIMEOUT);
        if (ch < 0)
            return 0;
        tail[1] = (unsigned char)ch;

        got16 = (ushort)(((unsigned)tail[0] << 8) | tail[1]);
        calc16 = crc16_ccitt(crcbuf, n + 1);
        if (got16 != calc16)
            return 0;
    }

    *gotlen = n;
    return 1;
}

static int zmodem_send_zrinit_caps(void)
{
    int f0 = ZF0_CANFDX | ZF0_CANOVIO | ZF0_CANFC32;

    if (g_zm.escape_ctrl)
        f0 |= ZF0_ESCCTL;
    if (g_zm.escape_8th)
        f0 |= ZF0_ESC8;

    return zmodem_send_header(ZRINIT, 0UL, f0, 0, 0, 0);
}

static int zmodem_send_zrqinit(void)
{
    return zmodem_send_header(ZRQINIT, 0UL, 0, 0, 0, 0);
}

static int zmodem_send_zfin(void)
{
    return zmodem_send_header(ZFIN, 0UL, 0, 0, 0, 0);
}

static int zmodem_send_zfile(path, batch_mode)
char *path;
int batch_mode;
{
    unsigned char data[256];
    char fname[128];
    char info[96];
    long len;
    int n = 0;
    int conv = ZCBIN;
    int mgmt = ZCRESUM;

    basename_only(path, fname, sizeof(fname));
    len = file_length_local(path);
    sprintf(info, "%ld 0 0 0 %d", len < 0L ? 0L : len, batch_mode ? 1 : 0);

    memset(data, 0, sizeof(data));
    strcpy((char *)data + n, fname);
    n += strlen(fname) + 1;
    strcpy((char *)data + n, info);
    n += strlen(info) + 1;

    if (!zmodem_send_header(ZFILE, 0UL, conv, mgmt, 0, 0))
        return 0;

    return zmodem_send_data_subpacket(data, n, ZCRCW);
}

static int zmodem_wait_receiver(h)
ZHDR *h;
{
    int tries = 0;

    while (tries < ZMODEM_RETRIES)
    {
        if (!zmodem_read_header(h))
        {
            tries++;
            continue;
        }

        if (h->type == ZRINIT || h->type == ZRPOS || h->type == ZSKIP ||
            h->type == ZACK || h->type == ZFIN)
            return 1;
    }

    return 0;
}

static int zmodem_stream_file(fp, start_pos)
FILE *fp;
unsigned long start_pos;
{
    unsigned char buf[ZMODEM_DATABUF];
    ZHDR h;
    int n;
    unsigned long pos = start_pos;
    unsigned long last_ack = start_pos;
    unsigned long unacked = 0UL;
    int frame;

    fseek(fp, (long)start_pos, SEEK_SET);

    if (!zmodem_send_header(ZDATA, pos, 0, 0, 0, 0))
        return 0;

    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
    {
        if (g_zm.tx_window && unacked >= g_zm.tx_window)
            frame = ZCRCW;
        else if (feof(fp))
            frame = ZCRCW;
        else
            frame = g_zm.full_duplex ? ZCRCG : ZCRCQ;

        if (!zmodem_send_data_subpacket(buf, n, frame))
            return 0;

        pos += (unsigned long)n;
        unacked = pos - last_ack;

        if (frame == ZCRCQ || frame == ZCRCW)
        {
            if (!zmodem_wait_receiver(&h))
                return 0;

            if (h.type == ZACK)
            {
                last_ack = h.pos;
                unacked = pos - last_ack;
            }
            else if (h.type == ZRPOS)
                return (int)h.pos + 1;
            else if (h.type == ZSKIP)
                return -2;
        }
    }

    if (!zmodem_send_header(ZEOF, pos, 0, 0, 0, 0))
        return 0;

    if (!zmodem_wait_receiver(&h))
        return 0;

    if (h.type == ZRPOS)
        return (int)h.pos + 1;
    if (h.type == ZRINIT)
        return -1;
    if (h.type == ZSKIP)
        return -2;

    return -1;
}

static int zmodem_send_one_file(path, batch_mode)
char *path;
int batch_mode;
{
    FILE *fp;
    ZHDR h;
    int rc;
    unsigned long start_pos = 0UL;

    fp = fopen(path, "rb");
    if (!fp)
        return 0;

    if (!zmodem_send_zfile(path, batch_mode))
    {
        fclose(fp);
        return 0;
    }

    if (!zmodem_wait_receiver(&h))
    {
        fclose(fp);
        return 0;
    }

    if (h.type == ZSKIP)
    {
        fclose(fp);
        return 1;
    }

    if (h.type == ZRPOS)
        start_pos = h.pos;
    else if (h.type == ZRINIT)
        start_pos = 0UL;
    else
    {
        fclose(fp);
        return 0;
    }

    for (;;)
    {
        rc = zmodem_stream_file(fp, start_pos);

        if (rc == -1)
            break;
        if (rc == -2)
        {
            fclose(fp);
            return 1;
        }
        if (rc <= 0)
        {
            fclose(fp);
            return 0;
        }

        start_pos = (unsigned long)(rc - 1);
    }

    fclose(fp);
    return 1;
}

int zmodem_send_file(path)
char *path;
{
    ZHDR h;

    zmodem_reset_state();

    if (!zmodem_send_zrqinit())
        return 0;

    if (!zmodem_wait_receiver(&h))
        return 0;
    if (h.type != ZRINIT)
        return 0;

    return zmodem_send_one_file(path, 0);
}

int zmodem_batch_send_file(path)
char *path;
{
    ZHDR h;

    zmodem_reset_state();
    g_zm.batch = 1;

    if (!zmodem_send_zrqinit())
        return 0;

    if (!zmodem_wait_receiver(&h))
        return 0;
    if (h.type != ZRINIT)
        return 0;

    if (!zmodem_send_one_file(path, 1))
        return 0;

    if (!zmodem_send_zfin())
        return 0;

    return 1;
}

int zmodem_recv_file(path)
char *path;
{
    FILE *fp = NULL;
    unsigned char buf[ZMODEM_DATABUF];
    int n, fend;
    unsigned long pos = 0UL;
    ZHDR h;

    zmodem_reset_state();

    if (!zmodem_wait_receiver(&h))
        return 0;
    if (h.type != ZRQINIT)
        return 0;

    if (!zmodem_send_zrinit_caps())
        return 0;

    for (;;)
    {
        if (!zmodem_read_header(&h))
            return 0;

        if (h.type == ZFILE)
        {
            if (!zmodem_recv_data_subpacket(buf, sizeof(buf), &n, &fend))
                return 0;

            if (!path || !*path)
                path = (char *)buf;

            fp = fopen(path, "r+b");
            if (!fp)
                fp = fopen(path, "wb");
            if (!fp)
                return 0;

            g_zm.resume_pos = file_length_local(path);
            if ((long)g_zm.resume_pos < 0L)
                g_zm.resume_pos = 0UL;

            pos = g_zm.resume_pos;
            fseek(fp, (long)pos, SEEK_SET);

            if (!zmodem_send_header(ZRPOS, pos, 0, 0, 0, 0))
            {
                fclose(fp);
                return 0;
            }
        }
        else if (h.type == ZDATA)
        {
            pos = h.pos;
            if (!fp)
                return 0;

            fseek(fp, (long)pos, SEEK_SET);

            for (;;)
            {
                if (!zmodem_recv_data_subpacket(buf, sizeof(buf), &n, &fend))
                {
                    fclose(fp);
                    return 0;
                }

                fwrite(buf, 1, n, fp);
                pos += (unsigned long)n;

                if (fend == ZCRCQ)
                {
                    if (!zmodem_send_header(ZACK, pos, 0, 0, 0, 0))
                    {
                        fclose(fp);
                        return 0;
                    }
                }
                else if (fend == ZCRCW || fend == ZCRCE)
                {
                    if (!zmodem_send_header(ZACK, pos, 0, 0, 0, 0))
                    {
                        fclose(fp);
                        return 0;
                    }
                    break;
                }
            }
        }
        else if (h.type == ZEOF)
        {
            if (fp)
            {
                fclose(fp);
                fp = NULL;
            }

            if (!zmodem_send_zrinit_caps())
                return 0;
        }
        else if (h.type == ZFIN)
        {
            if (!zmodem_send_zfin())
                return 0;
            return 1;
        }
        else if (h.type == ZSKIP)
        {
            if (fp)
            {
                fclose(fp);
                fp = NULL;
            }

            if (!zmodem_send_zrinit_caps())
                return 0;
        }
        else
        {
            if (!zmodem_send_header(ZRPOS, pos, 0, 0, 0, 0))
            {
                if (fp) fclose(fp);
                return 0;
            }
        }
    }
}

int zmodem_batch_recv_file(path)
char *path;
{
    zmodem_reset_state();
    g_zm.batch = 1;
    return zmodem_recv_file(path);
}

/* ------------------------------------------------------------ */
/* modem lifecycle                                              */
/* ------------------------------------------------------------ */

void modem_init(void)
{
    modem_zero_state();
    modem_copy_user_state();
    g_modem.initialized = 1;

    if (!g_sess.local_login)
        fossil_driver_installed();
}

void modem_reset(void)
{
    if (!g_sess.local_login)
        fossil_deinit();

    modem_zero_state();
}

void modem_begin_session(void)
{
    if (!g_modem.initialized)
        modem_init();

    modem_copy_user_state();

    if (g_sess.local_login)
    {
        g_modem.online = 1;
        return;
    }

    fossil_start_session();
    g_modem.online = fossil_carrier() ? 1 : 0;
}

void modem_end_session(void)
{
    if (!g_sess.local_login)
        fossil_end_session();

    g_modem.online = 0;
}

int modem_is_online(void)
{
    if (g_sess.local_login)
        return 1;

    return fossil_carrier();
}

/* ------------------------------------------------------------ */
/* protocol handling                                            */
/* ------------------------------------------------------------ */

char *modem_protocol_name(proto)
int proto;
{
    switch (proto)
    {
        case 0:  return "ASCII";
        case 1:  return "XMODEM";
        case 2:  return "XMODEM-CRC";
        case 3:  return "YMODEM";
        case 4:  return "YMODEM-Batch";
        case 5:  return "Kermit";
        case 6:  return "ZMODEM";
    }

    return "Unknown";
}

void modem_set_protocol(proto)
int proto;
{
    if (proto < 0)
        proto = 0;

    g_modem.current_protocol = proto;
    g_sess.user.protocol = (byte)proto;
    user_save_current();
}

int modem_get_protocol(void)
{
    return g_modem.current_protocol;
}

void modem_show_protocols(void)
{
    puts("");
    puts("Protocols:");
    puts("0: ASCII");
    puts("1: XMODEM");
    puts("2: XMODEM-CRC");
    puts("3: YMODEM");
    puts("4: YMODEM-Batch");
    puts("5: Kermit");
    puts("6: ZMODEM");
    puts("");
}

void modem_select_protocol(void)
{
    char line[16];
    int proto;

    modem_show_protocols();
    data_prompt_line("Protocol? ", line, sizeof(line));
    if (!line[0])
        return;

    proto = atoi(line);
    modem_set_protocol(proto);

    printf("Protocol set to %s\n", modem_protocol_name(g_modem.current_protocol));
}

void modem_show_status(void)
{
    puts("");
    printf("Modem initialized  %s\n", g_modem.initialized ? "Yes" : "No");
    printf("Modem online       %s\n", modem_is_online() ? "Yes" : "No");
    printf("Mode               %s\n", g_modem.local_mode ? "Local" : "Remote");
    printf("Baud               %d\n", g_modem.baud);
    printf("Protocol           %s\n", modem_protocol_name(g_modem.current_protocol));
    printf("ASCII transfer     %s\n",
        g_modem.ascii_mode == ASCII_MODE_BLOCK ? "Block" : "Line");
    printf("XON/XOFF pacing    %s\n", g_modem.xonxoff ? "Yes" : "No");

    if (!g_sess.local_login)
        fossil_show_status();

    puts("");
}

void modem_switch_for_protocol(label)
char *label;
{
    if (protocol_is_binary(g_modem.current_protocol))
        term_switch_8n1(label ? label : "transfer");
}

void modem_restore_after_protocol(void)
{
    term_restore_mode();
}

void modem_show_transfer_banner(direction, name)
char *direction;
char *name;
{
    if (!direction)
        direction = "Transfer";
    if (!name)
        name = "";

    printf("Starting %s %s\n", direction, name);
}

void modem_show_transfer_result(ok, direction)
int ok;
char *direction;
{
    if (!direction)
        direction = "Transfer";

    printf("%s %s\n", direction, ok ? "successful" : "unsuccessful");
}

int modem_can_send_text(void)
{
    return 1;
}

int modem_can_send_binary(void)
{
    if (!modem_is_online() && !g_sess.local_login)
        return 0;
    return 1;
}

int modem_can_transfer_protocol(proto)
int proto;
{
    if (proto < 0 || proto > 6)
        return 0;

    if (proto == 0)
        return modem_can_send_text();

    return modem_can_send_binary();
}

/* ------------------------------------------------------------ */
/* transfers                                                    */
/* ------------------------------------------------------------ */

int modem_upload_file(path, proto)
char *path;
int proto;
{
    int ok = 0;

    if (!path || !*path)
    {
        puts("No upload filename specified");
        return 0;
    }

    if (!modem_can_transfer_protocol(proto))
    {
        puts("Unknown or unavailable transfer protocol");
        return 0;
    }

    modem_switch_for_protocol(path);
    modem_show_transfer_banner("upload", path);

    switch (proto)
    {
        case 0:
            ok = ascii_transfer_send(path);
            break;

        case 1:
        case 2:
            ok = xmodem_recv_file(path, proto);
            break;

        case 3:
            ok = ymodem_recv_file(path);
            break;

        case 4:
            ok = ymodem_batch_recv_file(path);
            break;

        case 5:
            ok = kermit_recv_file(path);
            break;

        case 6:
            ok = zmodem_recv_file(path);
            break;
    }

    modem_restore_after_protocol();
    modem_show_transfer_result(ok, "Upload");
    return ok;
}

int modem_download_file(path, proto)
char *path;
int proto;
{
    long len;
    int ok = 0;

    if (!path || !*path)
    {
        puts("No download filename specified");
        return 0;
    }

    if (!modem_can_transfer_protocol(proto))
    {
        puts("Unknown or unavailable transfer protocol");
        return 0;
    }

    if (proto != 0 && !file_exists_local(path))
    {
        puts("Requested file not found");
        return 0;
    }

    if (proto != 0)
    {
        len = file_length_local(path);
        if (len < 0L)
        {
            puts("Can't read requested file");
            return 0;
        }
    }

    modem_switch_for_protocol(path);
    modem_show_transfer_banner("download", path);

    switch (proto)
    {
        case 0:
            ok = ascii_transfer_send(path);
            break;

        case 1:
        case 2:
            ok = xmodem_send_file(path, proto);
            break;

        case 3:
            ok = ymodem_send_file(path);
            break;

        case 4:
            ok = ymodem_batch_send_file(path);
            break;

        case 5:
            ok = kermit_send_file(path);
            break;

        case 6:
            ok = zmodem_send_file(path);
            break;
    }

    modem_restore_after_protocol();
    modem_show_transfer_result(ok, "Download");
    return ok;
}

int modem_upload_text(path)
char *path;
{
    int ok;

    if (!path || !*path)
    {
        puts("No upload filename specified");
        return 0;
    }

    modem_show_transfer_banner("ASCII upload", path);
    ok = ascii_transfer_send(path);
    modem_show_transfer_result(ok, "Upload");
    return ok;
}

int modem_download_text(path)
char *path;
{
    int ok;

    if (!path || !*path)
    {
        puts("No download filename specified");
        return 0;
    }

    modem_show_transfer_banner("ASCII download", path);
    ok = ascii_transfer_send(path);
    modem_show_transfer_result(ok, "Download");
    return ok;
}

void modem_direct_upload(void)
{
    char line[MAX_PATHNAME];

    if (!g_sess.local_login && !sysop_password_prompt())
        return;

    data_prompt_line("Upload filename: ", line, sizeof(line));
    if (!line[0])
        return;

    (void)modem_upload_file(line, g_modem.current_protocol);
}

void modem_direct_download(void)
{
    char line[MAX_PATHNAME];

    if (!g_sess.local_login && !sysop_password_prompt())
        return;

    data_prompt_line("Download filename: ", line, sizeof(line));
    if (!line[0])
        return;

    (void)modem_download_file(line, g_modem.current_protocol);
}

/* ------------------------------------------------------------ */
/* dialing / carrier                                            */
/* ------------------------------------------------------------ */

int modem_dial(number)
char *number;
{
    if (!number || !*number)
        return 0;

    printf("Dialing %s\n", number);
    puts("Dial/connect not implemented yet");
    return 0;
}

int modem_hangup(void)
{
    if (!g_sess.local_login)
        fossil_drop_dtr();

    puts("Hangup requested");
    return 1;
}

int modem_answer(void)
{
    puts("Answer not implemented yet");
    return 0;
}

void do_modem_defaults(void)
{
    char line[16];

    if (!sysop_password_prompt())
        return;

    puts("");
    puts("Modem defaults");
    puts("--------------");
    modem_show_status();

    modem_select_protocol();

    data_prompt_line("ASCII mode (L=line, B=block)? ", line, sizeof(line));
    if (line[0] == 'B' || line[0] == 'b')
        g_modem.ascii_mode = ASCII_MODE_BLOCK;
    else if (line[0] == 'L' || line[0] == 'l')
        g_modem.ascii_mode = ASCII_MODE_LINE;

    g_modem.xonxoff = data_yesno("Use XON/XOFF pacing (Y/N)? ",
        g_modem.xonxoff ? 1 : 0) ? 1 : 0;

    bbs_pause();
}

/* ------------------------------------------------------------ */
/* transport hooks                                              */
/* ------------------------------------------------------------ */

int modem_send_block(buf, len)
void *buf;
unsigned len;
{
    unsigned char *p;
    unsigned i;

    p = (unsigned char *)buf;
    for (i = 0; i < len; i++)
        if (modem_send_byte(p[i]) != 0)
            return -1;

    return 0;
}

int modem_recv_block(buf, len)
void *buf;
unsigned len;
{
    unsigned char *p;
    unsigned i;
    int ch;

    p = (unsigned char *)buf;
    for (i = 0; i < len; i++)
    {
        ch = modem_recv_byte(ASCII_PACE_TIMEOUT);
        if (ch < 0)
            return -1;
        p[i] = (unsigned char)ch;
    }

    return 0;
}

int modem_send_byte(ch)
int ch;
{
    if (g_sess.local_login)
    {
        putchar(ch);
        fflush(stdout);
        return 0;
    }

    return fossil_send_char(ch);
}

int modem_recv_byte(timeout_ticks)
int timeout_ticks;
{
    if (g_sess.local_login)
        return -1;

    return fossil_recv_char(timeout_ticks);
}