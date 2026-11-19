# Tiny raster lib for pixel-accurate mockups (no PIL). RGB float-free bytearray.
import re, zlib, struct

FONT_C = "/home/matt/pixel-painter/bead-designer-fw-p1/bead-designer-fw/main/font_chicago.c"

def load_font():
    src = open(FONT_C).read()
    gl = []
    for m in re.finditer(r"\{\s*(\d+),\s*\{([^}]*)\}\s*\}", src):
        adv = int(m.group(1))
        rows = [int(v, 16) for v in m.group(2).split(",")]
        gl.append((adv, rows))
    assert len(gl) == 95, len(gl)
    return gl
GLYPHS = load_font()
FH = 13

class Img:
    def __init__(s, w, h, col=(0, 0, 0)):
        s.w, s.h = w, h
        s.d = bytearray(bytes(col) * (w * h))
    def get(s, x, y):
        i = (y * s.w + x) * 3
        return (s.d[i], s.d[i + 1], s.d[i + 2])
    def set(s, x, y, c):
        if 0 <= x < s.w and 0 <= y < s.h:
            i = (y * s.w + x) * 3
            s.d[i], s.d[i + 1], s.d[i + 2] = c
    def blend(s, x, y, c, a):
        if a <= 0 or not (0 <= x < s.w and 0 <= y < s.h): return
        if a >= 1: s.set(x, y, c); return
        i = (y * s.w + x) * 3
        for k in range(3):
            s.d[i + k] = int(round(s.d[i + k] * (1 - a) + c[k] * a))
    def rect(s, x0, y0, x1, y1, c):
        for y in range(max(0, y0), min(s.h, y1)):
            for x in range(max(0, x0), min(s.w, x1)):
                s.set(x, y, c)
    def copy(s):
        o = Img(s.w, s.h); o.d = bytearray(s.d); return o
    def scale_rgb(s, f, x0=0, y0=0, x1=None, y1=None):
        x1 = s.w if x1 is None else x1; y1 = s.h if y1 is None else y1
        for y in range(max(0,y0), min(s.h,y1)):
            for x in range(max(0,x0), min(s.w,x1)):
                i = (y * s.w + x) * 3
                for k in range(3): s.d[i + k] = int(s.d[i + k] * f)
    def paste(s, o, ox, oy, sx=0, sy=0, w=None, h=None):
        w = o.w if w is None else w; h = o.h if h is None else h
        for y in range(h):
            for x in range(w):
                s.set(ox + x, oy + y, o.get(sx + x, sy + y))
    def save(s, path):
        raw = b"".join(b"\x00" + bytes(s.d[y * s.w * 3:(y + 1) * s.w * 3]) for y in range(s.h))
        def chunk(t, data):
            c = struct.pack(">I", len(data)) + t + data
            return c + struct.pack(">I", zlib.crc32(t + data) & 0xFFFFFFFF)
        png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", s.w, s.h, 8, 2, 0, 0, 0))
        png += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
        open(path, "wb").write(png)

def load_png(path):
    # minimal reader for 8-bit RGB/RGBA non-interlaced PNGs (sim output)
    data = open(path, "rb").read(); p = 8; idat = b""
    while p < len(data):
        n, = struct.unpack(">I", data[p:p+4]); t = data[p+4:p+8]; body = data[p+8:p+8+n]; p += 12 + n
        if t == b"IHDR": w, h, bd, ct = struct.unpack(">IIBB", body[:10])
        elif t == b"IDAT": idat += body
    bpp = {2: 3, 6: 4}[ct]; raw = zlib.decompress(idat); st = w * bpp
    img = Img(w, h); prev = bytearray(st); q = 0
    for y in range(h):
        f = raw[q]; line = bytearray(raw[q+1:q+1+st]); q += 1 + st
        for i in range(st):
            a = line[i-bpp] if i >= bpp else 0; b = prev[i]; c = prev[i-bpp] if i >= bpp else 0
            if f == 1: line[i] = (line[i] + a) & 255
            elif f == 2: line[i] = (line[i] + b) & 255
            elif f == 3: line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 255
        for x in range(w): img.set(x, y, tuple(line[x*bpp:x*bpp+3]))
        prev = line
    return img

