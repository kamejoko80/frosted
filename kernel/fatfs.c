/*  
 *      This file is part of frosted.
 *
 *      frosted is free software: you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License version 2, as 
 *      published by the Free Software Foundation.
 *      
 *
 *      frosted is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with frosted.  If not, see <http://www.gnu.org/licenses/>.
 *
 *      Based on Petit FatFs R0.03 by ChaN
 *      Copyright (C) 2014, ChaN
 *      When redistribute the Petit FatFs with any modification, 
 *      the license can also be changed to GNU GPL or BSD-style license.
 *
 *      Copyright (C) 2016, Insane Adding Machines
 *
 */  

#include <stdint.h>
#include "string.h"
#include "frosted.h"

static struct module mod_fatfs = { };

struct fatfs_disk {
    struct fnode *blockdev;
    struct fnode *mountpoint;
    struct fatfs *fs;
};

typedef uint32_t fatfs_cluster;
#ifdef CONFIG_FAT32
# define FATFS_FAT32	1	/* Enable FAT32 */
#endif
#ifdef CONFIG_FAT16
# define FATFS_FAT16	1	/* Enable FAT16 */
#endif

#ifndef FATFS_FAT16
# define FATFS_FAT16 0
# define FATFS_FAT32_ONLY 1
#endif

#ifndef FATFS_FAT32
# define FATFS_FAT16 0
#endif

#ifndef FATFS_FAT32_ONLY
# define FATFS_FAT32_ONLY 0
#endif


#define FATFS_FAT12	0

#define	LD_WORD(ptr)		(uint16_t)(*(uint16_t *)(ptr))
#define	LD_DWORD(ptr)		(uint32_t)(*(uint32_t *)(ptr))
#define FATFS_CODE_PAGE (858)

#define _USE_LCC	1	/* Allow lower case characters for path name */


/* Macro proxies for disk operations */
#define disk_readp(f,b,s,o,l) f->blockdev->owner->ops.block_read(f->blockdev,b,s,o,l)
#define disk_writep(f,b,s,o,l) f->blockdev->owner->ops.block_write(f->blockdev,b,s,o,l)


/* File system object structure */

struct fatfs {
    uint8_t	fs_type;	/* FAT sub type */
    uint8_t	flag;		/* File status flags */
    uint8_t	csize;		/* Number of sectors per cluster */
    uint8_t	pad1;
    uint16_t	n_rootdir;	/* Number of root directory entries (0 on FAT32) */
    fatfs_cluster n_fatent;	/* Number of FAT entries (= number of clusters + 2) */
    uint32_t	fatbase;	/* FAT start sector */
    uint32_t	dirbase;	/* Root directory start sector (Cluster# on FAT32) */
    uint32_t	database;	/* Data start sector */
    uint32_t	fptr;		/* File R/W pointer */
    uint32_t	fsize;		/* File size */
    fatfs_cluster	org_clust;	/* File start cluster */
    fatfs_cluster	curr_clust;	/* File current cluster */
    uint32_t	dsect;		/* File current data sector */
};



/* Directory object structure */

struct fatfs_dir {
    uint16_t	index;		/* Current read/write index number */
    uint8_t*	fn;			/* Pointer to the SFN (in/out) {file[8],ext[3],status[1]} */
    fatfs_cluster	sclust;		/* Table start cluster (0:Static table) */
    fatfs_cluster	clust;		/* Current cluster */
    uint32_t	sect;		/* Current sector */
};



/* File status structure */

struct fatfs_finfo{
    uint32_t	fsize;		/* File size */
    uint16_t	fdate;		/* Last modified date */
    uint16_t	ftime;		/* Last modified time */
    uint8_t	fattrib;	/* Attribute */
    char	fname[13];	/* File name */
};



/* File function return code (int) */

#define FR_OK            0	
#define FR_DISK_ERR      1
#define FR_NOT_READY     2
#define FR_NO_FILE       3
#define FR_NOT_OPENED    4
#define FR_NOT_ENABLED   5
#define FR_NO_FILESYSTEM 6	

/* File status flag (FATFS.flag) */

#define	FA_OPENED	0x01
#define	FA_WPRT		0x02
#define	FA__WIP		0x40

/* DISK status */
#define STA_OK          0x00
#define STA_NOINIT		0x01	/* Drive not initialized */
#define STA_NODISK		0x02	/* No medium in the drive */

/* FAT sub type (FATFS.fs_type) */

#define FS_FAT12	1
#define FS_FAT16	2
#define FS_FAT32	3


/* File attribute bits for directory entry */

#define	AM_RDO	0x01	/* Read only */
#define	AM_HID	0x02	/* Hidden */
#define	AM_SYS	0x04	/* System */
#define	AM_VOL	0x08	/* Volume label */
#define AM_LFN	0x0F	/* LFN entry */
#define AM_DIR	0x10	/* Directory */
#define AM_ARC	0x20	/* Archive */
#define AM_MASK	0x3F	/* Mask of defined bits */


/*--------------------------------------------------------------------------

  Module Private Definitions

  ---------------------------------------------------------------------------*/

#define ABORT(err)	{fs->flag = 0; return err;}



/*---------------------------------------------------------------------------/
  / Locale and Namespace Configurations
  /---------------------------------------------------------------------------*/





/*--------------------------------------------------------*/
/* DBCS code ranges and SBCS extend char conversion table */
/*--------------------------------------------------------*/

#if FATFS_CODE_PAGE == 932	/* Japanese Shift-JIS */
#define _DF1S	0x81	/* DBC 1st byte range 1 start */
#define _DF1E	0x9F	/* DBC 1st byte range 1 end */
#define _DF2S	0xE0	/* DBC 1st byte range 2 start */
#define _DF2E	0xFC	/* DBC 1st byte range 2 end */
#define _DS1S	0x40	/* DBC 2nd byte range 1 start */
#define _DS1E	0x7E	/* DBC 2nd byte range 1 end */
#define _DS2S	0x80	/* DBC 2nd byte range 2 start */
#define _DS2E	0xFC	/* DBC 2nd byte range 2 end */

#elif FATFS_CODE_PAGE == 936	/* Simplified Chinese GBK */
#define _DF1S	0x81
#define _DF1E	0xFE
#define _DS1S	0x40
#define _DS1E	0x7E
#define _DS2S	0x80
#define _DS2E	0xFE

