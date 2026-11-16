#include "control_logic.h"
#include "param_config.h"
#include "appvalve.h"
#include "flush_control.h"
#include "delay.h"
#include "buzzer.h"
#include "menu_system.h"
#include "system_init.h"
#include "adc.h"

/* 压差采样降频：每5个tick(100ms)采一次=10Hz，其余圈复用 g_actual_pressure [2026-08-08] */
#define PRESSURE_SAMPLE_DIV  5
static u8 pressure_sample_cnt = 0;

/*=============================================================
 * EEPROM底层(IAP_ReadByte/WriteByte/EraseSector)实现在param_config.c，
 * 本文件通过 param_config.h 声明复用。
 * 使用EEPROM扇区0(0x0000-0x01FF)存放连续冲洗计数（扇区1留给参数持久化）。
 *=============================================================*/
#define EEPROM_ADDR_CFC_LOW   0x0000  // continuous_flush_count 低字节
#define EEPROM_ADDR_CFC_HIGH  0x0001  // continuous_flush_count 高字节
#define EEPROM_ADDR_CFC_CHK   0x0002  // 校验字节（撕裂写检测）[2026-08-03]
#define EEPROM_SECTOR_ADDR    0x0000  // 扇区0起始地址 (512B对齐)

static void IAP_SaveCounter(u16 val)
{
    IAP_EraseSector(EEPROM_SECTOR_ADDR);
    IAP_WriteByte(EEPROM_ADDR_CFC_LOW, (u8)(val & 0xFF));
    IAP_WriteByte(EEPROM_ADDR_CFC_HIGH, (u8)(val >> 8));
    IAP_WriteByte(EEPROM_ADDR_CFC_CHK,  (u8)((val & 0xFF) ^ ((u8)(val >> 8)) ^ 0x5A));
}

static u16 IAP_LoadCounter(void)
{
    u16 val;
    u8 chk;
    val  = (u16)IAP_ReadByte(EEPROM_ADDR_CFC_LOW);
    val |= ((u16)IAP_ReadByte(EEPROM_ADDR_CFC_HIGH)) << 8;
    chk  = IAP_ReadByte(EEPROM_ADDR_CFC_CHK);
    /* 校验失败（未编程或高/低字节只写了一半的撕裂写）→ 判为无效0 [2026-08-03] */
    if(chk != (u8)((val & 0xFF) ^ ((u8)(val >> 8)) ^ 0x5A)) val = 0;
    if(val > 65500) val = 0;
    return val;
}

float g_actual_pressure = 0.0f;

bit g_pressure_diff_pending = 0;  /* 压差≥设定但延时未满：等待中（外露给显示层）[2026-08-06] */

static u32 flush_cycle_timer = 0;
static u16 pressure_diff_start_tick = 0;
static u16 continuous_flush_count = 0;
static bit pressure_diff_triggered = 0;
static bit initiated = 0;
static bit alarm_beep_on = 0;
static u16 alarm_last_toggle = 0;
static bit calib_active = 0;          /* 校准模式挂起标志 [2026-08-03] */
static u16 last_sys_tick = 0;         /* CheckFlushCycle的tick基准（文件作用域，校准需重置）[2026-08-03] */
static u16 calib_old_adc_zero = 0;    /* 进入校准时保存的adc_zero，取消时恢复 [2026-08-03] */

CalibStep_t xdata g_calib_step = CALIB_STEP_IDLE;
u16 g_calib_adc_live = 0;
float g_calib_pressure_ref = 700.0f;   /* 默认参考压力 700kPa = MPX5700满量程 */

/*=============================================================
 * Control_ReadPressure() - 读取ADC并计算压力(kPa)
 * 使用两点校准公式: P = P_ref × (ADC - ADC_zero) / (ADC_span - ADC_zero)
 * 未校准时使用出厂默认斜率(zero≈37, span≈874, p_ref=700kPa，匹配
 * MPX5700DP×0.6分压+3.3V基准的实际链路) [2026-08-03]
 *=============================================================*/
