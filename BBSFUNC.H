/* BBSFUNC.H
 *
 * BBS-PC 4.20 source reconstruction
 * Global function prototypes
 */

#ifndef BBSFUNC_H
#define BBSFUNC_H

#include "bbsdata.h"

/* ------------------------------------------------------------ */
/* BBS.C                                                        */
/* ------------------------------------------------------------ */

int  main(int argc, char *argv[]);
void bbs_run(void);
void bbs_startup(int argc, char *argv[]);
void bbs_shutdown(void);
void bbs_banner(void);
void bbs_main_loop(void);
void bbs_login_sequence(void);
void bbs_logout_sequence(void);
void bbs_pause(void);
void bbs_cls(void);
void bbs_beep(void);
void bbs_press_enter(void);
void bbs_print_center(char *s);
void bbs_print_file(char *fname);
void bbs_print_file_nostop(char *fname);
void bbs_type_file(char *fname, int cls_flag, int stop_flag);
void bbs_goodbye(void);

/* ------------------------------------------------------------ */
/* BBSCFG.C / config/data open                                  */
/* ------------------------------------------------------------ */

int  load_bbs_paths(char *fname);
int  load_cfginfo(char *fname);
int  open_main_datafiles(void);
void close_main_datafiles(void);
void make_data_path(char *dst, char *dir, char *name);

/* ------------------------------------------------------------ */
/* BBSMENU.C                                                    */
/* ------------------------------------------------------------ */

int       menu_load(char *fname, MENUFILE *m);
void      menu_display(MENUFILE *m);
int       menu_execute(MENUFILE *m);
int       menu_push(char *fname);
int       menu_pop(void);
MENUFILE *menu_current(void);

/* ------------------------------------------------------------ */
/* BBSLOGIN.C                                                   */
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
/* BBSUSER.C                                                    */
/* ------------------------------------------------------------ */

void do_user_statistics(void);
void do_section_names(void);
void do_change_section_mask(void);
void do_user_edit(void);
void do_print_caller_log(void);

long user_find_by_name(char *name, USRDESC *u);
int  user_load_by_name(char *name, USRDESC *u);
int  user_save_current(void);

/* ------------------------------------------------------------ */
/* BBSMSG.C                                                     */
/* ------------------------------------------------------------ */

void do_leave_message(void);
void do_leave_message_to_sysop(void);
void do_read_messages(void);
void do_read_email(void);
void do_scan_messages(void);
void do_delete_message(void);
void do_leave_message_to_name(void);
void do_reply_message(void);
void do_purge_messages(void);

/* ------------------------------------------------------------ */
/* BBSFILE.C                                                    */
/* ------------------------------------------------------------ */

void do_print_catalog(void);
void do_browse_files(void);
void do_upload_file(void);
void do_upload_local(void);
void do_download_file(void);
void do_read_file(void);
void do_kill_file(void);
void do_search_catdesc(void);
void do_new_files(void);

/* ------------------------------------------------------------ */
/* BBSMAINT.C                                                   */
/* ------------------------------------------------------------ */

void do_add_user(void);
void do_delete_user(void);
void do_change_user(void);
void do_purge_inactive_users(void);
void do_print_user_list(void);
void do_reset_bulletin_flags(void);
void do_update_user_defaults(void);
void do_define_section_names(void);
void do_define_terminal_types(void);
void do_user_defaults(void);
void do_system_defaults(void);

/* ------------------------------------------------------------ */
/* BBSSPEC.C                                                    */
/* ------------------------------------------------------------ */

void do_list_phone_directory(void);
void do_change_phone_listing(void);
void do_dial_connect_remote(void);
void do_unlisted_dial_connect(void);
void do_upload_direct(void);
void do_download_direct(void);
void do_direct_file_kill(void);
void do_dos_gate(void);
void do_execute_external_program(char *cmd);
void do_return_specified_levels(void);
void do_return_top_level(void);
void do_change_menu_sets(void);
void do_special_file_command(void);
void do_special_phone_command(void);

/* ------------------------------------------------------------ */
/* BBSNODE.C                                                    */
/* ------------------------------------------------------------ */

