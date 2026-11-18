# -*- coding: utf-8 -*-
"""FlowPress 屏工程校验器
用法: python -I tools/validate_flowpress_screen.py   (退出码 0=全过 1=有问题)
检查: XML 良构 / 工程参数 / 变量表地址与固件映射一致 / 控件绑定与画面切换 /
坐标边界 / 字体存在 / 输入限幅 / 按钮置位复位 / 窗口判定语义(镜像固件算法)
"""
import os, sys
import xml.etree.ElementTree as ET

ROOT = r"D:/VisualTFT_Project/FlowPress"
PAGES = ("标题页", "实时数据页", "定时采集页")
fails = []

def chk(cond, msg):
    if not cond:
        fails.append(msg)

# ---- 1. XML 良构 ----
roots = {}
for f in PAGES:
    try:
        roots[f] = ET.parse(os.path.join(ROOT, f + ".tft")).getroot()
    except Exception as e:
        fails.append(f"{f}.tft 解析失败: {e}")
proj = script = fonts = None
try:
    proj = ET.parse(os.path.join(ROOT, "FlowPress.tftprj")).getroot()
    script = ET.parse(os.path.join(ROOT, "script.xml")).getroot()
    fonts = ET.parse(os.path.join(ROOT, "font", "fonts.xml")).getroot()
except Exception as e:
    fails.append(f"工程文件解析失败: {e}")
if fails:
    print("FAIL:\n" + "\n".join(fails)); sys.exit(1)

# ---- 2. 工程参数 ----
chk(proj.get("DeviceType") == "21014", "DeviceType != 21014 (DC80480DN070)")
chk(proj.get("DeviceBaudRate") == "3", "DeviceBaudRate != 3 (应为 9600)")
chk(proj.get("StartupPage") == "标题页", "StartupPage != 标题页")
plist = [p.get("RelativePath") for p in proj.find("Pages")]
chk(sorted(plist) == sorted(f + ".tft" for f in PAGES), f"Pages 不符: {plist}")
chk(script.get("protocol") == "1" and script.get("slave") == "1", "script 根参数")
chk(script.get("cycle_time") == "200" and script.get("screen_optimize") == "0",
    "轮询周期/优化读取参数")

# ---- 3. 变量表与固件映射一致 ----
vs = script.findall("./variants/variant")
names = {v.get("name"): v for v in vs}
addr = {v.get("name"): int(v.get("addr")) for v in vs}
expect = {"流量1瞬时": 0, "流量2瞬时": 2, "流量3瞬时": 4,
          "压力1": 6, "压力2": 8, "压力3": 10,
          "在线位图": 12, "流量1累计": 16, "流量2累计": 18, "流量3累计": 20,
          "采集使能": 24, "采集状态": 26,
          "流量1平均": 80, "流量2平均": 82, "流量3平均": 84,
          "压力1平均": 86, "压力2平均": 88, "压力3平均": 90,   # 0x0050~0x5B REG_AVG_BASE (config_app.h)
          "RTC年": 64, "RTC月": 65, "RTC日": 66, "RTC时": 67,
          "RTC分": 68, "RTC秒": 69, "RTC星期": 70}
for n, a in expect.items():
    chk(n in addr and addr[n] == a, f"变量 {n} 地址应为 {a}, 实为 {addr.get(n)}")
chn = ("流量1", "流量2", "流量3", "压力1", "压力2", "压力3")
for ci, ch in enumerate(chn):
    for ki, k in enumerate(("开始时", "开始分", "停止时", "停止分")):
        n = ch + k
        chk(n in addr and addr[n] == 32 + ci * 4 + ki, f"时刻变量 {n} 地址")
