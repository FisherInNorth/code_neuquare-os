#ifndef __FAT32_DISK_H__
#define __FAT32_DISK_H__

#include "common.h"

/*
    FAT32
    +---------------------------+
    | 0:    Boot Sector         |
    +---------------------------+
    | 1:       FsInfo           |
    +---------------------------+
    | 6: Backup BPB Structure   |
    +---------------------------+
    | 7: Backup FsInfo Structure|
    +---------------------------+
    |     FAT Region (FAT1)     |
    +---------------------------+
    |     FAT Region (FAT2)     |
    +---------------------------+
    |    Data Region (Cluster 2)|
    +---------------------------+
    |    Data Region (Cluster 3)|
    +---------------------------+
    |           ...             |
    +---------------------------+
    |    Data Region (Cluster n)|
    +---------------------------+
*/
// /*BPB (BIOS Parameter Block)*/
// #define BS_JmpBoot 0      /* jump instruction (3-byte) */
// #define BS_OEMName 3      /* OEM name (8-byte) */
// #define BPB_BytsPerSec 11 /* Sector size [byte] (WORD) */
// #define BPB_SecPerClus 13 /* Cluster size [sector] (BYTE) */
// #define BPB_RsvdSecCnt 14 /* Size of reserved area [sector] (WORD) */
// #define BPB_NumFATs 16    /* Number of FATs (BYTE) */
// #define BPB_RootEntCnt 17 /* Size of root directory area for FAT [entry] (WORD) */
// #define BPB_TotSec16 19   /* Volume size (16-bit) [sector] (WORD) */
// #define BPB_Media 21      /* Media descriptor byte (BYTE) */
// #define BPB_FATSz16 22    /* FAT size (16-bit) [sector] (WORD) */
// #define BPB_SecPerTrk 24  /* Number of sectors per track for int13h [sector] (WORD) */
// #define BPB_NumHeads 26   /* Number of heads for int13h (WORD) */
// #define BPB_HiddSec 28    /* Volume offset from top of the drive (DWORD) */
// #define BPB_TotSec 32     /* Volume size (32-bit) [sector] (DWORD) */

// /*Extended Boot Record*/
// #define BPB_FATSz 36     /* FAT32: FAT size [sector] (DWORD) */
// #define BPB_ExtFlags 40  /* FAT32: Extended flags (WORD) */
// #define BPB_FSVer 42     /* FAT32: Filesystem version (WORD) */
// #define BPB_RootClus 44  /* FAT32: Root directory cluster (DWORD) */
// #define BPB_FSInfo 48    /* FAT32: Offset of FSINFO sector (WORD) */
// #define BPB_BkBootSec 50 /* FAT32: Offset of backup boot sector (WORD) */
// #define BPB_Reserved 52  /* FAT32: Reserved. Must be set to 0x0.*/

// #define BS_DrvNum 64     /* FAT32: Physical drive number for int13h (BYTE) */
// #define BS_NTres 65      /* FAT32: Error flag (BYTE) */
// #define BS_BootSig 66    /* FAT32: Extended boot signature (BYTE) */
// #define BS_VolID 67      /* FAT32: Volume serial number (DWORD) */
// #define BS_VolLab 71     /* FAT32: Volume label string (8-byte) */
// #define BS_FilSysType 82 /* FAT32: Filesystem type string (8-byte) */
// #define BS_BootCode 90   /* FAT32: Boot code (420-byte) */
// #define BS_BootSign 510  /* Signature word (WORD) */

