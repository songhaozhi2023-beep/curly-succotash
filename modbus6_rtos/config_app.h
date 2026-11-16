#ifndef __CONFIG_APP_H
#define __CONFIG_APP_H

/*=============================================================
 * 流量压力检测集成装置 · 双角色 Modbus-RTU (FreeRTOS 版)
 * 芯片: STC32G144K246  编译: Keil C251 (照抄 FreeRTOS demo 配置)
 * 串口分配: UART5→流量计总线(1/2/3) UART6→压力计总线(1/2/3)
 *           UART7→备用  UART8→大彩屏(本机从站,地址1)  全部9600 8N1
 * 类型 u8/u16/u32 由 type_def.h 提供, 此处不重复定义
 *=============================================================*/

#define SYSCLK        24000000L
#define BAUD_SENS     9600L
#define BAUD_SCREEN   9600L
#define BAUD_REL      (u16)(65536UL - SYSCLK / 4UL / 9600L)

/* 改地址模式: 1=固件改传感器地址 0=正常轮询 (改完记得改回0) */
#define ADDR_MODE_ENABLE   0
#define ADDR_SENSOR_TYPE   0      /* 0=流量计 1=压力计 */
#define ADDR_TARGET        1      /* 目标地址 1~252 */

/*================ 扩展 SFR 访问 ================*/
/* K246 的串口5~8/Timer5~8/P8~PF 为 XFR 区寄存器(far 空间 0x7EF7xx),
 * 须用 C251 far 指针访问; EAXFR(P_SW2.7) 保持常开 */
#define XSFR_ON()    (P_SW2 |= 0x80)
#define XSFR_OFF()   (P_SW2 &= ~0x80)
#define XREG(a)      (*(volatile u8 far *)(a))

/*=============================================================
 * ✅ 核对区 —— 已按官方 STC32G144K246.H 核对(2026-11-14)
 * 来源: STC-official-usys-UART-ISP-bootloader-demo 包内
 *       isp\src\stc32g144k246.h, 全部为 K246 far 空间真实地址
 * 注: K246 定时器模式寄存器官方名为 T6T5M/T8T7M(高半字节在前),
 *     位序与下方 T5T6M/T7T8M 宏的用法兼容, 上层代码零改动
 *=============================================================*/
#define S5CON    XREG(0x7ef740)
#define S5BUF    XREG(0x7ef741)
#define S6CON    XREG(0x7ef770)
#define S6BUF    XREG(0x7ef771)
#define S7CON    XREG(0x7ef7a0)
#define S7BUF    XREG(0x7ef7a1)
#define S8CON    XREG(0x7ef7d0)
#define S8BUF    XREG(0x7ef7d1)
#define S5CFG    XREG(0x7ef76d)   /* 波特率时钟源选择(默认值待真机确认) */
#define S6CFG    XREG(0x7ef79d)
#define S7CFG    XREG(0x7ef7cd)
#define S8CFG    XREG(0x7ef7fd)
#define T5T6M    XREG(0x7efbb3)   /* K246 官方名 T6T5M: T6高/T5低 */
#define T5L      XREG(0x7efbb7)
#define T5H      XREG(0x7efbb6)
#define T6L      XREG(0x7efbb5)
#define T6H      XREG(0x7efbb4)
#define T7T8M    XREG(0x7efbbb)   /* K246 官方名 T8T7M: T8高/T7低 */
#define T7L      XREG(0x7efbbf)
#define T7H      XREG(0x7efbbe)
#define T8L      XREG(0x7efbbd)
#define T8H      XREG(0x7efbbc)
#define PAOUT    XREG(0x7ef702)   /* P10 输出锁存 */
#define PAM0     XREG(0x7ef712)   /* P10 模式0 */
#define PAM1     XREG(0x7ef71a)   /* P10 模式1 */
#define PBOUT    XREG(0x7ef703)   /* P11 输出锁存 */
#define PBM0     XREG(0x7ef713)   /* P11 模式0 */
#define PBM1     XREG(0x7ef71b)   /* P11 模式1 */

/* 串口5~8 控制位 (与 S3CON 同构: bit6=波特率源 bit4=REN) */
#define SX_ST3   0x40
#define SX_REN   0x10
#define SX_RI    0x01
#define SX_TI    0x02
/* 定时器: bit1=Tx12(1T) bit3=TxR(启动) */
#define TX_1T    0x02
#define TX_RUN   0x08

/*================ 485 方向 (DE/RE 短接, 单脚控制) ================*/
#define DE_FLOW_TX()   (P16 = 1)
#define DE_FLOW_RX()   (P16 = 0)
#define DE_PRESS_TX()  (P23 = 1)
#define DE_PRESS_RX()  (P23 = 0)
#define DE_SPARE_TX()  (P65 = 1)
#define DE_SPARE_RX()  (P65 = 0)
#define DE_SCREEN_TX()  do{ PAOUT |= 0x80; }while(0)   /* P10.7, EAXFR 已常开 */
#define DE_SCREEN_RX()  do{ PAOUT &= (u8)~0x80; }while(0)

