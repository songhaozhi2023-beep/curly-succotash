#ifndef __ML307R_H
#define __ML307R_H

#include "config.h"

/*=============================================================
 * 4G 模组 ML307R-DC（中移物联 LTE Cat.1）驱动框架
 *
 * 硬件（.tel 网表核实）：
 *   - AT 串口：MCU UART1（P3.0=TXD→模组 RXD，P3.1=RXD←模组 TXD）
 *   - PWRKEY（模组 pin7）与 RESET（pin15）为板上手动按键 SW7/SW8，
 *     MCU 不直接控制（模组上电/复位由人工操作）
 *   - SIM 卡座 + IPX/SMA 天线
 *
 * 数据手册已确认（4G芯片.pdf，2026-08）：
 *   - LTE Cat.1，AT 指令接口，默认波特率 115200 (8N1)
 *   - VBAT 3.3~4.3V（典型 3.8V）
 *   - 支持 TCP/UDP、MQTT、HTTP、FTP、短信
 *
 * 典型 AT 流程：
 *   AT             → 就绪
 *   AT+CPIN?       → SIM 卡状态（READY）
 *   AT+CSQ         → 信号强度
 *   AT+CGREG?/CEREG? → 注网状态（1/5 = 已注网）
 *   AT+MQTTCFG/OPEN/CONN/SUB/PUB  → MQTT 连接与收发（具体命令以 AT 手册为准）
 *=============================================================*/

/* 模组 AT 串口波特率（ML307R 默认 115200） */
#define ML307R_BAUD      115200L

void ML307R_Init(void);                    /* 初始化 UART1 + 模组 AT 串口 */
bit  ML307R_SendAt(char *cmd, u16 timeout); /* 发 AT 指令并等待响应（TODO） */
bit  ML307R_CheckReady(void);               /* 查询模组是否就绪（AT） */
bit  ML307R_CheckNet(void);                 /* 查询注网状态（CSQ/CGREG） */

/* MQTT（TODO：具体主题/payload 协议待定） */
bit  ML307R_MqttConnect(void);
bit  ML307R_MqttPublish(char *topic, char *payload);
bit  ML307R_MqttSubscribe(char *topic);

#endif
