#include "menu_system.h"    // 菜单系统头文件：提供MenuStatus_t、g_lcd_dirty等声明
#include "param_config.h"    // 参数配置：提供g_param_config、ParamType_t等
#include "ht1622.h"         // HT1622 LCD驱动：提供HT1622_WriteData等函数
#include "appkey.h"         // 按键模块：提供key_add/key_sub/key_ok等标志
#include "appvalve.h"       // 阀门控制：提供Valve相关函数
#include "flush_control.h"  // 反冲洗控制：提供Flush_IsActive等函数
#include "delay.h"          // 延时函数
#include "app_led.h"        // LED控制：提供LED_MiniRefresh函数
#include "control_logic.h"  // 控制逻辑：提供g_actual_pressure等
#include "system_init.h"    // 系统初始化：提供g_sys_tick
#include "adc.h"            /* ADC驱动：提供ADC_CH7常量 */

#define OK_LONG_PRESS_TICKS  (TICK_PER_SEC * 2)   // OK长按阈值：2秒（100个tick）

/*=============================================================
 * LCD段码编码表
 * 
 * Pattern A (PA)：用于188:88:88时钟显示和F8.8秒显示
 *   ABFG地址：a=bit3, b=bit2, f=bit1, g=bit0
 *   CDE地址： c=bit3, e=bit2, d=bit1, sp=bit0（sp=特殊标签位）
 * 
 * Pattern B (PB)：用于8.8(左)、8.8(中)、8888(右)显示
 *   ABFG地址：a=bit0, b=bit1, f=bit2, g=bit3（注意：与Pattern A的bit顺序相反）
 *   CDE地址： c=bit0, e=bit1, d=bit2, sp=bit3
 * 
 * code关键字：将数组存储在Flash中（C51的ROM存储），节省RAM
 *=============================================================*/
static const u8 code PA_ABFG[10] = {0x0E,0x04,0x0D,0x0D,0x07,0x0B,0x0B,0x0C,0x0F,0x0F}; // Pattern A ABFG编码：数字0~9
static const u8 code PA_CDE[10]  = {0x0E,0x08,0x06,0x0A,0x08,0x0A,0x0E,0x08,0x0E,0x0A}; // Pattern A CDE编码：数字0~9

static const u8 code PB_ABFG[10] = {0x07,0x02,0x0B,0x0B,0x0E,0x0D,0x0D,0x03,0x0F,0x0F}; // Pattern B ABFG编码：数字0~9
static const u8 code PB_CDE[10]  = {0x07,0x01,0x06,0x05,0x01,0x05,0x07,0x01,0x07,0x05}; // Pattern B CDE编码：数字0~9

/*=============================================================
 * 全局/静态变量
 *=============================================================*/
static u8 xdata clr_i;          // LCD_ClearAll循环变量（必须xdata，C51 overlay机制要求）
bit g_lcd_dirty = 1;            // LCD脏标记：1=需要刷新，0=不需要。初始化为1，启动时刷新一次
bit f_select_choice = 1;        // F-SELECT选择：1=YES，0=NO
bit g_screen_off = 0;                        /* 灭屏标志 [2026-08-08] */
#define SCREEN_ON_TICKS  (TICK_PER_SEC * 30) /* 亮屏时长30秒（真实时间，基于g_sys_tick） */
static u16 screen_off_base = 0;              /* 亮屏起点tick：距上次操作≥SCREEN_ON_TICKS则灭屏 */

static u8  clock_h = 0, clock_m = 0, clock_s = 0; // 周期冲洗倒计时显示值(时/分/秒) [2026-08-03]
static u8  blink_timer = 0;     // Level1闪烁计数器：每~0.3秒翻转一次
static bit blink_on = 1;        // Level1闪烁状态：1=显示，0=隐藏
static u16 flush_cycle_min = 0; // 冲洗周期分钟数（用于冲洗周期编辑时的显示）
static u8  count_page = 0;      // 冲洗计数页索引：0=压差, 1=周期, 2=手动 [2026-08-06]

/*=============================================================
 * Level 2 参数导航
 * 9个参数的导航顺序：主阀选择→主阀延时→间隔时间→压差延时→
 *   循环限制→报警输出→零点校准→站数识别→单位选择
 * [2026-08-03] 移除PARAM_CLOCK_SET（右上角为周期冲洗倒计时，无壁钟可设置）
 *=============================================================*/
static const ParamType_t code level2_params[9] = {
    PARAM_MAIN_VALVE_SELECT,
    PARAM_MAIN_VALVE_DELAY,
    PARAM_INTERVAL_TIME,
    PARAM_PRESSURE_DELAY,
    PARAM_CYCLE_LIMIT,
    PARAM_ALARM_OUTPUT,
    PARAM_ZERO_CALIBRATION,
    PARAM_STATION_RECOGNITION,
    PARAM_UNIT_SELECTION,
};
static u8  l2_blink_timer = 0;   // Level2闪烁计数器（独立于Level1）
static bit l2_blink_on = 1;      // Level2闪烁状态

/*=============================================================
 * 底层LCD写入函数
 * 
 * LCD地址映射（38个有效地址）：
 *   addr 0-13: 188:88:88时钟区域（Pattern A）
 *   addr 16-19: F8.8秒区域（Pattern A）
 *   addr 20-25: 8.8(左)区域（Pattern B）
 *   addr 26-29: 8.8(中)区域（Pattern B）
 *   addr 30-37: 8888(右)区域（Pattern B）
 * 
 * 每个数字占2个HT1622地址：ABFG地址 + CDE地址
 * sp参数控制特殊标签位（冒号、F、秒等）
 *=============================================================*/

// LCD_ClearAll - 清零全部38个LCD显示地址
// 每写2个地址调用一次LED_MiniRefresh(1)维持NET60切换（防止LED1~20闪烁）
static void LCD_ClearAll(void) {
    for(clr_i=0;clr_i<38;clr_i++){
        HT1622_WriteData(clr_i,0);
        if(clr_i&1)LED_MiniRefresh(1);
    }
}

// LCD_Write188 - 在188:88:88时钟区域写入一个数字（Pattern A）
// 参数：pos=位置(1~6，1=秒个位，6=时十位)，digit=数字(0~9)，sp=是否显示冒号标签
static void LCD_Write188(u8 pos,u8 digit,bit sp) {
    u8 a_abfg,a_cde,s_mask;  // ABFG地址、CDE地址、特殊标签掩码
    u8 v;                     // CDE写入值（数字编码 + 标签位）
    switch(pos){
        case 1:
            a_abfg=2;
            a_cde=3;
            s_mask=1;    /* 0x01 = "间隔延时"标签 (addr3.0) [2026-08-06] */
            break;
        case 2:
            a_abfg=4;
            a_cde=5;
            s_mask=1;
            break;
        case 3:
            a_abfg=6;
            a_cde=7;
            s_mask=1;
            break;
        case 4:
            a_abfg=8;
            a_cde=9;
            s_mask=0;
            break;
        case 5:
            a_abfg=10;
            a_cde=11;
            s_mask=0;
            break;
        case 6:
            a_abfg=12;
            a_cde=13;
            s_mask=0;
            break;
    }
    if(digit<=9){
        HT1622_WriteData(a_abfg,PA_ABFG[digit]);
        v=PA_CDE[digit];
        if(sp)v|=s_mask;
        HT1622_WriteData(a_cde,v);
    }
    else{
        HT1622_WriteData(a_abfg,0);
        HT1622_WriteData(a_cde,0);
    }  // 无效数字：显示空白
}

// LCD_WriteF88S - 在F8.8秒区域写入一个数字（Pattern A）
// 参数：pos=位置(1=整数位,2=小数位)，digit=数字(0~9)，sp=是否显示"秒"标签
static void LCD_WriteF88S(u8 pos,u16 digit,bit sp) {
    u8 aa,ac,sm;  // ABFG地址、CDE地址、标签掩码
    u8 v;         // CDE写入值
    if(pos==1)
    {
        aa=16;
        ac=17;
        sm=0x01;
    }  // addr16-17=整数位，addr18-19=小数位    
    else
    {
        aa=18;
        ac=19;
        sm=0x01;
    }
    if(digit<=9)
    {
        HT1622_WriteData(aa,PA_ABFG[digit]);
        v=PA_CDE[digit];
        if(sp)v|=sm;
        HT1622_WriteData(ac,v);
    }
    else
    {
        HT1622_WriteData(aa,0);
        HT1622_WriteData(ac,sp?sm:0);
    }
}

// LCD_Write88L - 在8.8(左)区域写入一个数字（Pattern B）
// 参数：pos=位置(1=十位,2=个位)，digit=数字(0~9)，sp=是否显示"分"标签
static void LCD_Write88L(u8 pos,u8 digit,bit sp) {
    u8 aa,ac,sm;
    u8 v;
    if(pos==1)
    {
        aa=21;
        ac=20;
        sm=0x08;
    }
    else
    {
        aa=23;
        ac=22;
        sm=0x08;
    }  // addr20-21=十位，addr22-23=个位
    if(digit<=9)
    {
        HT1622_WriteData(aa,PB_ABFG[digit]);
        v=PB_CDE[digit];
        if(sp)v|=sm;
        HT1622_WriteData(ac,v);
    }
    else
    {
        HT1622_WriteData(aa,0);
        HT1622_WriteData(ac,sp?sm:0);
    }
}

