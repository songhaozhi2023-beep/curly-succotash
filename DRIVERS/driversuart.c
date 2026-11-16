#include "config.h"        // 系统配置：u8/u16/u32 类型 + 引脚宏
#include "driversuart.h"   // 串口驱动头文件
#include "delay.h"         // delay_us/delay_ms（fire-and-forget 延时）

/*=============================================================
 * UART2 驱动 — Modbus-RTU（RS485-1）
 * 4G 板 RS485-1 接 UART2：P1.0=RXD2, P1.1=TXD2, 方向 DE/RE=P8.7
 * 基础版 Modbus 走 UART1，4G 版 UART1 让给 4G 模组(ML307R-DC)。
 *
 * ★ STC32G UART 关键方案（2026-08-18 实测验证，移植必须遵守）：
 *   1. fire-and-forget 发送：写 S2BUF 即返回，不阻塞等 S2TI
 *      —— STC32G 在"收到字节后"再发送时，S2TI 可能永不置位，
 *        用 while(!S2TI) 阻塞会死循环卡死（实测踩坑）。
 *   2. 发送前重新初始化 UART2（UART2_Init）清掉"收后发"卡死状态。
 *   3. 接收：先读 S2BUF 再清 S2RI。
 *=============================================================*/

u8 xdata uart2_rx_buf[UART_RX_BUF_SIZE];  // 接收缓冲区（XDATA）
u8 uart2_rx_wptr = 0;      // 接收写指针
u8 uart2_rx_rptr = 0;      // 接收读指针

/* UART2 初始化：引脚 + Timer2 波特率(1T) + 中断 */
void UART2_Init(void)
{
    /* 引脚：P1.1=TXD2 推挽输出，P1.0=RXD2 高阻输入 */
    P1M1 &= ~0x02;  P1M0 |= 0x02;
    P1M1 |= 0x01;   P1M0 &= ~0x01;

    /* 方向控制 P8.7 推挽，初始接收（扩展端口访问需临时 EAXFR=1；RS485_DIR_RX 已内置切换） */
    XSFR_ON();
    P8M1 &= ~0x80;  P8M0 |= 0x80;
    XSFR_OFF();
    RS485_DIR_RX();

    /* Timer2 波特率发生器，1T 模式（Tx12=1→1T；9600@24MHz → T2=0xFD8F） */
    AUXR |= 0x04;                   /* T2x12=1：1T 模式 */
    T2L = (u8)UART2_T2_RELOAD;
    T2H = (u8)(UART2_T2_RELOAD >> 8);
    AUXR |= 0x10;                   /* T2R=1：启动 Timer2 */

    S2CON = 0x50;                   /* 模式1(8位) S2SM1=1 + 允许接收 S2REN=1 */
    IE2 |= 0x01;                    /* ES2=1：使能 UART2 中断 */
    EA = 1;
}

/* UART2 中断服务：接收入环形缓冲（先读后清），发送完成清 S2TI */
void UART2_ISR(void) interrupt 8
{
    if(S2CON & S2RI) {
        u8 dat;
        u8 next_wptr;
        dat = S2BUF;                /* 先读数据 */
        S2CON &= ~S2RI;             /* 再清 RI */
        next_wptr = uart2_rx_wptr + 1;
        if(next_wptr >= UART_RX_BUF_SIZE) next_wptr = 0;
        if(next_wptr != uart2_rx_rptr) {
            uart2_rx_buf[uart2_rx_wptr] = dat;
            uart2_rx_wptr = next_wptr;
        }
        /* 缓冲区满则丢弃，防止覆盖未读数据 */
    }
    if(S2CON & S2TI) {
        S2CON &= ~S2TI;
    }
}

/* 发送单字节（fire-and-forget，不阻塞） */
void UART2_SendByte(u8 dat)
{
    UART2_Init();                   /* 清"收后发"卡死状态 */
    RS485_DIR_TX();                 /* 切发送 */
    S2BUF = dat;                    /* 写 S2BUF 即启动发送 */
    delay_us(1200);                 /* 9600 一字节≈1.04ms，延时等发完 */
    RS485_DIR_RX();                 /* 切回接收 */
}

/* 发一帧（Modbus 用）：帧级方向控制，重初始化一次 */
void UART2_SendFrame(u8 xdata *buf, u8 len)
{
    u8 i;
    UART2_Init();                   /* 清卡死状态（每帧一次） */
    RS485_DIR_TX();
    for(i = 0; i < len; i++) {
        S2BUF = buf[i];
        delay_us(1200);             /* 字节间隔<3.5字符时间，满足 Modbus 帧要求 */
    }
    delay_ms(1);                    /* 最后一字节发完再切回接收 */
    RS485_DIR_RX();
}

void UART2_SendStr(char *str)
{
    while(*str) {
        UART2_SendByte((u8)*str);
        str++;
    }
}

bit UART2_GetRxFlag(void)
{
    return (uart2_rx_wptr != uart2_rx_rptr);
}

void UART2_ClrRxFlag(void)
{
    uart2_rx_rptr = uart2_rx_wptr;
}

u8 UART2_GetRxLen(void)
{
    return (u8)(uart2_rx_wptr - uart2_rx_rptr);
}

u8 xdata *UART2_GetRxBuf(void)
{
    return &uart2_rx_buf[uart2_rx_rptr];
}
