/* BBSFIX.C
 *
 * BBS-PC ! 4.21
 *
 * Reconstructed and modernised source
 * derived from BBS-PC 4.20
 *
 * Database repair / verification utility.
 *
 * Updated to use the strengthened ISAM safety layer:
 * - verifies fixed-record layout before rebuild
 * - reports probable corruption
 * - can blank missing headers
 * - can append blank records safely
 * - rebuilds only when required
 *
 * Notes:
 * - This is a conservative maintenance utility.
 * - It does not alter conversion utilities.
 * - It assumes the current rebuilt runtime naming:
 *     g_usrfp  USERDESC.DAT
 *     g_msgfp  MSGHEAD.DAT
 *     g_txtfp  MSGTEXT.DAT
 *     g_udfp   UDHEAD.DAT
 *     g_logfp  CALLER.DAT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bbsdata.h"
#include "bbsfunc.h"

#ifndef BBS_VERSION
#define BBS_VERSION "BBS-PC ! 4.21"
#endif

#ifndef HDRLEN
#define HDRLEN 128L
#endif

#ifndef MAX_PATHNAME
#define MAX_PATHNAME 128
#endif

typedef struct {
    char *label;
    char *filename;
    int   reclen;
    FILE **fpp;
} FIXFILE;

static int g_fix_verbose = 1;

/* ------------------------------------------------------------ */
/* local helpers                                                */
/* ------------------------------------------------------------ */

static void fix_print(s)
char *s;
{
    if (g_fix_verbose)
        puts(s);
}

static void fix_print_file_status(label, msg)
char *label;
char *msg;
{
    printf("%-12s : %s\n", label, msg);
}

static int fix_open_rw_or_create(path, fp)
char *path;
FILE **fp;
{
    *fp = fopen(path, "r+b");
    if (*fp)
        return 1;

    *fp = fopen(path, "w+b");
    if (!*fp)
        return 0;

    return isam_blank_header(*fp);
}

static int fix_ensure_header(fp)
FILE *fp;
{
    long size;

    if (!fp)
        return 0;

    size = isam_file_size(fp);
    if (size < 0L)
        return 0;

    if (size == 0L)
        return isam_blank_header(fp);

    if (size < HDRLEN)
    {
        unsigned char hdr[HDRLEN];
        long cur;
        long i;

        memset(hdr, 0, sizeof(hdr));

        cur = ftell(fp);
        if (cur < 0L)
            cur = 0L;

        if (fseek(fp, 0L, SEEK_SET) != 0)
            return 0;

        for (i = 0L; i < size && i < HDRLEN; i++)
        {
            int ch = fgetc(fp);
            if (ch == EOF)
                break;
            hdr[i] = (unsigned char)ch;
        }

        if (fseek(fp, 0L, SEEK_SET) != 0)
            return 0;

        if (fwrite(hdr, 1, sizeof(hdr), fp) != sizeof(hdr))
            return 0;

        fflush(fp);
        (void)fseek(fp, cur, SEEK_SET);
    }

    return 1;
}

static int fix_layout_valid(fp, reclen)
FILE *fp;
int reclen;
{
    long size;
    long data_bytes;
    int hdr_reclen;

    if (!fp || reclen <= 0)
        return 0;

    size = isam_file_size(fp);
    if (size < HDRLEN)
        return 0;

    data_bytes = size - HDRLEN;
    if ((data_bytes % (long)reclen) != 0L)
        return 0;

    if (isam_header_reclen(fp, &hdr_reclen))
        if (hdr_reclen != reclen)
            return 0;

    return 1;
}

static int fix_rewrite_header_reclen(fp, reclen)
FILE *fp;
int reclen;
{
    unsigned char hdr[HDRLEN];
    long cur;

    if (!fp || reclen <= 0)
        return 0;

    memset(hdr, 0, sizeof(hdr));
    if (!isam_read_header(fp, hdr, sizeof(hdr)))
        memset(hdr, 0, sizeof(hdr));

    hdr[0] = (unsigned char)(reclen & 0xFF);
    hdr[1] = (unsigned char)((reclen >> 8) & 0xFF);

    cur = ftell(fp);
    if (cur < 0L)
        cur = 0L;

    if (!isam_write_header(fp, hdr, sizeof(hdr)))
        return 0;

    (void)fseek(fp, cur, SEEK_SET);
    return 1;
}

