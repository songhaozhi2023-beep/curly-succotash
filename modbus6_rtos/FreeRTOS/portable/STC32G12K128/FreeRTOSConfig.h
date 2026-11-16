/*
 * FreeRTOSv202212.01
 */

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include "Config.h"

/* STC official port layer V1.0.2 required options (from official FreeRTOSConfig.h) */
#define configUSE_ROMHUGE               0
#if ( configUSE_ROMHUGE == 0 )
    #pragma ROM ( LARGE )
#else
    #pragma ROM ( HUGE )
#endif
#pragma WARNING DISABLE = 47          /* STC 官方写法, C251 实测不支持, 保留无害 */
#pragma WARNING DISABLE = 138
#pragma WARNING DISABLE = 183

/* UNUSED 宏改造(根治 20 条 UNUSED 链 C183):
 * 原实现 if((expr)==0) 的条件读取无副作用, 会被编译器整体优化删除,
 * 所有经它"使用"的变量仍被判死赋值(C183); 且空 if 会把下一条语句
 * 吞为条件体, 属于隐患。新实现把参数地址写入 volatile 全局指针:
 * volatile 写是 C 标准保护的可观察行为, 编译器不得删除, 参数由此
 * 获得不可消除的真实使用。pvUnusedDummy 定义在 port.c(内核必需,
 * 链接可达), 声明与定义类型一致: 指向 const void 的 volatile 指针 */
extern const void * volatile pvUnusedDummy;
#define UNUSED(expr)        do { pvUnusedDummy = &(expr); } while( 0 )




/***************************************************************************************************************/
/*                                        FreeRTOS������������ѡ��                                             */
/***************************************************************************************************************/

#define configUSE_PREEMPTION                    1                       //1ʹ����ռʽ�ںˣ�0ʹ��Э��
#define configUSE_TIME_SLICING                  1                       //1ʹ��ʱ��Ƭ����(Ĭ��ʽʹ�ܵ�)
#define configUSE_PORT_OPTIMISED_TASK_SELECTION	0                       //1�������ⷽ����ѡ����һ��Ҫ���е�����
                                                                        //һ����Ӳ������ǰ����ָ������ʹ�õ�
                                                                        //MCUû����ЩӲ��ָ��Ļ��˺�Ӧ������Ϊ0��
#define configUSE_TICKLESS_IDLE                 0                       //1���õ͹���ticklessģʽ
#define configUSE_QUEUE_SETS                    0                       //Ϊ1ʱ���ö���
#define configCPU_CLOCK_HZ                      (MAIN_Fosc)             //CPUƵ��
#define configTICK_RATE_HZ                      ((TickType_t)1000)      //ʱ�ӽ���Ƶ��
#define configMAX_PRIORITIES                    (16)                    //��ʹ�õ�������ȼ�
#define configDEFAULT_PRIORITIES                (tskIDLE_PRIORITY + 1)  //����Ĭ�����ȼ�
#define configMINIMAL_STACK_SIZE                ((unsigned short)256)   //��������ʹ�õĶ�ջ��С
#define configDEFAULT_STACK_SIZE                ((unsigned short)256)   //����Ĭ�϶�ջ��С
#define configMAX_TASK_NAME_LEN                 (8)                     //���������ַ�������
#define configUSE_IDLE_HOOK						0                       //1ʹ�ÿ��й���,0��ʹ�� /*void vApplicationIdleHook( void );*/
#define configUSE_TICK_HOOK						0                       //1ʹ��ʱ��Ƭ����,0��ʹ�� /*void vApplicationTickHook( void );*/
#define configSUPPORT_DYNAMIC_ALLOCATION        1                       //֧�ֶ�̬�ڴ�����
#define configTOTAL_HEAP_SIZE					((size_t)(4*1024))     	// ϵͳ�����ܵĶѴ�С

#define configUSE_16_BIT_TICKS                  0                       //ϵͳ���ļ����������������ͣ�
                                                                        //1��ʾΪ16λ�޷������Σ�0��ʾΪ32λ�޷�������
#define configIDLE_SHOULD_YIELD                 1                       //Ϊ1ʱ�����������CPUʹ��Ȩ������ͬ���ȼ����û�����
#define configUSE_TASK_NOTIFICATIONS            1                       //Ϊ1ʱ��������֪ͨ���ܣ�Ĭ�Ͽ���
#define configUSE_MUTEXES                       1                       //Ϊ1ʱʹ�û����ź���
#define configQUEUE_REGISTRY_SIZE               0                       //��Ϊ0ʱ��ʾ���ö��м�¼�������ֵ�ǿ���
                                                                        //��¼�Ķ��к��ź��������Ŀ��

#define configUSE_RECURSIVE_MUTEXES             0                       //Ϊ1ʱʹ�õݹ黥���ź���
#define configUSE_MALLOC_FAILED_HOOK            0                       //1ʹ���ڴ�����ʧ�ܹ��Ӻ���
#define configUSE_APPLICATION_TASK_TAG          0
#define configUSE_COUNTING_SEMAPHORES           1                       //Ϊ1ʱʹ�ü����ź���




//����ʱ��ͳ�ƹ���
#define configGENERATE_RUN_TIME_STATS	        0                      	//Ϊ1ʱ��������ʱ��ͳ�ƹ���

#if configGENERATE_RUN_TIME_STATS == 1
	void ConfigureTimeForRunTimeStats(void);
	u32 GetFreeRTOSRunTimeTicks(void);
	#define configUSE_TRACE_FACILITY					1                       //Ϊ1���ÿ��ӻ����ٵ���
	#define configUSE_STATS_FORMATTING_FUNCTIONS		1                       /*  ���configUSE_TRACE_FACILITYͬʱΪ1ʱ���������3������
                                                                                    prvWriteNameToBuffer(),vTaskList(),
                                                                                    vTaskGetRunTimeStats()*/
	#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS() 	ConfigureTimeForRunTimeStats()		//��ʼ��һ��Ӳ����ʱ��
	#define portGET_RUN_TIME_COUNTER_VALUE()			GetFreeRTOSRunTimeTicks()			//��ȡ��������ֵ
#else
	#define configUSE_TRACE_FACILITY					0                       //Ϊ1���ÿ��ӻ����ٵ���
	#define configUSE_STATS_FORMATTING_FUNCTIONS		0                       //���configUSE_TRACE_FACILITYͬʱΪ1ʱ���������3������
#endif



/* Co-routine definitions. */
#define configUSE_CO_ROUTINES 			        	0                       //Ϊ1ʱ����Э�̣�����Э���Ժ���������ļ�croutine.c
#define configMAX_CO_ROUTINE_PRIORITIES         	(2)                     //Э�̵���Ч���ȼ���Ŀ



/* Set the following definitions to 1 to include the API function, or zero
to exclude the API function. */
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_vTaskPrioritySet                1
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskCleanUpResources           1
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_vTaskDelayUntil                 1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_eTaskGetState                   1

#define INCLUDE_xTimerPendFunctionCall          0
#define INCLUDE_uxTaskGetStackHighWaterMark		0			//ջ����ʷʣ����Сֵ		
#define INCLUDE_xTaskGetIdleTaskHandle			0			//��ȡ����������

#define configCHECK_FOR_STACK_OVERFLOW          0           //����0ʱ���ö�ջ�����⹦�ܣ����ʹ�ô˹���,�û������ṩһ��ջ������Ӻ��� vApplicationStackOverflowHook



#endif /* FREERTOS_CONFIG_H */




