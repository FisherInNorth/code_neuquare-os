#include "atomic/semaphore.h"
#include "memory/allocator.h"
#include "fs/fat/fat32_stack.h"
#include "fs/fat/fat32_disk.h"
#include "fs/fat/fat32_mem.h"
#include "fs/vfs/fs.h"
#include "fs/bio.h"
#include "test.h"
#include "debug.h"
#include "test.h"
#include "param.h"
#include "common.h"

extern struct _superblock fat32_sb;

// initialize superblock obj and root inode obj.
int fat32_fs_mount(int dev, struct _superblock *sb) {
    /* superblock initialization */
    sb->s_op = TODO();
    sb->s_dev = dev;
    sema_init(&sb->sem, 1, "fat32_sb_sem");
    initlock(&sb->lock, "fat32_sb_lock"); // for bit map (to replace FAT table)

    /* read boot sector in sector 0 */
    struct buffer_head *bp;
    bp = bread(dev, SECTOR_BPB);
    fat32_boot_sector_parser(sb, (fat_bpb_t *)bp->data);
    brelse(bp);

    /* read fsinfo sector in sector 1 */
    bp = bread(dev, SECTOR_FSINFO);
    fat32_fsinfo_parser(sb, (fsinfo_t *)bp->data);
    brelse(bp);

    Info("======= BIT MAP and FAT TABLE ======\n");
    // FAT table -> bit map
    int n = DIV_ROUND_UP((FAT_CLUSTER_MAX >> 3), PGSIZE); // ÷ 8
    sb->bit_map = fat32_page_alloc(n);
    Info("bit map : %d pages\n", n);
    n = DIV_ROUND_UP((FAT_CLUSTER_MAX << 2), PGSIZE); // x 4
    sb->fat_table = fat32_page_alloc(n);
    Info("fat table : %d pages\n", n);

    fat32_fat_bitmap_init(ROOTDEV, sb);

    /* 调用fat32_root_entry_init，获取到root根目录的fat_entry*/
    sb->root = fat32_root_inode_init(sb);
    sb->s_mount = sb->root;

    // dirty list and lock protecting it
    INIT_LIST_HEAD(&sb->s_dirty);
    initlock(&sb->dirty_lock, "dirty_lock");

    return 0;
}

int fat32_fsinfo_parser(struct _superblock *sb, fsinfo_t *fsinfo) {
    /* superblock fsinfo fileds initialization */
    sb->fat32_sb_info.free_count = fsinfo->Free_Count;
    sb->fat32_sb_info.nxt_free = fsinfo->Nxt_Free;

    // help fsinfo
    sb->fat32_sb_info.hint_valid = 0;

    ////////////////////////////////////////////////////////////////////////////////
    Info("============= FSINFO ==========\n");
    // Info("LeadSig : ");
    // Show_bytes((byte_pointer)&fsinfo->LeadSig, sizeof(fsinfo->LeadSig));

    // Info("StrucSig : ");
    // Show_bytes((byte_pointer)&fsinfo->StrucSig, sizeof(fsinfo->StrucSig));

    Info("Free_Count : %d\n", fsinfo->Free_Count);

    Info("Nxt_Free : %d\n", fsinfo->Nxt_Free);

    // Info("TrailSig : ");

    // Show_bytes((byte_pointer)&fsinfo->TrailSig, sizeof(fsinfo->TrailSig));
    return 0;
}

