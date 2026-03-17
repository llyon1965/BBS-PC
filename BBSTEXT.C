/* BBSTEXT.C
 *
 * Text-file display and simple screen helpers
 */

#include <stdio.h>
#include <string.h>

#include "bbsdata.h"
#include "bbsfunc.h"

void bbs_cls(void)
{
    int i;
    for (i = 0; i < 25; i++)
        puts("");
}

void bbs_pause(void)
{
    puts("");
    puts("Press [RETURN]");
    while (getchar() != '\n')
        ;
}

int type_text_file(fname, pause_flag)
char *fname;
int pause_flag;
{
    FILE *fp;
    char line[MAX_TEXT_LINE];

    fp = fopen(fname, "rt");
    if (!fp)
    {
        printf("Can't open %s\n", fname);
        return 0;
    }

    while (fgets(line, sizeof(line), fp))
        fputs(line, stdout);

    fclose(fp);

    if (pause_flag)
        bbs_pause();

    return 1;
}

int cls_type_text_file(fname, pause_flag)
char *fname;
int pause_flag;
{
    bbs_cls();
    return type_text_file(fname, pause_flag);
}

/* ------------------------------------------------------------ */
/* stubs                                                        */
/* ------------------------------------------------------------ */

void login_user(void)              { puts("login_user not implemented"); }
void logout_user(void)             { puts("logout_user not implemented"); }
void do_user_statistics(void)      { puts("User statistics"); }
void do_change_section_mask(void)  { puts("Change section mask"); }
void do_user_edit(void)            { puts("Change/examine user record"); }
void do_print_caller_log(void)     { puts("Print caller log"); }
void do_time_on_system(void)       { printf("Time left: %d minutes\n", g_sess.time_left); }
void do_chat_with_sysop(void)      { puts("Chat with SYSOP"); }
void do_expert_toggle(void)
{
    g_sess.expert = !g_sess.expert;
    printf("Expert mode %s\n", g_sess.expert ? "ON" : "OFF");
}
void do_leave_message(void)        { puts("Leave a message"); }
void do_read_messages(void)        { puts("Read messages"); }
void do_scan_messages(void)        { puts("Scan messages"); }
void do_print_catalog(void)        { puts("Print file catalog"); }
void do_browse_files(void)         { puts("Browse files"); }
void do_upload_file(void)          { puts("Upload a file"); }
void do_download_file(void)        { puts("Download a file"); }
void do_read_file(void)            { puts("Read a file"); }