/* BBSSTUB.C
 *
 * BBS-PC! 4.21
 *
 * Future feature placeholders and legacy unimplemented functions.
 *
 * IMPORTANT:
 * No completed functionality should remain in this file.
 * Functions are kept here either:
 *  - for future roadmap implementation
 *  - or as historical compatibility stubs
 */

#include <stdio.h>

#include "bbsdata.h"
#include "bbsfunc.h"

/* ------------------------------------------------------------ */
/* Legacy dial / phone directory functionality                  */
/* (Retained for historical compatibility - not implemented)    */
/* ------------------------------------------------------------ */

void do_callback_user(void)
{
    puts("Callback user not supported");
}

void do_list_phone_directory(void)
{
    puts("Phone directory not supported");
}

void do_change_phone_listing(void)
{
    puts("Phone listing changes not supported");
}

void do_dial_connect_remote(void)
{
    puts("Dial-out not supported");
}

void do_unlisted_dial_connect(void)
{
    puts("Unlisted dial-out not supported");
}