int  node_open_current(void);
int  node_save_current(void);
void node_close_current(void);
void node_mark_waiting(void);
void node_mark_logged_in(void);
void node_mark_menu(char *menu_name);
void node_mark_message_activity(void);
void node_mark_file_activity(void);
void node_mark_user_activity(void);
void node_set_chat(int flag);
void node_set_status(char *text);
long node_get_pseudo_counter(void);
void node_set_pseudo_counter(long v);
void node_bump_pseudo_counter(void);
void node_make_pseudo_name(char *out, int ext_num);
void node_show_status(void);
void node_show_brief_line(void);
void node_reset_current(void);
void node_change_banner(void);
void node_startup_init(void);
void node_session_begin(void);
void node_session_end(void);

/* ------------------------------------------------------------ */
/* BBSNODEED.C                                                  */
/* ------------------------------------------------------------ */

void node_edit_file(int node_num);
void node_edit_current(void);
void node_create_file(int node_num);
void node_convert_file(int node_num);
void node_set_pseudo_counter_file(int node_num, long v);
long node_get_pseudo_counter_file(int node_num);
void do_change_node_defaults(void);

/* ------------------------------------------------------------ */
/* BBSTERM.C                                                    */
/* ------------------------------------------------------------ */

void  term_init_defaults(void);
void  term_apply_user(USRDESC *u);
void  term_apply_cfg_terminal(int termno);
void  term_start_session(void);
void  term_end_session(void);

void  term_putc(int ch);
void  term_puts(char *s);
void  term_newline(void);
void  term_crlf(void);
void  term_beep(void);
void  term_cls(void);
void  term_reset_pager(void);
void  term_pause(void);
int   term_more_needed(void);
void  term_page_check(void);
void  term_type_text(char *s, int stop_flag);
void  term_print_center(char *s);
void  term_print_status_line(char *s);

int   term_getc(void);
int   term_getkey(void);
void  term_getline(char *prompt, char *out, int len);
void  term_getline_upper(char *prompt, char *out, int len);
void  term_getline_hidden(char *prompt, char *out, int len);
int   term_yesno(char *prompt, int def_yes);

char *term_protocol_name(int proto);
char *term_type_name(int termno);
void  term_show_current(void);
void  term_set_protocol(int proto);
void  term_set_page_length(int n);
void  term_set_width(int n);
void  term_set_linefeeds(int flag);
void  term_set_nuls(int n);

int   term_type_file(char *fname, int cls_flag, int stop_flag);

int   term_carrier(void);
void  term_drop_dtr(void);
void  term_toggle_chat(int flag);
void  term_switch_8n1(char *label);
void  term_restore_mode(void);

int   type_text_file(char *fname, int pause_flag);
int   cls_type_text_file(char *fname, int pause_flag);

/* ------------------------------------------------------------ */
/* BBSMODM.C                                                    */
/* ------------------------------------------------------------ */

void  modem_init(void);
void  modem_reset(void);
void  modem_begin_session(void);
void  modem_end_session(void);
int   modem_is_online(void);

char *modem_protocol_name(int proto);
void  modem_set_protocol(int proto);
int   modem_get_protocol(void);
void  modem_show_protocols(void);
void  modem_select_protocol(void);

void  modem_show_status(void);
void  modem_switch_for_protocol(char *label);
void  modem_restore_after_protocol(void);
void  modem_show_transfer_banner(char *direction, char *name);
void  modem_show_transfer_result(int ok, char *direction);

int   modem_can_send_text(void);
int   modem_can_send_binary(void);
int   modem_can_transfer_protocol(int proto);

int   modem_upload_file(char *path, int proto);
int   modem_download_file(char *path, int proto);
int   modem_upload_text(char *path);
int   modem_download_text(char *path);

void  modem_direct_upload(void);
void  modem_direct_download(void);

int   modem_dial(char *number);
int   modem_hangup(void);
int   modem_answer(void);

int   modem_send_block(void *buf, unsigned len);
int   modem_recv_block(void *buf, unsigned len);
int   modem_send_byte(int ch);
int   modem_recv_byte(int timeout_ticks);

void  do_modem_defaults(void);

/* ------------------------------------------------------------ */
/* BBSPROTO.C                                                   */
/* ------------------------------------------------------------ */

char *proto_name(int id);
char *proto_short_name(int id);
int   proto_is_binary(int id);
int   proto_is_batch(int id);
int   proto_valid(int id);

void  proto_show_list(void);
void  proto_select_current(void);
void  proto_show_current(void);