// /*FSInfo Structure (FAT32 only)*/
// #define FSI_LeadSig 0      /* FAT32 FSI: Leading signature (DWORD) */
// #define FSI_Reserved1 4    /* FAT32 FSI: Reserved1. This field should be always initialized to zero.*/
// #define FSI_StrucSig 484   /* FAT32 FSI: Structure signature (DWORD) */
// #define FSI_Free_Count 488 /* FAT32 FSI: Number of free clusters (DWORD) */
// #define FSI_Nxt_Free 492   /* FAT32 FSI: Last allocated cluster (DWORD) */
// #define FSI_Reserved2 496  /* FAT32 FSI: Reserved. This field should be always initialized to zero.*/
// #define FSI_TrailSig 512   /* FAT32 FSI: */
// /*reserved, may be used in the future*/
// #define ClnShutBitMask 0x08000000
// // If bit is 1, volume is "clean".If bit is 0, volume is "dirty".
// #define HrdErrBitMask 0x04000000
// // If this bit is 1, no disk read/write errors were encountered.
// // If this bit is 0, the file system driver encountered a disk I/O error on theVolume the last time it was mounted,
// // which is an indicator that some sectorsmay have gone bad on the volume.
// #define NAME0_NOT_VALID(x) (x == 0x20)
// #define NAME_CHAR_NOT_VALID(x) ((x < 0x20 && x != 0x05) || (x == 0x22) || (x >= 0x2A && x <= 0x2C) || (x >= 0x2E && x <= 0x2F) || (x >= 0x3A || x <= 0x3F) || (x >= 0x5B && x <= 0x5D) || (x == 0x7C))
// // " * + , . / : ; < =	> ? [ \ ] |
// #define FCB_NUM(x) (sizeof(x) / sizeof(dirent_s_t))
extern struct _superblock fat32_sb;

// the sector of bpb and fsinfo
#define SECTOR_BPB 0
#define SECTOR_FSINFO 1
#define SECTOR_BPB_BACKUP 6
#define SECTOR_FSINFO_BACKUP 7

// some useful field of fat32 super block
#define __BPB_RootEntCnt 0
#define __BPB_BytsPerSec (fat32_sb.sector_size)
#define __TotSec (fat32_sb.n_sectors)
#define __BPB_ResvdSecCnt (fat32_sb.fat32_sb_info.fatbase)
#define __BPB_NumFATs (fat32_sb.fat32_sb_info.n_fats)
#define __FATSz (fat32_sb.fat32_sb_info.n_sectors_fat)
#define __BPB_SecPerClus (fat32_sb.sectors_per_block)
#define __Free_Count (fat32_sb.fat32_sb_info.free_count)
#define __Nxt_Free (fat32_sb.fat32_sb_info.nxt_free)
#define __CLUSTER_SIZE (fat32_sb.cluster_size)
#define FAT_BASE __BPB_ResvdSecCnt

#define FAT_SFN_LENGTH 11 // 短文件名长度
#define FAT_LFN_LENGTH 13 // 长文件名长度

/* some useful macro from manual */
// 1. number of root directory sectors (not important in fat32 file system)
// (0*32+512-1)/512 FAT32 没有Root Directory 的概念，所以一直是0
#define RootDirSectors (((__BPB_RootEntCnt * 32) + (__BPB_BytsPerSec - 1)) / __BPB_BytsPerSec)

// 2. the first data sector num(sector number index from 0)
// 假设一个FAT32区域占2017个扇区: 32 + (2 * 2017) + 0
#define FirstDataSector ((__BPB_ResvdSecCnt) + ((__BPB_NumFATs) * (__FATSz)) + (RootDirSectors))

// 3. the first sector number of cluster N
// 假设一个簇两个扇区,保留区有32个扇区，一个fat区域占2017个扇区，有两个fat区域：(N-2)*2+32+(2*2017)+0
#define FirstSectorofCluster(N) (((N)-2) * (__BPB_SecPerClus) + (FirstDataSector))

// 4. the number of sectors in data region
// 就是用总的数量减去保留扇区，然后减去FAT的扇区
// remeber, RootDirSectors is always 0
#define DataSec ((__TotSec) - ((__BPB_ResvdSecCnt) + ((__BPB_NumFATs) * (__FATSz)) + (RootDirSectors)))

// 5. the count of clusters in data region
// remeber, cluster 0 and cluster 1 can not used!!!
#define CountofClusters ((DataSec) / (__BPB_SecPerClus))

// 6. the maxium of FAT entry
// 最后一个簇
#define FAT_CLUSTER_MAX ((CountofClusters) + 1)

// 7. 每个BLOCK每个FCB的数量
#define FCB_PER_BLOCK ((__BPB_BytsPerSec) / sizeof(dirent_s_t))

// 8. bytes of (FAT32 entry)
#define FATOffset(N) ((N)*4)

