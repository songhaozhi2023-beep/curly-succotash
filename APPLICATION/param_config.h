#ifndef __PARAM_CONFIG_H      // 头文件保护宏，防止重复包含
#define __PARAM_CONFIG_H      // 定义头文件标识符

#include "config.h"        // 包含系统配置头文件，定义基本数据类型和系统配置

// 参数类型定义 - 定义系统参数中所有可配置参数的枚举类型
typedef enum {
    PARAM_UNIT_FLUSH_TIME = 0,        // 枚举值：单元冲洗时间参数，序号为0
    PARAM_PRESSURE_DIFF_SET,          // 枚举值：压差设定参数
    PARAM_FLUSH_CYCLE,                // 枚举值：冲洗周期参数
    PARAM_COUNT_DIFF_TRIGGER,         // 枚举值：反冲洗计数（累计，压差）
    PARAM_COUNT_CYCLE_TRIGGER,        // 枚举值：反冲洗计数（累计，周期）
    PARAM_COUNT_MANUAL_TRIGGER,       // 枚举值：反冲洗计数（累计，手动）
    PARAM_FLUSH_SELECT,               // 枚举值：冲洗分站设定
    PARAM_MAIN_VALVE_SELECT,          // 枚举值：主阀选择参数
    PARAM_MAIN_VALVE_DELAY,           // 枚举值：主阀延时参数
    PARAM_INTERVAL_TIME,              // 枚举值：间隔时间参数
    PARAM_PRESSURE_DELAY,             // 枚举值：压差延时参数
    PARAM_CYCLE_LIMIT,                // 枚举值：循环次数限制参数
    PARAM_ALARM_OUTPUT,               // 枚举值：报警输出参数
    PARAM_ZERO_CALIBRATION,           // 枚举值：零点校准参数
    PARAM_ZERO_CALIB_VALUE,           // 枚举值：零点校准值参数
    PARAM_STATION_RECOGNITION,        // 枚举值：分站识别参数
    PARAM_UNIT_SELECTION,             // 枚举值：单位选择参数
    PARAM_ADC_ZERO,                  /* 枚举值：零点ADC校准值 */
    PARAM_ADC_SPAN,                  /* 枚举值：量程ADC校准值 */
    PARAM_CALIB_PRESSURE_KPA,        /* 枚举值：参考压力值(kPa) */
    PARAM_TOTAL_COUNT                 // 枚举值：参数总数，用于计数校验
} ParamType_t;                       // 参数类型枚举定义

// 参数配置结构体 - 定义系统参数配置的数据结构
typedef struct {
    float unit_flush_time;            // 结构体成员：单元冲洗时间 (秒)，浮点数
    float pressure_diff_set;          // 结构体成员：压差设定 (bar或psi)，浮点数
    float flush_cycle;                // 结构体成员：冲洗周期 (小时)，浮点数
    u16 count_diff_trigger;           // 结构体成员：压差触发计数，16位无符号整数
    u16 count_cycle_trigger;          // 结构体成员：周期触发计数，16位无符号整数
    u16 count_manual_trigger;         // 结构体成员：手动触发计数，16位无符号整数
    u8 flush_select[10];              // 结构体成员：分站选择数组 (0=OFF, 1=YES)，包含10个元素的数组
    u8 main_valve_select;             // 结构体成员：主阀选择 (0=NO, 1=YES)，8位无符号整数
    u16 main_valve_delay;             // 结构体成员：主阀延时 (秒)，16位无符号整数
    u16 interval_time;                // 结构体成员：间隔时间 (秒)，16位无符号整数
    u16 pressure_delay;               // 结构体成员：压差延时 (秒)，16位无符号整数
    u16 cycle_limit;                  // 结构体成员：循环次数 (次数)，16位无符号整数
    u8 alarm_output;                  // 结构体成员：报警输出 (0=NO, 1=YES)，8位无符号整数
    u8 zero_calibration;              // 结构体成员：零点校准 (0=NO, 1=YES)，8位无符号整数
    float zero_calib_value;           // 结构体成员：零点校准值 (偏移量)，浮点数
    u8 station_recognition;           // 结构体成员：分站识别 (1-10)，8位无符号整数
    u8 unit_selection;                // 结构体成员：单位选择 (0=BAR, 1=PSI)，8位无符号整数
    u16 adc_zero;                    /* 结构体成员：零点ADC读数（无压差时） */
    u16 adc_span;                    /* 结构体成员：量程参考点ADC读数 */
    float calib_pressure_kPa;        /* 结构体成员：量程参考点压力值(kPa) */
} ParamConfig_t;                     // 参数配置结构体定义

// 菜单层级定义 - 定义菜单系统的层级枚举类型
typedef enum {
    MENU_LEVEL_NONE = 0,              // 枚举值：无菜单层级，序号为0
    MENU_LEVEL_1,                     // 枚举值：一级菜单层级
    MENU_LEVEL_2,                     // 枚举值：二级菜单层级
    MENU_LEVEL_FLUSH_SELECT,           // 枚举值：分站选择子菜单层级
    MENU_LEVEL_ZERO_CALIB,             // 枚举值：零点校准子菜单层级
    MENU_LEVEL_COUNT                   // 枚举值：冲洗计数页面（压差/周期/手动）[2026-08-06]
} MenuLevel_t;                       // 菜单层级枚举定义

// 菜单状态结构体 - 定义菜单系统状态的数据结构
typedef struct {
    MenuLevel_t menu_level;           // 结构体成员：当前菜单层级，菜单层级枚举值
    ParamType_t current_param;        // 结构体成员：当前参数类型，参数类型枚举值
    u8 flush_station_num;             // 结构体成员：当前选择的冲洗分站号，8位无符号整数
    u8 editing;                      // 结构体成员：是否在编辑状态，0=否 1=是
    u8 entering_submenu;             // 结构体成员：是否正在进入子菜单，0=否 1=是
} MenuStatus_t;                      // 菜单状态结构体定义

// 外部变量声明 - 定义全局参数配置和菜单状态变量
extern ParamConfig_t xdata g_param_config;  // 外部变量声明：全局系统参数结构体 (XDATA)
extern MenuStatus_t  xdata g_menu_status;    // 外部变量声明：全局菜单状态结构体 (XDATA)

// 参数管理函数声明 - 定义参数管理功能函数接口
void Param_Init(void);                                  // 函数声明：参数初始化函数
void Param_Save(void);                                  // 函数声明：参数保存函数
void Param_Load(void);                                  // 函数声明：参数加载函数
float Param_GetValue(ParamType_t param);                // 函数声明：参数获取函数，参数为参数类型，返回浮点值
void Param_IncrementCounter(ParamType_t param);         // 函数声明：参数计数器增加函数，参数为参数类型

// EEPROM IAP 底层函数（实现于param_config.c，供control_logic.c等复用）
u8  IAP_ReadByte(u16 addr);                             // 读EEPROM字节（EA自动关开）
void IAP_WriteByte(u16 addr, u8 dat);                   // 写EEPROM字节（必须先擦除）
void IAP_EraseSector(u16 addr);                         // 擦除一个扇区(512B)

#endif                          // 头文件结束标识
