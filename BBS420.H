/* BBS420.H
 *
 * BBS-PC! 4.21
 *
 * Utility compatibility header for legacy 4.20 utilities such as
 * BBSFILE.COM. This intentionally exposes the older utility-side
 * file catalog model without changing the rebuilt main executable
 * headers.
 */

#ifndef BBS420_H
#define BBS420_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

/* ------------------------------------------------------------ */
/* legacy scalar types                                          */
/* ------------------------------------------------------------ */

#ifndef BYTE
typedef unsigned char  BYTE;
#endif

#ifndef UWORD
typedef unsigned short UWORD;
#endif

#ifndef WORD
typedef short WORD;
#endif

#ifndef LONG
typedef long LONG;
#endif

#ifndef ULONG
typedef unsigned long ULONG;
#endif

#ifndef bits
typedef unsigned short bits;
#endif

#ifndef TRUE
#define TRUE  1
#endif

#ifndef FALSE
#define FALSE 0
#endif

/* ------------------------------------------------------------ */
/* legacy lengths / layout                                      */
/* ------------------------------------------------------------ */

#define HDRLEN              128L
#define FILEHDR_LEN         HDRLEN

#define MAXSECT             16
#define FNAME_LEN           12
#define DESC_LEN            40
#define OWNER_LEN           35

/* file type flags */
#define UDH_VALID           0x0001
#define UDH_LOCAL           0x0002
#define UDH_ONLINE          0x0004
#define UDH_BIN             0x0008

/* packed directory number inside flags */
#define UDH_DIRNUM_SHIFT    8
#define UDH_DIRNUM_MASK     0xFF00

/* ------------------------------------------------------------ */
/* legacy utility file catalog record                           */
/* ------------------------------------------------------------ */

typedef struct {
    UWORD flags;                          /* validity/type/dir bits */
    char  disk_name[FNAME_LEN + 1];       /* disk filename */
    char  owner[OWNER_LEN + 1];           /* uploader/owner */
    char  desc[DESC_LEN + 1];             /* file description */
    UWORD accesses;                       /* download count */
    LONG  length;                         /* file size */
    UWORD date;                           /* packed DOS date */
    BYTE  section;                        /* section number */
    BYTE  type;                           /* text/binary marker */
    char  dir;                            /* utility-side directory designator */
} UDHEAD;

/* ------------------------------------------------------------ */
/* minimal shared helpers expected by utilities                 */
/* ------------------------------------------------------------ */

#ifndef byte
typedef BYTE byte;
#endif

#ifndef ushort
typedef UWORD ushort;
#endif

#ifndef ulong
typedef ULONG ulong;
#endif

#endif /* BBS420_H */
