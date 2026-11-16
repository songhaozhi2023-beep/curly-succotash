#ifndef __CONTROL_LOGIC_H  // 头文件保护宏，防止重复包含
#define __CONTROL_LOGIC_H  // 定义头文件标识符

#include "config.h"       // 包含系统配置头文件，定义基本数据类型和系统配置
#include "param_config.h" // 包含参数配置头文件，定义参数类型枚举

extern float g_actual_pressure;  // 实际压差只读值 (全局, 默认0.0)

/* 压差超限等待标志：压力≥设定但延时未满（显示"压差延迟"标签、倒计时切到压差延时）[2026-08-06] */
extern bit g_pressure_diff_pending;

/* 188倒计时显示模式（Control_GetDisplayMode 返回值）[2026-08-06] */
#define DISP_MODE_CYCLE      0   /* 周期倒计时（待机） */
#define DISP_MODE_MAIN_DELAY 1   /* 主阀延时倒计时（冲洗 MAIN_VALVE） */
#define DISP_MODE_FLUSH      2   /* 单元冲洗倒计时（COMPLETE） */
#define DISP_MODE_INTERVAL   3   /* 间隔倒计时（INTERVAL） */
#define DISP_MODE_DIFF_PEND  4   /* 压差延时等待倒计时 */
#define DISP_MODE_OFF        5   /* 周期关闭（flush_cycle==0）→ --:--:-- */

/* 校准步骤枚举 */
typedef enum {
    CALIB_STEP_IDLE = 0,         /* 空闲：不在校准模式 */
    CALIB_STEP_SPAN_ADJUST       /* 调整参考压力值（进入时已自动采零点） */
} CalibStep_t;

extern CalibStep_t xdata g_calib_step;    /* 当前校准步骤 */
extern u16 g_calib_adc_live;              /* 实时ADC值（校准步骤2显示用） */
extern float g_calib_pressure_ref;        /* 参考压力值（校准步骤2可调） */

/* 校准操作函数 */
void Control_ExecuteZeroCalib(void);              /* 执行零点标定：采样8次写入adc_zero */
void Control_ExecuteSpanCalib(float p_ref_kPa);   /* 执行量程标定：采样8次写入adc_span + p_ref */
void Control_CancelCalib(void);                   /* 取消校准：恢复进入前adc_zero、清零zero_calibration并落盘 [2026-08-03] */

/* 压力读数 */
float Control_ReadPressure(void);                 /* 读ADC→滤波→校准→返回kPa */

/* 周期冲洗倒计时（秒）：距下次周期冲洗的剩余时间，用于右上角188:88:88显示 [2026-08-03] */
u32  Control_GetCycleCountdownSec(void);

// 控制逻辑函数声明 - 定义控制系统相关功能函数接口
void Control_Init(void);                    // 函数声明：控制逻辑初始化函数 - 初始化控制器状态、变量和配置参数
void Control_MainLoop(void);                // 函数声明：控制主循环函数 - 主控制循环，处理所有控制任务的调度和执行
void Control_CheckPressureDiff(void);       // 函数声明：压差检查函数 - 检查并处理压差相关的控制逻辑
void Control_CheckFlushCycle(void);         // 函数声明：冲洗周期检查函数 - 检查是否需要执行冲洗周期
void Control_ExecuteManualFlush(void);      // 函数声明：手动冲洗执行函数 - 执行手动触发的冲洗操作
void Control_CheckAlarm(void);              // 函数声明：报警检查函数 - 检查系统状态并根据需要触发报警
void Control_CalibrateZero(void);           // 函数声明：零点校准函数 - 执行传感器零点校准程序
void Control_ResetContinuousCount(void);    // 函数声明：重置连续冲洗计数 — 修改cycle_limit时调用，归零并写入EEPROM

/* 显示倒计时源 [2026-08-06]：由188:88:88区域显示使用 */
u8   Control_GetDisplayMode(void);          // 当前188倒计时显示模式（DISP_MODE_xxx，宏在menu_system.c定义）
u32  Control_GetDisplayCountdownSec(void);  // 当前188倒计时剩余秒（含冲洗各状态/压差等待）
void Control_ResetCycleTimer(void);         // 重置周期冲洗计时起点（改周期参数时调用）

#endif                  // 头文件结束标识
