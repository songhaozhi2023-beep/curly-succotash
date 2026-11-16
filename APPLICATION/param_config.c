#include "param_config.h"  // 参数配置头文件：提供ParamConfig_t、MenuStatus_t、ParamType_t等类型定义
#include "config.h"        // 系统配置：提供u8/u16/u32类型定义
#include "ht1622.h"        // HT1622 LCD驱动（当前未使用，预留）
#include "delay.h"         // 延时函数（当前未使用，预留）

/*=============================================================
 * 全局变量定义
 *=============================================================*/
ParamConfig_t xdata g_param_config = {0};  // 全局系统参数结构体（XDATA，占外部RAM）
MenuStatus_t  xdata g_menu_status = {0};   // 全局菜单状态结构体（XDATA）

/*=============================================================
 * EEPROM IAP 底层函数（STC32G144K246，自 STC15W4K 移植）
 * 扇区分配: 扇区0(0x0000) → control_logic.c 的连续冲洗计数
 *           扇区1(0x0200) → 本文件的参数持久化（512B/扇区）
 * [2026-08-22] STC32G 关键差异（数据手册 19.3/19.4，已逐条核对）：
 *   - IAP 地址为 24 位：IAP_ADDRE(0xF6)=ADDR[23:16]、IAP_ADDRH=ADDR[15:8]、IAP_ADDRL=ADDR[7:0]。
 *     ★ EEPROM 访问必须 IAP_ADDRE=0x00（复位值 0xFF 指向程序区 → 读写落空 → 参数断电不保存）。
 *   - IAP_CONTR 无 WT 位（时序由 IAP_TPS 控制），EEPROM 操作 = 0x80(IAPEN)。
 *   - IAP_TPS = 时钟MHz（24MHz→24）。
 *   ※ STC-ISP 烧录时必须设 EEPROM 大小 ≥1K（本工程用 0x0000/0x0200 两个扇区=1K，推荐 2K）。
 *=============================================================*/
#define IAP_SECTOR0_ADDR  0x0000   /* 扇区0起始地址 */
#define IAP_SECTOR1_ADDR  0x0200   /* 扇区1起始地址 */

static void IAP_Idle(void)
{
    IAP_CONTR = 0;
    IAP_CMD   = 0;
    IAP_TRIG  = 0;
    IAP_ADDRE = 0x00;    /* STC32G: 空闲态也停在 EEPROM 区，避免指向程序区 */
    IAP_ADDRH = 0x80;
    IAP_ADDRL = 0;
}

u8 IAP_ReadByte(u16 addr)
{
    /* [2026-08-24 实测] STC32G144K246 的 IAP_CMD=1(读) 永远返回 0x00（IAP_DATA 未装载），
       但 MOV 直读 EEPROM(FC:2800h+偏移) 正常（全新0xFF、写入值可读回）。
       手册19.4.1：MOV 可对 EEPROM 读操作；19.1：读1字节建议用 MOVC。故读改用 MOV，
       写/擦除仍用 IAP(IAP_CMD=2/3 实测正常)。 */
    return *(unsigned char volatile far *)(0xFC2800UL + addr);
}

void IAP_WriteByte(u16 addr, u8 dat)
{
    EA = 0;
    IAP_TPS = (u8)(SYSCLK / 1000000UL);   /* STC32G 必须设 IAP_TPS=时钟MHz */
    WDT_CONTR |= 0x10;                    /* [诊断] 只置CLR_WDT喂狗，不启用EN_WDT */
    IAP_ADDRE = 0x00;                     /* STC32G: EEPROM 访问必须 IAP_ADDRE=0x00（手册19.4.1） */
    IAP_DATA  = dat;
    IAP_ADDRH = (u8)(addr >> 8);
    IAP_ADDRL = (u8)(addr);
    IAP_CMD   = 2;
    IAP_CONTR = 0x80;
    IAP_TRIG  = 0x5A;
    IAP_TRIG  = 0xA5;
    IAP_Idle();
    EA = 1;
}

void IAP_EraseSector(u16 addr)
{
    EA = 0;
    IAP_TPS = (u8)(SYSCLK / 1000000UL);   /* STC32G 必须设 IAP_TPS=时钟MHz */
    WDT_CONTR |= 0x10;                    /* [诊断] 只置CLR_WDT喂狗，不启用EN_WDT */
    IAP_ADDRE = 0x00;                     /* STC32G: EEPROM 访问必须 IAP_ADDRE=0x00（手册19.4.1） */
    IAP_ADDRH = (u8)(addr >> 8);
    IAP_ADDRL = (u8)(addr);
    IAP_CMD   = 3;
    IAP_CONTR = 0x80;
    IAP_TRIG  = 0x5A;
    IAP_TRIG  = 0xA5;
    IAP_Idle();
    EA = 1;
}

