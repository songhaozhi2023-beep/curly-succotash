#include "appvalve.h"
#include "config.h"
#include "system_init.h"    // g_sys_tick

// ====== 引脚定义: NET60控制 ======
sbit NET60_N = P6^3;   // NET60=GND 时 HIGH (N-ch ON)
sbit NET60_P = P6^2;   // NET60=HV 时 HIGH (P-ch ON via Q26 NPN)

// ====== 阀端口静态值定义（推挽输出） ======
// [4G移植-2026-08-17修正] 4G板实际阀引脚（网标沿用基础版旧名，实为PB口）：
// 阀1: P7.0(P-ch) P7.1(N-ch), 阀2: P7.2(P-ch) P7.3(N-ch), 阀3: P7.4(P-ch) P7.5(N-ch), 阀4: P7.6(P-ch) P7.7(N-ch)
// 阀5: PB.7(P-ch) PB.6(N-ch)   ← 基础版是P4.5/P4.6
// 阀6: P0.0(P-ch) PB.5(N-ch)   ← 基础版是P0.0/P0.1
// 阀7: PB.4(P-ch) P0.3(N-ch)   ← 基础版是P0.2/P0.3
// 阀8: P0.4(P-ch) P0.5(N-ch), 阀9: P0.6(P-ch) P0.7(N-ch), 阀10: P6.0(P-ch) P6.1(N-ch)
// PB 为 STC32G 扩展端口(xdata映射)：PBOUT/PBM0/PBM1，无 sbit，用掩码

// ====== 全局状态 ======

// 脉冲相位状态机（2026-08-07 起 P1.0 升压随脉冲开闭）
//   IDLE  : 无脉冲，P1_0=0（升压关闭）
//   BOOST : P1_0=1，等待升压稳定（BOOST_STARTUP_TICKS），尚未切换阀
//   ACTIVE: 阀已切换，80ms 脉冲进行中
typedef enum {
    PULSE_PHASE_IDLE = 0,
    PULSE_PHASE_BOOST,
    PULSE_PHASE_ACTIVE
} PulsePhase_t;

static PulsePhase_t pulse_phase = PULSE_PHASE_IDLE;  // 取代原 pulse_running 位
static bit  pulse_is_open = 0;       // 1=开阀脉冲, 0=关阀脉冲
static u8   pulse_valve = 0;         // 当前脉冲的目标阀号(1-10)
static u16  pulse_start_tick = 0;    // 脉冲起始(BOOST)时的g_sys_tick快照

// BOOST_STARTUP_TICKS 定义于 appvalve.h（公共接口）
// P1.0 拉高后等待升压(NET59)稳定的 tick 数 [2026-08-07]
// FP6291 EN 拉高到输出稳定需软启动+输出电容充电，取 200ms 保守值（[2026-08-08] 原60/100ms不足，
//   第一次电磁头开启时 NET59 未站稳→线圈把输出电容拉到~8V→升压追不回来→仅12-13V）。
// 每个脉冲总时长 = BOOST_STARTUP_TICKS + PULSE_WIDTH_TICKS = 200ms + 80ms。

// ISR侧脉冲超时保险（由Timer0 ISR每20ms调用，见Valve_PulseFailsafeISR）
// 正常脉冲 15+2=17 tick(340ms)内由主循环Valve_PulseUpdate回收；若计数超过阈值说明主循环卡死
// [2026-08-20] 阈值 24(480ms)：BOOST_STARTUP_TICKS 15 + PULSE_WIDTH_TICKS 2 = 17 tick(340ms)，
//   留足余量防止正常脉冲误触发保险
#define PULSE_FAILSAFE_TICKS  24     // 480ms：正常脉冲 15+2=17 tick(340ms)，留足余量 [2026-08-20]
static volatile u8 pulse_isr_ticks = 0;  // ISR侧通电时长计数（主循环armed时清零，单字节写原子）