chk(len(vs) == 49, f"variant 数 {len(vs)} != 49")
addrs = [v.get("addr") for v in vs]
chk(len(addrs) == len(set(addrs)), "变量地址有重复")
for v in vs:
    chk(v.get("type") == "2" and v.get("rw") == "0" and v.get("byte_order") == "0",
        f"变量 {v.get('name')} type/rw/byte_order 不符")
    chk(v.get("factor_enable") == "0",
        f"变量 {v.get('name')} factor_enable != 0 (显示缩放未关)")
for n in ("流量1瞬时", "流量2瞬时", "流量3瞬时", "压力1", "压力2", "压力3",
          "流量1累计", "流量2累计", "流量3累计",
          "流量1平均", "流量2平均", "流量3平均",
          "压力1平均", "压力2平均", "压力3平均"):
    v = names.get(n)
    chk(v is not None and v.get("format") == "2" and v.get("dec_fill_zero") == "3",
        f"{n} 缺失或 format/dec != 2/3 (浮点3位小数)")
for n in ("RTC年", "RTC月", "RTC日", "RTC时", "RTC分", "RTC秒", "RTC星期"):
    chk(n in names, f"缺 RTC 变量 {n} (main.lua set_variant 需要同名)")

# ---- 4. 页面控件: 绑定/切换/坐标 ----
for pn, rt in roots.items():
    for it in rt.iter("item"):
        x, y = int(it.get("xOffset", 0)), int(it.get("yOffset", 0))
        w, h = int(it.get("width", 0)), int(it.get("height", 0))
        chk(x >= 0 and y >= 0 and x + w <= 800 and y + h <= 480,
            f"{pn}/{it.get('name')} 越界 ({x},{y},{w},{h})")
        b = it.get("bind_variant", "")
        if b:
            chk(b in names, f"{pn}/{it.get('name')} 绑定未知变量 {b}")
        sw = it.get("switch", "")
        if sw:
            chk(sw in PAGES, f"{pn}/{it.get('name')} 切换到未知画面 {sw}")

disp2 = [it for it in roots["实时数据页"].iter("item")
         if it.get("type") == "text_display" and it.get("input_mode") == "0"]
chk(len([d for d in disp2 if d.get("bind_variant")]) == 6,
    f"实时数据页数值框 {len(disp2)} != 6")
disp3 = [it for it in roots["定时采集页"].iter("item")
         if it.get("type") == "text_display" and it.get("input_mode") == "0"]
chk(len([d for d in disp3 if d.get("bind_variant")]) == 6,
    f"定时采集页数值框 {len(disp3)} != 6")
inst_v = {"流量1瞬时", "流量2瞬时", "流量3瞬时", "压力1", "压力2", "压力3"}
avg_v = {"流量1平均", "流量2平均", "流量3平均", "压力1平均", "压力2平均", "压力3平均"}
b2 = {d.get("bind_variant") for d in disp2}
chk(b2 == inst_v, f"实时数据页数值框应绑瞬时变体, 实为 {b2}")
b3 = {d.get("bind_variant") for d in disp3}
chk(b3 == avg_v, f"定时采集页数值框应绑平均变体, 实为 {b3}")
avg_order = ("流量1平均", "流量2平均", "流量3平均", "压力1平均", "压力2平均", "压力3平均")
by3 = {d.get("name"): d.get("bind_variant") for d in disp3}
for r in range(6):
    chk(by3.get(f"Val{r+1}") == avg_order[r],
        f"定时采集页 Val{r+1} 应绑 {avg_order[r]}, 实为 {by3.get(f'Val{r+1}')}")
by2 = {d.get("name"): d.get("bind_variant") for d in disp2}
flow_order = ("流量1瞬时", "流量2瞬时", "流量3瞬时")
press_order = ("压力1", "压力2", "压力3")
for r in range(3):
    chk(by2.get(f"ValF{r+1}") == flow_order[r],
        f"实时数据页 ValF{r+1} 应绑 {flow_order[r]}, 实为 {by2.get(f'ValF{r+1}')}")
    chk(by2.get(f"ValP{r+1}") == press_order[r],
        f"实时数据页 ValP{r+1} 应绑 {press_order[r]}, 实为 {by2.get(f'ValP{r+1}')}")
