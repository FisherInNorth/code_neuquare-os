#include "platform/hifive/sdcard_hifive.h"
#include "platform/hifive/spi_hifive.h"
#include "driver/disk.h"
#include "driver/crc.h"
#include "atomic/semaphore.h"
#include "fs/bio.h"
#include "debug.h"
#include "memory/allocator.h"
#include "memory/memlayout.h"

/* 
    ref =>
    >> https://sifive.cdn.prismic.io/sifive/1a82e600-1f93-4f41-b2d8-86ed8b16acba_fu740-c000-manual-v1p6.pdf
    >> https://suda-morris.github.io/2015/10/30/sd-card/
    >> https://picture.iczhiku.com/resource/eetop/wHKwutAIPkWkWMbn.pdf  【SD 2.0 协议中文】
    >> https://www.docin.com/p-645567037.html?docfrom=rrela             【SD卡规范V4.1】
    >> http://www.ip33.com/crc.html                                     【循环冗余校验码计算工具】
    >> https://blog.csdn.net/qq_44868609/article/details/105057068
    >> https://www.docin.com/p-645567037.html?docfrom=rrela             【SD 2.0 协议英文】
    ...
*/

uint8 polltest(size_t *ptimer, uint8 tar, uint8 mask, char *msg) {
    volatile uint8 data;
    size_t timer = ptimer ? *ptimer : 1000;
    // int debug = 0;
// if (strstr(msg,"cmd_read_mutiple_block: wait too long!")) debug = 1;
// if (strstr(msg,"R3 wait too long")) debug = 1;
// if (strstr(msg,"read_mutiple_block fail!")) debug = 1;
// if (strstr(msg,"sd_single_write:write failed!")) debug = 1;
// if (strstr(msg,"sd_multiple_write:write failed!")) debug = 1;
// if (strstr(msg,"sd_single_write:busytest failed")) debug = 1;
// if (strstr(msg,"sd_multiple_write:write all failed")) debug = 1;
    
    while (timer) {
        data = spi_read();
// if (debug) printf("spi_read : 0x%x\n",data); 
        if ((data & mask) == tar) {
            break;
        }
        --timer;
    }
    if (timer == 0) panic(msg);

    return data;
}

extern uint16 crc16(uint16 crc, uint8 data);
extern uint8 calculate_crc7(const void *data, int len);

uint8 gen_crc(uint8 cmd, uint32 arg) {
    uint8 sd_cmd_buffer[5];
    sd_cmd_buffer[0] = cmd | 0x40;
    sd_cmd_buffer[1] = (arg >> 24) & 0xFF;
    sd_cmd_buffer[2] = (arg >> 16) & 0xFF;
    sd_cmd_buffer[3] = (arg >> 8) & 0xFF;
    sd_cmd_buffer[4] = arg & 0xFF;

    // 调用calculateCRC7函数计算CRC7值
    uint8 calculated_crc = calculate_crc7(sd_cmd_buffer, 5); // 传入除CRC字段之外的5个字节
    return calculated_crc;
}

static struct sdcard_disk {
    struct semaphore mutex_disk;
    struct spinlock lock_disk;
} sdcard_disk;


static void __sdcard_cmd(uint8 cmd, uint32 arg, uint8 crc) {
    ASSERT(cmd < 64);

    QSPI2_CSMODE = CSMODE_HOLD; // 设置为 HOLD 模式

    spi_write(0x40 | cmd);
    spi_write(arg >> 24);
    spi_write(arg >> 16);
    spi_write(arg >> 8);
    spi_write(arg);
    spi_write((crc << 1) | 1);

    return;
}