// ====== 初始化 ======
void Valve_Init(void)
{
    // ★ 开机保护：先写端口锁存器为 LOW，再配置推挽输出
    // STC15 上电复位后端口输出锁存器 = 0xFF，若先配推挽会导致 P-ch/N-ch 同时导通→短路
    // 必须先清锁存器（此时端口还是弱上拉/高阻，不影响电路），然后再改模式
    P0 = 0x00;  P4 = 0x00;  P6 = 0x00;  P7 = 0x00;
    XSFR_ON(); PBOUT = 0x00; XSFR_OFF();   // [4G修正] 阀5/6/7在PB口(PB.7/PB.6/PB.5/PB.4)，先清零防直通（EAXFR 切换访问）

    // P0端口: VALVE6(P-ch) VALVE7(N-ch) VALVE8 VALVE9 (P0.0/P0.3/P0.4/P0.5/P0.6/P0.7)
    P0M1 = 0x00; P0M0 = 0xFF;

    // P6端口: VALVE10 (P6.0-P6.1) + NET60 (P6.2-P6.3)
    P6M1 &= ~0x0F; P6M0 |= 0x0F;

    // P7端口: VALVE1-4 (P7.0-P7.7)
    P7M1 = 0x00; P7M0 = 0xFF;

    // [4G修正] PB端口: VALVE5(PB.7/PB.6) VALVE6(N-ch=PB.5) VALVE7(P-ch=PB.4)
    XSFR_ON();
    PBM1 &= ~0xF0; PBM0 |= 0xF0;   // PB.4-PB.7 推挽输出（扩展端口，EAXFR 切换访问）
    XSFR_OFF();

    Stop_All();  // 稳态: 所有阀=GND, NET60=GND
}

// ====== NET60 安全切换（150µs 死区，防止 Q15 穿通） ======
// Q15 为 N+P 双MOSFET: NET60_N→N-ch(R66 47K下拉，关断~68µs), NET60_P→Q26(NPN)→P-ch
// N-ch关断最慢(3τ ≈ 68µs)，死区取150µs留足余量。若死区不足→两端短暂同时导通→15V直通GND→烧Q15。
// 内部循环: volatile u16 每次~3µs (12T模式)，50次 ≈ 150µs

static void net60_deadband(void)
{
    volatile u16 db;
    NET60_N = 0;           // 先关N-ch (慢: 关断需~68µs)
    NET60_P = 0;           // 先关P-ch (快: 关断需~4µs)
    db = 60; while(db) { db--; }   // ~180µs 死区 @24MHz 12T（与基础版相同）
}

void Net60_SetGND(void)
{
    NET60_P = 0;           // 先关P-ch (快)
    net60_deadband();      // 等N-ch完全关断
    NET60_N = 1;           // N-ch ON → NET60=GND
}

void Net60_SetHV(void)
{
    NET60_N = 0;           // 先关N-ch (慢)
    net60_deadband();      // 等N-ch完全关断
    NET60_P = 1;           // P-ch ON → NET60=HV
}

// ====== 单阀底层三态控制 ======

void Valve_SetHV(u8 n)
{
    // ★ 穿通安全顺序：先关N-ch，再开P-ch
    //   若从GND态(HV=0,N-ch=1)转换，先开P-ch会留下P/N同时导通的µs级穿通窗口
    //   (NET59→GND短路电流)。其他SetGND/SetHiZ已是安全顺序，仅此处修正。
    switch(n) {
        case 1:  P7 &= ~0x02; P7 |= 0x01; break;  // P7.1=OFF(N-ch=0)先关, P7.0=HV(P-ch=1)再开
        case 2:  P7 &= ~0x08; P7 |= 0x04; break;
        case 3:  P7 &= ~0x20; P7 |= 0x10; break;
        case 4:  P7 &= ~0x80; P7 |= 0x40; break;
        case 5:  XSFR_ON(); PBOUT &= ~0x40; PBOUT |= 0x80; XSFR_OFF(); break;  // PB.6(N-ch=0)先关, PB.7(P-ch=1)再开
        case 6:  XSFR_ON(); PBOUT &= ~0x20; XSFR_OFF(); P0 |= 0x01; break;     // PB.5(N-ch=0)先关, P0.0(P-ch=1)再开
        case 7:  P0 &= ~0x08; XSFR_ON(); PBOUT |= 0x10; XSFR_OFF(); break;     // P0.3(N-ch=0)先关, PB.4(P-ch=1)再开
        case 8:  P0 &= ~0x20; P0 |= 0x10; break;
        case 9:  P0 &= ~0x80; P0 |= 0x40; break;
        case 10: P6 &= ~0x02; P6 |= 0x01; break;
    }
}

