# -*- coding: utf-8 -*-
# 用途:从 STC32G144K246 官方手册 PDF 中提取串口5~8及端口寄存器定义页
# 跑完删除本脚本即可,不影响任何工程文件
import io, sys
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

try:
    import fitz
except ImportError:
    print("缺少依赖,请先运行: python -m pip install pymupdf")
    input("按回车退出")
    sys.exit(1)

PDF = r"d:\360Downloads\xwechat_files\wxid_w59y8l351sox22_22e0\temp\RWTemp\2026-11\cdb0e1daaf41b54e037b03ebf2d9860c\STC32G144K246-20260908.pdf"
OUT = r"d:\流量压力检测集成装置\k246_uart5_8.txt"

KEYS = ["USART5","USART6","USART7","USART8",
        "S5CON","S5BUF","S6CON","S6BUF","S7CON","S7BUF","S8CON","S8BUF",
        "T5T6M","T7T8M","PAOUT","PBOUT","PAM0","PAM1","PBM0","PBM1",
        "EAXFR","S5_S1","S5_S0"]

doc = fitz.open(PDF)
n = len(doc)
print("总页数:", n)

pages_text = []
for i in range(n):
    try:
        t = doc[i].get_text() or ""
    except Exception:
        t = ""
    pages_text.append(t)
    if (i + 1) % 200 == 0:
        print("  已处理 %d / %d 页..." % (i + 1, n))
doc.close()
print("提取完成,开始检索关键词...")

page_keys = {}
for i, t in enumerate(pages_text):
    ks = [k for k in KEYS if k in t]
    if ks:
        page_keys[i + 1] = ks

print()
print("=== 命中统计 ===")
for k in KEYS:
    ps = [p for p, ks in sorted(page_keys.items()) if k in ks]
    print("%s: %d 页 -> %s" % (k, len(ps), ps[:30]))

with open(OUT, "w", encoding="utf-8") as f:
    for pno, ks in sorted(page_keys.items()):
        f.write("\n" + "=" * 60 + "\n")
        f.write("第 %d 页  命中: %s\n" % (pno, ", ".join(ks)))
        f.write("=" * 60 + "\n")
        f.write(pages_text[pno - 1])

print()
if page_keys:
    print("结果已写入:", OUT)
else:
    print("!! 全部关键词 0 命中,该 PDF 可能是扫描图片版,需换 OCR 方案")
input("按回车退出")