int R1_response(uint8 *cmdix, uint8 *crc, uint8 *card_status) {
    uint8 data, crc7_end;
    int ret = 0;
    // do {
    //     data = spi_read();
    // } while (data >= 0x40); // wait for b'00xx_xxxx 
    data = polltest(NULL,0x00,0xC0, "R1 wait too long!");
    *cmdix = data;

    for (int i = 3; i >= 0; --i) {
        card_status[i] = spi_read();
    }
    crc7_end = spi_read();
    *crc = (crc7_end >> 1);
    
    // ASSERT(crc == (crc7_end >> 1) );
    // ASSERT(crc7_end & 0x1 ); // end bit should be 1

    wmb();
    spi_write(0xff);
    QSPI2_CSMODE = CSMODE_AUTO; // 设置回 AUTO 模式

    if (crc7_end & 0x1) {
        // 停止位为 1
        ret = 0;
    } else {
        ret = -1;
    }

    return ret;
}

// 针对 CMD58, READ OCR 寄存器的响应
int R3_response(uint8 *ocr) {
    uint8 res_end;
    // do {
    //     data = spi_read();
    // } while (data >= 0x40 && ( (data & 1) == RES_IX_IDLE) ); // wait for b'0000_0X01
    polltest(NULL, 0, 0xff, "R3 wait too long!");

    for (int i = 3; i >= 0; --i) {  
        ocr[i] = spi_read();
    }

    int rc = 0;
    if ( !(ocr[3] & 0x80) ) {    /* Power up status */ // 上电没完成时 第31位设置为0
        rc = -1;                
    }

    res_end = spi_read(); // b'1111_1111

    wmb();
    spi_write(0xff);
    QSPI2_CSMODE = CSMODE_AUTO; // 设置回 AUTO 模式

    if (res_end & 0x1) {
        ;
    } else {
        rc = -1;
    }
    return rc;
}

// 针对 CMD8 的响应
int R7_response(uint8 *vhs, uint8* check_pattern, uint8 *crc) {
    uint8 crc7_end;
    int ret = 0;
    // do {
    //     data = spi_read();
    // } while (data >= 0x40); // wait for b'0000_1000; 
    polltest(NULL,0x00,0xC0, "R7 wait too long!");

    // ASSERT(data == 0x08);

    spi_read(); // resvd
    spi_read(); // resvd
    *vhs = spi_read() & 0xf;
    *check_pattern = spi_read();
    crc7_end = spi_read();
    *crc = (crc7_end >> 1);

    // ASSERT(crc == (crc7_end >> 1) );
    // ASSERT(crc7_end & 0x1 ); // end bit should be 1

    wmb();
    QSPI2_CSMODE = CSMODE_AUTO; // 设置回 AUTO 模式

    if (crc7_end & 0x1) {
        // 停止位为 1
        ret = 0;
    } else {
        ret = -1;
    }

    return ret;
}

// /*
//     ==== 基本命令集 ====
// */
// CMD0 R1 response
int cmd_go_idle_state() {
    // SD 卡复位
    const uint8 cmd = SD_CMD0;
    uint32 arg = 0;
    uint8 crc = 0x4A;
    // ASSERT(gen_crc(cmd, arg) == crc);

    int ret = 0;
    // /*
    uint8 cmdix;
    uint32 card_status = 0;
    uint8 crc_ret = 0;

    __sdcard_cmd(cmd, arg, crc);
    ret = R1_response(&cmdix, &crc_ret, (uint8 *)&card_status);
    // */
    if (cmdix != RES_IX_IDLE) {
        ret = -1;
    }
    return ret;

}

// CMD8
int cmd_send_if_cond() {
    const uint8 cmd = SD_CMD8;
    uint32 arg = 0x1AA; // 检测 2.7~3.6 V 电压是否支持，检测模式 10101010'b
    uint8 crc = 0x43;
    // ASSERT(gen_crc(cmd, arg) == crc);

    int ret;
    uint8 vhs;
    uint8 check_pattern;
    uint8 crc_ret = 0;

    __sdcard_cmd(cmd, arg, crc);
    ret = R7_response(&vhs, &check_pattern, &crc_ret);
    if ( vhs == 1 && check_pattern == 0xAA ) {
        // ok
        ;
    } else {
        ret = -1;
    }

    return ret;
}

