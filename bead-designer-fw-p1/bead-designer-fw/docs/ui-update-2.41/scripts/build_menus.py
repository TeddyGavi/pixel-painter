import sys
from screens import *
import os
O = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "mockups") + "/"

def base():
    img = draw_screen(12, 'A', False, 0); dim_all(img, 0.38); return img

def main_menu():
    img = base()
    rows = [("Pattern", "unsaved", 'sub'), ("Brand", "Artkal", 'sub'), ("Edit shortlist", "", 'sub'),
            ("Colours used", "5", 'sub'), ("Board", "Solid", 'val'), ("Texture", "1/8", 'tex'),
            ("Beads", "Bevel", 'val'), ("Screensaver", "1m", 'val'), ("Export", "", 'sub')]
    x, y, w = 12, 12, 426; h = TITLE_H + 1 + len(rows) * ROW_H + 10
    panel(img, x, y, w, h, "Menu")
    menu_rows(img, x, y + TITLE_H + 6, w, rows, 0)
    return img

def pattern_panel(img, sel, dimmed=False):
    rows = [("Save...", "", 'val'), ("Load...", "", 'val'), ("Grid size", "12x12", 'sub'), ("Reset board...", "", 'val')]
    x, y, w = 12, 12, 426; h = TITLE_H + 1 + len(rows) * ROW_H + 10
    panel(img, x, y, w, h, "Pattern")
    menu_rows(img, x, y + TITLE_H + 6, w, rows, sel)
    if dimmed: img.scale_rgb(0.45, x - 2, y - 2, x + w + 2, y + h + 12)
    return h

def menu_grid():
    img = base()
    pattern_panel(img, 2, dimmed=True)
    g12 = pattern(12)
    def crop(g, n):    # resize centred: offset floor((n-12)/2)
        o = (n - 12) // 2 if n > 12 else (12 - n) // 2
        if n >= 12:
            out = [['.'] * n for _ in range(n)]
            for y in range(12):
                for x in range(12): out[y + o][x + o] = g[y][x]
            return out, 0
        lost = sum(1 for y in range(12) for x in range(12) if g[y][x] != '.' and not (o <= x < o + n and o <= y < o + n))
        return [row[o:o + n] for row in g[o:o + n]], lost
    x, y, w = 30, 196, 408; rh = 104
    h = TITLE_H + 1 + 3 * rh + 10
    panel(img, x, y, w, h, "Grid size")
    ry0 = y + TITLE_H + 6
    for i, n in enumerate((9, 12, 15)):
        ry = ry0 + i * rh
        if i == 2: sel_frame(img, x + 6, ry + 2, w - 12, rh - 4)
        gg, lost = crop(g12, n)
        px = {9: 9, 12: 7, 15: 6}[n]
        tx = x + 22 + (90 - n * px) // 2; ty = ry + (rh - n * px) // 2
        thumb(img, tx, ty, gg, px, border=WHITE if i != 2 else WHITE)
        lx = x + 136
        draw_text(img, lx, ry + 10, f"{n} x {n}", WHITE)
        sub = {9: f"cuts {lost} beads", 12: "current", 15: "more room"}[n]
        if n == 9:
            # caution: warn in dim text + a small '!' box
            draw_text(img, lx, ry + 52, sub, (0xFF, 0xC8, 0x40))
        else:
            draw_text(img, lx, ry + 52, sub, TXT_DIM)
        if n == 12: check(img, x + w - 52, ry + 34, WHITE)
    return img

def caution_icon(img, x, y, s=54):
    # System 7 caution: triangle outline with "!" (1-bit), white on black
    for j in range(s):
        half = j * 0.5 * (s / s)
        cx = x + s / 2
        x0, x1 = int(round(cx - half * 1.08)), int(round(cx + half * 1.08))
        for xx in range(x0, x1 + 1):
            edge = j >= s - 3 or xx <= x0 + 2 or xx >= x1 - 2
            img.set(xx, y + j, WHITE if edge else BLACK)
    draw_text(img, x + s // 2 - 6, y + 14, "!", WHITE, 3, True)

def dialog():
    img = base()
    # Save list behind (dimmed)
    x, y, w = 12, 12, 426; rows = 4; h = TITLE_H + 1 + rows * 2 * ROW_H + 10
    panel(img, x, y, w, h, "Save")
    ry0 = y + TITLE_H + 6
    g12 = pattern(12)
    import copy
    other = [['.'] * 12 for _ in range(12)]
    for (xx, yy) in [(5, 3), (6, 3), (4, 4), (5, 4), (6, 4), (7, 4), (5, 5), (6, 5), (5, 6), (6, 6), (4, 7), (7, 7)]:
        other[yy][xx] = 'Y'
    for yy in range(12): other[11][yy] = 'B'
    slots = [("1  Artkal", "23 beads", other), ("2  Artkal", "37 beads", g12), ("3  Empty", "", None), ("4  Empty", "", None)]
    for i, (a, b, g) in enumerate(slots):
        ry = ry0 + i * 2 * ROW_H
        if i == 0: sel_frame(img, x + 6, ry + 2, w - 12, 2 * ROW_H - 4)
        if g: thumb(img, x + 22, ry + (2 * ROW_H - 84) // 2, g, 7)
        else: thumb(img, x + 22, ry + (2 * ROW_H - 84) // 2, [['.'] * 12 for _ in range(12)], 7, border=DIVIDER)
        draw_text(img, x + 130, ry + 12, a, WHITE)
        if b: draw_text(img, x + 130, ry + 58, b, TXT_DIM)
    img.scale_rgb(0.4)
    # the alert
    dw, dh = 404, 316; dx, dy = (W - dw) // 2, 142
    shadow(img, dx, dy, dw, dh, PANEL_R, 0, 10, 8, 0.9)
    fill_rrect(img, dx, dy, dw, dh, PANEL_R, BLACK)
    ring_rrect(img, dx, dy, dw, dh, PANEL_R, 1, WHITE)
    ring_rrect(img, dx + 4, dy + 4, dw - 8, dh - 8, PANEL_R - 2, 2, gray(0x80))   # System 7 alert double frame
    caution_icon(img, dx + 26, dy + 28)
    draw_text(img, dx + 100, dy + 26, "Replace slot 1?", WHITE)
    draw_text(img, dx + 100, dy + 68, "23 beads lost", TXT_DIM)
    # old -> new thumbnails
    thumb(img, dx + 100, dy + 124, other, 6)
    draw_text(img, dx + 190, dy + 140, ">", TXT_DIM)
    thumb(img, dx + 222, dy + 124, g12, 6)
    # buttons: Cancel = default (heavy ring = what Press does)
    bw, bh, by = 160, 64, dy + dh - 88
    bx1, bx2 = dx + 28, dx + dw - 28 - bw
    for bx, label, default in ((bx1, "Cancel", True), (bx2, "Replace", False)):
        if default:
            ring_rrect(img, bx - 6, by - 6, bw + 12, bh + 12, 16, 3, WHITE)
        ring_rrect(img, bx, by, bw, bh, 11, 1, WHITE)
        draw_text(img, bx + (bw - text_w(label)) // 2, by + (bh - 39) // 2 + 1, label, WHITE)
    return img

jobs = {'menu.png': main_menu, 'menu_grid.png': menu_grid, 'dialog.png': dialog}
for k in (sys.argv[1:] or jobs):
    jobs[k]().save(O + k); print(k)