#elif FATFS_CODE_PAGE == 949	/* Korean */
#define _DF1S	0x81
#define _DF1E	0xFE
#define _DS1S	0x41
#define _DS1E	0x5A
#define _DS2S	0x61
#define _DS2E	0x7A
#define _DS3S	0x81
#define _DS3E	0xFE

#elif FATFS_CODE_PAGE == 950	/* Traditional Chinese Big5 */
#define _DF1S	0x81
#define _DF1E	0xFE
#define _DS1S	0x40
#define _DS1E	0x7E
#define _DS2S	0xA1
#define _DS2E	0xFE

#elif FATFS_CODE_PAGE == 437	/* U.S. (OEM) */
#define _EXCVT {0x80,0x9A,0x90,0x41,0x8E,0x41,0x8F,0x80,0x45,0x45,0x45,0x49,0x49,0x49,0x8E,0x8F,0x90,0x92,0x92,0x4F,0x99,0x4F,0x55,0x55,0x59,0x99,0x9A,0x9B,0x9C,0x9D,0x9E,0x9F, \
    0x41,0x49,0x4F,0x55,0xA5,0xA5,0xA6,0xA7,0xA8,0xA9,0xAA,0xAB,0xAC,0x21,0xAE,0xAF,0xB0,0xB1,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xB8,0xB9,0xBA,0xBB,0xBC,0xBD,0xBE,0xBF, \
    0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,0xD0,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xDF, \
    0xE0,0xE1,0xE2,0xE3,0xE4,0xE5,0xE6,0xE7,0xE8,0xE9,0xEA,0xEB,0xEC,0xED,0xEE,0xEF,0xF0,0xF1,0xF2,0xF3,0xF4,0xF5,0xF6,0xF7,0xF8,0xF9,0xFA,0xFB,0xFC,0xFD,0xFE,0xFF}

#elif FATFS_CODE_PAGE == 720	/* Arabic (OEM) */
#define _EXCVT {0x80,0x81,0x45,0x41,0x84,0x41,0x86,0x43,0x45,0x45,0x45,0x49,0x49,0x8D,0x8E,0x8F,0x90,0x92,0x92,0x93,0x94,0x95,0x49,0x49,0x98,0x99,0x9A,0x9B,0x9C,0x9D,0x9E,0x9F, \
    0xA0,0xA1,0xA2,0xA3,0xA4,0xA5,0xA6,0xA7,0xA8,0xA9,0xAA,0xAB,0xAC,0xAD,0xAE,0xAF,0xB0,0xB1,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xB8,0xB9,0xBA,0xBB,0xBC,0xBD,0xBE,0xBF, \
    0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,0xD0,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xDF, \
    0xE0,0xE1,0xE2,0xE3,0xE4,0xE5,0xE6,0xE7,0xE8,0xE9,0xEA,0xEB,0xEC,0xED,0xEE,0xEF,0xF0,0xF1,0xF2,0xF3,0xF4,0xF5,0xF6,0xF7,0xF8,0xF9,0xFA,0xFB,0xFC,0xFD,0xFE,0xFF}

#elif FATFS_CODE_PAGE == 737	/* Greek (OEM) */
#define _EXCVT {0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8A,0x8B,0x8C,0x8D,0x8E,0x8F,0x90,0x92,0x92,0x93,0x94,0x95,0x96,0x97,0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87, \
    0x88,0x89,0x8A,0x8B,0x8C,0x8D,0x8E,0x8F,0x90,0x91,0xAA,0x92,0x93,0x94,0x95,0x96,0xB0,0xB1,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xB8,0xB9,0xBA,0xBB,0xBC,0xBD,0xBE,0xBF, \
    0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,0xD0,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xDF, \
    0x97,0xEA,0xEB,0xEC,0xE4,0xED,0xEE,0xE7,0xE8,0xF1,0xEA,0xEB,0xEC,0xED,0xEE,0xEF,0xF0,0xF1,0xF2,0xF3,0xF4,0xF5,0xF6,0xF7,0xF8,0xF9,0xFA,0xFB,0xFC,0xFD,0xFE,0xFF}

#elif FATFS_CODE_PAGE == 775	/* Baltic (OEM) */
#define _EXCVT {0x80,0x9A,0x91,0xA0,0x8E,0x95,0x8F,0x80,0xAD,0xED,0x8A,0x8A,0xA1,0x8D,0x8E,0x8F,0x90,0x92,0x92,0xE2,0x99,0x95,0x96,0x97,0x97,0x99,0x9A,0x9D,0x9C,0x9D,0x9E,0x9F, \
    0xA0,0xA1,0xE0,0xA3,0xA3,0xA5,0xA6,0xA7,0xA8,0xA9,0xAA,0xAB,0xAC,0xAD,0xAE,0xAF,0xB0,0xB1,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xB8,0xB9,0xBA,0xBB,0xBC,0xBD,0xBE,0xBF, \
    0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,0xB5,0xB6,0xB7,0xB8,0xBD,0xBE,0xC6,0xC7,0xA5,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xDF, \
    0xE0,0xE1,0xE2,0xE3,0xE5,0xE5,0xE6,0xE3,0xE8,0xE8,0xEA,0xEA,0xEE,0xED,0xEE,0xEF,0xF0,0xF1,0xF2,0xF3,0xF4,0xF5,0xF6,0xF7,0xF8,0xF9,0xFA,0xFB,0xFC,0xFD,0xFE,0xFF}

#elif FATFS_CODE_PAGE == 850	/* Multilingual Latin 1 (OEM) */
#define _EXCVT {0x80,0x9A,0x90,0xB6,0x8E,0xB7,0x8F,0x80,0xD2,0xD3,0xD4,0xD8,0xD7,0xDE,0x8E,0x8F,0x90,0x92,0x92,0xE2,0x99,0xE3,0xEA,0xEB,0x59,0x99,0x9A,0x9D,0x9C,0x9D,0x9E,0x9F, \
    0xB5,0xD6,0xE0,0xE9,0xA5,0xA5,0xA6,0xA7,0xA8,0xA9,0xAA,0xAB,0xAC,0x21,0xAE,0xAF,0xB0,0xB1,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xB8,0xB9,0xBA,0xBB,0xBC,0xBD,0xBE,0xBF, \
    0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC7,0xC7,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,0xD0,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xDF, \
    0xE0,0xE1,0xE2,0xE3,0xE5,0xE5,0xE6,0xE7,0xE7,0xE9,0xEA,0xEB,0xED,0xED,0xEE,0xEF,0xF0,0xF1,0xF2,0xF3,0xF4,0xF5,0xF6,0xF7,0xF8,0xF9,0xFA,0xFB,0xFC,0xFD,0xFE,0xFF}

