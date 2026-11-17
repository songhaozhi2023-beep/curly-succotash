# -*- coding: utf-8 -*-
"""FlowPress 大彩屏工程生成器 (DC80480DN070 7寸800x480, Modbus 主机 9600 8N1)
用法: python -I tools/gen_flowpress_screen.py
输出: D:/VisualTFT_Project/FlowPress/  (目录已存在则拒绝运行, 防误覆盖)
依据: docs/superpowers/specs/2026-11-17-screen-three-page-design.md
XML 字段模板逐字取自实样: D:/STM32_Project/fattyWater3.0/Project3/ 与
D:/VisualTFT_Project/Project/ (水肥), 枚举经二进制 schema 核实(见规格 §3.3)。
"""
import os, shutil, sys

ROOT     = r"D:/VisualTFT_Project/FlowPress"
IMG_SRC  = r"D:/VisualTFT_Project/Project/images"
FONT_SRC = r"D:/VisualTFT_Project/Project/font"

# ---- 颜色 (深色工业风, 与水肥工程同底色) ----
BG       = "15;30;50"
TITLE_C  = "102;255;255"
WHITE    = "255;255;255"
FLOW_C   = "0;255;255";    FLOW_BG  = "18;44;72"
PRESS_C  = "255;210;138";  PRESS_BG = "44;36;18"
LINE_C   = "42;74;116"
INP_FG   = "0;0;0";        INP_BG   = "255;255;255"
GRAY     = "138;170;170"

# ---- 字体 (fonts.xml: 4=16px 6=20px 8=24px 12=32px 13=36px 17=52px) ----
F_BIG, F_H1, F_LBL, F_VAL, F_SML, F_HDR = 17, 13, 8, 12, 6, 4

def esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")

def text_item(i, name, t, x, y, w, h, font, fg, align=1, valign=1):
    t15 = " ".join(f'text{n}=""' for n in range(1, 16))
    return (f'<item name="{name}" id="{i}" type="text" multi_lang="1" '
            f'text_align="{align}" text_align_v="{valign}" text="{esc(t)}" {t15} '
            f'font="{font}" encode="0" show_bk="0" fore_color="{fg}" '
            f'bk_color="{WHITE}" xOffset="{x}" yOffset="{y}" width="{w}" '
            f'height="{h}" row_gap="0" col_gap="0"/>')

def display_item(i, name, t, x, y, w, h, font, fg, bkg, bind="",
                 editable=False, vmax=0, vmin=0):
    im, vl = (1, 1) if editable else (0, 0)
    return (f'<item name="{name}" id="{i}" type="text_display" text="{esc(t)}" '
            f'tipinfo="" font="{font}" encode="1" '
            f'show_bkg_style="{1 if editable else 0}" fore_color="{fg}" '
            f'bkg_color="{bkg}" bkg_image_path="" xOffset="{x}" yOffset="{y}" '
            f'width="{w}" height="{h}" input_mode="{im}" variant="0" '
            f'text_type="0" text_len_max="255" password="0" focus_rect="0" '
            f'text_align="1" text_align_v="1" value_limit="{vl}" '
            f'value_precision="0" max_value="{vmax}" min_value="{vmin}" '
            f'keyboard_init="0" keyboard_position="0" keyboard_x="0" '
            f'keyboard_y="0" art_digit="0" art_digit_icon="" half_width_dot="0" '
            f'bind_variant="{bind}" show_condition="0" condition_variant="" '
            f'condition_value="0"/>')

def button_item(i, name, x, y, w, h, img, text, style=0, switch="", bind="",
                font=F_LBL, fg=WHITE):
    e = esc(text)
    return (f'<item name="{name}" id="{i}" type="button" button_type="1" '
            f'focus="1" notify_disable="0" key_code="a" key_type="0" '
            f'init_state="0" button_style="{style}" longpress_delay="0" '
            f'url_down="{img}" url_up="{img}" popup_menu_id="0" '
            f'input_text_id="0" switch="{switch}" switch_effect="0" '
            f'switch_area="0" switch_area_left="0" switch_area_right="0" '
            f'switch_area_top="0" switch_area_bottom="0" action="" '
            f'xOffset="{x}" yOffset="{y}" width="{w}" height="{h}" cut_up="0" '
            f'cut_up_offset_x="0" cut_up_offset_y="0" cut_down="0" '
            f'cut_down_offset_x="0" cut_down_offset_y="0" custom_data_up="" '
            f'custom_data_down="" external_data_up="" external_data_down="" '
            f'external_data_delay="100" child_screen="0" need_login="0" '
            f'login_password="888888" show_text_state="1" font="{font}" '
            f'font_color_up="{fg}" font_color_down="{fg}" '
            f'text_state_up="{e}" text_state_down="{e}" bind_variant="{bind}" '
            f'show_condition="0" condition_variant="" condition_value="0"/>')

