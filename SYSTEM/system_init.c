#include "system_init.h"    // 包含系统初始化头文件，定义系统初始化接口
#include "buzzer.h"         // 包含蜂鸣器驱动头文件，用于系统提示音
#include "ht1622.h"         // 包含HT1622 LCD驱动头文件，用于显示测试
#include "delay.h"          // 包含延时函数头文件，提供精确延时功能
#include "config.h"
#include "appvalve.h"       // 脉冲超时保险：Valve_PulseFailsafeISR（仅由Timer0 ISR调用）

// 系统硬件时钟节拍 (由Timer0 ISR每20ms递增一次)
volatile u16 g_sys_tick = 0;

// Timer0中断服务: 50Hz系统节拍
// [4G移植] 24MHz 12T: 24000000/12=2MHz, 65536-40000=25536=0x63C0（与基础版相同）
void Timer0_ISR(void) interrupt 1
{
    TL0 = 0xC0;               // 先设TL0减少时序偏差
    TH0 = 0x63;
    g_sys_tick++;
    Valve_PulseFailsafeISR(); // 脉冲超时保险：主循环卡死>300ms时强制NET60高阻断电（防线圈持续通电）
}

// 原子读取g_sys_tick：连续读两次直到一致，避免ISR在两字节读取之间递增造成撕裂
u16 Sys_GetTick(void)
{
    u16 t;
    do { t = g_sys_tick; } while(t != g_sys_tick);
    return t;
}

void Timer0_Init(void)
{
    // Timer0: 模式1 (16-bit), 12T, ~20ms溢出 = 50Hz
    // [4G移植] 24MHz 12T: 65536 - (24000000/12/50) = 65536 - 40000 = 25536 = 0x63C0
    AUXR &= ~0x80;            // T0x12=0: 12T模式
    TMOD &= ~0x03;            // 清除Timer0模式位
    TMOD |= 0x01;             // Timer0 模式1 (16-bit)
    TL0 = 0xC0;
    TH0 = 0x63;
    ET0 = 1;                  // 使能Timer0中断
    EA  = 1;                  // 使能全局中断
    TR0 = 1;                  // 启动Timer0
}

// 系统初始化 - 执行系统启动时的基本初始化操作
void System_Init(void)      // 系统初始化函数定义
{
    // ★ 看门狗策略（2026-07-25修订）：
    //   main() 第一行已确保 PS=6(~2.1s)，delay_ms() 内部每1ms喂狗
    //   因此阻塞初始化不再有超时风险。此处仅喂狗一次保持活性
    WDT_CONTR |= 0x10;      // CLR_WDT=1：喂狗（未启动时为无害空操作）

    // 启动硬件定时器驱动系统节拍（Buzzer_Beep依赖delay_ms，不受影响）
    Timer0_Init();

    // 系统启动提示音 - 通过蜂鸣器发出两次短促提示音表示系统启动
    Buzzer_Beep(100);       // 蜂鸣器嗡鸣100毫秒
    delay_ms(50);           // 延时50毫秒
    Buzzer_Beep(100);       // 蜂鸣器再次嗡鸣100毫秒
}

// 系统自检 - 执行系统硬件自检程序
bit System_SelfTest(void)   // 系统自检函数定义，返回自检结果
{
    // 执行系统自检 - 测试各个硬件模块是否正常工作
    // 1. 检查蜂鸣器 - 测试蜂鸣器是否能正常发声
    Buzzer_Beep(200);       // 蜂鸣器嗡鸣200毫秒
    delay_ms(250);          // 延时250毫秒，确保蜂鸣器测试完成

    // 2. 检查LCD显示 - 全屏闪亮一次后清除（段码LCD无法任意显示数字）
    HT1622_AllOn();           // 全屏点亮，验证所有段码均能发光
    delay_ms(1000);           // 延时1000毫秒
    HT1622_Clear();           // 清除LCD显示内容

    // 3. 自检完成提示音 - 发出两声短促蜂鸣表示自检完成
    Buzzer_Beep(150);       // 蜂鸣器嗡鸣150毫秒
    delay_ms(50);           // 延时50毫秒
    Buzzer_Beep(150);       // 蜂鸣器再次嗡鸣150毫秒

    return 1;               // 返回自检成功状态（1表示成功）
}