#elif FATFS_CODE_PAGE == 852	/* Latin 2 (OEM) */
#define _EXCVT {0x80,0x9A,0x90,0xB6,0x8E,0xDE,0x8F,0x80,0x9D,0xD3,0x8A,0x8A,0xD7,0x8D,0x8E,0x8F,0x90,0x91,0x91,0xE2,0x99,0x95,0x95,0x97,0x97,0x99,0x9A,0x9B,0x9B,0x9D,0x9E,0x9F, \
    0xB5,0xD6,0xE0,0xE9,0xA4,0xA4,0xA6,0xA6,0xA8,0xA8,0xAA,0x8D,0xAC,0xB8,0xAE,0xAF,0xB0,0xB1,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xB8,0xB9,0xBA,0xBB,0xBC,0xBD,0xBD,0xBF, \
    0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC6,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,0xD1,0xD1,0xD2,0xD3,0xD2,0xD5,0xD6,0xD7,0xB7,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xDF, \
    0xE0,0xE1,0xE2,0xE3,0xE3,0xD5,0xE6,0xE6,0xE8,0xE9,0xE8,0xEB,0xED,0xED,0xDD,0xEF,0xF0,0xF1,0xF2,0xF3,0xF4,0xF5,0xF6,0xF7,0xF8,0xF9,0xFA,0xEB,0xFC,0xFC,0xFE,0xFF}

#elif FATFS_CODE_PAGE == 855	/* Cyrillic (OEM) */
#define _EXCVT {0x81,0x81,0x83,0x83,0x85,0x85,0x87,0x87,0x89,0x89,0x8B,0x8B,0x8D,0x8D,0x8F,0x8F,0x91,0x91,0x93,0x93,0x95,0x95,0x97,0x97,0x99,0x99,0x9B,0x9B,0x9D,0x9D,0x9F,0x9F, \
    0xA1,0xA1,0xA3,0xA3,0xA5,0xA5,0xA7,0xA7,0xA9,0xA9,0xAB,0xAB,0xAD,0xAD,0xAE,0xAF,0xB0,0xB1,0xB2,0xB3,0xB4,0xB6,0xB6,0xB8,0xB8,0xB9,0xBA,0xBB,0xBC,0xBE,0xBE,0xBF, \
    0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC7,0xC7,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,0xD1,0xD1,0xD3,0xD3,0xD5,0xD5,0xD7,0xD7,0xDD,0xD9,0xDA,0xDB,0xDC,0xDD,0xE0,0xDF, \
    0xE0,0xE2,0xE2,0xE4,0xE4,0xE6,0xE6,0xE8,0xE8,0xEA,0xEA,0xEC,0xEC,0xEE,0xEE,0xEF,0xF0,0xF2,0xF2,0xF4,0xF4,0xF6,0xF6,0xF8,0xF8,0xFA,0xFA,0xFC,0xFC,0xFD,0xFE,0xFF}

#elif FATFS_CODE_PAGE == 857	/* Turkish (OEM) */
#define _EXCVT {0x80,0x9A,0x90,0xB6,0x8E,0xB7,0x8F,0x80,0xD2,0xD3,0xD4,0xD8,0xD7,0x98,0x8E,0x8F,0x90,0x92,0x92,0xE2,0x99,0xE3,0xEA,0xEB,0x98,0x99,0x9A,0x9D,0x9C,0x9D,0x9E,0x9E, \
    0xB5,0xD6,0xE0,0xE9,0xA5,0xA5,0xA6,0xA6,0xA8,0xA9,0xAA,0xAB,0xAC,0x21,0xAE,0xAF,0xB0,0xB1,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xB8,0xB9,0xBA,0xBB,0xBC,0xBD,0xBE,0xBF, \
    0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC7,0xC7,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,0xD0,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xDF, \
    0xE0,0xE1,0xE2,0xE3,0xE5,0xE5,0xE6,0xE7,0xE8,0xE9,0xEA,0xEB,0xDE,0x59,0xEE,0xEF,0xF0,0xF1,0xF2,0xF3,0xF4,0xF5,0xF6,0xF7,0xF8,0xF9,0xFA,0xFB,0xFC,0xFD,0xFE,0xFF}

#elif FATFS_CODE_PAGE == 858	/* Multilingual Latin 1 + Euro (OEM) */
#define _EXCVT {0x80,0x9A,0x90,0xB6,0x8E,0xB7,0x8F,0x80,0xD2,0xD3,0xD4,0xD8,0xD7,0xDE,0x8E,0x8F,0x90,0x92,0x92,0xE2,0x99,0xE3,0xEA,0xEB,0x59,0x99,0x9A,0x9D,0x9C,0x9D,0x9E,0x9F, \
    0xB5,0xD6,0xE0,0xE9,0xA5,0xA5,0xA6,0xA7,0xA8,0xA9,0xAA,0xAB,0xAC,0x21,0xAE,0xAF,0xB0,0xB1,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xB8,0xB9,0xBA,0xBB,0xBC,0xBD,0xBE,0xBF, \
    0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC7,0xC7,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,0xD1,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xDF, \
    0xE0,0xE1,0xE2,0xE3,0xE5,0xE5,0xE6,0xE7,0xE7,0xE9,0xEA,0xEB,0xED,0xED,0xEE,0xEF,0xF0,0xF1,0xF2,0xF3,0xF4,0xF5,0xF6,0xF7,0xF8,0xF9,0xFA,0xFB,0xFC,0xFD,0xFE,0xFF}

