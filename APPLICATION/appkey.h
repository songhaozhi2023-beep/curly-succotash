#ifndef __KEY_H          // 头文件保护宏，防止重复包含
#define __KEY_H          // 定义头文件标识符

/*********************************************************
*  按键驱动头文件 - 定义按键驱动相关接口和功能
**********************************************************/

#include "config.h"      // 包含系统配置头文件，定义基本数据类型和系统配置

/* [4G移植] 按键引脚（数字量、低有效，47K 上拉 + 100nF 消抖 + ESD）：
 *   返回键 SW1: P1.1 → P8.1（扩展端口，无 sbit）
 *   ADD    SW2: P1.2 → PA.2（扩展端口）
 *   SUB    SW3: P1.3 → PA.1（扩展端口）
 *   OK     SW4: P1.4（不变）
 *   RUN    SW5: P1.5（不变）
 * 读宏返回 0=按下、非0=松开。P8IN/PAIN 为扩展端口寄存器(far 0x7ef708/0x7ef70a)，
 * 读取必须 EAXFR=1，统一走 Key_ReadExt() 函数内部切换（读后恢复 0，防止 xdata 损坏） */
u8  Key_ReadExt(void);                      // 读扩展口按键原始位：bit0=P8.1, bit1=PA.1, bit2=PA.2
#define KEY_RETURN   (Key_ReadExt() & 0x01) // 返回键 P8.1（物理最左）
#define KEY_ADD      (Key_ReadExt() & 0x04) // ADD(+) PA.2
#define KEY_SUB      (Key_ReadExt() & 0x02) // SUB(-) PA.1
sbit KEY_OK_PIN  = P1^4;            // OK(设置确认) P1.4
sbit KEY_RUN_PIN = P1^5;            // RUN(手动冲洗) P1.5

// 全局按键标志 - 由Key_Scan()维护，其他模块只读
extern bit key_return;   // 外部声明：返回键标志（单次脉冲，首次按下）
extern bit key_add;      // 外部声明：ADD按键状态标志
extern bit key_sub;      // 外部声明：SUB按键状态标志
extern bit key_ok;       // 外部声明：OK按键状态标志（单次脉冲，首次按下）
extern bit key_ok_held;  // 外部声明：OK按键持续按下标志（消抖后，按住不放时持续为1）
extern bit key_run;      // 外部声明：RUN按键状态标志

// 按键初始化 - 初始化按键相关的GPIO端口
void Key_Init(void);     // 函数声明：按键初始化函数

// 按键扫描（电平检测） - 扫描按键状态并进行消抖处理
void Key_Scan(void);     // 函数声明：按键扫描函数

// 按键业务处理 - 根据按键状态执行相应的业务逻辑
void Key_Process(void);  // 函数声明：按键业务处理函数

#endif                  // 头文件结束标识