// LCD_Write88M - 在8.8(中)区域写入一个数字（Pattern B）
// 参数：pos=位置(1=十位,2=个位)，digit=数字(0~9)，sp=是否显示"秒"标签
static void LCD_Write88M(u8 pos,u16 digit,bit sp) {
    u8 aa,ac,sm;
    u8 v;
    if(pos==1)
    {
        aa=27;
        ac=26;
        sm=0x08;
    }
    else
    {
        aa=29;
        ac=28;
        sm=0x08;
    }
    if(digit<=9)
    {
        HT1622_WriteData(aa,PB_ABFG[digit]);
        v=PB_CDE[digit];
        if(sp)v|=sm;
        HT1622_WriteData(ac,v);
    }
    else
    {
        HT1622_WriteData(aa,0);
        HT1622_WriteData(ac,sp?sm:0);
    }
}

// LCD_Write8888R - 在8888(右)区域写入一个数字（Pattern B）
// 参数：pos=位置(1~4)，digit=数字(0~9)，sp=是否显示"时"标签
// ★ 2026-07-25修正: 新板子LCD物理布局与原始映射表一致，不需要对调。
//   pos1=addr31/30(左数第1), pos2=addr33/32(左数第2), pos3=addr35/34(左数第3), pos4=addr37/36(左数第4)
static void LCD_Write8888R(u8 pos,u16 digit,bit sp) {
    u8 aa,ac,sm;
    u8 v;
    if(pos==1)
    {
        aa=31;
        ac=30;
        sm=0x08;
    }
    else if(pos==2)
    {
        aa=33;
        ac=32;
        sm=0x08;
    }
    else if(pos==3)
    {
        aa=35;
        ac=34;
        sm=0x08;
    }
    else
    {
        aa=37;
        ac=36;
        sm=0x08;
    }
    if(digit<=9)
    {
        HT1622_WriteData(aa,PB_ABFG[digit]);
        v=PB_CDE[digit];
        if(sp)v|=sm;
        HT1622_WriteData(ac,v);
    }
    else
    {
        HT1622_WriteData(aa,0);
        HT1622_WriteData(ac,sp?sm:0);
    }
}

// LCD_Write8888R_Raw - 在8888(右)区域写入原始段码值（用于YES/no等特殊显示）
// 参数：pos=位置(1~4)，abfg=ABFG段码值，cde=CDE段码值
// ★ 2026-07-25修正: pos1/pos2地址对调回原始映射 (同LCD_Write8888R)
static void LCD_Write8888R_Raw(u8 pos,u8 abfg,u8 cde) {
    u8 aa,ac;
    if(pos==1)
    {
        aa=31;
        ac=30;
    }
    else if(pos==2)
    {
        aa=33;
        ac=32;
    }
    else if(pos==3)
    {
        aa=35;
        ac=34;
    }
    else
    {
        aa=37;
        ac=36;
    }
    HT1622_WriteData(aa,abfg);
    HT1622_WriteData(ac,cde);
}

/*=============================================================
 * 区域绘制/清除函数
 *=============================================================*/

// Draw_Clock - 绘制时钟显示（188:88:88格式）
// [2026-08-06] 补零：小时十位为0也显示0；按冲洗状态合并"主阀/间隔延时"标签；
//   压差等待时点亮addr14.1"压差延迟"
static void Draw_Clock(void) {
    u8 h1,h2,m1,m2,s1,s2;  // 时/分/秒的十位和个位
    bit show_main;          // "主阀"标签 (addr9.0)
    bit show_interval;      // "间隔延时"标签 (addr3.0)
    show_main = 0;
    show_interval = 0;
    if(Flush_IsActive()) {
        if(g_flush_control.state == FLUSH_STATE_MAIN_VALVE) {
            show_main = 1;        /* 主阀延时：两个标签都亮 [2026-08-06] */
            show_interval = 1;
        } else if(g_flush_control.state == FLUSH_STATE_INTERVAL) {
            show_interval = 1;    /* 间隔等待：只亮"间隔延时" [2026-08-06] */
        }
    }
    h1=clock_h/10;h2=clock_h%10;m1=clock_m/10;m2=clock_m%10;s1=clock_s/10;s2=clock_s%10;
    LCD_Write188(6,h1,0); LCD_Write188(5,h2,0);   // 时：十位(补零)+个位
    LCD_Write188(4,m1,show_main); LCD_Write188(3,m2,Flush_IsActive());  // 分：(主阀标签)+个位(反冲洗中!)
    LCD_Write188(2,s1,1); LCD_Write188(1,s2,show_interval);             // 秒：(冒号)+个位(间隔延时)
    HT1622_WriteData(14, g_pressure_diff_pending ? 0x02 : 0x00);  /* addr14.1=压差延迟标签 [2026-08-06] */
}

// Draw_CountdownOff - 周期冲洗关闭时188区域显示 --:--:--（6个g段，冒号仍亮） [2026-08-06]
// [2026-08-13] 同时清零 addr14.1「压差延迟」标签——OFF模式下必无压差等待，
//   否则从DIFF_PEND切到OFF时旧标签残留（Draw_Clock的addr14写入只发生在数字分支）。
static void Draw_CountdownOff(void) {
    HT1622_WriteData(12,0x01); HT1622_WriteData(13,0x00);  // pos6 时十位: -
    HT1622_WriteData(10,0x01); HT1622_WriteData(11,0x00);  // pos5 时个位: -
    HT1622_WriteData(8,0x01);  HT1622_WriteData(9,0x00);   // pos4 分十位: -
    HT1622_WriteData(6,0x01);  HT1622_WriteData(7,0x00);   // pos3 分个位: -
    HT1622_WriteData(4,0x01);  HT1622_WriteData(5,0x01);   // pos2 秒十位: -（addr5.0冒号亮）
    HT1622_WriteData(2,0x01);  HT1622_WriteData(3,0x00);   // pos1 秒个位: -
    HT1622_WriteData(14,0x00);                             // 清「压差延迟」标签 [2026-08-13]
}

/* Draw_CountdownValue - 按当前188显示模式绘制倒计时区域 [2026-08-13]
 * 统一入口：Draw_FullScreen 与 Menu_ClockTick 都走这里，保证
 *   - OFF（flush_cycle==0）→ Draw_CountdownOff() 显示 --:--:--
 *   - 其他（周期/压差等待/冲洗各状态）→ 用 remain 秒数画数字倒计时
 * 修复：全屏重绘（开机/按键/冲洗状态转移）此前无条件画数字，用陈旧 clock_h/m/s
 *   （OFF 时 remain 恒为0 → clock=0）把 --:--:-- 覆盖成 00:00:00。 */
static void Draw_CountdownValue(void) {
    u32 remain;
    if(Control_GetDisplayMode() == DISP_MODE_OFF) {
        Draw_CountdownOff();
        return;
    }
    remain = Control_GetDisplayCountdownSec();
    /* 小时上限99（188:88:88最多显示99:59:59） */
    if((remain / 3600) > 99) remain = 99UL * 3600 + 59 * 60 + 59;
    clock_h = (u8)(remain / 3600);
    clock_m = (u8)((remain / 60) % 60);
    clock_s = (u8)(remain % 60);
    Draw_Clock();
}

// 前置声明：冲洗期间F8.8秒区域显示站号用（定义在下方）[2026-08-06]
static void Draw_F88S_Station(u8 val);

// Draw_ActualPressure - 绘制实际压力值（F8.8秒格式，整数+小数）
// [2026-08-06] 冲洗期间不显示实际压力：F8.8秒区域显示 F+当前阀站号（如 F03/F10）
static void Draw_ActualPressure(void) {
    u8 t,d;  // 整数部分、小数部分
    /* 冲洗期间：F8.8秒显示当前冲洗阀站号 [2026-08-06] */
    if(Flush_IsActive()) {
        Draw_F88S_Station(g_flush_control.current_station);
        return;
    }
    t=(u8)g_actual_pressure;                         // 取整数部分
    if(t >= 10) {
        if(t > 99) t = 99;                           // 超过99钳位（传感器上限101.5PSI）
        LCD_WriteF88S(1,t/10,0);                     // 十位
        LCD_WriteF88S(2,t%10,0);                     // 个位（sp=0，不亮小数点addr19.0）
    } else {
        d=((u8)(g_actual_pressure*10.0f))%10;        // 取小数部分（×10后取个位）
        LCD_WriteF88S(1,t,0);                        // 整数位
        LCD_WriteF88S(2,d,1);                        // 小数位（sp=1，亮小数点）
    }
}