void Valve_SetGND(u8 n)
{
    switch(n) {
        case 1:  P7 &= ~0x01; P7 |= 0x02; break;  // P7.0=OFF, P7.1=GND(N-ch=1)
        case 2:  P7 &= ~0x04; P7 |= 0x08; break;
        case 3:  P7 &= ~0x10; P7 |= 0x20; break;
        case 4:  P7 &= ~0x40; P7 |= 0x80; break;
        case 5:  XSFR_ON(); PBOUT &= ~0x80; PBOUT |= 0x40; XSFR_OFF(); break;  // PB.7=OFF, PB.6=GND(N-ch=1)
        case 6:  P0 &= ~0x01; XSFR_ON(); PBOUT |= 0x20; XSFR_OFF(); break;     // P0.0=OFF, PB.5=GND(N-ch=1)
        case 7:  XSFR_ON(); PBOUT &= ~0x10; XSFR_OFF(); P0 |= 0x08; break;     // PB.4=OFF, P0.3=GND(N-ch=1)
        case 8:  P0 &= ~0x10; P0 |= 0x20; break;
        case 9:  P0 &= ~0x40; P0 |= 0x80; break;
        case 10: P6 &= ~0x01; P6 |= 0x02; break;
    }
}

void Valve_SetHiZ(u8 n)
{
    switch(n) {
        case 1:  P7 &= ~0x03; break;  // P7.0=OFF, P7.1=OFF
        case 2:  P7 &= ~0x0C; break;
        case 3:  P7 &= ~0x30; break;
        case 4:  P7 &= ~0xC0; break;
        case 5:  XSFR_ON(); PBOUT &= ~0xC0; XSFR_OFF(); break;  // PB.7=OFF, PB.6=OFF
        case 6:  P0 &= ~0x01; XSFR_ON(); PBOUT &= ~0x20; XSFR_OFF(); break;  // P0.0=OFF, PB.5=OFF
        case 7:  XSFR_ON(); PBOUT &= ~0x10; XSFR_OFF(); P0 &= ~0x08; break;  // PB.4=OFF, P0.3=OFF
        case 8:  P0 &= ~0x30; break;
        case 9:  P0 &= ~0xC0; break;
        case 10: P6 &= ~0x03; break;
    }
}

// ====== 批量控制 ======

void Valve_AllGND(void)
{
    u8 i;
    for(i = 1; i <= 10; i++) {
        Valve_SetGND(i);
    }
}

// [2026-08-03] Valve_AllHiZ() 已移除：无调用方（关阀脉冲的非目标阀在Valve_PulseClose内逐个
//   Valve_SetHiZ，无需批量Hi-Z助手），消除 L16 UNCALLED SEGMENT 警告。

// ====== 脉冲管理 ======

// ★ 脉冲生命周期绑定 P1.0 升压 [2026-08-07]
//   Valve_PulseOpen/Close() 只"请求"脉冲：记录目标+拉高 P1.0 → BOOST 阶段。
//   实际阀切换延迟到 P1.0 拉高后 BOOST_STARTUP_TICKS(200ms) 由 Valve_PulseUpdate() 执行。
//   脉冲到期回收时 P1_0=0，升压关闭。稳态下 P1_0 恒为低。

