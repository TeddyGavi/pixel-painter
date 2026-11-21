import gfx
from gfx import Img, gray, lighten, darken, dim, mix, fill_rrect, ring_rrect, shadow, draw_text, text_w, in_rrect

W, H = 450, 600
WHITE, BLACK = (255, 255, 255), (0, 0, 0)
TXT_DIM = (0xAA, 0xAA, 0xAA)
DIVIDER = (0x38, 0x38, 0x38)

# Artkal S colours (palette_artkal_s.c)
COL = {'R': ("S05", "Red", (225, 6, 0)), 'K': ("S13", "Black", (0, 0, 0)),
       'W': ("S01", "White", (255, 255, 255)), 'Y': ("S27", "Yellow", (255, 209, 0)),
       'B': ("S24", "Sky", (20, 123, 209))}
SLOTS = ['R', 'K', 'W', 'Y', 'B']

# ---- texture A (1 px checker in the 8x8 table), drawn at 2 panel px (today's size)
TEX_A = [0x55, 0xAA] * 4
def tex(x, y, tscale=2):
    lx, ly = x // tscale, y // tscale
    return gray(0x60) if (TEX_A[ly & 7] >> (7 - (lx & 7))) & 1 else gray(0x30)

# ---- grid geometry per size (Solid variant). cell, gap, peg, bead corner r, shadow
GEOM = {9: dict(cell=43, gap=6, peg=8, r=3, sh=(2, 2, 1)),
        12: dict(cell=32, gap=5, peg=6, r=2, sh=(2, 2, 1)),
        15: dict(cell=25, gap=4, peg=5, r=2, sh=(1, 2, 0))}
def layout(n):
    g = GEOM[n]; c, gp = g['cell'], g['gap']
    used = n * c + (n + 1) * gp
    ox = (W - used) // 2 + gp
    return c, gp, ox, ox

# ---- sample patterns
HEART = ["..KK...KK..".replace('.', '.'),]
HEART9 = [".........",
          ".KK...KK.",
          "KRRK.KRRK",
          "KRWRKRRRK",
          "KRRRRRRRK",
          ".KRRRRRK.",
          "..KRRR...",
          "...KR....",
          "........."]
def pattern(n):
    g = [['.'] * n for _ in range(n)]
    off = {9: 0, 12: 1, 15: 3}[n]
    for y, row in enumerate(HEART9):
        for x, ch in enumerate(row):
            if ch != '.': g[y + off][x + off] = ch
    extra = {9: [], 12: [(10, 1, 'Y'), (11, 0, 'Y'), (10, 0, '.'), (0, 11, 'B'), (1, 11, 'B'), (2, 11, 'B'), (0, 10, 'B')],
             15: [(12, 1, 'Y'), (13, 1, 'Y'), (12, 0, 'Y'), (1, 13, 'B'), (2, 13, 'B'), (3, 13, 'B'), (4, 13, 'B'),
                  (5, 13, 'B'), (1, 14, 'B'), (2, 14, 'B'), (3, 14, 'B'), (4, 14, 'B'), (5, 14, 'B'), (6, 14, 'B'),
                  (6, 13, 'B'), (1, 1, 'W'), (2, 2, 'W')]}[n]
    for x, y, ch in extra: g[y][x] = ch
    return g
CURSOR = {9: (6, 6), 12: (7, 7), 15: (9, 9)}

# ---- beads / cells, by option
def bead(img, px, py, n, col, opt, r):
    """draw a bead whose cell top-left is (px,py), size n"""
    rr = r if opt in ('A', 'C') else 0
    bw = 2 if opt == 'A' else (3 if opt == 'C' else 0)
    for y in range(n):
        for x in range(n):
            if not in_rrect(x, y, 0, 0, n, n, rr): continue
            c = col
            if bw:
                db = min(n - 1 - x, n - 1 - y); dt = min(x, y)
                if db < bw and db <= dt: c = darken(col, 0.20 if db == 0 else (0.11 if db == 1 else 0.05))
                elif dt < bw: c = lighten(col, 0.20 if dt == 0 else (0.11 if dt == 1 else 0.05))
            img.set(px + x, py + y, c)
    if opt in ('A', 'C'):   # sheen: 3 stepped light bands in the top-left corner (retro, cacheable)
        lim = n * 0.62
        for y in range(bw, n):
            for x in range(bw, n):
                d = (x + y) / lim
                if d >= 1 or not in_rrect(x, y, 0, 0, n, n, rr): continue
                f = 0.16 if d < 0.45 else (0.09 if d < 0.75 else 0.04)
                img.set(px + x, py + y, lighten(img.get(px + x, py + y), f))

