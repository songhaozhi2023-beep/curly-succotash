#ifndef __CONFIG_H__
#define __CONFIG_H__

#define FOSC                    48000000UL
#define BAUD                    (65536 - FOSC/4/115200)

//#define DEBUG

#define LDR_SIZE                (4 * 1024UL)                //用户系统ISP空间大小（4K字节）
#define CHIP_SIZE               (246 * 1024UL)              //芯片FLASH大小（246K字节）
#define AP_SIZE                 (CHIP_SIZE - LDR_SIZE)      //用户可用FLASH大小
#define IAP_BASE                (0x01000000 - AP_SIZE)      //IAP操作的基地址（逻辑地址）

#define LDR_VERSION             0x0200




#endif
