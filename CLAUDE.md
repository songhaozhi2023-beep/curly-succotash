# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 仓库本质：一个仓库，两条互不相交的固件线

同一块主控 **STC32G144K246**（LQFP100，251 内核，**必须用 Keil C251 编译**，C51 产物跑不起来），两条线代码零交集：

- **线 A：过滤反冲洗 4G 版**（裸机前后台）。根目录 `README.md` 只描述这条线。源码在 `STARTUP/ SYSTEM/ APPLICATION/ DRIVERS/ TEST/`。10 路双稳态阀半桥（NET60 母线 + P8.0 升压）、HT1622 LCD、压差 ADC 触发反冲洗、ML307R 4G（UART1，仓库内 ml307r.c 只是壳，完整 AT 引擎在仓库外 `D:\STM32_Project\filter_backwashing_system_4g\4g_test\`）、Modbus 走 UART2（仅链路层，协议解析未实现）。
- **线 B：`modbus6_rtos/`**（**当前活跃工程**，"流量压力检测集成装置"指这条线）。FreeRTOS V10.4.6（STC 官方移植包改造，`portable/STC32G12K128/` 目录名沿用官方命名，目标器件已是 K246）+ 双角色 Modbus-RTU：**主站** UART5（3 流量计，DE=P1.6）/ UART6（3 压力计，DE=P2.3）9600 8N1 轮询；**从站** UART8（DE=P10.7，far 空口 PAOUT.7）服务大彩屏，float 按大端拆寄存器。

参考工程与工具：`FreeRTOS-STC32G-Demo/`（线 B 移植层来源，portasm/portmacro 逐字节相同）、`serial_test/`（独立 485 验证工程，已实测）、`STC-official-usys-UART-ISP-bootloader-demo-*/`（官方 bootloader 参考）、`stc-isp6.96z/`（烧录工具）、`modbus6/`（空目录，无内容）。

## 构建与烧录

**本机装了两套 Keil：`D:\Keil` 是 ARM 版 MDK，`D:\keil5C251` 才是 C251。C251 工程必须用后者**（UV4 自动探测会命中错误的那套）。

```bash
# 线 B 全量重建（UV4 退出码 0/1 均为成功，以日志 "0 Error(s)" 行为准；目标名 FreeRTOS_STC32G_Demo）
"D:/keil5C251/UV4/UV4.exe" -j0 -r "D:/流量压力检测集成装置/modbus6_rtos/FreeRTOS_STC32G12K.uvproj" -o "<日志路径>"
# 产物: modbus6_rtos/Objects/modbus6.hex
```

也可用 build-keil skill（`~/.claude/skills/build-keil/scripts/keil_builder.py`），首次需 `--uv4 "D:/keil5C251/UV4/UV4.exe"`，之后 profile（`--workspace` 下的 `.em_skill.json`）会缓存并自动复用。增量构建对改动的 .c/.A51 生效；port.c 与 sensors.c 配置为"先编译成 .SRC 再汇编"（Objects/port.src、sensors.src），直接看它们的 .src 才能看到真实汇编。

**线 A 的 .uvproj 不在本仓库**：原件在 `D:\STM32_Project\filter_backwashing_system_4g\hengshi_4g.uvproj`（源码与本仓库逐字节同源，末次构建 0 错 0 警；README 里 `hengshi_4g_c251.uvproj` 是笔误）。要在本仓库恢复编译，把该文件拷回根目录即可（包含路径唯一本机依赖：`D:\keil5C251\C251\INC\STC`）。

**烧录（STC-ISP，GUI 手动）**：`stc-isp6.96z/STC-ISP-v6.96Z.exe`，芯片 STC32G144K246，**IRC 必须选 24MHz**（两线所有波特率/定时器重装值都按 24MHz 硬算），看门狗不勾，hex 见上。历史教训：日志里出现过误选 `.c` 源文件而非 hex 导致 ISPRESULT=-1。

**没有自动化测试**。`serial_test/` 是一次性硬件回显验证工程（查询收发，验证了"收后发三件套"，结论已工程化进线 A 的 DRIVERS/driversuart.c）。

## C251 / STC32G 平台约束（跨工程的坑，改代码前先读）

- **far XFR 扩展寄存器**：K246 的 UART5~8、Timer5~8、P10/P11 等在 0x7EF7xx/0x7EFBxx far 空间，访问需 EAXFR（P_SW2|=0x80）。**线 B 全程 EAXFR 常开**（main.c 开头置位不回清，因此不能用 xdata 全局指针语义敏感的写法）；**线 A 必须 XSFR_ON()/XSFR_OFF() 成对切换**（EAXFR 常开会毁 xdata 全局变量访问）。线 A 的 PB/PA/P8 扩展口一律走 `STARTUP/config.h` 的 XSFR 宏。
- **上电 RAM 清零范围**：`START251.A51` 的 `EDATALEN` 是手写常量，必须 ≥ 链接实际 EDATA 用量（看 `Listings/modbus6.map`，当前 0x12BD，EDATALEN 已修为 0x1300）。链接器**不会**对"清零范围 < 用量"报警。线 B 的 Modbus 状态机变量另在 `Sensor_Init()` 显式清零双保险——新增 EDATA 变量时复查这两处。
- **STC32G 串口"收后发"三件套**（485 实测踩坑，违反会死循环）：发送 fire-and-forget（不等 TI 置位）；每帧发送前重新初始化串口清卡死；接收 ISR **先读 SBUF 再清 RI**。
- **IAP 读在该芯片恒返 0x00**：EEPROM 读已改 MOV 直读 `*(far*)(0xFC2800+偏移)`（0xFC2800 是 246K flash 在 24 位空间的基址），写/擦仍走 IAP，且 `IAP_ADDRE` 必须清 0、`IAP_TPS=24`。官方 bootloader 例程印证了基址但操作的是 AP 区不是 EEPROM 区。
- **FreeRTOS 移植层**（线 B）：tick=Timer0 1kHz；临界区=全局 EA 开关（无嵌套计数）；任务栈=heap_4（4KB，在 EDATA 不在 XDATA），每任务 256B；251 的上下文切换靠 portasm.h 的 ECALL 归一化帧。**未验证风险**：Timer3 ISR 内 `portEND_SWITCHING_ISR` 的栈帧与 C251 ISR 序言可能错配（STC 移植包自述未充分验证）——上板若跑飞，预案是把 TaskSensor 改轮询唤醒。中断里不要用会阻塞的 API。
- **线 A 阀口安全**：复位默认 0xFF 会使 10 路半桥同时直通（总电流 >20A 掉压跑飞），main.c 第一件事就是清阀口锁存器；Timer0 ISR 里有 480ms 脉冲保险丝强制断 NET60。动 valve/NET60 相关代码时保持这两个机制。
- **线 A 参数存储**：EEPROM 扇区 0=连续冲洗计数、扇区 1=参数（魔数+版本+XOR 校验，compare-dedup 防磨损）。改动 ParamConfig_t 布局必须升版本号。

## 当前状态与已知问题

- **git 仓库零提交、无远程**——所有文件未跟踪。首次大改动前建议先做初始提交。
- `modbus6_rtos`：编译 0 错 0 警，**尚未上板联调**。待办：S5CFG/S6CFG 波特率时钟源默认值待真机确认（config_app.h 注释标明）；传感器浮点系数"到货后按实测修正"（config_app.h）；彩屏寄存器表与 float 大端是按 VisualHMI 的单侧假设；485 发送等待用固定 2ms/判帧 5 tick 未验证。
- 线 A README 有三处过时（代码为准）：ADC 结果格式已改 `(ADC_RES<<4)|(ADC_RESL>>4)`；蜂鸣器实际在 PA.0（P5.5 是基础版网标残留）；uvproj 文件名笔误（见上）。
- 线 B 的 Modbus 寄存器映射定义在 `config_app.h` 注释（0x0000~0x005B，0x0050~0x005B 为六路采集窗口平均值），CRC16 在 `mb_crc.c`（0xA001 低字节先发）。