#elif FATFS_CODE_PAGE == 862	/* Hebrew (OEM) */
#define _EXCVT {0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8A,0x8B,0x8C,0x8D,0x8E,0x8F,0x90,0x91,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9A,0x9B,0x9C,0x9D,0x9E,0x9F, \
    0x41,0x49,0x4F,0x55,0xA5,0xA5,0xA6,0xA7,0xA8,0xA9,0xAA,0xAB,0xAC,0x21,0xAE,0xAF,0xB0,0xB1,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xB8,0xB9,0xBA,0xBB,0xBC,0xBD,0xBE,0xBF, \
    0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,0xD0,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xDF, \
    0xE0,0xE1,0xE2,0xE3,0xE4,0xE5,0xE6,0xE7,0xE8,0xE9,0xEA,0xEB,0xEC,0xED,0xEE,0xEF,0xF0,0xF1,0xF2,0xF3,0xF4,0xF5,0xF6,0xF7,0xF8,0xF9,0xFA,0xFB,0xFC,0xFD,0xFE,0xFF}

#elif FATFS_CODE_PAGE == 866	/* Russian (OEM) */
#define _EXCVT {0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8A,0x8B,0x8C,0x8D,0x8E,0x8F,0x90,0x91,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9A,0x9B,0x9C,0x9D,0x9E,0x9F, \
    0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8A,0x8B,0x8C,0x8D,0x8E,0x8F,0xB0,0xB1,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xB8,0xB9,0xBA,0xBB,0xBC,0xBD,0xBE,0xBF, \
    0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,0xD0,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xDF, \
    0x90,0x91,0x92,0x93,0x9d,0x95,0x96,0x97,0x98,0x99,0x9A,0x9B,0x9C,0x9D,0x9E,0x9F,0xF0,0xF0,0xF2,0xF2,0xF4,0xF4,0xF6,0xF6,0xF8,0xF9,0xFA,0xFB,0xFC,0xFD,0xFE,0xFF}

#elif FATFS_CODE_PAGE == 874	/* Thai (OEM, Windows) */
#define _EXCVT {0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8A,0x8B,0x8C,0x8D,0x8E,0x8F,0x90,0x91,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9A,0x9B,0x9C,0x9D,0x9E,0x9F, \
    0xA0,0xA1,0xA2,0xA3,0xA4,0xA5,0xA6,0xA7,0xA8,0xA9,0xAA,0xAB,0xAC,0xAD,0xAE,0xAF,0xB0,0xB1,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xB8,0xB9,0xBA,0xBB,0xBC,0xBD,0xBE,0xBF, \
    0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,0xD0,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xDF, \
    0xE0,0xE1,0xE2,0xE3,0xE4,0xE5,0xE6,0xE7,0xE8,0xE9,0xEA,0xEB,0xEC,0xED,0xEE,0xEF,0xF0,0xF1,0xF2,0xF3,0xF4,0xF5,0xF6,0xF7,0xF8,0xF9,0xFA,0xFB,0xFC,0xFD,0xFE,0xFF}

#elif FATFS_CODE_PAGE == 1250 /* Central Europe (Windows) */
#define _EXCVT {0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8A,0x8B,0x8C,0x8D,0x8E,0x8F,0x90,0x91,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x8A,0x9B,0x8C,0x8D,0x8E,0x8F, \
    0xA0,0xA1,0xA2,0xA3,0xA4,0xA5,0xA6,0xA7,0xA8,0xA9,0xAA,0xAB,0xAC,0xAD,0xAE,0xAF,0xB0,0xB1,0xB2,0xA3,0xB4,0xB5,0xB6,0xB7,0xB8,0xA5,0xAA,0xBB,0xBC,0xBD,0xBC,0xAF, \
    0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,0xD0,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xDF, \
    0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,0xD0,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,0xF7,0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xFF}

#elif FATFS_CODE_PAGE == 1251 /* Cyrillic (Windows) */
#define _EXCVT {0x80,0x81,0x82,0x82,0x84,0x85,0x86,0x87,0x88,0x89,0x8A,0x8B,0x8C,0x8D,0x8E,0x8F,0x80,0x91,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x8A,0x9B,0x8C,0x8D,0x8E,0x8F, \
    0xA0,0xA2,0xA2,0xA3,0xA4,0xA5,0xA6,0xA7,0xA8,0xA9,0xAA,0xAB,0xAC,0xAD,0xAE,0xAF,0xB0,0xB1,0xB2,0xB2,0xA5,0xB5,0xB6,0xB7,0xA8,0xB9,0xAA,0xBB,0xA3,0xBD,0xBD,0xAF, \
    0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,0xD0,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xDF, \
    0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,0xD0,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xDF}

#elif FATFS_CODE_PAGE == 1252 /* Latin 1 (Windows) */
#define _EXCVT {0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8A,0x8B,0x8C,0x8D,0x8E,0x8F,0x90,0x91,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0xAd,0x9B,0x8C,0x9D,0xAE,0x9F, \
    0xA0,0x21,0xA2,0xA3,0xA4,0xA5,0xA6,0xA7,0xA8,0xA9,0xAA,0xAB,0xAC,0xAD,0xAE,0xAF,0xB0,0xB1,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xB8,0xB9,0xBA,0xBB,0xBC,0xBD,0xBE,0xBF, \
    0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,0xD0,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xDF, \
    0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,0xD0,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,0xF7,0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0x9F}

#elif FATFS_CODE_PAGE == 1253 /* Greek (Windows) */
#define _EXCVT {0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8A,0x8B,0x8C,0x8D,0x8E,0x8F,0x90,0x91,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9A,0x9B,0x9C,0x9D,0x9E,0x9F, \
    0xA0,0xA1,0xA2,0xA3,0xA4,0xA5,0xA6,0xA7,0xA8,0xA9,0xAA,0xAB,0xAC,0xAD,0xAE,0xAF,0xB0,0xB1,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xB8,0xB9,0xBA,0xBB,0xBC,0xBD,0xBE,0xBF, \
    0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,0xD0,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,0xD8,0xD9,0xDA,0xDB,0xA2,0xB8,0xB9,0xBA, \
    0xE0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,0xD0,0xD1,0xF2,0xD3,0xD4,0xD5,0xD6,0xD7,0xD8,0xD9,0xDA,0xFB,0xBC,0xFD,0xBF,0xFF}