// // CMD9
// void cmd_send_csd() {
//     ;
//     // __sdcard_cmd(SD_CMD9, );
// }

// // CMD12 R1b响应
int cmd_stop_transmission() {
    // 停止读多块
    const uint8 cmd = SD_CMD12;
    uint32 arg = 0;
    uint8 crc = 0x30;
    // ASSERT(gen_crc(cmd, arg) == crc);

    int ret = 0;
    // uint8 cmdix;
    // uint32 card_status = 0;
    // uint8 crc_ret = 0;

    __sdcard_cmd(cmd, arg, crc);
    // uint8 data;
    // while ( (data = spi_read()) >= 0x40 ) {    // wait for b'00xx_xxxx
    //     ;
    // }
    polltest(NULL,0x00,0x80, "sdcard::cmd_stop_transmission: wait too long!");


    // ret = R1_response(&cmdix, &crc_ret, (uint8 *)&card_status); // is it ok ?

    return ret;
}

/*
    ==== 块读命令集 ====
*/
// CMD16 R1 响应
int cmd_set_blocklen(uint blksize) {
    // 设置块的长度
    const uint8 cmd = SD_CMD16;
    uint32 arg = blksize;
    uint8 crc = gen_crc(cmd, arg);

    int ret = 0;
    uint8 cmdix;
    uint32 card_status = 0;
    uint8 crc_ret = 0;

    __sdcard_cmd(cmd, arg, crc);
    ret = R1_response(&cmdix, &crc_ret, (uint8 *)&card_status);

// printf("ret = %d\n",ret);
// printf("cmdix = %d\n",cmdix);

    if ((cmdix & 0xC0) > RES_IX_IDLE) {
        ret = -1;
    }

    return ret;
}

// CMD17 R1 响应
int cmd_read_single_block(uint sector) {
    // 读单块
    const uint8 cmd = SD_CMD17;
    uint32 arg = sector;
    uint8 crc = gen_crc(cmd, arg);
    
    int ret = 0;
    // uint8 cmdix;
    // uint32 card_status = 0;
    // uint8 crc_ret = 0;

    __sdcard_cmd(cmd, arg, crc);
    // while (spi_read() >= 0x40) {    // wait for b'00xx_xxxx
    //     ;
    // }
    polltest(NULL,0x00,0x80, "sdcard::cmd_read_single_block: wait too long!");


    // ret = R1_response(&cmdix, &crc_ret, (uint8 *)&card_status);
    // if ( cmdix > RES_IX_IDLE ) {
    //     ret = -1;
    // }

    return ret;
}

// CMD18 R1 响应
int cmd_read_mutiple_block(uint sector) {
    // 读多块
    const uint8 cmd = SD_CMD18;
    uint32 arg = sector;
    uint8 crc = gen_crc(cmd, arg);
    
    int ret = 0;
    // uint8 cmdix;
    // uint32 card_status = 0;
    // uint8 crc_ret = 0;

    __sdcard_cmd(cmd, arg, crc);
    // int timer = 10;
    // while (spi_read() >= 0x40 && --timer) {     // wait for b'00xx_xxxx
    //     ;
    // }
    polltest(NULL,0x00,0xC0, "sdcard::cmd_read_mutiple_block: wait too long!");

    // ret = R1_response(&cmdix, &crc_ret, (uint8 *)&card_status);

    // if ( cmdix > RES_IX_IDLE ) {
    //     ret = -1;
    // }

    return ret;
}

