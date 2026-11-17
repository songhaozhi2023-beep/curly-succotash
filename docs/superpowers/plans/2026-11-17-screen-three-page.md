# 大彩屏三页界面 + 定时采集 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 新建 VisualTFT 屏工程（标题页/实时数据页/定时采集页，Modbus 主机模式），并扩展 `modbus6_rtos` 固件：每通道定时采集窗口调度。

**Architecture:** 屏 = Modbus-RTU 主机（script.xml 变量表轮询 MCU 从站地址 1）；固件 = 从站（现有 `screen.c`）+ 调度器（`sensors.c` 按每路"开始/停止时刻"窗口开关轮询）；时间由屏端 `main.lua` 每分钟写入 0x0040~0x0046。设计全文见 `docs/superpowers/specs/2026-11-17-screen-three-page-design.md`（含 2026-11-17 调查修订记录）。

**Tech Stack:** Keil C251（STC32G144K246）+ FreeRTOS V10.4.6 移植层；广州大彩 VisualTFT 组态（工程 = .tftprj + 页面.tft + script.xml + main.lua，全部 XML/文本，本计划用 Python 生成器产出）。

---

## 前置知识（实现者必读）

| 事项 | 内容 |
|---|---|
| 构建命令 | `"D:/keil5C251/UV4/UV4.exe" -j0 -r "D:/流量压力检测集成装置/modbus6_rtos/FreeRTOS_STC32G12K.uvproj" -o "<日志>"`；**UV4 退出码 0/1 都算成功**，以日志里 `0 Error(s)` 行为准。C251 必须用 `D:/keil5C251`（不是 `D:/Keil`） |
| 代码风格 | C251 老式：变量声明在块首；`u8/u16/u32` 来自 type_def.h；far 空间寄存器经 `XREG`；EAXFR 常开（main.c 已置位，不许关） |
| 互斥纪律 | `g_sen`/`g_sched`/`g_time`/`g_run` 读写一律持 `xDataMutex`；屏任务写侧只入队不改状态 |
| 已核实枚举（勿再猜） | script.xml: `protocol="1"`=Modbus主机, `type="2"`=保持寄存器, `format="2"`=浮点(0=短整型,4=字符串), `byte_order="0"`=ABCD 与固件一致, `rw="0"` 全用, `screen_optimize="0"`=容许优化; .tftprj: `DeviceBaudRate="3"`=9600; 按钮 `button_style`: 0=瞬间 1=开关 3=置位(写1) 4=复位(写0); 输入控件 `input_mode="1"`+`text_type="0"` |
| CRC | 标准 Modbus RTU 低字节在前。fattyWater 的 `Crc16()` 返回前换序是假象，**固件 CRC 零改动** |
| 素材来源 | 按钮图：`D:/VisualTFT_Project/Project/images/{按钮 (1).png, 开始按钮.png, 急停按钮.png}`；字体：`D:/VisualTFT_Project/Project/font/`；XML 模板参照 `D:/STM32_Project/fattyWater3.0/Project3/`（另一个实样 `D:/VisualTFT_Project/Project3/`） |
| 屏工程输出 | `D:/VisualTFT_Project/FlowPress/`（新目录，其上层有独立 .git）；生成器/校验器放本仓库 `tools/` |
| Python | 本机 `python` 可用，跑生成的/磁盘来的脚本一律加 `-I` |

---

### Task 1: 固件——config_app.h 寄存器表与命令扩展

**Files:**
- Modify: `modbus6_rtos/config_app.h`

- [x] **Step 1.1: 命令常量增加 ARM/DISARM**

将（约 105~109 行）：
```c
/*================ 采集控制命令 (xCmdQueue 载荷) ================*/
#define CMD_FLOW_START      1
#define CMD_FLOW_STOP       2
#define CMD_PRESS_START     3
#define CMD_PRESS_STOP      4
```
替换为：
```c
/*================ 采集控制命令 (xCmdQueue 载荷) ================*/
#define CMD_FLOW_START      1
#define CMD_FLOW_STOP       2
#define CMD_PRESS_START     3
#define CMD_PRESS_STOP      4
#define CMD_SCHED_ARM       5      /* 定时采集总开关: 启用全部时刻表 */
#define CMD_SCHED_DISARM    6      /* 定时采集总开关: 取消并立即停止全部 */
```

- [x] **Step 1.2: 寄存器地址宏**

将：
```c
#define REG_PRESS_VALUE     0x0000
#define REG_FLOW_INST       0x0030
#define REG_FLOW_TOTAL      0x0031
#define REG_DEV_ADDR        0x0100   /* 改地址寄存器, 流量计侧需核对 */
```
替换为：
```c
#define REG_PRESS_VALUE     0x0000
#define REG_FLOW_INST       0x0030
#define REG_FLOW_TOTAL      0x0031
#define REG_DEV_ADDR        0x0100   /* 改地址寄存器, 流量计侧需核对 */

/*---- 屏接口寄存器 (screen.c 用; 映射表详见表尾注释) ----*/
#define REG_SCHED_ARM       0x0018   /* 屏写: 1=启用全部时刻表 0=取消并立即停止 */
#define REG_COLLECT_MASK    0x001A   /* 屏读: bit0~5 各路当前是否在采集窗口内 */
#define REG_SCHED_BASE      0x0020   /* 24 寄存器: 通道 i 的 [开始时,开始分,停止时,停止分] @ +4*i */
#define REG_TIME_BASE       0x0040   /* 7 寄存器: 年,月,日,时,分,秒,星期 (屏 Lua 每分钟写) */
```

- [x] **Step 1.3: extern 声明替换**

将：
```c
extern sensor_t xdata g_sen[6];         /* [0~2]=流量计1~3 [3~5]=压力计1~3 */
extern u8 xdata g_collect_flow;         /* 1=流量采集运行中 */
extern u8 xdata g_collect_press;        /* 1=压力采集运行中 */
```
替换为：
```c
extern sensor_t xdata g_sen[6];         /* [0~2]=流量计1~3 [3~5]=压力计1~3 */
extern u16 xdata g_sched[6][4];         /* 时刻表 [通道][开始时,开始分,停止时,停止分] */
extern u16 xdata g_time[7];             /* 屏 RTC [年,月,日,时,分,秒,星期] */
extern u8  xdata g_armed;               /* 1=时刻表已启用(屏按过"采集") */
extern u8  xdata g_time_valid;          /* 1=收到过屏的 RTC 喂时 */
extern u8  xdata g_run[6];              /* 调度评估结果: 1=该路在采集窗口内 */
```

- [x] **Step 1.4: 寄存器映射注释表更新**

