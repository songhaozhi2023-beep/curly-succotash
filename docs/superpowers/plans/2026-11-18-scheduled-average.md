# 定时采集平均值 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 定时采集各路在开始~停止窗口内显示动态平均值，出窗冻结（规格：`docs/superpowers/specs/2026-11-18-scheduled-average-design.md`）。

**Architecture:** 固件在采样点递推结算平均（`g_avg[6]`，xdata），经新读寄存器 0x0050~0x005B（float 大端）供屏读；屏端第 3 页数值框改绑 6 个新平均变体，列头"实时值"→"平均值"。窗口调度/按钮/RTC 全部不动。

**Tech Stack:** Keil C251（STC32G144K246，FreeRTOS 线 B）+ Python 屏工程生成器/校验器（仓库外工程 `D:/VisualTFT_Project/FlowPress/`）。

**平台铁律（执行前必读，摘自 CLAUDE.md）：**
- 必须用 `D:/keil5C251`（C251 版）编译，ARM 版 `D:\Keil` 不行
- 新增变量全部显式 xdata（EDATA 清零范围坑），`Sensor_Init` 显式清零双保险
- UV4 退出码 0/1 均算成功，以日志 `0 Error(s)` 行为准

---

### Task 1: 固件——config_app.h 平均值寄存器定义

**Files:**
- Modify: `modbus6_rtos/config_app.h`

- [ ] **Step 1.1: 加寄存器常量**

在 `#define REG_TIME_NUM        7u         /* RTC 寄存器数 */` 之后追加：

```c
#define REG_AVG_BASE        0x0050   /* 12 寄存器: 6 路采集窗口平均值, 各 float 大端占 2 */
#define REG_AVG_NUM         (6u * 2u)  /* 平均值寄存器数 12 */
```

- [ ] **Step 1.2: 加 extern 声明**

在 `extern u8  xdata g_run[6];              /* 调度评估结果: 1=该路在采集窗口内 */` 之后追加：

```c
extern float xdata g_avg[6];            /* 各路采集窗口平均值(窗口内动态递推, 出窗冻结) */
```

- [ ] **Step 1.3: 更新映射表注释**

在映射注释 `*  0x001A    采集状态位图 bit0~5         U16` 行之后追加一行：

```c
 *  0x0050~5B 六路采集窗口平均值         FLOAT  (窗口内动态平均, 出窗冻结)
```

### Task 2: 固件——sensors.c 递推平均结算

**Files:**
- Modify: `modbus6_rtos/sensors.c`

- [ ] **Step 2.1: 定义状态变量**

在 `u8  xdata g_run[6];                /* 调度结果: 1=该路在窗口内, Sensor_Poll 只轮询这些 */` 之后追加：

```c
float xdata g_avg[6];              /* 各路窗口平均值: 窗口内动态递推, 出窗冻结(屏读 0x0050~5B) */
static u32 xdata m_smp[6];         /* 本窗口已计入样本数 (递推平均分母) */
static u8  xdata m_prev_run[6];    /* 上个 tick 的 g_run 快照: 0→1 上升沿=开窗, 清零重算 */
```

- [ ] **Step 2.2: Sensor_Init 显式清零（双保险惯例）**

在 `g_time_valid = 0;`（`Sensor_Init` 末段）之后追加：

```c
    for (i = 0; i < 6; i++) { g_avg[i] = 0; m_smp[i] = 0; m_prev_run[i] = 0; }
```

- [ ] **Step 2.3: Sensor_Poll 加循环变量**

将 `Sensor_Poll` 开头的 `u8 dat, cmd;` 改为：

```c
    u8 dat, cmd, i;
```

- [ ] **Step 2.4: 窗口沿检测（上升沿清零）**

在 `Sensor_Poll` 中这段之后：

```c
    /* 每 10ms 依最新时刻表/RTC 重算各路采集窗口 */
    xSemaphoreTake(xDataMutex, portMAX_DELAY);
    Sched_UpdateRuns();
    xSemaphoreGive(xDataMutex);
```

追加：

```c
    /* 平均值窗口沿检测: 上升沿(进窗)清零重算。g_avg/m_smp/m_prev_run 与
     * g_run 同在 TaskSensor 上下文(唯一写者=读者), 免锁, 沿用 g_run 惯例;
     * 下降沿(出窗)无需动作——递推值就地冻结; 零样本窗口因开窗清零显示 0 */
    for (i = 0; i < 6; i++) {
        if (g_run[i] && !m_prev_run[i]) { g_avg[i] = 0; m_smp[i] = 0; }
        m_prev_run[i] = g_run[i];
    }
```

- [ ] **Step 2.5: Sen_Parse 计入动态平均**

5a. 函数头局部变量，将：

