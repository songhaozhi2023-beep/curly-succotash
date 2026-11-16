#include "config.h"          // 系统配置：基本数据类型(u8/u16/u32)、系统时钟、引脚定义
#include "driversuart.h"    // UART串口驱动：用于Modbus-RTU通信
#include "ht1622.h"         // HT1622段码LCD驱动：控制LCD显示内容
#include "app_led.h"        // LED控制模块：LED21指示灯、20ms节拍器（不再切换NET60）
#include "appvalve.h"       // 双稳态脉冲阀控制：Valve_PulseOpen/Close/Update、脉搏管理
#include "appkey.h"         // 按键扫描模块：扫描4个按键(ADD/SUB/OK/RUN)并消抖
#include "param_config.h"   // 参数配置模块：管理系统参数的存储和读取
#include "menu_system.h"    // 菜单系统模块：管理LCD菜单界面、参数编辑、闪烁等
#include "control_logic.h"  // 控制逻辑模块：压差检查、周期检查、手动冲洗、报警等
#include "flush_control.h"  // 反冲洗控制模块：双稳态脉冲反冲洗状态机
#include "buzzer.h"         // 蜂鸣器驱动模块：控制蜂鸣器发声、报警
#include "system_init.h"    // 系统初始化模块：Timer0初始化(50Hz)、系统自检、启动提示音
#include "adc.h"            /* ADC驱动：压差传感器采集 */

// 主函数 - 程序从这里开始执行
void main(void)
{
    /* ====== 第零阶段：上电即时端口清零（必须在任何函数调用前执行） ====== */
    // ★ 关键：上电复位后所有端口输出锁存器=0xFF（全HIGH）
    //   P-ch脚(奇数位)=HIGH → NPN驱动管导通 → P-ch MOSFET导通 → 阀输出=NET59
    //   N-ch脚(偶数位)=HIGH → N-ch MOSFET导通 → 阀输出=GND
    //   结果：10路半桥全部同时直通（NET59→GND），总电流>20A → 电源限流 → MCU掉压跑飞
    //   解决方案：在升压EN拉高之前先把所有阀端口锁存器清零，将直通窗口从~1ms缩短到~1µs
    P0 = 0x00;                       // 清阀6-9端口锁存器（P0.0/P0.3/P0.4-P0.7 等）
    P4 = 0x00;                       // 清LED21(P4.0)+RS232-2(P4.5/P4.6)锁存器
    P6 = 0x00;                       // 清阀10+NET60端口锁存器（P6.0-P6.3）
    P7 = 0x00;                       // 清阀1-4端口锁存器（P7.0-P7.7）
    // ★ STC32G 的 MOVX 指令由 EAXFR 决定访问目标：
    //   EAXFR=1→扩展SFR(P8OUT/PBOUT等)，EAXFR=0→扩展RAM(xdata)。
    //   访问扩展端口前必须 EAXFR=1、访问后立即恢复 0；若永久置 1，所有 xdata 变量
    //   (g_param_config/g_menu_status/g_flush_control…) 的 MOVX 会错接到 XSFR，
    //   全局状态全毁 → 上电无任何动作。
    XSFR_ON();
    P8OUT = 0x00;                    // 清扩展P8：升压EN(P8.0)=0、RS485方向(P8.7/P8.6)=0
    P8OUT |= 0x02;                   // ★ P8.1(返回键)置1：整口清零把P8.1锁存也清了，准双向输入锁存必须=1
                                     //   才有上拉；否则引脚被强拉低=永远"按下"→Key_Scan串扰抑制杀掉RUN/ADD
    PBOUT = 0x00;                    // [4G修正] 清扩展PB：阀5/6/7锁存器(PB.7/PB.6/PB.5/PB.4)=0，防半桥直通
    XSFR_OFF();

    // ★ BOR恢复安全第一原则：P8.0拉低，升压关闭，防止准双向默认1使能升压
    // 否则BOR后复位→P8.0=HIGH→升压启动+端口0xFF→所有半桥直通→PSU限流→死循环
    BOOST_EN_LOW();                  // [4G移植] 升压EN在P8.0（基础版是P1.0）

    // ★ NET60安全：P6.2/P6.3配置推挽输出并拉低，防止Q15上电穿通
    P6M1 &= ~0x0C; P6M0 |= 0x0C;   // P6.2/P6.3 推挽输出
    P6 &= ~0x0C;                    // P6.2=P6.3=0 → NET60 Hi-Z（安全状态）

    // ★ 看门狗：统一设PS=6(~2.1s)。delay_ms 内部每ms喂狗，阻塞初始化无超时风险。
    WDT_CONTR = 0x36;               // CLR_WDT|EN_WDT|PS=6 → 喂狗+2.1s超时

    // 初始化顺序：HT1622→ADC→LED→Key→Valve→Buzzer→Param→Control→Menu→System
    HT1622_Init();        // 初始化HT1622段码LCD
    ADC_Init();           /* 初始化ADC（P1IE使能P1.7模拟输入 + 12位左对齐） */
    LED_Init();           // 初始化LED(P4.0)
    Key_Init();           // 初始化按键
    Valve_Init();         // 双稳态脉冲阀初始化
    Buzzer_Init();        // 初始化蜂鸣器(PA.0)
    Param_Init();         // 参数加载（IAP/EEPROM，IAP_TPS已设）
    Control_Init();       // 控制逻辑初始化
    Menu_Init();          // 菜单初始化
    System_Init();        // Timer0 + 蜂鸣器提示音
    System_SelfTest();    // 自检：蜂鸣器+LCD全屏

    /* ====== 第二阶段：配置Boost升压使能引脚 ====== */
    // ★ 必须在Valve_Init之后(阀门已初始化为安全稳态GND)才配置升压使能引脚
    // 防止升压15V加载到未初始化的阀门端口→半桥直通烧MOSFET
    // [4G移植] P8.0 控制 PW5300 升压芯片的使能(EN)引脚（基础版是 P1.0 控制 FP6291）
    XSFR_ON();
    P8M1 &= ~0x01;       // [4G移植] P8.0 的 M1位=0（P8 为扩展端口，EAXFR 由宏切换）
    P8M0 |= 0x01;        // P8.0 的 M0位=1 → 推挽输出模式
    XSFR_OFF();
    BOOST_EN_LOW();      // 升压关闭，直到首次脉冲请求

    /* ====== 第二阶段半：看门狗已在上方启用（WDT_CONTR=0x36），此处无需再改 ====== */

    /* ====== 第三阶段：主循环（超级循环） ====== */
    // 每次循环约20ms，由LED_Update内部延时控制
    while (1)
    {
        WDT_CONTR = 0x36;     // 喂狗：EN_WDT|CLR_WDT|PS=6（每圈~20ms喂一次）
        Key_Scan();           // 按键扫描
        /* [2026-08-08] 人为按键 → 亮屏/重置亮屏倒计时 */
        if(key_return || key_add || key_sub || key_ok || key_run || key_ok_held) {
            Menu_ScreenActivity();
        }
        Key_Process();        // 按键处理
        Menu_HandleKey();     // OK按键长按/短按
        Menu_ClockTick();     // 周期冲洗倒计时节拍
        Menu_BlinkTick();     // 闪烁节拍
        if (g_lcd_dirty) {    // LCD脏标记
            g_lcd_dirty = 0;
            Menu_Display();   // 绘制LCD
        }
        BootTest_Process();   // 上电关阀测试状态机
        Control_MainLoop();   // 控制逻辑主循环
        Valve_PulseUpdate();  // 脉冲管理
        LED_Update();         // LED + 20ms节拍
    }
}
