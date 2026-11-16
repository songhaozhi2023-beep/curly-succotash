#ifndef __ADC_H
#define __ADC_H

#include "config.h"

/* [4G移植] STC32G144K246 ADC：12 位，无 P1ASF、无 SPEED 位。
 * 通道选择在 ADC_CONTR[3:0]，结果在 ADC_RES/ADC_RESL（左对齐 12 位）。
 * 详见 adc.c 注释；细节以 STC32G144K246 数据手册 ADC 章节为准。 */

/* ADC 通道映射（STC32G 的 ADC 通道号，压差传感器在通道 7 = P1.7） */
#define ADC_CH0  0
#define ADC_CH1  1
#define ADC_CH2  2
#define ADC_CH3  3
#define ADC_CH4  4
#define ADC_CH5  5
#define ADC_CH6  6
#define ADC_CH7  7   /* P1.7 — 压差传感器输入 */

void ADC_Init(void);
u16  ADC_ReadChannel(u8 ch);
u16  ADC_ReadFiltered(u8 ch);

/* 压差传感器电源控制（P3.5 → HX4004A EN，有源低关断） */
void Sensor_PowerOn(void);   /* P3_5=1 + 延时等待输出稳定 */
void Sensor_PowerOff(void);  /* P3_5=0，传感器断电（关断电流 <1µA） */

#endif
