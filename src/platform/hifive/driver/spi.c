#include "platform/hifive/spi_hifive.h"

void QSPI2_Init() {
    // may need TODO():hfpclkpll 时钟初始化； 先假定上电后会初始化好
    QSPI2_SCKDIV &= (~0xfff);
    QSPI2_SCKDIV |= 0x3;            // 波特率为 pclk 时钟 8 分频
    QSPI2_CSID = 0;                 // 设置默认片选 id 为 0
    QSPI2_CSDEF = 1;                // 设置 片选位宽为 1
    QSPI2_FMT |= 0x80005;           // 数据帧 8 位，小端，双工

    QSPI2_FCTRL |= 1;               // 控制器进行直接内存映射
    QSPI2_CSMODE = CSMODE_OFF;               
    for ( int _ = 0; _ != 10; ++_) {;}

    QSPI2_CSMODE = CSMODE_AUTO;         // 设置 AUTO mode
}

// 读写同步很重要
static uint8 __spi_xfer(uint8 dataframe)
{
	int r;
    int txdata; 
    do {
        txdata = QSPI2_TXDATA;
    } while ( txdata < 0 );
    
    QSPI2_TXDATA = dataframe;
	
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