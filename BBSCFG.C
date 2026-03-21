/* BBSCFG.C
 *
 * BBS-PC! 4.21
 *
 * Sysop configuration editors.
 *
 * Owns:
 * - do_define_section_names()
 * - do_define_terminal_types()
 * - do_modem_defaults()
 * - do_user_defaults()
 * - do_system_defaults()
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "bbsdata.h"
#include "bbsfunc.h"

void do_define_section_names(void)
{
    char line[64];
    int sec;

    if (!sysop_password_prompt())
        return;

    for (;;)
    {
        puts("");
        puts("Section Names");
        puts("-------------");
        puts("");

        for (sec = 0; sec < NUM_SECT; sec++)
            printf("%2d  %s\n",
                   sec,
                   g_cfg.sect_name[sec][0] ? g_cfg.sect_name[sec] : "(unnamed)");

        puts("");
        term_getline("Section to edit (blank to quit): ", line, sizeof(line));
        data_trim_crlf(line);

        if (!line[0])
            break;

        sec = atoi(line);
        if (sec < 0 || sec >= NUM_SECT)
        {
            puts("Invalid section");
            continue;
        }

        term_getline("New section name: ",
                     g_cfg.sect_name[sec],
                     sizeof(g_cfg.sect_name[sec]));
        data_trim_crlf(g_cfg.sect_name[sec]);

        if (!save_cfginfo("CFGINFO.DAT"))
        {
            puts("Unable to save configuration");
            return;
        }
    }
}

void do_define_terminal_types(void)
{
    char line[64];
    int termno;

    if (!sysop_password_prompt())
        return;

    for (;;)
    {
        puts("");
        puts("Terminal Types");
        puts("--------------");
        puts("");

        for (termno = 0; termno < NUM_TERM_TYPES; termno++)
            printf("%2d  %s\n",
                   termno,
                   g_cfg.term_name[termno][0] ? g_cfg.term_name[termno] : "(unnamed)");

        puts("");
        term_getline("Terminal type to edit (blank to quit): ", line, sizeof(line));
        data_trim_crlf(line);

        if (!line[0])
            break;

        termno = atoi(line);
        if (termno < 0 || termno >= NUM_TERM_TYPES)
        {
            puts("Invalid terminal type");
            continue;
        }

        term_getline("New terminal type name: ",
                     g_cfg.term_name[termno],
                     sizeof(g_cfg.term_name[termno]));
        data_trim_crlf(g_cfg.term_name[termno]);

        if (!save_cfginfo("CFGINFO.DAT"))
        {
            puts("Unable to save configuration");
            return;
        }
    }
}

void do_modem_defaults(void)
{
    char line[64];
    int val;

    if (!sysop_password_prompt())
        return;

    for (;;)
    {
        puts("");
        puts("Modem Defaults");
        puts("--------------");
        puts("");
        printf("1  Baud      : %d\n", g_cfg.modem_baud);
        printf("2  Parity    : %c\n",
               g_cfg.modem_parity ? g_cfg.modem_parity : 'N');
        printf("3  Data bits : %d\n", g_cfg.modem_data_bits);
        printf("4  Stop bits : %d\n", g_cfg.modem_stop_bits);
        puts("");

        term_getline("Item to edit (blank to quit): ", line, sizeof(line));
        data_trim_crlf(line);

        if (!line[0])
            break;

        switch (atoi(line))
        {
            case 1:
                term_getline("New baud: ", line, sizeof(line));
                data_trim_crlf(line);
                if (line[0])
                {
                    val = atoi(line);
                    if (val > 0)
                        g_cfg.modem_baud = val;
                }
                break;

            case 2:
                term_getline("New parity (N/E/O): ", line, sizeof(line));
                data_trim_crlf(line);
                if (line[0])
                    g_cfg.modem_parity = toupper((unsigned char)line[0]);

                if (g_cfg.modem_parity != 'N' &&
                    g_cfg.modem_parity != 'E' &&
                    g_cfg.modem_parity != 'O')
                    g_cfg.modem_parity = 'N';
                break;

            case 3:
                term_getline("New data bits (7/8): ", line, sizeof(line));
                data_trim_crlf(line);
                if (line[0])
                    g_cfg.modem_data_bits = atoi(line);

                if (g_cfg.modem_data_bits != 7 &&
                    g_cfg.modem_data_bits != 8)
                    g_cfg.modem_data_bits = 8;
                break;

            case 4:
                term_getline("New stop bits (1/2): ", line, sizeof(line));
                data_trim_crlf(line);
                if (line[0])
                    g_cfg.modem_stop_bits = atoi(line);

                if (g_cfg.modem_stop_bits != 1 &&
                    g_cfg.modem_stop_bits != 2)
                    g_cfg.modem_stop_bits = 1;
                break;

            default:
                puts("Invalid selection");
                continue;
        }

        if (!save_cfginfo("CFGINFO.DAT"))
        {
            puts("Unable to save configuration");
            return;
        }
    }
}

void do_user_defaults(void)
{
    char line[64];
    int val;

    if (!sysop_password_prompt())
        return;

    for (;;)
    {
        puts("");
        puts("User Defaults");
        puts("-------------");
        puts("");
        printf("1  Guest time limit   : %d minutes\n", g_cfg.limit[0]);
        printf("2  Member time limit  : %d minutes\n", g_cfg.limit[1]);
        printf("3  Guest privilege    : %d\n", g_cfg.priv[0]);
        printf("4  Member privilege   : %d\n", g_cfg.priv[1]);
        puts("");

        term_getline("Item to edit (blank to quit): ", line, sizeof(line));
        data_trim_crlf(line);

        if (!line[0])
            break;

        switch (atoi(line))
        {
            case 1:
                term_getline("New guest time limit (minutes): ", line, sizeof(line));
                data_trim_crlf(line);
                if (line[0])
                {
                    val = atoi(line);
                    if (val >= 0)
                        g_cfg.limit[0] = (ushort)val;
                }
                break;

            case 2:
                term_getline("New member time limit (minutes): ", line, sizeof(line));
                data_trim_crlf(line);
                if (line[0])
                {
                    val = atoi(line);
                    if (val >= 0)
                        g_cfg.limit[1] = (ushort)val;
                }
                break;

            case 3:
                term_getline("New guest privilege (0-255): ", line, sizeof(line));
                data_trim_crlf(line);
                if (line[0])
                {
                    val = atoi(line);
                    if (val >= 0 && val <= 255)
                        g_cfg.priv[0] = (byte)val;
                }
                break;

            case 4:
                term_getline("New member privilege (0-255): ", line, sizeof(line));
                data_trim_crlf(line);
                if (line[0])
                {
                    val = atoi(line);
                    if (val >= 0 && val <= 255)
                        g_cfg.priv[1] = (byte)val;
                }
                break;

            default:
                puts("Invalid selection");
                continue;
        }

        if (!save_cfginfo("CFGINFO.DAT"))
        {
            puts("Unable to save configuration");
            return;
        }
    }
}

void do_system_defaults(void)
{
    char line[128];
    int val;

    if (!sysop_password_prompt())
        return;

    for (;;)
    {
        puts("");
        puts("System Defaults");
        puts("---------------");
        puts("");
        printf("1  BBS name      : %s\n", g_cfg.bbsname);
        printf("2  Sysop name    : %s\n", g_cfg.sysopname);
        printf("3  Sysop password: %s\n", g_cfg.sysop_pass[0] ? "(set)" : "(blank)");
        printf("4  Min baud      : %d\n", g_cfg.min_baud);
        printf("5  Max baud      : %d\n", g_cfg.max_baud);
        printf("6  Page length   : %d\n", g_cfg.page_len);
        printf("7  Max nodes     : %d\n", g_cfg.max_nodes);
        printf("8  Idle timeout  : %u\n", (unsigned)g_cfg.idle_limit);
        puts("");

        term_getline("Item to edit (blank to quit): ", line, sizeof(line));
        data_trim_crlf(line);

        if (!line[0])
            break;

        switch (atoi(line))
        {
            case 1:
                term_getline("New BBS name: ", g_cfg.bbsname, sizeof(g_cfg.bbsname));
                data_trim_crlf(g_cfg.bbsname);
                break;

            case 2:
                term_getline("New sysop name: ", g_cfg.sysopname, sizeof(g_cfg.sysopname));
                data_trim_crlf(g_cfg.sysopname);
                break;

            case 3:
                term_getline_hidden("New sysop password: ", g_cfg.sysop_pass, sizeof(g_cfg.sysop_pass));
                data_trim_crlf(g_cfg.sysop_pass);
                break;

            case 4:
                term_getline("New minimum baud: ", line, sizeof(line));
                data_trim_crlf(line);
                if (line[0])
                {
                    val = atoi(line);
                    if (val > 0)
                        g_cfg.min_baud = val;
                }
                break;

            case 5:
                term_getline("New maximum baud: ", line, sizeof(line));
                data_trim_crlf(line);
                if (line[0])
                {
                    val = atoi(line);
                    if (val > 0)
                        g_cfg.max_baud = val;
                }
                break;

            case 6:
                term_getline("New page length: ", line, sizeof(line));
                data_trim_crlf(line);
                if (line[0])
                {
                    val = atoi(line);
                    if (val > 0)
                        g_cfg.page_len = val;
                }
                break;

            case 7:
                term_getline("New max nodes: ", line, sizeof(line));
                data_trim_crlf(line);
                if (line[0])
                {
                    val = atoi(line);
                    if (val > 0)
                        g_cfg.max_nodes = val;
                }
                break;

            case 8:
                term_getline("New idle timeout (minutes, 0=off): ", line, sizeof(line));
                data_trim_crlf(line);
                if (line[0])
                {
                    val = atoi(line);
                    if (val >= 0)
                        g_cfg.idle_limit = (ushort)val;
                }
                break;

            default:
                puts("Invalid selection");
                continue;
        }

        if (g_cfg.min_baud > g_cfg.max_baud)
        {
            val = g_cfg.min_baud;
            g_cfg.min_baud = g_cfg.max_baud;
            g_cfg.max_baud = val;
        }

        if (g_cfg.page_len <= 0)
            g_cfg.page_len = 24;

        if (g_cfg.max_nodes <= 0)
            g_cfg.max_nodes = 1;

        if (!save_cfginfo("CFGINFO.DAT"))
        {
            puts("Unable to save configuration");
            return;
        }
    }
}