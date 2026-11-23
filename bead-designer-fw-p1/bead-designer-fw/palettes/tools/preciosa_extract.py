# Preciosa Ornela "Seed Beads - Rocailles" sample cards (Rocailles-Sample-Cards.pdf,
# pages 2-6): codes, ART numbers and section titles from the PDF text layer (exact, no
# OCR); colour sampled from each bead-strip photo on the page render.
# usage: preciosa_extract.py bbox.html renderdir out.json cropdir
#   renderdir holds hi-<page>.png at 400 dpi (pdftoppm -r 400 -f 2 -l 6 -png chart.pdf hi)
import sys, re, json, os, html
import numpy as np
from PIL import Image, ImageDraw
Image.MAX_IMAGE_PIXELS = None
bbox, rdir, out_json, cropdir = sys.argv[1:5]
os.makedirs(cropdir, exist_ok=True)
SC = 400 / 72.0
PAGES = range(2, 7)
COLX = [44.2, 172.7, 301.1, 429.5]            # column left edges (pt), from the ART headers


def lab(rgb):
    c = np.asarray(rgb, float) / 255.0
    c = np.where(c > 0.04045, ((c + 0.055) / 1.055) ** 2.4, c / 12.92)
    M = np.array([[0.4124, 0.3576, 0.1805], [0.2126, 0.7152, 0.0722], [0.0193, 0.1192, 0.9505]])
    xyz = c @ M.T / np.array([0.95047, 1.0, 1.08883])
    f = np.where(xyz > 0.008856, np.cbrt(xyz), 7.787 * xyz + 16 / 116)
    return np.stack([116 * f[..., 1] - 16, 500 * (f[..., 0] - f[..., 1]), 200 * (f[..., 1] - f[..., 2])], -1)


src = open(bbox).read()
pages = re.split(r"<page ", src)[1:]
res = []
for pno in PAGES:
    words = [(float(a), float(b), float(c), float(d), html.unescape(w)) for a, b, c, d, w in
             re.findall(r'xMin="([\d.]+)" yMin="([\d.]+)" xMax="([\d.]+)" yMax="([\d.]+)">([^<]+)<',
                        pages[pno - 1])]
    A = np.asarray(Image.open(os.path.join(rdir, f"hi-{pno}.png")).convert("RGB")).astype(float)
    col = lambda x: min(range(4), key=lambda k: abs(COLX[k] - x)) if min(abs(c - x) for c in COLX) < 6 else None
    # section headers: "ART No. a b c" lines; title = the words below it, above the first code
    heads = []
    for w in words:
        if w[4] == "ART" and col(w[0]) is not None:
            line = sorted([v for v in words if abs(v[1] - w[1]) < 1 and w[0] <= v[0] < w[0] + 110],
                          key=lambda v: v[0])
            art = " ".join(v[4] for v in line[2:5])
            heads.append({"col": col(w[0]), "y": w[1], "art": art})
    codes = [w for w in words if re.fullmatch(r"\d{5}\*?", w[4]) and col(w[0]) is not None]
    def title(c, hy):
        """words in column c below a header at hy and above the column's first code."""
        first = min((v[1] for v in codes if col(v[0]) == c and v[1] > hy), default=9e9)
        tl = sorted([v for v in words if COLX[c] - 1 <= v[0] < COLX[c] + 125 and hy + 5 < v[1] < first - 2
                     and not re.fullmatch(r"\d{5}\*?", v[4])], key=lambda v: (round(v[1]), v[0]))
        return " ".join(v[4] for v in tl)
    for x0, y0, x1, y1, code in codes:
        c = col(x0)
        # own column's ART header; page 6's "New arrivals 2010" block has one ART header
        # (column 0) over four columns, each with its own title line
        above = [g for g in heads if g["col"] == c and g["y"] < y0]
        allab = [g for g in heads if g["y"] < y0]
        if not allab: print("NO HEADER", pno, code); continue
        h = max(above if above else allab, key=lambda g: g["y"])
        sect = title(c, h["y"])
        # strip: right of the code, inside the row (rules sit at the row edges)
        X0, X1 = int((COLX[c] + 40) * SC), int((COLX[c] + 120) * SC)
        Y0, Y1 = int((y0 + 1.5) * SC), int((y1 - 1.5) * SC)
        reg = A[Y0:Y1, X0:X1]
        bead = (255 - reg).max(axis=2) > 25                  # not paper
        ys, xs = np.nonzero(bead)
        if len(ys) < 200:
            print("NO STRIP", pno, code); continue
        x_lo, x_hi = np.percentile(xs, [2, 98]).astype(int)
        y_lo, y_hi = np.percentile(ys, [5, 95]).astype(int)
        cy, hh = (y_lo + y_hi) // 2, max(2, (y_hi - y_lo) // 4)
        band = reg[cy - hh:cy + hh + 1, x_lo + 5:x_hi - 5]
        m = bead[cy - hh:cy + hh + 1, x_lo + 5:x_hi - 5]
        px = band[m]
        Lc = lab(px)[:, 0]
        lo, hi = np.percentile(Lc, [20, 90])
        keep = px[(Lc >= lo) & (Lc <= hi)]
        med = np.median(keep, axis=0)
        dE = np.linalg.norm(lab(keep) - lab(med), axis=1)
        cid = f"p{pno}c{c}y{int(y0):03d}"
        oy, ox = int((y0 - 2) * SC), int((COLX[c] - 2) * SC)
        crop = Image.fromarray(A[oy:int((y1 + 2) * SC), ox:int((COLX[c] + 124) * SC)].astype(np.uint8))
        ImageDraw.Draw(crop).rectangle((X0 + x_lo + 5 - ox, Y0 + cy - hh - oy, X0 + x_hi - 5 - ox,
                                        Y0 + cy + hh - oy), outline=(255, 0, 255))   # sampled band
        crop.save(os.path.join(cropdir, cid + ".png"))
        res.append({"id": cid, "page": pno, "col": c, "y": round(y0, 1), "code": code.rstrip("*"),
                    "star": code.endswith("*"), "art": h["art"], "section": sect,
                    "rgb": [int(round(v)) for v in med], "uniformity": round(float((dE < 15).mean()), 2),
                    "band": [int(X0 + x_lo + 5), int(Y0 + cy - hh), int(X0 + x_hi - 5), int(Y0 + cy + hh)]})
res.sort(key=lambda r: (r["page"], r["col"], r["y"]))
json.dump(res, open(out_json, "w"), indent=1)
print("rows", len(res), "unique codes", len({r["code"] for r in res}),
      "unique art+code", len({(r["art"], r["code"]) for r in res}), "sections", len({(r["page"], r["section"], r["art"]) for r in res}))