// 9. the sector number of fat entry
// 保留区域的扇区个数+一个扇区对应4字节的FAT表项的偏移/一个扇区多少个字节，就可以算出这个FAT表项在那个扇区
#define ThisFATEntSecNum(N) ((__BPB_ResvdSecCnt) + ((FATOffset(N)) / (__BPB_BytsPerSec)))

// 10. the offset of an entry within a sector
// 算出FAT表项在对应扇区的具体多少偏移的位置
#define ThisFATEntOffset(N) ((FATOffset(N)) % (__BPB_BytsPerSec))

// remember ,the valid number of bits of the FAT32 entry is 28
#define FAT_VALID_MASK 0x0FFFFFFF

// 11. read the FAT32 cluster entry value
// 读FAT32 的entry
#define FAT32ClusEntryVal(SectorBuf, N) ((*((DWORD *)&((char *)SectorBuf)[(ThisFATEntOffset(N))])) & FAT_VALID_MASK)

// 12. set the FAT32 cluster entry value
// 写FAT32 的entry
#define SetFAT32ClusEntryVal(SecBuff, N, Val)                                                                    \
    do {                                                                                                         \
        int fat_entry_val = Val & FAT_VALID_MASK;                                                                \
        *((DWORD *)&SecBuff[ThisFATEntOffset(N)]) = (*((DWORD *)&SecBuff[ThisFATEntOffset(N)])) & 0xF0000000;    \
        *((DWORD *)&SecBuff[ThisFATEntOffset(N)]) = (*((DWORD *)&SecBuff[ThisFATEntOffset(N)])) | fat_entry_val; \
    } while (0);

// FAT[0] and FAT[1] is reserved !!!
// FAT[0]: 0FFF FFF8
// FAT[1]: 0FFF FFFF

/*some useful macro for fat chain travel*/
// 1. reach the end of fat32 chain?
#define ISEOF(FATContent) ((FATContent) >= 0x0FFFFFF8)

// 2. EOC and FREE
#define EOC 0x0FFFFFFF
#define FREE_MASK 0x00000000

// 3. NAME0 flag
#define NAME0_FREE_ONLY(x) (x == 0xE5)
#define NAME0_FREE_ALL(x) (x == 0x00)
#define NAME0_FREE_BOOL(x) (NAME0_FREE_ONLY(x) || NAME0_FREE_ALL(x))
#define NAME0_SPECIAL(x) (x == 0x05) // 0xE5伪装->0x05

/* File attribute bits for directory entry*/
// 1.  some attrs definition
#define ATTR_READ_ONLY 0x01 // Read only 0000_0001
#define ATTR_HIDDEN 0x02    // Hidden 0000_0010
#define ATTR_SYSTEM 0x04    // System 0000_0100
#define ATTR_VOLUME_ID 0x08 // VOLUME_ID 0000_1000
#define ATTR_ARCHIVE 0x20   // Archive 0010_0000
#define ATTR_DIRECTORY 0x10 // Directory 0001_0000
// 2. long directory attr
#define ATTR_LONG_NAME (ATTR_READ_ONLY | ATTR_HIDDEN | ATTR_SYSTEM | ATTR_VOLUME_ID)
#define ATTR_LONG_NAME_MASK (ATTR_READ_ONLY | ATTR_HIDDEN | ATTR_SYSTEM | ATTR_VOLUME_ID | ATTR_DIRECTORY | ATTR_ARCHIVE)

// 3. is dir? and is long dir entry?
#define DIR_BOOL(x) ((x)&ATTR_DIRECTORY)

// 4. set attr to dir and set attr to none dir
#define DIR_SET(x) ((x) = ((x)&FREE_MASK) | ATTR_DIRECTORY)
// we suppose it is archive if a file is not a directory when initialized
#define NONE_DIR_SET(x) ((x) = ((x)&FREE_MASK) | ATTR_ARCHIVE)

// 5. read only ?
#define READONLY_GET(x) ((x)&ATTR_READ_ONLY)

// first cluster number
#define DIR_FIRST_CLUS(high, low) ((high << 16) | (low))
#define DIR_FIRST_HIGH(x) ((x) >> 16)
#define DIR_FIRST_LOW(x) ((x)&0x0000FFFF)