void Valve_PulseOpen(u8 n)
{
    if(n < 1 || n > 10) return;

    // 有脉冲进行中（含 boost 等待）：忽略并兜底回稳态
    if(pulse_phase != PULSE_PHASE_IDLE) {
        Stop_All();
        return;
    }

    pulse_isr_ticks = 0;   // 重置ISR超时保险计数（须在armed前，单字节写原子）

    pulse_is_open = 1;
    pulse_valve = n;
    pulse_phase = PULSE_PHASE_BOOST;
    pulse_start_tick = Sys_GetTick();
    BOOST_EN_HIGH();              // 拉高升压使能(P8.0) → 升压启动，NET59 升压到 ~15V
    // 实际阀切换(目标阀=HV, NET60=GND)由 Valve_PulseUpdate 在 BOOST 期满后执行
}

void Valve_PulseClose(u8 n)
{
    if(n < 1 || n > 10) return;

    // 有脉冲进行中（含 boost 等待）：忽略并兜底回稳态
    if(pulse_phase != PULSE_PHASE_IDLE) {
        Stop_All();
        return;
    }

    pulse_isr_ticks = 0;   // 重置ISR超时保险计数（须在armed前，单字节写原子）

    pulse_is_open = 0;
    pulse_valve = n;
    pulse_phase = PULSE_PHASE_BOOST;
    pulse_start_tick = Sys_GetTick();
    BOOST_EN_HIGH();              // 拉高升压使能(P8.0) → 升压启动，NET59 升压到 ~15V
    // 实际阀切换(目标阀=GND, NET60=HV)由 Valve_PulseUpdate 在 BOOST 期满后执行
}

bit Valve_PulseIsActive(void)
{
    return (pulse_phase != PULSE_PHASE_IDLE);
}

// ★ 脉冲相位状态机（2026-08-07 起 P1.0 升压随脉冲开闭）
//   Valve_PulseOpen/Close() 只"请求"脉冲：记录目标+拉高 P1.0 → BOOST 阶段。
//   实际阀切换延迟到 P1.0 拉高后 BOOST_STARTUP_TICKS(200ms) 由 Valve_PulseUpdate() 执行。
//   脉冲到期回收时 P1_0=0，升压关闭。稳态下 P1_0 恒为低。
void Valve_PulseUpdate(void)
{
    u16 elapsed;
    u8 i;

    if(pulse_phase == PULSE_PHASE_IDLE) return;

    elapsed = (u16)(Sys_GetTick() - pulse_start_tick);

    if(pulse_phase == PULSE_PHASE_BOOST) {
        // 升压稳定等待期：期满后执行实际阀切换（原 Valve_PulseOpen/Close 的切换体）
        if(elapsed >= BOOST_STARTUP_TICKS) {
            if(pulse_is_open) {
                // 开阀：目标阀=HV, 其他阀=GND, NET60=GND
                for(i = 1; i <= 10; i++) {
                    if(i == pulse_valve)
                        Valve_SetHV(i);
                    else
                        Valve_SetGND(i);
                }
                Net60_SetGND();
            } else {
                // 关阀：目标阀=GND, 其他阀=Hi-Z, NET60=HV
                for(i = 1; i <= 10; i++) {
                    if(i == pulse_valve)
                        Valve_SetGND(i);
                    else
                        Valve_SetHiZ(i);
                }
                Net60_SetHV();
            }
            pulse_phase = PULSE_PHASE_ACTIVE;
        }
        return;
    }

    // PULSE_PHASE_ACTIVE：脉冲到期回收
    // elapsed 从 BOOST 起点算 → 总时长 = BOOST_STARTUP_TICKS + PULSE_WIDTH_TICKS
    if(elapsed >= (u16)(BOOST_STARTUP_TICKS + PULSE_WIDTH_TICKS)) {
        // ★ 先切 NET60 到 GND，再置所有阀为 GND
        // 否则关阀脉冲到期时 NET60 还是 HV，Valve_AllGND() 会把所有阀拉到 GND
        // → 所有线圈同时导通 (NET60=HV→线圈→GND) → 电源短路 → MCU 掉电
        Net60_SetGND();
        Valve_AllGND();
        BOOST_EN_LOW();             // ★ 关闭升压(P8.0)：只有脉冲期间为高
        pulse_phase = PULSE_PHASE_IDLE;
        pulse_valve = 0;
    }
}

