# 过滤反冲洗 4G 版固件

**主控**: STC32G144K246（LQFP100，24MHz IRC，**Keil C251**）
**4G 模组**: ML307R-DC（中移物联 LTE Cat.1，AT 指令，走 MCU UART1）
**基准**: 由基础版 `filter_backwashing_system_best`（STC15W4K56S4）移植

> 完整移植方案见 `filter_backwashing_system_best/docs/2026-08-15-4G版移植方案.md`
> **注意：STC32G144K246 是 251 内核，必须用 Keil C251 编译（C51 编译的 hex 跑不起来）**

---

## 一、编译（C251）

1. 用 **Keil C251**（`D:\keil5C251\UV4\UV4.exe`）打开 **`hengshi_4g_c251.uvproj`**（器件已配 STC32G144K246-32Bit Series，CreateHexFile 已勾）
2. **Rebuild all**（F7）
3. STC-ISP 烧录：芯片 STC32G144K246，IRC 频率选 **24MHz**
4. 注意：烧录选项里**看门狗不要勾**

> 另有独立 485 测试工程 `serial_test\serial_test_c251.uvproj`（已验证两路 485 收发回显全通）

已完成 STC15 → STC32G144K246 的核心移植，代码结构完整：

| 模块 | 状态 |
|---|---|
| `config.h` | ✅ 换 `STC32G144K246.H` + Px_x→Pxy 兼容宏 + 升压/RS485/按键引脚宏 |
| `delay.c` | ✅ 24MHz（与基础版一致，4800/5） |
| `system_init.c` | ✅ Timer0 重装值 0x63C0（24MHz，与基础版一致） |
| `adc.c/h` | ⚠️ 12 位重写（通道选 ADC_CONTR[3:0]，结果 (ADC_RES<<4)\|(ADC_RESL&0x0F)，待上板核实） |
| `appvalve.c` | ✅ 升压 EN P1.0→P8.0（已确认）；阀5/6/7 改 PB 口；NET60 死区 60（24MHz） |
| `appkey.c/h` | ✅ 按键引脚重映射（RETURN=P8.1/ADD=PA.2/SUB=PA.1/OK=P1.4/RUN=P1.5） |
| `control_logic.c` | ✅ ADC 满量程 1023→4095 |
| `param_config.c` | ✅ ADC 默认校准值 10 位→12 位（149/3499）；**IAP 读改 MOV 直读**（STC32G 的 IAP_CMD=1 读恒返回 0x00，实测参数已可保存） |
| `main.c` | ✅ 上电安全时序 + P8.0 升压配置 |
| `driversuart.c/h` | ✅ UART2(Modbus RS485-1) + **STC32G 收后发方案**（fire-and-forget 发送 + 发前重初始化 + 先读后清，485 实测验证） |
| `ml307r.c/h` | ✅ 框架 + 规格已确认（115200/8N1，VBAT 3.3~4.3V）；AT/MQTT 流程待补 |
| `ht1622/buzzer/app_led` | ✅ 引脚不变，无需改动 |
| `menu_system/flush_control` | ✅ 芯片无关，无需改动 |

## 二、关键引脚映射（4G 板 vs 基础版）

| 功能 | 基础版 | 4G 版 | 备注 |
|---|---|---|---|
| 10 路阀 + NET60 | P0/P4/P6/P7 | **相同** | P0.0-0.7/P4.5-4.6/P6.0-6.3/P7.0-7.7 |
| HT1622 LCD | P3.4/P3.6/P3.7 | **相同** | CS/WR/DATA |
| 蜂鸣器 | P5.5 | **相同** | |
| LED21 | P4.0 | **相同** | |
| 传感器供电 EN | P3.5 | **相同** | HX4004A |
| 压差 ADC | P1.7 | **相同** | 10 位→12 位 |
| **升压 EN** | P1.0 | **P8.0** ✅已确认 | PW5300，扩展端口 |
| **按键** | P1.1~P1.5 | P8.1/PA.2/PA.1/P1.4/P1.5 | RETURN/ADD/SUB/OK/RUN |
| **Modbus RS485** | UART1 | **UART2**（方向 P8.7） | |
| 4G 模组 | — | UART1（P3.0/P3.1） | ML307R-DC |

> ⚠️ 4G 板原理图**复用了基础版网标名**（网标"P1.0"实为升压 EN 接 P8.0），本映射已按 `.tel` 网表 + 引脚表核实；升压 EN 已经用户确认。

## 三、ML307R-DC 规格（已从数据手册确认）

数据手册 `filter_backwashing_system_best/4G芯片.pdf`（4 页，图片型）：

- LTE Cat.1，AT 指令接口，默认波特率 **115200**（8N1）
- VBAT 3.3~4.3V（典型 3.8V）
- 支持 TCP/UDP、MQTT、HTTP、FTP、短信
- PWRKEY（pin7）/ RESET（pin15）为板上手动按键 SW7/SW8，MCU 不直接控制
- AT 流程：`AT` → `AT+CPIN?` → `AT+CSQ` → `AT+CGREG?` → MQTT 连接/收发

## 五、待办 / 需上板或补充（重要）

1. **ADC 12 位结果格式**：STC32G 数据手册为图片/CID 字体 PDF，无法程序化提取；当前按官方头文件 + 标准 STC32G 模型实现（左对齐 `(ADC_RES<<4)|(ADC_RESL&0x0F)`），需上板读已知电压核实。
2. **delay/Timer0 精确值**：24MHz 下 4800/5、0x63C0 与基础版一致，仍建议示波器实测确认。
3. ~~**IAP/EEPROM**~~ ✅ 已解决（2026-08-24）：STC32G144K246 的 `IAP_CMD=1`(读) 恒返回 0x00，`IAP_ReadByte` 已改 **MOV 直读**(`FC:2800+偏移`)，写/擦仍用 IAP；EEPROM 大小 16K 在 STC-ISP 设置，改参数→断电→上电已实测可保存。
4. **按键串扰**：基础版的串扰抑制是针对 STC15 板 P1.1~P1.5 相邻布局；4G 板按键改到 P8/PA/P1，串扰特性不同，需实测确认。
5. **Modbus**：波特率 + UART2 波特率发生器 + Modbus 协议解析待实现（基础版也未初始化，485 为扩展用）。
6. **4G（ML307R-DC）**：完整 AT 命令手册（当前 4 页是概述）以补全注网/TCP/MQTT；MQTT 服务器信息、主题/payload 协议待定。
7. **2×RS485 + 2×RS232**：仅扩展用，无明确用途，驱动暂未启用。

## 六、待提供资料

- ~~ML307R-DC 数据手册~~ ✅ 已提供（`4G芯片.pdf`）
- ~~STC32G144K246 数据手册~~ ✅ 已提供（`C53120917_...规格书.PDF`，图片型，仅人工查阅）
- ML307R-DC **完整 AT 命令手册**（4 页概述不含全部 AT 命令细节）
- 可读原理图（嘉立创 EDA 源文件或清晰 PDF）以二次核对引脚
- MQTT broker 地址/端口/鉴权 + 主题/payload 协议定义
