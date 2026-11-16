/*=============================================================
 * 传感器轮询模块 (sensors.c)
 *-------------------------------------------------------------
 * 总线分配: UART5=流量计总线(485, DE=P1.6, 从站地址1~3)
 *           UART6=压力计总线(485, DE=P2.3, 从站地址1~3)
 *           UART7=备用总线(485, DE=P6.5)
 * 工作方式: TaskSensor 每 10ms 节拍(xSampleSem)调用 Sensor_Poll
 *           一次, 内部状态机"发请求→等响应"交替推进, 单次只
 *           访问一只传感器, 6 只轮完为一圈
 * 收发方式: 主循环轮询 RI/TI 标志, 不用串口中断(规避 STC32G
 *           串口5~8 的中断号与 ACC 相关问题); 接收必须先读
 *           BUF 再清 RI, 否则会丢字节
 * 数据出口: 解析结果经 xDataMutex 互斥保护写入 g_sen[6],
 *           屏任务读取时同样加锁, 保证 float 两字一致
 *=============================================================*/
#include "Config.h"
#include "task.h"
#include "semphr.h"
#include "queue.h"
#include "portasm.h"

/* RTOS 三件套 (定义在 main.c) */
extern SemaphoreHandle_t xSampleSem;
extern SemaphoreHandle_t xDataMutex;
extern QueueHandle_t     xCmdQueue;

sensor_t xdata g_sen[6];           /* 6路传感器数据: [0~2]=流量计 [3~5]=压力计 */
u8 xdata g_collect_flow  = 0;      /* 流量采集开关: 1=运行中, 由屏写 0x0018 控制 */
u8 xdata g_collect_press = 0;      /* 压力采集开关: 1=运行中, 由屏写 0x0019 控制 */

#define BUS_FLOW   0    /* 流量总线: UART5, 挂3个流量计 */
#define BUS_PRESS  1    /* 压力总线: UART6, 挂3个压力计 */

/*================ 传感器轮询状态机 ================*/
static u8 xdata m_txbuf[8];          /* 发送帧: 地址+功能码+数据+CRC */
static u8 xdata m_rxbuf[MB_RXBUF_SIZE]; /* 接收帧缓冲 */
static u8  m_rxlen;                  /* 已接收字节数 */
static u8  m_expect;                 /* 期望响应长度: 流量9 压力7 */
static u8  m_cur;                    /* 当前传感器编号 0~5 (0~2流量,3~5压力) */
static u8  m_bus;                    /* 当前总线 BUS_FLOW/BUS_PRESS */
static u8  m_state;                  /* 状态机: 0=待发请求 1=等响应 */
static u8  m_retry;                  /* 当前请求剩余重试次数 */
static TickType_t m_t0;              /* 请求发出时刻, 用于超时判断 */

/* 粗延时: 调度器未启动时使用, 防止 vTaskDelay 不可用 */
static void CrudeMs(u8 n)
{
    while (n--) { volatile u16 i = 2500; while (i--); }
}

/* 等待 2ms: 调度器未启动用忙等, 已启动用 RTOS 延时 */
static void SenWait2ms(void)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED) CrudeMs(2);
    else vTaskDelay(2);
}

