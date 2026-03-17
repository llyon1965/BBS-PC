/* BBSDATA.H
 *
 * BBS-PC 4.20 reconstructed common data/header definitions
 *
 * Updated:
 * - USRDESC.name changed from char name[NAME_LEN] to char name[NAME_LEN+1]
 */

#ifndef BBSDATA_H
#define BBSDATA_H

#include <stdio.h>
#include <time.h>

/* ------------------------------------------------------------ */
/* basic types                                                  */
/* ------------------------------------------------------------ */

#ifndef BYTE_TYPEDEF_DONE
#define BYTE_TYPEDEF_DONE
typedef unsigned char  byte;
typedef unsigned short ushort;
typedef unsigned long  ulong;
typedef signed char    shortint;
#endif

#ifndef BOOL_TYPEDEF_DONE
#define BOOL_TYPEDEF_DONE
typedef int BOOL;
#endif

#ifndef TRUE
#define TRUE  1
#define FALSE 0
#endif

#ifndef NULL
#define NULL 0
#endif

/* ------------------------------------------------------------ */
/* general constants                                            */
/* ------------------------------------------------------------ */

#define NUM_SECT        16
#define MAX_MENU_ITEMS  64
#define MAX_MENU_STACK  16
#define MAX_PATHNAME    80
#define MAX_PROTO       16
#define MAX_NODES       99

#define NAME_LEN        24
#define LOC_LEN         24
#define PHONE_LEN       12
#define PASS_LEN        12
#define SECT_LEN        24
#define TERM_LEN        20
#define CAT_LEN         40
#define FILE_LEN        12
#define DESC_LEN        58
#define MENU_PROMPT_LEN 80
#define MENU_TEXT_LEN   96
#define MENU_PARM_LEN   32

/* ------------------------------------------------------------ */
/* menu record types / opcodes                                  */
/* ------------------------------------------------------------ */

#define MEN_TYPE_TITLE  0
#define MEN_TYPE_ITEM   1
#define MEN_TYPE_IRET   2

#define MF_USER_STATS       0
#define MF_SECTION_NAMES    1
#define MF_CHANGE_MASK      2
#define MF_REGISTER         3
#define MF_USER_EDIT        4
#define MF_CALLER_LOG       5
#define MF_TIME_ON_SYS      6
#define MF_CHAT_SYSOP       7
#define MF_EXPERT_TOGGLE    8

#define MF_LEAVE_MSG        9
#define MF_LEAVE_SYSOP      10
#define MF_READ_MSGS        11
#define MF_READ_EMAIL       12
#define MF_SCAN_MSGS        13
#define MF_DELETE_MSG       14
#define MF_PRINT_CAT        15
#define MF_BROWSE_FILES     16
#define MF_UPLOAD_FILE      17
#define MF_UPLOAD_LOCAL     18
#define MF_DOWNLOAD_FILE    19
#define MF_READ_FILE        20
#define MF_KILL_FILE        21

#define MF_ADD_USER         22
#define MF_DEL_USER         23
#define MF_CHG_USER         24
#define MF_PURGE_USERS      25
#define MF_PURGE_MSGS       26
#define MF_PRINT_USERS      27
#define MF_RESET_BULL       28
#define MF_UPDATE_DEF       29
#define MF_DEF_SECTIONS     30
#define MF_DEF_TERMS        31
#define MF_MODEM_DEF        32
#define MF_USER_DEF         33
#define MF_SYS_DEF          34
#define MF_NODE_DEF         35

#define MF_LIST_PHONE       36
#define MF_CHG_PHONE        37
#define MF_DIAL_REMOTE      38
#define MF_DIAL_UNLISTED    39
#define MF_UPLOAD_DIRECT    40
#define MF_DNL_DIRECT       41
#define MF_KILL_DIRECT      42
#define MF_DOS_GATE         43
#define MF_SRCH_CATDESC     44
#define MF_NEW_FILES        45

#define MF_EXIT_SYS         100
#define MF_SHOW_MENU        101
#define MF_RET_LEVELS       102
#define MF_RET_TOP          103
#define MF_CALL_MENU        104
#define MF_GOTO_MENU        105
#define MF_CLS_TYPE         106
#define MF_TYPE_FILE        107
#define MF_CLS_TYPE_NS      108
#define MF_TYPE_FILE_NS     109
#define MF_CHG_MENUSET      110
#define MF_EXEC_EXT         111

/* ------------------------------------------------------------ */
/* user / config / runtime records                              */
/* ------------------------------------------------------------ */

typedef struct {
    char      name[NAME_LEN+1];
    char      loc[LOC_LEN+1];
    char      phone[PHONE_LEN+1];
    char      pass[PASS_LEN+1];

    byte      priv;
    byte      rd_acc;
    byte      wr_acc;
    byte      up_acc;
    byte      dn_acc;
    byte      sav_sec;
    byte      menu;
    byte      term;
    byte      protocol;
    byte      expert;
    byte      ansi;
    byte      nokill;
    byte      reserved1;

    shortint  limit;
    shortint  time_rem;
    shortint  width;
    shortint  length;
    shortint  nuls;
    shortint  last_call_slot;

    ushort    calls_total;
    ushort    msg_total;
    ushort    upl_total;
    ushort    dnl_total;
    ushort    bul_flg;

    ulong     last_call;
    ulong     high_msg;

    ushort    sect_mask;
    ushort    flags;

    char      reserved[32];
} USRDESC;

