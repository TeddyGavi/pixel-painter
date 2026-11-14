# Locate every code cell on a Miyuki Delica chart page, OCR its code, and sample the
# bead strip to its right.  usage: miyuki_cells.py page.jpg pageno out.json cropdir
import sys, re, json, subprocess, os
import numpy as np
from PIL import Image, ImageFilter, ImageOps
Image.MAX_IMAGE_PIXELS = None
src, pageno, out_json, cropdir = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4]
os.makedirs(cropdir, exist_ok=True)
img = Image.open(src).convert("RGB")
A = np.asarray(img)
H, W = A.shape[:2]

# 1. page-wide sparse OCR at half res to find code words
half = ImageOps.autocontrast(img.convert("L").resize((W // 2, H // 2)), cutoff=1)
tmp = os.path.join(cropdir, "_page.png"); half.save(tmp)
tsv = subprocess.run(["tesseract", tmp, "-", "--psm", "11", "tsv"], capture_output=True, text=True).stdout
words = []
for line in tsv.splitlines()[1:]:
    f = line.split("\t")
    if len(f) < 12: continue
    t = f[11].strip()
    if re.fullmatch(r"DBC?-\d{1,4}(-1|L|F)?", t):
        x, y, w, h = (int(v) * 2 for v in f[6:10])
        words.append((x, y, w, h, t))

# 2. column lefts: cluster word x
xs = sorted(w[0] for w in words)
cols = []
for x in xs:
    if cols and x - cols[-1][-1] < 60: cols[-1].append(x)
    else: cols.append([x])
cols = [c for c in cols if len(c) >= 4]
colx = [int(np.median(c)) for c in cols]
gaps = np.diff(colx); sp = np.median(gaps[gaps < 1500])
full = [colx[0]]
for x in colx[1:]:
    while x - full[-1] > 1.5 * sp: full.append(int(full[-1] + sp))
    full.append(x)
colx = full

# 3. rows: one pitch per page (from all columns), offset/slope per column
alld = []
for cx in colx:
    ys = np.array(sorted(w[1] + w[3] / 2 for w in words if abs(w[0] - cx) < 60))
    d = np.diff(ys); alld += list(d[(d > 150) & (d < 260)])
P = float(np.median(alld))
res = []
for cx in colx:
    ws = [w for w in words if abs(w[0] - cx) < 60]
    if not ws: continue
    ys = np.array(sorted(w[1] + w[3] / 2 for w in ws))
    k = np.round((ys - ys[0]) / P)
    if len(set(k)) >= 3 and k.max() - k.min() > 10:
        b, a = np.polyfit(k, ys, 1)
    else:
        b, a = P, float(np.median(ys - P * k))
    wx = np.array([w[0] for w in ws]); wy = np.array([w[1] + w[3] / 2 for w in ws])
    tilt, x0fit = np.polyfit(wy, wx, 1) if len(ws) >= 5 and np.ptp(wy) > 2000 else (0.0, float(np.median(wx)))
    kmin = int(np.floor((0 - a) / b)); kmax = int(np.ceil((H - a) / b))
    for kk in range(kmin, kmax + 1):
        cy = int(round(a + b * kk))
        cxr = int(round(x0fit + tilt * cy))   # this row's label left edge
        if cy < 150 or cy > H - 150: continue
        lab = A[cy - 80:cy + 80, cxr - 45:cxr + 505].astype(int)
        ssum = lab.sum(axis=2)
        txt = ssum < np.median(ssum) - 110
        txt[:, txt.mean(axis=0) > 0.85] = False        # drop vertical grid lines
        txt[txt.mean(axis=1) > 0.6, :] = False        # and horizontal ones
        if txt.sum() < 300: continue
        bw = Image.fromarray(np.where(txt, 0, 255).astype(np.uint8))
        bw = bw.resize((bw.width * 2 // 5, bw.height * 2 // 5), Image.LANCZOS)
        bw = Image.fromarray(np.pad(np.asarray(bw), 15, constant_values=255))
        p = os.path.join(cropdir, "_c.png"); bw.save(p)
        code = subprocess.run(["tesseract", p, "-", "--psm", "7", "-c",
                               "tessedit_char_whitelist=DBC-0123456789LF"],
                              capture_output=True, text=True).stdout.strip().replace(" ", "")
        # bead strip: central rows of the strip, right of the label cell
        sx0, sx1 = cxr + 700, cxr + 1150
        sw = A[cy - 22:cy + 22, sx0:sx1].reshape(-1, 3).astype(int)
        bg = A[cy - 50:cy - 40, cxr + 560:cxr + 640].reshape(-1, 3).astype(int).mean(axis=0)
        beadmask = np.abs(sw - bg).max(axis=1) > 18
        filled = int(beadmask.sum()) > 2000
        pick = sw[beadmask] if beadmask.sum() > 2000 else sw
        samp = np.median(pick, axis=0).round().astype(int).tolist()
        beadpx = sw[beadmask]
        cid = f"p{pageno}x{cx:04d}y{cy:05d}"
        Image.fromarray(A[cy - 55:cy + 55, cxr - 45:sx1 + 30]).save(os.path.join(cropdir, cid + ".png"))
        res.append({"id": cid, "page": pageno, "cx": cx, "cy": cy, "code": code,
                    "filled": bool(filled), "nbead": int(beadpx.shape[0]), "sampled": samp})
json.dump(res, open(out_json, "w"), indent=1)
ok = [r for r in res if re.fullmatch(r"DBC?-\d{1,4}(-1|L|F)?", r["code"])]
print(f"page {pageno}: cols {colx} cells {len(res)} ocr-ok {len(ok)} words {len(words)}")
