/*---------------------------------------------------------------------*/
/* --- STC MCU Limited ------------------------------------------------*/
/* --- STC 1T Series MCU Demo Programme -------------------------------*/
/* --- Mobile: (86)13922805190 ----------------------------------------*/
/* --- Fax: 86-0513-55012956,55012947,55012969 ------------------------*/
/* --- Tel: 86-0513-55012928,55012929,55012966 ------------------------*/
/* --- Web: www.STCMCU.com --------------------------------------------*/
/* --- Web: www.STCMCUDATA.com  ---------------------------------------*/
/* --- QQ:  800003751 -------------------------------------------------*/
/* 如果要在程序中使用此代码,请在程序中注明使用了STC的资料及程序            */
/*---------------------------------------------------------------------*/


/*************  功能说明    **************

本例程基于STC32G为主控芯片的实验箱9进行编写测试.

MCU通过USB CDC(Communication Device Class)协议识别为串口设备;

使用串口助手打开CDC虚拟串口，然后向MCU发送数据，MCU返回接收到的数据到串口助手;

在“usb.h”头文件里面开启“PRINTF_HID”定义，将printf输出直接重定向到USB HID接口;

******************************************/

#include "FreeRTOS.h"
#include "task.h"

#include "usb.h"
#include "usb_cdc.h"

/****************************** 用户定义 ***********************************/

//USB调试及复位所需定义
char *USER_DEVICEDESC = NULL;
char *USER_PRODUCTDESC = NULL;
char *USER_STCISPCMD = "@STCISP#";                      //设置自动复位到ISP区的用户接口命令

/* USB CDC任务函数 */
portTASK_FUNCTION_PROTO( vUsb_CdcTask, pvParameters )
{
    //USB调试及复位所需代码-----
    P3M0 &= ~0x03;
    P3M1 |= 0x03;
    IRC48MCR = 0x80;
    while (!(IRC48MCR & 0x01));
    
    USBCLK = 0x00;
    USBCON = 0x90;
    usb_init();
    //-------------------------

    while(1)
    {
        EUSB = 1;
        if(DeviceState != DEVSTATE_CONFIGURED)  //等待USB完成配置
            continue;

        if (bUsbOutReady)
        {
            usb_OUT_done();    //接收应答（固定格式）
            
            printf("OutNumber=0x%X\r\n",OutNumber);  //使用 printf 函数打印接收数据长度

            USB_SendData(UsbOutBuffer,OutNumber);    //发送数据缓冲区，长度
        }

        vTaskDelay(1);
    }
    
    vTaskDelete(NULL);
}   