typedef struct {
    char      sec_name[NUM_SECT][SECT_LEN+1];

    byte      priv[10];
    byte      rd_acc[10];
    byte      wr_acc[10];
    byte      up_acc[10];
    byte      dn_acc[10];
    byte      sav_sec[10];
    byte      menu[10];
    shortint  limit[10];

    char      syspass[PASS_LEN+1];
    char      bbs_name[40];
    char      sysop_name[40];

    ushort    max_msg;
    ushort    max_user;
    ushort    max_ud;
    ushort    max_log;

    ushort    node_num;
    ushort    total_nodes;

    char      reserved[128];
} CFGINFO;

typedef struct {
    ulong     number;
    char      from[NAME_LEN+1];
    char      to[NAME_LEN+1];
    char      subj[40];
    ulong     date;
    ushort    flags;
    ushort    text_start;
    ushort    text_count;
    byte      section;
    byte      reserved1;
    char      reserved[16];
} MSGHEAD;

typedef struct {
    char      text[80];
} MSGTEXT;

typedef struct {
    char      cat_name[CAT_LEN];
    char      filename[FILE_LEN+1];
    char      desc[DESC_LEN+1];
    ulong     size;
    ulong     uploaded;
    char      owner[NAME_LEN+1];
    byte      section;
    byte      flags;
    ushort    downloads;
    char      reserved[16];
} UDHEAD;

typedef struct {
    char      name[NAME_LEN+1];
    ulong     when;
    shortint  node;
    shortint  baud;
    char      reserved[16];
} USRLOG;

typedef struct {
    char      msg_path[MAX_PATHNAME];
    char      usr_path[MAX_PATHNAME];
    char      ud_path[MAX_PATHNAME];
    char      log_path[MAX_PATHNAME];
    char      updn_path[NUM_SECT][MAX_PATHNAME];
} BBSPATHS;

typedef struct {
    int       active;
    int       number;
    char      username[NAME_LEN+1];
    char      status[40];
    char      menu_name[FILE_LEN+1];
    ulong     login_time;
    ulong     last_activity;
    ulong     msg_count;
    ulong     file_count;
    ulong     caller_count;
    ulong     pseudo_counter;
} NODEINFO;

typedef struct {
    char      key;
    char      text[MENU_TEXT_LEN];
    char      parm[MENU_PARM_LEN];
    char      access_spec[32];

    byte      function;
    byte      access_mode;
    byte      rec_type;
    byte      is_iret;
    byte      boost;

    byte      has_lo;
    byte      has_hi;
    byte      priv_lo;
    byte      priv_hi;

    ushort    section_mask;

    int       in_use;
} MENUITEM;

typedef struct {
    char      filename[FILE_LEN+1];
    char      prompt[MENU_PROMPT_LEN];
    byte      display_option;
    byte      input_mode;
    int       item_count;
    MENUITEM  item[MAX_MENU_ITEMS];
} MENUFILE;

typedef struct {
    MENUFILE  menu[MAX_MENU_STACK];
    int       sp;
} MENUSTACK;

typedef struct {
    int       running;
    int       logged_in;
    int       local_mode;
    int       expert;
    int       time_left;
    int       sysop_chat;

    USRDESC   user;
    long      user_recno;

    MENUSTACK mstack;
} BBSSESSION;

/* ------------------------------------------------------------ */
/* globals                                                      */
/* ------------------------------------------------------------ */

extern CFGINFO     g_cfg;
extern BBSPATHS    g_paths;
extern BBSSESSION  g_sess;
extern NODEINFO    g_node;

extern FILE *g_fp_msghead;
extern FILE *g_fp_msgtext;
extern FILE *g_fp_userdesc;
extern FILE *g_fp_udhead;
extern FILE *g_fp_caller;

/* ------------------------------------------------------------ */
/* common helper/data APIs                                      */
/* ------------------------------------------------------------ */

int  load_cfginfo(char *fname);
int  load_bbs_paths(char *fname);
int  open_main_datafiles(void);
void close_main_datafiles(void);

void data_trim_crlf(char *s);
void data_prompt_line(char *prompt, char *buf, int len);
void data_prompt_hidden(char *prompt, char *buf, int len);
int  data_yesno(char *prompt, int def_yes);

long data_find_user_by_name(char *name, USRDESC *u);
long data_find_blank_user(void);
int  data_read_user(long recno, USRDESC *u);
int  data_write_user(long recno, USRDESC *u);
long data_first_user(USRDESC *u);
long data_next_user(long currec, USRDESC *u);

long data_first_msg(MSGHEAD *m);
long data_next_msg(long currec, MSGHEAD *m);
int  data_write_msghead(long recno, MSGHEAD *m);

void bbs_pause(void);

#endif