// Draw_UnitFlush - 绘制冲洗时间（8.8(左)格式）
// ≥60秒显示"分"格式（X.X分），<60秒显示"秒"格式（XX秒）
static void Draw_UnitFlush(void) {
    u16 sec;  // 冲洗时间（秒）
    u8 t,o;   // 十位、个位
    u8 v24;   // addr24写入值（"分"标签+BAR标签）
    sec=(u16)(g_param_config.unit_flush_time+0.5f);  // 四舍五入取整
    if(sec<5)sec=5;  // 最小冲洗时间5秒
    if(sec>=60){t=sec/60;o=(sec/6)%10;LCD_Write88L(1,t,1);LCD_Write88L(2,o,1);HT1622_WriteData(25,0);
        v24=0x01; if(g_param_config.unit_selection==0) v24|=0x04; HT1622_WriteData(24,v24);}
    else{o=sec%10;t=sec/10;LCD_Write88L(1,(t>0)?t:99,1);LCD_Write88L(2,o,0);HT1622_WriteData(25,0x08);
        v24=0x00; if(g_param_config.unit_selection==0) v24|=0x04; HT1622_WriteData(24,v24);}
}

// Draw_PressureSet - 绘制压差设定值（8.8(中)格式）
// 存储恒为bar；PSI模式下乘以14.5038按PSI显示，且整数位≥10时只显示整数 [2026-08-03]
static void Draw_PressureSet(void) {
    float v; u16 tenths; u8 t,d;  // 压差值、十分位值、整数部分、小数部分
    u8 cde_val;       // CDE写入值（数字编码 + PSI标签）
    u8 int_digit;     // 整数位显示值（两位整数时为十位）
    v=g_param_config.pressure_diff_set;
    if(g_param_config.unit_selection==1){
        v *= 14.5038f;                               // bar → PSI 显示
    }
    /* 先四舍五入到十分位再拆整数/小数，避免浮点误差（如 1.9+0.1=1.99999…）
       导致整数位截断少1 [2026-08-03] */
    tenths = (u16)(v * 10.0f + 0.5f);
    if(tenths >= 100){
        /* 两位整数：只显示整数，不显示小数点 */
        t = (u8)(tenths / 10);
        if(t > 99) t = 99;
        LCD_Write88M(1,t/10,0);                      // 十位
        LCD_Write88M(2,t%10,0);                      // 个位（sp=0，不亮小数点addr28.3）
        int_digit = t/10;
    } else {
        t = (u8)(tenths / 10);
        d = (u8)(tenths % 10);
        LCD_Write88M(1,t,0);LCD_Write88M(2,d,1);     // 整数+小数（带小数点）
        int_digit = t;
    }
    // PSI标签: addr26.3，必须与整数位CDE合并写入（不能单独写，否则覆盖digit段）
    if(g_param_config.unit_selection==1){
        cde_val = PB_CDE[int_digit];                 // 整数位（或十位）的CDE编码
        cde_val |= 0x08;                             // bit3=PSI标签（OR合并，不覆盖digit段）
        HT1622_WriteData(26, cde_val);               // 写入合并后的值
    }
}

// Draw_FlushCycle - 绘制冲洗周期（8888(右)格式）
// ≥60分钟显示"分"格式，<60分钟显示"秒"格式；flush_cycle==0=OFF显示"OFF" [2026-08-06]
static void Draw_FlushCycle(void) {
    u16 iv; u16 t;  // 冲洗周期（分钟或秒）、小时数（u16避免≥256h溢出显示 [2026-08-03]）
    /* flush_cycle==0 = 周期冲洗关闭：8888右显示 OFF [2026-08-06]
       段码Pattern B: O=(0x07,0x07) F=(0x0D,0x02) */
    if(g_param_config.flush_cycle <= 0.0f) {
        LCD_Write8888R_Raw(1,0x07,0x07);  /* O */
        LCD_Write8888R_Raw(2,0x0D,0x02);  /* F */
        LCD_Write8888R_Raw(3,0x0D,0x02);  /* F */
        LCD_Write8888R_Raw(4,0x00,0x00);  /* 空 */
        return;
    }
    iv=(u16)(Param_GetValue(PARAM_FLUSH_CYCLE)*60.0f+0.5f);  // 小时→分钟，四舍五入
    if(iv>=60){t=iv/60;  // ≥60分钟：显示"分"格式


        LCD_Write8888R(1,(t/1000)%10,0);LCD_Write8888R(2,(t/100)%10,0);  // 千位+百位
        LCD_Write8888R(3,(t/10)%10,0);LCD_Write8888R(4,t%10,1);}         // 十位+个位(带"时"标签)
    else{  // <60分钟：显示"秒"格式
        LCD_Write8888R(1,(iv/1000)%10,0);LCD_Write8888R(2,(iv/100)%10,0);
        LCD_Write8888R(3,(iv/10)%10,1);LCD_Write8888R(4,iv%10,0);}
}

// Clear_UnitFlush - 清除冲洗时间显示区域（addr20-23）
static void Clear_UnitFlush(void) {
    HT1622_WriteData(21,0);HT1622_WriteData(20,0x08);HT1622_WriteData(23,0);HT1622_WriteData(22,0);
}

// Clear_PressureSet - 清除压差设定显示区域（addr26-29）
static void Clear_PressureSet(void) {
    HT1622_WriteData(27,0);HT1622_WriteData(26,0);HT1622_WriteData(29,0);HT1622_WriteData(28,0);
}

// Clear_FlushCycle - 清除冲洗周期显示区域（通过写入99实现"空白"效果）
static void Clear_FlushCycle(void) {
    LCD_Write8888R(1,99,0);LCD_Write8888R(2,99,0);LCD_Write8888R(3,99,0);LCD_Write8888R(4,99,0);
}

// Draw_FSelectScreen - 绘制F-SELECT选择画面（YES/no）
// [2026-08-06] F8.8秒区域显示 "F--"（不再显示F01）：F标签 + 两个短横线(g段)
static void Draw_FSelectScreen(void) {
    LCD_ClearAll();
    /* F-- : F8.8秒 pos1=短横线(g段)+F标签(addr17.0)，pos2=短横线(g段) [2026-08-06] */
    HT1622_WriteData(16,0x01);  /* F8.8秒 pos1 ABFG: g段='-' */
    HT1622_WriteData(17,0x01);  /* pos1 CDE: bit0=F标签 */
    HT1622_WriteData(18,0x01);  /* F8.8秒 pos2 ABFG: g段='-' */
    HT1622_WriteData(19,0x00);  /* pos2 CDE: 句点灭 */
    if(f_select_choice) {
        // YES：在8888(右)区域显示"YES"
        LCD_Write8888R_Raw(1,0x0E,0x05);  // Y
        LCD_Write8888R_Raw(2,0x0D,0x06);  // E
        LCD_Write8888R_Raw(3,0x0D,0x05);  // S
        LCD_Write8888R_Raw(4,0x00,0x00);  // 空
    } else {
        // no：在8888(右)区域显示"no"
        LCD_Write8888R_Raw(1,0x07,0x03);  // N (a,b,c,e,f)
        LCD_Write8888R_Raw(2,0x07,0x07);  // O (a,b,c,d,e,f) [修2026-07-17: 原0x03,0x07缺f段]
        LCD_Write8888R_Raw(3,0x00,0x00);  // 空
        LCD_Write8888R_Raw(4,0x00,0x00);  // 空
    }
}

/*=============================================================
 * Level 2 辅助绘制函数
 *=============================================================*/

// Draw_8888R_YES - 在8888(右)区域显示"YES"（使用原始段码值）
static void Draw_8888R_YES(void) {
    LCD_Write8888R_Raw(1,0,0);          // 位置1清零
    LCD_Write8888R_Raw(2,0x0E,0x05);    // Y
    LCD_Write8888R_Raw(3,0x0D,0x06);    // E
    LCD_Write8888R_Raw(4,0x0D,0x05);    // S
}

// Draw_8888R_no - 在8888(右)区域显示"no"
static void Draw_8888R_no(void) {
    LCD_Write8888R_Raw(1,0,0);
    LCD_Write8888R_Raw(2,0,0);
    LCD_Write8888R_Raw(3,0x08,0x03);
    LCD_Write8888R_Raw(4,0x08,0x07);
}

// Clear_8888R - 清除8888(右)全部4个位置的段码
static void Clear_8888R(void) {
    LCD_Write8888R_Raw(1,0,0); LCD_Write8888R_Raw(2,0,0);
    LCD_Write8888R_Raw(3,0,0); LCD_Write8888R_Raw(4,0,0);
}

// Clear_88L_ValueOnly - 仅清除8.8(左)数字值（保留"分/秒"标签）
static void Clear_88L_ValueOnly(void) {
    HT1622_WriteData(21,0); HT1622_WriteData(20,0);  // 十位清零
    HT1622_WriteData(23,0); HT1622_WriteData(22,0);  // 个位清零
}

// Clear_8888R_ValueOnly - 仅清除8888(右)数字值（保留"时"标签）
static void Clear_8888R_ValueOnly(void) {
    LCD_Write8888R_Raw(2,0,0); LCD_Write8888R_Raw(3,0,0); LCD_Write8888R_Raw(4,0,0);
}

