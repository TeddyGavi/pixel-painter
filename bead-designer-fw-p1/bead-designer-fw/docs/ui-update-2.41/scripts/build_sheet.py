import gfx
from gfx import Img, load_png, upscale, draw_text, text_w, gray
from screens import draw_screen, tex, strip, name_line, W, H, WHITE, BLACK, TXT_DIM, COL
import os
O = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "mockups") + "/"
HW = "/home/matt/pixel-painter/bead-designer-fw-p1/bead-designer-fw/hw/out/"
cur = upscale(load_png(HW + "draw.png"), 2)
curm = upscale(load_png(HW + "menu.png"), 2)
row1 = [("Today (225x300 x2)", cur), ("A Soft bead (rec.)", load_png(O + "draw_12.png")),
        ("B Flat + lift", load_png(O + "draw_12_optB.png")), ("C Tray", load_png(O + "draw_12_optC.png"))]
row2 = [("A 9x9", load_png(O + "draw_9.png")), ("A 15x15 strip focus", load_png(O + "draw_15.png")),
        ("Menu (today)", curm), ("Menu", load_png(O + "menu.png")),
        ("Grid size", load_png(O + "menu_grid.png")), ("Dialog", load_png(O + "dialog.png"))]
pad, lab = 20, 50
cols = max(len(row1), len(row2))
S = Img(cols * (W + pad) + pad, 2 * (H + lab + pad) + pad, (24, 24, 24))
for r, row in enumerate((row1, row2)):
    for c, (t, im) in enumerate(row):
        x = pad + c * (W + pad); y = pad + r * (H + lab + pad)
        draw_text(S, x, y, t, WHITE, 3, True)
        S.paste(im, x, y + lab)
S.save(O + "sheet.png")

# ---- 1:1 crop: strip + name line, today vs proposal; text and texture samples
C = Img(3 * W + 4 * pad, 150 + 236 + 2 * pad + lab + 40, (24, 24, 24))
a = load_png(O + "draw_12.png"); f = load_png(O + "draw_15.png")
for i, (t, im) in enumerate((("Today", cur), ("A, board focus", a), ("A, strip focus", f))):
    x = pad + i * (W + pad)
    draw_text(C, x, pad, t, WHITE, 3, True)
    C.paste(im, x, pad + lab, 0, 450, W, 150)
y2 = pad + lab + 150 + pad
draw_text(C, pad, y2, "Text: x4 plain (today) / x3 plain / x3 Scale3x / x2 Scale3x", WHITE, 2, True)
T = Img(W * 3 + 2 * pad, 200)
T2 = None
for yy in range(T.h):
    for xx in range(T.w): T.set(xx, yy, tex(xx, yy))
draw_text(T, 10, 6, "S57 Red 25", WHITE, 4, False, outline=BLACK, oth=2)
draw_text(T, 10, 70, "S57 Red 25", WHITE, 3, False, outline=BLACK, drop=(1, 2, 0.6))
draw_text(T, 10, 120, "S57 Red 25", WHITE, 3, True, outline=BLACK, drop=(1, 2, 0.6))
draw_text(T, 10, 168, "S57 Red 25  (x2 is too small)", WHITE, 2, True, outline=BLACK)
# texture swatches: 2 px (today, recommended) vs 1 px
for k, (t, ts) in enumerate((("tex 2px", 2), ("tex 1px", 1))):
    bx = 520 + k * 210
    T.rect(bx - 2, 8, bx + 182, 132, BLACK)
    for yy in range(120):
        for xx in range(180): T.set(bx + xx, 10 + yy, tex(xx, yy, ts))
    draw_text(T, bx, 140, t, WHITE, 3, True, outline=BLACK)
C.paste(T, pad, y2 + 36)
C.save(O + "crop_1to1.png")
print("ok")