inp = [it for it in roots["定时采集页"].iter("item")
       if it.get("type") == "text_display" and it.get("input_mode") == "1"]
chk(len(inp) == 24, f"输入框 {len(inp)} != 24")
for it in inp:
    chk(it.get("value_limit") == "1", f"{it.get('name')} 未开限幅")
    chk(it.get("max_value") in ("23", "59"), f"{it.get('name')} 限幅值错")
    chk(int(it.get("min_value", -1)) == 0, f"{it.get('name')} min!=0")

btns = {it.get("name"): it for it in roots["定时采集页"].iter("item")
        if it.get("type") == "button"}
chk("BtnCollect" in btns and btns["BtnCollect"].get("button_style") == "3"
    and btns["BtnCollect"].get("bind_variant") == "采集使能", "采集按钮(置位写1)")
chk("BtnStop" in btns and btns["BtnStop"].get("button_style") == "4"
    and btns["BtnStop"].get("bind_variant") == "采集使能", "停止按钮(复位写0)")
nav = [(pg, it) for pg, rt in roots.items()
       for it in rt.iter("item") if it.get("type") == "button"
       and it.get("switch")]
swset = {it.get("switch") for _, it in nav}
chk(swset == {"实时数据页", "标题页", "定时采集页"}, f"导航切换目标: {swset}")
for pg, it in nav:
    chk(it.get("button_type") == "0", f"{pg}/{it.get('name')} 切换按钮 button_type 应为 0")
for pg, rt in roots.items():
    for it in rt.iter("item"):
        for k in ("url_up", "url_down"):
            u = it.get(k, "")
            if u:
                chk(os.path.exists(os.path.join(ROOT, u.replace("\\", "/"))),
                    f"{pg}/{it.get('name')} 缺图 {u}")

hdr = next((it for it in roots["定时采集页"].iter("item") if it.get("name") == "HdV"), None)
chk(hdr is not None and hdr.get("text") == "平均值", "定时采集页列头 HdV 应为 '平均值'")

# ---- 5. 字体存在 ----
fids = {f.get("id") for f in fonts.iter("font")}
for pn, rt in roots.items():
    for it in rt.iter("item"):
        f = it.get("font")
        if f:
            chk(f in fids, f"{pn}/{it.get('name')} 字体 {f} 不在 fonts.xml")

# ---- 6. 窗口判定语义 (镜像固件 Sched_WindowHit) ----
def window_hit(sh, sm, eh, em, nh, nm):
    s, e, n = sh * 60 + sm, eh * 60 + em, nh * 60 + nm
    if s == e:
        return 0
    if s < e:
        return 1 if (n >= s and n < e) else 0
    return 1 if (n >= s or n < e) else 0

cases = [  # (sh,sm,eh,em,nh,nm) -> 期望
    ((8, 0, 18, 0, 8, 0), 1),      # 边界: 开始时刻含
    ((8, 0, 18, 0, 17, 59), 1),
    ((8, 0, 18, 0, 18, 0), 0),     # 停止时刻不含
    ((8, 0, 18, 0, 7, 59), 0),
    ((22, 0, 6, 0, 23, 30), 1),    # 跨午夜: 22:00~06:00
    ((22, 0, 6, 0, 5, 59), 1),
    ((22, 0, 6, 0, 6, 0), 0),
    ((22, 0, 6, 0, 21, 59), 0),
    ((8, 0, 8, 0, 8, 30), 0),      # 零窗口无效
]
for a, want in cases:
    got = window_hit(*a)
    chk(got == want, f"窗口 {a} 期望 {want} 实得 {got}")

print(("FAIL %d 项:\n" % len(fails) + "\n".join(fails)) if fails else "PASS 全部通过")
sys.exit(1 if fails else 0)