def line_item(i, name, x0, y0, x1, y1, color=LINE_C):
    return (f'<item name="{name}" id="{i}" type="line" x0="{x0}" y0="{y0}" '
            f'x1="{x1}" y1="{y1}" pen_width="1" pen_color="{color}"/>')

def page(name, items):
    return (f'<DrawPage name="{name}" bk_transparent="0" bk_color="{BG}" '
            f'bk_image="" size_option="0" width="800" height="480">'
            + "".join(items) + "</DrawPage>")

def variant(name, addr, fmt=0, dec=0, txtfmt=0):
    return (f'<variant name="{esc(name)}" length="16" slave="1" addr="{addr}" '
            f'sub_addr="0" type="2" rw="0" init_variant="0" restore_on_err="0" '
            f'value="0" string_value="" char_set="0" char_order="0" '
            f'format="{fmt}" byte_count="32" sign="0" byte_order="0" '
            f'factor_enable="0" factor="1" offset="0" text_format="{txtfmt}" '
            f'int_fill_zero="0" dec_fill_zero="{dec}" show_invalid_zero="0" '
            f'system_variant="0" save_data="0" is_array="0" array_size="10"/>')

def wfile(path, content):
    with open(path, "w", encoding="utf-8", newline="\r\n") as f:
        f.write(content)
    print("写", path)

# ============ 页面 ============
def build_title():
    return page("标题页", [
        text_item(1, "Text1", "流量压力集成检测仪器", 80, 150, 640, 70, F_BIG, TITLE_C),
        button_item(2, "BtnEnter", 355, 300, 90, 44, r"Images\btn.png", "进  入",
                    style=0, switch="实时数据页"),
    ])

ROWS = (130, 242, 354)          # 第2页行顶
FLOWN = ("流量 1", "流量 2", "流量 3")
PRESSN = ("压力 1", "压力 2", "压力 3")
FLOTV = ("流量1瞬时", "流量2瞬时", "流量3瞬时")
PRESSV = ("压力1", "压力2", "压力3")

def build_data():
    it = [
        button_item(1, "BtnBack", 8, 10, 90, 36, r"Images\btn.png", "返 回",
                    style=0, switch="标题页", font=F_SML),
        text_item(2, "TextTitle", "流量压力集成检测仪器", 200, 10, 400, 44, F_H1, TITLE_C),
        button_item(3, "BtnSched", 702, 10, 90, 36, r"Images\btn.png", "定时设置",
                    style=0, switch="定时采集页", font=F_SML),
        text_item(4, "TextFH", "流量（m³/h）", 30, 68, 240, 26, F_LBL, FLOW_C, align=0),
        text_item(5, "TextPH", "压力（MPa）", 430, 68, 240, 26, F_LBL, PRESS_C, align=0),
        line_item(6, "LineSep", 400, 64, 400, 480),
    ]
    k = 10
    for r, y in enumerate(ROWS):
        it.append(text_item(k, f"TextFL{r+1}", FLOWN[r], 30, y + 18, 140, 24, F_LBL, WHITE, align=0)); k += 1
        it.append(text_item(k, f"TextPL{r+1}", PRESSN[r], 430, y + 18, 140, 24, F_LBL, WHITE, align=0)); k += 1
        it.append(display_item(k, f"ValF{r+1}", "0.000", 180, y, 200, 60, F_VAL, FLOW_C, FLOW_BG, bind=FLOTV[r])); k += 1
        it.append(display_item(k, f"ValP{r+1}", "0.000", 580, y, 200, 60, F_VAL, PRESS_C, PRESS_BG, bind=PRESSV[r])); k += 1
    return page("实时数据页", it)

SROW = (78, 135, 192, 249, 306, 363)   # 第3页行顶 (表头占 54~72)
SCHED_V = tuple(f"{ch}{k}" for ch in ("流量1", "流量2", "流量3", "压力1", "压力2", "压力3")
                for k in ("开始时", "开始分", "停止时", "停止分"))
SCHED_LBL = FLOWN + PRESSN

