#include "test.h"
#include "fs/fat/fat32_disk.h"
#include "fs/fat/fat32_mem.h"
#include "fs/vfs/fs.h"
#include "debug.h"
extern struct _superblock fat32_sb;

void fat32_test_functions(void) {
    // fat_entry_t* fat_root = fat32_name_fat_entry(global_fatfs.root_entry.fname);
    // struct inode *inode_dir = namei("/hello");

    // struct inode *inode_new = fat32_inode_create("/hello/raw/applepeach",  ATTR_DIRECTORY);
    // struct inode *inode_new = fat32_inode_create("/apple/raw",  ATTR_DIRECTORY);
    // struct inode *inode_new = fat32_inode_create("/hello/b.txt",  S_IFREG);
    // struct inode *inode_new = fat32_inode_create("/hello/userkernelapples.txt",  S_IFREG);
    // struct inode *inode_new = fat32_inode_create("/test_dir/userkernelap.txt", S_IFREG);

    // struct inode *inode_new = fat32_inode_create("/test_dir/apple.txt", S_IFREG);
    // uint sector_num = FATINUM_TO_SECTOR(inode_new->i_ino);
    // uint sec_pos = DEBUG_SECTOR(inode_new, sector_num);//debug
    // char tmp[30];
    // fat32_time_parser(inode_new, &inode_new->fat32_i.DIR_CrtTime, tmp, 1);
    // printf("%s\n", tmp);

    // fat32_date_parser(&inode_new->fat32_i.DIR_CrtDate, tmp);
    // printf("%s\n", tmp);

    // fat32_time_parser(inode_new, &inode_new->fat32_i.DIR_WrtTime, tmp, 1);
    // printf("%s\n", tmp);

    // fat32_date_parser(&inode_new->fat32_i.DIR_LstAccDate, tmp);
    // printf("%s\n", tmp);

    // fat32_date_parser(&inode_new->fat32_i.DIR_WrtDate, tmp);
    // printf("%s\n", tmp);

    // fat32_fcb_delete(inode_new->parent, inode_new);
    // int ms;
    // uint16 time_now = fat32_inode_get_time(&ms);

    // char tmp[30];
    // fat32_time_parser(&time_now, tmp, &ms);
    // printf("%s\n", tmp);

    return;
}