```c
    u16 raw;
    sensor_t *s = &g_sen[m_cur];
```

改为：

```c
    u16 raw;
    u8   fresh = 0;                    /* 本帧解析出有效测量值 */
    float v = 0;                       /* fresh=1 时为本帧测量值 */
    sensor_t *s = &g_sen[m_cur];
```

5b. 流量分支，在该分支内 `s->online = 1;                                 /* 标记在线 */` 一行之后追加：

```c
        v = s->flow_inst;                              /* 平均只计瞬时量 */
        fresh = 1;
```

5c. 压力分支，在 `s->online = 1;` 之后追加：

```c
        v = s->press;
        fresh = 1;
```

5d. 解锁前递推，将 `xSemaphoreGive(xDataMutex);                       /* 解锁 */` 改为：

```c
    /* 窗口内合格样本计入动态平均: g_run 本任务 10ms 刷新(写者=读者)免锁;
     * g_avg 由屏任务经本函数已持的 xDataMutex 读, float 两字一致 */
    if (fresh && g_run[m_cur]) {
        m_smp[m_cur]++;
        g_avg[m_cur] += (v - g_avg[m_cur]) / (float)m_smp[m_cur];
    }
    xSemaphoreGive(xDataMutex);                       /* 解锁 */
```

- [ ] **Step 2.6: 构建验证**

```bash
"D:/keil5C251/UV4/UV4.exe" -j0 -r "D:/流量压力检测集成装置/modbus6_rtos/FreeRTOS_STC32G12K.uvproj" -o "D:/流量压力检测集成装置/modbus6_rtos/Listings/build_avg_t2.log"
grep -E "Error|Warning" "D:/流量压力检测集成装置/modbus6_rtos/Listings/build_avg_t2.log"
```

期望（退出码 0 或 1 都算 UV4 正常，以日志为准）：`0 Error(s), 0 Warning(s)`。

- [ ] **Step 2.7: 提交**

```bash
git add modbus6_rtos/config_app.h modbus6_rtos/sensors.c
git commit -m "feat(sensor): 采集窗口动态平均值递推(g_avg/m_smp/上升沿清零)"
```

### Task 3: 固件——screen.c 读寄存器分支

**Files:**
- Modify: `modbus6_rtos/screen.c`

- [ ] **Step 3.1: Scr_GetReg 加 0x0050~0x005B 分支**

在 `0x0010~0x0015 累计流量` 分支（`if (a >= 0x0010 && a <= 0x0015) { ... }` 的收尾 `}` ）之后、`/* 0x0018: 定时采集总开关读回 */` 之前插入：

```c
    /* 0x0050~0x005B: 六路采集窗口平均值 (float 大端, 2 regs/通道)
     * 窗口内=动态平均, 出窗冻结, 零样本窗口=0; 调用方 03H 已持 xDataMutex */
    if (a >= REG_AVG_BASE && a < (REG_AVG_BASE + REG_AVG_NUM)) {
        idx = (u8)((a - REG_AVG_BASE) >> 1);
        return Scr_FloatWord(g_avg[idx], (a & 1) ? 0 : 1);
    }
```

- [ ] **Step 3.2: 构建验证 + map 复查**

```bash
"D:/keil5C251/UV4/UV4.exe" -j0 -r "D:/流量压力检测集成装置/modbus6_rtos/FreeRTOS_STC32G12K.uvproj" -o "D:/流量压力检测集成装置/modbus6_rtos/Listings/build_avg_t3.log"
grep -E "Error|Warning" "D:/流量压力检测集成装置/modbus6_rtos/Listings/build_avg_t3.log"
grep -n "EDATA" "D:/流量压力检测集成装置/modbus6_rtos/Listings/modbus6.map" | tail -5
```

期望：`0 Error(s), 0 Warning(s)`；EDATA 用量仍在 0x12BD 附近（新变量全 xdata，未动 EDATA；XDATA 增约 54B）。

- [ ] **Step 3.3: 提交**

```bash
git add modbus6_rtos/screen.c
git commit -m "feat(screen): 读寄存器0x0050~0x005B六路窗口平均值"
```

### Task 4: 校验器先红（TDD，本任务不提交）

**Files:**
- Modify: `tools/validate_flowpress_screen.py`

- [ ] **Step 4.1: expect 表加 6 个平均变体**

将（§3 变量表断言处）：

```python
          "采集使能": 24, "采集状态": 26,
```

改为：

```python
          "采集使能": 24, "采集状态": 26,
          "流量1平均": 80, "流量2平均": 82, "流量3平均": 84,
          "压力1平均": 86, "压力2平均": 88, "压力3平均": 90,
```

- [ ] **Step 4.2: 变体总数 43→49**