def build_sched():
    it = [
        button_item(1, "BtnBack", 8, 10, 90, 36, r"Images\btn.png", "返 回",
                    style=0, switch="实时数据页", font=F_SML),
        text_item(2, "TextTitle", "定时采集设置", 200, 10, 400, 44, F_H1, TITLE_C),
        text_item(3, "HdS1", "开始 时", 100, 54, 80, 18, F_HDR, GRAY),
        text_item(4, "HdS2", "分", 180, 54, 64, 18, F_HDR, GRAY),
        text_item(5, "HdE1", "停止 时", 262, 54, 80, 18, F_HDR, GRAY),
        text_item(6, "HdE2", "分", 342, 54, 64, 18, F_HDR, GRAY),
        text_item(7, "HdV", "实时值", 580, 54, 200, 18, F_HDR, GRAY),
    ]
    k = 10
    for r, y in enumerate(SROW):
        it.append(text_item(k, f"TextL{r+1}", SCHED_LBL[r], 16, y + 10, 90, 24, F_LBL, WHITE, align=0)); k += 1
        for c in range(4):   # 开始时/开始分/停止时/停止分
            it.append(display_item(k, f"Inp{r+1}_{c}", "0", 110 + c * 74, y, 64, 44,
                                   F_LBL, INP_FG, INP_BG, bind=SCHED_V[r * 4 + c],
                                   editable=True,
                                   vmax=(23 if c in (0, 2) else 59))); k += 1
        it.append(display_item(k, f"Val{r+1}", "0.000", 580, y, 200, 44, F_VAL,
                               FLOW_C if r < 3 else PRESS_C,
                               FLOW_BG if r < 3 else PRESS_BG,
                               bind=(FLOTV + PRESSV)[r])); k += 1
    it.append(button_item(k, "BtnCollect", 280, 424, 110, 44, r"Images\btn_start.png", "采 集",
                          style=3, bind="采集使能", font=F_LBL)); k += 1
    it.append(button_item(k + 1, "BtnStop", 410, 424, 110, 44, r"Images\btn_stop.png", "停 止",
                          style=4, bind="采集使能", font=F_LBL))
    return page("定时采集页", it)

# ============ script.xml ============
def build_script():
    v = [variant("流量1瞬时", 0, fmt=2, dec=3, txtfmt=1),
         variant("流量2瞬时", 2, fmt=2, dec=3, txtfmt=1),
         variant("流量3瞬时", 4, fmt=2, dec=3, txtfmt=1),
         variant("压力1",     6, fmt=2, dec=3, txtfmt=1),
         variant("压力2",     8, fmt=2, dec=3, txtfmt=1),
         variant("压力3",    10, fmt=2, dec=3, txtfmt=1),
         variant("在线位图", 12),
         variant("流量1累计", 16, fmt=2, dec=3, txtfmt=1),
         variant("流量2累计", 18, fmt=2, dec=3, txtfmt=1),
         variant("流量3累计", 20, fmt=2, dec=3, txtfmt=1),
         variant("采集使能", 24),
         variant("采集状态", 26)]
    v += [variant(n, 32 + i) for i, n in enumerate(SCHED_V)]
    v += [variant(n, 64 + i) for i, n in
          enumerate(("RTC年", "RTC月", "RTC日", "RTC时", "RTC分", "RTC秒", "RTC星期"))]
    return ('<script protocol="1" version="1" slave="1" cycle_time="200" '
            'pool_delay_time="10" pool_mode="1" response_timeout="100" '
            'mulitple_read="64" screen_optimize="0" save_data="0" '
            'save_data_flag="0" enable_script="1" dgus_frame_head="23205">'
            '<variants>\r\n' + "\r\n".join(v) + '\r\n</variants><instructs/></script>')

MAIN_LUA = '''------------------------------------
-- FlowPress: 每分钟把屏 RTC 喂给 MCU (寄存器 0x0040~0x0046)
-- 参照 Project3 已验证写法: get_date_time + set_variant + 蔡勒星期
-- 注意: 请用 VisualTFT 自带 Lua 编辑器之外的工具编辑后,
--       关闭 VisualTFT 的 Lua 视图再打开工程(其自动保存会回滚外部修改)
------------------------------------

local rtcSyncMin = -1

local function calc_weekday(y, mo, d)
    local t = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4}
    if mo < 3 then y = y - 1 end
    local w = (y + math.floor(y / 4) - math.floor(y / 100)
                 + math.floor(y / 400) + t[mo] + d) % 7
    if w == 0 then w = 7 end
    return w
end

function on_systick()
    local sy, smo, sd, sh, smi = get_date_time()
    if smi ~= rtcSyncMin then
        rtcSyncMin = smi
        set_variant("RTC年", sy)
        set_variant("RTC月", smo)
        set_variant("RTC日", sd)
        set_variant("RTC时", sh)
        set_variant("RTC分", smi)
        set_variant("RTC秒", 0)
        set_variant("RTC星期", calc_weekday(sy, smo, sd))
    end
end
'''

