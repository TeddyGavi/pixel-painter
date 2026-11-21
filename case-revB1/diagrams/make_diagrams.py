# Simple 2D diagrams of the rev B5 stack (dims from bead_case.scad, 2026-11-20)
import sys, os
OUT = sys.argv[1]
C = dict(shellF="#e3a165", shellB="#9a9aa6", glass="#9cc8f0", disp="#333", pcb="#2f8f4a",
         parts="#777", j8="#222", hdr="#555", adp="#1f6f8f", fh12="#d9d9d9", ffc="#c58b00",
         ctrl="#2f8f4a", batt="#4a64c8", boss="#d08a3a", standoff="#c9a54a", text="#111")
FONT = 'font-family="DejaVu Sans, Arial" '

class Svg:
    def __init__(s, w, h): s.w, s.h, s.el = w, h, []
    def rect(s, x0, y0, x1, y1, fill, stroke="#000", sw=0.8, op=1, dash=None):
        x, y = min(x0, x1), min(y0, y1)
        d = f' stroke-dasharray="{dash}"' if dash else ""
        s.el.append(f'<rect x="{x:.1f}" y="{y:.1f}" width="{abs(x1-x0):.1f}" height="{abs(y1-y0):.1f}" fill="{fill}" fill-opacity="{op}" stroke="{stroke}" stroke-width="{sw}"{d}/>')
    def line(s, pts, col="#000", sw=1, dash=None, marker=False):
        d = f' stroke-dasharray="{dash}"' if dash else ""
        m = ' marker-end="url(#arr)"' if marker else ""
        p = " ".join(f"{x:.1f},{y:.1f}" for x, y in pts)
        s.el.append(f'<polyline points="{p}" fill="none" stroke="{col}" stroke-width="{sw}" stroke-linejoin="round" stroke-linecap="round"{d}{m}/>')
    def path(s, d, col, sw, fill="none"):
        s.el.append(f'<path d="{d}" fill="{fill}" stroke="{col}" stroke-width="{sw}" stroke-linecap="round"/>')
    def circ(s, x, y, r, fill, stroke="#000", sw=0.8, dash=None):
        d = f' stroke-dasharray="{dash}"' if dash else ""
        s.el.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="{r:.1f}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}"{d}/>')
    def text(s, x, y, t, size=13, anchor="start", bold=False, col="#111"):
        b = ' font-weight="bold"' if bold else ""
        for i, ln in enumerate(t.split("\n")):
            s.el.append(f'<text x="{x:.1f}" y="{y + i*size*1.2:.1f}" {FONT}font-size="{size}" text-anchor="{anchor}" fill="{col}"{b}>{ln}</text>')
    def label(s, px, py, tx, ty, t, size=13, anchor="start"):
        s.line([(px, py), (tx, ty)], "#444", 0.8)
        s.circ(px, py, 2, "#444", "none")
        s.text(tx + (4 if anchor == "start" else -4 if anchor == "end" else 0), ty + 4, t, size, anchor)
    def badge(s, x, y, n):
        s.circ(x, y, 11, "white", "#c00", 2)
        s.text(x, y + 5, str(n), 13, "middle", bold=True, col="#c00")
    def key(s, x, y, items, size=13, colw=None, rows=None):
        for i, t in enumerate(items):
            cx = x + (i // rows) * colw if rows else x
            cy = y + (i % rows if rows else i) * (size * 1.9)
            s.badge(cx + 11, cy - 4, i + 1)
            s.text(cx + 28, cy + 1, t, size)
    def save(s, name):
        hdr = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{s.w}" height="{s.h}" viewBox="0 0 {s.w} {s.h}">'
               '<defs><marker id="arr" markerWidth="10" markerHeight="8" refX="9" refY="4" orient="auto">'
               '<path d="M0,0 L10,4 L0,8 z" fill="#c00"/></marker></defs>'
               f'<rect width="{s.w}" height="{s.h}" fill="white"/>')
        open(os.path.join(OUT, name), "w").write(hdr + "\n".join(s.el) + "</svg>")