// Clear_F88S_ValueOnly - 仅清除F8.8秒数字值（保留"F"和"秒"标签）
static void Clear_F88S_ValueOnly(void) {
    HT1622_WriteData(16,0); HT1622_WriteData(17,0);  // 整数位清零
    HT1622_WriteData(18,0); HT1622_WriteData(19,0);  // 小数位清零
}

// Draw_88L_Seconds - 在8.8(左)区域显示秒数（带"秒"标签）
static void Draw_88L_Seconds(u8 val) {
    u8 t,o;  // 十位、个位
    t=val/10; o=val%10;
    LCD_Write88L(1,(t>0)?t:99,0);  // 十位（0→空白）
    LCD_Write88L(2,o,0);             // 个位
    HT1622_WriteData(25,0x08);        // addr25.3=秒标签
}

// Clear_88L - 清除8.8(左)全部段码（包括"秒"标签）
static void Clear_88L(void) {
    HT1622_WriteData(21,0); HT1622_WriteData(20,0);  // 十位清零
    HT1622_WriteData(23,0); HT1622_WriteData(22,0);  // 个位清零
    HT1622_WriteData(25,0);                              // 秒标签清零
}

// Draw_F88S_Station - 在F8.8秒区域显示站编号（带"F"和"秒"标签）
static void Draw_F88S_Station(u8 val) {
    u8 t,o;  // 十位、个位
    t=val/10; o=val%10;
    LCD_WriteF88S(1,(t>0)?t:99,1);  // 整数位（带"F"标签）
    LCD_WriteF88S(2,o,0);             // 小数位
}

// Clear_F88S - 清除F8.8秒全部段码（包括"F"和"秒"标签）
static void Clear_F88S(void) {
    HT1622_WriteData(16,0); HT1622_WriteData(17,0);  // 整数位清零
    HT1622_WriteData(18,0); HT1622_WriteData(19,0);  // 小数位清零
}

// ---- 各参数画面（Level 2独立显示） ----
// show_val=1:显示参数值，show_val=0:清除参数值（闪烁用）

// Draw_L2_MainValve - 主阀选择：显示"主阀选择"标签 + YES/no
static void Draw_L2_MainValve(bit show_val) {
    LCD_ClearAll();
    HT1622_WriteData(14,0x08);  // addr14.3=主阀选择标签
    if(show_val)
    {
        if(g_param_config.main_valve_select)
        { 
            Draw_8888R_YES();
        }
        else
        {
            Draw_8888R_no();
        }
    }
    else
    {
        Clear_8888R();
    }
}

// Draw_L2_MainValveDelay - 主阀延时：显示"延迟阀"标签 + 秒数
static void Draw_L2_MainValveDelay(bit show_val) {
    u16 vv;
    u8 v;
    vv=g_param_config.main_valve_delay;
    if(vv>99)vv=99;   // 先u16钳位再转u8，避免 (u8)强转在前导致 >99 显示错乱 [2026-08-03]
    v=(u8)vv;
    LCD_ClearAll();
    HT1622_WriteData(15,0x02);  // addr15.1=延迟阀标签
    if(show_val)
    {
        Draw_88L_Seconds(v);
    }
    else
    {
        Clear_88L();
    }
}

// Draw_L2_IntervalTime - 间隔时间：显示"间隔时间"标签 + 秒数
static void Draw_L2_IntervalTime(bit show_val) {
    u16 vv;
    u8 v;
    vv=g_param_config.interval_time;
    if(vv>99)vv=99;
    v=(u8)vv;
    LCD_ClearAll();
    HT1622_WriteData(14,0x04);  // addr14.2=间隔时间标签
    if(show_val)
    {
        Draw_88L_Seconds(v);
    }
    else
    {
        Clear_88L();
    }
}

// Draw_L2_PressureDelay - 压差延迟：显示"压差延迟"标签 + 秒数
static void Draw_L2_PressureDelay(bit show_val) {
    u16 vv;
    u8 v;
    vv=g_param_config.pressure_delay;
    if(vv>99)vv=99;
    v=(u8)vv;
    LCD_ClearAll();
    HT1622_WriteData(14,0x02);  // addr14.1=压差延迟标签
    if(show_val)
    {
        Draw_88L_Seconds(v);
    }
    else
    {
        Clear_88L();
    }
}

// Draw_L2_CycleLimit - 循环限制：显示"循环限制"标签 + 次数（8888右区域，十位+个位）
static void Draw_L2_CycleLimit(bit show_val) {
    u16 val; u8 d1,d2;  // 循环次数、十位、个位
    val=g_param_config.cycle_limit;
    if(val>99)val=99;
    LCD_ClearAll();
    HT1622_WriteData(14,0x01);  // addr14.0=循环限制标签
    if(show_val){
        d1=val/10;
        d2=val%10;
        LCD_Write8888R_Raw(1,0,0);
        LCD_Write8888R_Raw(2,0,0);  // 高两位清零
        LCD_Write8888R(3,(d1>0)?d1:99,0);
        LCD_Write8888R(4,d2,0);  // 低两位显示数字
    }
    else
    {
        Clear_8888R();
    }
}

// Draw_L2_AlarmOutput - 报警输出：显示"报警输出"标签 + YES/no
static void Draw_L2_AlarmOutput(bit show_val) {
    LCD_ClearAll();
    HT1622_WriteData(15,0x08);  // addr15.3=报警输出标签
    if(show_val){
        if(g_param_config.alarm_output)
        {
            Draw_8888R_YES();
        }
        else
        {
            Draw_8888R_no();
        }
    }
    else
    {
        Clear_8888R();
    }
}

// Draw_L2_ZeroCalib - 零点校准：显示"零点校准"标签 + YES/no
static void Draw_L2_ZeroCalib(bit show_val) {
    LCD_ClearAll();
    HT1622_WriteData(15,0x04);  // addr15.2=零点校准标签
    if(show_val){
        if(g_param_config.zero_calibration)
        {
            Draw_8888R_YES();
        }
        else
        {
            Draw_8888R_no();
        }
    }
    else
    {
        Clear_8888R();
    }
}

// Draw_L2_StationRecog - 站数识别：显示"站数识别"标签 + 站数（F8.8秒格式）
static void Draw_L2_StationRecog(bit show_val) {
    u8 v; 
    v=(u8)g_param_config.station_recognition;
    if(v<1)v=1;
    if(v>10)v=10;  // 范围限制1~10
    LCD_ClearAll();
    HT1622_WriteData(15,0x01);  // addr15.0=站数识别标签
    if(show_val)
    {
        Draw_F88S_Station(v);
    }
    else
    {
        Clear_F88S();
    }
}

// [2026-08-03] Draw_L2_ClockSet 已移除（右上角为周期冲洗倒计时，无壁钟设置）

static void Draw_L2_UnitSelect(bit show_val) {
    LCD_ClearAll();
    if(show_val)
    {
        if(g_param_config.unit_selection==0)
        {
            HT1622_WriteData(24,0x04);
        }
        else
        {
            HT1622_WriteData(26,0x08);
        }
    }
}

/* Draw_CalibSpanAdjust - 零点校准画面（量程标定调整）
 * 8888(右): 实时ADC值 (4位整数, 0~1023)
 * 8.8(左):  参考压力值 (bar, 可+/-调整, 显示范围0.0~9.9)
 * addr15.2: 零点校准标签
 */
static void Draw_CalibSpanAdjust(void)
{
    u8 t, o;
    u16 adc;
    float p_bar;

    LCD_ClearAll();
    HT1622_WriteData(15, 0x04);   /* addr15.2 = 零点校准标签 */

    /* 8888(右)区域显示ADC原始值 (4位整数) */
    adc = g_calib_adc_live;
    if(adc > 1023) adc = 1023;
    LCD_Write8888R(1, (adc/1000)%10, 0);
    LCD_Write8888R(2, (adc/100)%10, 0);
    LCD_Write8888R(3, (adc/10)%10, 0);
    LCD_Write8888R(4, adc%10, 0);

    /* 8.8(左)区域显示参考压力值 (bar, 1位小数) */
    p_bar = g_calib_pressure_ref / 100.0f;   /* kPa → bar */
    t = (u8)p_bar;
    o = ((u8)(p_bar * 10.0f + 0.5f)) % 10;
    LCD_Write88L(1, (t>0) ? t : 99, 0);     /* 十位(0→空白) */
    LCD_Write88L(2, o, 0);                    /* 个位，无单位标签 */
    HT1622_WriteData(25, 0x00);               /* 清除addr25所有标签位 */
}

/* [2026-08-03] Draw_ClockSet / Clear_ClockSet_Hour / Clear_ClockSet_Minute 已移除
   （右上角188:88:88改为周期冲洗倒计时，无壁钟设置） */

