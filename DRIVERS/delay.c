#include "delay.h"         // 包含延时函数头文件，定义延时函数接口

// 微秒延时函数 @24MHz - 用计数空循环替代 volatile while(--) 避免优化等级8下时序漂移
// [4G移植] IRC 定为 24MHz，与基础版一致（5 次内循环 ≈ 1µs）
// 校准基准：delay_ms 中 4800 次内循环 ≈ 1ms → 5 次 ≈ 1µs
void delay_us(u16 us)      // 微秒延时函数定义
{
    volatile u16 j;        // volatile 防止编译器消除循环
    while(us--)            // 外层循环us次
    {
        for(j = 0; j < 5; j++); // 5次内循环 ≈ 1µs (24MHz 1T)
    }
}

// 毫秒延时函数 - 毫秒级延时函数，基于内部循环实现
void delay_ms(u16 ms)      // 毫秒延时函数定义
{
    u16 i;                 // 定义循环计数器
    volatile u16 j;        // volatile防止编译器优化掉延时循环
    for(i = 0; i < ms; i++)      // 外层循环ms次，每次循环约1毫秒
    {
        WDT_CONTR |= 0x10;       // 每ms喂一次狗，兼容ISP"看门狗自动启动"的任何分频
        for(j = 0; j < 4800; j++); // 内层循环: 4800*5cycles≈24000cycles≈1ms (24MHz 1T 8051)
    }
}