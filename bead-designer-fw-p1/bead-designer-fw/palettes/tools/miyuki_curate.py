# Features per bead cell + greedy max-coverage selection of N colours.
# usage: curate.py scratch N
import json, re, sys, os
import numpy as np
from PIL import Image
S, N = sys.argv[1], int(sys.argv[2])
PAT = re.compile(r"DBC?-\d{1,4}(-1|L|F)?")
cells = [c for c in json.load(open(f"{S}/work/all.json")) if PAT.fullmatch(c["code"])]

def lab(rgb):
    c = rgb / 255.0
    c = np.where(c > 0.04045, ((c + 0.055) / 1.055) ** 2.4, c / 12.92)
    M = np.array([[0.4124, 0.3576, 0.1805], [0.2126, 0.7152, 0.0722], [0.0193, 0.1192, 0.9505]])
    xyz = c @ M.T / np.array([0.95047, 1.0, 1.08883])
    f = np.where(xyz > 0.008856, np.cbrt(xyz), 7.787 * xyz + 16 / 116)
    return np.stack([116 * f[..., 1] - 16, 500 * (f[..., 0] - f[..., 1]), 200 * (f[..., 1] - f[..., 2])], -1)

for c in cells:
    im = np.asarray(Image.open(f"{S}/work/crops{c['page']}/{c['id']}.png").convert("RGB")).astype(float)
    paper = np.median(im[5:50, 640:700].reshape(-1, 3), axis=0)   # paper beside the strip
    gain = 245.0 / paper                                              # white balance: paper -> 245
    px = np.clip(im[38:72, 770:1140].reshape(-1, 3) * gain, 0, 255)
    L = lab(px)
    med = np.median(px, axis=0)
    mL = lab(med[None])[0]
    dE = np.linalg.norm(L - mL, axis=1)
    c["rgb"] = [int(round(v)) for v in med]
    c["lab"] = mL.tolist()
    c["solid"] = float((dE < 15).mean())          # 1 = uniform colour; low = AB/metallic/mixed
json.dump(cells, open(f"{S}/work/feat.json", "w"))

# drop duplicate OCR codes from selection until verified (keep first); greedy pick
seen, pool = set(), []
for c in cells:
    if c["code"] in seen: continue
    seen.add(c["code"]); pool.append(c)
labs = np.array([c["lab"] for c in pool]); sol = np.array([c["solid"] for c in pool])
w = 0.4 + 0.6 * np.clip((sol - 0.2) / 0.5, 0, 1)    # solidity weight
sel = [i for i, c in enumerate(pool) if c["code"] in ("DB-10", "DB-200")]
mind = np.min([np.linalg.norm(labs - labs[i], axis=1) for i in sel], axis=0)
while len(sel) < N:
    i = int(np.argmax(mind * w)); sel.append(i)
    mind = np.minimum(mind, np.linalg.norm(labs - labs[i], axis=1))
picked = [pool[i] for i in sel]
print("pool", len(pool), "picked", len(picked), "final min dE %.1f" % mind.max())
print("solid of picked: median %.2f, <0.3: %d" % (np.median([c["solid"] for c in picked]), sum(c["solid"] < 0.3 for c in picked)))
json.dump([c["id"] for c in picked], open(f"{S}/work/picked.json", "w"))
