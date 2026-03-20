/* BBSFUNC.H
 *
 * BBS-PC ! 4.21
 *
 * Reconstructed and modernised source
 * derived from BBS-PC 4.20
 *
 * Global function prototypes and menu function opcodes.
 */

#ifndef BBSFUNC_H
#define BBSFUNC_H

#define BBSFUNC_INCLUDED   1

#include "bbsdata.h"

/* ------------------------------------------------------------ */
/* Menu function opcodes (BBS-PC compatible + 4.21 extensions)  */
/* ------------------------------------------------------------ */

/* General functions */
#define MF_USER_STATS        0
#define MF_SECTION_NAMES     1
#define MF_CHANGE_MASK       2
#define MF_REGISTER          3
#define MF_USER_EDIT         4
#define MF_CALLER_LOG        5
#define MF_TIME_ON_SYS       6
#define MF_CHAT_SYSOP        7
#define MF_EXPERT_TOGGLE     8

/* Message / file functions */
#define MF_LEAVE_MSG         9
#define MF_LEAVE_SYSOP       10
#define MF_READ_MSGS         11
#define MF_READ_EMAIL        12
#define MF_SCAN_MSGS         13
#define MF_DELETE_MSG        14
#define MF_PRINT_CAT         15
#define MF_BROWSE_FILES      16
#define MF_UPLOAD_FILE       17
#define MF_UPLOAD_LOCAL      18
#define MF_DOWNLOAD_FILE     19
#define MF_READ_FILE         20
#define MF_KILL_FILE         21

/* Maintenance functions */
#define MF_ADD_USER          22
#define MF_DEL_USER          23
#define MF_CHG_USER          24
#define MF_PURGE_USERS       25
#define MF_PURGE_MSGS        26
#define MF_PRINT_USERS       27
#define MF_RESET_BULL        28
#define MF_UPDATE_DEF        29
#define MF_DEF_SECTIONS      30
#define MF_DEF_TERMS         31
#define MF_MODEM_DEF         32
#define MF_USER_DEF          33
#define MF_SYS_DEF           34
#define MF_NODE_DEF          35

/* Special functions */
#define MF_LIST_PHONE        36
#define MF_CHG_PHONE         37
#define MF_DIAL_REMOTE       38
#define MF_DIAL_UNLISTED     39
#define MF_UPLOAD_DIRECT     40
#define MF_DNL_DIRECT        41
#define MF_KILL_DIRECT       42
#define MF_DOS_GATE          43
#define MF_SRCH_CATDESC      44
#define MF_NEW_FILES         45

/* 4.21 extensions */
#define MF_READ_NEW_MSGS     90
#define MF_READ_NEW_EMAIL    91
#define MF_SCAN_NEW_MSGS     92

/* Control functions */
#define MF_EXIT_SYS          100
#define MF_SHOW_MENU         101
#define MF_RET_LEVELS        102
#define MF_RET_TOP           103
#define MF_CALL_MENU         104
#define MF_GOTO_MENU         105
#define MF_CLS_TYPE          106
#define MF_TYPE_FILE         107
#define MF_CLS_TYPE_NS       108
#define MF_TYPE_FILE_NS      109
#define MF_CHG_MENUSET       110
#define MF_EXEC_EXT          111

/* ------------------------------------------------------------ */
/* BBS.C                                                        */
/* ------------------------------------------------------------ */

int  main(int argc, char *argv[]);
void bbs_startup(int argc, char *argv[]);
void bbs_shutdown(void);
void bbs_run(void);
void bbs_main_loop(void);
void bbs_waiting_for_call(void);
void bbs_session_loop(void);
void bbs_login_sequence(void);
void bbs_logout_sequence(void);

void bbs_banner(void);
void bbs_goodbye(void);
void bbs_pause(void);
void bbs_cls(void);
void bbs_beep(void);
void bbs_press_enter(void);
void bbs_print_center(char *s);
void bbs_print_file(char *fname);
void bbs_print_file_nostop(char *fname);
void bbs_type_file(char *fname, int cls_flag, int stop_flag);

/* ------------------------------------------------------------ */
/* BBSDATA / data / config                                      */
/* ------------------------------------------------------------ */

int    load_bbs_paths(char *fname);
int    load_cfginfo(char *fname);
int    open_main_datafiles(void);
void   close_main_datafiles(void);

void   data_now_strings(char *d, char *t);
void   data_trim_crlf(char *s);
void   data_prompt_line(char *prompt, char *buf, int buflen);

ushort data_pack_date_now(void);
ushort data_pack_time_now(void);
void   data_unpack_date(ushort p, char *out);
void   data_unpack_time(ushort p, char *out);

int    data_user_match(char *a, char *b);
int    data_name_match(char *a, char *b, int maxlen);
int    data_cat_match(char *a, char *b);

long   data_user_count(void);
long   data_msg_count(void);
long   data_msgtext_count(void);
long   data_file_count(void);
long   data_caller_count(void);

long   data_highest_msg_number(void);

