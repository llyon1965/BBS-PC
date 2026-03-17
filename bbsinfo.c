/*
 * BBSINFO - BBS-PC system information utility
 *
 * Manual-aligned reconstruction for BBS-PC! 4.20
 *
 * Copyright (c) 1985,86,87 Micro-Systems Software Inc.
 * Reconstruction Copyright (c) 2026 Lance Lyon
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>
#include <fcntl.h>
#include "bbs420.h"

#define MAX_TOPFILES 100
#define MAX_HISTORY  512

#define PRIV_GUEST    0
#define PRIV_SYSOP  255

#define DISC_NORMAL   0
#define DISC_CARRIER  1
#define DISC_SLEEP    2
#define DISC_TIMEOUT  3

typedef struct {
    char name[16];
    LONG count;
} TOPFILE;

typedef struct {
    char name[16];
    UWORD accesses;
} FILESNAP;

typedef struct {
    UWORD days;
    UWORD hourly[24];
    UWORD file_count;
    FILESNAP file[MAX_HISTORY];
} HISTORY;

typedef struct {
    LONG sysop_sessions;
    LONG local_calls;
    LONG baud_19200;
    LONG baud_9600;
    LONG baud_4800;
    LONG baud_2400;
    LONG baud_1200;
    LONG baud_600;
    LONG baud_300;
    LONG mode_8n1;
    LONG mode_7e1;
    LONG disc_normal;
    LONG disc_carrier;
    LONG disc_sleep;
    LONG disc_timeout;
    LONG files_down;
    LONG files_up;
    LONG msgs_entered;
    LONG calls_in_log;
    LONG util_minutes;
    LONG elapsed_minutes;
    LONG users_sysop;
    LONG users_guest;
    LONG users_locked;
    LONG users_total;
    LONG msgs_private;
    LONG msgs_roll_locked;
    LONG msgs_total;
    LONG files_total;
    LONG file_bytes_total;
    LONG bytes_downloaded;
    LONG files_downloaded_period;
    LONG diff_files_downloaded;
    LONG msg_section[NUM_SECT];
    LONG file_section[NUM_SECT];
    LONG terminal[NUM_TERM];
    LONG hourly[24];
} STATS;

static CFGINFO cfg;
static STATS stats;
static HISTORY prev_hist, new_hist;
static TOPFILE topfiles[MAX_TOPFILES];
static int topcount = 0;
static int ibm_graphics = 0;

/* ------------------------------------------------------------ */

void fatal(msg) char *msg;
{
    puts(msg);
    exit(1);
}

void trim_crlf(s) char *s;
{
    char *p;
    p = strchr(s, '\r'); if (p) *p = 0;
    p = strchr(s, '\n'); if (p) *p = 0;
}

void prompt_string(prompt, buf, len, def)
char *prompt; char *buf; int len; char *def;
{
    printf("%s ", prompt);
    fgets(buf, len, stdin);
    trim_crlf(buf);
    if (!buf[0] && def) strcpy(buf, def);
}

long indexed_record_count(fd, reclen) int fd; int reclen;
{
    long sz = filelength(fd);
    if (sz < FILEHDR_LEN) return 0L;
    return (sz - FILEHDR_LEN) / (long)reclen;
}

long indexed_offset(recno, reclen) long recno; int reclen;
{
    return FILEHDR_LEN + recno * (long)reclen;
}

long unpack_minutes(t) UWORD t;
{
    int hh = (t >> 11) & 31;
    int mm = (t >> 5) & 63;
    return (long)hh * 60L + (long)mm;
}

int unpack_hour(t) UWORD t
{
    return (t >> 11) & 31;
}

void load_cfg()
{
    int fd = open("CFGINFO.DAT", O_RDONLY | O_BINARY);
    if (fd < 0) fatal("Can't open CFGINFO.DAT");
    if (read(fd, &cfg, sizeof(cfg)) != sizeof(cfg))
    {
        close(fd);
        fatal("Can't read CFGINFO.DAT");
    }
    close(fd);
}

void load_history(name) char *name;
{
    int fd;

    memset(&prev_hist, 0, sizeof(prev_hist));

    fd = open(name, O_RDONLY | O_BINARY);
    if (fd < 0)
        return;

    read(fd, &prev_hist, sizeof(prev_hist));
    close(fd);
}

