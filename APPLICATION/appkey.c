#include "appkey.h"         // 按键模块头文件：提供按键标志和函数声明
#include "config.h"         // 系统配置：提供u8/u16/u32类型定义
#include "delay.h"          // 延时函数
#include "appvalve.h"       // 阀门控制（保留引用，valve相关类型/宏）
#include "param_config.h"   // 参数配置：提供g_param_config和ParamType_t
#include "control_logic.h"  // 控制逻辑：提供Control_ExecuteManualFlush
#include "menu_system.h"    // 菜单系统：提供g_menu_status、g_lcd_dirty
#include "flush_control.h"  // 反冲洗控制
#include "menu.h"           // 菜单入口头文件（Show_Menu已移除，占位保留）


// 按键初始化 - 配置按键引脚为输入模式（准双向口，外部 47K 上拉）
void Key_Init(void)
{
	/* [4G移植] OK(P1.4)/RUN(P1.5) 仍在 P1 口 */
	P1M1 &= ~0x30;  // 清除 P1.4~P1.5 的 M1 位，准双向口
	P1M0 &= ~0x30;  // 清除 P1.4~P1.5 的 M0 位

	/* 返回键 P8.1：扩展端口（EAXFR 切换访问），准双向输入 */
	XSFR_ON();
	P8M1 &= ~0x02;
	P8M0 &= ~0x02;
	P8OUT |= 0x02;   /* ★ 返回键锁存置1：准双向输入锁存必须=1才有上拉，否则引脚被锁存0强拉低=恒按下 */

	/* ADD PA.2 / SUB PA.1：扩展端口 PA，准双向输入 */
	PAM1 &= ~0x06;
	PAM0 &= ~0x06;
	XSFR_OFF();
}

// 读扩展口按键原始位（EAXFR 切换封装）
// STC32G：P8IN/PAIN 是扩展端口寄存器，读取必须 EAXFR=1；读后立即恢复 EAXFR=0，
// 否则 MOVX 访问 xdata 变量(g_param_config 等)会错接到 XSFR → 全局状态损坏。
u8 Key_ReadExt(void)
{
	u8 v;
	XSFR_ON();
	v = 0;
	if(P8IN & 0x02) v |= 0x01;   // P8.1 返回键
	if(PAIN & 0x02) v |= 0x02;   // PA.1 SUB
	if(PAIN & 0x04) v |= 0x04;   // PA.2 ADD
	XSFR_OFF();
	return v;
}

// 按键状态标志（由Key_Scan()维护，其他模块只读）
bit key_return = 0;  // 返回键单次脉冲：首次按下时置1，Key_Process()消费后清0 [2026-08-06]
bit key_add  = 0;   // ADD按键单次脉冲：首次按下时置1，Key_Process()消费后清0
bit key_sub  = 0;   // SUB按键单次脉冲：首次按下时置1，Key_Process()消费后清0
bit key_ok   = 0;   // OK按键单次脉冲：首次按下时置1，Menu_HandleKey()消费后清0
bit key_run  = 0;   // RUN按键单次脉冲：首次按下时置1，Key_Process()消费后清0
bit key_ok_held = 0; // OK按键持续信号：消抖后按住不放时持续为1，松手清0

#define DEBOUNCE_COUNT  2  // 消抖确认阈值：连续2次读取相同状态才确认按键（约40ms）