/*=============================================================
 * Param_Init() - 参数初始化
 * 从EEPROM加载参数，如果加载失败（unit_flush_time==0）则使用默认值。
 *=============================================================*/
void Param_Init(void)
{
    u8 i;  // 循环计数器

    Param_Load();  // 从EEPROM加载参数（未编程/校验失败时保持全0 → 走默认值）

    // 如果unit_flush_time为0，说明是首次启动或EEPROM为空，使用默认值
    if(g_param_config.unit_flush_time == 0) {
        g_param_config.unit_flush_time = 5.0f;      // 默认单元冲洗时间：5秒
        g_param_config.pressure_diff_set = 2.0f;     // 默认压差设定：2.0 bar
        g_param_config.flush_cycle = 5.0f/60.0f;      // 默认冲洗周期：5分钟（单位：小时）
        g_param_config.main_valve_select = 0;        // 默认主阀选择：关闭
        g_param_config.main_valve_delay = 30;        // 默认主阀延时：30秒
        g_param_config.interval_time = 5;            // 默认间隔时间：5秒
        g_param_config.pressure_delay = 5;           // 默认压差延时：5秒
        g_param_config.cycle_limit = 3;              // 默认循环限制：3次
        g_param_config.alarm_output = 0;             // 默认报警输出：关闭
        g_param_config.zero_calibration = 0;         // 默认零点校准：关闭
        g_param_config.zero_calib_value = 0.0f;      // 默认零点校准值：0
        g_param_config.station_recognition = 10;     // 默认分站识别：10站
        g_param_config.unit_selection = 0;           // 默认单位：BAR
        /* [4G移植] 默认斜率匹配实际硬件链路: MPX5700DP(0.2~4.7V) × R35/R36(0.6) → P1.7,
           3.3V基准下 0kPa≈149计数、700kPa≈3499计数（12位ADC；旧值37/874是10位）。
           注：实际板需用零点/量程校准重新标定，此值仅作出厂近似 */
        g_param_config.adc_zero = 149;
        g_param_config.adc_span = 3499;
        g_param_config.calib_pressure_kPa = 700.0f; /* 默认参考压力：700kPa（MPX5700满量程） */

        // 所有分站默认启用（1=YES）
        for(i = 0; i < 10; i++) {
            g_param_config.flush_select[i] = 1;
        }

        Param_Save();  // 保存默认参数到EEPROM（首次上电落盘）
    }
}

/*=============================================================
 * Param_Save() / Param_Load() - EEPROM持久化 [2026-08-03实现]
 * 布局(扇区1, 0x0200): ParamConfig_t(53字节) + 魔数 + 版本 + XOR校验和 = 56字节
 * Param_Save 先与EEPROM现有内容比较，只有变化才整扇区擦写（compare-dedup），
 * 避免菜单导航/无实际改动时磨损EEPROM。
 *=============================================================*/
#define PARAM_EEPROM_BASE    IAP_SECTOR1_ADDR     /* 扇区1：参数区，扇区0留给冲洗计数 */
#define PARAM_STRUCT_BYTES   ((u8)sizeof(ParamConfig_t))
#define PARAM_MAGIC          0x5A                 /* 首字节魔数：区分未编程(0xFF) */
#define PARAM_VERSION        0x01
#define PARAM_TOTAL_BYTES    (PARAM_STRUCT_BYTES + 3)