int   proto_recommended_for_file(char *fname, int is_binary);
int   proto_can_send_file(int id, int is_binary);

int   proto_upload(char *path, int id);
int   proto_download(char *path, int id);

void  proto_direct_upload(void);
void  proto_direct_download(void);

int   proto_batch_upload(char *listfile, int id);
int   proto_batch_download(char *listfile, int id);

int   proto_text_send(char *path);
int   proto_text_recv(char *path);
int   proto_xmodem_send(char *path);
int   proto_xmodem_recv(char *path);
int   proto_xmodemcrc_send(char *path);
int   proto_xmodemcrc_recv(char *path);
int   proto_ymodem_send(char *path);
int   proto_ymodem_recv(char *path);
int   proto_ymodem_batch_send(char *path);
int   proto_ymodem_batch_recv(char *path);
int   proto_kermit_send(char *path);
int   proto_kermit_recv(char *path);
int   proto_zmodem_send(char *path);
int   proto_zmodem_recv(char *path);
int   proto_zmodem_batch_send(char *path);
int   proto_zmodem_batch_recv(char *path);

/* ------------------------------------------------------------ */
/* BBSISAM.C                                                    */
/* ------------------------------------------------------------ */

long isam_file_size(FILE *fp);
long isam_record_count(FILE *fp, int reclen);
long isam_record_offset(long recno, int reclen);
int  isam_seek_record(FILE *fp, long recno, int reclen);
int  isam_read_record(FILE *fp, long recno, int reclen, void *buf);
int  isam_write_record(FILE *fp, long recno, int reclen, void *buf);
int  isam_zero_record(FILE *fp, long recno, int reclen);

FILE *isam_open(char *path, char *mode);
void  isam_close(FILE *fp);

int  isam_read_header(FILE *fp, void *hdr, unsigned hdrlen);
int  isam_write_header(FILE *fp, void *hdr, unsigned hdrlen);
int  isam_blank_header(FILE *fp);

long isam_append_blank_record(FILE *fp, int reclen);
long isam_find_blank_record(FILE *fp, int reclen, int (*blank_test)(), void *ctx);

long isam_count_users(void);
long isam_count_msgs(void);
long isam_count_msgtext(void);
long isam_count_files(void);
long isam_count_callers(void);

int  isam_read_user(long recno, USRDESC *u);
int  isam_write_user(long recno, USRDESC *u);

int  isam_read_msghead(long recno, MSGHEAD *h);
int  isam_write_msghead(long recno, MSGHEAD *h);

int  isam_read_msgtext(long recno, MSGTEXT *t);
int  isam_write_msgtext(long recno, MSGTEXT *t);

int  isam_read_udhead(long recno, UDHEAD *u);
int  isam_write_udhead(long recno, UDHEAD *u);

int  isam_read_caller(long recno, USRLOG *c);
int  isam_write_caller(long recno, USRLOG *c);

int  isam_blank_user(void *buf, void *ctx);
int  isam_blank_msghead(void *buf, void *ctx);
int  isam_blank_msgtext(void *buf, void *ctx);
int  isam_blank_udhead(void *buf, void *ctx);
int  isam_blank_caller(void *buf, void *ctx);

long isam_find_blank_user(void);
long isam_find_blank_msghead(void);
long isam_find_blank_msgtext(void);
long isam_find_blank_udhead(void);
long isam_find_blank_caller(void);

long isam_find_user_by_name(char *name, USRDESC *out);
long isam_find_msg_by_number(long msgno, MSGHEAD *out);
long isam_find_file_by_catalog(char *name, UDHEAD *out);

int  isam_rebuild_keys(char *path_dat, char *path_key);
int  isam_delete_record(FILE *fp, long recno, int reclen);
int  isam_insert_record(FILE *fp, long recno, int reclen, void *buf);

/* ------------------------------------------------------------ */
/* BBSDATA.C                                                    */
/* ------------------------------------------------------------ */

void   data_trim_crlf(char *s);
void   data_prompt_line(char *prompt, char *out, int len);
void   data_prompt_hidden(char *prompt, char *out, int len);
int    data_yesno(char *prompt, int def_yes);