// 按键扫描（每20ms调用一次）
// 使用"最高计数胜出"算法：每个按键独立消抖，计数最高的键判定为真正按键
void Key_Scan(void)
{
	static u8 db[5] = {0};    // 消抖计数器：db[0]=返回键(P1.1)，db[1]=ADD，db[2]=SUB，db[3]=OK，db[4]=RUN [2026-08-06 db[0]重新启用]
	static u8 fired = 0;       // 已触发标志位域：防止同一次按键产生多个脉冲（必须u8，bit类型无法做位掩码操作）
	u8 max_cnt;                 // 最高消抖计数：用于串扰抑制算法

	// 返回键(P1.1)独立消抖 [2026-08-06重新启用]
	if(KEY_RETURN == 0)
	{
		if(db[0] < DEBOUNCE_COUNT) db[0]++;
	}
	else
	{
		db[0] = 0;
		fired &= ~0x01;
		key_return = 0;
	}

	// 独立消抖：每个按键独立计数，按下时递增，松开时清零
	if(KEY_ADD  == 0)
	{ 
		if(db[1] < DEBOUNCE_COUNT) db[1]++; 
	} 
	else 
	{ 
		db[1] = 0; 
		fired &= ~0x02;
		key_add  = 0; 
	}
	if(KEY_SUB  == 0) 
	{ 
		if(db[2] < DEBOUNCE_COUNT) db[2]++; 
	} 
	else 
	{ 
		db[2] = 0; 
		fired &= ~0x04; 	
		key_sub  = 0; 
	}
	if(KEY_OK_PIN   == 0)
	{ 
		if(db[3] < DEBOUNCE_COUNT) db[3]++; 
	} 
	else 
	{ 
		db[3] = 0; 
		fired &= ~0x08; 
		key_ok   = 0; 
		key_ok_held = 0; 
	}
	if(KEY_RUN_PIN  == 0)
	{ 
		if(db[4] < DEBOUNCE_COUNT) db[4]++; 
	} 
	else 
	{ 
		db[4] = 0; 
		fired &= ~0x10; 	
		key_run  = 0; 
	}

	// 串扰抑制：消抖计数最高的键判定为真正按键，抑制其他所有键
	// [2026-08-03] 抑制时只清 db/key 标志，不再清 fired —— fired 只在物理释放时清除，
	//   否则"按住ADD时SUB瞬时过阈"会把ADD的fired清掉导致重复调整（一次按住多次触发）
	max_cnt = 0;
	if(db[0] > max_cnt) max_cnt = db[0];
	if(db[1] > max_cnt) max_cnt = db[1];
	if(db[2] > max_cnt) max_cnt = db[2];
	if(db[3] > max_cnt) max_cnt = db[3];
	if(db[4] > max_cnt) max_cnt = db[4];
	if(db[0] < max_cnt)
	{
		db[0]=0;
		key_return=0;
	}
	if(db[1] < max_cnt)
	{
		db[1]=0;
		key_add=0;
	}
	if(db[2] < max_cnt)
	{
		db[2]=0;
		key_sub=0;
	}
	if(db[3] < max_cnt)
	{
		db[3]=0;
		key_ok=0;
	}
	if(db[4] < max_cnt)
	{
		db[4]=0;
		key_run=0;
	}

	// P1.3(SUB)→P1.2(ADD) 硬件串扰: 平局时压制ADD，信任SUB
	if(db[1] >= DEBOUNCE_COUNT && db[2] >= DEBOUNCE_COUNT)
	{
		db[1]=0;
		key_add=0;
	}

	// P1.5(RUN)→P1.4(OK) 硬件串扰: 平局时压制OK，信任RUN
	if(db[3] >= DEBOUNCE_COUNT && db[4] >= DEBOUNCE_COUNT)
	{
		db[3]=0;
		key_ok=0;
		key_ok_held=0;
	}

	// P1.1(返回)→P1.2(ADD) 硬件串扰: 平局时压制ADD，信任返回键 [2026-08-06]
	if(db[0] >= DEBOUNCE_COUNT && db[1] >= DEBOUNCE_COUNT)
	{
		db[1]=0;
		key_add=0;
	}

	// P1.1(返回)→P1.5(RUN) 硬件串扰: 平局时压制RUN，信任返回键 [2026-08-08]
	//   与上一规则同模式。P1.1 历史串扰严重（CLAUDE.md 记录），空闲睡眠改变主循环
	//   采样节奏后串扰更容易逃逸，这里从扫描层保证返回键按下时 RUN 不被误置。
	if(db[0] >= DEBOUNCE_COUNT && db[4] >= DEBOUNCE_COUNT)
	{
		db[4]=0;
		key_run=0;
	}

	// 生成单次flag（仅首次按下时置1，fired位防止重复触发）
	if(db[0] >= DEBOUNCE_COUNT && !(fired & 0x01))
	{
		db[0]=0;
		key_return=1;
		fired |= 0x01;
	}
	if(db[1] >= DEBOUNCE_COUNT && !(fired & 0x02))
	{ 
		db[1]=0;
		key_add=1;
		fired |= 0x02; 
	}
	if(db[2] >= DEBOUNCE_COUNT && !(fired & 0x04)) 
	{ 
		db[2]=0;
		key_sub=1;
		fired |= 0x04; 
	}
		// OK: 单次flag + 持续信号合并，db[3]只清零一次
		if(db[3] >= DEBOUNCE_COUNT)
		{
			db[3]=0;
			key_ok_held = 1;
			if(!(fired & 0x08))
			{
				key_ok=1;
				fired |= 0x08;
			}
		}
	if(db[4] >= DEBOUNCE_COUNT && !(fired & 0x10)) 
	{ 
		db[4]=0; 
		key_run  = 1; 
		fired |= 0x10; 
	}
}