// The maximum length of the name and the maximum length of the path
// 1. short
#define NAME_SHORT_MAX 8
#define PATH_SHORT_MAX 80
// 2. long
// $ % ' - _ @ ~ ` ! ( ) { } ^ # & is allowed for fat32 file system allowed long directory
#define NAME_LONG_MAX 255
#define PATH_LONG_MAX 260

// some useful macro related to long dirent
#define FIRST_LONG_ENTRY 0x01
#define LAST_LONG_ENTRY 0x40
#define LONG_DIRENT_CNT 20

#define LONG_NAME_BOOL(x) (((x) ^ ATTR_LONG_NAME) == 0)
#define LAST_LONG_ENTRY_BOOL(x) ((x)&LAST_LONG_ENTRY)
#define LAST_LONG_ENTRY_SET(x) ((x) = ((x) | LAST_LONG_ENTRY))

// long name char macro
#define LONG_NAME_CHAR_MASK(x) ((x)&0xFF)
#define LONG_NAME_CHAR_VALID(x) (((x) != 0x0000) && ((x) != 0xFFFF))
#define LONG_NAME_CHAR_SET(x) (((ushort)(x)) & 0x00FF)

// NOTE: inum index from 0 (root is 0)
// allocate an ino for each fat32 short entry
#define FCB_PER_BLOCK ((__BPB_BytsPerSec) / sizeof(dirent_s_t))
#define SECTOR_TO_FATINUM(s, offset) (((s)-FirstDataSector) * (FCB_PER_BLOCK) + (offset) + 1)
#define INUM_TRANSFER(ino) ((ino == 0) ? 0 : ino - 1)
#define FATINUM_TO_SECTOR(ino) (INUM_TRANSFER(ino) / (FCB_PER_BLOCK) + FirstDataSector)
#define FATINUM_TO_OFFSET(ino) (INUM_TRANSFER(ino) % (FCB_PER_BLOCK))
#define FATDEV_TO_DEVMAJOR(DIR_Dev) (((DIR_Dev)&0xC) >> 2)
#define FATDEV_TO_DEVMINOR(DIR_Dev) ((DIR_Dev)&0x3)
#define FATDEV_TO_IRDEV(DIR_Dev) ((FATDEV_TO_DEVMAJOR(DIR_Dev) << 8) | FATDEV_TO_DEVMINOR(DIR_Dev))
#define FATDEV_TO_ITYPE(DIR_Dev) (((DIR_Dev)&0xF0) << 8) // DIR_Dev: 4 + 2 + 2
#define ITYPE_TO_FATDEV(type) (((type)&S_IFMT) >> 8)
// #define ITYPE_TO_FATDEV(i_mode) ( ()  )

// the logistic number of cluster for position : off
// start from 0
#define LOGISTIC_C_NUM(off) ((off) / (__BPB_SecPerClus * __BPB_BytsPerSec))
#define LOGISTIC_C_OFFSET(off) ((off) % (__BPB_SecPerClus * __BPB_BytsPerSec))
// the logistic number of sector for position : off
// start form 0
#define LOGISTIC_S_NUM(off) (LOGISTIC_C_OFFSET(off) / __BPB_BytsPerSec)
#define LOGISTIC_S_OFFSET(off) (LOGISTIC_C_OFFSET(off) % __BPB_BytsPerSec)

// misc
// 1. the length of a dir file
#define DIRLENGTH(ip) ((ip->fat32_i.cluster_cnt) * __CLUSTER_SIZE)
// 2. the fat32 entry number of a sector
#define FAT_PER_SECTOR ((__BPB_BytsPerSec) / 4)
// 3. the maxium of FCB (short and long entry)
#define FCB_MAX_LENGTH 672 // (20+1)*32
// 4. first long directory in the data region ?
#define first_long_dir(ip) (ip == fat32_sb.fat32_sb_info.root_entry && off == 0)

// for debug: the start addr of the cluster in the fat32.img
#define FSIMG_STARTADDR (FirstDataSector * __BPB_BytsPerSec)

// compare the s and t
#define fat32_namecmp(s, t) (strncmp(s, t, PATH_LONG_MAX))

// whether adjacent clusters are physically adjacent?
#define CLUSTER_ADJACENT(vec_cur, first_sector) ((vec_cur->blockno_start) + (vec_cur->block_len) == (first_sector))