# ---------------- model numbers ----------------
lens = (-5.28, 56.28); wsY = (-3.35, 54.35); j8Y = (5.18 - 1.27, 5.18 + 16 * 2.54 + 1.27)
adpY = (-4.6, 47.5); cpY = (-48, -7); caseY = (-50, 60.3); cavY = (-48.8, 58.3)
J2Y = (-4.4, 1.0); J1Y = (-13.4, -7.0)
# FFC centreline (Y, Z): J1 slot -> run +Y at Z10.6 -> U up -> run -Y at Z12.3 -> U up -> run +Y at Z16.1 into J2
ffc = [(-7.6, 10.6), (-1.1, 10.6), "U", (-1.1, 12.3), (-7.9, 12.3), "U", (-7.9, 16.1), (-4.0, 16.1)]

def section(sv, Y0, Y1, Zmax, k, ox, oy, zoom):
    X = lambda y: ox + (y - Y0) * k
    Z = lambda z: oy + z * k
    clipY = lambda a, b: (max(a, Y0), min(b, Y1))
    def R(ya, yb, za, zb, fill, **kw):
        a, b = clipY(ya, yb)
        if a < b: sv.rect(X(a), Z(za), X(b), Z(zb), fill, **kw)
    # shells (simplified): front panel Z0..2 outside the glass, side walls, back shell
    R(caseY[0], lens[0], 0, 2, C["shellF"]); R(lens[1], caseY[1], 0, 2, C["shellF"])
    R(caseY[0], cavY[0], 0, 13, C["shellF"]); R(cavY[1], caseY[1], 0, 13, C["shellF"])
    R(caseY[0], cavY[0], 13, 21.5, C["shellB"]); R(cavY[1], caseY[1], 13, 21.5, C["shellB"])
    R(caseY[0], caseY[1], 20.3, 21.5, C["shellB"])
    # adapter ribs (in the back shell, beside the cut)
    for a, b in ((24, 25.5), (45.8, 47.2)): R(a, b, 15.8, 20.3, C["shellB"], dash="3,2", op=0.5)
    # Waveshare
    R(*lens, 0, 0.7, C["glass"]); R(*wsY, 0.7, 4.8, C["disp"]); R(*wsY, 4.8, 6.0, C["pcb"])
    R(wsY[0] + 3, 30, 6.0, 9.5, C["parts"], op=0.35, dash="3,2")
    # standoffs + bosses at X 36.5 (2.5 beside the cut)
    for y, screw in ((0, False), (51, True)):
        R(y - 1.75, y + 1.75, 6.0, 9.5, C["standoff"])
        R(y - 2.15, y + 2.15, 9.5, 9.7, C["boss"])
        R(y - 3.25, y + 3.25, 9.7, 11.5 if screw else 10.3, C["boss"])
        if screw: R(y - 1.9, y + 1.9, 11.5, 13.1, "#888")
    # J8 + header + adapter + J2
    R(*j8Y, 6.0, 11.5, C["j8"]); R(*j8Y, 11.5, 14.0, C["hdr"]); R(*adpY, 14.0, 15.6, C["adp"])
    R(*J2Y, 15.6, 17.6, C["fh12"])
    # control PCB + J1
    R(*cpY, 8.5, 10.1, C["ctrl"]); R(*J1Y, 10.1, 12.1, C["fh12"])
    # FFC
    w = max(2.5, 0.3 * k)
    d = ""; prev = None
    for i, p in enumerate(ffc):
        if p == "U":
            continue
        if prev is None: d += f"M{X(p[0]):.1f},{Z(p[1]):.1f} "
        elif ffc[i - 1] == "U":
            r = (p[1] - prev[1]) / 2 * k
            sweep = 1 if p[0] > ffc[i - 3][0] else 0
            sweep = 1 if prev[0] > ffc[i-3][0] else 0
            d += f"A{r:.1f},{r:.1f} 0 0 {sweep} {X(p[0]):.1f},{Z(p[1]):.1f} "
        else: d += f"L{X(p[0]):.1f},{Z(p[1]):.1f} "
        prev = p
    sv.path(d, C["ffc"], w)
    return X, Z

