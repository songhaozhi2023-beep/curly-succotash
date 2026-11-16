#include "stc.h"
#include "uart.h"
#include "iap.h"
#include "dfu.h"

long xdata DfuFlag _at_ 0xfffc;

static void delay()
{
    int i;
    
    for (i = 0; i < 30000; i++);
}

void dfu_check()
{
    EAXFR = 1;
    P3M1 &= ~0x08;
    P3PU |= 0x08;
    _nop_();
    _nop_();
    _nop_();
    _nop_();
    delay();
    
    if ((DFU_FORCEPIN != 0) &&
        (DfuFlag != DFU_TAG))
    {
        P3M1 |= 0x08;
        P3PU = 0x00;
        IAP_CONTR = 0x20;
    }
    
    P3M1 |= 0x08;
    P3PU = 0x00;
    DfuFlag = 0;
}

void dfu_events()
{
    BYTE cmd;
    DWORD addr;
    BYTE size;
    BYTE ret;
    BYTE *ptr;
    BYTE status;

    if (!bUartRxReady)
        return;

    cmd = UartRxBuffer[1];
    addr = *(DWORD *)&UartRxBuffer[2];
    size = UartRxBuffer[6];
    ptr = &UartRxBuffer[7];
    status = STATUS_OK;
    ret = 0;

    switch (cmd)
    {
    case DFU_CMD_CONNECT:
        UartTxBuffer[0] = LDR_VERSION >> 8;
        UartTxBuffer[1] = LDR_VERSION;
        ret = 2;
        break;
    case DFU_CMD_READ:
#ifdef DEBUG
        ret = size;
        ptr = &UartInBuffer[0];
        while (size--)
        {
            *ptr++ = iap_read_byte(addr++);
        }
#else
        status = STATUS_ERRORCMD;
#endif
        break;
    case DFU_CMD_PROGRAM:
        while (size--)
        {
            if (!iap_write_byte(addr, *ptr))
            {
                status = STATUS_PROGRAMERR;
                break;
            }
            addr++;
            ptr++;
        }
        break;
    case DFU_CMD_ERASE:
        addr = 0;
        while (addr < AP_SIZE)
        {
            iap_erase_page(addr);
            addr += 0x200;
        }
        break;
    case DFU_CMD_REBOOT:
        IAP_CONTR = 0x20;
        while (1);
        break;
    default:
        status = STATUS_ERRORCMD;
        break;
    }

    uart_send(status, ret);
    uart_recv_done();
}