/*================ Modbus 常量 ================*/
#define MB_FUNC_READ_HOLD   0x03
#define MB_FUNC_WRITE_ONE   0x06
#define MB_FUNC_WRITE_MULTI 0x10
#define MB_ADDR_SCREEN      1
#define MB_RXBUF_SIZE       64
#define MB_RESP_TIMEOUT     200UL    /* 主站等响应 ms */
#define MB_RETRY            2

#define FLOW_ADDR_BASE      1
#define PRESS_ADDR_BASE     1

/*================ Timer3 采样节拍 (10ms @ 24MHz, 12T 模式) ================*/
#define T3_RELOAD_10MS      45536UL    /* 65536 - 24000000/12*0.01 = 45536 */
/* TMR3_VECTOR=19 由芯片头文件定义 (AUXINTIF^1=T3IF) */

/*================ 浮点系数 (到货后按实测修正) ================*/
#define FLOW_INST_K         0.001f     /* 瞬时流量: raw * 0.001 = m³/h */
#define FLOW_TOTAL_K        0.001f     /* 累计流量: raw * 0.001 = m³ */
#define PRESS_K             0.001f     /* 压力:   raw * 0.001 (单位待确认) */

/*================ 采集控制命令 (xCmdQueue 载荷) ================*/
#define CMD_FLOW_START      1
#define CMD_FLOW_STOP       2
#define CMD_PRESS_START     3
#define CMD_PRESS_STOP      4

#define REG_PRESS_VALUE     0x0000
#define REG_FLOW_INST       0x0030
#define REG_FLOW_TOTAL      0x0031
#define REG_DEV_ADDR        0x0100   /* 改地址寄存器, 流量计侧需核对 */

/*================ 数据与接口 ================*/
typedef struct {
    float flow_inst;        /* 瞬时流量 m³/h */
    float flow_total;       /* 累计流量 m³ */
    float press;            /* 压力 (单位待确认) */
    u8    online;           /* 1=在线 */
} sensor_t;

extern sensor_t xdata g_sen[6];         /* [0~2]=流量计1~3 [3~5]=压力计1~3 */
extern u8 xdata g_collect_flow;         /* 1=流量采集运行中 */
extern u8 xdata g_collect_press;        /* 1=压力采集运行中 */

/* RTOS 三件套定义在 main.c, extern 声明放在各 .c 文件 include FreeRTOS 头之后 */

/*=============================================================
 * 大彩屏寄存器映射表 (屏侧 VisualHMI 配置参考)
 * 本机 Modbus 从站地址 = MB_ADDR_SCREEN (1)
 * 数据类型: FLOAT=4字节大端(占2个寄存器)  U16=2字节
 *
 *  【读寄存器 屏←MCU】
 *  地址      内容                     类型    说明
 *  0x0000~1  流量计1 瞬时流量         FLOAT   m³/h
 *  0x0002~3  流量计2 瞬时流量         FLOAT   m³/h
 *  0x0004~5  流量计3 瞬时流量         FLOAT   m³/h
 *  0x0006~7  压力计1                  FLOAT   (单位待确认)
 *  0x0008~9  压力计2                  FLOAT   (单位待确认)
 *  0x000A~B  压力计3                  FLOAT   (单位待确认)
 *  0x000C    在线位图                 U16     bit0~5 对应传感器0~5, 1=在线
 *  0x0010~1  流量计1 累计流量         FLOAT   m³
 *  0x0012~3  流量计2 累计流量         FLOAT   m³
 *  0x0014~5  流量计3 累计流量         FLOAT   m³
 *  0x0016    流量采集运行状态         U16     1=采集中 0=已停止
 *  0x0017    压力采集运行状态         U16     1=采集中 0=已停止
 *
 *  【写寄存器 屏→MCU】
 *  地址      内容                     写入值
 *  0x0018    流量采集开关             1=启动, 0=停止
 *  0x0019    压力采集开关             1=启动, 0=停止
 *
 *  【异常码】
 *  0x01  不支持的功能码
 *  0x02  非法寄存器地址
 *  0x03  非法数据(数量/长度)
 *=============================================================*/

void Sensor_Init(void);
void Sensor_Poll(void);
void Timer3_Init(void);                 /* 10ms 节拍中断, 给出 xSampleSem */
void Screen_Init(void);
void Screen_Service(void);
#if ADDR_MODE_ENABLE
void AddrMode_Run(void);
#endif
u16 mb_crc16(const u8 *p, u8 len);

#endif