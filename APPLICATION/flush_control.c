#include "flush_control.h"
#include "appvalve.h"
#include "param_config.h"
#include "menu_system.h"
#include "control_logic.h"
#include "system_init.h"

FlushControl_t xdata g_flush_control = {0};

/* 内部辅助: tick计时 */
static void timer_reset(void)
{
    g_flush_control.tick_start = Sys_GetTick();
}
static u16 timer_elapsed(void)
{
    return (u16)(Sys_GetTick() - g_flush_control.tick_start);
}

/*
 * 双稳态脉冲阀状态机:
 *   开阀/关阀均调用 Valve_PulseOpen/Close() 发送80ms脉冲
 *   脉冲由 Valve_PulseUpdate() 自动管理(80ms后切回稳态GND)
 *   状态机在脉冲期间暂停等待, 脉冲结束后阀靠永磁体自保持
 */

/* 状态机内部标志 */
static bit pulse_stage = 0;   // 当前状态中是否已发过脉冲(0=待发, 1=已发)

void Flush_ControlInit(void)
{
    g_flush_control.state = FLUSH_STATE_IDLE;
    g_flush_control.current_station = 1;
    g_flush_control.tick_start = 0;
    g_flush_control.sequence_index = 0;
    g_flush_control.active = 0;
    g_flush_control.main_valve_opened = 0;
    g_flush_control.main_valve_closed = 0;
    g_flush_control.trigger_type = FLUSH_TRIG_NONE;
    pulse_stage = 0;
}

/* 三个触发入口: 激活冲洗并进入MAIN_VALVE */
static void flush_trigger(ParamType_t counter_type)
{
    g_flush_control.active = 1;
    g_flush_control.state = FLUSH_STATE_MAIN_VALVE;
    g_flush_control.current_station = 1;
    g_flush_control.sequence_index = 0;
    g_flush_control.main_valve_opened = 0;
    g_flush_control.main_valve_closed = 0;
    /* trigger_type 由调用者设置(F_Execute*Trigger在调用前已设好), 此处不可清零 */
    pulse_stage = 0;
    timer_reset();
    g_lcd_dirty = 1;
    Param_IncrementCounter(counter_type);
}

void Flush_ExecuteDiffTrigger(void)
{
    if(!g_flush_control.active) {
        g_flush_control.trigger_type = FLUSH_TRIG_DIFF;
        flush_trigger(PARAM_COUNT_DIFF_TRIGGER);
    }
}

void Flush_ExecuteCycleTrigger(void)
{
    if(!g_flush_control.active) {
        g_flush_control.trigger_type = FLUSH_TRIG_CYCLE;
        flush_trigger(PARAM_COUNT_CYCLE_TRIGGER);
    }
}

void Flush_ExecuteManualTrigger(void)
{
    if(!g_flush_control.active) {
        g_flush_control.trigger_type = FLUSH_TRIG_MANUAL;
        flush_trigger(PARAM_COUNT_MANUAL_TRIGGER);
    }
}

/* 找到一个启用分站并发送开阀脉冲;
   返回1=找到, 0=全部完成 */
static bit find_and_open_next_station(void)
{
    while(g_flush_control.sequence_index < g_param_config.station_recognition) {
        if(g_param_config.flush_select[g_flush_control.sequence_index]) {
            g_flush_control.current_station = g_flush_control.sequence_index + 1;
            Valve_PulseOpen(g_flush_control.current_station);
            timer_reset();
            pulse_stage = 0;
            return 1;
        }
        g_flush_control.sequence_index++;
    }
    /* 所有分站完成 */
    return 0;
}

/* 仅检查下一个分站是否存在(不发脉冲), 用于CLOSING中超前判断 */
static bit find_and_open_next_station_check(void)
{
    u8 idx;
    idx = g_flush_control.sequence_index;
    while(idx < g_param_config.station_recognition) {
        if(g_param_config.flush_select[idx]) return 1;
        idx++;
    }
    return 0;
}