// Draw_CountPage - 冲洗计数页：底部"反冲洗的触发次数累计：" + 模式标签 + 8888右计数 [2026-08-06]
// page: 0=压差, 1=周期, 2=手动
// 段码: addr24.1="反冲洗的触发次数累计：" addr24.3="压差" addr30.3="周期" addr32.3="手动"
// ★ 周期/手动标签必须与数字CDE段合并写入（sp参数），否则LCD_Write8888R会清零标签位 [2026-08-06]
static void Draw_CountPage(u8 page) {
    u16 cnt;
    LCD_ClearAll();
    /* addr24.1="反冲洗的触发次数累计："；压差页同时亮 addr24.3"压差" */
    if(page==0) HT1622_WriteData(24, 0x0A);   /* bit1(累计标签) + bit3(压差) */
    else        HT1622_WriteData(24, 0x02);   /* 仅 bit1(累计标签) */
    /* 计数（RAM累计，断电清零 [2026-08-06]） */
    if(page==0) cnt = g_param_config.count_diff_trigger;
    else if(page==1) cnt = g_param_config.count_cycle_trigger;
    else cnt = g_param_config.count_manual_trigger;
    if(cnt > 9999) cnt = 9999;                /* 8888右最多4位 */
    /* 数字写入：周期页pos1带"周期"标签(addr30.3)，手动页pos2带"手动"标签(addr32.3)，
       其余位置sp=0 [2026-08-06] */
    LCD_Write8888R(1,(cnt/1000)%10,(page==1));
    LCD_Write8888R(2,(cnt/100)%10,(page==2));
    LCD_Write8888R(3,(cnt/10)%10,0);
    LCD_Write8888R(4,cnt%10,0);
}

// Draw_Level2Screen - Level2画面统一调度：根据当前参数类型调用对应绘制函数
// show_val=1:显示参数值，show_val=0:清除参数值（闪烁用）
static void Draw_Level2Screen(bit show_val) {
    switch((u8)g_menu_status.current_param){
        case PARAM_MAIN_VALVE_SELECT:   Draw_L2_MainValve(show_val);     break;
        case PARAM_MAIN_VALVE_DELAY:    Draw_L2_MainValveDelay(show_val); break;
        case PARAM_INTERVAL_TIME:       Draw_L2_IntervalTime(show_val);   break;
        case PARAM_PRESSURE_DELAY:      Draw_L2_PressureDelay(show_val);  break;
        case PARAM_CYCLE_LIMIT:         Draw_L2_CycleLimit(show_val);     break;
        case PARAM_ALARM_OUTPUT:        Draw_L2_AlarmOutput(show_val);    break;
        case PARAM_ZERO_CALIBRATION:    Draw_L2_ZeroCalib(show_val);      break;
        case PARAM_STATION_RECOGNITION: Draw_L2_StationRecog(show_val);   break;
        case PARAM_UNIT_SELECTION:      Draw_L2_UnitSelect(show_val);     break;
        default: break;
    }
}

// 闪烁时只绘制/擦除数值，不清屏，不写标签
static void Draw_Level2Screen_Blink(bit show_val) {
    switch((u8)g_menu_status.current_param){
        case PARAM_MAIN_VALVE_SELECT:
            if(show_val){if(g_param_config.main_valve_select)Draw_8888R_YES();else Draw_8888R_no();}
            else Clear_8888R_ValueOnly();
            break;
        case PARAM_MAIN_VALVE_DELAY:
        case PARAM_INTERVAL_TIME:
        case PARAM_PRESSURE_DELAY:
            if(show_val){u16 vl;switch((u8)g_menu_status.current_param){
                case PARAM_MAIN_VALVE_DELAY:vl=g_param_config.main_valve_delay;break;
                case PARAM_INTERVAL_TIME:vl=g_param_config.interval_time;break;
                default:vl=g_param_config.pressure_delay;break;
            }if(vl>99)vl=99;Draw_88L_Seconds((u8)vl);}   /* 先u16钳位再转u8 [2026-08-03] */
            else Clear_88L_ValueOnly();
            break;
        case PARAM_CYCLE_LIMIT:
            if(show_val){u16 vl;u8 d1,d2;vl=g_param_config.cycle_limit;if(vl>99)vl=99;
                d1=vl/10;d2=vl%10;
                LCD_Write8888R(3,(d1>0)?d1:99,0);LCD_Write8888R(4,d2,0);}
            else Clear_8888R_ValueOnly();
            break;
        case PARAM_ALARM_OUTPUT:
            if(show_val){if(g_param_config.alarm_output)Draw_8888R_YES();else Draw_8888R_no();}
            else Clear_8888R_ValueOnly();
            break;
        case PARAM_ZERO_CALIBRATION:
            if(show_val){if(g_param_config.zero_calibration)Draw_8888R_YES();else Draw_8888R_no();}
            else Clear_8888R_ValueOnly();
            break;
        case PARAM_STATION_RECOGNITION:
            if(show_val){u8 vs;vs=g_param_config.station_recognition;if(vs<1)vs=1;if(vs>10)vs=10;Draw_F88S_Station(vs);}
            else Clear_F88S_ValueOnly();
            break;
        case PARAM_UNIT_SELECTION:
            if(show_val){if(g_param_config.unit_selection==0)HT1622_WriteData(24,0x04);else HT1622_WriteData(26,0x08);}
            else{HT1622_WriteData(24,0);HT1622_WriteData(26,0);}
            break;
        default: break;
    }
}

// ==================== 时钟和闪烁 ====================

// Menu_ClockTick - 节拍处理（每20ms调用一次）
// [2026-08-06] 188:88:88 按 Control_GetDisplayMode/CountdownSec 显示：
//   待机=周期倒计时、冲洗各状态=对应延时倒计时、压差等待=压差延时、OFF=--:--:--
//   BootTest期间不刷新（画面由Draw_BootTest接管）
void Menu_ClockTick(void) {
    static u8  press_refresh = 0;   // 压力/F88刷新计数器（待机/Level1时约每200ms刷新一次）
    static u32 last_remain = 0xFFFFFFFF;  /* 上次显示的剩余秒数（初值强制首帧刷新） */
    static u8  last_mode = 0xFF;          /* 上次显示模式（初值强制首帧刷新） */
    u32 remain;                     // 当前显示剩余秒数
    u8  mode;                       // 当前显示模式
    u8 lvl;                         // 当前菜单层级
    u8 cp;                          // 当前参数类型
    u8 editing;                     // 是否在编辑状态
    bit show_clock;                 // 当前画面是否显示倒计时（待机 / Level1参数编辑）

    /* 亮屏：无操作30秒(真实时间)后灭屏。用 g_sys_tick 绝对时间差而非循环计数，
     * 不受主循环速度影响（[2026-08-18] 修复主循环空转导致上电立即灭屏） */
    if(!g_screen_off)
    {
        if((u16)(Sys_GetTick() - screen_off_base) >= SCREEN_ON_TICKS)
        {
            g_screen_off = 1;
            LCD_ClearAll();   /* 段码清零：HT1622 电流降至 µA 级（clr_i 为文件作用域 xdata，无 overlay 风险） */
            g_lcd_dirty = 0;
            return;
        }
    }
    else
    {
        return;   /* 灭屏：不刷新倒计时/压力/闪烁 */
    }

    if(BootTest_IsActive()) return;   /* 上电关阀测试期间不刷新倒计时 [2026-08-06] */

    lvl = g_menu_status.menu_level;
    cp  = g_menu_status.current_param;
    editing = g_menu_status.editing;
    show_clock = (lvl==MENU_LEVEL_NONE) || (lvl==MENU_LEVEL_1 && editing);
    if(!show_clock) return;

    mode = Control_GetDisplayMode();
    remain = Control_GetDisplayCountdownSec();

    /* 显示模式或剩余秒数变化 → 重绘188区域（含补零/标签/OFF） */
    if(mode != last_mode || remain != last_remain) {
        last_mode = mode;
        last_remain = remain;
        Draw_CountdownValue();   /* 统一分派：OFF→--:--:--，其余按remain画数字 [2026-08-13] */
    }
    /* 待机/Level1时持续刷新实际压差值（≈5Hz）；冲洗期间Draw_ActualPressure改画F88站号 */
    /* ★ 此处不可设 g_lcd_dirty=0，否则会覆盖反冲洗状态机刚设的脏标记，
       导致冲洗图标(手/尖峰/P)和"反冲洗中!"永远不更新(或延迟数十秒才出现)。
       Draw_ActualPressure 是定向写入 F8.8秒 区域，不影响全屏重绘逻辑。 */
    if(++press_refresh >= 10){press_refresh = 0; Draw_ActualPressure();}
}

