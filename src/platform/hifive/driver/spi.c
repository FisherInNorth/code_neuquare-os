#include "platform/hifive/spi_hifive.h"
#include "memory/memlayout.h"
extern inline unsigned int spi_min_clk_divisor(unsigned int input_khz, unsigned int max_target_khz);


// SD card initialization must happen at 100-400kHz
#define SD_POWER_ON_FREQ_KHZ 400L
// SD cards normally support reading/writing at 20MHz
#define SD_POST_INIT_CLK_KHZ 20000L

// 读写同步很重要
inline uint8 __spi_xfer(uint8 dataframe) {
    int r;
    // int txdata;
    // do {
    //     txdata = QSPI2_TXDATA;
    // } while ( txdata < 0 );

    QSPI2_TXDATA = dataframe;
    rmb();

    do {
        r = QSPI2_RXDATA;
    } while (r < 0);
    
    return (r & 0xff);
}

inline void spi_write(uint8 dataframe) {
    __spi_xfer(dataframe);
}

inline uint8 spi_read() {
    return __spi_xfer(0xff);
}

void QSPI2_Init() {
    // may need TODO():hfpclkpll 时钟初始化； 先假定上电后会初始化好
    for (int _ = 0; _ != 1000; ++_) { ; }

    QSPI2_CSID = 0;     // 设置默认片选 id 为 0
    QSPI2_CSDEF |= 0x1; // 设置 片选位宽为 1
    QSPI2_FMT = 0x80005;           // 数据帧 8 位，小端，双工

    QSPI2_FCTRL |= 1; // 控制器进行直接内存映射
    QSPI2_CSMODE = CSMODE_OFF;

    // QSPI2_SCKDIV &= (~0xfff);
    // QSPI2_SCKDIV = 0x3;            // 波特率为 pclk 时钟 8 分频
    // QSPI2_SCKDIV = spi_min_clk_divisor(input_clk_khz, SD_POWER_ON_FREQ_KHZ);
    
    QSPI2_SCKDIV = 1250;        // set the QSPI controller to 400 kHz
    for (int _ = 0; _ != 10; ++_) {
        __spi_xfer(0xff);
    }

    QSPI2_CSMODE = CSMODE_AUTO; // 设置 AUTO mode
}

