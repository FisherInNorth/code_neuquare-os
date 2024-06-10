#ifndef __DRIVER_CRC_H__
#define __DRIVER_CRC_H__

/*
*   Include some crc algorithm
*   
*   To reference functions below, do 
*       1. include this header file
*       2. add "extern RETTYPE FunName()" in .c file
*/


#include "common.h"


// CRC7 多项式：x^7 + x^3 + 1
#define CRC7_POLY 0x89
#define CRC16_POLY 0x11021

inline uint8 calculate_crc7(const void *data, int len) {
// ==> https://blog.csdn.net/ZLK1214/article/details/113427599
    const uint8 *p = data;
    int i, j;
    uint16 temp = 0;

    if (len != 0)
        temp = p[0] << 8;

    for (i = 1; i <= len; i++) {
        if (i != len)
            temp |= p[i];
        for (j = 0; j < 8; j++) {
            if (temp & 0x8000)
                temp ^= CRC7_POLY << 8;
            temp <<= 1;
        }
    }
    return temp >> 9;
}

inline uint8 crc7(uint8 prev, uint8 in)
{
// ==> https://github.com/sifive/freedom-u540-c000-bootloader/blob/master/sd/sd.c
  // CRC polynomial 0x89
  uint8 remainder = prev & in;
  remainder ^= (remainder >> 4) ^ (remainder >> 7);
  remainder ^= remainder << 4;
  return remainder & 0x7f;
}

inline uint16 crc16(uint16 crc, uint8 data)
{
// ==> https://github.com/sifive/freedom-u540-c000-bootloader/blob/master/sd/sd.c
  // CRC polynomial 0x11021
  crc = (uint8)(crc >> 8) | (crc << 8);
  crc ^= data;
  crc ^= (uint8)(crc >> 4) & 0xf;
  crc ^= crc << 12;
  crc ^= (crc & 0xff) << 5;
  return crc;
}

/* 计算CRC16校验码 */
inline uint16 calculate_crc16(const void *data, int len)
{
// ==> https://blog.csdn.net/ZLK1214/article/details/113427599
	const uint8 *p = data;
	int i, j;
	uint32 temp = 0;
 
	if (len != 0)
		temp = (p[0] << 24) | (p[1] << 16); // 填充前二分之一
	if (len > 2)
		temp |= p[2] << 8; // 填充到四分之三
 
	for (i = 3; i <= len + 2; i++)
	{
		if (i < len)
			temp |= p[i]; // 每次都填充最后四分之一的空间
		
		// 从左数第0~7位计算到左数第16~23位
		for (j = 0; j < 8; j++)
		{
			if (temp & 0x80000000)
				temp ^= CRC16_POLY << 15;
			temp <<= 1;
		}
	}
	return temp >> 16;
}


#endif // __CRC_H__