static int fix_pad_to_record_boundary(fp, reclen)
FILE *fp;
int reclen;
{
    long size;
    long data_bytes;
    long rem;
    long need;
    unsigned char zero[256];
    long i;

    if (!fp || reclen <= 0)
        return 0;

    size = isam_file_size(fp);
    if (size < HDRLEN)
        return 0;

    data_bytes = size - HDRLEN;
    rem = data_bytes % (long)reclen;
    if (rem == 0L)
        return 1;

    need = (long)reclen - rem;
    memset(zero, 0, sizeof(zero));

    if (fseek(fp, 0L, SEEK_END) != 0)
        return 0;

    while (need > 0L)
    {
        i = (need > (long)sizeof(zero)) ? (long)sizeof(zero) : need;
        if (fwrite(zero, 1, (unsigned)i, fp) != (unsigned)i)
            return 0;
        need -= i;
    }

    fflush(fp);
    return 1;
}

static int fix_scan_records(fp, reclen, label)
FILE *fp;
int reclen;
char *label;
{
    long recs;
    long i;
    char *buf;
    int ok = 1;

    recs = isam_record_count(fp, reclen);
    if (recs < 0L)
    {
        fix_print_file_status(label, "invalid record count");
        return 0;
    }

    buf = (char *)malloc((unsigned)reclen);
    if (!buf)
    {
        fix_print_file_status(label, "memory allocation failed");
        return 0;
    }

    for (i = 0L; i < recs; i++)
    {
        if (!isam_read_record(fp, i, reclen, buf))
        {
            printf("%-12s : read failure at record %ld\n", label, i);
            ok = 0;
            break;
        }
    }

    free(buf);
    return ok;
}

static int fix_repair_file(ff)
FIXFILE *ff;
{
    char path[MAX_PATHNAME];
    FILE *fp;
    long recs;
    char msg[80];

    data_make_data_path(path, ff->filename);
    if (!fix_open_rw_or_create(path, &fp))
    {
        fix_print_file_status(ff->label, "open/create failed");
        return 0;
    }

    *(ff->fpp) = fp;

    if (!fix_ensure_header(fp))
    {
        fix_print_file_status(ff->label, "header repair failed");
        fclose(fp);
        *(ff->fpp) = (FILE *)0;
        return 0;
    }

    if (!fix_layout_valid(fp, ff->reclen))
    {
        fix_print_file_status(ff->label, "layout invalid; padding/rebuilding header");
        if (!fix_pad_to_record_boundary(fp, ff->reclen))
        {
            fix_print_file_status(ff->label, "padding failed");
            fclose(fp);
            *(ff->fpp) = (FILE *)0;
            return 0;
        }

        if (!fix_rewrite_header_reclen(fp, ff->reclen))
        {
            fix_print_file_status(ff->label, "header rewrite failed");
            fclose(fp);
            *(ff->fpp) = (FILE *)0;
            return 0;
        }

        if (!fix_layout_valid(fp, ff->reclen))
        {
            fix_print_file_status(ff->label, "layout still invalid");
            fclose(fp);
            *(ff->fpp) = (FILE *)0;
            return 0;
        }
    }
    else
    {
        fix_print_file_status(ff->label, "layout verified");
    }

    if (!fix_scan_records(fp, ff->reclen, ff->label))
    {
        fix_print_file_status(ff->label, "record scan failed");
        fclose(fp);
        *(ff->fpp) = (FILE *)0;
        return 0;
    }

    recs = isam_record_count(fp, ff->reclen);
    sprintf(msg, "OK (%ld records)", recs < 0L ? 0L : recs);
    fix_print_file_status(ff->label, msg);
    return 1;
}