// /*
//     ==== 块写命令集
// */
// CMD24
int cmd_write_block(uint sector) {
    // 写单块
    const uint8 cmd = SD_CMD24;
    uint32 arg = sector;
    uint8 crc = gen_crc(cmd, arg);
    
    int ret = 0;
    // uint8 cmdix;
    // uint32 card_status = 0;
    // uint8 crc_ret = 0;

    __sdcard_cmd(cmd, arg, crc);
    // ret = R1_response(&cmdix, &crc_ret, (uint8 *)&card_status);

    // if (cmdix != 0) {
    //     ret = -1;
    // }
    polltest(NULL,0x00,0xff, "sdcard::cmd_write_block: wait too long!");


    return ret;
}

// CMD25
int cmd_write_multiple_block(uint sector) {
    // 写多块
    const uint8 cmd = SD_CMD25;
    uint32 arg = sector;
    uint8 crc = gen_crc(cmd, arg);
    
    int ret = 0;
    // uint8 cmdix;
    // uint32 card_status = 0;
    // uint8 crc_ret = 0;

    __sdcard_cmd(cmd, arg, crc);

    // while (spi_read() != 0x0) {    
    //     ;
    // }
    polltest(NULL,0x00,0xff, "sdcard::cmd_write_multiple_block: wait too long!");
    
    // ret = R1_response(&cmdix, &crc_ret, (uint8 *)&card_status);

    // if (cmdix != 0) {
    //     ret = -1;
    // }

    return ret;
}

// /*
//     ==== 应用命令 ====
// */
//     // ;

// CMD55 R1 response
int cmd_app() {
    const uint8 cmd = SD_CMD55;
    uint32 arg = 0;
    uint8 crc = 0x32;
    ASSERT(gen_crc(cmd, arg) == crc);

    int ret;
    uint8 cmdix;
    uint32 card_status = 0;
    uint8 crc_ret = 0;

    __sdcard_cmd(cmd, arg, crc);
    ret = R1_response(&cmdix, &crc_ret, (uint8 *)&card_status);
    if ( cmdix != RES_IX_IDLE ) {
        ret = -1;
    }

    return ret;
}

// CMD58  R3 响应
int cmd_read_ocr(uint8 *ccr) {
// return 0;
    const uint8 cmd = SD_CMD58;
    uint32 arg = 0;
    uint8 crc = 0x7E;
    ASSERT(gen_crc(cmd, arg) == crc);

    int ret;
    uint32 ocr = 0;

    __sdcard_cmd(cmd, arg, crc);
    ret = R3_response((uint8 *)&ocr);

// printf("cmd_read_ocr: ocr = 0x%x\n",ocr);
    if ((int)ocr >= 0) {
        printf("[SD card] ERROR! 上电未完成!\n");
        ret = -1;
    } else {
        if (ocr & (0x1 << 30)) {        // CCS bit
            *ccr = 1;
            printf("[SD card]It's SDHC or SDXC.大容量卡\n");
            ;
        } else {
            *ccr = 0;
            printf("[SD card]It's SDSD. 小容量卡，<= 2GB\n");
        }
    }
    
    return ret;

}

// CMD59  R1 响应
int cmd_crc_on_off(int on) {
    const uint8 cmd = SD_CMD59;
    uint32 arg = on;
    uint8 crc = on ? 0x41 : 0x48;
    ASSERT(gen_crc(cmd, arg) == crc);

    int ret;
    uint8 cmdix;
    uint32 card_status = 0;
    uint8 crc_ret = 0;

    __sdcard_cmd(cmd, arg, crc);
    ret = R1_response(&cmdix, &crc_ret, (uint8 *)&card_status);
    
    if (cmdix > RES_IX_IDLE) {
        ret = -1;
    }

    return ret;
}