# ================= Figure 1: side section =================
k1 = 9; Y0, Y1 = -52, 62
sv = Svg(1180, 530)
sv.text(20, 32, "1 · Side section through the cable (cut at X 34, looking from the right side)", 20, bold=True)
sv.text(20, 56, "Front (glass) at the top, back of the case at the bottom. Scale ≈ 9 px per mm. Top of the device is on the right.", 13)
X, Z = section(sv, Y0, Y1, 21.5, k1, 40, 110, False)
pts1 = [(X(30), Z(0.35)), (X(20), Z(2.7)), (X(48), Z(5.4)), (X(25), Z(8.7)), (X(25), Z(12.75)), (X(25), Z(14.8)),
        (X(51), Z(10.6)), (X(0), Z(9.0)), (X(-30), Z(9.3)), (X(-10.2), Z(11.1)), (X(-1.7), Z(16.6)), (X(-3.5), Z(12.3)), (X(46.5), Z(18.0))]
for i, (px, py) in enumerate(pts1): sv.badge(px, py, i + 1)
sv.key(40, Z(21.5) + 40, ["glass, flush with the front face", "Waveshare display", "Waveshare PCB",
    "J8 female socket (on the Waveshare)", "2x17 male header (you solder it to the adapter)", "J8 adapter PCB",
    "screw boss + M2x4 screw (3 corners)", "support pad, no screw (sits under the FFC)", "control PCB rev B5",
    "J1 FH12 on the control PCB back", "J2 FH12 on the adapter back", "30 mm FFC, Z-folded (zoom in figure 2)",
    "ribs in the back shell press the adapter into J8"], 13, 380, 5)
# legend
lx, ly = 40, 495
for i, (n, c) in enumerate([("front shell", C["shellF"]), ("back shell", C["shellB"]), ("PCBs", C["pcb"]),
                            ("adapter PCB", C["adp"]), ("FH12 connector", C["fh12"]), ("FFC cable", C["ffc"]), ("standoff / boss", C["boss"])]):
    x = lx + i * 150
    sv.rect(x, ly, x + 16, ly + 12, c); sv.text(x + 22, ly + 11, n, 12)
sv.save("1-side-section.svg")

# ================= Figure 2: zoom on the fold =================
k2 = 38; Y0, Y1 = -16, 8
sv = Svg(1100, 1260)
sv.text(20, 32, "2 · Zoom: how the FFC folds (same cut, 4x larger)", 20, bold=True)
sv.text(20, 56, "The cable never twists. It makes an S (Z-fold) between the two connectors, whose slots face each other.", 13)
X, Z = section(sv, Y0, Y1, 21.5, k2, 170, 100, True)
pts2 = [(X(-10.2), Z(11.4)), (X(-4.5), Z(10.6)), (X(-1.1), Z(11.45)), (X(-4.5), Z(12.3)), (X(-7.9), Z(14.2)),
        (X(-6.0), Z(16.1)), (X(-1.7), Z(16.9)), (X(4), Z(14.8)), (X(6.5), Z(12.75)), (X(6.5), Z(8.7)), (X(0), Z(10.0)), (X(-12), Z(9.3)), (X(4), Z(5.4))]
for i, (px, py) in enumerate(pts2): sv.badge(px, py, i + 1)
sv.key(40, Z(21.5) + 50, ["J1 on the control PCB: plug the cable in here FIRST", "run 1: leaves J1 toward the top of the device",
    "bend 1: U-turn (gentle curve, don't crease it)", "run 2: back toward the bottom", "bend 2: U-turn",
    "run 3: into J2 (contacts face the adapter)", "J2 on the adapter: plug in second", "adapter PCB",
    "2x17 male header", "J8 female socket", "support pad (no screw)", "control PCB", "Waveshare PCB"], 14, 520, 7)