// Menu_BlinkTick - 闪烁节拍处理（每20ms调用一次）
// Level1：每15×20ms=300ms翻转一次闪烁状态
// Level2/零点校准：使用独立的l2_blink_timer，同样300ms翻转
// 非编辑状态时重置所有闪烁计数器
void Menu_BlinkTick(void) {
    u8 editing;  // 是否在编辑状态
    u8 lvl;      // 当前菜单层级
    u8 cp;       // 当前参数类型
    editing = g_menu_status.editing;
    lvl     = g_menu_status.menu_level;
    cp      = g_menu_status.current_param;
    if(g_screen_off) return;   /* 灭屏：无闪烁 [2026-08-08] */
    if(BootTest_IsActive()) return;        /* 上电关阀测试期间无闪烁 [2026-08-06] */
    // Level1闪烁：仅在编辑非F-SELECT状态时生效
    if(editing && lvl==MENU_LEVEL_1 && cp!=PARAM_FLUSH_SELECT) {
        if(++blink_timer>=15){blink_timer=0;blink_on=!blink_on;  // 300ms翻转
            if(blink_on){switch(cp){  // 显示：重绘参数值
                case PARAM_UNIT_FLUSH_TIME:Draw_UnitFlush();break;
                case PARAM_PRESSURE_DIFF_SET:Draw_PressureSet();break;
                case PARAM_FLUSH_CYCLE:Draw_FlushCycle();break;}}
            else{switch(cp){  // 隐藏：清除参数值（保留标签）
                case PARAM_UNIT_FLUSH_TIME:Clear_UnitFlush();break;
                case PARAM_PRESSURE_DIFF_SET:Clear_PressureSet();break;
                case PARAM_FLUSH_CYCLE:Clear_FlushCycle();break;}}
        }
    } else if(editing && lvl==MENU_LEVEL_2) {
        // Level2闪烁：独立计时器，仅重绘/擦除数值
        if(++l2_blink_timer>=15){l2_blink_timer=0;l2_blink_on=!l2_blink_on;
            Draw_Level2Screen_Blink(l2_blink_on);
        }
    } else if(editing && lvl==MENU_LEVEL_ZERO_CALIB) {
        /* 零点校准：闪烁显示参考压力值 */
        if(++l2_blink_timer >= 15){l2_blink_timer = 0; l2_blink_on = !l2_blink_on;
            if(l2_blink_on)
                Draw_CalibSpanAdjust();
            else
                Clear_88L_ValueOnly();
        }
    } else {blink_timer=0;blink_on=1;l2_blink_timer=0;l2_blink_on=1;}  // 非编辑：重置闪烁
}

// ==================== 菜单核心 ====================

/* 人为按键活动：重置亮屏倒计时；若已灭屏则唤醒并重绘 [2026-08-08]
 * 由 main.c 主循环在 Key_Scan() 之后、检测到任一确认按键时调用 */
void Menu_ScreenActivity(void)
{
    screen_off_base = Sys_GetTick();   /* 按键活动：重置30秒亮屏计时 */
    if(g_screen_off)
    {
        g_screen_off = 0;
        g_lcd_dirty = 1;   /* 唤醒：主循环 if(g_lcd_dirty) → Menu_Display() 重绘 */
    }
}

// Menu_Init - 初始化菜单系统：层级=NONE，倒计时显示清零，LCD脏标记=1
void Menu_Init(void) {
    g_menu_status.menu_level=MENU_LEVEL_NONE;g_menu_status.current_param=PARAM_UNIT_FLUSH_TIME;
    g_menu_status.editing=0;g_menu_status.entering_submenu=0;g_menu_status.flush_station_num=1;
    clock_h=0;clock_m=0;clock_s=0;g_lcd_dirty=1;f_select_choice=1;count_page=0;  /* count_page复位 [2026-08-06] */
    g_screen_off = 0;                /* 上电亮屏 [2026-08-08] */
    screen_off_base = Sys_GetTick(); /* 亮屏30秒从当前起算 */
}

// Menu_HandleKey - OK按键处理（短按/长按）
// 使用g_sys_tick快照计算按住时长：
//   长按(≥2秒)→进入Level2菜单
//   短按(≥40ms)→根据当前层级执行导航/保存/进入操作
void Menu_HandleKey(void) {
    static u16 ok_start_tick = 0;  // OK按下时的tick快照（0=未按住）
    u8 cp;                          // 当前参数类型
    u8 idx;                         // Level2参数导航索引
    u8 lvl;                         // 当前菜单层级
    u16 held;                       // 按住时长（tick单位）
    cp  = g_menu_status.current_param;
    lvl = g_menu_status.menu_level;

    if(BootTest_IsActive()) return;        /* 上电关阀测试期间忽略OK [2026-08-06] */

    // OK被按住时：记录起始tick，等待松开
    if(key_ok_held){
        if(ok_start_tick == 0){
            ok_start_tick = Sys_GetTick();  // 首次检测到按住：记录tick（原子读，防撕裂造成长按误判）
            if(ok_start_tick == 0) ok_start_tick = 1;  // 0是"未按住"哨兵值，真实tick=0时偏移1个tick(20ms，可忽略)
        }
        return;  // 继续等待松开
    }

    // OK已松开：计算按住时长并执行对应操作
    if(ok_start_tick == 0) return;  // 未按过OK：不处理
    held = (u16)(Sys_GetTick() - ok_start_tick);  // 计算按住时长（u16减法自动处理溢出）
    ok_start_tick = 0;  // 重置：准备下次按键

    // 长按判断（≥2秒）
    if(held >= OK_LONG_PRESS_TICKS){
        if(lvl == MENU_LEVEL_ZERO_CALIB) {
            /* 校准模式：长按OK = 取消校准（恢复进入前adc_zero、清零zero_calibration），返回Level2 [2026-08-03]
               RUN键已专用于手动冲洗，取消校准改用长按OK */
            Control_CancelCalib();
            g_menu_status.menu_level = MENU_LEVEL_2;
            g_menu_status.current_param = PARAM_ZERO_CALIBRATION;
            g_menu_status.editing = 1;
            l2_blink_timer = 0; l2_blink_on = 1;
            g_lcd_dirty = 1;
        }
        else if(lvl!=MENU_LEVEL_2 && lvl!=MENU_LEVEL_FLUSH_SELECT) Menu_EnterLevel2();
    }
    // 短按判断（≥2个tick=≥40ms，滤除机械毛刺）
    else if(held >= 2){
        if(lvl==MENU_LEVEL_NONE)Menu_EnterLevel1();  // 待机→Level1
        else if(lvl==MENU_LEVEL_1)Menu_NavigateParam();  // Level1：导航到下一个参数
        else if(lvl==MENU_LEVEL_2){
            // Level2：零点校准特殊处理（进入子模式），其他参数导航到下一个
            if(cp==PARAM_ZERO_CALIBRATION && g_param_config.zero_calibration==1){
                if(Flush_IsActive()){
                    /* 冲洗进行中禁止进入校准：零点/量程采样会被冲洗压力扰动 [2026-08-03] */
                    g_lcd_dirty = 1;
                }
                else {
                    Menu_SaveParam();
                    /* 进入时立即采样零点ADC（操作者需先确保传感器零压） */
                    Control_ExecuteZeroCalib();
                    g_menu_status.menu_level = MENU_LEVEL_ZERO_CALIB;
                    g_menu_status.editing = 1;
                    l2_blink_timer = 0; l2_blink_on = 1;
                    g_calib_step = CALIB_STEP_SPAN_ADJUST;
                    g_calib_pressure_ref = 700.0f;
                    g_lcd_dirty = 1;
                }
            } else {
                Menu_SaveParam();  // 保存当前参数
                for(idx=0;idx<9;idx++){if(level2_params[idx]==cp)break;}  // 查找当前参数索引
                if(idx<8){g_menu_status.current_param=level2_params[idx+1];g_menu_status.editing=1;l2_blink_timer=0;l2_blink_on=1;g_lcd_dirty=1;}  // 还有下一个参数
                else{g_menu_status.menu_level=MENU_LEVEL_NONE;g_menu_status.editing=0;g_lcd_dirty=1;}  // 最后一个参数：返回待机
            }
        } else if(lvl==MENU_LEVEL_FLUSH_SELECT){
            Menu_SaveParam();
            g_menu_status.menu_level = MENU_LEVEL_COUNT;  /* 选完阀站→计数页(压差) [2026-08-06] */
            g_menu_status.editing = 0;
            count_page = 0;
            g_lcd_dirty = 1;
        } else if(lvl==MENU_LEVEL_COUNT){
            /* 冲洗计数页：压差→周期→手动→待机 [2026-08-06] */
            if(count_page >= 2){
                g_menu_status.menu_level = MENU_LEVEL_NONE;
                g_menu_status.editing = 0;
            }
            else count_page++;
            g_lcd_dirty = 1;
        } else if(lvl==MENU_LEVEL_ZERO_CALIB){
            /* 零点校准模式：OK=保存量程标定并返回Level2（继续翻后面参数）。
               保存前清零zero_calibration开关（随Param_Save落盘）：否则保存后停在
               参数7仍为YES，下次OK会重进校准重新采样零点、覆盖刚保存的值 [2026-08-03] */
            g_param_config.zero_calibration = 0;
            Control_ExecuteSpanCalib(g_calib_pressure_ref);
            g_calib_step = CALIB_STEP_IDLE;
            g_menu_status.menu_level = MENU_LEVEL_2;
            g_menu_status.current_param = PARAM_ZERO_CALIBRATION;
            g_menu_status.editing = 1;
            l2_blink_timer = 0; l2_blink_on = 1;
            g_lcd_dirty = 1;
        }
    }
}

// 前置声明：上电关阀测试画面绘制（定义在下方）[2026-08-06]
static void Draw_BootTest(void);