// 按键业务处理（由主循环调用，处理ADD/SUB/RUN按键）
// OK按键由Menu_HandleKey()独占处理
void Key_Process(void)
{
	// 注：原 standby 的 current_valve 阀切换变量已随 dead code 一并移除 [2026-08-03]

	// ★ 强制互斥：返回键(P1.1)与RUN(P1.5)同帧同时置1时，清零RUN只处理返回键 [2026-08-08]
	//   背景：P1.1 历史串扰到相邻引脚（硬件不可改），空闲睡眠改变了主循环节奏后，
	//   串扰帧更容易逃过 Key_Scan 的 max-count-wins 抑制，导致 key_run 被误置。
	//   这里在消费前硬性保证"返回键优先"，从根上堵住串扰→手动冲洗的路径。
	if(key_return && key_run) {
		key_run = 0;                   // 返回键优先，丢弃同帧的误触RUN
	}

	// 返回键(P1.1)：任何页面短按→回待机；校准页=取消校准 [2026-08-06]
	// 只切屏幕，不停止正在进行的反冲洗（冲洗在后台继续，待机画面显示冲洗状态）
	if(key_return) {
		key_return = 0;                   // 消费按键标志
		if(g_menu_status.menu_level == MENU_LEVEL_ZERO_CALIB) {
			Control_CancelCalib();        // 取消校准：恢复adc_zero、清零zero_calibration、落盘
		}
		g_menu_status.menu_level = MENU_LEVEL_NONE;
		g_menu_status.editing = 0;
		g_lcd_dirty = 1;
		return;
	}

	// 上电关阀测试期间忽略所有按键 [2026-08-06]
	if(BootTest_IsActive()) return;

	// RUN按键：只用于手动冲洗，且仅待机有效 [2026-08-03]
	// 菜单/子菜单/校准模式中RUN一律忽略（取消校准改用长按OK，见Menu_HandleKey）
	if(key_run) {
		key_run = 0;                   // 消费按键标志（防止重复触发）
		if(g_menu_status.menu_level != MENU_LEVEL_NONE)
		{
			return;
		}
		Control_ExecuteManualFlush();
		return;
	}

	// ADD按键：根据当前菜单层级执行不同操作
	if(key_add) {
		key_add = 0;                   // 消费按键标志
		if(g_menu_status.menu_level == MENU_LEVEL_NONE) {
			/* [2026-08-03] 待机：ADD无操作。原"切换下一个阀"是dead code——
			   current_valve从未被屏幕显示或被任何阀驱动函数消费，已随其一起移除 */
		} else if(g_menu_status.menu_level == MENU_LEVEL_1) {
			if(g_menu_status.editing) Menu_AdjustParam(1);  // 编辑状态：增加参数值
			else if(g_menu_status.current_param == PARAM_FLUSH_SELECT) {
				f_select_choice = !f_select_choice; g_lcd_dirty = 1;
			}  // F-SELECT：切换YES/NO
		} else if(g_menu_status.menu_level == MENU_LEVEL_2) {
			if(g_menu_status.editing) Menu_AdjustParam(1);  // Level2编辑：增加参数值
		} else if(g_menu_status.menu_level == MENU_LEVEL_ZERO_CALIB) {
			if(g_menu_status.editing) Menu_AdjustParam(1);  // 零点校准：增加校准值
		} else if(g_menu_status.menu_level == MENU_LEVEL_FLUSH_SELECT) {
			// 冲洗站子菜单：切换到下一个站（1→2→...→10→1循环）
			g_menu_status.flush_station_num = (g_menu_status.flush_station_num >= 10) ? 1 : (g_menu_status.flush_station_num + 1);
			g_lcd_dirty = 1;
		}
		return;
	}

	// SUB按键：根据当前菜单层级执行不同操作
	if(key_sub) {
		key_sub = 0;                   // 消费按键标志
		if(g_menu_status.menu_level == MENU_LEVEL_NONE) {
			/* [2026-08-03] 待机：SUB无操作（原"切换上一个阀"dead code，已移除） */
		} else if(g_menu_status.menu_level == MENU_LEVEL_1) {
			if(g_menu_status.editing) Menu_AdjustParam(0);  // 编辑状态：减少参数值
			else if(g_menu_status.current_param == PARAM_FLUSH_SELECT) {
				f_select_choice = !f_select_choice; g_lcd_dirty = 1;
			}  // F-SELECT：切换YES/NO
		} else if(g_menu_status.menu_level == MENU_LEVEL_2) {
			if(g_menu_status.editing) Menu_AdjustParam(0);  // Level2编辑：减少参数值
		} else if(g_menu_status.menu_level == MENU_LEVEL_ZERO_CALIB) {
			if(g_menu_status.editing) Menu_AdjustParam(0);  // 零点校准：减少校准值
		} else if(g_menu_status.menu_level == MENU_LEVEL_FLUSH_SELECT) {
			// 冲洗站子菜单：切换当前站的启用/禁用状态
			g_param_config.flush_select[g_menu_status.flush_station_num-1] ^= 1;  // 异或切换：0→1或1→0
			Menu_SaveParam();   // 保存参数
			g_lcd_dirty = 1;
		}
	}
}