for z, t in ((0, "Z 0 front face"), (6, "Z 6 Waveshare rear"), (10.1, "Z 10.1"), (15.6, "Z 15.6 adapter back"), (20.3, "Z 20.3 back wall")):
    sv.line([(X(Y0), Z(z)), (X(Y0) - 10, Z(z))], "#999"); sv.text(X(Y0) - 12, Z(z) + 4, t, 11, "end")
sv.save("2-ffc-fold-zoom.svg")

# ================= Figure 3: plan view from the back =================
k3 = 7.5
sv = Svg(800, 1200)
sv.text(20, 32, "3 · Looking into the front shell from the back (back shell off)", 20, bold=True)
sv.text(20, 56, "Left/right are mirrored compared to looking at the screen. USB-C is on the right in this view.", 13)
ox, oy = 110 + 45.25 * k3, 90 + 60.3 * k3
PX = lambda x: ox - x * k3          # mirrored: looking from the back
PY = lambda y: oy - y * k3
def RR(x0, x1, y0, y1, fill, **kw): sv.rect(PX(x0), PY(y0), PX(x1), PY(y1), fill, **kw)
RR(-8.75, 45.25, -50, 60.3, C["shellF"]); RR(-7.55, 44.05, -48.8, 58.3, "#f6e3cf")
RR(-3.1, 39.6, -3.35, 54.35, C["pcb"], op=0.9)
RR(4, 24, 0, 50, C["batt"], op=0.85)
sv.text(PX(14), PY(27), "battery", 14, "middle", col="white"); sv.text(PX(14), PY(24), "(taped on)", 12, "middle", col="white")
RR(-6.5, 43, -48, -7, C["ctrl"], op=0.9)
RR(26.5, 41.5, -4.6, 47.5, C["adp"], op=0.9)
for cx in (32.73, 35.27):
    for r in range(17): sv.circ(PX(cx), PY(5.18 + r * 2.54), 2.2, "#ddd", "#000", 0.4)
for a, b in ((24, 25.5), (45.8, 47.2)):
    for x0, x1 in ((27, 31), (37, 41)): RR(x0, x1, a, b, C["shellB"], dash="2,2")
RR(27.5, 40.5, -13.4, -7, C["fh12"]); RR(28.6, 39.4, -4.4, 1.0, C["fh12"])
RR(29.75, 38.25, -7.9, -0.7, C["ffc"], op=0.9)
for (x, y), s in zip([(0, 0), (36.5, 0), (0, 51), (36.5, 51)], [True, False, True, True]):
    sv.circ(PX(x), PY(y), 3.25 * k3, C["boss"], "#000", 0.8)
    sv.circ(PX(x), PY(y), 1.75 * k3, C["standoff"] if not s else "#888", "#000", 0.8)
    if s: sv.line([(PX(x) - 8, PY(y)), (PX(x) + 8, PY(y))], "#222", 2)
for (x, y) in [(-4, -20), (40, -44.5), (18.25, -25)]: sv.circ(PX(x), PY(y), 1.6 * k3 / 1.2, "#bbb", "#000")
sv.circ(PX(31.5), PY(-34), 6 * k3 / 1.2, "none", "#ddd", 1.5, dash="4,3")
pts3 = [(PX(0), PY(51) - 26), (PX(36.5), PY(0) - 26), (PX(34), PY(36)), (PX(34), PY(45.8)), (PX(29), PY(24.75)),
        (PX(34), PY(-4)), (PX(34), PY(-11)), (PX(39.5), PY(-1.6)), (PX(14), PY(12))]
for i, (px, py) in enumerate(pts3): sv.badge(px, py, i + 1)
sv.key(40, PY(-50) + 60, ["M2x4 screw into the standoff (3 corners)", "support pad, no screw", "adapter, header pins down into J8",
    "J8 pin 1 end (GND/BAT) is at the TOP, by the keys", "ribs in the back shell press on the adapter", "FFC (Z-folded, on top of everything here)",
    "J1 on the control PCB", "J2 on the adapter back (under the FFC end)", "battery, taped to the Waveshare rear"], 13)