// Menu_Display - LCD画面绘制调度（由g_lcd_dirty触发，每~20ms检查一次）
// 根据当前菜单层级调用对应的绘制函数，刷新LCD显示内容
void Menu_Display(void) {
    u8 lvl;      // 当前菜单层级
    u8 cp;       // 当前参数类型
    u8 editing;  // 是否在编辑状态
    if(g_screen_off) return;   /* 灭屏：不重绘 [2026-08-08] */
    lvl = g_menu_status.menu_level;
    cp  = g_menu_status.current_param;
    editing = g_menu_status.editing;
    switch(lvl){
        case MENU_LEVEL_NONE:
            if(BootTest_IsActive()) Draw_BootTest();  /* 上电关阀测试画面 [2026-08-06] */
            else Menu_DisplayMainMenu();
            break;
        case MENU_LEVEL_1:
            if(cp==PARAM_FLUSH_SELECT && !editing)Draw_FSelectScreen();  // F-SELECT非编辑：显示YES/no
            else Menu_DisplayParamValue(cp);                              // 参数编辑：全屏显示
            break;
        case MENU_LEVEL_2:
            if(editing) Draw_Level2Screen(1);  // Level2编辑：独立画面
            else Menu_DisplayParamValue(cp);    // 非编辑：全屏显示
            break;
        case MENU_LEVEL_FLUSH_SELECT:
            Menu_DisplayFlushStation();
            break;  // 冲洗站子菜单
        case MENU_LEVEL_ZERO_CALIB:
            Draw_CalibSpanAdjust();
            break;
        case MENU_LEVEL_COUNT:
            Draw_CountPage(count_page);
            break;  // 冲洗计数页 [2026-08-06]
    }
}

// Menu_EnterLevel1 - 进入一级菜单：从待机切换到Level1编辑模式
void Menu_EnterLevel1(void) {
    g_menu_status.menu_level=MENU_LEVEL_1;
    g_menu_status.current_param=PARAM_UNIT_FLUSH_TIME;  // 从冲洗时间开始编辑
    g_menu_status.editing=1;blink_timer=0;blink_on=1;f_select_choice=1;  // 重置闪烁和F-SELECT
    g_lcd_dirty=1;
}

// Menu_EnterLevel2 - 进入二级菜单：从待机/Level1长按OK进入Level2编辑模式
void Menu_EnterLevel2(void) {
    g_menu_status.menu_level=MENU_LEVEL_2;
    g_menu_status.current_param=PARAM_MAIN_VALVE_SELECT;  // 从主阀选择开始
    g_menu_status.editing=1;l2_blink_timer=0;l2_blink_on=1;g_lcd_dirty=1;
}

// Menu_NavigateParam - Level1参数导航：冲洗时间→压差设定→冲洗周期→F-SELECT→待机
// F-SELECT YES→进入冲洗站子菜单，NO→保存并返回待机
void Menu_NavigateParam(void) {
    u8 cp;  // 当前参数类型
    cp  = g_menu_status.current_param;
    if(cp==PARAM_FLUSH_SELECT && !g_menu_status.editing) {
        // F-SELECT非编辑状态：根据YES/NO决定去向
        if(f_select_choice) {
            g_menu_status.menu_level=MENU_LEVEL_FLUSH_SELECT;  // YES：进入冲洗站子菜单
            g_menu_status.flush_station_num=1;g_menu_status.editing=0;
        } else {
            Menu_SaveParam();g_menu_status.editing=0;  // NO：保存并进入计数页(压差) [2026-08-06]
            g_menu_status.menu_level=MENU_LEVEL_COUNT;
            count_page=0;
        }
    } else if(g_menu_status.editing) {
        // 编辑状态：保存当前参数，切换到下一个
        Menu_SaveParam();
        if(cp==PARAM_UNIT_FLUSH_TIME){cp=PARAM_PRESSURE_DIFF_SET;}      // 冲洗时间→压差设定
        else if(cp==PARAM_PRESSURE_DIFF_SET){cp=PARAM_FLUSH_CYCLE;}     // 压差设定→冲洗周期
        else if(cp==PARAM_FLUSH_CYCLE){cp=PARAM_FLUSH_SELECT;}          // 冲洗周期→F-SELECT
        else{cp=PARAM_UNIT_FLUSH_TIME;g_menu_status.menu_level=MENU_LEVEL_NONE;}  // F-SELECT→待机
        g_menu_status.current_param=cp;
        if(cp==PARAM_FLUSH_SELECT){g_menu_status.editing=0;f_select_choice=1;}  // F-SELECT不进入编辑
        else{
            g_menu_status.editing=1;blink_timer=0;blink_on=1;  // 其他参数进入编辑
            if(cp==PARAM_FLUSH_CYCLE)flush_cycle_min=(u16)(g_param_config.flush_cycle*60.0f+0.5f);  // 同步分钟数
        }
    }
    g_lcd_dirty=1;
}

// Menu_AdjustParam - 参数调整（ADD=增加，SUB=减少）
// 支持Level1（冲洗时间/压差/周期）、Level2（9个参数）、零点校准
// d=1表示增加，d=0表示减少
void Menu_AdjustParam(u8 d) {
    u8 cp,lvl;  // 当前参数类型、菜单层级
    u16 iv;     // 临时整数值
    float fv;   /* 校准压力调整值 */
    cp  = g_menu_status.current_param;
    lvl = g_menu_status.menu_level;
    // Level2参数调整：每个参数独立处理
    if(lvl==MENU_LEVEL_2){
        l2_blink_timer=0;
        l2_blink_on=1;  // 重置闪烁
        switch(cp){
            case PARAM_MAIN_VALVE_SELECT:
                g_param_config.main_valve_select = d ? 1 : 0;  // YES/NO切换
                Draw_L2_MainValve(1);
                break;
            case PARAM_MAIN_VALVE_DELAY:
                iv=g_param_config.main_valve_delay;
                if(d)
                {
                    if(iv<999)iv++;
                }
                else
                {
                    if(iv>0)iv--;
                }  // 范围0~999秒
                g_param_config.main_valve_delay=iv;
                Draw_L2_MainValveDelay(1);
                break;
            case PARAM_INTERVAL_TIME:
                iv=g_param_config.interval_time;
                if(d)
                {
                    if(iv<999)iv++;
                }
                else
                {
                    if(iv>0)iv--;
                }
                g_param_config.interval_time=iv;
                Draw_L2_IntervalTime(1);
                break;
            case PARAM_PRESSURE_DELAY:
                iv=g_param_config.pressure_delay;
                if(d)
                {
                    if(iv<999)iv++;
                }
                else
                {
                    if(iv>0)iv--;
                }
                g_param_config.pressure_delay=iv;
                Draw_L2_PressureDelay(1);
                break;
            case PARAM_CYCLE_LIMIT:
                iv=g_param_config.cycle_limit;
                if(d)
                {
                    if(iv<99)iv++;
                }
                else
                {
                    if(iv>0)iv--;
                }  // 范围0~99次
                g_param_config.cycle_limit=iv;
                /* [2026-08-01: 修改循环限制时计数归零重新累计。
                   例: 计数=3时改 3→4，计数从0重新开始 → 可再周期冲洗4次，到4又卡住。
                   cycle_limit=0 → 无限次循环(见Control_CheckFlushCycle门控)。
                   手动/压差冲洗均不影响计数，只有修改此参数才刷新计数。] */
                Control_ResetContinuousCount();  // 修改限制时重置计数器，重新累计
                Draw_L2_CycleLimit(1);
                break;
            case PARAM_ALARM_OUTPUT:
                g_param_config.alarm_output = d ? 1 : 0;  // YES/NO切换
                Draw_L2_AlarmOutput(1);
                break;
            case PARAM_ZERO_CALIBRATION:
                g_param_config.zero_calibration = d ? 1 : 0;  // YES/NO切换
                Draw_L2_ZeroCalib(1);
                break;
            case PARAM_STATION_RECOGNITION:
                iv=g_param_config.station_recognition;
                if(d)
                {
                    if(iv<10)iv++;
                }
                else
                {
                    if(iv>1)iv--;
                }  // 范围1~10站
                g_param_config.station_recognition=(u8)iv;
                Draw_L2_StationRecog(1);
                break;
            case PARAM_UNIT_SELECTION:
                g_param_config.unit_selection = d ? 1 : 0;  // BAR/PSI切换
                Draw_L2_UnitSelect(1);
                break;
            default:break;
        }
        return;
    }
    /* 零点校准模式：+/-调整参考压力值 */
    if(lvl == MENU_LEVEL_ZERO_CALIB)
    {
        l2_blink_timer = 0; l2_blink_on = 1;
        fv = g_calib_pressure_ref;
        fv += d ? 10.0f : -10.0f;   /* 步进10kPa */
        if(fv < 10.0f) fv = 10.0f;
        if(fv > 700.0f) fv = 700.0f;
        g_calib_pressure_ref = fv;
        Draw_CalibSpanAdjust();
        return;
    }
    /* 时钟设置模式已移除 [2026-08-03]：右上角为周期冲洗倒计时，无壁钟可设置 */
    // Level1参数调整
    if(lvl!=MENU_LEVEL_1)return;
    blink_timer=0;blink_on=1;  // 重置闪烁
    switch(cp){
        case PARAM_UNIT_FLUSH_TIME:  // 冲洗时间 [2026-08-06]：秒级(<60s)±5秒；分级(≥60s)±30秒；最大9.5分(570s)
            iv=(u16)(g_param_config.unit_flush_time+0.5f);
            if(iv<5)iv=5;  // 最小5秒
            if(iv>=60){if(d)iv=(iv<570)?(iv+30):570;else iv=(iv>60)?(iv-30):55;}  // 分级：±30s，60减→55
            else{if(d)iv=(iv<55)?(iv+5):60;else if(iv>5)iv-=5;}                   // 秒级：±5s，55加→60 [2026-08-06]
            g_param_config.unit_flush_time=(float)iv;
            Draw_UnitFlush();
            break;
        case PARAM_PRESSURE_DIFF_SET:  // 压差设定：步进±0.1，范围0.0~7.0 bar（=传感器量程700kPa）
            {
                int iv10;
                /* 用十分位整数步进，避免浮点累积漂移：0.1在二进制浮点中不可精确表示，
                   连续累加后 1.9+0.1=1.99999… → 显示截断成1.0 [2026-08-03] */
                iv10 = (int)(g_param_config.pressure_diff_set * 10.0f + 0.5f);
                iv10 += d ? 1 : -1;
                if(iv10 < 0) iv10 = 0;
                if(iv10 > 70) iv10 = 70;   /* 上限7.0bar */
                g_param_config.pressure_diff_set = (float)iv10 / 10.0f;
            }
            Draw_PressureSet();
            break;
        case PARAM_FLUSH_CYCLE:  // 冲洗周期 [2026-08-06]：分级(<60分)±5分；时级(≥60分)±1时；5分再减→OFF(0)；OFF加→5分
            if(d){
                /* 加：OFF(0)→5分；<60分 +5分（55→60）；≥60分 +1时 */
                if(flush_cycle_min==0) flush_cycle_min=5;
                else if(flush_cycle_min>=60) flush_cycle_min=(flush_cycle_min<59940UL)?(flush_cycle_min+60):59940UL;
                else flush_cycle_min=(flush_cycle_min<55)?(flush_cycle_min+5):60;
            } else {
                /* 减：5分→OFF(0)；<60分 -5分（55→50）；60→55；≥60分 -1时 */
                if(flush_cycle_min<=5) flush_cycle_min=0;
                else if(flush_cycle_min>=60) flush_cycle_min=(flush_cycle_min==60)?55:(flush_cycle_min-60);
                else flush_cycle_min-=5;
            }
            g_param_config.flush_cycle=(float)flush_cycle_min/60.0f;  // 分钟→小时（OFF时=0.0f）
            Control_ResetCycleTimer();   /* 改周期重置计时起点 [2026-08-06] */
            Draw_FlushCycle();
            break;
        default:break;
    }
}

