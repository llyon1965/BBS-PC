/*
 * BBSMENU - BBS-PC menu compiler
 *
 * Revised reconstruction with corrected menu item semantics:
 *
 *   menu = filename, "prompt", display_option, input_mode
 *   title = "text"
 *   item = key, "text", access_spec, access_mode, function [, parameter]
 *   iret = ...
 *   endmenu
 *
 *   qa = question.qa, question.txt
 *   display = "text"
 *   question = "text", x
 *   endqa
 *
 *   ballot = voting.bal
 *   question = "text"
 *   choice = C, "text"
 *   endballot
 *
 * Comments begin with '.'
 *
 * Copyright (c) 1985,86,87 Micro-Systems Software Inc.
 * Reconstruction Copyright (c) 2026 Lance Lyon
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_LINE         256
#define MAX_LINES         64
#define MAX_NAME          64
#define MAX_FIELDS         8
#define MAX_FIELD_LEN    128
#define MAX_MENU_BYTES  4000
#define MAX_MENU_LINES    50

typedef enum {
    ST_NONE,
    ST_MENU,
    ST_QA,
    ST_BALLOT
} STATE;

typedef struct {
    char filename[MAX_NAME];
    char prompt[MAX_FIELD_LEN];
    int  display_option;
    int  input_mode;
    char record[MAX_LINES][MAX_LINE];
    int  count;
    int  bytes;
} MENUOBJ;

typedef struct {
    char qa_name[MAX_NAME];
    char txt_name[MAX_NAME];
    char record[MAX_LINES][MAX_LINE];
    int  count;
} QAOBJ;

typedef struct {
    char bal_name[MAX_NAME];
    char record[MAX_LINES][MAX_LINE];
    int  count;
} BALLOTOBJ;

static STATE state = ST_NONE;
static MENUOBJ menuobj;
static QAOBJ qaobj;
static BALLOTOBJ ballotobj;

/* ------------------------------------------------------------ */

void fatal(msg) char *msg;
{
    puts(msg);
    exit(1);
}

void compiler_error(msg, lineno)
char *msg;
int lineno;
{
    printf("%s (line %d)\n", msg, lineno);
    exit(1);
}

void trim_crlf(s) char *s;
{
    char *p;
    p = strchr(s, '\r'); if (p) *p = 0;
    p = strchr(s, '\n'); if (p) *p = 0;
}

void ltrim(s) char *s;
{
    while (*s == ' ' || *s == '\t')
        memmove(s, s + 1, strlen(s));
}

void rtrim(s) char *s;
{
    int n = (int)strlen(s);
    while (n > 0 && (s[n-1] == ' ' || s[n-1] == '\t'))
        s[--n] = 0;
}

void trim(s) char *s;
{
    ltrim(s);
    rtrim(s);
}

int is_blank(s) char *s;
{
    while (*s)
    {
        if (*s != ' ' && *s != '\t')
            return 0;
        s++;
    }
    return 1;
}

int is_comment(s) char *s;
{
    return (*s == '.');
}

int is_digits(s) char *s;
{
    if (!*s) return 0;
    while (*s)
    {
        if (!isdigit((unsigned char)*s))
            return 0;
        s++;
    }
    return 1;
}

/* ------------------------------------------------------------ */
/* quoted text / field split                                    */
/* ------------------------------------------------------------ */

void decode_escapes(dst, src)
char *dst, *src;
{
    int n;

    while (*src)
    {
        if (*src == '\\')
        {
            src++;

            if (*src >= '0' && *src <= '9')
            {
                n = 0;
                while (*src >= '0' && *src <= '9')
                {
                    n = n * 10 + (*src - '0');
                    src++;
                }
                *dst++ = (char)n;
                continue;
            }

            if (*src == 'n')      { *dst++ = '\n'; src++; continue; }
            if (*src == 'r')      { *dst++ = '\r'; src++; continue; }
            if (*src == 't')      { *dst++ = '\t'; src++; continue; }
            if (*src == '\\')     { *dst++ = '\\'; src++; continue; }
            if (*src == '"')      { *dst++ = '"';  src++; continue; }

            if (*src)
                *dst++ = *src++;
            continue;
        }

        *dst++ = *src++;
    }

    *dst = 0;
}