TFTPRJ = '''<?xml version="1.0" encoding="utf-8"?>
<VisualTFT Name="FlowPress" OutputDirectory="output\\" StartupPage="标题页" StartupAction="" StartupActionLoop="1" DeviceType="21014" DeviceEnableControl="1" DeviceEnableTouchPane="1" DeviceEnableBuzzer="2" DeviceEnableCRC="0" DeviceBaudRate="3" DeviceCoordinateUpdateMode="4" DeviceControlNotify="3" DeviceScreenNotify="1" DeviceScreenRvs="0" DeviceBacklightAutoControl="0" DeviceBacklightTime="10" DeviceBacklightOn="200" DeviceBacklightOff="50" DeviceBacklightNotify="0" DeviceLockConfig="1" DeviceStartupVoice="0" DeviceVoiceVolume="100" DeviceEnableSlidingScreen="0" BeginSlidingScreen="" EndSlidingScreen="" CompatibleScreenRotate="1" AddResourceFloder="1" FastRefreshScreen="0" LUAPrecompile="0" DelayInitScreen="0" ImageDithering="1" ImageFormat="1" JPEGQuality="95" StartupLogo="" DHCP="0" IPAddress="192.168.1.100" NetMask="255.255.255.0" Gateway="192.168.1.1" DNS="192.168.1.1" NetworkMode="0" ServerAddress="192.168.1.200" ServicePort="5050" NetworkTransfer="0" WifiMode="0" WifiSSID="" WifiPassword="" FlashType="1" PartitionSize0="100" PartitionSize1="10" PartitionSize2="0" Repartition="1" FormatPartition="1" UartDownloadSpeed="0" PKGSetting="0" PKGControl="1" PKGImage="1" PKGFont="1" PKGOther="1" Version="1.0" EnableSlidingRange="0" EnableKeysound="0" BacklightAutoControl="0" BacklightNotify="0" StandbyPage="" ServerPort="5050" WifiSSI="">
  <Pages><Page RelativePath="标题页.tft"/><Page RelativePath="实时数据页.tft"/><Page RelativePath="定时采集页.tft"/></Pages>
  <Images><Image RelativePath="Images\\btn.png" Download="0"/><Image RelativePath="Images\\btn_start.png" Download="0"/><Image RelativePath="Images\\btn_stop.png" Download="0"/></Images>
  <Waves/>
</VisualTFT>'''

def main():
    if os.path.exists(ROOT):
        sys.exit(f"拒绝运行: {ROOT} 已存在 (防误覆盖)。确认无用后手动删除再跑。")
    os.makedirs(os.path.join(ROOT, "Images"))
    # 字体整目录拷贝 (fonts.xml + 编译好的字库)
    shutil.copytree(FONT_SRC, os.path.join(ROOT, "font"))
    # 按钮底图 (来自水肥工程, 改 ASCII 名)
    imgs = {r"按钮 (1).png": "btn.png",
            r"开始按钮.png": "btn_start.png",
            r"急停按钮.png": "btn_stop.png"}
    for src, dst in imgs.items():
        p = os.path.join(IMG_SRC, src)
        if not os.path.exists(p):
            sys.exit(f"缺素材: {p}")
        shutil.copy(p, os.path.join(ROOT, "Images", dst))
    wfile(os.path.join(ROOT, "FlowPress.tftprj"), TFTPRJ)
    wfile(os.path.join(ROOT, "标题页.tft"), build_title())
    wfile(os.path.join(ROOT, "实时数据页.tft"), build_data())
    wfile(os.path.join(ROOT, "定时采集页.tft"), build_sched())
    wfile(os.path.join(ROOT, "script.xml"), build_script())
    wfile(os.path.join(ROOT, "main.lua"), MAIN_LUA)
    print("完成:", ROOT)

if __name__ == "__main__":
    main()