void save_history(name) char *name;
{
    int fd = open(name, O_CREAT | O_TRUNC | O_WRONLY | O_BINARY, 0666);
    if (fd < 0) fatal("Can't create history file");
    if (write(fd, &new_hist, sizeof(new_hist)) != sizeof(new_hist))
    {
        close(fd);
        fatal("Can't write history file");
    }
    close(fd);
}

UWORD prev_accesses(name) char *name;
{
    int i;
    for (i = 0; i < prev_hist.file_count && i < MAX_HISTORY; i++)
        if (!stricmp(prev_hist.file[i].name, name))
            return prev_hist.file[i].accesses;
    return 0;
}

void add_snapshot(name, accesses) char *name; UWORD accesses;
{
    int i;

    if (new_hist.file_count >= MAX_HISTORY)
        return;

    i = new_hist.file_count++;
    strncpy(new_hist.file[i].name, name, 15);
    new_hist.file[i].name[15] = 0;
    new_hist.file[i].accesses = accesses;
}

void add_topfile(name, count) char *name; LONG count;
{
    int i;

    if (count <= 0)
        return;

    for (i = 0; i < topcount; i++)
    {
        if (!stricmp(topfiles[i].name, name))
        {
            topfiles[i].count += count;
            return;
        }
    }

    if (topcount >= MAX_TOPFILES)
        return;

    strncpy(topfiles[topcount].name, name, 15);
    topfiles[topcount].name[15] = 0;
    topfiles[topcount].count = count;
    topcount++;
}

void sort_topfiles()
{
    int i, j;
    TOPFILE t;

    for (i = 0; i < topcount - 1; i++)
        for (j = i + 1; j < topcount; j++)
            if (topfiles[j].count > topfiles[i].count)
            {
                t = topfiles[i];
                topfiles[i] = topfiles[j];
                topfiles[j] = t;
            }
}

/* ------------------------------------------------------------ */

void scan_caller_log(hours_per_day)
int hours_per_day;
{
    int fd;
    long nrec, i;
    USRLOG rec;
    long inmin, outmin, dur;
    int hour;

    puts("Scanning caller log...");
    fd = open("CALLER.DAT", O_RDONLY | O_BINARY);
    if (fd < 0) fatal("Can't open CALLER.DAT");

    nrec = indexed_record_count(fd, sizeof(USRLOG));

    for (i = 0; i < nrec; i++)
    {
        lseek(fd, indexed_offset(i, sizeof(USRLOG)), SEEK_SET);
        if (read(fd, &rec, sizeof(rec)) != sizeof(rec)) continue;
        if (rec.number == 0L) continue;

        stats.calls_in_log++;

        if (rec.priv == PRIV_SYSOP) stats.sysop_sessions++;
        if (rec.baud == 0) stats.local_calls++;

        switch (rec.baud)
        {
            case 300:   stats.baud_300++; break;
            case 600:   stats.baud_600++; break;
            case 1200:  stats.baud_1200++; break;
            case 2400:  stats.baud_2400++; break;
            case 4800:  stats.baud_4800++; break;
            case 9600:  stats.baud_9600++; break;
            case 19200: stats.baud_19200++; break;
        }

        if (comm_word_bits(rec.com) == 8 &&
            comm_parity_char(rec.com) == 'N' &&
            comm_stop_bits(rec.com) == 1)
            stats.mode_8n1++;
        else if (comm_word_bits(rec.com) == 7 &&
                 comm_parity_char(rec.com) == 'E' &&
                 comm_stop_bits(rec.com) == 1)
            stats.mode_7e1++;

        switch (rec.disconnect)
        {
            case DISC_NORMAL:  stats.disc_normal++; break;
            case DISC_CARRIER: stats.disc_carrier++; break;
            case DISC_SLEEP:   stats.disc_sleep++; break;
            case DISC_TIMEOUT: stats.disc_timeout++; break;
        }

        stats.files_down += rec.dnls;
        stats.files_up += rec.upls;
        stats.msgs_entered += rec.msgs;

        inmin = unpack_minutes(rec.in_time);
        outmin = unpack_minutes(rec.out_time);
        dur = (outmin >= inmin) ? (outmin - inmin) : 0;
        stats.util_minutes += dur;

        hour = unpack_hour(rec.in_time);
        if (hour >= 0 && hour < 24)
        {
            stats.hourly[hour]++;
            new_hist.hourly[hour]++;
        }
    }

    close(fd);

    if (prev_hist.days)
        new_hist.days = prev_hist.days + 1;
    else
        new_hist.days = (hours_per_day > 0) ? (int)((stats.calls_in_log ? 1 : 0)) : 0;

    if (new_hist.days == 0)
        new_hist.days = 1;

    stats.elapsed_minutes = (LONG)new_hist.days * (LONG)hours_per_day * 60L;
}