/* 传感器总线初始化: 配置 3 路 485 端口方向、引脚模式、串口波特率 */
void Sensor_Init(void)
{
    u8 i;

    /* ---- 485 方向控制引脚(DE) 推挽输出 ---- */
    P1M1 &= ~0x40;  P1M0 |= 0x40;    /* P1.6=DE 流量总线 推挽 */
    P2M1 &= ~0x08;  P2M0 |= 0x08;    /* P2.3=DE 压力总线 推挽 */
    P6M1 &= ~0x20;  P6M0 |= 0x20;    /* P6.5=DE 备用   推挽 */

    /* ---- UART5 引脚: P2.0=RX5 P2.1=TX5 ---- */
    P2M1 |= 0x01;   P2M0 &= ~0x01;   /* P2.0=RX5 输入 */
    P2M1 &= ~0x02;  P2M0 |= 0x02;    /* P2.1=TX5 推挽 */
    /* ---- UART6 引脚: P2.4=RX6 P2.5=TX6 ---- */
    P2M1 |= 0x10;   P2M0 &= ~0x10;   /* P2.4=RX6 输入 */
    P2M1 &= ~0x20;  P2M0 |= 0x20;    /* P2.5=TX6 推挽 */
    /* ---- UART7 引脚: P6.6=RX7 P6.7=TX7 ---- */
    P6M1 |= 0x40;   P6M0 &= ~0x40;   /* P6.6=RX7 输入 */
    P6M1 &= ~0x80;  P6M0 |= 0x80;    /* P6.7=TX7 推挽 */

    /* UART5 (流量总线): Timer5 作波特率, 9600 8N1 */
    T5T6M |= TX_1T;
    T5L = (u8)BAUD_REL;  T5H = (u8)(BAUD_REL >> 8);
    T5T6M |= TX_RUN;
    S5CON = SX_ST3 | SX_REN;

    /* UART6 (压力总线): Timer6 作波特率, 9600 8N1 */
    T5T6M |= 0x20;                   /* T6x12=1T */
    T6L = (u8)BAUD_REL;  T6H = (u8)(BAUD_REL >> 8);
    T5T6M |= 0x80;                   /* T6R=启动 */
    S6CON = SX_ST3 | SX_REN;

    /* UART7 (备用总线): Timer7 作波特率, 9600 8N1 */
    T7T8M |= TX_1T;
    T7L = (u8)BAUD_REL;  T7H = (u8)(BAUD_REL >> 8);
    T7T8M |= TX_RUN;
    S7CON = SX_ST3 | SX_REN;

    /* 3 路总线默认切到接收模式 */
    DE_FLOW_RX();
    DE_PRESS_RX();
    DE_SPARE_RX();

    /* 状态机变量显式清零。原因: START251.A51 的 EDATALEN 清零长度
     * 小于链接实际 EDATA 用量(modbus6.map 实测用到 0x12BD), 这些
     * 无初始化器的 EDATA 变量上电/WDT 复位后值不确定; 且 Timer3
     * 先于调度器启动, 上电 10ms 内 TaskSensor 就会先读后写它们。
     * 不清零的后果: m_cur 残留>=6 时 g_sen[m_cur] 越界写,
     * m_state 残留非 0 时首轮从"等响应"脏状态起步。 */
    m_state = 0;                     /* 从"待发请求"起步 */
    m_cur   = 0;                     /* 从 0 号传感器开始轮询 */
    m_bus   = BUS_FLOW;
    m_expect = 0;
    m_rxlen = 0;
    m_retry = 0;
    m_t0    = 0;

    /* 初始所有传感器标记为离线 */
    for (i = 0; i < 6; i++) g_sen[i].online = 0;
}

/* 通过 485 总线发送 Modbus 请求帧, 发送完毕切回接收模式
 * bus: BUS_FLOW(流量) 或 BUS_PRESS(压力)
 * f:   发送帧缓冲区
 * len: 帧长度(字节) */
static void Sen_Send(u8 bus, u8 *f, u8 len)
{
    u8 i;

    if (bus == BUS_FLOW) {
        DE_FLOW_TX();                      /* 485 切到发送方向 */
        for (i = 0; i < len; i++) {
            S5CON &= ~SX_TI;               /* 清发送完成标志 */
            S5BUF = f[i];                  /* 写发送寄存器 */
            while (!(S5CON & SX_TI));      /* 等待发送完成(卡死说明S5CON地址错) */
        }
    } else {
        DE_PRESS_TX();
        for (i = 0; i < len; i++) {
            S6CON &= ~SX_TI;
            S6BUF = f[i];
            while (!(S6CON & SX_TI));
        }
    }
    SenWait2ms();                        /* 等待最后一字节移位完成 + 帧间隔 */
    if (bus == BUS_FLOW) DE_FLOW_RX(); else DE_PRESS_RX();
}

/* 构造并发送当前传感器的 Modbus 读请求帧 */
static void Sen_StartReq(void)
{
    u16 crc;
    u8 addr;

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

    /* 确定总线、从站地址、期望响应长度 */
    if (m_cur < 3) {
        m_bus  = BUS_FLOW;  addr = FLOW_ADDR_BASE + m_cur;      m_expect = 9;  /* 流量响应9字节 */
    } else {
        m_bus  = BUS_PRESS; addr = PRESS_ADDR_BASE + m_cur - 3; m_expect = 7;  /* 压力响应7字节 */
    }

    m_txbuf[0] = addr;                    /* 从站地址 */
    m_txbuf[1] = MB_FUNC_READ_HOLD;       /* 03: 读保持寄存器 */
    if (m_bus == BUS_FLOW) {
        m_txbuf[2] = 0x00;  m_txbuf[3] = 0x30;  /* 起始寄存器: 瞬时流量 0x0030 */
        m_txbuf[4] = 0x00;  m_txbuf[5] = 0x02;  /* 读取数量: 2 个寄存器(瞬时+累计) */
    } else {
        m_txbuf[2] = 0x00;  m_txbuf[3] = 0x00;  /* 起始寄存器: 压力值 0x0000 */
        m_txbuf[4] = 0x00;  m_txbuf[5] = 0x01;  /* 读取数量: 1 个寄存器 */
    }
    crc = mb_crc16(m_txbuf, 6);
    m_txbuf[6] = (u8)crc;
    m_txbuf[7] = (u8)(crc >> 8);

    m_rxlen = 0;                       /* 清空接收计数 */
    m_retry = MB_RETRY;                /* 初始化重试次数 */
    Sen_Send(m_bus, m_txbuf, 8);       /* 发送请求帧 */
    m_t0 = xTaskGetTickCount();        /* 记录发送时刻, 用于超时判断 */
    m_state = 1;                       /* 进入等待响应状态 */
}

