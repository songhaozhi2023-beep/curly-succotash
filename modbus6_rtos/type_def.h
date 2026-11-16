/*---------------------------------------------------------------------*/
//type_def.h
/*---------------------------------------------------------------------*/

#ifndef		__TYPE_DEF_H
#define		__TYPE_DEF_H

/*---------------------------------------------------------------------
 * 类型定义文件 —— 全工程统一的 u8/u16/u32 基础类型
 * 由 Config.h include, 所有应用模块(Modbus/传感器/屏)共用
 * 命名: u8/u16/u32 = 无符号 8/16/32 位, 本工程主要用这三个;
 *       int8/s8 等别名保留兼容, 新代码勿用, 保持风格统一
 *--------------------------------------------------------------*/

//========================================================================
//                               ���Ͷ���
//========================================================================

typedef unsigned char   u8;     //  8 bits 
typedef unsigned int    u16;    // 16 bits 
typedef unsigned long   u32;    // 32 bits 

typedef signed char     int8;   //  8 bits 
typedef signed int      int16;  // 16 bits 
typedef signed long     int32;  // 32 bits 

typedef signed char     s8;     //  8 bits 
typedef signed int      s16;    // 16 bits 
typedef signed long     s32;    // 32 bits 

typedef unsigned char   uint8;  //  8 bits 
typedef unsigned int    uint16; // 16 bits 
typedef unsigned long   uint32; // 32 bits 

//typedef unsigned char   uint8_t;  //  8 bits 
//typedef unsigned int    uint16_t; // 16 bits 
//typedef unsigned long   uint32_t; // 32 bits 


//========================================================================

/* 布尔常量(部分官方库函数返回值使用) */
#define	TRUE	1
#define	FALSE	0

//========================================================================


//========================================================================

#define	Priority_0			0	//�ж����ȼ�Ϊ 0 ������ͼ���
#define	Priority_1			1	//�ж����ȼ�Ϊ 1 �����ϵͼ���
#define	Priority_2			2	//�ж����ȼ�Ϊ 2 �����ϸ߼���
#define	Priority_3			3	//�ж����ȼ�Ϊ 3 ������߼���

/* 使能/禁用 开关常量(官方库初始化函数参数用) */
#define ENABLE		1
#define DISABLE		0

/* 操作结果码: SUCCESS=0 表示成功, FAIL=-1 表示失败(官方库返回值) */
#define SUCCESS		0
#define FAIL		-1

//========================================================================

#endif