void Param_Save(void)
{
    u8 i;
    u8 chk;
    u8 xdata *src;
    bit changed;

    if(PARAM_TOTAL_BYTES > 0x100) return;   /* 不应发生：防御 */

    src = (u8 xdata *)&g_param_config;
    changed = 0;

    /* 先计算本次校验和（dedup比较与写入共用） */
    chk = PARAM_MAGIC ^ PARAM_VERSION;
    for(i = 0; i < PARAM_STRUCT_BYTES; i++) chk ^= src[i];

    /* 与EEPROM现有内容比较（含魔数/版本/校验和）：完全相同则不擦写 [2026-08-03] */
    for(i = 0; i < PARAM_STRUCT_BYTES; i++) {
        if(IAP_ReadByte(PARAM_EEPROM_BASE + i) != src[i]) { changed = 1; break; }
    }
    if(!changed) {
        if(IAP_ReadByte(PARAM_EEPROM_BASE + PARAM_STRUCT_BYTES) != PARAM_MAGIC)   changed = 1;
        if(IAP_ReadByte(PARAM_EEPROM_BASE + PARAM_STRUCT_BYTES + 1) != PARAM_VERSION) changed = 1;
        if(IAP_ReadByte(PARAM_EEPROM_BASE + PARAM_STRUCT_BYTES + 2) != chk)       changed = 1;
    }
    if(!changed) return;

    EA = 0;                                   /* 擦写期间关中断 */
    IAP_EraseSector(PARAM_EEPROM_BASE);
    for(i = 0; i < PARAM_STRUCT_BYTES; i++) {
        IAP_WriteByte(PARAM_EEPROM_BASE + i, src[i]);
    }
    IAP_WriteByte(PARAM_EEPROM_BASE + PARAM_STRUCT_BYTES,     PARAM_MAGIC);
    IAP_WriteByte(PARAM_EEPROM_BASE + PARAM_STRUCT_BYTES + 1, PARAM_VERSION);
    IAP_WriteByte(PARAM_EEPROM_BASE + PARAM_STRUCT_BYTES + 2, chk);
    EA = 1;
}

/* 范围校验：加载的参数若明显越界（校验和碰巧通过但数据被破坏），拒绝并回退默认值 */
static bit Param_DataValid(void)
{
    u8 i;

    /* NaN 拒绝：IEEE754 下任何比较对 NaN 都为假，用 x != x 检测 [2026-08-03] */
    if(g_param_config.unit_flush_time != g_param_config.unit_flush_time) return 0;
    if(g_param_config.flush_cycle != g_param_config.flush_cycle) return 0;
    if(g_param_config.pressure_diff_set != g_param_config.pressure_diff_set) return 0;
    if(g_param_config.calib_pressure_kPa != g_param_config.calib_pressure_kPa) return 0;
    if(g_param_config.zero_calib_value != g_param_config.zero_calib_value) return 0;

    if(g_param_config.unit_flush_time < 1.0f || g_param_config.unit_flush_time > 600.0f) return 0;
    /* flush_cycle 下限=5分钟（菜单最小值）；0=OFF（周期冲洗关闭，合法 [2026-08-06]）；
       其他 0 或负值会导致周期阈值=0 → 连续冲洗失控 */
    if(g_param_config.flush_cycle != 0.0f
       && (g_param_config.flush_cycle < 5.0f/60.0f || g_param_config.flush_cycle > 1000.0f)) return 0;
    if(g_param_config.pressure_diff_set < 0.0f || g_param_config.pressure_diff_set > 7.0f) return 0;
    if(g_param_config.main_valve_delay > 999) return 0;
    if(g_param_config.interval_time > 999) return 0;
    if(g_param_config.pressure_delay > 999) return 0;
    if(g_param_config.cycle_limit > 99) return 0;
    if(g_param_config.station_recognition < 1 || g_param_config.station_recognition > 10) return 0;
    if(g_param_config.adc_zero > 4095 || g_param_config.adc_span > 4095) return 0;   /* [4G移植] 12位 */
    if(g_param_config.calib_pressure_kPa < 0.0f || g_param_config.calib_pressure_kPa > 1000.0f) return 0;
    /* 布尔/枚举字段校验 [2026-08-03] */
    if(g_param_config.main_valve_select > 1) return 0;
    if(g_param_config.alarm_output > 1) return 0;
    if(g_param_config.zero_calibration > 1) return 0;
    if(g_param_config.unit_selection > 1) return 0;
    for(i = 0; i < 10; i++) {
        if(g_param_config.flush_select[i] > 1) return 0;
    }
    return 1;
}