void Flush_MainProcess(void)
{
    u16 elapsed;

    if(!g_flush_control.active) return;

    /* 如果当前正在发送脉冲，等待脉冲完成后再继续 */
    if(Valve_PulseIsActive()) return;

    switch(g_flush_control.state) {

    /* ====== MAIN_VALVE: 主阀处理 ====== */
    case FLUSH_STATE_MAIN_VALVE:

        if(g_param_config.main_valve_select) {

            if(!g_flush_control.main_valve_opened) {
                /* 发送开阀脉冲(主阀=10) */
                Valve_PulseOpen(10);
                g_flush_control.main_valve_opened = 1;
                timer_reset();
                return;
            }

            /* 脉冲已完成, 等待总延时:
               PULSE_WIDTH_TICKS (阀通电80ms) + main_valve_delay秒 */
            elapsed = timer_elapsed();
            if(elapsed >= (PULSE_WIDTH_TICKS + g_param_config.main_valve_delay * TICK_PER_SEC)) {
                g_flush_control.state = FLUSH_STATE_STATION_FLUSH;
                timer_reset();
            }

        } else {
            /* 无主阀: 直接进入分站冲洗 */
            g_flush_control.state = FLUSH_STATE_STATION_FLUSH;
            timer_reset();
        }
        break;

    /* ====== STATION_FLUSH: 找下一个启用分站 → 发送开阀脉冲 ====== */
    case FLUSH_STATE_STATION_FLUSH:

        if(!pulse_stage) {
            /* 找下一个启用分站并发送开阀脉冲 */
            if(!find_and_open_next_station()) {
                /* 所有分站完成 */

                /* 如果用了主阀: 发关阀脉冲并等待其完成(MAIN_CLOSE状态)
                   [修复2026-07-17: 旧代码在Valve_PulseClose(10)后立即Stop_All()，
                    把刚发出的关阀脉冲几µs内取消，主阀10从未真正关闭] */
                if(g_flush_control.main_valve_opened) {   /* 关阀条件只依赖"本循环是否打开过主阀"，
                                              冲洗中途关掉main_valve_select也必须关阀 [2026-08-03] */
                    Valve_PulseClose(10);
                    g_flush_control.main_valve_closed = 1;
                    g_flush_control.state = FLUSH_STATE_MAIN_CLOSE;
                    timer_reset();
                    g_lcd_dirty = 1;
                    return;
                }

                /* 无主阀: 直接结束循环 */
                g_flush_control.state = FLUSH_STATE_IDLE;
                g_flush_control.active = 0;
                g_flush_control.trigger_type = FLUSH_TRIG_NONE;  // 立即清除，LCD图标即时消失
                g_flush_control.main_valve_opened = 0;
                g_flush_control.main_valve_closed = 0;
                Stop_All();
                g_lcd_dirty = 1;
                return;
            }
            /* find_and_open_next_station 已发送开阀脉冲并重置timer */
            pulse_stage = 1;
            return;
        }

        /* 脉冲已完成, 进入COMPLETE等待冲洗时间 */
        g_flush_control.state = FLUSH_STATE_COMPLETE;
        timer_reset();
        pulse_stage = 0;
        break;

    /* ====== COMPLETE: 等待冲洗时间(阀靠永磁体自保持, 不供电!) ====== */
    case FLUSH_STATE_COMPLETE:

        elapsed = timer_elapsed();
        if(elapsed >= g_param_config.unit_flush_time * TICK_PER_SEC) {
            /* 冲洗结束, 发送关阀脉冲 */
            Valve_PulseClose(g_flush_control.current_station);
            g_flush_control.state = FLUSH_STATE_CLOSING;
            timer_reset();
        }
        break;

    /* ====== CLOSING: 等待关阀脉冲完成 ====== */
    case FLUSH_STATE_CLOSING:

        /* Valve_PulseIsActive() 已在函数开头检查, 到这里说明脉冲已完成 */
        /* 关阀完毕 — 检查是否还有下一个分站 */
        g_flush_control.sequence_index++;
        if(!find_and_open_next_station_check()) {
            /* 最后一个分站: 跳过INTERVAL, 直接结束 */
            if(g_flush_control.main_valve_opened) {   /* 关阀条件只依赖"本循环是否打开过主阀"，
                                              冲洗中途关掉main_valve_select也必须关阀 [2026-08-03] */
                Valve_PulseClose(10);
                g_flush_control.main_valve_closed = 1;
                g_flush_control.state = FLUSH_STATE_MAIN_CLOSE;
                timer_reset();
                g_lcd_dirty = 1;
                return;
            }
            g_flush_control.state = FLUSH_STATE_IDLE;
            g_flush_control.active = 0;
            g_flush_control.trigger_type = FLUSH_TRIG_NONE;
            g_flush_control.main_valve_opened = 0;
            g_flush_control.main_valve_closed = 0;
            Stop_All();
            g_lcd_dirty = 1;
            return;
        }
        /* 还有下一个分站: 进入间隔等待 */
        g_flush_control.state = FLUSH_STATE_INTERVAL;
        timer_reset();
        break;

    /* ====== INTERVAL: 分站间隔等待 ====== */
    case FLUSH_STATE_INTERVAL:

        elapsed = timer_elapsed();
        if(elapsed >= g_param_config.interval_time * TICK_PER_SEC) {
            /* sequence_index 已在 CLOSING 中递增，直接找下一分站 */
            g_flush_control.state = FLUSH_STATE_STATION_FLUSH;
            timer_reset();
        }
        break;

    /* ====== MAIN_CLOSE: 等待主阀关阀脉冲完成后结束循环 ====== */
    case FLUSH_STATE_MAIN_CLOSE:

        /* Valve_PulseIsActive() 已在函数开头检查, 到这里说明关阀脉冲已完整送达
           (Valve_PulseUpdate已恢复稳态: NET60=GND, 所有阀=GND) */
        g_flush_control.state = FLUSH_STATE_IDLE;
        g_flush_control.active = 0;
        g_flush_control.trigger_type = FLUSH_TRIG_NONE;  // 立即清除
        g_flush_control.main_valve_opened = 0;
        g_flush_control.main_valve_closed = 0;
        Stop_All();   // 稳态兜底
        g_lcd_dirty = 1;
        break;

    } /* switch */
}

