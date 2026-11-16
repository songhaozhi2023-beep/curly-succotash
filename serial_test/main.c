#include "config.h"

/*=============================================================
 * 485 串口回显测试（STC32G144K246）
 *
 * ===== 诊断开关 DIAG_BLINK =====
 *   DIAG_BLINK = 1 ：只闪 LED21（诊断 MCU/烧录/供电是否正常）
 *   DIAG_BLINK = 0 ：485 回显测试
 * 用法：先在 DIAG_BLINK=1 下烧录，LED21 能闪 = MCU 在跑、烧录/供电正常，
 *       再改 DIAG_BLINK=0 烧录做 485 回显。
 *
 * 测试对象：
 *   RS485-1 = UART2（P1.0=RXD2, P1.1=TXD2），方向 DE/RE = P8.7 → CN34
 *   RS485-2 = UART3（P5.0=RXD3, P5.1=TXD3），方向 DE/RE = P8.6 → CN35
 *
 * 用法（回显模式）：
 *   1. USB-485 转接器接 CN34（A→脚2，B→脚3，GND→脚1），串口助手 9600 8N1
 *   2. 发任意字节，固件原样回显 = RS485-1 通
 *   3. 把 USB-485 换到 CN35 测 RS485-2
 *
 * 波特率可改 TEST_BAUD（9600 默认）。
 * 波特率定时器公式（1T 模式）：baud = SYSCLK / (4 * (65536 - T))
 *=============================================================*/
#define DIAG_BLINK  0      /* 1=LED21闪灯诊断；0=485回显 */
#define TEST_BAUD    9600L

#define T2_RELOAD    (u16)(65536UL - SYSCLK / 4UL / TEST_BAUD)   /* UART2 用 Timer2 */
#define T3_RELOAD    (u16)(65536UL - SYSCLK / 4UL / TEST_BAUD)   /* UART3 用 Timer3 */

static void test_delay_ms(u16 ms);   /* 前置声明，供 SendByte 使用 */

/* ====== UART2 (RS485-1) ====== */
void UART2_Init(void)
{
    /* 引脚：P1.1=TXD2 推挽输出，P1.0=RXD2 输入 */
    P1M1 &= ~0x02;  P1M0 |= 0x02;   /* P1.1 推挽 */
    P1M1 |= 0x01;   P1M0 &= ~0x01;  /* P1.0 高阻输入（非模拟） */

    /* 方向控制 P8.7 推挽，初始接收 */
    P_SW2 |= 0x80;                  /* EAXFR：使能扩展端口 P8 访问 */
    P8M1 &= ~0x80;  P8M0 |= 0x80;
    RS485_DIR_RX();

    /* Timer2 波特率发生器，1T 模式（Tx12=1 → 1T；Tx12=0 是 12T）
     * 1T 公式：baud = SYSCLK/(4*(65536-T2))，9600@24MHz → T2=0xFD8F */
    AUXR |= 0x04;                   /* T2x12=1：1T 模式 */
    T2L = (u8)T2_RELOAD;
    T2H = (u8)(T2_RELOAD >> 8);
    AUXR |= 0x10;                   /* T2R=1：启动 Timer2 */

    S2CON = 0x50;                   /* 模式1(8位) S2SM1=1 + 允许接收 S2REN=1 */
}

void UART2_SendByte(u8 d)
{
    RS485_DIR_TX();
    S2BUF = d;                       /* 写 S2BUF 即启动发送 */
    test_delay_ms(2);                /* fire-and-forget：不阻塞等 S2TI，延时等字节发完（9600≈1ms）再切回接收 */
    RS485_DIR_RX();
}

bit UART2_GetByte(u8 *d)
{
    if(S2CON & S2RI) {
        *d = S2BUF;                  /* 先读数据 */
        S2CON &= ~S2RI;              /* 再清 RI */
        return 1;
    }
    return 0;
}

/* ====== UART3 (RS485-2) ====== */
void UART3_Init(void)
{
    /* 引脚：P5.1=TXD3_2 推挽输出，P5.0=RXD3_2 输入 */
    P5M1 &= ~0x02;  P5M0 |= 0x02;
    P5M1 |= 0x01;   P5M0 &= ~0x01;

    /* 方向控制 P8.6 推挽，初始接收；S3_S=1 选择 UART3 映射2（P5.0/P5.1） */
    P_SW2 |= 0x82;                  /* EAXFR(0x80) + S3_S(0x02)：UART3 切到 P5.0/P5.1 */
    P8M1 &= ~0x40;  P8M0 |= 0x40;
    RS485_2_DIR_RX();

    /* UART3 选 Timer3（S3CON.S3ST3=1）作波特率发生器，1T 模式 */
    T3T4M |= 0x02;                  /* T3x12=1：1T 模式 */
    T3L = (u8)T3_RELOAD;
    T3H = (u8)(T3_RELOAD >> 8);
    T3T4M |= 0x08;                  /* T3R=1：启动 Timer3 */

    S3CON = 0x50;                   /* S3ST3=1(用Timer3) + S3REN=1(允许接收) */
}

void UART3_SendByte(u8 d)
{
    RS485_2_DIR_TX();
    S3BUF = d;
    test_delay_ms(2);                /* fire-and-forget：不阻塞等 S3TI */
    RS485_2_DIR_RX();
}

bit UART3_GetByte(u8 *d)
{
    if(S3CON & S3RI) {
        *d = S3BUF;                  /* 先读数据 */
        S3CON &= ~S3RI;              /* 再清 RI */
        return 1;
    }
    return 0;
}

/* ====== 主函数 ====== */
#if DIAG_BLINK
/* 极简诊断：LED21(P4.0) 慢闪，只验证 MCU 在跑、烧录/供电正常 */
void main(void)
{
    volatile u32 i;                 /* volatile 防止优化等级8把延时循环删掉 */
    P4M1 &= ~0x01;  P4M0 |= 0x01;   /* P4.0 推挽 */
    while(1) {
        P40 = 1;
        for(i = 0; i < 500000; i++); /* ~0.3-0.5s 半周期（24MHz 粗估） */
        P40 = 0;
        for(i = 0; i < 500000; i++);
    }
}
#else
/* 简单忙等延时（24MHz 粗估） */
static void test_delay_ms(u16 ms)
{
    volatile u32 i;
    while(ms--) {
        for(i = 0; i < 4000; i++);
    }
}

void main(void)
{
    u8 d;
    bit led = 0;
    volatile u32 i;

    /* LED21(P4.0)：独立心跳，约1秒翻转一次。闪 = 主循环在跑；灭=卡死 */
    P4M1 &= ~0x01;  P4M0 |= 0x01;
    P40 = 1;

    P_SW2 |= 0x80;                  /* EAXFR */
    UART2_Init();
    UART3_Init();

    while(1)
    {
        /* LED 心跳：约1秒闪一次 = 固件在跑（不再周期发 0x55） */
        led = !led;
        P40 = (bit)led;
        for(i = 0; i < 700000; i++);   /* ~0.5s */

        /* 纯回显：收到什么回什么（发一个收一个） */
        if(UART2_GetByte(&d)) {
            UART2_Init();           /* 清接收后卡死的发送状态 */
            test_delay_ms(10);      /* RS485 半双工转向 */
            UART2_SendByte(d);      /* 回显 RS485-1 */
        }
        if(UART3_GetByte(&d)) {
            UART3_Init();
            test_delay_ms(10);
            UART3_SendByte(d);      /* 回显 RS485-2 */
        }
    }
}
#endif
