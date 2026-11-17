/*=============================================================
 * 大彩屏 Modbus 从站模块 (screen.c)
 *-------------------------------------------------------------
 * 本机作为 Modbus-RTU 从站(地址=MB_ADDR_SCREEN=1), UART8
 * (9600 8N1, DE=P10.7)与大彩屏通信, 寄存器映射表见 config_app.h
 * 职责: 03H 读   -> 屏取 6 通道实时值/累计流量/在线状态
 *       06H/10H 写 -> 采集使能/6路时刻表/屏RTC, 经 xCmdQueue 或直接入表
 * 收帧方式: TaskScreen 每 1ms 轮询 UART8 RI, 以 5 tick 帧间隔
 *           超时判定整帧结束(屏侧请求帧间隔远大于此阈值)
 *=============================================================*/
#include "Config.h"
#include "task.h"
#include "semphr.h"
#include "queue.h"

/* RTOS 三件套 (定义在 main.c) */
extern SemaphoreHandle_t xDataMutex;   /* 保护 g_sen, 与传感器任务互斥 */
extern QueueHandle_t     xCmdQueue;    /* 采集命令队列 -> TaskSensor */

static u8 xdata s_rx[MB_RXBUF_SIZE];   /* 屏发来的请求帧 */
static u8 xdata s_tx[MB_RXBUF_SIZE];   /* 本机回复帧 */
static u8  s_len;                      /* 接收字节数 */
static u8  s_busy;                     /* 1=正在接收帧 */
static TickType_t s_t0;                /* 最后接收时刻, 用于帧间隔判断 */

/* 将 float 拆为 2 个 u16 寄存器, 大端字序(高字在前)
 * 与大彩屏 FLOAT 控件一致: 控件会自动按大端还原 float
 * high=1 返回高16位, high=0 返回低16位 */
static u16 Scr_FloatWord(float f, u8 high)
{
    union { float fv; u8 b[4]; } u;     /* float 与 4 字节共用内存 */
    u.fv = f;
    if (high) return ((u16)u.b[0] << 8) | u.b[1];  /* 高16位 */
    return ((u16)u.b[2] << 8) | u.b[3];             /* 低16位 */
}

/* 读取寄存器: 根据地址 a 返回对应的值(u16)
 * 寄存器映射表见 config_app.h 注释 */
static u16 Scr_GetReg(u16 a)
{
    u8 st, idx, k;

    /* 0x0000~0x000B: 瞬时值 (float, 2 regs/通道) [0~2]=流量 [3~5]=压力 */
    if (a <= 0x000B) {
        idx = (u8)(a >> 1);
        if (idx < 3) return Scr_FloatWord(g_sen[idx].flow_inst, (a & 1) ? 0 : 1);
        else          return Scr_FloatWord(g_sen[idx].press,      (a & 1) ? 0 : 1);
    }
    /* 0x000C: 在线位图 bit0~5 = flow1~3, press1~3 */
    if (a == 0x000C) {
        st = 0;
        if (g_sen[0].online) st |= 0x01;
        if (g_sen[1].online) st |= 0x02;
        if (g_sen[2].online) st |= 0x04;
        if (g_sen[3].online) st |= 0x08;
        if (g_sen[4].online) st |= 0x10;
        if (g_sen[5].online) st |= 0x20;
        return st;
    }
    /* 0x0010~0x0015: 累计流量 (float, 2 regs/通道) [0~2]=流量计1~3 */
    if (a >= 0x0010 && a <= 0x0015) {
        idx = (u8)((a - 0x0010) >> 1);
        return Scr_FloatWord(g_sen[idx].flow_total, (a & 1) ? 0 : 1);
    }
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
}

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