bit Flush_IsActive(void)
{
    return g_flush_control.active;
}

/* ====== 上电关阀测试（逐路关阀脉冲） [2026-08-06] ======
 * 上电进入主循环后，对阀1~station_recognition逐路发100ms关阀脉冲，
 * 确认双稳态阀能正常关闭。期间LCD显示 F{阀号}_OFF（由menu_system绘制）。
 * 每阀脉冲完成后停留 BOOT_TEST_HOLD_TICKS（0.5秒），让操作者看清阀号与关阀LED。
 * [2026-08-07] P1.0 升压由 Valve_PulseClose 随脉冲自动开启（不再常开）——
 *   BootTest 每个关阀脉冲内部都会先拉高 P1.0 等升压稳定再打阀，无需外部使能。
 * 喂狗由主循环每圈进行，本状态机不阻塞。
 */
#define BOOT_TEST_HOLD_TICKS  (TICK_PER_SEC / 2)   /* 每阀显示停留0.5秒 [2026-08-06] */
static bit boot_test_active = 0;
static u8  boot_test_valve  = 1;
static bit boot_test_stage  = 0;   /* 0=待发脉冲, 1=等待脉冲完成+停留 */
static u16 boot_test_tick_start = 0;

void BootTest_Init(void)
{
    if(g_param_config.station_recognition >= 1) {
        boot_test_active = 1;
        boot_test_valve = 1;
        boot_test_stage = 0;
        boot_test_tick_start = 0;
    }
}

void BootTest_Process(void)
{
    u16 elapsed;

    if(!boot_test_active) return;

    if(!boot_test_stage) {
        if(Valve_PulseIsActive()) return;      /* 安全等待前一脉冲结束 */
        Valve_PulseClose(boot_test_valve);     /* 100ms关阀脉冲 */
        boot_test_stage = 1;
        boot_test_tick_start = Sys_GetTick();
        g_lcd_dirty = 1;
        return;
    }
    if(Valve_PulseIsActive()) return;          /* 等待当前脉冲完成 */

    /* 脉冲已完成：停留0.5秒再推进，让操作者看清阀号与LED [2026-08-06] */
    elapsed = (u16)(Sys_GetTick() - boot_test_tick_start);
    if(elapsed < BOOT_TEST_HOLD_TICKS) return;

    boot_test_valve++;
    if(boot_test_valve > g_param_config.station_recognition) {
        boot_test_active = 0;                  /* 测试完成 */
        Stop_All();
        g_lcd_dirty = 1;
    } else {
        boot_test_stage = 0;                   /* 下一个阀 */
    }
}

bit BootTest_IsActive(void)
{
    return boot_test_active;
}

u8 BootTest_GetValve(void)
{
    return boot_test_valve;
}
