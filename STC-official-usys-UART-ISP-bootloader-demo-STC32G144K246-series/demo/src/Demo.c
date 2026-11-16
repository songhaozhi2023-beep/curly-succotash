#include "stc32g144k246.h"
#include "intrins.h"

#define FOSC            48000000UL
#define T1MS            (65536 - FOSC/1000)

#define DFU_TAG         0x12abcd34      //DFU强制执行标志
long xdata DfuFlag _at_ 0xfffc;         //DFU标志, 定义在xdata的最后4字节

void sys_init();

int cnt200;

void int0_isr() interrupt 0
{
}

void tm0_isr() interrupt 1
{
    if (cnt200++ >= 200)
    {
        cnt200 = 0;
        P2 = ~P2;
    }
}

void main()
{
    sys_init();
    
    while (1)
    {
        if (P32 == 0)
        {
            DfuFlag = DFU_TAG;          //当需要执行用户ISP代码时,将强制执行标志赋值到DFU标志变量中
            IAP_CONTR = 0x28;           //然后执行软复位指令触发芯片软复位到用户系统区
        }
    }
}

void sys_init()
{
    WTST = 0x00;
    CKCON = 0x00;
    EAXFR = 1;
    
    P2M0 = 0xff;
    P2M1 = 0x00;
    P3M0 = 0x00;
    P3M1 = 0x00;
    
    P32 = 0;
    _nop_();
    _nop_();
    P32 = 1;
    P3PU |= 0x04;
    
    TMOD = 0x00;
    T0x12 = 1;
    TL0 = T1MS;
    TH0 = T1MS >> 8;
    TR0 = 1;
    ET0 = 1;
    
    DfuFlag = 0;                        //上电正常执行用户AP,时需要将DFU标志清零
    cnt200 = 0;
    
    EA = 1;
}