sv.text(PX(18), PY(-30), "control PCB", 14, "middle", col="white")
sv.text(PX(18), PY(-33), "(screws at 3 holes)", 12, "middle", col="white")
sv.text(PX(-1), PY(52.5) - 10, "", 12)
sv.text(PX(18), PY(60.3) - 8, "TOP of device (PWR / BOOT / RST keys)", 13, "middle")
sv.text(PX(18), PY(-50) + 18, "BOTTOM", 13, "middle")
sv.save("3-back-view.svg")

# ================= Figure 4: assembly order (cartoon) =================
sv = Svg(1180, 360)
sv.text(20, 32, "4 · Order of assembly (cable part)", 20, bold=True)
def panel(px, title, steps):
    sv.rect(px, 50, px + 270, 345, "#fafafa", "#bbb")
    sv.text(px + 10, 74, title, 15, bold=True)
    return px
s = 1.0
# frame A: control PCB + cable straight
p = panel(20, "A. Cable into J1", None)
sv.rect(p + 20, 190, p + 250, 205, C["ctrl"]); sv.rect(p + 150, 175, p + 200, 190, C["fh12"])
sv.line([(p + 195, 182), (p + 260, 182)], C["ffc"], 4)
sv.text(p + 10, 250, "Control PCB and Waveshare are\nalready screwed in.\nOpen the J1 latch, push the\ncable in, close the latch.\nThe free end points to the\ntop of the device.", 12)
# frame B: adapter on the cable end, held above
p = panel(310, "B. Other end into J2", None)
sv.rect(p + 20, 190, p + 160, 205, C["ctrl"]); sv.rect(p + 90, 175, p + 140, 190, C["fh12"])
sv.path(f"M{p+135},182 L{p+170},182 A40,40 0 0 0 {p+170},102 L{p+150},102", C["ffc"], 4)
sv.rect(p + 140, 80, p + 260, 92, C["adp"]); sv.rect(p + 150, 92, p + 195, 108, C["fh12"])
sv.text(p + 200, 120, "J2 faces\ndown", 11)
sv.text(p + 10, 250, "Hold the adapter above, J2\nside down, header pins away.\nPush the cable into J2 and\nclose its latch.\nCheck: no twist in the cable.", 12)
# frame C: fold + press into J8
p = panel(600, "C. Fold and press into J8", None)
sv.rect(p + 20, 190, p + 140, 205, C["ctrl"]); sv.rect(p + 80, 175, p + 130, 190, C["fh12"])
sv.path(f"M{p+125},182 L{p+165},182 A6,6 0 0 0 {p+165},170 L{p+115},170 A9,9 0 0 1 {p+115},152 L{p+150},152", C["ffc"], 4)
sv.rect(p + 145, 145, p + 175, 158, C["fh12"]); sv.rect(p + 130, 158, p + 260, 168, C["adp"])
sv.rect(p + 150, 168, p + 250, 178, C["hdr"]); sv.rect(p + 150, 110 + 68, p + 250, 120 + 75, C["j8"])
sv.line([(p + 200, 120), (p + 200, 140)], "#c00", 2, marker=True)
sv.text(p + 10, 250, "Flip the adapter over the\ncable so it lands header-down\non J8: the cable folds into\nan S by itself.\nPin 1 to the TOP (keys end).\nPress the adapter fully home.", 12)
# frame D: close
p = panel(890, "D. Battery, then close", None)
sv.rect(p + 20, 120, p + 250, 135, C["shellB"]); sv.rect(p + 40, 135, p + 60, 155, C["shellB"])
sv.rect(p + 150, 135, p + 170, 160, C["shellB"])
sv.rect(p + 120, 160, p + 230, 170, C["adp"]); sv.rect(p + 30, 150, p + 110, 200, C["batt"])
sv.line([(p + 135, 95), (p + 135, 115)], "#c00", 2, marker=True)
sv.text(p + 10, 250, "Battery taped on beside J8.\nBack shell on: its ribs\npress the adapter into J8.\n2x M2x12 at the top,\n2x M2x14 at H1/H2.", 12)
sv.save("4-assembly-order.svg")