/*
    ==== ACMD 命令

*/
// ACMD41
int cmd_sd_send_op_cond() {
    const uint8 cmd = SD_ACMD41;
    uint32 arg = 0x1 << 30;
    uint8 crc = 0x3B;

    int ret = 0;

    // uint32 ocr = 0;
    // if ( cmd_app() < 0 ) {
    //     return -1;
    // }
    // __sdcard_cmd(cmd, arg, crc);
    // // ret = R3_response((uint8*)&ocr);
    
    // if ( (int)ocr < 0 ) {
    //     // ok, busy bit is 1
    //     // card initialization finished
    //     ;
    // } else {
    //     ret = -1;
    // }
    // return ret;   

    int rc;
    int timer = 100;
    do {
        // 没有使用 R3 响应
        cmd_app();
        __sdcard_cmd(cmd, arg, crc);
        rc = polltest(NULL,0x00,0x80, "cmd_sd_send_op_cond: wait response too long!");
        spi_write(0xff);
        QSPI2_CSMODE = CSMODE_AUTO; 
    } while (rc == RES_IX_IDLE && --timer);

    if (!timer) panic("ACMD41 timeout!");

    if (rc != 0) {
        ret = -1;
    }
    return ret;
}

static int __sd_init() {
    // QSPI2_Init(2399200);
printf("ready to enter QSPI2_Init...\n");

    QSPI2_Init();
printf("QSPI2_Init [ok]\n");

    int timer = 100;
    // SD卡复位 CMD0
    while (--timer && cmd_go_idle_state() < 0) {
        ;
    }
    if (!timer) {
        panic("[SD card]cmd_go_idle_state error!\n");
    } else {
        printf("SD card:cmd0 go_idle_state [ok]\n");
    }

    // 检测操作条件 CMD8
    timer = 100;
    while (--timer && cmd_send_if_cond() < 0 ){
        ;
    }
    if (!timer) {
        panic("[SD card]cmd_send_if_cond error!\n");
    } else {
        printf("SD card:cmd8 send_if_cond [ok]\n");
    }
    
    // 开启 CRC CMD59 
    // timer = 100;
    // while (--timer && cmd_crc_on_off(1) < 0) {
    //     ;
    // }
    // if (!timer) {
    //     // QEMU seems not supporting this cmd
    //     panic("[SD card]cmd_crc_on_off error!\n");
    // } else {
    //     printf("SD card:cmd59 crc_on_off [ok]\n");
    // }

    // SD卡初始化 ACMD41 
    timer = 10;
    while (--timer && cmd_sd_send_op_cond() < 0 ){
        ;
    }
    if (!timer) {
        panic("[SD card]cmd_sd_send_op_cond error!\n");
    } else {
        printf("SD card:acmd41 sd_send_op_cond [ok]\n");
    }

    // 查询 OCR 的 CCS位 CMD58
    timer = 100;
    uint8 ccs = 0;

// #ifndef SIFIVE_U
    while (--timer && cmd_read_ocr(&ccs) < 0) {
        ;
    }
    if (!timer) {
        // QEMU seems not supporting this cmd
        panic("[SD card]cmd_read_ocr error!\n");
        ;
    } else {
        printf("SD card:cmd58 cmd_read_ocr [ok]\n");
    }
// #endif

    // 设置读写块长度 CMD16 (若为 SDHC或 SDXC则 block length is fixed to 512 Bytes)
    // if (!ccs) {
    timer = 100;
    while (--timer && cmd_set_blocklen(BSIZE) < 0) {
        ;
    }
    if (!timer) {
        panic("[SD card]cmd_set_blocklen error!\n");
    } else {
        printf("SD card:cmd16 set_blocklen [ok]\n");
    }
    // }

    // 读取 CSD， 获取存储卡信息 CMD9
    // TODO()
    // QSPI2_SCKDIV = 60;
    QSPI2_SCKDIV = 25;      // set the QSPI controller to 20 MHz

    for (int _ = 0; _ != 11; ++_) {;}
    
    printf("The SD card is initialized\n");
    return 0;
}


void sdcard_disk_init() {
    __sd_init();
    sema_init(&sdcard_disk.mutex_disk, 1, "sdcard_sem");
    initlock(&sdcard_disk.lock_disk, "sdcard_lock");
    return;
}