将 `/* 大彩屏寄存器映射表 (屏侧 VisualHMI 配置参考) ... */` 整块注释（0x0000~0x0019 那段，含【写寄存器 屏→MCU】两行）替换为：
```c
/*=============================================================
 * 大彩屏寄存器映射表 (屏侧 FlowPress 工程 script.xml 对应)
 * 本机 Modbus 从站地址 = MB_ADDR_SCREEN (1)  9600 8N1
 * 数据类型: FLOAT=4字节大端(占2寄存器,byte_order=0/ABCD)  U16
 *
 *  【读寄存器 屏←MCU 03H】
 *  0x0000~B  流量1~3瞬时/压力1~3        FLOAT
 *  0x000C    在线位图 bit0~5            U16
 *  0x0010~5  流量1~3 累计               FLOAT  (预留)
 *  0x0016/7  流量/压力"任一路在采"回读   U16    (预留)
 *  0x001A    采集状态位图 bit0~5         U16
 *
 *  【写寄存器 屏→MCU 06H/10H】
 *  0x0018    定时采集总开关  1=启用 0=取消并立即停
 *  0x0020~37 6路x[开始时,开始分,停止时,停止分] 各U16 (时0~23 分0~59)
 *  0x0040~46 屏RTC: 年,月,日,时,分,秒,星期 各U16 (main.lua 每分钟写)
 *
 *  窗口规则: run[i] = armed && time_valid && (开始<=now<停止),
 *  开始==停止 视为无效窗口, 开始>停止 视为跨午夜窗口
 *  【异常码】01=不支持功能码 02=非法地址 03=非法数据
 *=============================================================*/
```

- [x] **Step 1.5: 提交**

```bash
cd "D:/流量压力检测集成装置" && git add modbus6_rtos/config_app.h && git commit -m "feat(screen): 寄存器表扩展——定时采集总开关/6路时刻表/RTC喂时寄存器"
```

---

### Task 2: 固件——sensors.c 调度核心

**Files:**
- Modify: `modbus6_rtos/sensors.c`

- [x] **Step 2.1: 全局变量替换**

将：
```c
sensor_t xdata g_sen[6];           /* 6路传感器数据: [0~2]=流量计 [3~5]=压力计 */
u8 xdata g_collect_flow  = 0;      /* 流量采集开关: 1=运行中, 由屏写 0x0018 控制 */
u8 xdata g_collect_press = 0;      /* 压力采集开关: 1=运行中, 由屏写 0x0019 控制 */
```
替换为：
```c
sensor_t xdata g_sen[6];           /* 6路传感器数据: [0~2]=流量计 [3~5]=压力计 */
u16 xdata g_sched[6][4];           /* 时刻表: [通道][开始时,开始分,停止时,停止分] */
u16 xdata g_time[7];               /* 屏 RTC: [年,月,日,时,分,秒,星期] */
u8  xdata g_armed      = 0;        /* 1=时刻表已启用 (屏写 0x0018=1) */
u8  xdata g_time_valid = 0;        /* 1=收到过屏 RTC (Lua 每分钟写 0x0040~46) */
u8  xdata g_run[6];                /* 调度结果: 1=该路在窗口内, Sensor_Poll 只轮询这些 */
```

- [x] **Step 2.2: 新增窗口判定与调度评估函数**

在 `SenWait2ms` 函数之后、`Sensor_Init` 之前插入：
```c
/*---------------- 定时采集调度 ----------------
 * 窗口判定(纯函数, 无副作用): 开始==停止 无效;
 * 开始<停止 常规窗口 [s,e); 开始>停止 跨午夜 [s,1440)+[0,e) */
static u8 Sched_WindowHit(u16 sh, u16 sm, u16 eh, u16 em, u16 nh, u16 nm)
{
    u16 s, e, n;
    if (sh > 23u || sm > 59u || eh > 23u || em > 59u) return 0;  /* 值域防御: 非法输入不产生窗口 */
    s = (u16)(sh * 60u + sm);
    e = (u16)(eh * 60u + em);
    n = (u16)(nh * 60u + nm);
    if (s == e) return 0;
    if (s < e)  return (u8)((n >= s) && (n < e));
    return (u8)((n >= s) || (n < e));
}

/* 依 g_sched/g_time/g_armed 重算 g_run[6]。
 * 调用方必须已持 xDataMutex。时间未同步或未启用时全 0。 */
static void Sched_UpdateRuns(void)
{
    u8 i;
    if (!g_armed || !g_time_valid) {
        g_run[0] = 0; g_run[1] = 0; g_run[2] = 0;
        g_run[3] = 0; g_run[4] = 0; g_run[5] = 0;
        return;
    }
    for (i = 0; i < 6; i++) {
        g_run[i] = Sched_WindowHit(g_sched[i][0], g_sched[i][1],
                                   g_sched[i][2], g_sched[i][3],
                                   g_time[3], g_time[4]);
    }
}
```

- [x] **Step 2.3: Sensor_Init 清零新变量**

在 `Sensor_Init` 中，紧挨 `m_state = 0;` 那段状态机清零之后（`/* 初始所有传感器标记为离线 */` 之前）插入：
```c
    /* 新增调度变量清零 (xdata 上电不定值, 沿用双保险惯例) */
    for (i = 0; i < 6; i++) {
        g_sched[i][0] = 0; g_sched[i][1] = 0;
        g_sched[i][2] = 0; g_sched[i][3] = 0;
        g_run[i] = 0;
    }
    for (i = 0; i < 7; i++) g_time[i] = 0;
    g_armed = 0;
    g_time_valid = 0;
```

- [x] **Step 2.4: Sen_StartReq 换成按通道跳过**

将（152~166 行）：
```c
    /* 采集开关: 未开启的总线直接跳过并标离线 */
    if (m_cur < 3) {
        if (!g_collect_flow) {
            g_sen[m_cur].online = 0;    /* 标离线, 屏上显示断连 */
            m_cur = 3;                  /* 跳到压力总线第一个传感器 */
        }
    }
    if (m_cur >= 3) {
        if (!g_collect_press) {
            g_sen[m_cur].online = 0;
            m_cur = 0;                  /* 跳回流量总线第一个传感器 */
        }
    }
    /* 两条总线都关: 本轮不发请求, 等下个节拍 */
    if ((m_cur < 3 && !g_collect_flow) || (m_cur >= 3 && !g_collect_press)) return;
```
替换为（`u8 k;` 加到函数顶部现有声明区 `u8 addr;` 之后）：
```c
    /* 采集窗口: 跳过不在窗口内的通道并标离线。g_run 与本函数同在
     * TaskSensor 上下文(写者=读者, 无竞争), u8 读取天然原子, 免锁安全;
     * online 同为 u8 且此路是唯一写者(解析/超时路径也在本任务), 免锁良性 */
    for (k = 0; k < 6 && !g_run[m_cur]; k++) {
        g_sen[m_cur].online = 0;        /* 窗口外标离线, 屏显示断连 */
        m_cur = (u8)((m_cur + 1) % 6);
    }
    if (k == 6) return;                 /* 六路全不在窗口内: 本轮不发请求 */
```
注意函数声明区改为 `u16 crc; u8 addr, k;`。

- [x] **Step 2.5: Sensor_Poll 队列分支与调度刷新**

