#ifndef __FLUSH_CONTROL_H
#define __FLUSH_CONTROL_H

#include "config.h"
#include "param_config.h"

/* 双稳态脉冲阀反冲洗状态机
 *
 * 适配 Green Plus DC 双稳态自保持电磁头:
 *   - 开阀: 80ms脉冲 (NET60=GND, 阀=HV)
 *   - 关阀: 80ms脉冲 (NET60=HV, 阀=GND)
 *   - 等待: 线圈两端皆为GND, 永磁体自保持, 不耗电
 *
 * 状态:
 *   IDLE          → 空闲, 等待触发
 *   MAIN_VALVE    → 发开阀脉冲(主阀) → 等待主阀延时 → 进入冲洗
 *   STATION_FLUSH → 找到下一个启用分站 → 发开阀脉冲 → 立即进入COMPLETE
 *   COMPLETE      → 不供电, 仅等待冲洗时间(永磁体自保持阀门开启)
 *   CLOSING       → 发关阀脉冲(当前分站), 脉冲结束后进入INTERVAL
 *   INTERVAL      → 等待分站间隔时间 → 下一分站或结束
 *   MAIN_CLOSE    → 发关阀脉冲(主阀10), 等脉冲完成后才结束整个循环
 *                   [2026-07-17新增: 旧代码发完主阀关阀脉冲立即Stop_All()，
 *                    脉冲仅持续数µs，主阀永远关不上]
 */

typedef enum {
    FLUSH_STATE_IDLE = 0,
    FLUSH_STATE_MAIN_VALVE,
    FLUSH_STATE_STATION_FLUSH,
    FLUSH_STATE_COMPLETE,
    FLUSH_STATE_CLOSING,
    FLUSH_STATE_INTERVAL,
    FLUSH_STATE_MAIN_CLOSE
} FlushState_t;

/* Flush trigger source (recorded for LCD status icons) */
#define FLUSH_TRIG_NONE    0
#define FLUSH_TRIG_MANUAL  1
#define FLUSH_TRIG_CYCLE   2
#define FLUSH_TRIG_DIFF    3

typedef struct {
    FlushState_t state;
    u8 current_station;
    u16 tick_start;
    u8 sequence_index;
    u8 active;
    u8 main_valve_opened;
    u8 main_valve_closed;      // 主阀是否已发关阀脉冲
    u8 trigger_type;           // FLUSH_TRIG_xxx: 本次冲洗的触发来源
} FlushControl_t;

extern FlushControl_t xdata g_flush_control;

void Flush_ControlInit(void);
void Flush_ExecuteDiffTrigger(void);
void Flush_ExecuteCycleTrigger(void);
void Flush_ExecuteManualTrigger(void);
void Flush_MainProcess(void);
bit  Flush_IsActive(void);

/* 上电关阀测试 [2026-08-06] */
void BootTest_Init(void);           /* 上电调用：启动逐路关阀测试（范围=站数识别数） */
void BootTest_Process(void);        /* 主循环调用：逐路发关阀脉冲（状态机，不阻塞） */
bit  BootTest_IsActive(void);       /* 1=测试进行中 */
u8   BootTest_GetValve(void);       /* 当前测试的阀号（显示用） */

#endif
