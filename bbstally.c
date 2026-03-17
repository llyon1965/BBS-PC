/*
 * BBSTALLY - Ballot output generator
 *
 * Manual-aligned reconstruction for BBS-PC! 4.20
 *
 * Copyright (c) 1985,86,87 Micro-Systems Software Inc.
 * Reconstruction Copyright (c) 2026 Lance Lyon
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TITLE    80
#define MAX_CHOICE   64
#define MAX_LINE    256
#define DESC_LEN     40

typedef unsigned short UWORD;
typedef short          WORD;
typedef long           LONG;

static char title[MAX_TITLE];
static char choice[MAX_CHOICE][DESC_LEN + 1];
static UWORD votes[MAX_CHOICE];
static WORD nchoice = 0;

/* ------------------------------------------------------------ */

void fatal(msg) char *msg;
{
    puts(msg);
    exit(1);
}

void trim_crlf(s) char *s;
{
    char *p;

    p = strchr(s, '\r');
    if (p) *p = 0;

    p = strchr(s, '\n');
    if (p) *p = 0;
}

int is_blank(s) char *s;
{
    while (*s)
    {
        if (*s != ' ' && *s != '\t' && *s != '\r' && *s != '\n')
            return 0;
        s++;
    }
    return 1;
}

/* ------------------------------------------------------------ */
/*
 * Reconstructed BAL format:
 *  line 1 = ballot title
 *  following nonblank lines = choice text
 *  blank line or # line ends choice list
 *  remaining lines = votes:
 *      either "n"
 *      or "user n"
 */

int read_choices(fp) FILE *fp;
{
    char line[MAX_LINE];

    if (!fgets(title, sizeof(title), fp))
        return 0;

    trim_crlf(title);

    while (fgets(line, sizeof(line), fp))
    {
        trim_crlf(line);

        if (is_blank(line) || line[0] == '#')
            break;

        if (nchoice < MAX_CHOICE)
        {
            strncpy(choice[nchoice], line, DESC_LEN);
            choice[nchoice][DESC_LEN] = 0;
            votes[nchoice] = 0;
            nchoice++;
        }
    }

    return nchoice;
}

void process_votes(fp) FILE *fp;
{
    char line[MAX_LINE];
    char user[64];
    WORD vote;

    while (fgets(line, sizeof(line), fp))
    {
        trim_crlf(line);

        if (sscanf(line, "%hd", &vote) == 1)
        {
            if (vote > 0 && vote <= nchoice)
                votes[vote - 1]++;
        }
        else if (sscanf(line, "%63s %hd", user, &vote) == 2)
        {
            if (vote > 0 && vote <= nchoice)
                votes[vote - 1]++;
        }
    }
}

/* ------------------------------------------------------------ */

void write_results(fp) FILE *fp;
{
    WORD i;
    UWORD total;
    double pct;
    int bar, j;

    total = 0;
    for (i = 0; i < nchoice; i++)
        total += votes[i];

    fprintf(fp, "%s\n\n", title);

    for (i = 0; i < nchoice; i++)
    {
        if (total)
            pct = ((double)votes[i] * 100.0) / (double)total;
        else
            pct = 0.0;

        fprintf(fp, "%-32s %5u (%5.1f%%) ", choice[i], votes[i], pct);

        bar = (int)(pct / 2.0);
        for (j = 0; j < bar; j++)
            fputc('*', fp);

        fputc('\n', fp);
    }

    fprintf(fp, "\n");
    fprintf(fp, "Total votes: %u\n", total);
}

/* ------------------------------------------------------------ */

void make_default_output(in, out) char *in, *out;
{
    char *dot;

    strcpy(out, in);
    dot = strrchr(out, '.');

    if (dot)
        strcpy(dot, ".TXT");
    else
        strcat(out, ".TXT");
}

/* ------------------------------------------------------------ */

int main(argc, argv)
int argc;
char *argv[];
{
    FILE *ballot;
    FILE *out;
    char outname[128];

    puts("BBSTALLY - Ballot output generator - 4.20");
    puts("Copyright (c) 1985,86,87 Micro-Systems Software Inc.");
    puts("");

    if (argc < 2)
    {
        puts("Filename required");
        puts("Usage: BBSTALLY ballotfile.bal [textfile.txt]");
        return 1;
    }

    ballot = fopen(argv[1], "rt");
    if (ballot == (FILE *)0)
    {
        puts("Can't open ballot file");
        return 1;
    }

    if (!read_choices(ballot))
    {
        puts("Can't read from ballot file");
        fclose(ballot);
        return 1;
    }

    process_votes(ballot);
    fclose(ballot);

    if (argc > 2)
        strcpy(outname, argv[2]);
    else
        make_default_output(argv[1], outname);

    out = fopen(outname, "wt");
    if (out == (FILE *)0)
    {
        puts("Can't create output file");
        return 1;
    }

    write_results(out);
    fclose(out);

    printf("Output written to %s\n", outname);
    return 0;
}