int fat32_boot_sector_parser(struct _superblock *sb, fat_bpb_t *fat_bpb) {
    Info("============= BOOT Sector ==========\n");
    /* superblock initialization */
    // common
    sb->sector_size = fat_bpb->BytsPerSec;
    sb->n_sectors = fat_bpb->TotSec32;
    sb->sectors_per_block = fat_bpb->SecPerClus;

    // specail for fat32 superblock
    sb->fat32_sb_info.fatbase = fat_bpb->RsvdSecCnt;
    sb->fat32_sb_info.n_fats = fat_bpb->NumFATs;
    sb->fat32_sb_info.n_sectors_fat = fat_bpb->FATSz32;
    sb->fat32_sb_info.root_cluster_s = fat_bpb->RootClus;

    // repeat with sb->s_blocksize
    sb->cluster_size = (sb->sector_size) * (sb->sectors_per_block);
    sb->s_blocksize = sb->cluster_size;

    //////////////////////////////////////////////////////////////////
    /*然后是打印fat32的所有信息*/
    // Info_R("Jmpboot : ");
    // Show_bytes((byte_pointer)&fat_bpb->Jmpboot, sizeof(fat_bpb->Jmpboot));
    Info("OEMNAME : %s\n", fat_bpb->OEMName);

    Info("BytsPerSec : %d\n", fat_bpb->BytsPerSec);
    Info("SecPerClus : %d\n", fat_bpb->SecPerClus);

    Info("RsvdSecCnt : %d\n", fat_bpb->RsvdSecCnt);

    Info("NumFATs : %d\n", fat_bpb->NumFATs);

    // Info("RootEntCnt : ");
    // Show_bytes((byte_pointer)&fat_bpb->RootEntCnt, sizeof(fat_bpb->RootEntCnt));

    // Info("TotSec16 : %d\n", fat_bpb->TotSec16);
    // Info("Media : ");
    // Show_bytes((byte_pointer)&fat_bpb->Media, sizeof(fat_bpb->Media));

    // Info("FATSz16 : ");
    // Show_bytes((byte_pointer)&fat_bpb->FATSz16, sizeof(fat_bpb->FATSz16));

    Info("SecPerTrk : %d\n", fat_bpb->SecPerTrk);
    Info("NumHeads : %d\n", fat_bpb->NumHeads);
    // Info("HiddSec : %d\n", fat_bpb->HiddSec);

    Info("TotSec32 : %d\n", fat_bpb->TotSec32);

    Info("FATSz32 : %d\n", fat_bpb->FATSz32);

    // Info("ExtFlags : ");
    // printf_bin(fat_bpb->ExtFlags, sizeof(fat_bpb->ExtFlags));

    // Info("FSVer : ");
    // Show_bytes((byte_pointer)&fat_bpb->FSVer, sizeof(fat_bpb->FSVer));

    Info("RootClus : %d\n", fat_bpb->RootClus);

    Info("FSInfo : %d\n", fat_bpb->FSInfo);

    Info("BkBootSec : %d\n", fat_bpb->BkBootSec);

    // Info("DrvNum : %d\n", fat_bpb->DrvNum);
    // Info("BootSig : ");
    // Show_bytes((byte_pointer)&fat_bpb->BootSig, sizeof(fat_bpb->BootSig));

    // Info("VolID : ");
    // Show_bytes((byte_pointer)&fat_bpb->VolID, sizeof(fat_bpb->VolID));

    // Info("VolLab : ");
    // Show_bytes((byte_pointer)&fat_bpb->VolLab, sizeof(fat_bpb->VolLab));

    Info("FilSysType : ");
    printf("%.5s\n", fat_bpb->FilSysType);

    Info("FAT_CLUSTER_MAX : %d\n", FAT_CLUSTER_MAX);

    // Info("BootCode : ");
    // Show_bytes((byte_pointer)&fat_bpb->BootCode, sizeof(fat_bpb->BootCode));

    // Info("BootSign : ");
    // Show_bytes((byte_pointer)&fat_bpb->BootSign, sizeof(fat_bpb->BootSign));

    // panic("boot sector parser");
    return 0;
}

// fat table -> bitmap
// init the fat table in memory
void fat32_fat_bitmap_init(int dev, struct _superblock *sb) {
    struct buffer_head *bp;
    int c = 0;
    // cluster 0 and cluster 1 is reserved, cluster 2 belongs to root
    int sec = FAT_BASE;
    uint64 map_mini = 0;
    int map_mini_size = sizeof(map_mini) << 3; // * 8
    uint64 *map = (uint64 *)sb->bit_map;
    FAT_entry_t *fat_table = (FAT_entry_t *)sb->fat_table;
    while (c < FAT_CLUSTER_MAX) {
        bp = bread(fat32_sb.s_dev, sec);
        FAT_entry_t *fats = (FAT_entry_t *)(bp->data);
        for (int s = 0; s < FAT_PER_SECTOR; s++) {
            int idx = BIT_INDEX(c, map_mini_size);
            int off = BIT_OFFSET(c, map_mini_size);
            if (fats[s] != FREE_MASK) {
                SET_BIT(map_mini, off);   // set to
                fat_table[c] = fats[s];   // copy fat table to memory
            } else {
                CLEAR_BIT(map_mini, off); // set to 0
            }
            c++;
            int save_flag = (off + 1 == map_mini_size);
            if (save_flag) {
                map[idx] = map_mini;
            }
            if (c > FAT_CLUSTER_MAX) {
                brelse(bp);
                if (!save_flag) {
                    // remember to save the last map_mini
                    map[idx] = map_mini;
                }
                return;
            }
        }
        sec++;
        brelse(bp);
    }
    panic("fat32_fat_bitmap_init : can't reach here\n");
}

