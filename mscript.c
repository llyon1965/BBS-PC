/*
 * MSCRIPT - IBM word processing software
 *
 * Reconstructed period-style DOS C source based on MSCRIPT.EXE
 * Copyright (c) 1984, Micro-Systems Software Inc.
 * Reconstruction Copyright (c) 2026 Lance Lyon
 *
 * Notes:
 * - This is a best-effort functional reconstruction.
 * - It implements the visible editor/help/command/print features
 *   confirmed from the executable strings.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <conio.h>

#define DOC_MAX       65500U
#define LINEBUF_MAX    1024
#define FIND_MAX        128
#define NAME_MAX         64
#define TAB_MAX          32

#define CTRL(c) ((c) & 0x1F)

typedef unsigned char  BYTE;
typedef unsigned short UWORD;
typedef unsigned long  ULONG;

/* ------------------------------------------------------------ */

typedef struct {
    int justification;   /* JU */
    int single_sheet;    /* SS */
    int page_length;     /* PL */
    int page_spacing;    /* PS */
    int left_margin;     /* LM */
    int line_length;     /* LL */
    int line_spacing;    /* LS */
    int page_number;     /* PN */
} PRINTSET;

typedef struct {
    char data[DOC_MAX];
    unsigned len;
    unsigned cur;
    unsigned window_width;
    unsigned col;
    unsigned line;
    char name[NAME_MAX];

    char find_text[FIND_MAX];
    int mark_active;
    unsigned mark_a;
    unsigned mark_b;

    int insert_block;
    int sub_delete;

    int ink;
    int paper;

    int tabs[TAB_MAX];
    int tab_count;

    PRINTSET prn;
} DOC;

/* ------------------------------------------------------------ */

static DOC doc;

/* ------------------------------------------------------------ */

void init_doc(void)
{
    memset(&doc, 0, sizeof(doc));
    strcpy(doc.name, "NONAME");
    doc.window_width = 79;

    doc.prn.justification = 1;
    doc.prn.single_sheet = 0;
    doc.prn.page_length = 66;
    doc.prn.page_spacing = 6;
    doc.prn.left_margin = 0;
    doc.prn.line_length = 80;
    doc.prn.line_spacing = 1;
    doc.prn.page_number = 1;
}

void status_line(void)
{
    unsigned i, line, col;

    line = 1;
    col = 1;

    for (i = 0; i < doc.cur && i < doc.len; i++)
    {
        if (doc.data[i] == '\n')
        {
            line++;
            col = 1;
        }
        else
            col++;
    }

    doc.line = line;
    doc.col = col;

    cprintf("\rWindow:%3u  Column:%3u  Line:%3u  ALT-H for Help!   ",
            doc.window_width, doc.col, doc.line);
}

void msg(const char *s)
{
    cprintf("\r\n%s\r\n", s);
}

void pause_key(void)
{
    cprintf("\r\nKEY to continue");
    getch();
    cprintf("\r\n");
}

int confirm_quit(void)
{
    cprintf("\r\nExit MSCRIPT to DOS (Y/N)? ");
    return (toupper(getch()) == 'Y');
}

/* ------------------------------------------------------------ */

unsigned words_in_doc(void)
{
    unsigned i, words = 0;
    int inword = 0;

    for (i = 0; i < doc.len; i++)
    {
        if (isspace((unsigned char)doc.data[i]))
            inword = 0;
        else if (!inword)
        {
            inword = 1;
            words++;
        }
    }
    return words;
}

unsigned lines_in_doc(void)
{
    unsigned i, lines = 1;
    for (i = 0; i < doc.len; i++)
        if (doc.data[i] == '\n')
            lines++;
    return lines;
}

unsigned chars_free(void)
{
    return DOC_MAX - doc.len - 1;
}

/* ------------------------------------------------------------ */

int insert_char_at(unsigned pos, int ch)
{
    unsigned i;

    if (doc.len >= DOC_MAX - 1)
        return 0;

    if (pos > doc.len)
        pos = doc.len;

    for (i = doc.len; i > pos; i--)
        doc.data[i] = doc.data[i - 1];

    doc.data[pos] = (char)ch;
    doc.len++;
    doc.data[doc.len] = 0;
    return 1;
}

