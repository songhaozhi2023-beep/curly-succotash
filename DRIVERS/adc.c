#include "adc.h"
#include "config.h"
#include "delay.h"

/*=============================================================
 * 压差传感器电源开关：P3.5 → HX4004A EN（有源低关断）
 * +5V 传感器轨由 HX4004A 电荷泵产生，EN=0 时关断电流 <1µA 且 VOUT 与 VIN 断开。
 * MPX5700DP 是板上最大电流源（~7-10mA），仅采样瞬间供电、其余断电可大幅省电。
 * [4G移植] 引脚不变：4G 板 HX4004A EN 仍接 P3.5
 *=============================================================*/
#define SENSOR_STABLE_MS  10      /* 上电到输出稳定的等待时间 */

void Sensor_PowerOn(void)
{
    P3_5 = 1;                    /* EN 高 → 电荷泵工作 → +5V 供传感器 */
    delay_ms(SENSOR_STABLE_MS);  /* 等待稳定（delay_ms 内部每 ms 喂狗，无看门狗风险） */
}

void Sensor_PowerOff(void)
{
    P3_5 = 0;                    /* EN 低 → 关断，<1µA */
}

/*=============================================================
 * ADC_Init() - 初始化ADC模块
 * [4G移植] STC32G144K246 为 12 位 ADC，无 P1ASF 寄存器：
 *   - ADC_CONTR: bit7=POWER, bit6=START, bit5=FLAG, bit4=EPWMT, bit3-0=通道(CHS)
 *   - 12位结果：ADC_RES(高8位) + ADC_RESL(低4位)，左对齐
 *   - ADCCFG: RESFMT(bit5)=0 左对齐（默认）
 *   ※ 通道选择与结果格式须对照 STC32G144K246 数据手册 ADC 章节最终确认
 *=============================================================*/
void ADC_Init(void)
{
    /* P1.7 设为高阻输入（模拟输入）：11=高阻（原代码设 10=开漏，ADC输入需高阻） */
    P1M1 |= 0x80;   /* P1.7 M1=1 */
    P1M0 |= 0x80;   /* P1.7 M0=1 → 11=高阻输入 */

    /* ★ 使能 P1.7 的 ADC 模拟输入：P1IE(0x7efe31) bit7=P1.7 —— STC32G 的 P1ASF 等价物！
     *   STC15 用 P1ASF 使能模拟输入；STC32G 用 PxIE。不使能则转换永不完成 → while(!FLAG)死等 → 主循环卡死 */
    XSFR_ON();
    P1IE |= 0x80;
    XSFR_OFF();

    /* 12位结果左对齐（RESFMT=0，默认值；显式配置一次） */
    ADCCFG &= ~0x20;

    /* 打开ADC电源，不启动转换 */
    ADC_CONTR = ADC_POWER;

    /* 延时约1ms等待ADC电源稳定 */
    delay_ms(1);

    /* P3.5 → HX4004A EN：推挽输出，初始关断传感器 */
    P3M1 &= ~0x20;
    P3M0 |= 0x20;
    P3_5 = 0;        /* 初始：传感器断电（上电省电） */
}

/*=============================================================
 * ADC_ReadChannel() - 读取指定ADC通道的12-bit结果
 * 参数: ch=通道号(0~15)
 * 返回: 12-bit ADC值(0~4095)
 * [4G移植] 10位→12位：结果 = (ADC_RES<<4) | (ADC_RESL&0x0F)
 *=============================================================*/
u16 ADC_ReadChannel(u8 ch)
{
    u16 result;
    u16 timeout;

    /* 配置通道，清除FLAG，不启动转换 */
    ADC_CONTR = ADC_POWER | (ch & 0x0F);

    /* 启动ADC转换 */
    ADC_CONTR |= ADC_START;   /* bit6=START=1 */

    /* 等待转换完成（带超时保护，防止配置错误时永久死等卡死主循环） */
    timeout = 0;
    while(!(ADC_CONTR & ADC_FLAG)) {
        if(++timeout > 50000) break;   /* 转换正常约1µs；超时≈几十ms则放弃 */
    }

    /* 清除完成标志 */
    ADC_CONTR &= ~ADC_FLAG;

    /* 读取12-bit结果（左对齐 RESFMT=0：ADC_RES=高8位，ADC_RESL 的 bit7-4=低4位）
     * [2026-08-18修正] 原来用 (ADC_RESL&0x0F) 读的是低半字节(恒为0)→只剩8位精度；
     * 正确是 (ADC_RESL>>4) 取高半字节的 D3-D0。 */
    result = ((u16)ADC_RES << 4) | (ADC_RESL >> 4);

    return result;   /* 0~4095 */
}

/*=============================================================
 * ADC_ReadFiltered() - 滑动平均滤波读取
 * 连续采样8次取均值，消除工频噪声和随机抖动
 * 参数: ch=通道号
 * 返回: 滤波后的12-bit ADC值(0~4095)
 *=============================================================*/
u16 ADC_ReadFiltered(u8 ch)
{
    u8 i;
    u16 sum;

    sum = 0;
    for(i = 0; i < 8; i++)
    {
        sum += ADC_ReadChannel(ch);
    }
    return sum / 8;
}