将队列处理 switch：
```c
        switch (cmd) {
        case CMD_FLOW_START:  g_collect_flow  = 1; break;
        case CMD_FLOW_STOP:   g_collect_flow  = 0; break;
        case CMD_PRESS_START: g_collect_press = 1; break;
        case CMD_PRESS_STOP:  g_collect_press = 0; break;
        }
```
替换为：
```c
        switch (cmd) {
        case CMD_FLOW_START:  break;   /* 旧立即启停语义已废弃(空实现防旧屏) */
        case CMD_FLOW_STOP:   break;
        case CMD_PRESS_START: break;
        case CMD_PRESS_STOP:  break;
        case CMD_SCHED_ARM:   g_armed = 1; break;   /* 启用: 窗口内通道立即开采 */
        case CMD_SCHED_DISARM: g_armed = 0; break;  /* 取消: 本 tick 调度刷新即全停, 在途事务收尾 */
        }
```
并在 `while (xQueueReceive(...))` 循环结束后、`/* 状态0: 发请求 */` 之前插入：
```c
    /* 每 10ms 依最新时刻表/RTC 重算各路采集窗口 */
    xSemaphoreTake(xDataMutex, portMAX_DELAY);
    Sched_UpdateRuns();
    xSemaphoreGive(xDataMutex);
```

- [x] **Step 2.6: 提交**

```bash
cd "D:/流量压力检测集成装置" && git add modbus6_rtos/sensors.c && git commit -m "feat(sensor): 每通道定时采集窗口调度(g_sched/g_time/g_run)"
```

---

### Task 3: 固件——screen.c 从站读写扩展

**Files:**
- Modify: `modbus6_rtos/screen.c`
- Modify: `modbus6_rtos/config_app.h`（Step 3.0 微补充）

- [x] **Step 3.0: config_app.h 微补充**（Task 1 质量审查第 2/3/4 条）

在 `#define REG_TIME_BASE 0x0040` 行之后追加：
```c
#define REG_SCHED_NUM       (6u * 4u)  /* 时刻表寄存器数 24 */
#define REG_TIME_NUM        7u         /* RTC 寄存器数 */
```
映射注释表两行改为：
```
 *  0x0018    定时采集总开关  1=启用 0=取消并立即停 (读回=当前使能状态)
 *  0x0040~46 屏RTC: 年(任意),月(1~12),日(1~31),时(0~23),分(0~59),秒(0~59),星期(1~7) 各U16 (main.lua 每分钟写)
```

- [x] **Step 3.1: Scr_GetReg 增加读侧寄存器**

将：
```c
    /* 0x0016/0x0017: 采集运行状态回读 (1=采集中 0=已停止) */
    if (a == 0x0016) return g_collect_flow;
    if (a == 0x0017) return g_collect_press;
    return 0;   /* 未映射地址返回 0 */
```
替换为（`u8 st, idx;` 声明改为 `u8 st, idx, k;`）：
```c
    /* 0x0018: 定时采集总开关读回 (屏变体/按钮状态显示用) */
    if (a == REG_SCHED_ARM) return g_armed;
    /* 0x0016/0x0017: 总线级"任一路在采"回读 (预留) */
    if (a == 0x0016) return (u8)((g_run[0] | g_run[1] | g_run[2]) ? 1 : 0);
    if (a == 0x0017) return (u8)((g_run[3] | g_run[4] | g_run[5]) ? 1 : 0);
    /* 0x001A: 采集状态位图 bit0~5 */
    if (a == REG_COLLECT_MASK) {
        st = 0;
        for (k = 0; k < 6; k++) {
            if (g_run[k]) st |= (u8)(1u << k);
        }
        return st;
    }
    return 0;   /* 未映射地址返回 0 */
```

- [x] **Step 3.2: Scr_SetReg 整体替换**

返回值约定改为 `1=成功 0=非法地址 2=非法数据值`。整体替换为：
```c
/* 写寄存器: 屏写采集使能/时刻表/RTC
 * 返回 1=成功 0=地址不合法 2=数据值不合法 */
static u8 Scr_SetReg(u16 a, u16 v)
{
    u8 cmd, idx, off;

    /* 0x0018: 定时采集总开关 (1=启用全部时刻表 0=取消并立即停) */
    if (a == REG_SCHED_ARM) {
        cmd = v ? CMD_SCHED_ARM : CMD_SCHED_DISARM;
        xQueueSend(xCmdQueue, &cmd, 0);   /* 非阻塞发送, 队列满则丢弃 */
        return 1;
    }
    /* 0x0020~0x0037: 6 路时刻表 [开始时,开始分,停止时,停止分] */
    if (a >= REG_SCHED_BASE && a < (REG_SCHED_BASE + REG_SCHED_NUM)) {
        idx = (u8)((a - REG_SCHED_BASE) >> 2);        /* 通道 0~5 */
        off = (u8)((a - REG_SCHED_BASE) & 3u);        /* 0/2=时 1/3=分 */
        if (v > (u16)((off & 1u) ? 59u : 23u)) return 2;
        xSemaphoreTake(xDataMutex, portMAX_DELAY);
        g_sched[idx][off] = v;
        xSemaphoreGive(xDataMutex);
        return 1;
    }
    /* 0x0040~0x0046: 屏 RTC (年,月,日,时,分,秒,星期) */
    if (a >= REG_TIME_BASE && a < (REG_TIME_BASE + REG_TIME_NUM)) {
        xSemaphoreTake(xDataMutex, portMAX_DELAY);
        g_time[a - REG_TIME_BASE] = v;
        g_time_valid = 1;
        xSemaphoreGive(xDataMutex);
        return 1;
    }
    return 0;
}
```

- [x] **Step 3.3: Scr_Process 适配新返回值**

06H 分支，将：
```c
        if (!Scr_SetReg(a, v)) { Scr_Exc(fc, 0x02); return; }  /* 非法地址 */
```
替换为：
```c
        {                       /* 0=非法地址 2=非法数据值 */
            u8 r = Scr_SetReg(a, v);
            if (r == 0) { Scr_Exc(fc, 0x02); return; }
            if (r == 2) { Scr_Exc(fc, 0x03); return; }
        }
```
10H 分支，将：
```c
            if (!Scr_SetReg((u16)(a + i), v)) { Scr_Exc(fc, 0x02); return; }
```
替换为：
```c
            {   u8 r = Scr_SetReg((u16)(a + i), v);
                if (r == 0) { Scr_Exc(fc, 0x02); return; }  /* 注: 多写中途
                非法值会留下半截数据; 屏端键盘已限幅, 此路径仅防异常帧 */
                if (r == 2) { Scr_Exc(fc, 0x03); return; } }
```

- [x] **Step 3.4: 03H/10H 数量上限放宽**（Task 1 质量审查第 1 条——屏引擎会把 0x0020~0x0037 连续 24 寄存器整块读/写，原上限会回异常导致时刻表读回/批量写失效）

