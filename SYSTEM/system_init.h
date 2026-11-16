#ifndef __SYSTEM_INIT_H   // 头文件保护宏，防止重复包含
#define __SYSTEM_INIT_H   // 定义头文件标识符

#include "config.h"       // 包含系统配置头文件，定义基本数据类型和系统配置

/* 系统初始化模块头文件
 * 负责系统启动时的硬件初始化：
 *   1. Timer0初始化：产生50Hz系统节拍(g_sys_tick)
 *   2. 系统启动提示音：两次短促蜂鸣
 *   3. 系统自检：蜂鸣器→LCD全屏→完成提示音
 *=============================================================*/

// 系统硬件时钟节拍 (Timer0 ISR @50Hz, 每20ms递增)
extern volatile u16 g_sys_tick;  // 全局变量：系统时钟计数器，所有超时判断基于此变量

// 原子读取g_sys_tick（双读一致法）
// 8051读u16需两条字节指令，ISR可能在中间递增导致撕裂读（误差±256 tick）
// 撕裂读会把脉冲截短到0~20ms（阀不动作）、或造成长按误判——所有tick读取应经此函数
u16 Sys_GetTick(void);

// 系统初始化函数 - 启动Timer0并发出两次启动提示音
void System_Init(void);

// Timer0硬件定时器初始化 - 配置为12T模式，50Hz系统节拍
void Timer0_Init(void);

// 系统自检函数 - 蜂鸣器测试→LCD全屏闪亮→完成提示音
bit System_SelfTest(void);      // 返回值：1=自检成功

#endif                  // 头文件结束标识