/* ------------------------------------------------------------ */

void scan_user_files()
{
    int fd;
    long nrec, i;
    USRDESC u;

    puts("Scanning user files...");
    fd = open("USERDESC.DAT", O_RDONLY | O_BINARY);
    if (fd < 0) fatal("Can't open USERDESC.DAT");

    nrec = indexed_record_count(fd, sizeof(USRDESC));

    for (i = 0; i < nrec; i++)
    {
        lseek(fd, indexed_offset(i, sizeof(USRDESC)), SEEK_SET);
        if (read(fd, &u, sizeof(u)) != sizeof(u)) continue;
        if (!u.name[0]) continue;

        stats.users_total++;
        if (u.flags & USR_GUEST) stats.users_guest++;
        if (u.priv == PRIV_SYSOP || u.sys_acc != 0) stats.users_sysop++;
        if (u.priv == 0) stats.users_locked++;
        if (u.term < NUM_TERM) stats.terminal[u.term]++;
    }
    close(fd);
}

void scan_message_files()
{
    int fd;
    long nrec, i;
    MSGHEAD h;

    puts("Scanning message files...");
    fd = open("MSGHEAD.DAT", O_RDONLY | O_BINARY);
    if (fd < 0) fatal("Can't open MSGHEAD.DAT");

    nrec = indexed_record_count(fd, sizeof(MSGHEAD));

    for (i = 0; i < nrec; i++)
    {
        lseek(fd, indexed_offset(i, sizeof(MSGHEAD)), SEEK_SET);
        if (read(fd, &h, sizeof(h)) != sizeof(h)) continue;
        if (h.number == 0L) continue;

        stats.msgs_total++;
        if (h.flags & MH_PERSONAL) stats.msgs_private++;
        if (h.flags & MH_LOCKED) stats.msgs_roll_locked++;
        if (h.section < NUM_SECT) stats.msg_section[h.section]++;
    }
    close(fd);
}

void scan_ud_files()
{
    int fd;
    long nrec, i, delta;
    UDHEAD u;

    puts("Scanning upl/dnl files...");
    fd = open("UDHEAD.DAT", O_RDONLY | O_BINARY);
    if (fd < 0) fatal("Can't open UDHEAD.DAT");

    nrec = indexed_record_count(fd, sizeof(UDHEAD));

    memset(&new_hist, 0, sizeof(new_hist));
    new_hist.days = prev_hist.days + 1;
    if (new_hist.days == 0) new_hist.days = 1;

    for (i = 0; i < nrec; i++)
    {
        lseek(fd, indexed_offset(i, sizeof(UDHEAD)), SEEK_SET);
        if (read(fd, &u, sizeof(u)) != sizeof(u)) continue;
        if (!u.disk_name[0]) continue;
        if (!(u.flags & UDH_VALID)) continue;

        stats.files_total++;
        stats.file_bytes_total += u.length;
        if (u.section < NUM_SECT) stats.file_section[u.section]++;

        delta = (long)u.accesses - (long)prev_accesses(u.disk_name);
        if (delta < 0) delta = 0;

        if (delta > 0)
        {
            stats.files_downloaded_period += delta;
            stats.bytes_downloaded += delta * u.length;
            stats.diff_files_downloaded++;
            add_topfile(u.disk_name, delta);
        }

        add_snapshot(u.disk_name, u.accesses);
    }
    close(fd);
    sort_topfiles();
}

/* ------------------------------------------------------------ */

void print_bar_line(fp, pct, width, label, use_star)
FILE *fp;
int pct, width;
char *label;
int use_star;
{
    int i;
    fprintf(fp, "%s", label);
    for (i = 0; i < width; i++)
        fputc(use_star ? '*' : 176, fp);
    fputc('\n', fp);
}