float Control_ReadPressure(void)
{
    u16 adc;
    u16 zero;
    u16 span;
    float p_ref;
    float p_kPa;

    /* 按需供电：采样瞬间上电，读毕断电（传感器是板上最大电流源）[2026-08-08] */
    Sensor_PowerOn();                              /* P3_5=1 + 等10ms稳定 */
    adc = ADC_ReadFiltered(ADC_CH7);               /* P1.7 = ADC通道7 */
    Sensor_PowerOff();                             /* P3_5=0，传感器断电 */

    zero   = g_param_config.adc_zero;
    span   = g_param_config.adc_span;
    p_ref  = g_param_config.calib_pressure_kPa;

    if(span <= zero || (span - zero) < 50)
    {
        /* 无效校准数据（零点/量程太近或量程≤零点）→ 使用出厂斜率近似 */
        if(adc < zero)
            p_kPa = 0.0f;
        else
            p_kPa = p_ref * (float)(adc - zero) / 4095.0f;   /* [4G移植] 12位满量程 4095 */
    }
    else
    {
        /* 两点校准公式 */
        if(adc < zero)
            p_kPa = 0.0f;
        else
            p_kPa = p_ref * (float)(adc - zero) / (float)(span - zero);
    }

    if(p_kPa < 0.0f) p_kPa = 0.0f;        /* 截断负值（噪声） */
    return p_kPa;
}

void Control_Init(void)
{
    flush_cycle_timer = 0;
    pressure_diff_start_tick = Sys_GetTick();
    continuous_flush_count = IAP_LoadCounter();  /* 从EEPROM恢复计数器，重启后保持限制 */
    pressure_diff_triggered = 0;
    initiated = 0;
    alarm_beep_on = 0;
    alarm_last_toggle = 0;
    Flush_ControlInit();
    /* 触发计数仅在RAM累计、断电清零 [2026-08-06] */
    g_param_config.count_diff_trigger = 0;
    g_param_config.count_cycle_trigger = 0;
    g_param_config.count_manual_trigger = 0;
    BootTest_Init();   /* 启动上电关阀测试（实际发脉冲在主循环BootTest_Process） [2026-08-06] */
}

/* 校准模式切换 [2026-08-03]：
 * 进入时重置周期时钟（校准是维护事件），退出时同步tick防止CheckFlushCycle delta跳变 */
static void Control_SetCalibrationMode(bit active)
{
    calib_active = active;
    if(active)
    {
        flush_cycle_timer = 0;      /* 周期时钟从0重新计时 */
        g_pressure_diff_pending = 0; /* 校准进入：清压差等待标志 [2026-08-06] */
    }
    last_sys_tick = Sys_GetTick();
}

void Control_MainLoop(void)
{
    float p_kPa;

    /* 校准模式下更新实时ADC值供菜单显示 */
    if(g_calib_step == CALIB_STEP_SPAN_ADJUST)
    {
        g_calib_adc_live = ADC_ReadFiltered(ADC_CH7);
        /* 校准期间不更新g_actual_pressure，保留上次正常值 */
    }
    else
    {
        /* 正常模式：压差采样降频至10Hz（每5 tick采一次），其余圈复用 g_actual_pressure [2026-08-08] */
        /* 阀脉冲期间暂停采样：把3.3V轨余量让给线圈，防弱咔哒/BOR复位 [2026-08-20] */
        if(!Valve_PulseIsActive() && ++pressure_sample_cnt >= PRESSURE_SAMPLE_DIV)
        {
            pressure_sample_cnt = 0;
            p_kPa = Control_ReadPressure();
            if(g_param_config.unit_selection == 0)
            {
                g_actual_pressure = p_kPa / 100.0f;     /* kPa → bar */
            }
            else
            {
                g_actual_pressure = p_kPa * 0.145038f;   /* kPa → PSI */
            }
        }
    }

    /* 冲洗进行期间持续重置压差计时起点：冲洗结束后 pressure_delay 重新计时，
       避免周期/手动冲洗一结束、压力仍高时立即触发压差冲洗 [2026-08-03] */
    if(Flush_IsActive())
    {
        pressure_diff_start_tick = Sys_GetTick();
    }

    if(g_calib_step != CALIB_STEP_IDLE)
    {
        /* 校准模式：挂起压差/周期触发，防止校准中途启动反冲洗 [2026-08-03] */
        if(!calib_active) Control_SetCalibrationMode(1);
        pressure_diff_start_tick = Sys_GetTick();
        /* 注意：不重置 pressure_diff_triggered —— 一次性锁存跨校准保留，
           否则同一次压力超限在进出校准时会二次触发压差冲洗 */
    }
    else
    {
        if(calib_active) Control_SetCalibrationMode(0);
        if(!BootTest_IsActive()) {      /* 上电关阀测试期间挂起压差/周期触发 [2026-08-06] */
            Control_CheckPressureDiff();
            Control_CheckFlushCycle();
        }
    }
    Flush_MainProcess();
    Control_CheckAlarm();
    Control_CalibrateZero();
}