/* 发送应答帧到屏: 切 485 到发送, 逐字节发送, 再切回接收 */
static void Scr_Send(u8 xdata *f, u8 len)
{
    u8 i;

    DE_SCREEN_TX();                          /* 485 切发送方向 */
    for (i = 0; i < len; i++) {
        S8CON &= ~SX_TI;                     /* 清发送标志 */
        S8BUF = f[i];                        /* 写发送寄存器 */
        while (!(S8CON & SX_TI));            /* 等待发送完成 */
    }
    vTaskDelay(2);                           /* 等最后一字节移位完 */
    DE_SCREEN_RX();                          /* 切回接收方向 */
}

/* 发送 Modbus 异常应答
 * fc:  原功能码
 * exc: 异常码 (01=非法功能 02=非法地址 03=非法数据) */
static void Scr_Exc(u8 fc, u8 exc)
{
    u16 crc;

    s_tx[0] = MB_ADDR_SCREEN;         /* 本机地址 */
    s_tx[1] = (u8)(fc | 0x80);        /* 功能码最高位置1 表示异常 */
    s_tx[2] = exc;                    /* 异常码 */
    crc = mb_crc16(s_tx, 3);
    s_tx[3] = (u8)crc;
    s_tx[4] = (u8)(crc >> 8);
    Scr_Send(s_tx, 5);
}

/* 处理屏发来的 Modbus 请求帧
 * 支持 03H(读)、06H(写单)、10H(写多) 三种功能码 */
static void Scr_Process(void)
{
    u16 crc, a, q, v;
    u8 i, n, fc;

    if (s_len < 4) return;   /* 帧太短, 丢弃 */
    /* 校验 CRC: 帧末尾2字节是CRC */
    crc = ((u16)s_rx[s_len - 1] << 8) | s_rx[s_len - 2];
    if (crc != mb_crc16(s_rx, (u8)(s_len - 2))) return;  /* CRC 错, 丢弃 */
    if (s_rx[0] != MB_ADDR_SCREEN) return;               /* 地址不匹配, 丢弃 */

    fc = s_rx[1];   /* 功能码 */

    /* 功能码 03H: 读保持寄存器, 返回 q 个寄存器的值 */
    if (fc == MB_FUNC_READ_HOLD) {
        if (s_len != 8) return;                     /* 请求帧固定 8 字节 */
        a = ((u16)s_rx[2] << 8) | s_rx[3];          /* 起始地址 */
        q = ((u16)s_rx[4] << 8) | s_rx[5];          /* 读取数量 */
        if (q < 1 || q > 29) { Scr_Exc(fc, 0x03); return; }  /* 29=s_tx 64B 上限(3+2q+2<=64), 需容下时刻表整块 q=24 */
        s_tx[0] = MB_ADDR_SCREEN;
        s_tx[1] = fc;
        s_tx[2] = (u8)(q << 1);                     /* 字节数 = 寄存器数*2 */
        /* 加锁读取 g_sen, 保证 float 两字一致 */
        xSemaphoreTake(xDataMutex, portMAX_DELAY);
        for (i = 0; i < (u8)q; i++) {
            v = Scr_GetReg((u16)(a + i));
            s_tx[3 + (u8)(i << 1)] = (u8)(v >> 8);  /* 高字节 */
            s_tx[4 + (u8)(i << 1)] = (u8)v;         /* 低字节 */
        }
        xSemaphoreGive(xDataMutex);
        /* 附加 CRC */
        n = (u8)(3 + (u8)(q << 1));                 /* 应答帧头+数据长度 */
        crc = mb_crc16(s_tx, n);
        s_tx[n++] = (u8)crc;
        s_tx[n++] = (u8)(crc >> 8);
        Scr_Send(s_tx, n);
    }
    /* 功能码 06H: 写单个寄存器 (屏发采集启停命令) */
    else if (fc == MB_FUNC_WRITE_ONE) {
        if (s_len != 8) return;
        a = ((u16)s_rx[2] << 8) | s_rx[3];          /* 寄存器地址 */
        v = ((u16)s_rx[4] << 8) | s_rx[5];          /* 写入值 */
        {                       /* 0=非法地址 2=非法数据值 */
            u8 r = Scr_SetReg(a, v);
            if (r == 0) { Scr_Exc(fc, 0x02); return; }
            if (r == 2) { Scr_Exc(fc, 0x03); return; }
        }
        Scr_Send(s_rx, 8);                           /* 正常: 原样回显请求帧 */
    }
    /* 功能码 10H: 写多个寄存器 */
    else if (fc == MB_FUNC_WRITE_MULTI) {
        if (s_len < 11) return;
        a = ((u16)s_rx[2] << 8) | s_rx[3];          /* 起始地址 */
        q = ((u16)s_rx[4] << 8) | s_rx[5];          /* 寄存器数量 */
        if (q < 1 || q > 26 || s_rx[6] != (u8)(q << 1)) { Scr_Exc(fc, 0x03); return; }  /* 26=s_rx 64B 上限(11+2q<=64) */
        if (s_len != (u8)(9 + s_rx[6])) return;     /* 长度校验 */
        for (i = 0; i < (u8)q; i++) {
            v = ((u16)s_rx[7 + (u8)(i << 1)] << 8) | s_rx[8 + (u8)(i << 1)];
            {   u8 r = Scr_SetReg((u16)(a + i), v);
                if (r == 0) { Scr_Exc(fc, 0x02); return; }  /* 注: 多写中途
                非法值会留下半截数据; 屏端键盘已限幅, 此路径仅防异常帧 */
                if (r == 2) { Scr_Exc(fc, 0x03); return; } }
        }
        /* 正常应答: 回显地址+数量 */
        s_tx[0] = MB_ADDR_SCREEN;
        s_tx[1] = fc;
        for (i = 2; i < 6; i++) s_tx[i] = s_rx[i];
        crc = mb_crc16(s_tx, 6);
        s_tx[6] = (u8)crc;
        s_tx[7] = (u8)(crc >> 8);
        Scr_Send(s_tx, 8);
    }
    else {
        Scr_Exc(fc, 0x01);   /* 异常码 01: 不支持的功能码 */
    }
}

