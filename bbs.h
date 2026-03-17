/*
 * BBS.H
 *
 * BBS-PC! system definitions
 *
 * Copyright (c) 1985,86,87 Micro-Systems Software Inc.
 */

#ifndef BBS_H
#define BBS_H

#include <stdio.h>

/* ---------------------------------- */
/* Basic Types                        */
/* ---------------------------------- */

typedef unsigned char  byte;
typedef unsigned short ushort;
typedef unsigned long  ulong;
typedef unsigned short bits;

/* ---------------------------------- */
/* System Limits                      */
/* ---------------------------------- */

#define NUM_SECT     16     /* number of message/file sections */
#define NUM_TERM     8      /* supported terminal types */

#define SECT_LEN     20     /* section name length */
#define FPATH_LEN    64     /* file path length */
#define PASS_LEN     20     /* password length */

/* ---------------------------------- */
/* Terminal Definition                */
/* ---------------------------------- */

typedef struct {

    byte flags;          /* terminal capability flags */
    byte width;          /* screen width */
    byte height;         /* screen height */
    byte reserved;

} TRMNL;

/* terminal flags */

#define TERM_ANSI     0x01
#define TERM_COLOR    0x02
#define TERM_CLEAR    0x04
#define TERM_CURSOR   0x08

/* ---------------------------------- */
/* CFGINFO Structure                  */
/* ---------------------------------- */

typedef struct {

     ushort : 14;             /* reserved */
     ushort dir0_ok: 1;       /* directory 0 downloads */
     ushort by_call: 1;       /* time limit per call */

     short max_msg;           /* maximum messages */
     short max_user;          /* maximum users */
     short max_log;           /* maximum call log */
     short max_ud;            /* maximum upload/download files */
     short reward;            /* upload reward */

     short t1;                /* reserved */
     short t2;                /* reserved */
     short t3;                /* reserved */

     long sleeptime;          /* system sleep timeout */

     byte log_p1;             /* logging privilege low */
     byte log_p2;             /* logging privilege high */

     byte t4;                 /* reserved */

     byte hi_men;             /* highest menu set */

     short log_type;          /* login method */

     short limit[2];          /* guest/member time limits */
     short priv[2];           /* guest/member privileges */

     bits rd_acc[2];          /* read access bitmask */
     bits wr_acc[2];          /* write access bitmask */
     bits up_acc[2];          /* upload access bitmask */
     bits dn_acc[2];          /* download access bitmask */

     byte sav_sec[2];         /* default save section */

     byte sec_flag[NUM_SECT]; /* section flags */

     char sec_name[NUM_SECT][SECT_LEN+1]; /* section names */

     char ud_alt[NUM_SECT][FPATH_LEN+1];  /* alternate U/D paths */

     char syspass[PASS_LEN+1]; /* sysop password */

     byte menu[2];             /* guest/member menu sets */

     byte align;               /* alignment byte */

     TRMNL trmn[NUM_TERM];     /* terminal definitions */

} CFGINFO;

/* ---------------------------------- */
/* Message Header Structure           */
/* ---------------------------------- */

typedef struct {

    short number;
    short reply;
    short to;
    short from;

    byte flags;

    char date[9];
    char time[6];

    char subject[40];

    long textptr;

} MSGHEAD;

/* ---------------------------------- */
/* User Record Structure              */
/* ---------------------------------- */

typedef struct {

    char name[36];
    char password[16];

    short privilege;

    long last_call;

    short time_left;

    short uploads;
    short downloads;

} USER;

/* ---------------------------------- */
/* Upload/Download Record             */
/* ---------------------------------- */

typedef struct {

    char filename[13];
    char uploader[36];

    short section;

    long size;

    char description[60];

} UDREC;

/* ---------------------------------- */
/* Caller Log Record                  */
/* ---------------------------------- */

typedef struct {

    char name[36];

    char date[9];
    char time[6];

    short baud;

} CALLER;

/* ---------------------------------- */

#endif
