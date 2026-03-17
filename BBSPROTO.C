/* BBSPROTO.C
 *
 * BBS-PC 4.20 protocol dispatch / selection module
 *
 * Rebuilt with current suggested changes applied:
 * - protocol metadata centralized here
 * - single-file and batch dispatch wired to BBSMODM.C
 * - ZMODEM batch entry points exposed
 * - protocol recommendation logic improved
 * - text/binary capability checks aligned with current modem layer
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "bbsdata.h"
#include "bbsfunc.h"

typedef struct {
    int   id;
    char *name;
    char *short_name;
    int   is_binary;
    int   is_batch;
} PROTOINFO;

static PROTOINFO g_proto_table[] = {
    { 0, "ASCII",        "ASC",  0, 0 },
    { 1, "XMODEM",       "XMD",  1, 0 },
    { 2, "XMODEM-CRC",   "XCRC", 1, 0 },
    { 3, "YMODEM",       "YMD",  1, 0 },
    { 4, "YMODEM-Batch", "YB",   1, 1 },
    { 5, "Kermit",       "KER",  1, 0 },
    { 6, "ZMODEM",       "ZMD",  1, 1 }
};

#define PROTO_COUNT (sizeof(g_proto_table) / sizeof(g_proto_table[0]))

/* ------------------------------------------------------------ */
/* local helpers                                                */
/* ------------------------------------------------------------ */

static PROTOINFO *proto_lookup(id)
int id;
{
    int i;

    for (i = 0; i < (int)PROTO_COUNT; i++)
        if (g_proto_table[i].id == id)
            return &g_proto_table[i];

    return (PROTOINFO *)0;
}

static int proto_ext_matches(ext, lit)
char *ext;
char *lit;
{
    while (*ext && *lit)
    {
        if (toupper((unsigned char)*ext) != toupper((unsigned char)*lit))
            return 0;
        ext++;
        lit++;
    }

    return (*ext == 0 && *lit == 0);
}

static char *proto_file_ext(fname)
char *fname;
{
    char *p;

    p = strrchr(fname, '.');
    if (!p)
        return (char *)0;

    return p + 1;
}

static int proto_probably_text_file(fname)
char *fname;
{
    char *ext;

    ext = proto_file_ext(fname);
    if (!ext)
        return 0;

    if (proto_ext_matches(ext, "ASC")) return 1;
    if (proto_ext_matches(ext, "TXT")) return 1;
    if (proto_ext_matches(ext, "DOC")) return 1;
    if (proto_ext_matches(ext, "DIR")) return 1;
    if (proto_ext_matches(ext, "PAS")) return 1;
    if (proto_ext_matches(ext, "ASM")) return 1;
    if (proto_ext_matches(ext, "MAC")) return 1;
    if (proto_ext_matches(ext, "BAT")) return 1;
    if (proto_ext_matches(ext, "PRN")) return 1;
    if (proto_ext_matches(ext, "LST")) return 1;
    if (proto_ext_matches(ext, "HEX")) return 1;
    if (proto_ext_matches(ext, "ME"))  return 1;
    if (proto_ext_matches(ext, "C"))   return 1;
    if (proto_ext_matches(ext, "H"))   return 1;

    return 0;
}

/* ------------------------------------------------------------ */
/* metadata                                                     */
/* ------------------------------------------------------------ */

char *proto_name(id)
int id;
{
    PROTOINFO *p;

    p = proto_lookup(id);
    if (!p)
        return "Unknown";

    return p->name;
}

char *proto_short_name(id)
int id;
{
    PROTOINFO *p;

    p = proto_lookup(id);
    if (!p)
        return "UNK";

    return p->short_name;
}

int proto_is_binary(id)
int id;
{
    PROTOINFO *p;

    p = proto_lookup(id);
    if (!p)
        return 0;

    return p->is_binary;
}

int proto_is_batch(id)
int id;
{
    PROTOINFO *p;

    p = proto_lookup(id);
    if (!p)
        return 0;

    return p->is_batch;
}

int proto_valid(id)
int id;
{
    return proto_lookup(id) != (PROTOINFO *)0;
}

/* ------------------------------------------------------------ */
/* display / selection                                          */
/* ------------------------------------------------------------ */

void proto_show_list(void)
{
    int i;

    puts("");
    puts("Protocols:");
    puts("----------");

    for (i = 0; i < (int)PROTO_COUNT; i++)
    {
        printf("%d: %-14s  %s%s\n",
            g_proto_table[i].id,
            g_proto_table[i].name,
            g_proto_table[i].is_binary ? "Binary" : "Text",
            g_proto_table[i].is_batch ? ", Batch" : "");
    }

    puts("");
}

void proto_show_current(void)
{
    int id;

    id = g_sess.user.protocol;

    printf("Current protocol: %s\n", proto_name(id));
}

void proto_select_current(void)
{
    char line[16];
    int id;

    proto_show_list();
    data_prompt_line("Protocol? ", line, sizeof(line));
    if (!line[0])
        return;

    id = atoi(line);
    if (!proto_valid(id))
    {
        puts("Unknown protocol");
        return;
    }

    modem_set_protocol(id);
    printf("Protocol set to %s\n", proto_name(id));
}

/* ------------------------------------------------------------ */
/* recommendation / capability                                  */
/* ------------------------------------------------------------ */

int proto_recommended_for_file(fname, is_binary)
char *fname;
int is_binary;
{
    if (!is_binary)
    {
        if (fname && proto_probably_text_file(fname))
            return 0;

        return 0;
    }

    if (fname)
    {
        char *ext = proto_file_ext(fname);

        if (ext)
        {
            if (proto_ext_matches(ext, "ARC")) return 6;
            if (proto_ext_matches(ext, "ZIP")) return 6;
            if (proto_ext_matches(ext, "LZH")) return 6;
            if (proto_ext_matches(ext, "EXE")) return 6;
            if (proto_ext_matches(ext, "COM")) return 6;
            if (proto_ext_matches(ext, "OBJ")) return 6;
            if (proto_ext_matches(ext, "LIB")) return 6;
        }
    }

    return 2;
}