// seems to have bug
// /*
static void __sd_single_read(void *addr, uint sec) {
    volatile uint8 *pos;
    pos = (uint8 *)addr;
    // int retry_ctr = 1;
    // int timer;
// retry:
    // 发送 CMD17
    cmd_read_single_block(sec);
    spi_write(0xff);

    // while ( --timer && cmd_read_single_block(sec) < 0) {
    //     ;
    // }
    
    // QSPI2_CSMODE = CSMODE_HOLD;         // bugs!!!

    // 接收数据启始令牌 0xFE
    polltest(NULL,0xFE,0xff,"sdcard single read fail!");

    // 接收一个扇区的数据
    uint32 tot = BSIZE;
    uint16 crc = 0;
    uint8 d;
    while (tot-- > 0) {
        d = spi_read();
        *pos++ = d;
        crc = crc16(crc,d);
    }
    // crc16 = calculate_crc16(addr, BSIZE);

    // 接收两个字节的 CRC，若无开启，这两个字节读取后可丢弃
    uint16 crc16_ret;
    crc16_ret = (spi_read() << 8);
    crc16_ret |= spi_read();
    
    if (crc != crc16_ret ) {
        // if (retry_ctr-- > 0) {
        //     goto retry;
        // } 
        // else {
            // panic("single read : crc error!");
            printf("single read : crc error!\n");
        // }
    }

    // 8 CLK 之后禁止片选 : （未处理）
    for (int _ = 0; _ != 11; ++_) {;}


    // cmd_stop_transmission();
    spi_write(0xff);
    QSPI2_CSMODE = CSMODE_OFF;
    
    
    return;
}
// */

static void __sd_multiple_read(void *addr, uint sec, uint nr_sec) {
    volatile uint8 *pos;
    pos = (uint8 *)addr;
// printf("==> multiple_read %d\n",nr_sec);

    // 发送 CMD18
    cmd_read_mutiple_block(sec);
    spi_write(0xff);
    // while ( --timer && cmd_read_mutiple_block(sec) < 0) {
    //     ;
    // }
    // if (!timer) panic("read_mutiple_block fail!");
    // QSPI2_CSMODE = CSMODE_HOLD;         // bugs!!!

	// if (__sd_cmd(SD_CMD18, sec, gen_crc(SD_CMD18, sec)) != 0x00) {
    //     panic("...");
    // }


    for ( int block_ctr = 0; block_ctr != nr_sec; ++block_ctr) {
        // 接收数据启始令牌 0xFE
        polltest(NULL,0xFE,0xff,"read_mutiple_block fail!");

        // 接收一个扇区的数据
        uint32 tot = BSIZE;
        uint16 crc = 0;
        uint8 d;
        while (tot-- > 0) {
            d = spi_read();
            *pos++ = d;
            crc = crc16(crc,d);
        }
        // crc16 = calculate_crc16(addr + BSIZE* block_ctr, BSIZE);

        // 接收两个字节的 CRC，若无开启，这两个字节读取后可丢弃
        uint16 crc16_ret;
        crc16_ret = (spi_read() << 8);
        crc16_ret |= spi_read();
        
        if (crc != crc16_ret ) {
            // sloppy handle
            // panic("sdcard multiple read fail!");
            printf("sdcard multiple read fail! crc error!\n");
        }

        spi_write(0xff);    // 发送一字节的时钟, 挺重要的
    }

    // 发送 CMD12 停止命令
    cmd_stop_transmission();

    polltest(NULL,0xFF,0xff,"cmd_stop_transmission keep busy!"); 



    // 8 CLK 之后禁止片选 : （未处理）
    for (int _ = 0; _ != 11; ++_) {;}

    spi_write(0xff);
    QSPI2_CSMODE = CSMODE_OFF;


// printf("<== multiple_read %d\n",nr_sec);
    
    return;
}