#define UNIQUE_INO(off, cluster_start) (((uint64)(off) << 32) | (cluster_start))

#define ROOT_INO 2

// for bit map, starts from 0
#define BIT_INDEX(pos, unit) (pos / unit)
#define BIT_OFFSET(pos, unit) (pos % unit)
#define SET_BIT(bitmap, pos) (bitmap |= (1 << pos))
#define CLEAR_BIT(bitmap, pos) (bitmap &= ~(1 << pos))
#define TEST_BIT(bitmap, pos) (bitmap & (1 << pos))


// FAT32 Boot Record
typedef struct FAT32_BootRecord {
    /*FAT common field*/

    uchar Jmpboot[3]; // Jump instruction to boot code.
    // 0xEB 0x?? 0x??
    uchar OEMName[8]; // OEM Name Identifier.
    // Can be set by a FAT implementation to any desired value.
    uint16 BytsPerSec; // Count of bytes per sector(*)
                       // 512 1024 2048 4096
    uint8 SecPerClus;  // Number of sectors per allocation unit.
    // 1 2 4 8 16 32 64 128
    uint16 RsvdSecCnt; // Number of reserved sectors in the reserved region
    // of the volume starting at the first sector of the volume.
    uint8 NumFATs; // The count of file allocation tables (FATs) on the volume.
    // 1 or 2
    uchar RootEntCnt[2]; // for FAT32 , set to 0
    uint16 TotSec16;     // the count of all sectors in all four regions of the volume
    uchar Media;         // For removable media, 0xF0 is frequently used.
    uchar FATSz16[2];    // On FAT32 volumes this field must be 0, and BPB_FATSz32 contains the FAT size count.
    uint16 SecPerTrk;    // Sectors per track for interrupt 0x13.
    uint16 NumHeads;     // Number of heads for interrupt 0x13.
    uint32 HiddSec;      // Count of hidden sectors preceding the partition that contains this FAT volume.
    uint32 TotSec32;     // the count of all sectors in all four regions of the volume.

    /*special for FAT32*/
    uint32 FATSz32;    // 32-bit count of sectors occupied by ONE FAT
    uchar ExtFlags[2]; //  0-3 : Zero-based number of active FAT
    // 4-6 : Reserved
    // 7 : 0 means the FAT is mirrored at runtime into all FATs
    //     1 means only one FAT is active; it is the one referenced in bits 0-3
    // 8-15 : Reserved
    uchar FSVer[2];
    // High byte is major revision number
    // Low byte is minor revision number
    uint32 RootClus;
    // cluster number of the first cluster of the root directory,
    // usually 2 but not required to be 2
    uint16 FSInfo;
    // Sector number of FSINFO structure
    // in the reserved area of the FAT32 volume. Usually 1.
    uint16 BkBootSec;
    // If non-zero, indicates the sector number
    // in the reserved area of the volume of a copy of the boot record.
    // Usually 6. No value other than 6 is recommended.
    uchar Reserved1[12];
    // Reserved for future expansion
    uint8 DrvNum;
    // driver number
    uchar Reserved2;
    uchar BootSig;
    // Signature (must be 0x28 or 0x29).
    uchar VolID[4]; // Volume ID 'Serial' number
    uchar VolLab[11];
    // Volume label string.
    uchar FilSysType[8];
    // System identifier string  "FAT32 "
    uchar BootCode[420];
    // Boot code.
    uchar BootSign[2];
    // Bootable partition signature 0xAA55.
} __attribute__((packed)) fat_bpb_t;

// FAT32 Fsinfo
typedef struct FAT32_Fsinfo {
    uchar LeadSig[4]; // validate that this is in fact an FSInfo sector
    // Value 0x41615252
    uchar Reserved1[480]; // is currently reserved for future expansion
    uchar StrucSig[4];    // is more localized in the sector to the location of the fields that are used
    // Value 0x61417272
    uint32 Free_Count; // the last known free cluster count on the volume.
    // If the value is 0xFFFFFFFF, then the free count is unknown and must be computed.
    uint32 Nxt_Free;
    // the cluster number at which the driver should start looking for free clusters
    uchar Reserved2[12];
    uchar TrailSig[4]; // validate that this is in fact an FSInfo sector
    // Value 0xAA550000
} __attribute__((packed)) fsinfo_t;