#elif FATFS_CODE_PAGE == 1254 /* Turkish (Windows) */
#define _EXCVT {0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8A,0x8B,0x8C,0x8D,0x8E,0x8F,0x90,0x91,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x8A,0x9B,0x8C,0x9D,0x9E,0x9F, \
    0xA0,0x21,0xA2,0xA3,0xA4,0xA5,0xA6,0xA7,0xA8,0xA9,0xAA,0xAB,0xAC,0xAD,0xAE,0xAF,0xB0,0xB1,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xB8,0xB9,0xBA,0xBB,0xBC,0xBD,0xBE,0xBF, \
    0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,0xD0,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xDF, \
    0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,0xD0,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,0xF7,0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0x9F}

#elif FATFS_CODE_PAGE == 1255 /* Hebrew (Windows) */
#define _EXCVT {0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8A,0x8B,0x8C,0x8D,0x8E,0x8F,0x90,0x91,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9A,0x9B,0x9C,0x9D,0x9E,0x9F, \
    0xA0,0x21,0xA2,0xA3,0xA4,0xA5,0xA6,0xA7,0xA8,0xA9,0xAA,0xAB,0xAC,0xAD,0xAE,0xAF,0xB0,0xB1,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xB8,0xB9,0xBA,0xBB,0xBC,0xBD,0xBE,0xBF, \
    0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,0xD0,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xDF, \
    0xE0,0xE1,0xE2,0xE3,0xE4,0xE5,0xE6,0xE7,0xE8,0xE9,0xEA,0xEB,0xEC,0xED,0xEE,0xEF,0xF0,0xF1,0xF2,0xF3,0xF4,0xF5,0xF6,0xF7,0xF8,0xF9,0xFA,0xFB,0xFC,0xFD,0xFE,0xFF}

#elif FATFS_CODE_PAGE == 1256 /* Arabic (Windows) */
#define _EXCVT {0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8A,0x8B,0x8C,0x8D,0x8E,0x8F,0x90,0x91,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9A,0x9B,0x8C,0x9D,0x9E,0x9F, \
    0xA0,0xA1,0xA2,0xA3,0xA4,0xA5,0xA6,0xA7,0xA8,0xA9,0xAA,0xAB,0xAC,0xAD,0xAE,0xAF,0xB0,0xB1,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xB8,0xB9,0xBA,0xBB,0xBC,0xBD,0xBE,0xBF, \
    0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,0xD0,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xDF, \
    0x41,0xE1,0x41,0xE3,0xE4,0xE5,0xE6,0x43,0x45,0x45,0x45,0x45,0xEC,0xED,0x49,0x49,0xF0,0xF1,0xF2,0xF3,0x4F,0xF5,0xF6,0xF7,0xF8,0x55,0xFA,0x55,0x55,0xFD,0xFE,0xFF}

#elif FATFS_CODE_PAGE == 1257 /* Baltic (Windows) */
#define _EXCVT {0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8A,0x8B,0x8C,0x8D,0x8E,0x8F,0x90,0x91,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9A,0x9B,0x9C,0x9D,0x9E,0x9F, \
    0xA0,0xA1,0xA2,0xA3,0xA4,0xA5,0xA6,0xA7,0xA8,0xA9,0xAA,0xAB,0xAC,0xAD,0xAE,0xAF,0xB0,0xB1,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xA8,0xB9,0xAA,0xBB,0xBC,0xBD,0xBE,0xAF, \
    0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,0xD0,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xDF, \
    0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,0xD0,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,0xF7,0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xFF}

#elif FATFS_CODE_PAGE == 1258 /* Vietnam (OEM, Windows) */
#define _EXCVT {0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8A,0x8B,0x8C,0x8D,0x8E,0x8F,0x90,0x91,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9A,0x9B,0xAC,0x9D,0x9E,0x9F, \
    0xA0,0x21,0xA2,0xA3,0xA4,0xA5,0xA6,0xA7,0xA8,0xA9,0xAA,0xAB,0xAC,0xAD,0xAE,0xAF,0xB0,0xB1,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xB8,0xB9,0xBA,0xBB,0xBC,0xBD,0xBE,0xBF, \
    0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xCB,0xCC,0xCD,0xCE,0xCF,0xD0,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,0xDE,0xDF, \
    0xC0,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xCB,0xEC,0xCD,0xCE,0xCF,0xD0,0xD1,0xF2,0xD3,0xD4,0xD5,0xD6,0xF7,0xD8,0xD9,0xDA,0xDB,0xDC,0xDD,0xFE,0x9F}

#else
#error Unknown code page.

#endif



/* Character code support macros */

#define IsUpper(c)	(((c)>='A')&&((c)<='Z'))
#define IsLower(c)	(((c)>='a')&&((c)<='z'))

#ifndef _EXCVT	/* DBCS configuration */

#ifdef _DF2S	/* Two 1st byte areas */
#define IsDBCS1(c)	(((uint8_t)(c) >= _DF1S && (uint8_t)(c) <= _DF1E) || ((uint8_t)(c) >= _DF2S && (uint8_t)(c) <= _DF2E))
#else			/* One 1st byte area */
#define IsDBCS1(c)	((uint8_t)(c) >= _DF1S && (uint8_t)(c) <= _DF1E)
#endif

#ifdef _DS3S	/* Three 2nd byte areas */
#define IsDBCS2(c)	(((uint8_t)(c) >= _DS1S && (uint8_t)(c) <= _DS1E) || ((uint8_t)(c) >= _DS2S && (uint8_t)(c) <= _DS2E) || ((uint8_t)(c) >= _DS3S && (uint8_t)(c) <= _DS3E))
#else			/* Two 2nd byte areas */
#define IsDBCS2(c)	(((uint8_t)(c) >= _DS1S && (uint8_t)(c) <= _DS1E) || ((uint8_t)(c) >= _DS2S && (uint8_t)(c) <= _DS2E))
#endif

#else			/* SBCS configuration */

#define IsDBCS1(c)	0
#define IsDBCS2(c)	0

#endif /* _EXCVT */


/* FatFs refers the members in the FAT structures with byte offset instead
   / of structure member because there are incompatibility of the packing option
   / between various compilers. */

