#ifndef __HT1622_H        // 头文件保护宏，防止重复包含
#define __HT1622_H        // 定义头文件标识符

/*********************************************************
*  HT1622 段码LCD驱动头文件 - 定义HT1622 LCD控制器驱动接口
**********************************************************/

#include "config.h"       // 包含系统配置头文件，定义基本数据类型和系统配置

// HT1622命令定义 — HT1622 使用 12 位命令帧：ID(100) + 8-bit Command + 1 位 Don't Care
// 注意：HT1622 固定 1/4 偏压 1/8 占空比，不支持偏压/占空比配置命令
#define HT1622_SYS_DIS     0x00  // 关闭系统振荡器和LCD偏压发生器（上电后需先发送以重置状态）
#define HT1622_SYS_EN      0x01  // 启动系统振荡器（必须比 LCD_ON 先发送）
#define HT1622_LCD_OFF     0x02  // 关闭LCD显示（初始化序列中在SYS_EN之后发送）
#define HT1622_LCD_ON      0x03  // 开启LCD显示（最后一步，振荡器已稳定后）
#define HT1622_RC_256K     0x18  // 选择内部 256kHz RC 振荡器作为系统时钟
#define HT1622_NORMAL      0xE3  // 退出 TEST 模式进入正常工作模式（关键！否则 COM/SEG 无输出）
#define HT1622_TIMER_DIS   0x04  // 关闭时基输出
#define HT1622_WDT_DIS     0x08  // 关闭看门狗

// 显示地址定义 - 定义HT1622 LCD控制器的显示RAM地址
#define HT1622_ADDR_VALVE      0    // 宏定义：阀号显示地址，用于显示当前阀号
#define HT1622_ADDR_STATE      1    // 宏定义：状态显示地址，用于显示运行状态
#define HT1622_ADDR_DIGIT1     2    // 宏定义：第一个数字显示地址，用于显示数字1
#define HT1622_ADDR_DIGIT2     3    // 宏定义：第二个数字显示地址，用于显示数字2

// 显示状态 - 定义不同状态的显示代码
#define HT1622_STATE_RUN       0x01  // 宏定义：运行状态代码，显示RUN状态
#define HT1622_STATE_STOP      0x02  // 宏定义：停止状态代码，显示STOP状态

// 驱动接口函数 - 定义HT1622 LCD驱动相关函数接口
void HT1622_Init(void);             // 函数声明：HT1622初始化函数
void HT1622_WriteCmd(u8 cmd);       // 函数声明：HT1622写命令函数，参数为命令值
void HT1622_WriteData(u8 addr, u8 dat); // 函数声明：HT1622写数据函数，参数为地址和数据
void HT1622_Clear(void);            // 函数声明：HT1622清屏函数
void HT1622_AllOn(void);            // 函数声明：HT1622全屏点亮函数(调试用)

#endif                 // 头文件结束标识