// Menu_SaveParam - 保存参数到EEPROM（调 Param_Save：先比较、有变化才整扇区擦写）
void Menu_SaveParam(void){Param_Save();}

// ==================== 全屏绘制 ====================

static void Draw_FullScreen(void) {
    u8 v24;
    u8 v0,v1;  // addr0/addr1标签字节（图标按冲洗状态合并）
    /* 不在首行 LCD_ClearAll() — 空白38个地址耗时~3ms，造成每按OK键肉眼可见的黑闪。
       Draw_FullScreen已通过子函数覆盖全部38个地址(0-37)，无需擦除。
       addr14/15存放Level2专用标签，返回待机时必须显式清零。 */
    Draw_CountdownValue();   // 188:88:88：按显示模式画 --:--:-- 或数字倒计时 [2026-08-13]（原Draw_Clock）
    Draw_ActualPressure();
    Draw_UnitFlush();
    Draw_PressureSet();
    Draw_FlushCycle();
    HT1622_WriteData(14, g_pressure_diff_pending ? 0x02 : 0x00); HT1622_WriteData(15,0);  // 清除Level2专用标签(保留压差等待标签 [2026-08-06])
    v24 = 0x00;
    if(Param_GetValue(PARAM_UNIT_FLUSH_TIME)>=60.0f) v24 |= 0x01;
    if(g_param_config.unit_selection == 0) v24 |= 0x04;  // BAR: addr24.2
    v1 = 0x02;  // 默认: addr1.1"压差设定"（"实际压差"addr1.0仅在非冲洗时亮 [2026-08-06]）
    v0 = 0x04;  // addr0.2"冲洗周期"
    if(g_flush_control.active){
        u8 trig;
        trig = g_flush_control.trigger_type;
        if(trig==FLUSH_TRIG_CYCLE)  v1 |= 0x04;  // addr1.2 尖峰图标：周期冲洗
        if(trig==FLUSH_TRIG_MANUAL) v0 |= 0x02;  // addr0.1 手图标：手动冲洗
        if(trig==FLUSH_TRIG_DIFF)   v1 |= 0x08;  // addr1.3 P图标：压差冲洗
    } else {
        v1 |= 0x01;  // 非冲洗：亮 addr1.0"实际压差"（冲洗期间F8.8显示F88站号，此标签熄灭 [2026-08-06]）
    }
    HT1622_WriteData(1,v1); HT1622_WriteData(24,v24);
    // PSI由Draw_PressureSet内部通过addr26.3设置，此处不可写addr26（会覆盖digit段位）
    HT1622_WriteData(0,v0);
}

// Draw_BootTest - 上电关阀测试画面 [2026-08-06]
// 188:88:88 区域（7格，pos6最左→pos1最右）显示 "F{阀号}_OFF"
//   阀1~9: pos6=F, pos5={阀号}, pos4=_, pos3=O, pos2=F, pos1=F  → "F3_OFF"
//   阀10:  pos6=F, pos5=1, pos4=0, pos3=_, pos2=O, pos1=F         → "F10_OFF"
// 段码 Pattern A: F=(ABFG 0x0B, CDE 0x04) O=(0x0E,0x0E) _=(g段: ABFG 0x01, CDE 0x00)
static void Draw_BootTest(void) {
    u8 v;
    LCD_ClearAll();
    v = BootTest_GetValve();
    /* pos6(时十位)=F */
    HT1622_WriteData(12,0x0B);  HT1622_WriteData(13,0x04);
    if(v >= 10) {
        /* pos5(时个位)=1, pos4(分十位)=0 */
        HT1622_WriteData(10,PA_ABFG[1]);  HT1622_WriteData(11,PA_CDE[1]);
        HT1622_WriteData(8,PA_ABFG[0]);   HT1622_WriteData(9,PA_CDE[0]);
        /* pos3(分个位)=_, pos2(秒十位)=O, pos1(秒个位)=F */
        HT1622_WriteData(6,0x01);  HT1622_WriteData(7,0x00);
        HT1622_WriteData(4,0x0E);  HT1622_WriteData(5,0x0E);
        HT1622_WriteData(2,0x0B);  HT1622_WriteData(3,0x04);
    } else {
        /* pos5(时个位)=阀号, pos4(分十位)=_, pos3(分个位)=O, pos2(秒十位)=F, pos1(秒个位)=F */
        HT1622_WriteData(10,PA_ABFG[v]);  HT1622_WriteData(11,PA_CDE[v]);
        HT1622_WriteData(8,0x01);  HT1622_WriteData(9,0x00);
        HT1622_WriteData(6,0x0E);  HT1622_WriteData(7,0x0E);
        HT1622_WriteData(4,0x0B);  HT1622_WriteData(5,0x04);
        HT1622_WriteData(2,0x0B);  HT1622_WriteData(3,0x04);
    }
}

void Menu_DisplayMainMenu(void)  { Draw_FullScreen(); }
// [2026-08-03] 参数 p 保持命名；p=p 引用它避免任何"未使用参数"警告
//   （原 `(void)p;` 是触发 C275 的空表达式，已移除）
void Menu_DisplayParamValue(ParamType_t p) {
    /* 防御：非法参数直接返回（同时引用 p 避免"未使用参数"警告） */
    if((u8)p >= (u8)PARAM_TOTAL_COUNT) return;
    Draw_FullScreen();
}

// ==================== 阀站子菜单 ====================

void Menu_DisplayFlushStation(void) {
    u8 st,t,o;
    bit sel;
    st=g_menu_status.flush_station_num;sel=g_param_config.flush_select[st-1];
    LCD_ClearAll();
    HT1622_WriteData(17,0x01);
    t=st/10;o=st%10;LCD_WriteF88S(1,(t>0)?t:99,1);LCD_WriteF88S(2,o,0);
    if(sel){LCD_Write8888R_Raw(1,0,0);LCD_Write8888R_Raw(2,0,0);LCD_Write8888R_Raw(3,0x07,0x07);LCD_Write8888R_Raw(4,0x07,0x03);}
    else{LCD_Write8888R_Raw(1,0,0);LCD_Write8888R_Raw(2,0x07,0x07);LCD_Write8888R_Raw(3,0x0D,0x02);LCD_Write8888R_Raw(4,0x0D,0x02);}
}