03H 分支，将：
```c
        if (q < 1 || q > 16) { Scr_Exc(fc, 0x03); return; }  /* 数量超出范围 */
```
替换为：
```c
        if (q < 1 || q > 29) { Scr_Exc(fc, 0x03); return; }  /* 29=s_tx 64B 上限(3+2q+2<=64), 需容下时刻表整块 q=24 */
```
10H 分支，将：
```c
        if (q < 1 || q > 8 || s_rx[6] != (u8)(q << 1)) { Scr_Exc(fc, 0x03); return; }
```
替换为：
```c
        if (q < 1 || q > 26 || s_rx[6] != (u8)(q << 1)) { Scr_Exc(fc, 0x03); return; }  /* 26=s_rx 64B 上限(11+2q<=64) */
```

- [x] **Step 3.5: 文件头注释同步**

将文件头职责注释里 `06H/10H 写 -> 屏下发流量/压力采集启停命令` 一行改为 `06H/10H 写 -> 采集使能/6路时刻表/屏RTC, 经 xCmdQueue 或直接入表`。

- [x] **Step 3.6: 提交**

```bash
cd "D:/流量压力检测集成装置" && git add modbus6_rtos/screen.c && git commit -m "feat(screen): 从站写处理扩展(时刻表/RTC/总开关)+状态位图回读"
```

---

### Task 4: 固件全量构建验证

- [x] **Step 4.1: 全量重建**

```bash
"D:/keil5C251/UV4/UV4.exe" -j0 -r "D:/流量压力检测集成装置/modbus6_rtos/FreeRTOS_STC32G12K.uvproj" -o "D:/流量压力检测集成装置/modbus6_rtos/build_flowpress.log"
```
（命令立即返回属正常，等 1~2 分钟后看日志；也可用 build-keil skill。）

- [x] **Step 4.2: 检查日志**

```bash
grep -E "Error|Warning|0 Error" "D:/流量压力检测集成装置/modbus6_rtos/build_flowpress.log" | tail -5
```
Expected: `0 Error(s), 0 Warning(s)`。若有错：C251 报错行号定位修改（常见坑：变量声明必须在块首；u16 与 int 混算注意；far 指针别当普通指针），改后重跑直到 0 错 0 警。

- [ ] **Step 4.3: 若有修复则提交**

```bash
cd "D:/流量压力检测集成装置" && git add -A modbus6_rtos && git commit -m "fix: 构建修复(C251)"
```
（0 错 0 警则无此步。产物 `modbus6_rtos/Objects/modbus6.hex` 供烧录。）

---

### Task 5: 屏工程生成器

**Files:**
- Create: `tools/gen_flowpress_screen.py`
- 产出（不在本仓库）: `D:/VisualTFT_Project/FlowPress/` 下 `FlowPress.tftprj`、`标题页.tft`、`实时数据页.tft`、`定时采集页.tft`、`script.xml`、`main.lua`、`Images/`、`font/`

- [x] **Step 5.1: 写生成器**（完整文件如下）