def empty_cell(img, px, py, n, peg, opt, r):
    rr = r if opt == 'A' else 0
    c0 = (n - peg) // 2
    for y in range(n):
        for x in range(n):
            if not in_rrect(x, y, 0, 0, n, n, rr): continue
            X, Y = px + x, py + y
            c = gray(0x58) if (X + Y) & 1 else gray(0x50)
            if c0 <= x < c0 + peg and c0 <= y < c0 + peg: c = gray(0x78)
            if opt == 'C':      # recessed slot: shade top/left, catch light bottom/right
                dt, db = min(x, y), min(n - 1 - x, n - 1 - y)
                if dt < 3 and dt <= db: c = darken(c, (0.30, 0.20, 0.10)[dt])
                elif db == 0: c = lighten(c, 0.12)
            img.set(X, Y, c)

def cursor_ring(img, px, py, n, col, halo=True):
    # 1 px black at 1 px outside the cell, 2 px white ring at 2..3 px outside
    if halo:
        shadow(img, px - 3, py - 3, n + 6, n + 6, 3, 0, 1, 2, 0.6, exclude=(px - 3, py - 3, px + n + 3, py + n + 3))
    for t, c in ((1, BLACK), (2, col), (3, col)):
        x0, y0, s = px - t, py - t, n + 2 * t
        for i in range(s):
            img.set(x0 + i, y0, c); img.set(x0 + i, y0 + s - 1, c)
            img.set(x0, y0 + i, c); img.set(x0 + s - 1, y0 + i, c)

# ---- strip + name line
CHIP, PITCH, CHIP_R = 60, 84, 5
STRIP_CY = 497
NAME_Y = 549
def strip(img, sel, focus_palette, opt, lift=4):
    x0 = (W - 5 * PITCH) // 2 + (PITCH - CHIP) // 2
    fc = (lambda c: c) if focus_palette else dim
    for k, s in enumerate(SLOTS):
        col = fc(COL[s][2])
        x = x0 + k * PITCH; y = STRIP_CY - CHIP // 2
        if k == sel:
            y -= lift
            shadow(img, x, y, CHIP, CHIP, CHIP_R, 0, lift + 2, 4, 0.75)
        else:
            shadow(img, x, y, CHIP, CHIP, CHIP_R, 0, 2, 2, 0.55)
        fill_rrect(img, x - 1, y - 1, CHIP + 2, CHIP + 2, CHIP_R + 1, BLACK)   # 1 px black edge (agreed)
        if opt == 'B':
            fill_rrect(img, x, y, CHIP, CHIP, CHIP_R, col)
        else:
            tmp = Img(CHIP, CHIP)
            for yy in range(CHIP):
                for xx in range(CHIP): tmp.set(xx, yy, img.get(x + xx, y + yy))
            bead(tmp, 0, 0, CHIP, col, 'A', CHIP_R)
            for yy in range(CHIP):
                for xx in range(CHIP):
                    if in_rrect(xx, yy, 0, 0, CHIP, CHIP, CHIP_R): img.set(x + xx, y + yy, tmp.get(xx, yy))
        if k == sel:
            ring = fc(WHITE) if focus_palette else dim(WHITE)
            for t in (2, 3):
                ring_rrect(img, x - 1 - t, y - 1 - t, CHIP + 2 + 2*t, CHIP + 2 + 2*t, CHIP_R + 1 + t, 1, ring)
    return x0

def name_line(img, code, name, count, focus_palette):
    ink = WHITE if focus_palette else dim(WHITE)
    cnt = TXT_DIM if focus_palette else dim(TXT_DIM)
    s = f"{code} {name}"
    draw_text(img, 16, NAME_Y, s, ink, 3, True, outline=BLACK, drop=(1, 2, 0.6))
    cs = str(count)
    draw_text(img, W - 16 - text_w(cs), NAME_Y, cs, cnt, 3, True, outline=BLACK, drop=(1, 2, 0.6))

def draw_screen(n, opt='A', focus_palette=False, sel=0, cursor=None, grid=None):
    img = Img(W, H)
    for y in range(H):
        for x in range(W): img.set(x, y, tex(x, y))
    c, gp, ox, oy = layout(n)
    g = GEOM[n]; grid = grid or pattern(n); cur = cursor or CURSOR[n]
    pitch = c + gp
    # cast shadows first (A only), clipped to the cell's own tile (cell + half gaps)
    if opt == 'A':
        dx, dy, bl = g['sh']
        for y in range(n):
            for x in range(n):
                if grid[y][x] == '.': continue
                px, py = ox + x * pitch, oy + y * pitch; h = gp // 2
                shadow(img, px, py, c, c, g['r'], dx, dy, bl, 0.85,
                       clip=(px - h, py - h, px + c + (gp - h), py + c + (gp - h)))
    for y in range(n):
        for x in range(n):
            px, py = ox + x * pitch, oy + y * pitch
            ch = grid[y][x]
            if ch == '.': empty_cell(img, px, py, c, g['peg'], opt, g['r'])
            else: bead(img, px, py, c, COL[ch][2], opt, g['r'])
    cx, cy = cur
    cursor_ring(img, ox + cx * pitch, oy + cy * pitch, c, dim(WHITE) if focus_palette else WHITE, halo=(opt != 'B'))
    strip(img, sel, focus_palette, opt)
    k = SLOTS[sel]
    count = sum(row.count(k) for row in grid)
    name_line(img, COL[k][0], COL[k][1], count, focus_palette)
    return img

