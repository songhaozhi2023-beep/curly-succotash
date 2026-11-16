#include "app_led.h"
#include "config.h"
#include "delay.h"
#include <intrins.h>       /* _nop_()：空闲指令唤醒后的 NOP 序列 [2026-08-08] */
#include "menu_system.h"   /* g_screen_off：LED21 随屏态 [2026-08-08] */

// ====== LED21 ======
sbit LED_STATUS = P4^0;

// ====== 注意: NET60 由 appvalve.c 独占控制，LED 模块不再操作 P6.2/P6.3 ======

void LED_Init(void)
{
    // ★ P1.0(升压使能)现在由 main() 在 Valve_Init 之后统一配置
    // 此处不再操作 P1.0 — 防止升压在阀门端口未初始化时就给 NET59 充电

    // LED21
    P4M1 &= ~0x01;
    P4M0 |= 0x01;
    LED_STATUS = 1;

    // P6.2/P6.3 由 Valve_Init 配置（推挽输出），LED_Init 不再重复配置
}

void LED_Update(void)
{
    /* LED21 随屏态：亮屏=亮，灭屏=灭（省电）[2026-08-08] */
    if(g_screen_off) { LED_STATUS = 0; } else { LED_STATUS = 1; }

    /* 主循环节拍：空闲睡眠 [2026-08-08]
     * CPU 停、24MHz 时钟/Timer0/ADC 继续；Timer0 每 20ms 中断唤醒。
     * 看门狗：IDL_WDT=0 → 空闲时 WDT 暂停；CPU 每 20ms 醒来喂狗，无超时风险。 */
    EA = 1;               /* 防御：确保空闲前开中断，否则 EA=0 时睡眠将无唤醒源且WDT暂停→死锁 */
    PCON |= 0x01;         /* IDLE：进入空闲模式 */
    _nop_(); _nop_();     /* 唤醒后从 NOP 继续（数据手册要求） */
    _nop_(); _nop_();
    /* 唤醒：Timer0 ISR 已跑（g_sys_tick++ / Valve_PulseFailsafeISR），此处返回主循环 */
}

void LED_MiniRefresh(u8 cycles)
{
    u8 i;
    volatile u16 j;

    // 占位: menu_system.c 的 HT1622_Clear 调用此函数维持时序
    // 双稳态模式下无须NET60切换，仅空循环
    for(i = 0; i < cycles; i++) {
        for(j = 0; j < 1010; j++);   // ~210µs
    }
}