#define BS_jmpBoot			0
#define BS_OEMName			3
#define BPB_BytsPerSec		11
#define BPB_SecPerClus		13
#define BPB_RsvdSecCnt		14
#define BPB_NumFATs			16
#define BPB_RootEntCnt		17
#define BPB_TotSec16		19
#define BPB_Media			21
#define BPB_FATSz16			22
#define BPB_SecPerTrk		24
#define BPB_NumHeads		26
#define BPB_HiddSec			28
#define BPB_TotSec32		32
#define BS_55AA				510

#define BS_DrvNum			36
#define BS_BootSig			38
#define BS_VolID			39
#define BS_VolLab			43
#define BS_FilSysType		54

#define BPB_FATSz32			36
#define BPB_ExtFlags		40
#define BPB_FSVer			42
#define BPB_RootClus		44
#define BPB_FSInfo			48
#define BPB_BkBootSec		50
#define BS_DrvNum32			64
#define BS_BootSig32		66
#define BS_VolID32			67
#define BS_VolLab32			71
#define BS_FilSysType32		82

#define MBR_Table			446

#define	DIR_Name			0
#define	DIR_Attr			11
#define	DIR_NTres			12
#define	DIR_CrtTime			14
#define	DIR_CrtDate			16
#define	DIR_FstClusHI		20
#define	DIR_WrtTime			22
#define	DIR_WrtDate			24
#define	DIR_FstClusLO		26
#define	DIR_FileSize		28


/*-----------------------------------------------------------------------*/
/* FAT access - Read value of a FAT entry                                */
/*-----------------------------------------------------------------------*/
	/* 1:IO error, Else:Cluster status */
static
fatfs_cluster get_fat (struct fatfs_disk *f, fatfs_cluster clst)
{
    uint8_t buf[4];
    struct fatfs *fs = f->fs;

    if (clst < 2 || clst >= fs->n_fatent)	/* Range check */
        return 1;

    switch (fs->fs_type) {
#if FATFS_FAT12
        case FS_FAT12 : {
                            unsigned int wc, bc, ofs;

                            bc = (unsigned int)clst; bc += bc / 2;
                            ofs = bc % 512; bc /= 512;
                            if (ofs != 511) {
                                if (disk_readp(f, buf, fs->fatbase + bc, ofs, 2)) break;
                            } else {
                                if (disk_readp(f, buf, fs->fatbase + bc, 511, 1)) break;
                                if (disk_readp(f, buf+1, fs->fatbase + bc + 1, 0, 1)) break;
                            }
                            wc = LD_WORD(buf);
                            return (clst & 1) ? (wc >> 4) : (wc & 0xFFF);
                        }
#endif
#if FATFS_FAT16
        case FS_FAT16 :
                        if (disk_readp(f, buf, fs->fatbase + clst / 256, ((unsigned int)clst % 256) * 2, 2)) break;
                        return LD_WORD(buf);
#endif
#if FATFS_FAT32
        case FS_FAT32 :
                        if (disk_readp(f, buf, fs->fatbase + clst / 128, ((unsigned int)clst % 128) * 4, 4)) break;
                        return LD_DWORD(buf) & 0x0FFFFFFF;
#endif
    }

    return 1;	/* An error occured at the disk I/O layer */
}




/*-----------------------------------------------------------------------*/
/* Get sector# from cluster# / Get cluster field from directory entry    */
/*-----------------------------------------------------------------------*/
/* !=0: Sector number, 0: Failed - invalid cluster# */

static uint32_t clust2sect (struct fatfs_disk *f, fatfs_cluster clst)
{
    struct fatfs *fs = f->fs;
    clst -= 2;
    if (clst >= (fs->n_fatent - 2)) return 0;		/* Invalid cluster# */
    return (uint32_t)clst * fs->csize + fs->database;
}


    static
fatfs_cluster get_clust ( struct fatfs_disk *f, 
        uint8_t* dir		/* Pointer to directory entry */
        )
{
    struct fatfs *fs = f->fs;
    fatfs_cluster clst = 0;


    if (FATFS_FAT32_ONLY || (FATFS_FAT32 && fs->fs_type == FS_FAT32)) {
        clst = LD_WORD(dir+DIR_FstClusHI);
        clst <<= 16;
    }
    clst |= LD_WORD(dir+DIR_FstClusLO);

    return clst;
}



/*-----------------------------------------------------------------------*/
/* Check a sector if it is an FAT boot record                            */
/*-----------------------------------------------------------------------*/


static int check_fs(struct fatfs_disk *f, uint8_t *buf, uint32_t sect)
{
    if (!f || !f->blockdev || !f->blockdev->owner->ops.block_read) {
        return -1;
    }
    if (disk_readp(f, buf, sect, 510, 2) != 0)
        return 3;

    if (LD_WORD(buf) != 0xAA55)				/* Check record signature */
        return 2;

    if (!FATFS_FAT32_ONLY && !disk_readp(f, buf, sect, BS_FilSysType, 2) && LD_WORD(buf) == 0x4146)	/* Check FAT12/16 */
        return 0;
    if (FATFS_FAT32 && !disk_readp(f, buf, sect, BS_FilSysType32, 2) && LD_WORD(buf) == 0x4146)	/* Check FAT32 */
        return 0;
    return 1;
}