// Date
// typedef struct __date_t {
//     uchar day : 5;   // 0~31
//     uchar month : 4; // Jan ~ Dec
//     uchar year : 7;  // form 1980 (1980~2107)
// } __attribute__((packed)) uint16;

// // Time
// typedef struct __time_t {
//     uchar second_per_2 : 5; // 2-second increments 0~59
//     uchar minute : 6;       // number of minutes 0~59
//     uchar hour : 5;         // hours 0~23
// } __attribute__((packed)) uint16;

// typedef struct __date_t {
//     uchar day;   // 0~31
//     uchar month; // Jan ~ Dec
//     uchar year;  // form 1980 (1980~2107)
// } __attribute__((packed)) uint16;

// // Time
// typedef struct __time_t {
//     uchar second_per_2; // 2-second increments 0~59
//     uchar minute;       // number of minutes 0~59
//     uchar hour;         // hours 0~23
// } __attribute__((packed)) uint16;

// Directory Structure (short name)
typedef struct Short_Dir_t {
    uchar DIR_Name[FAT_SFN_LENGTH]; // directory name
    uchar DIR_Attr;                 // directory attribute

    // ====>
    // origin
    // uchar DIR_Dev;                  // reserved, but we use it as DEVICE
    // // DIR_NTRes                        // 3(i_mode) + 3(major) + 2(minor)
    // new
    uchar DIR_Dev; // reserved, but we use it as DEVICE
    // DIR_NTRes                        // 4(type) + 2(major) + 2(minor)
    // <====

    uchar DIR_CrtTimeTenth; // create time
    // Count of tenths of a second
    // 0 <= DIR_CrtTimeTenth <= 199
    // uint16 DIR_CrtTime;    // create time, 2 bytes
    uint16 DIR_CrtTime;    // create time, 2 bytes
    uint16 DIR_CrtDate;    // create date, 2 bytes
    uint16 DIR_LstAccDate; // last access date, 2 bytes
    uint16 DIR_FstClusHI;  // High word of first data cluster number
    uint16 DIR_WrtTime;    // Last modification (write) time.
    uint16 DIR_WrtDate;    // Last modification (write) date.
    uint16 DIR_FstClusLO;  // Low word of first data cluster number
    uint32 DIR_FileSize;   // 32-bit quantity containing size
} __attribute__((packed)) dirent_s_t;

// Directory Structure (long name)
typedef struct Long_Dir_t {
    uchar LDIR_Ord;       // The order of this entry in the sequence
    uint16 LDIR_Name1[5]; // characters 1 through 5
    uchar LDIR_Attr;      // Attributes
    uchar LDIR_Type;      // Must be set to 0.
    uchar LDIR_Chksum;    // Checksum of name
    uint16 LDIR_Name2[6]; // characters 6 through 11
    uint16 LDIR_Nlinks;   // (Must be set to 0, but we use it as nlinks)
    // LDIR_FstClusLO
    uint16 LDIR_Name3[2]; // characters 12 and 13
} __attribute__((packed)) dirent_l_t;

typedef uint32 FAT_entry_t;

// 1. mount fat32 file system
int fat32_fs_mount(int, struct _superblock *);

// 2. bpb parser
int fat32_boot_sector_parser(struct _superblock *, fat_bpb_t *);

// 3. fsinfo parser
int fat32_fsinfo_parser(struct _superblock *, fsinfo_t *);

// 4. fat table -> bit map
void fat32_fat_bitmap_init(int dev, struct _superblock *sb);

// 5. alloc a valid cluster given bit map
FAT_entry_t fat32_bitmap_alloc(struct _superblock *sb, FAT_entry_t hint);

// 6. set/clear bit map given cluster number
void fat32_bitmap_op(struct _superblock *sb, FAT_entry_t cluster, int set);

// 7. set fat table entry given cluster number using fat table in memory
void fat32_fat_cache_set(FAT_entry_t cluster, FAT_entry_t value);

// 8. get fat table entry given cluster number using fat table in memory
FAT_entry_t fat32_fat_cache_get(FAT_entry_t cluster);

// 9. writeback FATtable
void fat32_fat_bitmap_writeback(int dev, struct _superblock *sb);
#endif