/* 大彩屏串口初始化: UART8, 9600 8N1, 本机作 Modbus 从站(地址1) */
void Screen_Init(void)
{
    /* P10.7=DE(大彩屏485) 推挽输出 */
    PAM1 &= ~0x80;  PAM0 |= 0x80;
    /* P11.0=RX8 输入, P11.1=TX8 推挽 */
    PBM1 |= 0x01;   PBM0 &= ~0x01;
    PBM1 &= ~0x02;  PBM0 |= 0x02;

    DE_SCREEN_RX();   /* 默认接收模式, 等屏发请求 */

    /* UART8: Timer8 作波特率发生器, 9600 8N1 */
    T7T8M |= 0x20;                    /* T8x12=1T 模式 */
    T8L = (u8)BAUD_REL;
    T8H = (u8)(BAUD_REL >> 8);
    T7T8M |= 0x80;                    /* T8R=启动 */

    S8CON = SX_ST3 | SX_REN;          /* 模式3 + 允许接收 */

    s_len = 0;
    s_busy = 0;
}

/* 屏服务任务: 轮询 UART8 接收, 帧间隔超过 5 tick 则处理整帧 */
void Screen_Service(void)
{
    u8 d;

    /* 搬空接收缓冲区 */
    while (S8CON & SX_RI) {
        d = S8BUF;
        S8CON &= ~SX_RI;              /* 必须先读BUF再清RI */
        if (s_len < MB_RXBUF_SIZE) s_rx[s_len++] = d;
        s_t0 = xTaskGetTickCount();   /* 更新最后接收时刻 */
        s_busy = 1;
    }

    /* 帧间隔超时(5 tick): 认为一帧接收完毕, 处理 */
    if (s_busy && s_len && (TickType_t)(xTaskGetTickCount() - s_t0) >= 5) {
        s_busy = 0;
        Scr_Process();
        s_len = 0;                    /* 清空, 准备下一帧 */
    }
}