/* 解析传感器响应帧, 转换为 float 写入 g_sen(互斥锁保护) */
static void Sen_Parse(void)
{
    u16 raw;
    sensor_t *s = &g_sen[m_cur];

    xSemaphoreTake(xDataMutex, portMAX_DELAY);  /* 加锁, 防止屏任务读到半截数据 */
    /* 流量计响应: 9字节, 第3字节=4(返回2个寄存器) */
    if (m_bus == BUS_FLOW && m_rxlen == 9 && m_rxbuf[2] == 4) {
        raw = ((u16)m_rxbuf[3] << 8) | m_rxbuf[4];   /* 瞬时流量高字节在前 */
        s->flow_inst  = (float)raw * FLOW_INST_K;     /* 乘系数转 m³/h */
        raw = ((u16)m_rxbuf[5] << 8) | m_rxbuf[6];   /* 累计流量高字节在前 */
        s->flow_total = (float)raw * FLOW_TOTAL_K;    /* 乘系数转 m³ */
        s->online = 1;                                 /* 标记在线 */
    }
    /* 压力计响应: 7字节, 第3字节=2(返回1个寄存器) */
    else if (m_bus == BUS_PRESS && m_rxlen == 7 && m_rxbuf[2] == 2) {
        raw = ((u16)m_rxbuf[3] << 8) | m_rxbuf[4];   /* 压力值高字节在前 */
        s->press  = (float)raw * PRESS_K;             /* 乘系数转实际压力值 */
        s->online = 1;
    }
    xSemaphoreGive(xDataMutex);                       /* 解锁 */
}

/* 传感器轮询主循环: 每 10ms 节拍调用一次, 推进状态机 */
void Sensor_Poll(void)
{
    u8 dat, cmd;
    u8 ok = 0;
    u16 crc;

    /* 处理屏发来的采集启停命令 (非阻塞, 队列空就跳过) */
    while (xQueueReceive(xCmdQueue, &cmd, 0) == pdTRUE) {
        switch (cmd) {
        case CMD_FLOW_START:  g_collect_flow  = 1; break;
        case CMD_FLOW_STOP:   g_collect_flow  = 0; break;
        case CMD_PRESS_START: g_collect_press = 1; break;
        case CMD_PRESS_STOP:  g_collect_press = 0; break;
        }
    }

    /* 状态0: 发请求, 然后等下个节拍收响应 */
    if (m_state == 0) { Sen_StartReq(); return; }

    /* 状态1: 从串口接收缓冲区搬数据(轮询RI, 不用中断) */
    if (m_bus == BUS_FLOW) {
        while (S5CON & SX_RI) {
            dat = S5BUF;  S5CON &= ~SX_RI;   /* 读数据+清标志(必须先读BUF再清RI) */
            if (m_rxlen < MB_RXBUF_SIZE) m_rxbuf[m_rxlen++] = dat;
        }
    } else {
        while (S6CON & SX_RI) {
            dat = S6BUF;  S6CON &= ~SX_RI;
            if (m_rxlen < MB_RXBUF_SIZE) m_rxbuf[m_rxlen++] = dat;
        }
    }

    /* 收到完整帧: 校验CRC+地址+功能码 */
    if (m_rxlen >= m_expect) {
        crc = ((u16)m_rxbuf[m_rxlen - 1] << 8) | m_rxbuf[m_rxlen - 2];
        if (crc == mb_crc16(m_rxbuf, (u8)(m_rxlen - 2))   /* CRC 校验 */
            && m_rxbuf[0] == m_txbuf[0]                    /* 地址匹配 */
            && m_rxbuf[1] == MB_FUNC_READ_HOLD) ok = 1;    /* 功能码匹配 */
    }
    /* 校验通过: 解析数据, 切到下一个传感器 */
    if (ok) {
        Sen_Parse();
        m_cur = (u8)((m_cur + 1) % 6);
        m_state = 0;
        return;
    }

    /* 超时处理: 超过 MB_RESP_TIMEOUT 未收到完整响应 */
    if ((TickType_t)(xTaskGetTickCount() - m_t0) >= MB_RESP_TIMEOUT) {
        if (m_retry) {
            /* 还有重试次数: 重发请求 */
            m_retry--;
            m_rxlen = 0;
            Sen_Send(m_bus, m_txbuf, 8);
            m_t0 = xTaskGetTickCount();
        } else {
            /* 重试耗尽: 标记离线, 切到下一个传感器 */
            xSemaphoreTake(xDataMutex, portMAX_DELAY);
            g_sen[m_cur].online = 0;
            xSemaphoreGive(xDataMutex);
            m_cur = (u8)((m_cur + 1) % 6);
            m_state = 0;
        }
    }
}