static int fatfs_mount(char *source, char *tgt, uint32_t flags, void *arg)
{
    struct fnode *tgt_dir = NULL;
    struct fnode *src_dev = NULL;
    uint8_t fmt, buf[36];
    uint32_t bsect, fsize, tsect, mclst;
    struct fatfs_disk *fsd;

    /* Source must NOT be NULL */
    if (!source)
        return -1;

    /* Target must be a valid dir */
    if (!tgt)
        return -1;
    
    tgt_dir = fno_search(tgt);
    src_dev = fno_search(tgt);

    if (!tgt_dir || ((tgt_dir->flags & FL_DIR) == 0)) {
        /* Not a valid mountpoint. */
        return -1;
    }

    if (!src_dev || !(src_dev ->owner) || ((src_dev->flags & FL_BLK) == 0)) {
        /* Invalid block device. */
        return -1;
    }
   
    /* Initialize file system to disk association */
    fsd = kcalloc(sizeof(struct fatfs_disk), 1);
    if (!fsd)
        return -1;

    /* Associate the disk device */
    fsd->blockdev = src_dev;
    
    /* Associate a newly created fat filesystem */
    fsd->fs = kcalloc(sizeof(struct fatfs), 1);
    if (!fsd->fs) {
        kfree(fsd);
        return -1;
    }

    /* Associate the mount point */
    fsd->mountpoint = tgt_dir;
    tgt_dir->owner = &mod_fatfs;


    /* Search FAT partition on the drive */
    bsect = 0;
    fmt = check_fs(fsd, buf, bsect);			/* Check sector 0 as an SFD format */
    if (fmt == 1) {						/* Not an FAT boot record, it may be FDISK format */
        /* Check a partition listed in top of the partition table */
        if (disk_readp(fsd, buf, bsect, MBR_Table, 16)) {	/* 1st partition entry */
            fmt = 3;
        } else {
            if (buf[4]) {					/* Is the partition existing? */
                bsect = LD_DWORD(&buf[8]);	/* Partition offset in LBA */
                fmt = check_fs(fsd, buf, bsect);	/* Check the partition */
            }
        }
    }
    if (fmt != 0) 
        goto fail;

    /* Initialize the file system object */
    if (disk_readp(fsd, buf, bsect, 13, sizeof (buf))) return FR_DISK_ERR;

    fsize = LD_WORD(buf+BPB_FATSz16-13);				/* Number of sectors per FAT */
    if (!fsize) fsize = LD_DWORD(buf+BPB_FATSz32-13);

    fsize *= buf[BPB_NumFATs-13];						/* Number of sectors in FAT area */
    fsd->fs->fatbase = bsect + LD_WORD(buf+BPB_RsvdSecCnt-13); /* FAT start sector (lba) */
    fsd->fs->csize = buf[BPB_SecPerClus-13];					/* Number of sectors per cluster */
    fsd->fs->n_rootdir = LD_WORD(buf+BPB_RootEntCnt-13);		/* Nmuber of root directory entries */
    tsect = LD_WORD(buf+BPB_TotSec16-13);				/* Number of sectors on the file system */
    if (!tsect) tsect = LD_DWORD(buf+BPB_TotSec32-13);
    mclst = (tsect						/* Last cluster# + 1 */
            - LD_WORD(buf+BPB_RsvdSecCnt-13) - fsize - fsd->fs->n_rootdir / 16
            ) / fsd->fs->csize + 2;
    fsd->fs->n_fatent = (fatfs_cluster)mclst;

    fmt = 0;							/* Determine the FAT sub type */
    if (FATFS_FAT12 && mclst < 0xFF7)
        fmt = FS_FAT12;
    if (FATFS_FAT16 && mclst >= 0xFF8 && mclst < 0xFFF7)
        fmt = FS_FAT16;
    if (FATFS_FAT32 && mclst >= 0xFFF7)
        fmt = FS_FAT32;
    if (!fmt) 
        goto fail;
    fsd->fs->fs_type = fmt;

    if (FATFS_FAT32_ONLY || (FATFS_FAT32 && fmt == FS_FAT32))
        fsd->fs->dirbase = LD_DWORD(buf+(BPB_RootClus-13));	/* Root directory start cluster */
    else
        fsd->fs->dirbase = fsd->fs->fatbase + fsize;				/* Root directory start sector (lba) */
    fsd->fs->database = fsd->fs->fatbase + fsize + fsd->fs->n_rootdir / 16;	/* Data start sector (lba) */
    fsd->fs->flag = 0;
    return 0;

fail:
    kfree(fsd->fs);
    kfree(fsd);
    return -1;
}


#if 0

/*-----------------------------------------------------------------------*/
/* Open or Create a File                                                 */
/*-----------------------------------------------------------------------*/

static
int fatfs_open (struct fatfs_disk *f,
        const char *path	/* Pointer to the file name */
        )
{
    int res;
    struct fatfs_dir dj;
    uint8_t sp[12], dir[32];
    struct fatfs *fs = f->fs;


    if (!fs) return FR_NOT_ENABLED;		/* Check file system */

    fs->flag = 0;
    dj.fn = sp;
    res = follow_path(&dj, dir, path);	/* Follow the file path */
    if (res != FR_OK) return res;		/* Follow failed */
    if (!dir[0] || (dir[DIR_Attr] & AM_DIR))	/* It is a directory */
        return FR_NO_FILE;

    fs->org_clust = get_clust(f, dir);		/* File start cluster */
    fs->fsize = LD_DWORD(dir+DIR_FileSize);	/* File size */
    fs->fptr = 0;						/* File pointer */
    fs->flag = FA_OPENED;

    return FR_OK;
}




/*-----------------------------------------------------------------------*/
/* Read File                                                             */
/*-----------------------------------------------------------------------*/

static
int fatfs_read ( struct fatfs_disk *f,
        void* buff,		/* Pointer to the read buffer (NULL:Forward data to the stream)*/
        unsigned int btr,		/* Number of bytes to read */
        unsigned int* br		/* Pointer to number of bytes read */
        )
{
    int dr;
    fatfs_cluster clst;
    uint32_t sect, remain;
    unsigned int rcnt;
    uint8_t cs, *rbuff = buff;
    struct fatfs *fs = f->fs;


    *br = 0;
    if (!fs) return FR_NOT_ENABLED;		/* Check file system */
    if (!(fs->flag & FA_OPENED))		/* Check if opened */
        return FR_NOT_OPENED;

    remain = fs->fsize - fs->fptr;
    if (btr > remain) btr = (unsigned int)remain;			/* Truncate btr by remaining bytes */

    while (btr)	{									/* Repeat until all data transferred */
        if ((fs->fptr % 512) == 0) {				/* On the sector boundary? */
            cs = (uint8_t)(fs->fptr / 512 & (fs->csize - 1));	/* Sector offset in the cluster */
            if (!cs) {								/* On the cluster boundary? */
                if (fs->fptr == 0)					/* On the top of the file? */
                    clst = fs->org_clust;
                else
                    clst = get_fat(fs->curr_clust);
                if (clst <= 1) ABORT(FR_DISK_ERR);
                fs->curr_clust = clst;				/* Update current cluster */
            }
            sect = clust2sect(f, fs->curr_clust);		/* Get current sector */
            if (!sect) ABORT(FR_DISK_ERR);
            fs->dsect = sect + cs;
        }
        rcnt = 512 - (unsigned int)fs->fptr % 512;			/* Get partial sector data from sector buffer */
        if (rcnt > btr) rcnt = btr;
        dr = disk_readp(!buff ? 0 : rbuff, fs->dsect, (unsigned int)fs->fptr % 512, rcnt);
        if (dr) ABORT(FR_DISK_ERR);
        fs->fptr += rcnt; rbuff += rcnt;			/* Update pointers and counters */
        btr -= rcnt; *br += rcnt;
    }

    return FR_OK;
}