```python
# -*- coding: utf-8 -*-
"""FlowPress 大彩屏工程生成器 (DC80480DN070 7寸800x480, Modbus 主机 9600 8N1)
用法: python -I tools/gen_flowpress_screen.py
输出: D:/VisualTFT_Project/FlowPress/  (目录已存在则拒绝运行, 防误覆盖)
依据: docs/superpowers/specs/2026-11-17-screen-three-page-design.md
XML 字段模板逐字取自实样: D:/STM32_Project/fattyWater3.0/Project3/ 与
D:/VisualTFT_Project/Project/ (水肥), 枚举经二进制 schema 核实(见规格 §3.3)。
"""
import os, shutil, sys

ROOT     = r"D:/VisualTFT_Project/FlowPress"
IMG_SRC  = r"D:/VisualTFT_Project/Project/images"
FONT_SRC = r"D:/VisualTFT_Project/Project/font"

# ---- 颜色 (深色工业风, 与水肥工程同底色) ----
BG       = "15;30;50"
TITLE_C  = "102;255;255"
WHITE    = "255;255;255"
FLOW_C   = "0;255;255";    FLOW_BG  = "18;44;72"
PRESS_C  = "255;210;138";  PRESS_BG = "44;36;18"
LINE_C   = "42;74;116"
INP_FG   = "0;0;0";        INP_BG   = "255;255;255"

# ---- 字体 (fonts.xml: 6=20px 7=22px 8=24px 12=32px 13=36px 17=52px 4=16px) ----
F_BIG, F_H1, F_LBL, F_VAL, F_SML, F_HDR = 17, 13, 8, 12, 6, 4

def esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")

def text_item(i, name, t, x, y, w, h, font, fg, align=1, valign=1):
    t15 = " ".join(f'text{n}=""' for n in range(1, 16))
    return (f'<item name="{name}" id="{i}" type="text" multi_lang="1" '
            f'text_align="{align}" text_align_v="{valign}" text="{esc(t)}" {t15} '
            f'font="{font}" encode="0" show_bk="0" fore_color="{fg}" '
            f'bk_color="{WHITE}" xOffset="{x}" yOffset="{y}" width="{w}" '
            f'height="{h}" row_gap="0" col_gap="0"/>')

def display_item(i, name, t, x, y, w, h, font, fg, bkg, bind="",
                 editable=False, vmax=0, vmin=0):
    im, vl = (1, 1) if editable else (0, 0)
    return (f'<item name="{name}" id="{i}" type="text_display" text="{esc(t)}" '
            f'tipinfo="" font="{font}" encode="1" '
            f'show_bkg_style="{1 if editable else 0}" fore_color="{fg}" '
            f'bkg_color="{bkg}" bkg_image_path="" xOffset="{x}" yOffset="{y}" '
            f'width="{w}" height="{h}" input_mode="{im}" variant="0" '
            f'text_type="0" text_len_max="255" password="0" focus_rect="0" '
            f'text_align="1" text_align_v="1" value_limit="{vl}" '
            f'value_precision="0" max_value="{vmax}" min_value="{vmin}" '
            f'keyboard_init="0" keyboard_position="0" keyboard_x="0" '
            f'keyboard_y="0" art_digit="0" art_digit_icon="" half_width_dot="0" '
            f'bind_variant="{bind}" show_condition="0" condition_variant="" '
            f'condition_value="0"/>')

def button_item(i, name, x, y, w, h, img, text, style=0, switch="", bind="",
                font=F_LBL, fg=WHITE):
    e = esc(text)
    return (f'<item name="{name}" id="{i}" type="button" button_type="1" '
            f'focus="1" notify_disable="0" key_code="a" key_type="0" '
            f'init_state="0" button_style="{style}" longpress_delay="0" '
            f'url_down="{img}" url_up="{img}" popup_menu_id="0" '
            f'input_text_id="0" switch="{switch}" switch_effect="0" '
            f'switch_area="0" switch_area_left="0" switch_area_right="0" '
            f'switch_area_top="0" switch_area_bottom="0" action="" '
            f'xOffset="{x}" yOffset="{y}" width="{w}" height="{h}" cut_up="0" '
            f'cut_up_offset_x="0" cut_up_offset_y="0" cut_down="0" '
            f'cut_down_offset_x="0" cut_down_offset_y="0" custom_data_up="" '
            f'custom_data_down="" external_data_up="" external_data_down="" '
            f'external_data_delay="100" child_screen="0" need_login="0" '
            f'login_password="888888" show_text_state="1" font="{font}" '
            f'font_color_up="{fg}" font_color_down="{fg}" '
            f'text_state_up="{e}" text_state_down="{e}" bind_variant="{bind}" '
            f'show_condition="0" condition_variant="" condition_value="0"/>')

def line_item(i, name, x0, y0, x1, y1, color=LINE_C):
    return (f'<item name="{name}" id="{i}" type="line" x0="{x0}" y0="{y0}" '
            f'x1="{x1}" y1="{y1}" pen_width="1" pen_color="{color}"/>')

def page(name, items):
    return (f'<DrawPage name="{name}" bk_transparent="0" bk_color="{BG}" '
            f'bk_image="" size_option="0" width="800" height="480">'
            + "".join(items) + "</DrawPage>")

def variant(name, addr, fmt=0, dec=0, txtfmt=0):
    return (f'<variant name="{esc(name)}" length="16" slave="1" addr="{addr}" '
            f'sub_addr="0" type="2" rw="0" init_variant="0" restore_on_err="0" '
            f'value="0" string_value="" char_set="0" char_order="0" '
            f'format="{fmt}" byte_count="32" sign="0" byte_order="0" '
            f'factor_enable="0" factor="1" offset="0" text_format="{txtfmt}" '
            f'int_fill_zero="0" dec_fill_zero="{dec}" show_invalid_zero="0" '
            f'system_variant="0" save_data="0" is_array="0" array_size="10"/>')

def wfile(path, content):
    with open(path, "w", encoding="utf-8", newline="\r\n") as f:
        f.write(content)
    print("写", path)

# ============ 页面 ============
def build_title():
    return page("标题页", [
        text_item(1, "Text1", "流量压力集成检测仪器", 80, 150, 640, 70, F_BIG, TITLE_C),
        button_item(2, "BtnEnter", 355, 300, 90, 44, r"Images\btn.png", "进  入",
                    style=0, switch="实时数据页"),
    ])

ROWS = (130, 242, 354)          # 第2页行顶
FLOWN = ("流量 1", "流量 2", "流量 3")
PRESSN = ("压力 1", "压力 2", "压力 3")
FLOTV = ("流量1瞬时", "流量2瞬时", "流量3瞬时")
PRESSV = ("压力1", "压力2", "压力3")

def build_data():
    it = [
        button_item(1, "BtnBack", 8, 10, 90, 36, r"Images\btn.png", "返 回",
                    style=0, switch="标题页", font=F_SML),
        text_item(2, "TextTitle", "流量压力集成检测仪器", 200, 10, 400, 44, F_H1, TITLE_C),
        button_item(3, "BtnSched", 702, 10, 90, 36, r"Images\btn.png", "定时设置",
                    style=0, switch="定时采集页", font=F_SML),
        text_item(4, "TextFH", "流量（m³/h）", 30, 68, 240, 26, F_LBL, FLOW_C, align=0),
        text_item(5, "TextPH", "压力（MPa）", 430, 68, 240, 26, F_LBL, PRESS_C, align=0),
        line_item(6, "LineSep", 400, 64, 400, 480),
    ]
    k = 10
    for r, y in enumerate(ROWS):
        it.append(text_item(k, f"TextFL{r+1}", FLOWN[r], 30, y + 18, 140, 24, F_LBL, WHITE, align=0)); k += 1
        it.append(text_item(k, f"TextPL{r+1}", PRESSN[r], 430, y + 18, 140, 24, F_LBL, WHITE, align=0)); k += 1
        it.append(display_item(k, f"ValF{r+1}", "0.000", 180, y, 200, 60, F_VAL, FLOW_C, FLOW_BG, bind=FLOTV[r])); k += 1
        it.append(display_item(k, f"ValP{r+1}", "0.000", 580, y, 200, 60, F_VAL, PRESS_C, PRESS_BG, bind=PRESSV[r])); k += 1
    return page("实时数据页", it)

SROW = (78, 135, 192, 249, 306, 363)   # 第3页行顶 (表头占 54~72)
SCHED_V = tuple(f"{ch}{k}" for ch in FLOTV + PRESSV
                for k in ("开始时", "开始分", "停止时", "停止分"))
SCHED_LBL = FLOWN + PRESSN

GRAY = "138;170;170"

def build_sched():
    it = [
        button_item(1, "BtnBack", 8, 10, 90, 36, r"Images\btn.png", "返 回",
                    style=0, switch="实时数据页", font=F_SML),
        text_item(2, "TextTitle", "定时采集设置", 200, 10, 400, 44, F_H1, TITLE_C),
        text_item(3, "HdS1", "开始 时", 100, 54, 80, 18, F_HDR, GRAY),
        text_item(4, "HdS2", "分", 180, 54, 64, 18, F_HDR, GRAY),
        text_item(5, "HdE1", "停止 时", 262, 54, 80, 18, F_HDR, GRAY),
        text_item(6, "HdE2", "分", 342, 54, 64, 18, F_HDR, GRAY),
        text_item(7, "HdV", "实时值", 580, 54, 200, 18, F_HDR, GRAY),
    ]
    k = 10
    for r, y in enumerate(SROW):
        it.append(text_item(k, f"TextL{r+1}", SCHED_LBL[r], 16, y + 10, 90, 24, F_LBL, WHITE, align=0)); k += 1
        for c in range(4):   # 开始时/开始分/停止时/停止分
            it.append(display_item(k, f"Inp{r+1}_{c}", "0", 110 + c * 74, y, 64, 44,
                                   F_LBL, INP_FG, INP_BG, bind=SCHED_V[r * 4 + c],
                                   editable=True,
                                   vmax=(23 if c in (0, 2) else 59))); k += 1
        it.append(display_item(k, f"Val{r+1}", "0.000", 580, y, 200, 44, F_VAL,
                               FLOW_C if r < 3 else PRESS_C,
                               FLOW_BG if r < 3 else PRESS_BG,
                               bind=(FLOTV + PRESSV)[r])); k += 1
    it.append(button_item(k, "BtnCollect", 280, 424, 110, 44, r"Images\btn_start.png", "采 集",
                          style=3, bind="采集使能", font=F_LBL)); k += 1
    it.append(button_item(k + 1, "BtnStop", 410, 424, 110, 44, r"Images\btn_stop.png", "停 止",
                          style=4, bind="采集使能", font=F_LBL))
    return page("定时采集页", it)

# ============ script.xml ============
def build_script():
    v = [variant("流量1瞬时", 0, fmt=2, dec=3, txtfmt=1),
         variant("流量2瞬时", 2, fmt=2, dec=3, txtfmt=1),
         variant("流量3瞬时", 4, fmt=2, dec=3, txtfmt=1),
         variant("压力1",     6, fmt=2, dec=3, txtfmt=1),
         variant("压力2",     8, fmt=2, dec=3, txtfmt=1),
         variant("压力3",    10, fmt=2, dec=3, txtfmt=1),
         variant("在线位图", 12),
         variant("流量1累计", 16, fmt=2, dec=3, txtfmt=1),
         variant("流量2累计", 18, fmt=2, dec=3, txtfmt=1),
         variant("流量3累计", 20, fmt=2, dec=3, txtfmt=1),
         variant("采集使能", 24),
         variant("采集状态", 26)]
    v += [variant(n, 32 + i) for i, n in enumerate(SCHED_V)]
    v += [variant(n, 64 + i) for i, n in
          enumerate(("RTC年", "RTC月", "RTC日", "RTC时", "RTC分", "RTC秒", "RTC星期"))]
    return ('<script protocol="1" version="1" slave="1" cycle_time="200" '
            'pool_delay_time="10" pool_mode="1" response_timeout="100" '
            'mulitple_read="64" screen_optimize="0" save_data="0" '
            'save_data_flag="0" enable_script="1" dgus_frame_head="23205">'
            '<variants>\r\n' + "\r\n".join(v) + '\r\n</variants><instructs/></script>')

MAIN_LUA = '''------------------------------------
-- FlowPress: 每分钟把屏 RTC 喂给 MCU (寄存器 0x0040~0x0046)
-- 参照 Project3 已验证写法: get_date_time + set_variant + 蔡勒星期
-- 注意: 请用 VisualTFT 自带 Lua 编辑器之外的工具编辑后,
--       关闭 VisualTFT 的 Lua 视图再打开工程(其自动保存会回滚外部修改)
------------------------------------

local rtcSyncMin = -1

local function calc_weekday(y, mo, d)
    local t = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4}
    if mo < 3 then y = y - 1 end
    local w = (y + math.floor(y / 4) - math.floor(y / 100)
                 + math.floor(y / 400) + t[mo] + d) % 7
    if w == 0 then w = 7 end
    return w
end

function on_systick()
    local sy, smo, sd, sh, smi = get_date_time()
    if smi ~= rtcSyncMin then
        rtcSyncMin = smi
        set_variant("RTC年", sy)
        set_variant("RTC月", smo)
        set_variant("RTC日", sd)
        set_variant("RTC时", sh)
        set_variant("RTC分", smi)
        set_variant("RTC秒", 0)
        set_variant("RTC星期", calc_weekday(sy, smo, sd))
    end
end
'''

TFTPRJ = '''<?xml version="1.0" encoding="utf-8"?>
<VisualTFT Name="FlowPress" OutputDirectory="output\\" StartupPage="标题页" StartupAction="" StartupActionLoop="1" DeviceType="21014" DeviceEnableControl="1" DeviceEnableTouchPane="1" DeviceEnableBuzzer="2" DeviceEnableCRC="0" DeviceBaudRate="3" DeviceCoordinateUpdateMode="4" DeviceControlNotify="3" DeviceScreenNotify="1" DeviceScreenRvs="0" DeviceBacklightAutoControl="0" DeviceBacklightTime="10" DeviceBacklightOn="200" DeviceBacklightOff="50" DeviceBacklightNotify="0" DeviceLockConfig="1" DeviceStartupVoice="0" DeviceVoiceVolume="100" DeviceEnableSlidingScreen="0" BeginSlidingScreen="" EndSlidingScreen="" CompatibleScreenRotate="1" AddResourceFloder="1" FastRefreshScreen="0" LUAPrecompile="0" DelayInitScreen="0" ImageDithering="1" ImageFormat="1" JPEGQuality="95" StartupLogo="" DHCP="0" IPAddress="192.168.1.100" NetMask="255.255.255.0" Gateway="192.168.1.1" DNS="192.168.1.1" NetworkMode="0" ServerAddress="192.168.1.200" ServicePort="5050" NetworkTransfer="0" WifiMode="0" WifiSSID="" WifiPassword="" FlashType="1" PartitionSize0="100" PartitionSize1="10" PartitionSize2="0" Repartition="1" FormatPartition="1" UartDownloadSpeed="0" PKGSetting="0" PKGControl="1" PKGImage="1" PKGFont="1" PKGOther="1" Version="1.0" EnableSlidingRange="0" EnableKeysound="0" BacklightAutoControl="0" BacklightNotify="0" StandbyPage="" ServerPort="5050" WifiSSI="">
  <Pages><Page RelativePath="标题页.tft"/><Page RelativePath="实时数据页.tft"/><Page RelativePath="定时采集页.tft"/></Pages>
  <Images><Image RelativePath="Images\\btn.png" Download="0"/><Image RelativePath="Images\\btn_start.png" Download="0"/><Image RelativePath="Images\\btn_stop.png" Download="0"/></Images>
  <Waves/>
</VisualTFT>'''

def main():
    if os.path.exists(ROOT):
        sys.exit(f"拒绝运行: {ROOT} 已存在 (防误覆盖)。确认无用后手动删除再跑。")
    os.makedirs(os.path.join(ROOT, "Images"))
    # 字体整目录拷贝 (fonts.xml + 编译好的字库)
    shutil.copytree(FONT_SRC, os.path.join(ROOT, "font"))
    # 按钮底图 (来自水肥工程, 改 ASCII 名)
    imgs = {r"按钮 (1).png": "btn.png",
            r"开始按钮.png": "btn_start.png",
            r"急停按钮.png": "btn_stop.png"}
    for src, dst in imgs.items():
        p = os.path.join(IMG_SRC, src)
        if not os.path.exists(p):
            sys.exit(f"缺素材: {p}")
        shutil.copy(p, os.path.join(ROOT, "Images", dst))
    wfile(os.path.join(ROOT, "FlowPress.tftprj"), TFTPRJ)
    wfile(os.path.join(ROOT, "标题页.tft"), build_title())
    wfile(os.path.join(ROOT, "实时数据页.tft"), build_data())
    wfile(os.path.join(ROOT, "定时采集页.tft"), build_sched())
    wfile(os.path.join(ROOT, "script.xml"), build_script())
    wfile(os.path.join(ROOT, "main.lua"), MAIN_LUA)
    print("完成:", ROOT)

if __name__ == "__main__":
    main()
```

