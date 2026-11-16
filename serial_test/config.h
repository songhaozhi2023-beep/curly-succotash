#ifndef __CONFIG_H
#define __CONFIG_H

#include <STC32G144K246.H>

/* 485 串口回显测试 — 最小配置 */

typedef unsigned char  u8;
typedef unsigned int   u16;
typedef unsigned long  u32;

#define SYSCLK                24000000L   /* 系统时钟（STC-ISP 烧录时 IRC 选 24MHz） */

/* RS485-1 方向控制 DE/RE = P8.7 */
#define RS485_DIR_TX()   (P8OUT |= 0x80)
#define RS485_DIR_RX()   (P8OUT &= ~0x80)

/* RS485-2 方向控制 DE/RE = P8.6 */
#define RS485_2_DIR_TX() (P8OUT |= 0x40)
#define RS485_2_DIR_RX() (P8OUT &= ~0x40)

#endif