# ---- panels (menus, dialogs) -------------------------------------------------------
PANEL_R = 6
TITLE_H = 52
ROW_H = 56

def dim_all(img, f=0.38):
    img.scale_rgb(f)

def stripes(img, x0, y0, x1, y1, col=(0x58, 0x58, 0x58)):
    # System 7 title-bar stripes: 2 px on / 2 px off
    for y in range(y0, y1):
        if (y - y0) % 4 < 2:
            for x in range(x0, x1): img.set(x, y, col)

def close_box(img, x, y, s=26, col=WHITE):
    img.rect(x - 3, y - 3, x + s + 3, y + s + 3, BLACK)
    ring_rrect(img, x, y, s, s, 0, 1, col)

def panel(img, x, y, w, h, title, close=True, shadow_alpha=0.85):
    shadow(img, x, y, w, h, PANEL_R, 0, 8, 6, shadow_alpha)
    fill_rrect(img, x, y, w, h, PANEL_R, BLACK)
    ring_rrect(img, x, y, w, h, PANEL_R, 1, WHITE)
    # title bar
    tw = text_w(title)
    stripes(img, x + 8, y + 12, x + w - 8, y + TITLE_H - 12)
    tx = x + (w - tw) // 2
    img.rect(tx - 12, y + 6, tx + tw + 12, y + TITLE_H - 6, BLACK)
    draw_text(img, tx, y + 8, title, WHITE)
    if close: close_box(img, x + 18, y + (TITLE_H - 26) // 2)
    img.rect(x + 1, y + TITLE_H, x + w - 1, y + TITLE_H + 1, gray(0x80))

def tri(img, x, y, col):   # hierarchical-menu triangle, 8 x 15
    for i in range(8):
        for j in range(i, 15 - i): img.set(x + i, y + j, col)

def check(img, x, y, col):  # Mac menu check mark, 7x9 bitmap at 3x with scale3x
    bm = ["......X", ".....XX", ".....X.", "X...XX.", "XX..X..", ".XXXX..", "..XX...", "..X....", "......."]
    m = gfx.scale3x([[1 if ch == 'X' else 0 for ch in r] for r in bm])
    for yy, r in enumerate(m):
        for xx, v in enumerate(r):
            if v: img.set(x + xx, y + yy, col)

def sel_frame(img, x, y, w, h, col=WHITE):
    ring_rrect(img, x, y, w, h, 6, 1, col)

def menu_rows(img, x, y, w, rows, sel, row_h=ROW_H):
    for i, (left, right, kind) in enumerate(rows):
        ry = y + i * row_h
        if i == sel: sel_frame(img, x + 6, ry + 2, w - 12, row_h - 4)
        draw_text(img, x + 22, ry + (row_h - 39) // 2 + 1, left, WHITE)
        rx = x + w - 22
        if kind == 'sub':
            tri(img, rx - 8, ry + (row_h - 15) // 2, TXT_DIM); rx -= 24
        if right:
            if kind == 'tex':
                draw_text(img, rx - text_w(right), ry + (row_h - 39) // 2 + 1, right, TXT_DIM)
                sx = rx - text_w(right) - 38
                for yy in range(26):
                    for xx in range(26): img.set(sx + xx, ry + 15 + yy, tex(xx, yy))
                ring_rrect(img, sx - 1, ry + 14, 28, 28, 0, 1, gray(0x80))
            else:
                draw_text(img, rx - text_w(right), ry + (row_h - 39) // 2 + 1, right, TXT_DIM)

def thumb(img, x, y, grid, px, border=WHITE):
    n = len(grid); s = n * px
    img.rect(x - 2, y - 2, x + s + 2, y + s + 2, border)
    img.rect(x - 1, y - 1, x + s + 1, y + s + 1, BLACK)
    for gy in range(n):
        for gx in range(n):
            ch = grid[gy][gx]
            c = gray(0x24) if ch == '.' else COL[ch][2]
            if ch == 'K': c = gray(0x10)
            img.rect(x + gx * px, y + gy * px, x + (gx + 1) * px, y + (gy + 1) * px, c)
