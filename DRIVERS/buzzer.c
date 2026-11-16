#include "buzzer.h"         // 包含蜂鸣器驱动头文件，定义蜂鸣器控制接口
#include "config.h"         // XSFR_ON/OFF 宏
#include "delay.h"          // 包含延时函数头文件，提供精确延时功能

// [2026-08-18 修正] 蜂鸣器实际接在 **PA.0**（原理图误标为 P5.5——P5.5 是基础版端口名）。
// PA 为 STC32G 扩展端口，需 EAXFR=1 访问 PAOUT/PAM0/PAM1。
#define BUZZER_ON()   do { XSFR_ON(); PAOUT |= 0x01; XSFR_OFF(); } while(0)
#define BUZZER_OFF()  do { XSFR_ON(); PAOUT &= ~0x01; XSFR_OFF(); } while(0)

// 蜂鸣器状态变量 - 全局变量记录蜂鸣器当前工作状态
BuzzerState_t xdata g_buzzer_state = BUZZER_STATE_OFF;

// 蜂鸣器初始化 - 将 PA.0 设置为推挽输出模式并初始化状态
void Buzzer_Init(void)
{
    XSFR_ON();
    PAM1 &= ~0x01;          // PA.0 M1=0
    PAM0 |= 0x01;           // PA.0 M0=1 → 推挽输出
    XSFR_OFF();
    BUZZER_OFF();
    g_buzzer_state = BUZZER_STATE_OFF;
}

// 蜂鸣器开启 - PA.0 输出高电平
void Buzzer_On(void)
{
    BUZZER_ON();
    g_buzzer_state = BUZZER_STATE_ON;
}

// 关闭蜂鸣器 - PA.0 输出低电平
void Buzzer_Off(void)
{
    BUZZER_OFF();
    g_buzzer_state = BUZZER_STATE_OFF;
}

// 蜂鸣指定时间（毫秒） - 直流驱动（主动式蜂鸣器，与基础版一致）
void Buzzer_Beep(u16 ms)
{
    Buzzer_On();            // 开启蜂鸣器（PA.0=1）
    delay_ms(ms);           // 持续指定时间
    Buzzer_Off();           // 关闭蜂鸣器
}

// 停止报警 - 关闭蜂鸣器并恢复为普通关闭状态
void Buzzer_StopAlarm(void)
{
    Buzzer_Off();
    g_buzzer_state = BUZZER_STATE_OFF;
}