// ====== ISR侧脉冲超时保险（仅由Timer0 ISR调用，主循环禁止调用！） ======
// 用途：主循环因掉压跑飞/卡死而停止运行时，端口锁存器会把线圈冻结在通电状态
//   （2026-07-16实测事故：阀2关阀脉冲期间MCU跑飞→NET60=HV+阀2=GND永久保持→
//     线圈持续通电→电源被拖到限流点4.1V，直到断电）
// 原理：任何线圈电流的回路都经过NET60；把NET60_P/N全关（高阻）即可切断
//   开/关两种极性的电流，且只做"关断"操作，无穿通风险、无需死区。
// 触发条件（任一满足则计数，连续>300ms才动作）：
//   pulse_phase != IDLE      —— 正常脉冲(含boost等待)从未超过~170ms，超时=主循环死
//   NET60_P=1                —— 关阀脉冲电气特征（防相位标志被踩的兜底）
//   任意阀P-ch输出=1         —— 开阀脉冲电气特征（P7/P0奇数位、PB.7/PB.4、P6.0）[4G修正]
// 注意：本函数不恢复稳态、不清pulse_phase——若主循环复活，Valve_PulseUpdate
//   会看到elapsed巨大而走正常回收路径；若主循环死透，由看门狗复位收尾。
// 2026-08-07：超时同时切 P1_0=0 —— 任何卡死都强制升压断电，杜绝 NET59 残留15V带电母线。
// C51: 无auto变量（避免overlay冲突），仅访问static/sbit，可安全从ISR调用。
void Valve_PulseFailsafeISR(void)
{
    u8 pbout_val;                    // PB口P-ch导通标志（PB.7|PB.4 = 0x90）

    XSFR_ON();                       // [EAXFR修正] 读扩展端口(PBOUT)前切换 EAXFR
    pbout_val = (PBOUT & 0x90);
    XSFR_OFF();                      // 立即恢复 0，避免返回后主循环 xdata 错接

    if(pulse_phase != PULSE_PHASE_IDLE || NET60_P
       || (P7 & 0x55) || (P0 & 0x55) || pbout_val || (P6 & 0x01)) {
        if(pulse_isr_ticks < 255) pulse_isr_ticks++;
        if(pulse_isr_ticks > PULSE_FAILSAFE_TICKS) {
            NET60_P = 0;   // 只关不开：NET60切高阻，所有线圈电流回路断开
            NET60_N = 0;
            BOOST_EN_LOW();      // ★ 同时关闭升压(P8.0)，宏已内置 EAXFR 切换
        }
    } else {
        pulse_isr_ticks = 0;
    }
}

// ====== Stop_All ======
void Stop_All(void)
{
    pulse_phase = PULSE_PHASE_IDLE;
    pulse_valve = 0;

    Net60_SetGND();      // ★ 先切 NET60 到 GND，再把阀拉到 GND（防止短路）
    Valve_AllGND();
    BOOST_EN_LOW();            // ★ 关闭升压(P8.0)：稳态下恒为低
}

// ====== 兼容旧接口（内部改为脉冲模式） ======
// 这些函数不再持续通电，改为发送80ms脉冲后由Valve_PulseUpdate自动回收

// [2026-08-03] 移除 dead code: current_valve / Get_Current_Valve / Set_Current_Valve /
//   Run_Current_Valve / Valve_RunCurrentValve —— 待机ADD/SUB的"阀切换"无任何消费方
//   （屏幕不显示阀号、也不驱动任何阀），已随待机按键处理一起移除。