int proto_can_send_file(id, is_binary)
int id;
int is_binary;
{
    if (!proto_valid(id))
        return 0;

    if (!is_binary)
        return 1;

    return proto_is_binary(id);
}

/* ------------------------------------------------------------ */
/* direct wrappers to modem engines                             */
/* ------------------------------------------------------------ */

int proto_text_send(path)
char *path;
{
    return modem_download_text(path);
}

int proto_text_recv(path)
char *path;
{
    return modem_upload_text(path);
}

int proto_xmodem_send(path)
char *path;
{
    return modem_download_file(path, 1);
}

int proto_xmodem_recv(path)
char *path;
{
    return modem_upload_file(path, 1);
}

int proto_xmodemcrc_send(path)
char *path;
{
    return modem_download_file(path, 2);
}

int proto_xmodemcrc_recv(path)
char *path;
{
    return modem_upload_file(path, 2);
}

int proto_ymodem_send(path)
char *path;
{
    return modem_download_file(path, 3);
}

int proto_ymodem_recv(path)
char *path;
{
    return modem_upload_file(path, 3);
}

int proto_ymodem_batch_send(path)
char *path;
{
    return modem_download_file(path, 4);
}

int proto_ymodem_batch_recv(path)
char *path;
{
    return modem_upload_file(path, 4);
}

int proto_kermit_send(path)
char *path;
{
    return modem_download_file(path, 5);
}

int proto_kermit_recv(path)
char *path;
{
    return modem_upload_file(path, 5);
}

int proto_zmodem_send(path)
char *path;
{
    return modem_download_file(path, 6);
}

int proto_zmodem_recv(path)
char *path;
{
    return modem_upload_file(path, 6);
}

int proto_zmodem_batch_send(path)
char *path;
{
    return zmodem_batch_send_file(path);
}

int proto_zmodem_batch_recv(path)
char *path;
{
    return zmodem_batch_recv_file(path);
}

/* ------------------------------------------------------------ */
/* generic single-file dispatch                                 */
/* ------------------------------------------------------------ */

int proto_upload(path, id)
char *path;
int id;
{
    if (!proto_valid(id))
    {
        puts("Unknown protocol");
        return 0;
    }

    switch (id)
    {
        case 0: return proto_text_recv(path);
        case 1: return proto_xmodem_recv(path);
        case 2: return proto_xmodemcrc_recv(path);
        case 3: return proto_ymodem_recv(path);
        case 4: return proto_ymodem_batch_recv(path);
        case 5: return proto_kermit_recv(path);
        case 6: return proto_zmodem_recv(path);
    }

    return 0;
}

int proto_download(path, id)
char *path;
int id;
{
    if (!proto_valid(id))
    {
        puts("Unknown protocol");
        return 0;
    }

    switch (id)
    {
        case 0: return proto_text_send(path);
        case 1: return proto_xmodem_send(path);
        case 2: return proto_xmodemcrc_send(path);
        case 3: return proto_ymodem_send(path);
        case 4: return proto_ymodem_batch_send(path);
        case 5: return proto_kermit_send(path);
        case 6: return proto_zmodem_send(path);
    }

    return 0;
}

/* ------------------------------------------------------------ */
/* batch dispatch                                               */
/* ------------------------------------------------------------ */

static int proto_process_listfile(listfile, is_download, id)
char *listfile;
int is_download;
int id;
{
    FILE *fp;
    char line[MAX_PATHNAME];
    int ok_all = 1;
    int ok;

    fp = fopen(listfile, "rt");
    if (!fp)
    {
        puts("Can't open list file");
        return 0;
    }

    while (fgets(line, sizeof(line), fp))
    {
        data_trim_crlf(line);
        if (!line[0])
            continue;

        if (is_download)
            ok = proto_download(line, id);
        else
            ok = proto_upload(line, id);

        if (!ok)
            ok_all = 0;

        if (!proto_is_batch(id))
            break;
    }

    fclose(fp);
    return ok_all;
}

int proto_batch_upload(listfile, id)
char *listfile;
int id;
{
    if (!proto_valid(id))
    {
        puts("Unknown protocol");
        return 0;
    }

    if (!proto_is_batch(id))
    {
        puts("Protocol is not batch-capable");
        return 0;
    }

    if (id == 6)
        return proto_zmodem_batch_recv(listfile);

    return proto_process_listfile(listfile, 0, id);
}

int proto_batch_download(listfile, id)
char *listfile;
int id;
{
    if (!proto_valid(id))
    {
        puts("Unknown protocol");
        return 0;
    }

    if (!proto_is_batch(id))
    {
        puts("Protocol is not batch-capable");
        return 0;
    }

    if (id == 6)
        return proto_zmodem_batch_send(listfile);

    return proto_process_listfile(listfile, 1, id);
}

/* ------------------------------------------------------------ */
/* direct operator helpers                                      */
/* ------------------------------------------------------------ */

void proto_direct_upload(void)
{
    char path[MAX_PATHNAME];

    data_prompt_line("Upload filename: ", path, sizeof(path));
    if (!path[0])
        return;

    (void)proto_upload(path, g_sess.user.protocol);
}

void proto_direct_download(void)
{
    char path[MAX_PATHNAME];

    data_prompt_line("Download filename: ", path, sizeof(path));
    if (!path[0])
        return;

    (void)proto_download(path, g_sess.user.protocol);
}