static void __sd_single_write(void *addr, uint sec) {
    volatile uint8 *pos;
    pos = (uint8 *)addr;


    // 发送 CMD24，收到 0x00 表示成功
    cmd_write_block(sec);
    spi_write(0xff);        // 发送多个时钟

    // timer = 10;
    // while (--timer && cmd_write_block(sec) < 0 ) {
    //     ;
    // }
    // if (!timer) panic("__sd_single_write failed!");
    
    // for (int _ = 0; _ != 3; ++_) {
    //     spi_write(0xff);        // 发送多个时钟
    // }

    // QSPI2_CSMODE = CSMODE_HOLD; 
    // 发送写单块开始字节 0xFE
    spi_write(0xFE);

    // 发送 512个字节数据
    uint32 tot = BSIZE;
    while (tot-- > 0) {
        spi_write(*pos++);
    }

    // 发送两个字节的伪 CRC，(可以均为 0xff)
    spi_write(0xff);
    spi_write(0xff);

    // 连续读直到读到 XXX00101 表示数据写入成功
    polltest(NULL, 0x5, 0x1f, "sd_single_write:write failed!");

    // 继续读进行忙检测 (读到 0x00 表示SD卡正忙)，当读到 0xff 表示写操作完成
    polltest(NULL, 0xFF, 0xff, "sd_single_write:busytest failed!");

    QSPI2_CSMODE = CSMODE_AUTO; // 设置回 AUTO 模式
    return;
}

static void __sd_multiple_write(void *addr, uint sec, uint nr_sec) {
    volatile uint8 *pos;
    pos = (uint8 *)addr;
// printf("==> multiple_write %d\n",nr_sec);

    // 发送 CMD25，收到 0x00 表示成功
    cmd_write_multiple_block(sec);
    // int timer = 10;
    // while (--timer && cmd_write_multiple_block(sec) < 0 ) {
    //     ;
    // }
    // if (!timer) panic("__sd_multiple_write failed!");

    // spi_write(0xff);        // 发送一个字节的时钟

    do {
        // 发送若干时钟 
        // for (int _ = 0; _ != 10; ++_) { ; } // (也许这样可以)
        spi_write(0xff);        // 发送一个字节的时钟
        spi_write(0xff);        // 发送一个字节的时钟

        // 发送写多块开始字节 0xFC
        spi_write(0xFC);

        // 发送 512 字节
        uint32 tot = BSIZE;
        while (tot-- > 0) {
            spi_write(*pos++);
        }

        // 发送两个 CRC （可以均为 0xff）
        spi_write(0xff);
        spi_write(0xff);

        // 连续读直到读到 XXX00101 表示数据写入成功
        polltest(NULL, 0x5, 0x1f, "sd_multiple_write:write failed!");

        // 继续读进行忙检测，直到读到 0xFF 表示写操作完成
        polltest(NULL, 0xFF, 0xff, "sd_multiple_write:busytest failed!");

    } while (--nr_sec);

    // 发送写多块停止字节 0xFD 来停止写操作
    spi_write(0xFD);

    // 进行忙检测直到读到 0xFF
    polltest(NULL, 0xFF, 0xff, "sd_multiple_write:write all failed!");

    QSPI2_CSMODE = CSMODE_AUTO; // 设置回 AUTO 模式
// printf("<== multiple_write %d\n",nr_sec);
    return;
}

// extern int __sd_single_read(void * , uint );
extern void dma_req(uint64 pa_des, uint64 pa_src, uint32 nr_bytes );
void sdcard_disk_read(void *addr, uint sec, uint nr_sec) {
// printf("sd read %d \n",nr_sec);

    // sema_wait(&sdcard_disk.mutex_disk);
    acquire(&sdcard_disk.lock_disk);
    if (nr_sec == 1) {
        __sd_single_read(addr, sec);
        // __sd_multiple_read(addr, sec, 1);

        // __sd_test();

    } else {
        __sd_multiple_read(addr, sec, nr_sec);
    }
    release(&sdcard_disk.lock_disk);
    // sema_signal(&sdcard_disk.mutex_disk);
}