int delete_char_at(unsigned pos)
{
    unsigned i;

    if (pos >= doc.len)
        return 0;

    for (i = pos; i + 1 < doc.len; i++)
        doc.data[i] = doc.data[i + 1];

    if (doc.len)
        doc.len--;

    doc.data[doc.len] = 0;

    if (doc.cur > doc.len)
        doc.cur = doc.len;

    return 1;
}

void type_char(int ch)
{
    if (!insert_char_at(doc.cur, ch))
        msg("Out of memory!");
    else
        doc.cur++;
}

void backspace_char(void)
{
    if (doc.cur == 0)
        return;

    doc.cur--;
    delete_char_at(doc.cur);
}

void delete_under_cursor(void)
{
    delete_char_at(doc.cur);
}

void move_left(void)
{
    if (doc.cur)
        doc.cur--;
}

void move_right(void)
{
    if (doc.cur < doc.len)
        doc.cur++;
}

void move_top(void)
{
    doc.cur = 0;
}

void move_end(void)
{
    doc.cur = doc.len;
}

/* ------------------------------------------------------------ */

void show_document_screen(void)
{
    unsigned i, shown, start;

    clrscr();
    status_line();

    cprintf("\r\n");
    start = 0;
    shown = 0;

    for (i = start; i < doc.len && shown < 20; i++)
    {
        char ch = doc.data[i];

        if (ch == '\n')
        {
            cprintf("\r\n");
            shown++;
        }
        else if (ch == '\f')
            cprintf("^L");
        else
            cputch(ch);
    }

    cprintf("\r\n000,000      [W]ord  [H]ack  [R]emove  [SPACE BAR]    Command ?");
}

/* ------------------------------------------------------------ */

void help_screen(void)
{
    clrscr();
    cprintf("MSCRIPT Editor HELP!\r\n");
    cprintf("[ALT] B - BLOCK begin/end marker\r\n");
    cprintf("C - COPY a marked block\r\n");
    cprintf("D - DELETE char at cursor\r\n");
    cprintf("E - Position to document END\r\n");
    cprintf("F - Repeat last FIND command\r\n");
    cprintf("G - Imbed a printer code\r\n");
    cprintf("H - Display HELP! list\r\n");
    cprintf("I - Open an INSERT block\r\n");
    cprintf("M - MERGE two text blocks\r\n");
    cprintf("N - Imbed a NEW PAGE code\r\n");
    cprintf("P - Select PRINT menu\r\n");
    cprintf("Q - QUIT changes to line\r\n");
    cprintf("R - REMOVE a marked block\r\n");
    cprintf("S - Enter SUB-DELETE mode\r\n");
    cprintf("T - Position to document TOP\r\n");
    cprintf("U - UNMARK a marked block\r\n");
    cprintf("\r\nMSCRIPT 1.00 IBM Word processing software\r\n");
    cprintf("Copyright (c) 1984, Micro-Systems Software Inc.\r\n");
    pause_key();
}

/* ------------------------------------------------------------ */

int save_document(const char *name)
{
    FILE *fp;

    fp = fopen(name, "wb");
    if (!fp)
        return 0;

    if (doc.len)
        fwrite(doc.data, 1, doc.len, fp);

    fclose(fp);
    strncpy(doc.name, name, NAME_MAX - 1);
    doc.name[NAME_MAX - 1] = 0;
    return 1;
}

int load_document(const char *name)
{
    FILE *fp;
    size_t n;

    fp = fopen(name, "rb");
    if (!fp)
        return 0;

    doc.len = 0;
    doc.cur = 0;

    n = fread(doc.data, 1, DOC_MAX - 1, fp);
    doc.len = (unsigned)n;
    doc.data[doc.len] = 0;

    fclose(fp);
    strncpy(doc.name, name, NAME_MAX - 1);
    doc.name[NAME_MAX - 1] = 0;
    return 1;
}

int append_document(const char *name)
{
    FILE *fp;
    int ch;

    fp = fopen(name, "rb");
    if (!fp)
        return 0;

    while ((ch = fgetc(fp)) != EOF)
    {
        if (!insert_char_at(doc.len, ch))
        {
            fclose(fp);
            msg("Out of memory!");
            return 0;
        }
    }

    fclose(fp);
    return 1;
}

/* ------------------------------------------------------------ */