（生成器各页控件坐标与规格 §3.2 一致，仅第 3 页行起点从 y=70 调至 y=78 给表头留 18px——偏差已核对不影响布局。）

- [x] **Step 5.2: 运行生成器**

```bash
python -I "D:/流量压力检测集成装置/tools/gen_flowpress_screen.py"
```
Expected: 打印 6 个"写 ..." 行 + `完成: D:/VisualTFT_Project/FlowPress`。

- [x] **Step 5.3: 核对产物树**

```bash
find "D:/VisualTFT_Project/FlowPress" -maxdepth 2 | head -20
```
Expected: FlowPress.tftprj、3 个 .tft、script.xml、main.lua、Images/3 张图、font/。

---

### Task 6: 屏工程校验器（含窗口逻辑镜像测试）

**Files:**
- Create: `tools/validate_flowpress_screen.py`

- [x] **Step 6.1: 写校验器**（完整文件如下）

```python
# -*- coding: utf-8 -*-
"""FlowPress 屏工程校验器
用法: python -I tools/validate_flowpress_screen.py   (退出码 0=全过 1=有问题)
检查: XML 良构 / 工程参数 / 变量表地址与固件映射一致 / 控件绑定与画面切换 /
坐标边界 / 字体存在 / 输入限幅 / 按钮置位复位 / 窗口判定语义(镜像固件算法)
"""
import os, sys
import xml.etree.ElementTree as ET

ROOT = r"D:/VisualTFT_Project/FlowPress"
PAGES = ("标题页", "实时数据页", "定时采集页")
fails = []

def chk(cond, msg):
    if not cond:
        fails.append(msg)

# ---- 1. XML 良构 ----
roots = {}
for f in PAGES:
    try:
        roots[f] = ET.parse(os.path.join(ROOT, f + ".tft")).getroot()
    except Exception as e:
        fails.append(f"{f}.tft 解析失败: {e}")
proj = script = fonts = None
try:
    proj = ET.parse(os.path.join(ROOT, "FlowPress.tftprj")).getroot()
    script = ET.parse(os.path.join(ROOT, "script.xml")).getroot()
    fonts = ET.parse(os.path.join(ROOT, "font", "fonts.xml")).getroot()
except Exception as e:
    fails.append(f"工程文件解析失败: {e}")
if fails:
    print("FAIL:\n" + "\n".join(fails)); sys.exit(1)

# ---- 2. 工程参数 ----
chk(proj.get("DeviceType") == "21014", "DeviceType != 21014 (DC80480DN070)")
chk(proj.get("DeviceBaudRate") == "3", "DeviceBaudRate != 3 (应为 9600)")
chk(proj.get("StartupPage") == "标题页", "StartupPage != 标题页")
plist = [p.get("RelativePath") for p in proj.find("Pages")]
chk(sorted(plist) == sorted(f + ".tft" for f in PAGES), f"Pages 不符: {plist}")
chk(script.get("protocol") == "1" and script.get("slave") == "1", "script 根参数")
chk(script.get("cycle_time") == "200" and script.get("screen_optimize") == "0",
    "轮询周期/优化读取参数")

# ---- 3. 变量表与固件映射一致 ----
vs = script.findall("./variants/variant")
names = {v.get("name"): v for v in vs}
addr = {v.get("name"): int(v.get("addr")) for v in vs}
expect = {"流量1瞬时": 0, "流量2瞬时": 2, "流量3瞬时": 4,
          "压力1": 6, "压力2": 8, "压力3": 10,
          "在线位图": 12, "流量1累计": 16, "流量2累计": 18, "流量3累计": 20,
          "采集使能": 24, "采集状态": 26,
          "RTC年": 64, "RTC月": 65, "RTC日": 66, "RTC时": 67,
          "RTC分": 68, "RTC秒": 69, "RTC星期": 70}
for n, a in expect.items():
    chk(n in addr and addr[n] == a, f"变量 {n} 地址应为 {a}, 实为 {addr.get(n)}")
chn = ("流量1", "流量2", "流量3", "压力1", "压力2", "压力3")
for ci, ch in enumerate(chn):
    for ki, k in enumerate(("开始时", "开始分", "停止时", "停止分")):
        n = ch + k
        chk(n in addr and addr[n] == 32 + ci * 4 + ki, f"时刻变量 {n} 地址")
chk(len(vs) == 43, f"variant 数 {len(vs)} != 43")
addrs = [v.get("addr") for v in vs]
chk(len(addrs) == len(set(addrs)), "变量地址有重复")
for v in vs:
    chk(v.get("type") == "2" and v.get("rw") == "0" and v.get("byte_order") == "0",
        f"变量 {v.get('name')} type/rw/byte_order 不符")
for n in ("流量1瞬时", "流量2瞬时", "流量3瞬时", "压力1", "压力2", "压力3",
          "流量1累计", "流量2累计", "流量3累计"):
    chk(names[n].get("format") == "2", f"{n} format != 2 (浮点)")
for n in ("RTC年", "RTC月", "RTC日", "RTC时", "RTC分", "RTC秒", "RTC星期"):
    chk(n in names, f"缺 RTC 变量 {n} (main.lua set_variant 需要同名)")

# ---- 4. 页面控件: 绑定/切换/坐标 ----
for pn, rt in roots.items():
    for it in rt.iter("item"):
        x, y = int(it.get("xOffset", 0)), int(it.get("yOffset", 0))
        w, h = int(it.get("width", 0)), int(it.get("height", 0))
        chk(x >= 0 and y >= 0 and x + w <= 800 and y + h <= 480,
            f"{pn}/{it.get('name')} 越界 ({x},{y},{w},{h})")
        b = it.get("bind_variant", "")
        if b:
            chk(b in names, f"{pn}/{it.get('name')} 绑定未知变量 {b}")
        sw = it.get("switch", "")
        if sw:
            chk(sw in PAGES, f"{pn}/{it.get('name')} 切换到未知画面 {sw}")

disp2 = [it for it in roots["实时数据页"].iter("item")
         if it.get("type") == "text_display" and it.get("input_mode") == "0"]
chk(len([d for d in disp2 if d.get("bind_variant")]) == 6,
    f"实时数据页数值框 {len(disp2)} != 6")
disp3 = [it for it in roots["定时采集页"].iter("item")
         if it.get("type") == "text_display" and it.get("input_mode") == "0"]
chk(len([d for d in disp3 if d.get("bind_variant")]) == 6,
    f"定时采集页数值框 {len(disp3)} != 6")
inp = [it for it in roots["定时采集页"].iter("item")
       if it.get("type") == "text_display" and it.get("input_mode") == "1"]
chk(len(inp) == 24, f"输入框 {len(inp)} != 24")
for it in inp:
    chk(it.get("value_limit") == "1", f"{it.get('name')} 未开限幅")
    chk(it.get("max_value") in ("23", "59"), f"{it.get('name')} 限幅值错")
    chk(int(it.get("min_value", -1)) == 0, f"{it.get('name')} min!=0")

btns = {it.get("name"): it for it in roots["定时采集页"].iter("item")
        if it.get("type") == "button"}
chk("BtnCollect" in btns and btns["BtnCollect"].get("button_style") == "3"
    and btns["BtnCollect"].get("bind_variant") == "采集使能", "采集按钮(置位写1)")
chk("BtnStop" in btns and btns["BtnStop"].get("button_style") == "4"
    and btns["BtnStop"].get("bind_variant") == "采集使能", "停止按钮(复位写0)")
nav = [(pg, it) for pg, rt in roots.items()
       for it in rt.iter("item") if it.get("type") == "button"
       and it.get("switch")]
swset = {it.get("switch") for _, it in nav}
chk(swset == {"实时数据页", "标题页", "定时采集页"}, f"导航切换目标: {swset}")

# ---- 5. 字体存在 ----
fids = {f.get("id") for f in fonts.iter("font")}
for pn, rt in roots.items():
    for it in rt.iter("item"):
        f = it.get("font")
        if f:
            chk(f in fids, f"{pn}/{it.get('name')} 字体 {f} 不在 fonts.xml")

# ---- 6. 窗口判定语义 (镜像固件 Sched_WindowHit) ----
def window_hit(sh, sm, eh, em, nh, nm):
    s, e, n = sh * 60 + sm, eh * 60 + em, nh * 60 + nm
    if s == e:
        return 0
    if s < e:
        return 1 if (n >= s and n < e) else 0
    return 1 if (n >= s or n < e) else 0

cases = [  # (sh,sm,eh,em,nh,nm) -> 期望
    ((8, 0, 18, 0, 8, 0), 1),      # 边界: 开始时刻含
    ((8, 0, 18, 0, 17, 59), 1),
    ((8, 0, 18, 0, 18, 0), 0),     # 停止时刻不含
    ((8, 0, 18, 0, 7, 59), 0),
    ((22, 0, 6, 0, 23, 30), 1),    # 跨午夜: 22:00~06:00
    ((22, 0, 6, 0, 5, 59), 1),
    ((22, 0, 6, 0, 6, 0), 0),
    ((22, 0, 6, 0, 21, 59), 0),
    ((8, 0, 8, 0, 8, 30), 0),      # 零窗口无效
]
for a, want in cases:
    got = window_hit(*a)
    chk(got == want, f"窗口 {a} 期望 {want} 实得 {got}")

print(("FAIL %d 项:\n" % len(fails) + "\n".join(fails)) if fails else "PASS 全部通过")
sys.exit(1 if fails else 0)
```