int unquote_text(src, dst)
char *src, *dst;
{
    char *a, *b;
    char temp[MAX_FIELD_LEN];

    a = strchr(src, '"');
    b = strrchr(src, '"');

    if (!a || !b || a == b)
        return 0;

    *b = 0;
    strncpy(temp, a + 1, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = 0;
    decode_escapes(dst, temp);
    *b = '"';

    return 1;
}

int split_fields(src, field, maxf, lineno)
char *src;
char field[][MAX_FIELD_LEN];
int maxf, lineno;
{
    int count = 0, inquote = 0;
    char work[MAX_LINE];
    char *p, *start;

    strncpy(work, src, sizeof(work) - 1);
    work[sizeof(work) - 1] = 0;

    start = work;
    for (p = work; ; p++)
    {
        if (*p == '"')
            inquote = !inquote;
        else if ((*p == ',' && !inquote) || *p == 0)
        {
            if (count >= maxf)
                compiler_error("Too many entries", lineno);

            if (*p)
                *p = 0;

            strncpy(field[count], start, MAX_FIELD_LEN - 1);
            field[count][MAX_FIELD_LEN - 1] = 0;
            trim(field[count]);
            count++;

            if (*p == 0)
                break;

            start = p + 1;
        }
    }

    if (inquote)
        compiler_error("Missing double-quote", lineno);

    return count;
}

int parse_assignment(line, key, rhs)
char *line, *key, *rhs;
{
    char temp[MAX_LINE];
    char *eq;

    strncpy(temp, line, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = 0;
    trim(temp);

    eq = strchr(temp, '=');
    if (!eq)
        return 0;

    *eq = 0;
    strcpy(key, temp);
    trim(key);

    strcpy(rhs, eq + 1);
    trim(rhs);

    return 1;
}

/* ------------------------------------------------------------ */

void reset_menu(void)   { memset(&menuobj, 0, sizeof(menuobj)); }
void reset_qa(void)     { memset(&qaobj, 0, sizeof(qaobj)); }
void reset_ballot(void) { memset(&ballotobj, 0, sizeof(ballotobj)); }

/* ------------------------------------------------------------ */

void add_menu_record(rec, lineno)
char *rec;
int lineno;
{
    int n = (int)strlen(rec);

    if (menuobj.count >= MAX_MENU_LINES)
        compiler_error("Too many menu entries", lineno);

    if (menuobj.bytes + n + 1 > MAX_MENU_BYTES)
        compiler_error("Text too long", lineno);

    strncpy(menuobj.record[menuobj.count], rec, MAX_LINE - 1);
    menuobj.record[menuobj.count][MAX_LINE - 1] = 0;
    menuobj.count++;
    menuobj.bytes += n + 1;
}

void add_qa_record(rec, lineno)
char *rec;
int lineno;
{
    if (qaobj.count >= MAX_LINES)
        compiler_error("Too many entries", lineno);

    strncpy(qaobj.record[qaobj.count], rec, MAX_LINE - 1);
    qaobj.record[qaobj.count][MAX_LINE - 1] = 0;
    qaobj.count++;
}

void add_ballot_record(rec, lineno)
char *rec;
int lineno;
{
    if (ballotobj.count >= MAX_LINES)
        compiler_error("Too many ballot questions", lineno);

    strncpy(ballotobj.record[ballotobj.count], rec, MAX_LINE - 1);
    ballotobj.record[ballotobj.count][MAX_LINE - 1] = 0;
    ballotobj.count++;
}

/* ------------------------------------------------------------ */
/* write outputs                                                */
/* ------------------------------------------------------------ */

void write_menu_file(void)
{
    FILE *fp;
    int i;

    if (menuobj.count == 0)
        fatal("Menu contains no entries");

    fp = fopen(menuobj.filename, "wt");
    if (!fp)
        fatal("Can't create menu");

    fprintf(fp, "MENU|%s|%d|%d\n",
        menuobj.prompt,
        menuobj.display_option,
        menuobj.input_mode);

    for (i = 0; i < menuobj.count; i++)
        fprintf(fp, "%s\n", menuobj.record[i]);

    fclose(fp);
}

void write_qa_file(void)
{
    FILE *fp;
    int i;

    if (qaobj.count == 0)
        fatal("Q/A file contains no entries");

    fp = fopen(qaobj.qa_name, "wt");
    if (!fp)
        fatal("Can't create Q/A file");

    fprintf(fp, "QA|%s\n", qaobj.txt_name);
    for (i = 0; i < qaobj.count; i++)
        fprintf(fp, "%s\n", qaobj.record[i]);

    fclose(fp);
}

void write_ballot_file(void)
{
    FILE *fp;
    int i;

    if (ballotobj.count == 0)
        fatal("Ballot file contains no entries");

    fp = fopen(ballotobj.bal_name, "wt");
    if (!fp)
        fatal("Can't create ballot file");

    for (i = 0; i < ballotobj.count; i++)
        fprintf(fp, "%s\n", ballotobj.record[i]);

    fclose(fp);
}

/* ------------------------------------------------------------ */
/* validation helpers                                           */
/* ------------------------------------------------------------ */

void require_numeric(s, lineno)
char *s;
int lineno;
{
    if (!is_digits(s))
        compiler_error("Numeric digit expected", lineno);
}

void validate_access_mode(s, lineno)
char *s;
int lineno;
{
    require_numeric(s, lineno);

    if (atoi(s) < 0 || atoi(s) > 7)
        compiler_error("Numeric digit expected", lineno);
}

void validate_function_number(s, lineno)
char *s;
int lineno;
{
    require_numeric(s, lineno);
}

void validate_key(s, lineno)
char *s;
int lineno;
{
    if (!s[0] || s[1])
        compiler_error("Parameter too long", lineno);
}

/* ------------------------------------------------------------ */
/* section begin                                                */
/* ------------------------------------------------------------ */

void begin_menu(value, lineno)
char *value;
int lineno;
{
    char field[MAX_FIELDS][MAX_FIELD_LEN];
    int n;
    char prompt[MAX_FIELD_LEN];
    char tmp[MAX_FIELD_LEN];

    reset_menu();

    n = split_fields(value, field, MAX_FIELDS, lineno);
    if (n != 4)
        compiler_error("Missing comma", lineno);

    if ((int)strlen(field[0]) >= MAX_NAME)
        compiler_error("Parameter too long", lineno);

    strcpy(menuobj.filename, field[0]);

    strcpy(tmp, field[1]);
    if (!unquote_text(tmp, prompt))
        compiler_error("Missing double-quote", lineno);
    strcpy(menuobj.prompt, prompt);

    require_numeric(field[2], lineno);
    require_numeric(field[3], lineno);

    menuobj.display_option = atoi(field[2]);
    menuobj.input_mode = atoi(field[3]);

    state = ST_MENU;
}

void begin_qa(value, lineno)
char *value;
int lineno;
{
    char field[MAX_FIELDS][MAX_FIELD_LEN];
    int n;

    reset_qa();

    n = split_fields(value, field, MAX_FIELDS, lineno);
    if (n != 2)
        compiler_error("Missing comma", lineno);

    if ((int)strlen(field[0]) >= MAX_NAME || (int)strlen(field[1]) >= MAX_NAME)
        compiler_error("Parameter too long", lineno);

    strcpy(qaobj.qa_name, field[0]);
    strcpy(qaobj.txt_name, field[1]);

    state = ST_QA;
}

void begin_ballot(value, lineno)
char *value;
int lineno;
{
    reset_ballot();

    if ((int)strlen(value) >= MAX_NAME)
        compiler_error("Parameter too long", lineno);

    strcpy(ballotobj.bal_name, value);
    state = ST_BALLOT;
}

/* ------------------------------------------------------------ */
/* menu handlers                                                */
/* ------------------------------------------------------------ */

void handle_title(value, lineno)
char *value;
int lineno;
{
    char text[MAX_FIELD_LEN];
    char tmp[MAX_FIELD_LEN];
    char out[MAX_LINE];

    strcpy(tmp, value);
    if (!unquote_text(tmp, text))
        compiler_error("Missing double-quote", lineno);

    sprintf(out, "TITLE|%s", text);
    add_menu_record(out, lineno);
}

void handle_item(value, lineno)
char *value;
int lineno;
{
    char field[MAX_FIELDS][MAX_FIELD_LEN];
    int n;
    char text[MAX_FIELD_LEN];
    char tmp[MAX_FIELD_LEN];
    char out[MAX_LINE];

    n = split_fields(value, field, MAX_FIELDS, lineno);

    if (n < 5)
        compiler_error("Missing comma", lineno);

    validate_key(field[0], lineno);

    strcpy(tmp, field[1]);
    if (!unquote_text(tmp, text))
    {
        if (strcmp(field[1], "!") != 0)
            compiler_error("Missing double-quote", lineno);
        strcpy(text, "!");
    }

    validate_access_mode(field[3], lineno);
    validate_function_number(field[4], lineno);

    if (n == 5)
    {
        sprintf(out, "ITEM|%s|%s|%s|%s|%s",
            field[0], text, field[2], field[3], field[4]);
    }
    else if (n == 6)
    {
        sprintf(out, "ITEM|%s|%s|%s|%s|%s|%s",
            field[0], text, field[2], field[3], field[4], field[5]);
    }
    else
        compiler_error("Extraneous data on line", lineno);

    add_menu_record(out, lineno);
}

void handle_iret(value, lineno)
char *value;
int lineno;
{
    char out[MAX_LINE];
    sprintf(out, "IRET|%s", value);
    add_menu_record(out, lineno);
}

void handle_menu_keyword(key, value, lineno)
char *key, *value;
int lineno;
{
    if (!stricmp(key, "title"))
        handle_title(value, lineno);
    else if (!stricmp(key, "item"))
        handle_item(value, lineno);
    else if (!stricmp(key, "iret"))
        handle_iret(value, lineno);
    else if (!stricmp(key, "endmenu"))
    {
        write_menu_file();
        reset_menu();
        state = ST_NONE;
    }
    else
        compiler_error("Unknown menu keyword", lineno);
}

/* ------------------------------------------------------------ */
/* qa handlers                                                  */
/* ------------------------------------------------------------ */

void handle_qa_keyword(key, value, lineno)
char *key, *value;
int lineno;
{
    char field[MAX_FIELDS][MAX_FIELD_LEN];
    int n;
    char text[MAX_FIELD_LEN];
    char tmp[MAX_FIELD_LEN];
    char out[MAX_LINE];

    if (!stricmp(key, "display"))
    {
        strcpy(tmp, value);
        if (!unquote_text(tmp, text))
            compiler_error("Missing double-quote", lineno);

        sprintf(out, "DISPLAY|%s", text);
        add_qa_record(out, lineno);
    }
    else if (!stricmp(key, "question"))
    {
        n = split_fields(value, field, MAX_FIELDS, lineno);
        if (n != 2)
            compiler_error("Missing comma", lineno);

        strcpy(tmp, field[0]);
        if (!unquote_text(tmp, text))
            compiler_error("Missing double-quote", lineno);

        sprintf(out, "QUESTION|%s|%s", text, field[1]);
        add_qa_record(out, lineno);
    }
    else if (!stricmp(key, "endqa"))
    {
        write_qa_file();
        reset_qa();
        state = ST_NONE;
    }
    else
        compiler_error("Unknown Q/A keyword", lineno);
}

/* ------------------------------------------------------------ */
/* ballot handlers                                              */
/* ------------------------------------------------------------ */

void handle_ballot_keyword(key, value, lineno)
char *key, *value;
int lineno;
{
    char field[MAX_FIELDS][MAX_FIELD_LEN];
    int n;
    char text[MAX_FIELD_LEN];
    char tmp[MAX_FIELD_LEN];
    char out[MAX_LINE];

    if (!stricmp(key, "question"))
    {
        strcpy(tmp, value);
        if (!unquote_text(tmp, text))
            compiler_error("Missing double-quote", lineno);

        sprintf(out, "QUESTION|%s", text);
        add_ballot_record(out, lineno);
    }
    else if (!stricmp(key, "choice"))
    {
        n = split_fields(value, field, MAX_FIELDS, lineno);
        if (n != 2)
            compiler_error("Missing comma", lineno);

        validate_key(field[0], lineno);

        strcpy(tmp, field[1]);
        if (!unquote_text(tmp, text))
            compiler_error("Missing double-quote", lineno);

        sprintf(out, "CHOICE|%s|%s", field[0], text);
        add_ballot_record(out, lineno);
    }
    else if (!stricmp(key, "endballot"))
    {
        write_ballot_file();
        reset_ballot();
        state = ST_NONE;
    }
    else
        compiler_error("Unknown ballot keyword", lineno);
}

/* ------------------------------------------------------------ */

void process_line(line, lineno)
char *line;
int lineno;
{
    char key[MAX_FIELD_LEN];
    char rhs[MAX_LINE];

    trim(line);

    if (is_blank(line) || is_comment(line))
        return;

    puts(line);

    if (!parse_assignment(line, key, rhs))
    {
        if (!stricmp(line, "endmenu"))
        {
            handle_menu_keyword("endmenu", "", lineno);
            return;
        }
        if (!stricmp(line, "endqa"))
        {
            handle_qa_keyword("endqa", "", lineno);
            return;
        }
        if (!stricmp(line, "endballot"))
        {
            handle_ballot_keyword("endballot", "", lineno);
            return;
        }

        compiler_error("Missing assignment", lineno);
    }

    trim(key);
    trim(rhs);

    if (state == ST_NONE)
    {
        if (!stricmp(key, "menu"))
            begin_menu(rhs, lineno);
        else if (!stricmp(key, "qa"))
            begin_qa(rhs, lineno);
        else if (!stricmp(key, "ballot"))
            begin_ballot(rhs, lineno);
        else
            compiler_error("Unknown keyword", lineno);

        return;
    }

    switch (state)
    {
        case ST_MENU:
            handle_menu_keyword(key, rhs, lineno);
            break;
        case ST_QA:
            handle_qa_keyword(key, rhs, lineno);
            break;
        case ST_BALLOT:
            handle_ballot_keyword(key, rhs, lineno);
            break;
        default:
            compiler_error("Unknown keyword", lineno);
    }
}

/* ------------------------------------------------------------ */

int main(argc, argv)
int argc;
char *argv[];
{
    FILE *fp;
    char line[MAX_LINE];
    int lineno = 0;

    puts("BBSMENU - BBS-PC menu compiler - 4.20");
    puts("Copyright (c) 1985,86,87 Micro-Systems Software Inc.");
    puts("");

    if (argc < 2)
    {
        puts("Filename required");
        return 1;
    }

    fp = fopen(argv[1], "rt");
    if (!fp)
    {
        puts("Can't open source file");
        return 1;
    }

    reset_menu();
    reset_qa();
    reset_ballot();
    state = ST_NONE;

    while (fgets(line, sizeof(line), fp))
    {
        lineno++;
        trim_crlf(line);

        if ((int)strlen(line) >= MAX_LINE - 1)
            compiler_error("Line too long", lineno);

        process_line(line, lineno);
    }

    fclose(fp);

    if (state == ST_MENU)
        fatal("Missing endmenu");
    if (state == ST_QA)
        fatal("Missing endqa");
    if (state == ST_BALLOT)
        fatal("Missing endballot");

    return 0;
}