int find_text_forward(const char *needle)
{
    unsigned i, nlen;

    nlen = (unsigned)strlen(needle);
    if (!nlen)
        return 0;

    for (i = doc.cur; i + nlen <= doc.len; i++)
    {
        if (memcmp(doc.data + i, needle, nlen) == 0)
        {
            doc.cur = i;
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------ */

void set_mark(void)
{
    if (!doc.mark_active)
    {
        doc.mark_a = doc.cur;
        doc.mark_b = doc.cur;
        doc.mark_active = 1;
        msg("Block begin marker set.");
    }
    else
    {
        doc.mark_b = doc.cur;
        msg("Block end marker set.");
    }
}

void unmark_block(void)
{
    doc.mark_active = 0;
    msg("Block unmarked.");
}

int ordered_mark(unsigned *a, unsigned *b)
{
    if (!doc.mark_active)
        return 0;

    *a = doc.mark_a;
    *b = doc.mark_b;

    if (*a > *b)
    {
        unsigned t = *a;
        *a = *b;
        *b = t;
    }
    return (*a != *b);
}

void remove_block(void)
{
    unsigned a, b, i, count;

    if (!ordered_mark(&a, &b))
    {
        msg("Block marker error!");
        return;
    }

    count = b - a;
    for (i = a; i + count < doc.len; i++)
        doc.data[i] = doc.data[i + count];

    doc.len -= count;
    doc.data[doc.len] = 0;
    doc.cur = a;
    doc.mark_active = 0;
}

void copy_block(void)
{
    unsigned a, b, i, count;
    char temp[4096];

    if (!ordered_mark(&a, &b))
    {
        msg("Block marker error!");
        return;
    }

    count = b - a;
    if (count >= sizeof(temp))
        count = sizeof(temp) - 1;

    memcpy(temp, doc.data + a, count);
    temp[count] = 0;

    for (i = 0; i < count; i++)
    {
        if (!insert_char_at(doc.cur + i, temp[i]))
        {
            msg("Out of memory!");
            return;
        }
    }

    doc.cur += count;
}

void merge_blocks(void)
{
    msg("MERGE two text blocks not fully reconstructed.");
}

/* ------------------------------------------------------------ */

void input_line(prompt, out, size)
char *prompt, *out;
int size;
{
    int ch, pos = 0;

    cprintf("\r\n%s", prompt);

    while ((ch = getch()) != 13)
    {
        if (ch == 27)
        {
            out[0] = 0;
            cprintf("\r\n");
            return;
        }
        if (ch == 8)
        {
            if (pos > 0)
            {
                pos--;
                cprintf("\b \b");
            }
            continue;
        }
        if (ch >= ' ' && pos < size - 1)
        {
            out[pos++] = (char)ch;
            cputch(ch);
        }
    }

    out[pos] = 0;
    cprintf("\r\n");
}

/* ------------------------------------------------------------ */

void show_command_menu(void)
{
    clrscr();
    cprintf("MSCRIPT Command Menu\r\n");
    cprintf("Append a document\r\n");
    cprintf("A docname\r\n");
    cprintf("Change FIND text strings\r\n");
    cprintf("C [text]\r\n");
    cprintf("Exit MSCRIPT to DOS\r\n");
    cprintf("Find matching text string\r\n");
    cprintf("F [text]\r\n");
    cprintf("Set ink/paper colors\r\n");
    cprintf("I ink,paper\r\n");
    cprintf("Load a new document\r\n");
    cprintf("L docname\r\n");
    cprintf("Restart MSCRIPT\r\n");
    cprintf("Save current document\r\n");
    cprintf("S [docname]\r\n");
    cprintf("Change tab positions\r\n");
    cprintf("T [pos,...]\r\n");
    cprintf("Adjust window width\r\n");
    cprintf("W width\r\n");
    cprintf("\r\nCharacters used    %u\r\n", doc.len);
    cprintf("Characters free    %u\r\n", chars_free());
    cprintf("Window width       %u\r\n", doc.window_width);
    cprintf("Words in document  %u\r\n", words_in_doc());
    cprintf("Lines in document  %u\r\n", lines_in_doc());
    cprintf("Document name      %s\r\n", doc.name);
    cprintf("\r\nEnter command as shown or press ESC to return\r\n");
}

void command_menu(void)
{
    char line[LINEBUF_MAX];
    char arg[LINEBUF_MAX];

    show_command_menu();
    input_line("", line, sizeof(line));
    if (!line[0])
        return;

    if ((line[0] == 'A' || line[0] == 'a') && line[1] == ' ')
    {
        strcpy(arg, line + 2);
        if (!append_document(arg))
            msg("File not found!");
    }
    else if ((line[0] == 'C' || line[0] == 'c') && line[1] == ' ')
    {
        strcpy(doc.find_text, line + 2);
    }
    else if (line[0] == 'E' || line[0] == 'e')
    {
        if (confirm_quit())
            exit(0);
    }
    else if ((line[0] == 'F' || line[0] == 'f') && line[1] == ' ')
    {
        strcpy(doc.find_text, line + 2);
        if (!find_text_forward(doc.find_text))
            msg("String not found!");
    }
    else if ((line[0] == 'I' || line[0] == 'i') && line[1] == ' ')
    {
        int ink = 0, paper = 0;
        sscanf(line + 2, "%d,%d", &ink, &paper);
        doc.ink = ink;
        doc.paper = paper;
    }
    else if ((line[0] == 'L' || line[0] == 'l') && line[1] == ' ')
    {
        strcpy(arg, line + 2);
        if (!load_document(arg))
            msg("File not found!");
    }
    else if (line[0] == 'R' || line[0] == 'r')
    {
        init_doc();
    }
    else if (line[0] == 'S' || line[0] == 's')
    {
        if (line[1] == ' ')
            strcpy(arg, line + 2);
        else
            strcpy(arg, doc.name);

        if (!save_document(arg))
            msg("Disk I/O error!");
    }
    else if ((line[0] == 'T' || line[0] == 't') && line[1] == ' ')
    {
        char *p = strtok(line + 2, ",");
        doc.tab_count = 0;
        while (p && doc.tab_count < TAB_MAX)
        {
            doc.tabs[doc.tab_count++] = atoi(p);
            p = strtok(NULL, ",");
        }
    }
    else if ((line[0] == 'W' || line[0] == 'w') && line[1] == ' ')
    {
        doc.window_width = (unsigned)atoi(line + 2);
    }
}

/* ------------------------------------------------------------ */

void print_document_to(FILE *fp, int from_cursor)
{
    unsigned i;
    unsigned start;
    unsigned line_pos = 0;
    unsigned page_line = 0;
    int c;

    start = from_cursor ? doc.cur : 0;

    for (i = start; i < doc.len; i++)
    {
        c = (unsigned char)doc.data[i];

        if (c == '\f')
        {
            fputc('\f', fp);
            page_line = 0;
            line_pos = 0;
            continue;
        }

        if (c == '\n')
        {
            fputc('\n', fp);
            page_line += doc.prn.line_spacing;
            line_pos = 0;

            if (doc.prn.page_length > 0 && page_line >= (unsigned)doc.prn.page_length)
            {
                if (!doc.prn.single_sheet)
                    fputc('\f', fp);
                page_line = 0;
            }
            continue;
        }

        if (line_pos == 0)
        {
            int lm;
            for (lm = 0; lm < doc.prn.left_margin; lm++)
                fputc(' ', fp);
            line_pos = doc.prn.left_margin;
        }

        fputc(c, fp);
        line_pos++;

        if (doc.prn.line_length > 0 && line_pos >= (unsigned)doc.prn.line_length)
        {
            fputc('\n', fp);
            page_line += doc.prn.line_spacing;
            line_pos = 0;
        }
    }
}

void show_print_menu(void)
{
    clrscr();
    cprintf("MSCRIPT Print Menu\r\n");
    cprintf("JU - Justification\r\n");
    cprintf("SS - Single sheet\r\n");
    cprintf("PL - Page length\r\n");
    cprintf("PS - Page spacing\r\n");
    cprintf("LM - Left margin\r\n");
    cprintf("LL - Line length\r\n");
    cprintf("LS - Line spacing\r\n");
    cprintf("PN - Page number\r\n");
    cprintf("PD - Print document to a device\r\n");
    cprintf("PD devicename\r\n");
    cprintf("PF - Print document from cursor\r\n");
    cprintf("PF devicename\r\n");
    cprintf("\r\nEnter two letter command with required parameter to\r\n");
    cprintf("change setting (example: JU=N will turn justification\r\n");
    cprintf("off and PN=15 will set the starting page to 15)\r\n");
    cprintf("Press ENTER to print document or ESC to return\r\n");
}

void print_menu(void)
{
    char line[LINEBUF_MAX];
    char device[LINEBUF_MAX];
    FILE *fp;

    show_print_menu();
    input_line("", line, sizeof(line));
    if (!line[0])
        return;

    if (!strnicmp(line, "JU=", 3))
        doc.prn.justification = (toupper(line[3]) == 'Y');
    else if (!strnicmp(line, "SS=", 3))
        doc.prn.single_sheet = (toupper(line[3]) == 'Y');
    else if (!strnicmp(line, "PL=", 3))
        doc.prn.page_length = atoi(line + 3);
    else if (!strnicmp(line, "PS=", 3))
        doc.prn.page_spacing = atoi(line + 3);
    else if (!strnicmp(line, "LM=", 3))
        doc.prn.left_margin = atoi(line + 3);
    else if (!strnicmp(line, "LL=", 3))
        doc.prn.line_length = atoi(line + 3);
    else if (!strnicmp(line, "LS=", 3))
        doc.prn.line_spacing = atoi(line + 3);
    else if (!strnicmp(line, "PN=", 3))
        doc.prn.page_number = atoi(line + 3);
    else if (!strnicmp(line, "PD ", 3))
    {
        strcpy(device, line + 3);
        fp = fopen(device, "wt");
        if (!fp) { msg("Disk I/O error!"); return; }
        print_document_to(fp, 0);
        fclose(fp);
    }
    else if (!strnicmp(line, "PF ", 3))
    {
        strcpy(device, line + 3);
        fp = fopen(device, "wt");
        if (!fp) { msg("Disk I/O error!"); return; }
        print_document_to(fp, 1);
        fclose(fp);
    }
}

/* ------------------------------------------------------------ */

void imbed_printer_code(void)
{
    char line[32];
    int code = 0;

    input_line("Printer code: ", line, sizeof(line));
    code = atoi(line);

    if (!insert_char_at(doc.cur, code))
        msg("Out of memory!");
    else
        doc.cur++;
}

void imbed_new_page(void)
{
    if (!insert_char_at(doc.cur, '\f'))
        msg("Out of memory!");
    else
        doc.cur++;
}

/* ------------------------------------------------------------ */

void editor_loop(void)
{
    int ch, ext;

    for (;;)
    {
        show_document_screen();
        ch = getch();

        if (ch == 0 || ch == 224)
        {
            ext = getch();

            if (ext == 75) move_left();         /* left */
            else if (ext == 77) move_right();   /* right */
            else if (ext == 71) move_top();     /* home */
            else if (ext == 79) move_end();     /* end */
            else if (ext == 35) help_screen();  /* Alt-H commonly */
            else if (ext == 48) set_mark();     /* Alt-B approximate */
            continue;
        }

        switch (ch)
        {
            case 27:
                command_menu();
                break;

            case 8:
                backspace_char();
                break;

            case 13:
                type_char('\n');
                break;

            case 'C': case 'c':
                copy_block();
                break;

            case 'D': case 'd':
                delete_under_cursor();
                break;

            case 'E': case 'e':
                move_end();
                break;

            case 'F': case 'f':
                if (doc.find_text[0])
                {
                    if (!find_text_forward(doc.find_text))
                        msg("String not found!");
                }
                break;

            case 'G': case 'g':
                imbed_printer_code();
                break;

            case 'H': case 'h':
                help_screen();
                break;

            case 'I': case 'i':
                doc.insert_block = !doc.insert_block;
                break;

            case 'M': case 'm':
                merge_blocks();
                break;

            case 'N': case 'n':
                imbed_new_page();
                break;

            case 'P': case 'p':
                print_menu();
                break;

            case 'Q': case 'q':
                msg("QUIT changes to line not fully reconstructed.");
                break;

            case 'R': case 'r':
                remove_block();
                break;

            case 'S': case 's':
                doc.sub_delete = !doc.sub_delete;
                break;

            case 'T': case 't':
                move_top();
                break;

            case 'U': case 'u':
                unmark_block();
                break;

            default:
                if (ch >= ' ')
                    type_char(ch);
                break;
        }
    }
}

/* ------------------------------------------------------------ */

int main(void)
{
    init_doc();

    clrscr();
    cprintf("MSCRIPT 1.00 IBM Word processing software\r\n");
    cprintf("Copyright (c) 1984, Micro-Systems Software Inc.\r\n");
    pause_key();

    editor_loop();
    return 0;
}