static int fix_rebuild_keys_all(void)
{
    int ok = 1;
    char path_dat[MAX_PATHNAME];
    char path_key[MAX_PATHNAME];

    data_make_data_path(path_dat, "USERDESC.DAT");
    data_make_data_path(path_key, "USERDESC.KEY");
    ok &= isam_rebuild_keys(path_dat, path_key);

    data_make_data_path(path_dat, "MSGHEAD.DAT");
    data_make_data_path(path_key, "MSGHEAD.KEY");
    ok &= isam_rebuild_keys(path_dat, path_key);

    data_make_data_path(path_dat, "UDHEAD.DAT");
    data_make_data_path(path_key, "UDHEAD.KEY");
    ok &= isam_rebuild_keys(path_dat, path_key);

    return ok;
}

static void fix_close_all(void)
{
    if (g_usrfp) { fclose(g_usrfp); g_usrfp = (FILE *)0; }
    if (g_msgfp) { fclose(g_msgfp); g_msgfp = (FILE *)0; }
    if (g_txtfp) { fclose(g_txtfp); g_txtfp = (FILE *)0; }
    if (g_udfp)  { fclose(g_udfp);  g_udfp  = (FILE *)0; }
    if (g_logfp) { fclose(g_logfp); g_logfp = (FILE *)0; }
}

/* ------------------------------------------------------------ */
/* public repair entry points                                   */
/* ------------------------------------------------------------ */

int fix_verify_all_datafiles(void)
{
    FIXFILE files[5];
    int ok = 1;
    int i;

    files[0].label = "USERDESC";
    files[0].filename = "USERDESC.DAT";
    files[0].reclen = sizeof(USRDESC);
    files[0].fpp = &g_usrfp;

    files[1].label = "MSGHEAD";
    files[1].filename = "MSGHEAD.DAT";
    files[1].reclen = sizeof(MSGHEAD);
    files[1].fpp = &g_msgfp;

    files[2].label = "MSGTEXT";
    files[2].filename = "MSGTEXT.DAT";
    files[2].reclen = sizeof(MSGTEXT);
    files[2].fpp = &g_txtfp;

    files[3].label = "UDHEAD";
    files[3].filename = "UDHEAD.DAT";
    files[3].reclen = sizeof(UDHEAD);
    files[3].fpp = &g_udfp;

    files[4].label = "CALLER";
    files[4].filename = "CALLER.DAT";
    files[4].reclen = sizeof(USRLOG);
    files[4].fpp = &g_logfp;

    puts(BBS_VERSION);
    puts("Database verification");
    puts("");

    for (i = 0; i < 5; i++)
        ok &= fix_repair_file(&files[i]);

    if (ok)
        fix_print("All datafiles verified");
    else
        fix_print("One or more datafiles failed verification");

    return ok;
}

int fix_repair_all_datafiles(void)
{
    int ok;

    ok = fix_verify_all_datafiles();
    if (!ok)
        return 0;

    if (!fix_rebuild_keys_all())
    {
        puts("Key rebuild failed");
        return 0;
    }

    puts("Repair complete");
    return 1;
}

void fix_show_summary(void)
{
    printf("Users    : %ld\n", isam_count_users());
    printf("Msgs     : %ld\n", isam_count_msgs());
    printf("MsgText  : %ld\n", isam_count_msgtext());
    printf("Files    : %ld\n", isam_count_files());
    printf("Callers  : %ld\n", isam_count_callers());
}

/* ------------------------------------------------------------ */
/* utility main                                                 */
/* ------------------------------------------------------------ */

int main(argc, argv)
int argc;
char *argv[];
{
    int repair_mode = 1;
    int i;
    int ok;

    for (i = 1; i < argc; i++)
    {
        if (!stricmp(argv[i], "/V") || !stricmp(argv[i], "-V"))
            repair_mode = 0;
        else if (!stricmp(argv[i], "/Q") || !stricmp(argv[i], "-Q"))
            g_fix_verbose = 0;
    }

    if (!load_bbs_paths("BBSPATHS.CFG"))
    {
        puts("Unable to load BBSPATHS.CFG");
        return 1;
    }

    if (!load_cfginfo("CFGINFO.DAT"))
    {
        puts("Unable to load CFGINFO.DAT");
        return 1;
    }

    if (repair_mode)
        ok = fix_repair_all_datafiles();
    else
        ok = fix_verify_all_datafiles();

    if (ok)
        fix_show_summary();

    fix_close_all();
    return ok ? 0 : 1;
}