#ifndef __HIFIVE_SDCARD_H__
#define __HIFIVE_SDCARD_H__
// #include "fs/bio.h"

// SD 卡总共有 8 个寄存器，它们只能通过对应的命令来访问

#define SD_CMD0 0   // 复位 SD 卡
#define SD_CMD1 1   // 读 OCR 寄存器
#define SD_CMD8 8   // 发送 SD 卡接口条件，包含了主机支持的电压信息，并询问卡是否支持。
#define SD_CMD9 9   // 读 CSD 寄存器
#define SD_CMD10 10 // 读 CID 寄存器
#define SD_CMD12 12 // 停止读多块时的数据传输
#define SD_CMD13 13 // 读 Card_Status 寄存器
#define SD_CMD16 16 // 设置块的长度
#define SD_CMD17 17 // 读单块
#define SD_CMD18 18 // 读多块，直至主机发送 CMD12 为止
#define SD_CMD24 24 // 写单块
#define SD_CMD25 25 // 写多块
#define SD_CMD27 27 // 写 CSD 寄存器
#define SD_CMD28 28 // 设置地址保护组保护位
#define SD_CMD29 29 // 清除保护位
#define SD_CMD30 30 // 要求卡发送写保护状态，参数中有要查询的dizhi

#define SD_CMD55 55 // 告诉SD卡下一个命令是卡应用命令，不是常规命令
#define SD_CMD58 58 // 读 OCR 寄存器
#define SD_CMD59 59 // 开启/关闭 CRC

#define SD_ACMD41 41 // 发送卡的支持信息(HCS)，并要求卡通过命令线返回OCR 寄存器内容。

// 响应 command index
#define RES_IX_BUSY 0x00     // busy信号
#define RES_IX_IDLE 0x01     // 空闲状态
#define RES_IX_EERASE 0x02   // 擦除错误
#define RES_IX_ECMD 0x04     // 命令错误
#define RES_IX_ECRC 0x08     // CRC通信错误
#define RES_IX_EERASE2 0x10  // 擦除次序错误
#define RES_IX_EADDRESS 0x20 // 地址错误
#define RES_IX_EARG 0x40     // 参数错误

// #endif

#endif
