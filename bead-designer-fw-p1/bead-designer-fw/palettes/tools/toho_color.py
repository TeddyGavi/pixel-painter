# Colour for each Toho cell from toho_cells.py: median of the bead pixels after dropping
# the darkest 20% (holes, shadows) and brightest 10% (highlights) by L*, white-balanced so
# the cell's own paper reads 245 (Miyuki's target). Adds rgb + uniformity to cells.json.
# usage: toho_color.py cells.json cropdir
import sys, json, os
import numpy as np
J, C = sys.argv[1:3]


def lab(rgb):
    c = np.asarray(rgb, float) / 255
    c = np.where(c > 0.04045, ((c + 0.055) / 1.055) ** 2.4, c / 12.92)
    M = np.array([[0.4124, 0.3576, 0.1805], [0.2126, 0.7152, 0.0722], [0.0193, 0.1192, 0.9505]])
    xyz = c @ M.T / np.array([0.95047, 1.0, 1.08883])
    f = np.where(xyz > 0.008856, np.cbrt(xyz), 7.787 * xyz + 16 / 116)
    return np.stack([116 * f[..., 1] - 16, 500 * (f[..., 0] - f[..., 1]), 200 * (f[..., 1] - f[..., 2])], -1)


cells = json.load(open(J))
for c in cells:
    px = np.load(os.path.join(C, c["id"] + ".npy")).astype(float)
    gain = 245.0 / np.asarray(c["paper"], float)
    px = np.clip(px * gain, 0, 255)
    Lx = lab(px)
    lo, hi = np.percentile(Lx[:, 0], [20, 90])
    keep = (Lx[:, 0] >= lo) & (Lx[:, 0] <= hi)
    med = np.median(px[keep], axis=0)
    dE = np.linalg.norm(lab(px[keep]) - lab(med), axis=1)
    c["rgb"] = [int(round(v)) for v in med]
    c["uniformity"] = round(float((dE < 15).mean()), 2)
    c["gain"] = [round(float(g), 3) for g in gain]
json.dump(cells, open(J, "w"), indent=1)
u = sorted(cells, key=lambda c: c["uniformity"])[:8]
print("lowest uniformity:", [(c["code"], c["uniformity"]) for c in u])
print("gain range:", np.min([c["gain"] for c in cells], 0), np.max([c["gain"] for c in cells], 0))
