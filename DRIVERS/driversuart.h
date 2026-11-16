#ifndef __UART_H          // 头文件保护宏，防止重复包含
#define __UART_H          // 定义头文件标识符

/*********************************************************
*  串口驱动头文件 - 4G 版串口分配
*  UART1 (P3.0/P3.1)：4G 模组 ML307R-DC 的 AT 指令（见 ml307r.h）
*  UART2 (P1.0/P1.1)：Modbus-RTU（RS485-1，方向控制 P8.7）
*  UART3 (P5.0/P5.1)：RS485-2，方向控制 P8.6（扩展，未启用）
**********************************************************/

#include "config.h"       // 包含系统配置头文件，定义基本数据类型和系统配置

// UART2(Modbus)缓冲区大小定义
#define UART_RX_BUF_SIZE  64   // 接收缓冲区大小
#define UART_TX_BUF_SIZE  64   // 发送缓冲区大小

/* UART2 波特率（Modbus 常用 9600）与 Timer2 重载值（1T 模式） */
#define UART2_BAUD        9600L
#define UART2_T2_RELOAD   (u16)(65536UL - SYSCLK / 4UL / UART2_BAUD)

void UART2_Init(void);            // UART2 初始化（含波特率/中断/方向）
void UART2_SendByte(u8 dat);      // UART2 发送单字节（fire-and-forget）
void UART2_SendStr(char *str);    // UART2 发送字符串
void UART2_SendFrame(u8 xdata *buf, u8 len);  // 发一帧（Modbus 用，帧级方向控制）

// 接收接口（ISR 写入环形缓冲）
bit    UART2_GetRxFlag(void);     // 是否有收到数据
void   UART2_ClrRxFlag(void);     // 清空接收缓冲
u8     UART2_GetRxLen(void);      // 已收字节数
u8 xdata *UART2_GetRxBuf(void);   // 接收缓冲指针

#endif                  // 头文件结束标识
