#ifndef __MENU_SYSTEM_H
#define __MENU_SYSTEM_H

#include "config.h"
#include "param_config.h"

extern bit g_lcd_dirty;
extern bit g_screen_off;             /* 1=灭屏，0=亮屏 [2026-08-08] */
extern bit f_select_choice;
void Menu_ScreenActivity(void);      /* 人为按键：重置亮屏倒计时；若灭屏则唤醒重绘 [2026-08-08] */
void Menu_Init(void);
void Menu_HandleKey(void);
void Menu_Display(void);
void Menu_EnterLevel1(void);
void Menu_EnterLevel2(void);
void Menu_NavigateParam(void);
void Menu_AdjustParam(u8 direction);
void Menu_SaveParam(void);
void Menu_DisplayMainMenu(void);
void Menu_DisplayParamValue(ParamType_t param);
void Menu_DisplayFlushStation(void);
void Menu_ClockTick(void);
void Menu_BlinkTick(void);

#endif