/*-----------------------------------------------------------------------*/
/* Write File                                                            */
/*-----------------------------------------------------------------------*/
static
int fatfs_write (struct fatfs_disk *f,
        const void* buff,	/* Pointer to the data to be written */
        unsigned int btw,			/* Number of bytes to write (0:Finalize the current write operation) */
        unsigned int* bw			/* Pointer to number of bytes written */
        )
{
    fatfs_cluster clst;
    uint32_t sect, remain;
    const uint8_t *p = buff;
    uint8_t cs;
    unsigned int wcnt;
    struct fatfs *fs = f->fs;


    *bw = 0;
    if (!fs) return FR_NOT_ENABLED;		/* Check file system */
    if (!(fs->flag & FA_OPENED))		/* Check if opened */
        return FR_NOT_OPENED;

    if (!btw) {		/* Finalize request */
        if ((fs->flag & FA__WIP) && disk_writep(0, 0)) ABORT(FR_DISK_ERR);
        fs->flag &= ~FA__WIP;
        return FR_OK;
    } else {		/* Write data request */
        if (!(fs->flag & FA__WIP))		/* Round-down fptr to the sector boundary */
            fs->fptr &= 0xFFFFFE00;
    }
    remain = fs->fsize - fs->fptr;
    if (btw > remain) btw = (unsigned int)remain;			/* Truncate btw by remaining bytes */

    while (btw)	{									/* Repeat until all data transferred */
        if ((unsigned int)fs->fptr % 512 == 0) {			/* On the sector boundary? */
            cs = (uint8_t)(fs->fptr / 512 & (fs->csize - 1));	/* Sector offset in the cluster */
            if (!cs) {								/* On the cluster boundary? */
                if (fs->fptr == 0)					/* On the top of the file? */
                    clst = fs->org_clust;
                else
                    clst = get_fat(fs->curr_clust);
                if (clst <= 1) ABORT(FR_DISK_ERR);
                fs->curr_clust = clst;				/* Update current cluster */
            }
            sect = clust2sect(f, fs->curr_clust);		/* Get current sector */
            if (!sect) ABORT(FR_DISK_ERR);
            fs->dsect = sect + cs;
            if (disk_writep(0, fs->dsect)) ABORT(FR_DISK_ERR);	/* Initiate a sector write operation */
            fs->flag |= FA__WIP;
        }
        wcnt = 512 - (unsigned int)fs->fptr % 512;			/* Number of bytes to write to the sector */
        if (wcnt > btw) wcnt = btw;
        if (disk_writep(p, wcnt)) ABORT(FR_DISK_ERR);	/* Send data to the sector */
        fs->fptr += wcnt; p += wcnt;				/* Update pointers and counters */
        btw -= wcnt; *bw += wcnt;
        if ((unsigned int)fs->fptr % 512 == 0) {
            if (disk_writep(0, 0)) ABORT(FR_DISK_ERR);	/* Finalize the currtent secter write operation */
            fs->flag &= ~FA__WIP;
        }
    }

    return FR_OK;
}



/*-----------------------------------------------------------------------*/
/* Seek File R/W Pointer                                                 */
/*-----------------------------------------------------------------------*/

static
int fatfs_lseek (struct fatfs_disk *f,
        uint32_t ofs		/* File pointer from top of file */
        )
{
    fatfs_cluster clst;
    uint32_t bcs, sect, ifptr;
    struct fatfs *fs = f->fs;


    if (!fs) return FR_NOT_ENABLED;		/* Check file system */
    if (!(fs->flag & FA_OPENED))		/* Check if opened */
        return FR_NOT_OPENED;

    if (ofs > fs->fsize) ofs = fs->fsize;	/* Clip offset with the file size */
    ifptr = fs->fptr;
    fs->fptr = 0;
    if (ofs > 0) {
        bcs = (uint32_t)fs->csize * 512;	/* Cluster size (byte) */
        if (ifptr > 0 &&
                (ofs - 1) / bcs >= (ifptr - 1) / bcs) {	/* When seek to same or following cluster, */
            fs->fptr = (ifptr - 1) & ~(bcs - 1);	/* start from the current cluster */
            ofs -= fs->fptr;
            clst = fs->curr_clust;
        } else {							/* When seek to back cluster, */
            clst = fs->org_clust;			/* start from the first cluster */
            fs->curr_clust = clst;
        }
        while (ofs > bcs) {				/* Cluster following loop */
            clst = get_fat(clst);		/* Follow cluster chain */
            if (clst <= 1 || clst >= fs->n_fatent) ABORT(FR_DISK_ERR);
            fs->curr_clust = clst;
            fs->fptr += bcs;
            ofs -= bcs;
        }
        fs->fptr += ofs;
        sect = clust2sect(f, clst);		/* Current sector */
        if (!sect) ABORT(FR_DISK_ERR);
        fs->dsect = sect + (fs->fptr / 512 & (fs->csize - 1));
    }

    return FR_OK;
}
#endif

void fatfs_init(void)
{
    mod_fatfs.family = FAMILY_FILE;
    strcpy(mod_fatfs.name,"fatfs");

    mod_fatfs.mount = fatfs_mount;
    /*
    mod_fatfs.ops.read = fatfs_read; 
    mod_fatfs.ops.poll = fatfs_poll;
    mod_fatfs.ops.write = fatfs_write;
    mod_fatfs.ops.seek = fatfs_seek;
    mod_fatfs.ops.creat = fatfs_creat;
    mod_fatfs.ops.unlink = fatfs_unlink;
    mod_fatfs.ops.close = fatfs_close;
    mod_fatfs.ops.exe = fatfs_exe;
    */
    register_module(&mod_fatfs);
}