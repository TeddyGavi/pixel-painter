# Hama midi chart (colour-chart-number.pdf, 1 page): codes from the PDF text layer
# (exact, no OCR), colour sampled from each swatch photo, Mini/Bio dots read per tile.
# usage: hama_extract.py bbox.html page400.png out.json cropdir
import sys, re, json, os
import numpy as np
from PIL import Image
Image.MAX_IMAGE_PIXELS = None
bbox, png, out_json, cropdir = sys.argv[1:5]
os.makedirs(cropdir, exist_ok=True)
A = np.asarray(Image.open(png).convert("RGB")).astype(float)
SC = 400 / 72.0                                    # px per pt at 400 dpi

words = [(float(a), float(b), float(c), float(d), w) for a, b, c, d, w in
         re.findall(r'xMin="([\d.]+)" yMin="([\d.]+)" xMax="([\d.]+)" yMax="([\d.]+)">([^<]+)<', open(bbox).read())]
codes = [w for w in words if re.fullmatch(r"\d{2,3}", w[4]) and w[1] < 760]   # grid labels only
# grid: 8 columns, 11 rows; column = rounded x-position, row = rounded y-position
xs = sorted({round(w[0] / 63.5) for w in codes}); ys = sorted({round(w[1] / 58.6) for w in codes})
print("codes", len(codes), "cols", len(xs), "rows", len(ys))

def lab(rgb):
    c = rgb / 255.0
    c = np.where(c > 0.04045, ((c + 0.055) / 1.055) ** 2.4, c / 12.92)
    M = np.array([[0.4124, 0.3576, 0.1805], [0.2126, 0.7152, 0.0722], [0.0193, 0.1192, 0.9505]])
    xyz = c @ M.T / np.array([0.95047, 1.0, 1.08883])
    f = np.where(xyz > 0.008856, np.cbrt(xyz), 7.787 * xyz + 16 / 116)
    return np.stack([116 * f[..., 1] - 16, 500 * (f[..., 0] - f[..., 1]), 200 * (f[..., 1] - f[..., 2])], -1)

res = []
for x0, y0, x1, y1, code in codes:
    col = xs.index(round(x0 / 63.5)); row = ys.index(round(y0 / 58.6))
    # tile left edge = this label's own x minus the label inset (tile 03 is printed
    # ~4 pt right of its column, so a column median would misplace it)
    colx = x0
    rowy = np.median([w[1] for w in codes if round(w[1] / 58.6) == ys[row]])
    L, T = int(colx * SC - 26), int(rowy * SC)     # tile left; label top (px)
    photo = A[T - 170:T - 25, L + 15:L + 235]       # bead photo, inset from all edges
    band = A[T + 20:T + 80, L:L + 250]              # label band below the photo's bleed (code + dots)
    px = photo.reshape(-1, 3)
    Lc = lab(px)[:, 0]
    lo, hi = np.percentile(Lc, [20, 90])            # drop hole shadows and highlights
    keep = px[(Lc >= lo) & (Lc <= hi)]
    med = np.median(keep, axis=0)
    mL = lab(med[None])[0]
    dE = np.linalg.norm(lab(keep) - mL, axis=1)
    # dots: saturated, non-red pixels to the right of the code
    dots = band[:, 150:].reshape(-1, 3)
    sat = dots.max(1) - dots.min(1)
    blue = ((dots[:, 2] > dots[:, 0] + 25) & (sat > 30)).sum()
    green = ((dots[:, 1] > dots[:, 0] + 15) & (dots[:, 1] > dots[:, 2] + 5) & (sat > 25)).sum()
    Image.fromarray(A[T - 185:T + 90, L - 10:L + 260].astype(np.uint8)).save(f"{cropdir}/{code}.png")
    res.append({"code": code, "row": row, "col": col, "rgb": [int(round(v)) for v in med],
                "uniformity": float((dE < 15).mean()), "mini": bool(blue > 600), "bio": bool(green > 600),
                "blue_px": int(blue), "green_px": int(green)})
res.sort(key=lambda r: (r["row"], r["col"]))
json.dump(res, open(out_json, "w"), indent=1)
print("tiles", len(res), "unique codes", len({r['code'] for r in res}),
      "mini", sum(r["mini"] for r in res), "bio", sum(r["bio"] for r in res))