void Control_CheckPressureDiff(void)
{
    u16 elapsed;
    float current_pressure;

    /* 防误触发保护(2026-07-17): 压差设定被调到0.0时, 0.0>=0.0恒成立会产生一次
       多余的压差触发冲洗; 此检查应挂起 */
    if(g_param_config.pressure_diff_set <= 0.0f) {
        pressure_diff_start_tick = Sys_GetTick();
        pressure_diff_triggered = 0;
        return;
    }

    /* 单位统一: g_actual_pressure 为显示单位(bar/PSI)，pressure_diff_set 恒以bar存储。
       这里把当前压力换算成bar再比较，切换单位不会改变物理触发阈值 [2026-08-03] */
    current_pressure = g_actual_pressure;
    if(g_param_config.unit_selection == 1)
    {
        current_pressure = current_pressure / 14.5038f;   /* PSI → bar (1bar=14.5038PSI) */
    }

    if(current_pressure >= g_param_config.pressure_diff_set) {
        elapsed = (u16)(Sys_GetTick() - pressure_diff_start_tick);
        if(elapsed >= g_param_config.pressure_delay * TICK_PER_SEC) {
            g_pressure_diff_pending = 0;
            if(!pressure_diff_triggered && !Flush_IsActive()) {
                pressure_diff_triggered = 1;
                /* 压差触发不累计连续冲洗计数 —— 只有周期冲洗计入 cycle_limit
                   [2026-08-01: 原代码此处 continuous_flush_count++ 会与周期冲洗
                    共享同一计数，导致压差反复触发把周期冲洗永久卡死] */
                /* 压差触发同样重置周期时钟：避免压差冲洗结束后周期立即到点
                   → 背靠背两次完整反冲洗 [2026-08-03] */
                flush_cycle_timer = 0;
                Flush_ExecuteDiffTrigger();
            }
        } else {
            g_pressure_diff_pending = 1;   /* 超限但延时未满：等待中 [2026-08-06] */
        }
    } else {
        g_pressure_diff_pending = 0;
        pressure_diff_start_tick = Sys_GetTick();
        pressure_diff_triggered = 0;
    }
}

void Control_CheckFlushCycle(void)
{
    u32 cycle_threshold;
    u16 delta;
    u16 now;

    if(!initiated){initiated=1;last_sys_tick=Sys_GetTick();return;}

    now = Sys_GetTick();
    delta = (u16)(now - last_sys_tick);
    last_sys_tick = now;

    if(Flush_IsActive()) return;
    /* flush_cycle=0/负/NaN → 挂起周期触发，防连续冲洗失控 [2026-08-03]
       (NaN用 x!=x 检测，IEEE754下任何比较对NaN都为假) */
    if(g_param_config.flush_cycle <= 0.0f
       || g_param_config.flush_cycle != g_param_config.flush_cycle) return;

    flush_cycle_timer += delta;
    cycle_threshold = (u32)(g_param_config.flush_cycle * 3600 * TICK_PER_SEC);

    if(flush_cycle_timer >= cycle_threshold && !Flush_IsActive()) {
        if(g_param_config.cycle_limit > 0
           && continuous_flush_count >= g_param_config.cycle_limit) {
            return;
        }
        flush_cycle_timer -= cycle_threshold;  /* 保留余数，不丢失时间 */
        continuous_flush_count++;            /* 增加连续冲洗计数 */
        /* EEPROM寿命保护: 每次周期触发都整扇区擦写会在5分钟周期下~1年磨穿。
           改为每8次或达到cycle_limit才保存，磨损降8倍 [2026-08-03] */
        if(((continuous_flush_count & 0x07) == 0)
           || (g_param_config.cycle_limit > 0
               && continuous_flush_count >= g_param_config.cycle_limit)) {
            IAP_SaveCounter(continuous_flush_count);  /* 持久化到EEPROM，重启后继续生效 */
        }
        Flush_ExecuteCycleTrigger();
    }
}

/* 周期冲洗倒计时（秒）：距下次周期冲洗的剩余时间，供右上角188:88:88显示 [2026-08-03]
 * 由 flush_cycle_timer（已累计tick）与阈值差计算；冲洗触发后计时器重置/减去阈值，
 * 倒计时随之重新开始 */