void print_hourly_graph(fp, days)
FILE *fp; int days;
{
    int i, j, pct;
    int width;

    fprintf(fp, "Average Percentage of Hourly Usage - Starting %s - %d Days\n\n",
        "00-00-00", days);

    for (i = 100; i >= 5; i -= 5)
    {
        fprintf(fp, "%3d-| ", i);
        for (j = 0; j < 24; j++)
        {
            pct = days ? (int)(new_hist.hourly[j] * 100L / days) : 0;
            width = (pct >= i) ? 1 : 0;
            fputc(width ? (ibm_graphics ? 254 : '*') : ' ', fp);
            fputc(' ', fp);
        }
        fputc('\n', fp);
    }

    fprintf(fp, "AM 12 1 2 3 4 5 6 7 8 9 10 11 12 1 2 3 4 5 6 7 8 9 10 11 PM\n\n");
}

void print_comm_stats(fp)
FILE *fp;
{
    long pct;

    pct = (stats.elapsed_minutes > 0)
        ? (stats.util_minutes * 100L) / stats.elapsed_minutes
        : 0L;

    fprintf(fp, "Communications Statistics:\n\n");
    fprintf(fp, "Total wizard SYSOP sessions %ld\n", stats.sysop_sessions);
    fprintf(fp, "Total user calls at local %ld\n", stats.local_calls);
    fprintf(fp, "Total user calls at 19200 bps %ld\n", stats.baud_19200);
    fprintf(fp, "Total user calls at 9600 bps %ld\n", stats.baud_9600);
    fprintf(fp, "Total user calls at 4800 bps %ld\n", stats.baud_4800);
    fprintf(fp, "Total user calls at 2400 bps %ld\n", stats.baud_2400);
    fprintf(fp, "Total user calls at 1200 bps %ld\n", stats.baud_1200);
    fprintf(fp, "Total user calls at 600 bps %ld\n", stats.baud_600);
    fprintf(fp, "Total user calls at 300 bps %ld\n", stats.baud_300);
    fprintf(fp, "Total user calls at 8,N,1 %ld\n", stats.mode_8n1);
    fprintf(fp, "Total user calls at 7,E,1 %ld\n", stats.mode_7e1);
    fprintf(fp, "Total normal disconnects %ld\n", stats.disc_normal);
    fprintf(fp, "Total carrier lost disconnects %ld\n", stats.disc_carrier);
    fprintf(fp, "Total sleep disconnects %ld\n", stats.disc_sleep);
    fprintf(fp, "Total exceeded time disconnects %ld\n", stats.disc_timeout);
    fprintf(fp, "Total files downloaded %ld\n", stats.files_down);
    fprintf(fp, "Total files uploaded %ld\n", stats.files_up);
    fprintf(fp, "Total messages entered %ld\n", stats.msgs_entered);
    fprintf(fp, "Total calls in log %ld\n", stats.calls_in_log);
    fprintf(fp, "Total system utilization time (HHH:MM) %ld:%02ld\n",
        stats.util_minutes / 60L, stats.util_minutes % 60L);
    fprintf(fp, "Total system elapsed time (HHH:MM) %ld:%02ld\n",
        stats.elapsed_minutes / 60L, stats.elapsed_minutes % 60L);
    fprintf(fp, "Percentage of utilization %ld%%\n\n", pct);
}

void print_section_usage_graph(fp)
FILE *fp;
{
    int i, lvl;
    double msgpct[NUM_SECT], filepct[NUM_SECT];
    long msgtot = 0L, filetot = 0L;

    for (i = 0; i < NUM_SECT; i++)
    {
        msgtot += stats.msg_section[i];
        filetot += stats.file_section[i];
    }

    for (i = 0; i < NUM_SECT; i++)
    {
        msgpct[i] = msgtot ? (100.0 * (double)stats.msg_section[i] / (double)msgtot) : 0.0;
        filepct[i] = filetot ? (100.0 * (double)stats.file_section[i] / (double)filetot) : 0.0;
    }

    fprintf(fp, "Average Percentage of Section Usage - %ld Msgs, %ld Files\n\n",
        msgtot, filetot);

    for (lvl = 100; lvl >= 5; lvl -= 5)
    {
        fprintf(fp, "%3d-| ", lvl);
        for (i = 0; i < NUM_SECT; i++)
        {
            if (msgpct[i] >= (double)lvl)
                fputc('*', fp);
            else
                fputc(' ', fp);

            if (filepct[i] >= (double)lvl)
                fputc('#', fp);
            else
                fputc(' ', fp);

            fputc(' ', fp);
        }
        fputc('\n', fp);
    }

    fprintf(fp, "Sec 0123456789ABCDEF\n\n");
}