将 `chk(len(vs) == 43, f"variant 数 {len(vs)} != 43")` 改为：

```python
chk(len(vs) == 49, f"variant 数 {len(vs)} != 49")
```

- [ ] **Step 4.3: 浮点 format 断言补平均名（防御式，防红跑阶段 KeyError）**

将浮点 format 循环改为（原循环对缺失名字会 `names[n]` KeyError 崩溃，必须用 `names.get` 守护——红跑阶段平均变体还不存在，`chk()` 拦不住表达式求值）：

```python
for n in ("流量1瞬时", "流量2瞬时", "流量3瞬时", "压力1", "压力2", "压力3",
          "流量1累计", "流量2累计", "流量3累计",
          "流量1平均", "流量2平均", "流量3平均",
          "压力1平均", "压力2平均", "压力3平均"):
    v = names.get(n)
    chk(v is not None and v.get("format") == "2", f"{n} 缺失或 format != 2 (浮点)")
```

- [ ] **Step 4.4: 新增页面绑定断言（本次差异点入断言）**

在第 3 页 `disp3` 数量断言（`chk(len([d for d in disp3 if d.get("bind_variant")]) == 6, ...)`）之后插入：

```python
inst_v = {"流量1瞬时", "流量2瞬时", "流量3瞬时", "压力1", "压力2", "压力3"}
avg_v = {"流量1平均", "流量2平均", "流量3平均", "压力1平均", "压力2平均", "压力3平均"}
b2 = {d.get("bind_variant") for d in disp2}
chk(b2 == inst_v, f"实时数据页数值框应绑瞬时变体, 实为 {b2}")
b3 = {d.get("bind_variant") for d in disp3}
chk(b3 == avg_v, f"定时采集页数值框应绑平均变体, 实为 {b3}")
```

- [ ] **Step 4.5: 跑校验器，确认按预期变红**

```bash
python -I tools/validate_flowpress_screen.py; echo "exit=$?"
```

期望 FAIL（exit=1），失败项至少包含：`variant 数 43 != 49`、`变量 流量1平均 地址应为 80, 实为 None`、`流量1平均 缺失或 format != 2 (浮点)`、`定时采集页数值框应绑平均变体`；不得出现 Python KeyError 崩溃（出现即 Step 4.3 没改成防御式，回查）。

### Task 5: 生成器改绿——改绑定+新变体+重新生成屏工程

**Files:**
- Modify: `tools/gen_flowpress_screen.py`
- 重新生成（仓库外）: `D:/VisualTFT_Project/FlowPress/`（**破坏性：先删后生成**，会清掉 GUI 编译产物 output/、dciot_build/ 和用户的 GUI 归一化，规格 §7 已确认此流程）

- [ ] **Step 5.1: 素材预检（必须在删除前做）**

```bash
test -d "D:/VisualTFT_Project/Project/font" && test -f "D:/VisualTFT/VisualTFT/font/truefont/wqyMicroHei.ttf" && echo OK
```

期望输出 `OK`。生成器的 `copytree(FONT_SRC)` 在删除之后执行，素材缺失会留下半截工程，故此项失败则**中止**并报告，不要删目录。

- [ ] **Step 5.2: 列头文字改平均值**

将 `build_sched()` 中 `text_item(7, "HdV", "实时值", 580, 54, 200, 18, F_HDR, GRAY),` 改为：

```python
        text_item(7, "HdV", "平均值", 580, 54, 200, 18, F_HDR, GRAY),
```

- [ ] **Step 5.3: 定义平均变体名**

在 `SCHED_LBL = FLOWN + PRESSN` 之后追加：

```python
AVGV = tuple(f"{ch}平均" for ch in FLOWN + PRESSN)
```

- [ ] **Step 5.4: 第 3 页数值框改绑平均变体**

将 `build_sched()` 中：

```python
        it.append(display_item(k, f"Val{r+1}", "0.000", 580, y, 200, 44, F_VAL,
                               FLOW_C if r < 3 else PRESS_C,
                               FLOW_BG if r < 3 else PRESS_BG,
                               bind=(FLOTV + PRESSV)[r])); k += 1
```

改为：

```python
        it.append(display_item(k, f"Val{r+1}", "0.000", 580, y, 200, 44, F_VAL,
                               FLOW_C if r < 3 else PRESS_C,
                               FLOW_BG if r < 3 else PRESS_BG,
                               bind=AVGV[r])); k += 1
```

- [ ] **Step 5.5: script.xml 加 6 个平均变体**

将 `build_script()` 中：

```python
         variant("采集使能", 24),
         variant("采集状态", 26)]
```

改为：