long   data_find_user_by_name(char *name, USRDESC *u);
long   data_find_msg_by_number(long msgno, MSGHEAD *h);
long   data_find_file_by_catalog(char *name, UDHEAD *u);

long   data_find_blank_user(void);
long   data_find_blank_msghead(void);
long   data_find_blank_msgtext(void);
long   data_find_blank_udhead(void);
long   data_find_blank_caller(void);

int    data_write_user(long recno, USRDESC *u);
int    data_write_msghead(long recno, MSGHEAD *h);
int    data_write_msgtext(long recno, MSGTEXT *t);
int    data_write_udhead(long recno, UDHEAD *u);
int    data_write_caller(long recno, USRLOG *c);

int    data_read_user(long recno, USRDESC *u);
int    data_read_msghead(long recno, MSGHEAD *h);
int    data_read_msgtext(long recno, MSGTEXT *t);
int    data_read_udhead(long recno, UDHEAD *u);
int    data_read_caller(long recno, USRLOG *c);

int    data_store_message_text(char *body, ushort *firstptr);
int    data_load_message_text(ushort firstptr, char *out, int outlen);
void   data_zero_message_chain(ushort firstptr);

void   data_make_data_path(char *out, char *name);
void   data_make_updn_path(char *out, int section, char *name);
int    data_disk_file_exists(char *path);
long   data_disk_file_length(char *path);

/* ------------------------------------------------------------ */
/* BBSISAM                                                      */
/* ------------------------------------------------------------ */

long  isam_file_size(FILE *fp);
long  isam_record_count(FILE *fp, int reclen);
long  isam_record_offset(long recno, int reclen);
int   isam_seek_record(FILE *fp, long recno, int reclen);
int   isam_read_record(FILE *fp, long recno, int reclen, void *buf);
int   isam_write_record(FILE *fp, long recno, int reclen, void *buf);
int   isam_zero_record(FILE *fp, long recno, int reclen);

FILE *isam_open(char *path, char *mode);
void  isam_close(FILE *fp);

int   isam_read_header(FILE *fp, void *hdr, unsigned hdrlen);
int   isam_write_header(FILE *fp, void *hdr, unsigned hdrlen);
int   isam_blank_header(FILE *fp);

long  isam_append_blank_record(FILE *fp, int reclen);
long  isam_find_blank_record(FILE *fp, int reclen, int (*blank_test)(), void *ctx);

long  isam_count_users(void);
long  isam_count_msgs(void);
long  isam_count_msgtext(void);
long  isam_count_files(void);
long  isam_count_callers(void);

int   isam_read_user(long recno, USRDESC *u);
int   isam_write_user(long recno, USRDESC *u);
int   isam_read_msghead(long recno, MSGHEAD *h);
int   isam_write_msghead(long recno, MSGHEAD *h);
int   isam_read_msgtext(long recno, MSGTEXT *t);
int   isam_write_msgtext(long recno, MSGTEXT *t);
int   isam_read_udhead(long recno, UDHEAD *u);
int   isam_write_udhead(long recno, UDHEAD *u);
int   isam_read_caller(long recno, USRLOG *c);
int   isam_write_caller(long recno, USRLOG *c);

int   isam_blank_user(void *buf, void *ctx);
int   isam_blank_msghead(void *buf, void *ctx);
int   isam_blank_msgtext(void *buf, void *ctx);
int   isam_blank_udhead(void *buf, void *ctx);
int   isam_blank_caller(void *buf, void *ctx);

long  isam_find_blank_user(void);
long  isam_find_blank_msghead(void);
long  isam_find_blank_msgtext(void);
long  isam_find_blank_udhead(void);
long  isam_find_blank_caller(void);

long  isam_find_user_by_name(char *name, USRDESC *out);
long  isam_find_msg_by_number(long msgno, MSGHEAD *out);
long  isam_find_file_by_catalog(char *name, UDHEAD *out);

int   isam_rebuild_keys(char *path_dat, char *path_key);
int   isam_delete_record(FILE *fp, long recno, int reclen);
int   isam_insert_record(FILE *fp, long recno, int reclen, void *buf);

/* ------------------------------------------------------------ */
/* BBSNODE / BBSNODEED                                          */
/* ------------------------------------------------------------ */

void node_startup_init(void);
void node_session_begin(void);
void node_session_end(void);
void node_mark_menu(char *fname);
void node_mark_message_activity(void);
void node_mark_file_activity(void);
void node_mark_user_activity(void);
void node_set_chat(int on);

int  node_create_file(int node_no);
int  node_set_pseudo_counter_file(int node_no, long value);
void do_change_node_defaults(void);

/* ------------------------------------------------------------ */
/* BBSTERM                                                      */
/* ------------------------------------------------------------ */

void term_init_defaults(void);
void term_start_session(void);
void term_end_session(void);
void term_apply_user(USRDESC *u);

void term_pause(void);
void term_cls(void);
void term_beep(void);
void term_print_center(char *s);
int  term_type_file(char *fname, int cls_flag, int stop_flag);
void term_show_current(void);

int  term_carrier(void);
void term_getline(char *prompt, char *buf, int buflen);
void term_getline_hidden(char *prompt, char *buf, int buflen);
int  term_yesno(char *prompt, int dflt);