void print_top_files(fp)
FILE *fp;
{
    int i;

    fprintf(fp, "Most often downloaded files for this period:\n\n");
    for (i = 0; i < topcount; i++)
        fprintf(fp, "%s %ld\n", topfiles[i].name, topfiles[i].count);
    fprintf(fp, "\n");
}

void print_terminal_breakdown(fp)
FILE *fp;
{
    int i;

    fprintf(fp, "Breakdown of users by terminal:\n\n");
    for (i = 0; i < NUM_TERM; i++)
        fprintf(fp, "%X: %s %ld\n",
            i,
            cfg.trmnl[i].name[0] ? cfg.trmnl[i].name : "Terminal",
            stats.terminal[i]);
    fprintf(fp, "\n");
}

void print_system_control(fp)
FILE *fp;
{
    fprintf(fp, "System Control Statistics:\n\n");
    fprintf(fp, "Total number of sysops %ld\n", stats.users_sysop);
    fprintf(fp, "Total number of guests %ld\n", stats.users_guest);
    fprintf(fp, "Total users locked out %ld\n", stats.users_locked);
    fprintf(fp, "Total number of users in file %ld\n", stats.users_total);
    fprintf(fp, "Total number of private messages %ld\n", stats.msgs_private);
    fprintf(fp, "Total roll-locked messages %ld\n", stats.msgs_roll_locked);
    fprintf(fp, "Total number of messages in file %ld\n\n", stats.msgs_total);
}

void print_file_section_stats(fp)
FILE *fp;
{
    fprintf(fp, "File Section Statistics:\n\n");
    fprintf(fp, "Total files downloaded for period %ld\n", stats.files_downloaded_period);
    fprintf(fp, "Total different files downloaded %ld\n", stats.diff_files_downloaded);
    fprintf(fp, "Total bytes downloaded for period %ld\n", stats.bytes_downloaded);
    fprintf(fp, "Total number of files %ld\n", stats.files_total);
    fprintf(fp, "Total bytes occupied by files %ld\n\n", stats.file_bytes_total);
}

void generate_report(name)
char *name;
{
    FILE *fp;

    fp = fopen(name, "wt");
    if (!fp) fatal("Can't create stats file");

    print_hourly_graph(fp, new_hist.days);
    print_comm_stats(fp);
    print_section_usage_graph(fp);
    print_top_files(fp);
    print_terminal_breakdown(fp);
    print_system_control(fp);
    print_file_section_stats(fp);

    fclose(fp);
}

/* ------------------------------------------------------------ */

void parse_args(argc, argv)
int argc; char *argv[];
{
    int i;

    for (i = 1; i < argc; i++)
    {
        if (!stricmp(argv[i], "-g") || !stricmp(argv[i], "/g"))
            ibm_graphics = 1;
    }
}

/* ------------------------------------------------------------ */

int main(argc, argv)
int argc;
char *argv[];
{
    char hours[32], prevfile[64], newfile[64];

    parse_args(argc, argv);

    memset(&stats, 0, sizeof(stats));
    memset(&prev_hist, 0, sizeof(prev_hist));
    memset(&new_hist, 0, sizeof(new_hist));

    puts("BBSINFO - BBS-PC system information - 4.20");
    puts("Copyright (c) 1985,86,87 Micro-Systems Software Inc.");
    puts("");

    prompt_string("Hours/day of operation?", hours, sizeof(hours), "24");
    prompt_string("Previous period filename?", prevfile, sizeof(prevfile), "");
    prompt_string("New period filename?", newfile, sizeof(newfile), "");

    load_cfg();
    if (prevfile[0]) load_history(prevfile);

    scan_caller_log(atoi(hours) ? atoi(hours) : 24);
    scan_user_files();
    scan_message_files();
    scan_ud_files();

    generate_report("STATS.TXT");

    if (newfile[0]) save_history(newfile);

    puts("Report generated: STATS.TXT");
    return 0;
}