u32 Control_GetCycleCountdownSec(void)
{
    u32 threshold;
    u32 remain;

    /* flush_cycle非法(0/NaN)时显示0 */
    if(g_param_config.flush_cycle <= 0.0f
       || g_param_config.flush_cycle != g_param_config.flush_cycle) return 0;

    threshold = (u32)(g_param_config.flush_cycle * 3600 * TICK_PER_SEC);
    if(flush_cycle_timer >= threshold) return 0;
    remain = (threshold - flush_cycle_timer) / TICK_PER_SEC;
    return remain;
}

void Control_ExecuteManualFlush(void)
{
    if(!Flush_IsActive()) {
        flush_cycle_timer = 0;           /* reset cycle timer */
        /* 手动冲洗不污染连续冲洗计数: 只有修改 cycle_limit 参数才归零计数,
           手动冲洗只重置周期计时器, 不清除 cycle_limit 门控 [2026-08-01] */
        Flush_ExecuteManualTrigger();
    }
}

void Control_CheckAlarm(void)
{
    if(continuous_flush_count >= g_param_config.cycle_limit
       && g_param_config.cycle_limit > 0)
    {
        if(g_param_config.alarm_output) {
            u16 tick;
            tick = Sys_GetTick();
            if(alarm_beep_on) {
                if((u16)(tick - alarm_last_toggle) >= TICK_PER_SEC / 2) {
                    Buzzer_Off();
                    alarm_beep_on = 0;
                    alarm_last_toggle = tick;
                }
            } else {
                if((u16)(tick - alarm_last_toggle) >= TICK_PER_SEC) {
                    Buzzer_On();
                    g_buzzer_state = BUZZER_STATE_ALARM;
                    alarm_beep_on = 1;
                    alarm_last_toggle = tick;
                }
            }
        }
        else
        {
            /* 报警输出被关闭：立即停止蜂鸣器，避免P5.5锁死在ON状态 [2026-08-03] */
            Buzzer_StopAlarm();
            alarm_beep_on = 0;
            alarm_last_toggle = 0;
        }
    } else {
        if(g_buzzer_state == BUZZER_STATE_ALARM) {
            Buzzer_StopAlarm();
        }
        alarm_beep_on = 0;
        alarm_last_toggle = 0;
    }
}

/*=============================================================
 * Control_ExecuteZeroCalib() - 执行零点标定
 * 在无压差条件下被菜单调用，采样8次取均值写入adc_zero
 *=============================================================*/
void Control_ExecuteZeroCalib(void)
{
    u8 i;
    u16 sum;
    volatile u8 j;

    Sensor_PowerOn();              /* 校准期间传感器保持供电 [2026-08-08] */
    sum = 0;
    for(i = 0; i < 8; i++)
    {
        sum += ADC_ReadChannel(ADC_CH7);
        /* 短暂延时让ADC稳定 */
        for(j = 0; j < 100; j++);
    }
    calib_old_adc_zero = g_param_config.adc_zero;   /* 保存旧零点，取消校准时可恢复 [2026-08-03] */
    g_param_config.adc_zero = sum / 8;
    Param_Save();
}

/*=============================================================
 * Control_ExecuteSpanCalib() - 执行量程标定
 * 在已知参考压力条件下被菜单调用，采样8次取均值写入adc_span
 * 参数: p_ref_kPa = 参考压力值(kPa)
 *=============================================================*/
void Control_ExecuteSpanCalib(float p_ref_kPa)
{
    u8 i;
    u16 sum;
    volatile u8 j;

    sum = 0;
    for(i = 0; i < 8; i++)
    {
        sum += ADC_ReadChannel(ADC_CH7);          
        for(j = 0; j < 100; j++);
    }
    g_param_config.adc_span = sum / 8;
    g_param_config.calib_pressure_kPa = p_ref_kPa;
    Param_Save();
    Sensor_PowerOff();             /* 校准结束：传感器断电 [2026-08-08] */
}

/*=============================================================
 * Control_CancelCalib() - 取消校准
 * 不保存任何值，直接返回待机状态
 *=============================================================*/
