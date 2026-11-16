#ifndef __LED_H
#define __LED_H

#include "config.h"

/* LED控制模块 - 双稳态脉冲阀简化版
 *
 * LED21(P4.0): 电源/状态指示灯，支持常亮和闪烁
 * LED1~LED20: 阀门状态双色LED — 仅在阀门脉冲100ms期间由压差自然点亮（无需扫描）
 *   开阀脉冲(NET60=GND, 目标阀=HV): 开阀亮LED亮
 *   关阀脉冲(NET60=HV, 目标阀=GND): 关阀亮LED亮
 *   稳态(NET60=GND, 全部阀=GND): LED1~20全灭
 *
 * LED_Update: 纯20ms节拍器（LED21控制 + 空循环），不再切换NET60
 * LED_MiniRefresh: 空循环占位（从HT1622清屏调用，维持SPI+LCD时序）
 */

void LED_Init(void);
void LED_Update(void);
void LED_MiniRefresh(u8 cycles);

#endif
