# Locate every ruled cell on a Toho sample card (e.g. 111-1, round 11/0), OCR its code,
# and sample the bead strip in the right-hand cell.
# usage: toho_cells.py card.jpg out.json cropdir
# card.jpg = the PDF's embedded photo (pdfimages -j), not a re-render.
import sys, re, json, subprocess, os
import numpy as np
from PIL import Image, ImageOps
src, out_json, cropdir = sys.argv[1:4]
os.makedirs(cropdir, exist_ok=True)
img = Image.open(src).convert("RGB")
A = np.asarray(img).astype(int)
L = A.sum(axis=2) / 3
H, W = L.shape
# thin ruled lines: darker than the paper 6 px either side (not text blobs or beads)
def thin(axis):
    a = np.roll(L, 6, axis); b = np.roll(L, -6, axis)
    return (L < np.minimum(a, b) - 12) & (np.minimum(a, b) > 200)
vline, hline = thin(1), thin(0)
# the photo is slightly rotated: let a rule wander +-3 px across a table's width
hl = hline.copy()
for d in (1, 2, 3):
    hl |= np.roll(hline, d, 0) | np.roll(hline, -d, 0)


def runs(mask, mingap=1):
    """[start, end) of True runs in a 1-D mask."""
    idx = np.flatnonzero(mask)
    if not len(idx): return []
    out, s = [], idx[0]
    for a, b in zip(idx[:-1], idx[1:]):
        if b - a > mingap: out.append((s, a + 1)); s = b
    out.append((s, idx[-1] + 1))
    return out


# 1. vertical table lines: columns that are dark over long stretches
vfrac = vline[int(H * 0.15):int(H * 0.93)].mean(axis=0)
vx = [int((a + b) / 2) for a, b in runs(vfrac > 0.2, 3)]
# group into tables: left, mid, right lines (cell widths ~ 250 and ~ 330 px)
tables = []
i = 0
while i + 2 < len(vx):
    l, m, r = vx[i:i + 3]
    if 150 < m - l < 450 and 200 < r - m < 500:
        tables.append((l, m, r)); i += 3
    else:
        i += 1
print("vertical lines", vx, "\ntables", tables)

cells = []
for t, (l, m, r) in enumerate(tables):
    # 2. horizontal lines spanning this table
    hfrac = hl[:, l + 5:r - 5].mean(axis=1)
    hy = [int((a + b) / 2) for a, b in runs(hfrac > 0.5, 2)]
    for y0, y1 in zip(hy[:-1], hy[1:]):
        if not 60 < y1 - y0 < 130: continue          # section gap, not a cell
        # find this row's middle rule (it shifts between sub-tables); none = a heading
        # (within 40 px of the column's; heading letters can pass as a rule elsewhere)
        vf = vline[y0 + 10:y1 - 10, m - 40:m + 40].mean(axis=0)
        vf = np.convolve(vf, np.ones(5), "same")
        if vf.max() < 0.8: continue
        lf = np.convolve(vline[y0 + 10:y1 - 10, l - 12:l + 12].mean(axis=0), np.ones(5), "same")
        if lf.max() < 0.8: continue                    # no left rule: a heading row
        cells.append((t, l, m - 40 + int(np.argmax(vf)), r, y0, y1))

res = []
for k, (t, l, m, r, y0, y1) in enumerate(cells):
    cid = f"t{t}y{y0:04d}"
    # 3. code: left ~62% of the code cell (symbols sit in the right part)
    cx1 = l + int((m - l) * 0.62)
    lab = L[y0 + 8:y1 - 8, l + 8:cx1]
    txt = lab < np.median(lab) - 60
    bw = Image.fromarray(np.where(txt, 0, 255).astype(np.uint8))
    bw = Image.fromarray(np.pad(np.asarray(bw), 20, constant_values=255))
    p = os.path.join(cropdir, "_c.png"); bw.save(p)
    code = subprocess.run(["tesseract", p, "-", "--psm", "7", "-c",
                           "tessedit_char_whitelist=0123456789ABCDLS"],
                          capture_output=True, text=True).stdout.strip().replace(" ", "")
    # 4. bead strip: a fixed band through the middle of the strip. Every strip sits at the
    # same place in its cell (measured on the saturated ones: bead bodies span y0+28..67,
    # m+65..292). Locating it per cell by "differs from paper" fails on white and pale
    # beads, where only the drop shadow differs.
    ya, yb, xa, xb = y0 + 37, y0 + 56, m + 85, m + 280
    px = A[ya:yb, xa:xb].reshape(-1, 3)
    box = [xa - m, ya - y0, xb - m, yb - y0]
    # white reference: the code cell's paper (same place on the card, so same vignetting,
    # but no coloured bleed from the beads); text and rules dropped
    cc = A[y0 + 8:y1 - 8, l + 8:m - 8].reshape(-1, 3)
    ccl = cc.sum(axis=1) / 3
    paper = np.median(cc[ccl > np.median(ccl) - 15], axis=0)
    res.append({"id": cid, "table": t, "y0": int(y0), "y1": int(y1), "l": int(l), "m": int(m),
                "r": int(r), "code": code, "nbead": int(len(px)), "box": box,
                "paper": paper.round(1).tolist()})
    np.save(os.path.join(cropdir, cid + ".npy"), px.astype(np.uint8))
    Image.fromarray(A[y0 - 4:y1 + 4, l - 4:r + 4].astype(np.uint8)).save(os.path.join(cropdir, cid + ".png"))
json.dump(res, open(out_json, "w"), indent=1)
ok = [c for c in res if re.fullmatch(r"\d{1,4}[A-Z]{0,3}", c["code"])]
print(f"cells {len(res)}  ocr-ok {len(ok)}  per table",
      [sum(1 for c in res if c['table'] == t) for t in range(len(tables))])