def upscale(img, k):
    o = Img(img.w * k, img.h * k)
    for y in range(o.h):
        for x in range(o.w): o.set(x, y, img.get(x // k, y // k))
    return o

def downscale(img, k):  # box filter
    o = Img(img.w // k, img.h // k)
    for y in range(o.h):
        for x in range(o.w):
            acc = [0, 0, 0]
            for dy in range(k):
                for dx in range(k):
                    c = img.get(x*k+dx, y*k+dy)
                    for i in range(3): acc[i] += c[i]
            o.set(x, y, tuple(v // (k*k) for v in acc))
    return o

# ---- colour helpers ----
def mix(c, t, f): return tuple(int(round(c[i] + (t[i] - c[i]) * f)) for i in range(3))
def lighten(c, f): return mix(c, (255, 255, 255), f)
def darken(c, f): return mix(c, (0, 0, 0), f)
def dim(c): return tuple(v // 2 for v in c)
def gray(v): return (v, v, v)

# ---- shapes ----
def corner_in(i, j, r):
    # i, j = distance into the corner square from its outer edges
    if r <= 0 or i >= r or j >= r: return True
    return (r - i - 0.5) ** 2 + (r - j - 0.5) ** 2 <= r * r + 0.3

def in_rrect(x, y, x0, y0, w, h, r):
    if x < x0 or y < y0 or x >= x0 + w or y >= y0 + h: return False
    i = min(x - x0, x0 + w - 1 - x); j = min(y - y0, y0 + h - 1 - y)
    return corner_in(i, j, r)

def fill_rrect(img, x0, y0, w, h, r, c, a=1.0):
    for y in range(y0, y0 + h):
        for x in range(x0, x0 + w):
            if in_rrect(x, y, x0, y0, w, h, r): img.blend(x, y, c, a)

def ring_rrect(img, x0, y0, w, h, r, t, c):
    # ring of thickness t on the inside of the rounded rect
    for y in range(y0, y0 + h):
        for x in range(x0, x0 + w):
            if in_rrect(x, y, x0, y0, w, h, r) and not in_rrect(x, y, x0 + t, y0 + t, w - 2*t, h - 2*t, max(0, r - t)):
                img.set(x, y, c)

def box_blur(a, w, h, r, passes=3):
    if r <= 0: return a
    for _ in range(passes):
        b = [0.0] * (w * h)
        for y in range(h):
            row = a[y*w:(y+1)*w]; s = 0.0
            pre = [0.0]
            for v in row: s += v; pre.append(s)
            for x in range(w):
                lo, hi = max(0, x - r), min(w, x + r + 1)
                b[y*w + x] = (pre[hi] - pre[lo]) / (2*r + 1)
        c = [0.0] * (w * h)
        for x in range(w):
            s = 0.0; pre = [0.0]
            for y in range(h): s += b[y*w + x]; pre.append(s)
            for y in range(h):
                lo, hi = max(0, y - r), min(h, y + r + 1)
                c[y*w + x] = (pre[hi] - pre[lo]) / (2*r + 1)
        a = c
    return a

def shadow(img, x0, y0, w, h, r, dx, dy, blur, alpha, col=(0, 0, 0), clip=None, exclude=None):
    pad = blur * 3 + 2
    W, H = w + 2*pad, h + 2*pad
    a = [0.0] * (W * H)
    for y in range(h):
        for x in range(w):
            if in_rrect(x, y, 0, 0, w, h, r): a[(y+pad)*W + x+pad] = 1.0
    a = box_blur(a, W, H, blur // 1 if blur >= 1 else 0, 2) if blur else a
    for y in range(H):
        for x in range(W):
            v = a[y*W + x]
            if v <= 0.002: continue
            X, Y = x0 + dx + x - pad, y0 + dy + y - pad
            if clip and not (clip[0] <= X < clip[2] and clip[1] <= Y < clip[3]): continue
            if exclude and (exclude[0] <= X < exclude[2] and exclude[1] <= Y < exclude[3]): continue
            img.blend(X, Y, col, v * alpha)

# ---- Chicago text ----
def text_mask1(s):
    w = sum(GLYPHS[(ord(ch) if 32 <= ord(ch) <= 126 else 63) - 32][0] for ch in s)
    m = [[0] * w for _ in range(FH)]
    x = 0
    for ch in s:
        adv, rows = GLYPHS[(ord(ch) if 32 <= ord(ch) <= 126 else 63) - 32]
        for r in range(FH):
            for c in range(adv):
                if (rows[r] >> c) & 1: m[r][x + c] = 1
        x += adv
    return m

def scale_plain(m, k):
    return [[m[y // k][x // k] for x in range(len(m[0]) * k)] for y in range(len(m) * k)]

def _g(m, x, y):
    if 0 <= y < len(m) and 0 <= x < len(m[0]): return m[y][x]
    return 0

def scale2x(m):
    H, W = len(m), len(m[0]); o = [[0] * (W*2) for _ in range(H*2)]
    for y in range(H):
        for x in range(W):
            B, D, E, F, Hh = _g(m,x,y-1), _g(m,x-1,y), m[y][x], _g(m,x+1,y), _g(m,x,y+1)
            if B != Hh and D != F:
                e0 = D if D == B else E; e1 = F if B == F else E
                e2 = D if D == Hh else E; e3 = F if Hh == F else E
            else: e0 = e1 = e2 = e3 = E
            o[2*y][2*x], o[2*y][2*x+1], o[2*y+1][2*x], o[2*y+1][2*x+1] = e0, e1, e2, e3
    return o

def scale3x(m):
    H, W = len(m), len(m[0]); o = [[0] * (W*3) for _ in range(H*3)]
    for y in range(H):
        for x in range(W):
            A, B, C = _g(m,x-1,y-1), _g(m,x,y-1), _g(m,x+1,y-1)
            D, E, F = _g(m,x-1,y), m[y][x], _g(m,x+1,y)
            G, Hh, I = _g(m,x-1,y+1), _g(m,x,y+1), _g(m,x+1,y+1)
            if B != Hh and D != F:
                e = [D if D == B else E,
                     B if (D == B and E != C) or (B == F and E != A) else E,
                     F if B == F else E,
                     D if (D == B and E != G) or (D == Hh and E != A) else E,
                     E,
                     F if (B == F and E != I) or (Hh == F and E != C) else E,
                     D if D == Hh else E,
                     Hh if (D == Hh and E != I) or (Hh == F and E != G) else E,
                     F if Hh == F else E]
            else: e = [E] * 9
            for k in range(9): o[3*y + k // 3][3*x + k % 3] = e[k]
    return o

def text_mask(s, scale=3, smooth=True):
    m = text_mask1(s)
    if scale == 1: return m
    if smooth and scale == 3: return scale3x(m)
    if smooth and scale == 4: return scale2x(scale2x(m))
    if smooth and scale == 2: return scale2x(m)
    return scale_plain(m, scale)

def text_w(s, scale=3):
    return sum(GLYPHS[(ord(ch) if 32 <= ord(ch) <= 126 else 63) - 32][0] for ch in s) * scale

def draw_text(img, x, y, s, col, scale=3, smooth=True, outline=None, oth=1, drop=None):
    """outline: colour of an oth-px ring around ink; drop: (dx, dy, alpha) soft-ish shadow"""
    m = text_mask(s, scale, smooth)
    H, W = len(m), len(m[0]) if m else 0
    if drop:
        dx, dy, al = drop
        for yy in range(-oth, H + oth):
            for xx in range(-oth, W + oth):
                near = any(_g(m, xx+i, yy+j) for i in range(-oth, oth+1) for j in range(-oth, oth+1))
                if near: img.blend(x + xx + dx, y + yy + dy, (0, 0, 0), al)
    if outline is not None:
        for yy in range(-oth, H + oth):
            for xx in range(-oth, W + oth):
                if _g(m, xx, yy): continue
                if any(_g(m, xx+i, yy+j) for i in range(-oth, oth+1) for j in range(-oth, oth+1)):
                    img.set(x + xx, y + yy, outline)
    for yy in range(H):
        for xx in range(W):
            if m[yy][xx]: img.set(x + xx, y + yy, col)
    return W
