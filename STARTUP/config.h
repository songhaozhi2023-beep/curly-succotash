#ifndef __CONFIG_H
#define __CONFIG_H

#include <STC32G144K246.H>

/*============================================================================
 * 过滤反冲洗 4G 版 — STC32G144K246 配置头
 *
 * 由基础版(STC15W4K56S4)移植而来。关键结论：
 *   - P0~P7 端口、PxM0/PxM1、PCON、Timer0、WDT、IAP、SCON/SBUF、ADC 的
 *     SFR 地址与 STC15 完全相同，代码 90% 可复用。
 *   - 工具链：**Keil C251**（Device = STC32G144K246-32Bit Series）——STC32G 是 251 内核，必须用 C251（不是 C51）。
 *   - 系统时钟：IRC 设 24MHz（与基础版相同，STC-ISP 烧录时选定）。
 *   - P8/P9/PA/PB 为「扩展端口」，C251 下用 far 指针访问（P8OUT@0x7ef700…）。
 *     访问扩展端口必须 EAXFR=1、访问 xdata 必须 EAXFR=0 —— 所有扩展端口访问
 *     统一走下方 XSFR_ON()/XSFR_OFF() 切换宏，绝不永久置 EAXFR=1（会毁 xdata）。
 *
 * 4G 板与基础版引脚差异（已按 .tel 网表 + 引脚表核实）：
 *   - 升压 EN：基础版 P1.0 → 4G 板 P8.0（PW5300，经 R38 100Ω，网标名"ADC0"）
 *   - 按键：基础版 P1.1~P1.5 → 4G 板 RETURN=P8.1 / ADD=PA.2 / SUB=PA.1 /
 *           OK=P1.4 / RUN=P1.5（仍是数字量、47K 上拉、低有效）
 *   - Modbus RS485 方向控制：基础版 P3.2 → 4G 板 P8.7
 *   - 阀/NET60/HT1622/蜂鸣器/LED21/传感器供电/压差ADC 仍在基础版相同端口
 *==========================================================================*/

/* ---------- 兼容宏：STC15 的 Px_y 命名 → STC32G 头文件的 Pxy 命名 ---------- */
/* STC32G 头文件用 P00~P77（无下划线），基础版用 P0_0~P7_7，这里做全量映射 */
#define P0_0 P00
#define P0_1 P01
#define P0_2 P02
#define P0_3 P03
#define P0_4 P04
#define P0_5 P05
#define P0_6 P06
#define P0_7 P07

#define P1_0 P10
#define P1_1 P11
#define P1_2 P12
#define P1_3 P13
#define P1_4 P14
#define P1_5 P15
#define P1_6 P16
#define P1_7 P17

#define P2_0 P20
#define P2_1 P21
#define P2_2 P22
#define P2_3 P23
#define P2_4 P24
#define P2_5 P25
#define P2_6 P26
#define P2_7 P27

#define P3_0 P30
#define P3_1 P31
#define P3_2 P32
#define P3_3 P33
#define P3_4 P34
#define P3_5 P35
#define P3_6 P36
#define P3_7 P37

#define P4_0 P40
#define P4_1 P41
#define P4_2 P42
#define P4_3 P43
#define P4_4 P44
#define P4_5 P45
#define P4_6 P46
#define P4_7 P47

#define P5_0 P50
#define P5_1 P51
#define P5_2 P52
#define P5_3 P53
#define P5_4 P54
#define P5_5 P55
#define P5_6 P56
#define P5_7 P57

#define P6_0 P60
#define P6_1 P61
#define P6_2 P62
#define P6_3 P63
#define P6_4 P64
#define P6_5 P65
#define P6_6 P66
#define P6_7 P67

#define P7_0 P70
#define P7_1 P71
#define P7_2 P72
#define P7_3 P73
#define P7_4 P74
#define P7_5 P75
#define P7_6 P76
#define P7_7 P77

/* ---------- 扩展端口操作宏（P8/PA 无 sbit，用掩码） ---------- */

/* ★ EAXFR 切换：STC32G 的 MOVX 靠 EAXFR 区分访问目标——
 *   EAXFR=1 → 扩展SFR(P8OUT/PBOUT等)；EAXFR=0 → 扩展RAM(xdata)。
 *   访问扩展端口前必须 EAXFR=1，访问 xdata 前必须 EAXFR=0。
 *   所有扩展端口操作宏都已内置切换，外部无需再管。 */
#define XSFR_ON()   (P_SW2 |= 0x80)
#define XSFR_OFF()  (P_SW2 &= ~0x80)

/* 升压使能 EN = P8.0（PW5300 的 EN，高电平使能升压到 ~15V 供阀脉冲）
 * 基础版是 P1.0；4G 板原理图网标沿用旧名"ADC0"，实为升压 EN 接 P8.0 */
#define BOOST_EN_HIGH()  do { XSFR_ON(); P8OUT |= 0x01; XSFR_OFF(); } while(0)
#define BOOST_EN_LOW()   do { XSFR_ON(); P8OUT &= ~0x01; XSFR_OFF(); } while(0)

/* Modbus RS485-1 方向控制 DE/RE = P8.7（基础版是 P3.2）
 * TX=发(高)，RX=收(低) */
#define RS485_DIR_TX()   do { XSFR_ON(); P8OUT |= 0x80; XSFR_OFF(); } while(0)
#define RS485_DIR_RX()   do { XSFR_ON(); P8OUT &= ~0x80; XSFR_OFF(); } while(0)

/* RS485-2 方向控制 DE/RE = P8.6（扩展用） */
#define RS485_2_DIR_TX() do { XSFR_ON(); P8OUT |= 0x40; XSFR_OFF(); } while(0)
#define RS485_2_DIR_RX() do { XSFR_ON(); P8OUT &= ~0x40; XSFR_OFF(); } while(0)

/* ---------- 基本数据类型 ---------- */
typedef unsigned char  u8;
typedef unsigned int   u16;
typedef unsigned long  u32;

/* ---------- 系统时钟与常量 ---------- */
#define SYSCLK                24000000L

#define VALVE_MIN_NUM          1
#define VALVE_MAX_NUM         10
#define MODBUS_ADDR            1
#define MODBUS_REG_NUM        16
#define MODBUS_FRAME_TIMEOUT   10

/* 每秒主循环次数 (Timer0 硬件定时器 @50Hz，每 20ms 递增) */
#define TICK_PER_SEC          50

/* 系统硬件时钟节拍 (Timer0 ISR @50Hz) */
extern volatile u16 g_sys_tick;

#endif