void sdcard_disk_write(void *addr, uint sec, uint nr_sec) {
// printf("sd write %d \n",nr_sec);

    acquire(&sdcard_disk.lock_disk);
    if (nr_sec == 1) {
        __sd_single_write(addr, sec);
        // __sd_multiple_write(addr, sec, 1);
    } else {
        __sd_multiple_write(addr, sec, nr_sec);
    }
    release(&sdcard_disk.lock_disk);
}

void sdcard_disk_rw(struct bio_vec *bio_vec, int write) {
    uint sec;
    uint nr_sec;

    sec = bio_vec->blockno_start * (BSIZE / 512);
    nr_sec = bio_vec->block_len;

#ifdef SIFIVE_U
    sec *= BSIZE;
#endif

    push_off();
    if (write) {
        sdcard_disk_write(bio_vec->data, sec, nr_sec);
    } else {
        sdcard_disk_read(bio_vec->data, sec, nr_sec);
    }
    pop_off();

    return;
}

inline void disk_rw(void *bio_vec, int write, int type) {
    sdcard_disk_rw((struct bio_vec *)bio_vec, write);
}

void disk_init() {
    sdcard_disk_init();
}



void __sd_test() {

    int sec = 0;
        // just for test
    // __sdRead(addr, sec, 2);
    printf("SD card I/O test ...\n");

    // 单块读写连续性测试
    for ( int repeat = 1; repeat <= 114; ++repeat) {
        char addr[BSIZE] = {0};
        char data_tmp[BSIZE] = {0};
        // __sd_single_read(addr,sec);
        // __sd_single_read(addr,sec);
        __sd_multiple_read(addr, sec,1);        // 填充进 addr
        memmove(data_tmp,addr,BSIZE);
        __sd_single_write(data_tmp,sec);
        // __sd_single_read(addr,sec);
        __sd_multiple_read(addr, sec,1);        // 填充进 addr
        ASSERT(!memcmp(addr, data_tmp,BSIZE));
printf("No.%d [pass]\n",repeat);
    }

    // 多块读写连续性测试
    // __sd_multiple_write(addr,sec,1024);     // seem to fail

    for (int nr_sec = 2; nr_sec <= 514; nr_sec *= 2) {
        char *data_tmp = (char*)kzalloc(nr_sec * BSIZE);
        char *addr = (char*)kzalloc(nr_sec * BSIZE);
        ASSERT(data_tmp);
        __sd_multiple_read(addr, sec,nr_sec);        // 填充进 addr
printf("hit 1\n");            
        memmove(data_tmp,addr,nr_sec*BSIZE);
        __sd_multiple_write(data_tmp,sec,nr_sec);
printf("hit 2\n");            
        __sd_multiple_read(addr,sec,nr_sec);
printf("hit 3\n");            
        ASSERT(!memcmp(addr, data_tmp, nr_sec * BSIZE));       
printf("compare [ok]\n");            
        kfree(data_tmp);             // nr_sec = 512 时，运行到这里似乎会卡住
printf("nr_sec = %d [pass]\n",nr_sec);
    }

    printf("SD card I/O test [ok]\n");

// */ // end of sdcard I/O test

/*
        // DMA 测试
    {
        char *data_tmp = (char*)kzalloc(nr_sec * BSIZE);
        uint64 pa_des;
        pa_des = (uint64)data_tmp;
    
#ifndef SIFIVE_U
        sec *= BSIZE; 
#endif
        // pa_src = sec;
        dma_req(pa_des,(uint64)addr,nr_sec * BSIZE);      // 内存 to 内存

        ASSERT(!memcmp(addr, data_tmp,BSIZE));
        kfree(data_tmp);
    }
*/   // end DMA 测试
        return;
}