/* ------------------------------------------------------------ */
/* BBSMODM / BBSFOSSIL                                          */
/* ------------------------------------------------------------ */

void modem_init(void);
void modem_begin_session(void);
void modem_end_session(void);
int  modem_send_byte(int ch);
int  modem_recv_byte(int timeout);
void modem_direct_upload(void);

int  fossil_driver_installed(void);
int  fossil_carrier(void);

int  ascii_send_file(char *path);
int  xmodem_send_file(char *path, int proto);
int  xmodem_recv_file(char *path, int proto);
int  ymodem_send_file(char *path);
int  ymodem_batch_send_file(char *path);
int  ymodem_recv_file(char *path);
int  ymodem_batch_recv_file(char *path);
int  kermit_send_file(char *path);
int  kermit_recv_file(char *path);
int  zmodem_send_file(char *path);
int  zmodem_batch_send_file(char *path);
int  zmodem_recv_file(char *path);
int  zmodem_batch_recv_file(char *path);

/* ------------------------------------------------------------ */
/* BBSPROTO                                                     */
/* ------------------------------------------------------------ */

int  proto_download(char *path, int proto);
int  proto_upload(char *path, int proto);

/* ------------------------------------------------------------ */
/* BBSLOGIN                                                     */
/* ------------------------------------------------------------ */

int  login_user(void);
int  login_wizard(void);
int  login_guest(void);
int  login_local(void);
int  user_register(void);
int  password_prompt(USRDESC *u);
void login_record_call(void);
void login_update_lastcall(USRDESC *u);
void logout_user(void);

void do_register_user(void);
void do_callback_user(void);
void do_lockout_user(void);

/* ------------------------------------------------------------ */
/* BBSUSER                                                      */
/* ------------------------------------------------------------ */

int  user_load_by_name(char *name, USRDESC *u);
int  user_save_current(void);
void do_add_user(void);
void do_delete_user(void);
void do_change_user(void);
void do_purge_inactive_users(void);
void do_print_user_list(void);
void do_update_user_defaults(void);
void do_user_edit(void);
void do_user_statistics(void);

/* ------------------------------------------------------------ */
/* BBSMSG                                                       */
/* ------------------------------------------------------------ */

void do_leave_message(void);
void do_leave_message_to_sysop(void);
void do_leave_message_to_name(void);
void do_read_messages(void);
void do_read_new_messages(void);
void do_read_email(void);
void do_read_new_email(void);
void do_scan_messages(void);
void do_scan_new_messages(void);
void do_reply_message(void);
void do_delete_message(void);
void do_purge_messages(void);

/* ------------------------------------------------------------ */
/* BBSFILE                                                      */
/* ------------------------------------------------------------ */

void do_list_files(void);
void do_download_file(void);
void do_upload_file(void);
void do_kill_file(void);
void do_browse_files(void);
void do_new_files_scan(void);
void do_search_catdesc(void);
void do_read_file(void);
void do_upload_direct(void);
void do_download_direct(void);
void do_upload_local(void);
void do_direct_file_kill(void);
void do_print_catalog(void);
void do_new_files(void);

/* ------------------------------------------------------------ */
/* BBSMAINT                                                     */
/* ------------------------------------------------------------ */

void do_reset_bulletin_flags(void);
void do_define_section_names(void);
void do_define_terminal_types(void);
void do_modem_defaults(void);
void do_user_defaults(void);
void do_system_defaults(void);

/* ------------------------------------------------------------ */
/* BBSSYS                                                       */
/* ------------------------------------------------------------ */

int  session_minutes_used(void);
int  session_minutes_left(void);
int  session_time_expired(void);
void session_touch_activity(void);
int  session_idle_minutes(void);
int  session_idle_expired(void);

void do_time_on_system(void);
void do_chat_with_sysop(void);
void do_expert_toggle(void);
int  sysop_password_prompt(void);

/* ------------------------------------------------------------ */
/* BBSMENU                                                      */
/* ------------------------------------------------------------ */

int       menu_load(char *fname, MENUFILE *m);
MENUFILE *menu_current(void);
int       menu_push(char *fname);
int       menu_pop(void);
void      menu_display(MENUFILE *m);
int       menu_execute(MENUFILE *m);

/* ------------------------------------------------------------ */
/* BBSSPEC                                                      */
/* ------------------------------------------------------------ */

void do_execute_external_program(char *parm);
void do_dos_gate(void);
void do_section_names(void);
void do_change_section_mask(void);
void cls_type_text_file(char *fname, int stop_flag);
void type_text_file(char *fname, int stop_flag);
void do_change_menu_sets(void);
void do_print_caller_log(void);
void do_list_phone_directory(void);
void do_change_phone_listing(void);
void do_dial_connect_remote(void);
void do_unlisted_dial_connect(void);

/* ------------------------------------------------------------ */
/* menu/runtime return helpers                                  */
/* ------------------------------------------------------------ */

void do_return_specified_levels(void);
void do_return_top_level(void);

#endif /* BBSFUNC_H */