void Param_Load(void)
{
    u8 i;
    u8 chk;
    u8 xdata *dst;
    u8 xdata tmp[64];   /* 临时缓冲，固定大小须 ≥ PARAM_TOTAL_BYTES(56) */

    if(PARAM_TOTAL_BYTES > 0x100) return;

    /* 魔数/版本校验：未编程(0xFF)或无效数据 → 保持默认值 */
    if(IAP_ReadByte(PARAM_EEPROM_BASE + PARAM_STRUCT_BYTES) != PARAM_MAGIC) return;
    if(IAP_ReadByte(PARAM_EEPROM_BASE + PARAM_STRUCT_BYTES + 1) != PARAM_VERSION) return;

    /* XOR校验和 */
    chk = PARAM_MAGIC ^ PARAM_VERSION;
    for(i = 0; i < PARAM_STRUCT_BYTES; i++) chk ^= IAP_ReadByte(PARAM_EEPROM_BASE + i);
    if(chk != IAP_ReadByte(PARAM_EEPROM_BASE + PARAM_STRUCT_BYTES + 2)) return;

    /* 先读到临时缓冲做范围校验，避免把破坏数据写进g_param_config [2026-08-03] */
    for(i = 0; i < PARAM_STRUCT_BYTES; i++) {
        tmp[i] = IAP_ReadByte(PARAM_EEPROM_BASE + i);
    }
    dst = (u8 xdata *)&g_param_config;
    for(i = 0; i < PARAM_STRUCT_BYTES; i++) {
        dst[i] = tmp[i];
    }
    if(!Param_DataValid()) {
        /* 数据越界：清0 → Param_Init走默认值 */
        for(i = 0; i < PARAM_STRUCT_BYTES; i++) dst[i] = 0;
    }
}

/*=============================================================
 * Param_GetValue() - 获取参数值
 * 根据参数类型从全局结构体读取值，返回浮点类型。
 * 计数器类型需要(u16→float)转换。
 *=============================================================*/
float Param_GetValue(ParamType_t param)
{
    switch(param) {
        case PARAM_UNIT_FLUSH_TIME:
            return g_param_config.unit_flush_time;

        case PARAM_PRESSURE_DIFF_SET:
            return g_param_config.pressure_diff_set;

        case PARAM_FLUSH_CYCLE:
            return g_param_config.flush_cycle;

        case PARAM_COUNT_DIFF_TRIGGER:
            return (float)g_param_config.count_diff_trigger;  // u16转float

        case PARAM_COUNT_CYCLE_TRIGGER:
            return (float)g_param_config.count_cycle_trigger;

        case PARAM_COUNT_MANUAL_TRIGGER:
            return (float)g_param_config.count_manual_trigger;

        case PARAM_MAIN_VALVE_DELAY:
            return (float)g_param_config.main_valve_delay;

        case PARAM_INTERVAL_TIME:
            return (float)g_param_config.interval_time;

        case PARAM_PRESSURE_DELAY:
            return (float)g_param_config.pressure_delay;

        case PARAM_CYCLE_LIMIT:
            return (float)g_param_config.cycle_limit;

        case PARAM_FLUSH_SELECT:
            return (float)(g_param_config.flush_select[0]);  // 返回第1站的状态

        case PARAM_MAIN_VALVE_SELECT:
            return (float)g_param_config.main_valve_select;

        case PARAM_ALARM_OUTPUT:
            return (float)g_param_config.alarm_output;

        case PARAM_ZERO_CALIBRATION:
            return (float)g_param_config.zero_calibration;

        case PARAM_ZERO_CALIB_VALUE:
            return g_param_config.zero_calib_value;

        case PARAM_UNIT_SELECTION:
            return (float)g_param_config.unit_selection;

        case PARAM_STATION_RECOGNITION:
            return (float)g_param_config.station_recognition;

        case PARAM_ADC_ZERO:
            return (float)g_param_config.adc_zero;

        case PARAM_ADC_SPAN:
            return (float)g_param_config.adc_span;

        case PARAM_CALIB_PRESSURE_KPA:
            return g_param_config.calib_pressure_kPa;

        default:
            return 0.0f;
    }
}

/*=============================================================
 * Param_IncrementCounter() - 递增计数器
 * 将指定类型的计数器加1，有u16上限保护（65535）。
 *=============================================================*/
void Param_IncrementCounter(ParamType_t param)
{
    switch(param) {
        case PARAM_COUNT_DIFF_TRIGGER:
            if(g_param_config.count_diff_trigger < 65535) {
                g_param_config.count_diff_trigger++;  // u16溢出保护
            }
            break;

        case PARAM_COUNT_CYCLE_TRIGGER:
            if(g_param_config.count_cycle_trigger < 65535) {
                g_param_config.count_cycle_trigger++;
            }
            break;

        case PARAM_COUNT_MANUAL_TRIGGER:
            if(g_param_config.count_manual_trigger < 65535) {
                g_param_config.count_manual_trigger++;
            }
            break;

        default:
            break;
    }

    /* [2026-08-03] 不再调用Param_Save：触发计数(count_*_trigger)仅用于累计且当前未显示；
       每次触发都整扇区擦写会加剧EEPROM磨损。真正的周期门控计数
       continuous_flush_count 由 control_logic.c 独立持久化。 */
}