void fat32_fat_bitmap_writeback(int dev, struct _superblock *sb) {
    struct buffer_head *bp;
    int c = 0;
    // cluster 0 and cluster 1 is reserved, cluster 2 belongs to root
    int sec = FAT_BASE;
    // uint64 map_mini = 0;
    // int map_mini_size = sizeof(map_mini) << 3; // * 8
    // uint64 *map = (uint64 *)sb->bit_map;
    FAT_entry_t *fat_table = (FAT_entry_t *)sb->fat_table;
    while (c < FAT_CLUSTER_MAX) {
        bp = bread(fat32_sb.s_dev, sec);
        FAT_entry_t *fats = (FAT_entry_t *)(bp->data);
        for (int s = 0; s < FAT_PER_SECTOR; s++) {
            // int idx = BIT_INDEX(c, map_mini_size);
            // int off = BIT_OFFSET(c, map_mini_size);
            fats[s] = fat_table[c];
            // if (fats[s] != FREE_MASK) {
            //     SET_BIT(map_mini, off);   // set to
            //     fat_table[c] = fats[s];   // copy fat table to memory
            // } else {
            //     CLEAR_BIT(map_mini, off); // set to 0
            // }
            c++;
            // int save_flag = (off + 1 == map_mini_size);
            // if (save_flag) {
            //     map[idx] = map_mini;
            // }
            if (c > FAT_CLUSTER_MAX) {
                bwrite(bp);
                brelse(bp);
                // if (!save_flag) {
                //     // remember to save the last map_mini
                //     map[idx] = map_mini;
                // }
                return;
            }
        }
        sec++;
        bwrite(bp);
        brelse(bp);
    }
    panic("fat32_fat_bitmap_writeback : can't reach here\n");
}

// called not holding lock
void fat32_bitmap_op(struct _superblock *sb, FAT_entry_t cluster, int set) {
    acquire(&sb->lock);
    uint64 map_mini = 0;
    int map_mini_size = sizeof(map_mini) << 3; // * 8
    uint64 *map = (uint64 *)sb->bit_map;
    int idx = BIT_INDEX(cluster, map_mini_size);
    int off = BIT_OFFSET(cluster, map_mini_size);
    map_mini = map[idx];
    ASSERT(TEST_BIT(map_mini, off));
    if (set)
        SET_BIT(map_mini, off);
    else
        CLEAR_BIT(map_mini, off);
    map[idx] = map_mini; // don't forget it
    release(&sb->lock);
}

FAT_entry_t fat32_bitmap_alloc(struct _superblock *sb, FAT_entry_t hint) {
    // using hint speed up
    uint64 map_mini = 0;
    int map_mini_size = sizeof(map_mini) << 3; // * 8
    uint64 *map = (uint64 *)sb->bit_map;
    FAT_entry_t c = hint;
    // cluster 0 and cluster 1 is reserved, cluster 2 is for root
    int idx = BIT_INDEX(hint, map_mini_size);
    int off_init = BIT_OFFSET(hint, map_mini_size);
    // idx is similar to cluster
    // off is similar to FAT entry in cluster
    while (c < FAT_CLUSTER_MAX) {
        map_mini = map[idx];
        for (int off = off_init; off < map_mini_size; off++) {
            if (!TEST_BIT(map_mini, off)) {
                SET_BIT(map_mini, off);
                map[idx] = map_mini; // don't forget it
                return c;
            }
            c++;
            if (c > FAT_CLUSTER_MAX) {
                return 0;
            }
        }
        off_init = 0;
        idx++;
    }
    return 0;
}

// called holding lock
void fat32_fat_cache_set(FAT_entry_t cluster, FAT_entry_t value) {
    if (!(cluster >= 2 && cluster <= FAT_CLUSTER_MAX)) {
        printfRed("cluster : %d(%x)\n", cluster, cluster);
        panic("fat32_fat_cache_set, cluster error\n");
    }
    if (!(value >= 2 && value <= FAT_CLUSTER_MAX) && value != EOC && value != FREE_MASK) {
        printfRed("value : %d(%x)\n", value, value);
        panic("fat32_fat_cache_set, value error\n");
    }
    FAT_entry_t *fats = (FAT_entry_t *)fat32_sb.fat_table;
    // FAT_entry_t old_value = fats[cluster];
    fats[cluster] = value;
    // printfMAGENTA("cluster : %x, %x -> %x\n", cluster, old_value, fats[cluster]);
}

// called holding lock
FAT_entry_t fat32_fat_cache_get(FAT_entry_t cluster) {
    if (!(cluster >= 2 && cluster <= FAT_CLUSTER_MAX)) {
        printfRed("cluster_cur : %d(%x)\n", cluster, cluster);
        panic("fat32_fat_cache_get, cluster_cur error\n");
    }
    FAT_entry_t *fats = (FAT_entry_t *)fat32_sb.fat_table;
    FAT_entry_t fat_next = fats[cluster];

    if (fat_next == 0) {
        printfRed("cluster_cur : %d(%x)\n", cluster, cluster);
        panic("fat32_fat_cache_get, fat_next error\n");
    }
    return fat_next;
}