```python
         variant("采集使能", 24),
         variant("采集状态", 26),
         variant("流量1平均", 80, fmt=2, dec=3, txtfmt=1),
         variant("流量2平均", 82, fmt=2, dec=3, txtfmt=1),
         variant("流量3平均", 84, fmt=2, dec=3, txtfmt=1),
         variant("压力1平均", 86, fmt=2, dec=3, txtfmt=1),
         variant("压力2平均", 88, fmt=2, dec=3, txtfmt=1),
         variant("压力3平均", 90, fmt=2, dec=3, txtfmt=1)]
```

- [ ] **Step 5.6: 删除旧工程并重新生成**

```bash
rm -rf "D:/VisualTFT_Project/FlowPress" && python -I tools/gen_flowpress_screen.py
```

期望输出 `完成: D:/VisualTFT_Project/FlowPress`。

- [ ] **Step 5.7: 校验器转绿**

```bash
python -I tools/validate_flowpress_screen.py; echo "exit=$?"
```

期望 `PASS 全部通过`（GBK 控制台可能显示乱码，以 exit=0 为准）。

- [ ] **Step 5.8: 提交（含 Task 4 的校验器改动）**

```bash
git add tools/validate_flowpress_screen.py tools/gen_flowpress_screen.py
git commit -m "feat(tools): 第3页列头平均值+六路平均变体@0x50+数值框改绑; 校验器绑定断言"
```

### Task 6: CLAUDE.md 寄存器范围更新

**Files:**
- Modify: `CLAUDE.md`

- [ ] **Step 6.1: 更新范围注释**

将 `线 B 的 Modbus 寄存器映射定义在 \`config_app.h\` 注释（0x0000~0x0019）` 中的 `（0x0000~0x0019）` 改为 `（0x0000~0x005B，0x0050~0x005B 为六路采集窗口平均值）`。

- [ ] **Step 6.2: 提交**

```bash
git add CLAUDE.md
git commit -m "docs(claude): 寄存器映射范围更新至0x005B"
```

### Task 7: 终审与收尾

**Files:**
- 无新改动（审查可能产生 fix 提交）

- [ ] **Step 7.1: 全量重建复核**

```bash
"D:/keil5C251/UV4/UV4.exe" -j0 -r "D:/流量压力检测集成装置/modbus6_rtos/FreeRTOS_STC32G12K.uvproj" -o "D:/流量压力检测集成装置/modbus6_rtos/Listings/build_avg_final.log"
grep -E "Error|Warning" "D:/流量压力检测集成装置/modbus6_rtos/Listings/build_avg_final.log"
```

期望 `0 Error(s), 0 Warning(s)`。

- [ ] **Step 7.2: 校验器复跑**

```bash
python -I tools/validate_flowpress_screen.py; echo "exit=$?"
```

期望 exit=0。

- [ ] **Step 7.3: 规格§2 语义逐条对照（人工核查，每条指到代码行）**

| 语义 | 应在 |
|---|---|
| 进窗清零重算 | sensors.c 窗口沿检测 for 循环（上升沿分支） |
| 窗口内动态收敛 | Sen_Parse 递推 `g_avg += (v-g_avg)/n` |
| 出窗冻结（含到达停止时间/跨午夜闭合） | 沿检测无下降沿动作 + 递推仅在 `g_run[m_cur]` 为真时发生 |
| 停止中途=部分平均冻结 | 同上（disarm→g_run 全 0→无清零无递推） |
| 零样本窗口=0.000 | 上升沿清零 g_avg=0 |
| 次日窗口重新出平均 | 上升沿再次清零 |
| 第 2 页不受影响 | 屏端第 2 页绑定未改（校验器 b2 断言） |

另核对：0x0050~0x005B 与 RTC 0x0040~0x0046 无重叠；`REG_AVG_NUM` 边界用 `<` 不是 `<=`（避免读到 0x005C）。

- [ ] **Step 7.4: （执行环境为 ultracode 会话时）workflow 多视角审查本次 diff**

正确性（C251 语义/类型）、并发（锁与免锁边界）、寄存器映射一致性三个视角各一个 agent，confirmed findings 修复后以 `fix(final-review): ...` 提交。

- [ ] **Step 7.5: 提醒用户屏端手动步骤（固件可烧录，屏需 GUI）**

1. VisualTFT 打开 `D:/VisualTFT_Project/FlowPress/FlowPress.tftprj` → 打开脚本编辑器保存一次（GUI 归一化 float 变量 length/byte_count，规格 R7）→ 编译 → 下载到屏
2. 固件 hex：`modbus6_rtos/Objects/modbus6.hex`，STC-ISP 烧录（IRC 必须 24MHz）
3. 联调（规格§8）：设短窗口（当前+2min~+5min）看动态收敛与出窗冻结；串口抓 03H 读 0x0050~0x005B 帧核对
