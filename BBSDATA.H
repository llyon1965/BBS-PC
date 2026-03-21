/* BBSDATA.H
 *
 * BBS-PC! 4.21
 *
 * Core data structures, on-disk layouts, runtime state
 */

#ifndef BBSDATA_H
#define BBSDATA_H

#include <stdio.h>
#include <time.h>

/* ------------------------------------------------------------ */
/* basic types                                                  */
/* ------------------------------------------------------------ */

#ifndef byte
typedef unsigned char  byte;
#endif

#ifndef ushort
typedef unsigned short ushort;
#endif

#ifndef ulong
typedef unsigned long  ulong;
#endif

#ifndef bits
typedef unsigned short bits;
#endif

/* ------------------------------------------------------------ */
/* constants                                                    */
/* ------------------------------------------------------------ */

#define HDRLEN              128L
#define MAX_PATHNAME        128

#define NUM_SECT            16
#define NUM_MENUSET         2
#define NUM_TERM_TYPES      8

#define MAX_REPLY_LINKS     16
#define MAX_MENU_ITEMS      64
#define MAX_MENU_STACK      16

#define NAME_LEN            35
#define PWD_LEN             15
#define CITY_LEN            24
#define SUBJ_LEN            72
#define CAT_LEN             40
#define TEXT_LEN            128

#define MENU_TEXT_LEN       80
#define MENU_PROMPT_LEN     80
#define NODE_MENU_LEN       12

#define DISC_REASON_LEN     24
#define SECT_NAME_LEN       40
#define TERM_NAME_LEN       20

/* ------------------------------------------------------------ */
/* modem                                                        */
/* ------------------------------------------------------------ */

typedef struct {
    int port;
    int baud;
    int parity;        /* 'N','E','O' */
    int data_bits;     /* 7/8 */
    int stop_bits;     /* 1/2 */
} PARAMS;

/* ------------------------------------------------------------ */
/* paths                                                        */
/* ------------------------------------------------------------ */

typedef struct {
    char msg_path[MAX_PATHNAME];
    char usr_path[MAX_PATHNAME];
    char ud_path[MAX_PATHNAME];
    char log_path[MAX_PATHNAME];
    char updn_path[NUM_SECT][MAX_PATHNAME];
} BBSPATHS;

/* ------------------------------------------------------------ */
/* configuration                                                */
/* ------------------------------------------------------------ */

typedef struct {
    char bbsname[41];
    char sysopname[41];
    char sysop_pass[21];

    int  node;
    int  min_baud;
    int  max_baud;
    int  page_len;
    int  max_nodes;

    /* NEW: modem defaults */
    int  modem_baud;
    int  modem_parity;      /* 'N','E','O' */
    int  modem_data_bits;   /* 7/8 */
    int  modem_stop_bits;   /* 1/2 */

    ushort limit[2];
    byte   priv[2];
    ushort idle_limit;

    char sect_name[NUM_SECT][SECT_NAME_LEN + 1];
    char term_name[NUM_TERM_TYPES][TERM_NAME_LEN + 1];

    bits rd_acc[NUM_SECT];
    bits wr_acc[NUM_SECT];
    bits up_acc[NUM_SECT];
    bits dn_acc[NUM_SECT];
} CFGINFO;

/* ------------------------------------------------------------ */
/* menu                                                         */
/* ------------------------------------------------------------ */

typedef struct {
    char key;
    char text[MENU_TEXT_LEN + 1];
    char parm[MAX_PATHNAME];
    byte function;
    byte access_mode;
} MENUITEM;

typedef struct {
    char filename[MAX_PATHNAME];
    char prompt[MENU_PROMPT_LEN + 1];
    int  item_count;
    MENUITEM item[MAX_MENU_ITEMS];
} MENUFILE;

typedef struct {
    int sp;
    MENUFILE menu[MAX_MENU_STACK];
} MENUSTACK;

/* ------------------------------------------------------------ */
/* on-disk records                                              */
/* ------------------------------------------------------------ */