/*================ Timer3: 10ms 采样节拍中断 ================*/
/* 初始化 Timer3: 12T 模式, 重装值 45536 -> 24MHz 下产生 10ms 周期中断 */
void Timer3_Init(void)
{
    UNUSED(pxCurrentTCB);   /* 触发编译器生成 EXTRN pxCurrentTCB, 供内联汇编使用 */
    T3_CT  = 0;        /* 0=定时器模式(非计数器) */
    T3x12  = 0;        /* 0=12T 模式(每个机器周期12个时钟) */
    T3L    = (u8)T3_RELOAD_10MS;       /* 重装值低 8 位 */
    T3H    = (u8)(T3_RELOAD_10MS >> 8); /* 重装值高 8 位 */
    T3IF   = 0;        /* 清中断标志, 防止一上来就进中断 */
    ET3    = 1;        /* 使能 Timer3 中断 */
    T3R    = 1;        /* 启动 Timer3 计数 */
}

/* Timer3 中断服务: 给采样信号量, 唤醒 TaskSensor 轮询传感器 */
void Timer3_ISR(void) interrupt TMR3_VECTOR
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;  /* 记录是否需要任务切换 */

    T3IF = 0;   /* 清 Timer3 溢出标志 (AUXINTIF^1) */
    /* 从ISR给信号量, 通知 TaskSensor 该轮询了 */
    xSemaphoreGiveFromISR(xSampleSem, &xHigherPriorityTaskWoken);
    /* 如果有更高优先级任务被唤醒, 直接切换 */
    if (xHigherPriorityTaskWoken != pdFALSE) {
        portEND_SWITCHING_ISR();   /* STC 移植版无参数, 直接切换任务 */
    }
}

#if ADDR_MODE_ENABLE
/* 改地址模式: 一次性工具, 将出厂地址为1的传感器改为 ADDR_TARGET
 * 使用前把单个传感器接到对应总线, 改完记得把 ADDR_MODE_ENABLE 改回 0 */
void AddrMode_Run(void)
{
    u8 f[8];         /* 写地址请求帧 */
    u8 rbuf[8];      /* 响应帧缓冲 */
    u8 n, i, d, bus, tries, ok;
    u32 guard;
    u16 crc;

    /* 选择总线类型 */
    bus = (ADDR_SENSOR_TYPE == 0) ? BUS_FLOW : BUS_PRESS;

    /* 构造 06H 写单寄存器帧: 写 0x0100 寄存器为目标地址 */
    f[0] = 1;                        /* 默认从站地址 1 */
    f[1] = MB_FUNC_WRITE_ONE;        /* 06H 写单寄存器 */
    f[2] = (u8)(REG_DEV_ADDR >> 8);  /* 寄存器地址高字节 0x01 */
    f[3] = (u8)REG_DEV_ADDR;         /* 寄存器地址低字节 0x00 */
    f[4] = 0x00;                     /* 写入值高字节 */
    f[5] = ADDR_TARGET;              /* 写入值低字节 = 目标地址 */
    crc = mb_crc16(f, 6);
    f[6] = (u8)crc;
    f[7] = (u8)(crc >> 8);

    /* 最多重试 3 次 */
    ok = 0;
    for (tries = 0; tries < 3 && !ok; tries++) {
        n = 0;
        Sen_Send(bus, f, 8);
        guard = 0;
        /* 等待响应, 最多 200 万次循环 */
        while (n < 8 && ++guard < 2000000UL) {
            if (bus == BUS_FLOW) {
                while (S5CON & SX_RI) { d = S5BUF; S5CON &= ~SX_RI; rbuf[n++] = d; }
            } else {
                while (S6CON & SX_RI) { d = S6BUF; S6CON &= ~SX_RI; rbuf[n++] = d; }
            }
        }
        /* 校验响应: 写单寄存器会原样回显请求帧 */
        ok = (n == 8);
        for (i = 0; ok && i < 8; i++) { if (rbuf[i] != f[i]) ok = 0; }
    }
    while (1);   /* 停机, 防止进入正常轮询 */
}
#endif