void Control_CancelCalib(void)
{
    g_calib_step = CALIB_STEP_IDLE;
    /* 取消校准：恢复进入前的adc_zero并落盘，防止误进校准污染零点 [2026-08-03] */
    g_param_config.adc_zero = calib_old_adc_zero;
    /* 清零zero_calibration开关：否则RUN取消后停在参数7(YES)，下次OK会立即重进校准
       并重新采样零点，覆盖刚恢复的值 [2026-08-03] */
    g_param_config.zero_calibration = 0;
    Param_Save();
    Sensor_PowerOff();             /* 取消校准：传感器断电 [2026-08-08] */
}

void Control_CalibrateZero(void)
{
    /* 零点校准现在由菜单触发 Control_ExecuteZeroCalib()，
       此函数保留为空以避免 Control_MainLoop 调用出错 */
}

/* [2026-08-01] 重置连续冲洗计数(归零并同步EEPROM)。
   仅由修改 cycle_limit 参数时调用(Menu_AdjustParam PARAM_CYCLE_LIMIT分支)，
   手动/压差冲洗不得调用，否则会解除周期冲洗的 cycle_limit 门控。 */
void Control_ResetContinuousCount(void)
{
    continuous_flush_count = 0;
    flush_cycle_timer = 0;        /* 同步重置周期时钟，避免重置后立即触发周期冲洗 [2026-08-03] */
    IAP_SaveCounter(0);  /* 同步清除EEPROM中的值 */
}

/*=============================================================
 * 显示倒计时源 [2026-08-06]
 * 188:88:88 区域由 Menu_ClockTick 每秒调用 Control_GetDisplayMode/CountdownSec
 * 决定显示内容：压差等待 > 冲洗各状态 > 周期/OFF
 *=============================================================*/
u8 Control_GetDisplayMode(void)
{
    if(g_pressure_diff_pending) return DISP_MODE_DIFF_PEND;
    if(Flush_IsActive()) {
        switch(g_flush_control.state) {
            case FLUSH_STATE_MAIN_VALVE: return DISP_MODE_MAIN_DELAY;
            case FLUSH_STATE_INTERVAL:   return DISP_MODE_INTERVAL;
            default:                     return DISP_MODE_FLUSH;   /* COMPLETE/CLOSING/MAIN_CLOSE */
        }
    }
    if(g_param_config.flush_cycle <= 0.0f) return DISP_MODE_OFF;   /* flush_cycle==0 = OFF */
    return DISP_MODE_CYCLE;
}

u32 Control_GetDisplayCountdownSec(void)
{
    u16 elapsed_tick;
    u16 total_tick;
    u16 remain_tick;

    if(g_pressure_diff_pending) {
        elapsed_tick = (u16)(Sys_GetTick() - pressure_diff_start_tick);
        total_tick   = (u16)(g_param_config.pressure_delay * TICK_PER_SEC);
        remain_tick  = (elapsed_tick < total_tick) ? (u16)(total_tick - elapsed_tick) : 0;
        return (u32)((remain_tick + TICK_PER_SEC - 1) / TICK_PER_SEC);   /* 向上取整：5s显示5 [2026-08-06] */
    }
    if(Flush_IsActive()) {
        switch(g_flush_control.state) {
            case FLUSH_STATE_MAIN_VALVE:
                elapsed_tick = (u16)(Sys_GetTick() - g_flush_control.tick_start);
                total_tick   = (u16)(PULSE_WIDTH_TICKS + g_param_config.main_valve_delay * TICK_PER_SEC);
                break;
            case FLUSH_STATE_INTERVAL:
                elapsed_tick = (u16)(Sys_GetTick() - g_flush_control.tick_start);
                total_tick   = (u16)(g_param_config.interval_time * TICK_PER_SEC);
                break;
            default:    /* COMPLETE / CLOSING / MAIN_CLOSE */
                elapsed_tick = (u16)(Sys_GetTick() - g_flush_control.tick_start);
                total_tick   = (u16)(g_param_config.unit_flush_time * TICK_PER_SEC);
                break;
        }
        remain_tick = (elapsed_tick < total_tick) ? (u16)(total_tick - elapsed_tick) : 0;
        return (u32)((remain_tick + TICK_PER_SEC - 1) / TICK_PER_SEC);   /* 向上取整 [2026-08-06] */
    }
    if(g_param_config.flush_cycle <= 0.0f) return 0;   /* OFF */
    return Control_GetCycleCountdownSec();
}

/* 重置周期冲洗计时起点：改周期参数时调用，避免改完立即触发 [2026-08-06] */
void Control_ResetCycleTimer(void)
{
    flush_cycle_timer = 0;
    last_sys_tick = Sys_GetTick();
}