typedef struct {
    char   name[NAME_LEN + 1];
    char   pwd[PWD_LEN + 1];
    char   city[CITY_LEN + 1];

    ushort lastdate;
    ushort lasttime;

    ushort calls;
    ushort uploads;
    ushort downloads;

    long   highmsgread;

    bits   rd_acc;
    bits   wr_acc;
    bits   up_acc;
    bits   dn_acc;
    bits   sys_acc;
    bits   sect_mask;
    bits   bull_mask;

    ushort time_limit;
    byte   menu_set;

    byte   protocol;
    byte   expert;
    byte   seclevel;
    byte   dnldratio;
} USERREC;

typedef struct {
    long   number;
    long   replyto;
    long   replys[MAX_REPLY_LINKS];

    ushort msgptr;
    ushort date;
    ushort time;

    byte   section;

    char   from[NAME_LEN + 1];
    char   to[NAME_LEN + 1];
    char   subject[SUBJ_LEN + 1];
} MSGREC;

typedef struct {
    char cat_name[CAT_LEN + 1];
    byte section;
} UDREC;

typedef struct {
    byte   status;
    byte   wantchat;
    ushort baud;
    byte   page_sysop;

    char   user_name[NAME_LEN + 1];
    char   user_city[CITY_LEN + 1];
    char   current_menu[NODE_MENU_LEN + 1];
} NODEREC;

typedef struct {
    ulong  caller_no;
    byte   node_no;

    ushort baud;
    char   parity;
    byte   data_bits;
    byte   stop_bits;

    char   name[NAME_LEN + 1];
    char   city[CITY_LEN + 1];

    ushort in_date;
    ushort in_time;
    ushort out_date;
    ushort out_time;

    char   disc_reason[DISC_REASON_LEN + 1];

    ushort msgs_left;
    ushort uploads;
    ushort downloads;
} USRLOG;

typedef struct {
    char text[TEXT_LEN];
} MSGTEXT;

/* ------------------------------------------------------------ */
/* runtime                                                      */
/* ------------------------------------------------------------ */

typedef struct {
    char   name[NAME_LEN + 1];
    char   pwd[PWD_LEN + 1];
    char   city[CITY_LEN + 1];

    ushort lastdate;
    ushort lasttime;

    ushort calls;
    ushort uploads;
    ushort downloads;

    long   highmsgread;

    bits   rd_acc;
    bits   wr_acc;
    bits   up_acc;
    bits   dn_acc;
    bits   sys_acc;
    bits   sect_mask;
    bits   bull_mask;

    ushort time_limit;
    byte   menu_set;

    byte   protocol;
    byte   expert;
    byte   seclevel;
    byte   dnldratio;
} USRDESC;

typedef struct {
    int status;
    int wantchat;
    int baud;
    int page_sysop;

    char user_name[NAME_LEN + 1];
    char user_city[CITY_LEN + 1];
    char current_menu[NODE_MENU_LEN + 1];
} NODEINFO;

typedef struct {
    USRDESC user;

    int running;
    int node;
    int local_login;
    int logged_in;
    int expert;

    ulong logon_unix;
    ulong last_activity_unix;

    int base_minutes_allowed;
    int bonus_minutes;
    int ratio_off;
    int last_time_warning;

    long caller_log_recno;
    ulong caller_no;

    char disconnect_reason[DISC_REASON_LEN + 1];

    MENUSTACK mstack;
} BBSSESSION;

/* ------------------------------------------------------------ */
/* globals                                                      */
/* ------------------------------------------------------------ */

extern CFGINFO    g_cfg;
extern BBSPATHS   g_paths;
extern BBSSESSION g_sess;
extern NODEINFO   g_node;
extern PARAMS     g_modem;

extern FILE *g_usrfp;
extern FILE *g_msgfp;
extern FILE *g_txtfp;
extern FILE *g_udfp;
extern FILE *g_logfp;

/* ------------------------------------------------------------ */
/* record sizes                                                 */
/* ------------------------------------------------------------ */

#define USERREC_SIZE   ((int)sizeof(USERREC))
#define MSGREC_SIZE    ((int)sizeof(MSGREC))
#define UDREC_SIZE     ((int)sizeof(UDREC))
#define NODEREC_SIZE   ((int)sizeof(NODEREC))
#define USRLOG_SIZE    ((int)sizeof(USRLOG))
#define MSGTEXT_SIZE   ((int)sizeof(MSGTEXT))

#endif