- [x] **Step 6.2: 运行校验**

```bash
python -I "D:/流量压力检测集成装置/tools/validate_flowpress_screen.py"
```
Expected: `PASS 全部通过`，退出码 0。任何 FAIL 逐条修生成器后重跑（改 Task 5 的 .py 再执行 Step 5.2——生成器有防覆盖保护，需先删 `D:/VisualTFT_Project/FlowPress`）。

- [x] **Step 6.3: 提交两仓库**

```bash
cd "D:/流量压力检测集成装置" && git add tools/ && git commit -m "feat(tools): FlowPress 屏工程生成器+校验器"
git -C "D:/VisualTFT_Project" add FlowPress && git -C "D:/VisualTFT_Project" commit -m "feat: FlowPress 屏工程(生成器产出, 三页+Modbus变量表+RTC喂时脚本)"
```

---

### Task 7: 用户 GUI 验证（需用户操作，脚本无法替代）

- [ ] **Step 7.1: VisualTFT 打开工程** `D:/VisualTFT_Project/FlowPress/FlowPress.tftprj`。确认右下/工程属性显示设备 DC80480DN070、波特率 9600、启动画面 标题页。
- [ ] **Step 7.2: 菜单 工具→协议与变量设置**：确认左侧变量树正常列出 43 个变量、6 个浮点变量"格式"显示为**单精度浮点数**。点一次"保存"（让工具归一化 length/byte_count 等属性——R7 关闭动作）。若有属性报错弹窗，截图/抄录报错文本反馈给实现者。
- [ ] **Step 7.3: 三页编辑视图**逐一打开，控件无红叉/缺图（缺图说明 Images 拷贝名不符，把 VisualTFT 报的缺失文件名反馈回来）。
- [ ] **Step 7.4: 编译 → 下载到屏**（USB 或 SD，用 VisualTFT 下载器）。**下载后逐页肉眼确认字形**：m³/h 的上标 ³、定时采集页新增汉字（采/集/始/止/时/分等）无空白/方块——字库是水肥工程拷来的编译产物，不保证含这些字形；缺失则在 VisualTFT 里重新导出字体再下载。

