#ifndef __APPVALVE_H
#define __APPVALVE_H

#include "config.h"

/* 双稳态脉冲阀控制模块
 * Green Plus DC 12-24V 双稳态自保持电磁头驱动
 *
 * 电路拓扑: AO4616半桥输出(正端) → 线圈 → NET60(公共负端)
 *
 * 三种控制状态:
 *   稳态:         所有阀输出=GND, NET60=GND → 线圈两端0V → 永磁体自保持
 *   开阀脉冲(80ms): 目标阀=HV, NET60=GND → 正向电流 → 阀门打开
 *   关阀脉冲(80ms): 目标阀=GND, NET60=HV  → 反向电流 → 阀门关闭
 *   [2026-08-08] 阀通电 160ms→80ms（PULSE_WIDTH_TICKS 8→4，<100ms），升压预热 100ms→200ms
 *
 * NET60由P6.2/P6.3控制(含死区时间防止Q15穿通)
 */

// === 初始化 ===
void Valve_Init(void);

// === 单阀底层控制（含死区） ===
void Valve_SetHV(u8 n);      // 输出HV(~15V): P-ch=ON, N-ch=OFF
void Valve_SetGND(u8 n);     // 输出GND:     P-ch=OFF, N-ch=ON
void Valve_SetHiZ(u8 n);     // 高阻态:       P-ch=OFF, N-ch=OFF

// === 批量控制 ===
void Valve_AllGND(void);     // 10个阀全部输出GND（稳态）

// === NET60公共母线控制（含死区） ===
void Net60_SetGND(void);     // NET60=GND (P6.3=1, P6.2=0)
void Net60_SetHV(void);      // NET60=HV  (P6.2=1, P6.3=0)

// === 脉冲管理（80ms脉冲） ===
// [2026-08-07] P8.0(升压EN) 随脉冲开闭：Valve_PulseOpen/Close() 先拉高 P8.0
//   等待 BOOST_STARTUP_TICKS(200ms) 升压稳定后切换阀，脉冲到期(共280ms)回收时 P8.0=0。
//   稳态/Stop_All/ISR超时下 P8.0 恒为低——只有脉冲期间升压才工作。
// [2026-08-08] 时序放宽：NET59 升压建立慢（软启动+输出电容充电），原60/100ms预热不足，
//   第一次电磁头开启时 NET59 未站稳→线圈把输出电容拉到~8V→升压来不及追回15V→只有12-13V。
//   故 BOOST_STARTUP_TICKS 3→5→10(预热200ms)、PULSE_WIDTH_TICKS 5→8→4(通电80ms)，
//   让升压先空载升到15V再接线圈；阀通电缩短至<100ms，减轻升压持续压力。
#define PULSE_WIDTH_TICKS  2  // 2 × 20ms = 40ms（双稳态电磁头翻转仅需~10-30ms；短脉冲减少3.3V轨持续吃~2.8A电流时间→防BOR复位 [2026-08-20]）
#define BOOST_STARTUP_TICKS  15  // 15 × 20ms = 300ms（升压输出电容充满→翻转电流峰值够→咔哒清脆；200ms预热不足致咔哒弱 [2026-08-20]）

void Valve_PulseOpen(u8 n);      // 请求开阀脉冲（拉高P8.0→200ms后阀切换→80ms→回收关boost）
void Valve_PulseClose(u8 n);     // 请求关阀脉冲（拉高P8.0→200ms后阀切换→80ms→回收关boost）
bit  Valve_PulseIsActive(void);  // 是否有脉冲正在进行中（含boost等待阶段）
void Valve_PulseUpdate(void);    // 主循环调用：驱动boost等待→阀切换→脉冲到期切回稳态+关boost

// === ISR侧脉冲超时保险 ===
// 仅由Timer0 ISR每tick调用一次；主循环卡死>300ms时强制NET60高阻切断线圈电流
// （主循环代码禁止调用——会与ISR形成overlay/竞态问题）
void Valve_PulseFailsafeISR(void);

// === 关闭所有阀（稳态） ===
void Stop_All(void);         // 全部切GND + NET60=GND + 取消脉冲
// [2026-08-03] Valve_BootCloseAll 已按用户要求移除（上电不再对阀站发关阀脉冲测试）

// === 兼容旧接口（内部改为脉冲模式，不再持续通电） ===
// [2026-08-03] Valve_RunCurrentValve / Run_Current_Valve / Get_Current_Valve /
//   Set_Current_Valve 已移除（待机"阀切换"无消费方，dead code）。

#endif