void   data_join_path(char *dst, char *dir, char *name);
void   data_make_menu_name(char *dst, char *base, int menuset);
void   data_make_bulletin_name(char *dst, int secch);
void   data_make_node_name(char *dst, int node_num);
void   data_make_updn_path(char *dst, int section, char *fname);
void   data_make_msg_path(char *dst, char *fname);
void   data_make_user_path(char *dst, char *fname);
void   data_make_ud_path(char *dst, char *fname);
void   data_make_log_path(char *dst, char *fname);

ushort data_pack_date_now(void);
ushort data_pack_time_now(void);
void   data_unpack_date(ushort d, char *out);
void   data_unpack_time(ushort t, char *out);
void   data_now_strings(char *datebuf, char *timebuf);

long   data_user_count(void);
long   data_msg_count(void);
long   data_msgtext_count(void);
long   data_file_count(void);
long   data_caller_count(void);

long   data_find_blank_user(void);
long   data_find_blank_msghead(void);
long   data_find_blank_msgtext(void);
long   data_find_blank_udhead(void);
long   data_find_blank_caller(void);

long   data_find_user_by_name(char *name, USRDESC *out);
long   data_find_msg_by_number(long msgno, MSGHEAD *out);
long   data_find_file_by_catalog(char *name, UDHEAD *out);

int    data_read_user(long recno, USRDESC *u);
int    data_write_user(long recno, USRDESC *u);
int    data_read_msghead(long recno, MSGHEAD *h);
int    data_write_msghead(long recno, MSGHEAD *h);
int    data_read_msgtext(long recno, MSGTEXT *t);
int    data_write_msgtext(long recno, MSGTEXT *t);
int    data_read_udhead(long recno, UDHEAD *u);
int    data_write_udhead(long recno, UDHEAD *u);
int    data_read_caller(long recno, USRLOG *c);
int    data_write_caller(long recno, USRLOG *c);

long   data_first_user(USRDESC *out);
long   data_next_user(long cur, USRDESC *out);
long   data_first_msg(MSGHEAD *out);
long   data_next_msg(long cur, MSGHEAD *out);
long   data_first_file(UDHEAD *out);
long   data_next_file(long cur, UDHEAD *out);
long   data_first_caller(USRLOG *out);
long   data_next_caller(long cur, USRLOG *out);

int    data_load_message_text(ushort first_recno, char *out, int outlen);
int    data_store_message_text(char *text, ushort *first_recno);
void   data_zero_message_chain(ushort first_rec);

int    data_name_match(char *a, char *b, int maxlen);
int    data_cat_match(char *a, char *b);
int    data_user_match(char *a, char *b);

long   data_highest_msg_number(void);
long   data_lowest_msg_number(void);
long   data_count_visible_msgs(bits mask);
long   data_count_visible_files(bits mask);

long   data_disk_file_length(char *path);
int    data_disk_file_exists(char *path);

int    data_sync_indexes(void);
int    data_verify_open_files(void);

/* ------------------------------------------------------------ */
/* BBSSYS.C                                                     */
/* ------------------------------------------------------------ */

void do_time_on_system(void);
void do_chat_with_sysop(void);
void do_expert_toggle(void);
int  sysop_password_prompt(void);

/* ------------------------------------------------------------ */
/* BBSFOSSIL.C                                                  */
/* ------------------------------------------------------------ */

int      fossil_driver_installed(void);
int      fossil_init(void);
void     fossil_deinit(void);
int      fossil_configure(int baud, int parity, int word, int stops);
unsigned fossil_status_word(void);
int      fossil_carrier(void);
int      fossil_rx_ready(void);
int      fossil_tx_ready(void);
void     fossil_set_dtr(int on);
void     fossil_drop_dtr(void);
void     fossil_raise_dtr(void);
void     fossil_purge_input(void);
void     fossil_purge_output(void);
void     fossil_flush_output(void);
int      fossil_send_char(int ch);
int      fossil_recv_char(int timeout_ticks);
int      fossil_peek_char(void);
void     fossil_start_session(void);
void     fossil_end_session(void);
void     fossil_show_status(void);

/* ------------------------------------------------------------ */
/* ZMODEM internal protocol entry points                        */
/* ------------------------------------------------------------ */

int zmodem_send_file(char *path);
int zmodem_recv_file(char *path);
int zmodem_batch_send_file(char *path);
int zmodem_batch_recv_file(char *path);

#endif