---

### Task 8: 上板联调（对应规格 §6"联调验证"）

- [ ] **Step 8.1: 烧录固件** STC-ISP（`stc-isp6.96z/STC-ISP-v6.96Z.exe`，芯片 STC32G144K246，**IRC=24MHz**，hex=`modbus6_rtos/Objects/modbus6.hex`）。
- [ ] **Step 8.2: float 显示确认**（R1 关闭动作）：串口助手伪装从站或临时在 `Scr_GetReg` 固定返回 12.345f（`return Scr_FloatWord(12.345f, ...)`），进实时数据页应显示 **12.345**。异常→按 byte_order 0→1→2→3 换枚举重试并记录。
- [ ] **Step 8.3: 轮询帧抓包**（USB 转 485 接 UART8 总线）：进数据页应见 03H 帧 `01 03 00 00 00 0C 24 07`（CRC 以实际为准）类帧，200ms 周期；标题页时无数据轮询帧。**第 3 页多停留观察**：若出现从机回 `01 83 03` 异常（屏引擎把 0x18~0x37 跨空洞合批超过固件 29 上限所致），改屏侧 mulitple_read=16 或拆变量地址段。
- [ ] **Step 8.4: 输入链路**：屏上改某路"开始时"→ 抓到 `06H 01 06 00 2X xx 00 CRC`（xx=0x20~0x37 内偏移）。
- [ ] **Step 8.5: 按钮写值**（R6 必测）：按"采集"→ `01 06 00 18 00 01 <CRC>`；按"停止"→ `01 06 00 18 00 00 <CRC>`。若屏**不发**帧（R6 兑现），改用按钮外发原始帧方案：external_data_down 填 `01 06 00 18 00 01 C8 0D ;`（采集）/ `01 06 00 18 00 00 09 CD ;`（停止）——CRC 已按标准算法核算并双人复核，**勿抄 fattyWater 工程里的错误样例（48 07/89 07 是错的）**。
- [ ] **Step 8.6: 定时闭环**：屏 RTC 喂时后（约 1 分钟内 0x0040 有值），设一路窗口=当前时刻±1 分钟→"采集"→ 到点 UART5/6 抓到该通道请求帧；过停止时刻帧消失。
- [ ] **Step 8.7: 全部通过后**更新规格 §6 勾选状态、CLAUDE.md（当前状态段 + "0x0000~0x0019" 寄存器范围行改为 0x0000~0x0046——Task 1 质量审查第 2 条），提交。

---

## 计划自审记录（writing-plans Self-Review）

1. **规格覆盖**：§3.1 文件树→Task 5/7；§3.2 三页坐标→Task 5 生成器；§3.3 script.xml/Lua→Task 5；§4 寄存器→Task 1；§5 固件清单→Task 1/2/3（main.c 确认零改动）；§6 构建验证→Task 4/7.4、联调→Task 8；§7 R1/R7→Step 7.2/8.2，R6→Step 8.5，R5 已在窗口语义(含跨午夜)实现并测试。无遗漏。
2. **占位符扫描**：所有代码块完整；唯一提醒已内联在 Task 5.1 注释（build_script 死代码清理），实现者照注释做即可。
3. **类型一致性**：`g_sched[6][4]`/`g_time[7]`/`g_run[6]`/`g_armed`/`g_time_valid` 在 config_app.h(extern)/sensors.c(定义)/screen.c(使用) 三处一致；`CMD_SCHED_ARM/DISARM`、`REG_SCHED_ARM/BASE`、`REG_TIME_BASE`、`REG_COLLECT_MASK` 命名一致；variant 名（Lua `set_variant` 与 script.xml、校验器三方）一致。
