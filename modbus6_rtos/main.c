/*=============================================================
 * 工程入口 main.c (流量压力检测集成装置 · 双角色 Modbus-RTU)
 *-------------------------------------------------------------
 * 角色1 主站: TaskSensor 经 UART5/UART6 轮询 6 只传感器
 * 角色2 从站: TaskScreen 在 UART8 应答大彩屏的 Modbus 请求
 * RTOS 三件套:
 *   xSampleSem - Timer3 10ms 中断给出, 驱动传感器轮询节拍
 *   xDataMutex - 保护 g_sen[6] 与 g_collect_* 的互斥锁
 *   xCmdQueue  - 屏任务下发采集启停命令的队列(容量8)
 * 优先级: TaskSensor=3 > TaskScreen=2, 保证传感器字节不丢
 *=============================================================*/
#include "Config.h"
#include "task.h"
#include "semphr.h"
#include "queue.h"

/*================ RTOS 三件套 (全局, 由 config_app.h extern 声明) ================*/
SemaphoreHandle_t xSampleSem;   /* Timer3 10ms 节拍 -> TaskSensor */
SemaphoreHandle_t xDataMutex;   /* 保护 g_sen / g_collect_* */
QueueHandle_t     xCmdQueue;    /* 屏采集命令 -> TaskSensor */

/* 优先级: 传感器(3) > 屏幕(2)
 * 栈大小 256 字 = 512 字节, 传感器任务需较大栈(浮点运算+缓冲区) */
static void TaskSensor(void *pv)
{
    UNUSED( pv );   /* 宏将参数地址写入 volatile 全局指针, 真实使用参数, 消除 C138 */
    while (1) {
        /* 等 Timer3 10ms 节拍信号量, 到点才轮询一只传感器 */
        if (xSemaphoreTake(xSampleSem, portMAX_DELAY) == pdTRUE) {
            Sensor_Poll();
        }
    }
}

/* 屏幕服务任务: 优先级2, 低1ms 节拍轮询屏串口 */
static void TaskScreen(void *pv)
{
    UNUSED( pv );   /* 宏将参数地址写入 volatile 全局指针, 真实使用参数, 消除 C138 */
    while (1) {
        Screen_Service();
        vTaskDelay(1);
    }
}

void main(void)
{
    WTST = 0;               /* 程序指令等待时间=0, CPU全速 */
    P_SW2 |= 0x80;          /* EAXFR 常开(扩展SFR窗口0xFE00段与xdata区不重叠) */
    CKCON = 0;              /* 提高XRAM访问速度 */

    Screen_Init();
    Sensor_Init();

    /* 创建 RTOS 三件套: 信号量+互斥锁+队列 */
    xSampleSem = xSemaphoreCreateBinary();    /* 二值信号量: ISR 给, 任务等 */
    xDataMutex = xSemaphoreCreateMutex();     /* 互斥锁: 保护 g_sen 读写 */
    xCmdQueue  = xQueueCreate(8, sizeof(u8)); /* 队列: 8 个采集命令槽位 */

    Timer3_Init();   /* 10ms 节拍中断, 给出 xSampleSem */

#if ADDR_MODE_ENABLE
    AddrMode_Run();         /* 一次性工具模式, 执行完停机 */
#endif

    /* 创建两个应用任务 */
    xTaskCreate(TaskSensor, "sen", 256, NULL, 3, NULL);  /* 优先级3, 栈256字 */
    xTaskCreate(TaskScreen, "scr", 256, NULL, 2, NULL);  /* 优先级2, 栈256字 */

    vTaskStartScheduler();   /* 启动调度器, 不再返回 */
    while (1);               /* 正常不会到这里 */
}