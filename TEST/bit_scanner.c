#include "config.h"
#include "ht1622.h"
#include "delay.h"
#include "appkey.h"
#include "buzzer.h"
#include "app_led.h"

void BitScanner(void)
{
    u8 addr = 0;
    u8 bit_pattern = 1;  // 1=bit0, 2=bit1, 4=bit2, 8=bit3, 15=ALL
    u8 last_state = 0;   // 0=地址显示，切换到bit模式时显示bit值

    // 清屏，显示初始地址0全部bit
    HT1622_Clear();
    HT1622_WriteData(addr, 0x0F);
    delay_ms(500);

    while(1)
    {
        // ---- ADD键：地址+1 ----
        if(KEY_ADD == 0)
        {
            delay_ms(30);  // 消抖
            if(KEY_ADD == 0)
            {
                addr = (addr + 1) % 38;
                HT1622_Clear();
                HT1622_WriteData(addr, bit_pattern);
                // 蜂鸣器报地址号 (快响N次)
                {
                    u8 n;
                    for(n = 0; n < addr; n++)
                    {
                        Buzzer_On();  delay_ms(80);
                        Buzzer_Off(); delay_ms(120);
                    }
                    // 长响一声表示当前地址
                    Buzzer_On();  delay_ms(500);
                    Buzzer_Off();
                }
            }
            while(KEY_ADD == 0);  // 等待释放
        }

        // ---- SUB键：地址-1 ----
        if(KEY_SUB == 0)
        {
            delay_ms(30);
            if(KEY_SUB == 0)
            {
                addr = (addr == 0) ? 37 : addr - 1;
                HT1622_Clear();
                HT1622_WriteData(addr, bit_pattern);
                {
                    u8 n;
                    for(n = 0; n < addr; n++)
                    {
                        Buzzer_On();  delay_ms(80);
                        Buzzer_Off(); delay_ms(120);
                    }
                    Buzzer_On();  delay_ms(500);
                    Buzzer_Off();
                }
            }
            while(KEY_SUB == 0);
        }

        // ---- OK键：切换bit模式 (1→2→4→8→15→1...) ----
        if(KEY_OK_PIN == 0)
        {
            delay_ms(30);
            if(KEY_OK_PIN == 0)
            {
                if(bit_pattern == 1)       bit_pattern = 2;
                else if(bit_pattern == 2)  bit_pattern = 4;
                else if(bit_pattern == 4)  bit_pattern = 8;
                else if(bit_pattern == 8)  bit_pattern = 15;
                else                       bit_pattern = 1;

                HT1622_Clear();
                HT1622_WriteData(addr, bit_pattern);

                // 用蜂鸣报bit模式: 1声=bit0, 2声=bit1, 3声=bit2, 4声=bit3, 5声=ALL
                {
                    u8 beeps;
                    u8 i;
                    if(bit_pattern == 1)      beeps = 1;
                    else if(bit_pattern == 2) beeps = 2;
                    else if(bit_pattern == 4) beeps = 3;
                    else if(bit_pattern == 8) beeps = 4;
                    else                      beeps = 5;

                    for(i = 0; i < beeps; i++)
                    {
                        Buzzer_On();  delay_ms(100);
                        Buzzer_Off(); delay_ms(100);
                    }
                }
            }
            while(KEY_OK_PIN == 0);
        }

        LED_Update();  // 约19ms循环
    }
}
