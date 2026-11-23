# Curate N of the Preciosa rocailles rows (preciosa_extract.py output) for maximum colour
# coverage, then write palettes/preciosa_rocailles.tsv and the full card to
# palettes/sources/preciosa_rocailles_all.tsv.
# Selection = Miyuki's: greedy farthest-point in CIE Lab, weighted toward uniform-looking
# strips (striped, AB, metallic score lower), seeded with black 23980 and white 03050.
# Codes: as printed, except matt beads (ART 331 39 001) get a "39-" prefix, because
# the card prints the same colour code for the shiny and the matt version.
# usage: preciosa_write_tsv.py po.json palettes_dir [N=254]
import json, sys, os
import numpy as np
J, R = sys.argv[1], sys.argv[2]
N = int(sys.argv[3]) if len(sys.argv) > 3 else 254
sys.path.insert(0, R)
from colorname import name


def lab(rgb):
    c = np.asarray(rgb, float) / 255.0
    c = np.where(c > 0.04045, ((c + 0.055) / 1.055) ** 2.4, c / 12.92)
    M = np.array([[0.4124, 0.3576, 0.1805], [0.2126, 0.7152, 0.0722], [0.0193, 0.1192, 0.9505]])
    xyz = c @ M.T / np.array([0.95047, 1.0, 1.08883])
    f = np.where(xyz > 0.008856, np.cbrt(xyz), 7.787 * xyz + 16 / 116)
    return np.stack([116 * f[..., 1] - 16, 500 * (f[..., 0] - f[..., 1]), 200 * (f[..., 1] - f[..., 2])], -1)


# Short finish prefix for the generated name; the first keyword found in the section wins.
PREFIX = [("Striped", "Stripe"), ("Harlequin", "Stripe"), ("Pearl", "Pearl"),
          ("Phosphorescent", "Glow"), ("Metallic", "Metallic"), ("Iris", "Metallic"),
          ("Silver, Gold", "Metallic"), ("Bronze", "Metallic"), ("Melafyr", "Metallic"),
          ("Matt", "Matt"), ("Rainbow", "Rainbow"), ("Sfinx", "Sfinx"), ("Lined", "Lined"),
          ("Lustered", "Luster"), ("Solgel", "Dyed"), ("Dyed", "Dyed"), ("Shell", "Shell"),
          ("Travertin", "Travertin"), ("Cornelian", "Cornelian"), ("Transparent", "Clear"),
          ("Opaque", "Opaque"), ("Chalk", "Opaque"), ("Alabaster", "Alabaster")]

rows = json.load(open(J))
assert len(rows) == 667, len(rows)
# One global white balance, as for Hama: the strips are cut-outs on digital white, so
# there is no paper in the photos. Bead 03050 (Natural Opaque, chalk white) -> 245 grey.
ref = next(r for r in rows if r["code"] == "03050" and r["art"] == "311 19 001")["rgb"]
gain = [245.0 / v for v in ref]
for r in rows:
    r["raw"] = r["rgb"]
    r["rgb"] = [min(255, int(round(v * g))) for v, g in zip(r["rgb"], gain)]
print("white balance gain", [round(g, 3) for g in gain],
      "clipped", sum(1 for r in rows if 255 in r["rgb"]))
for r in rows:
    r["dev"] = ("39-" if r["art"] == "331 39 001" else "") + r["code"]
    r["lab"] = lab(r["rgb"]).tolist()
assert len({r["dev"] for r in rows}) == len(rows), "device codes not unique"

labs = np.array([r["lab"] for r in rows]); uni = np.array([r["uniformity"] for r in rows])
w = 0.4 + 0.6 * np.clip((uni - 0.2) / 0.5, 0, 1)
# seeds: black 23980 and white 03050 (Natural Opaque), as Miyuki seeds DB-10 / DB-200
dark = next(i for i, r in enumerate(rows) if r["dev"] == "23980")
light = next(i for i, r in enumerate(rows) if r["dev"] == "03050")
sel = [dark, light]
mind = np.min([np.linalg.norm(labs - labs[i], axis=1) for i in sel], axis=0)
while len(sel) < N:
    i = int(np.argmax(mind * w)); sel.append(i)
    mind = np.minimum(mind, np.linalg.norm(labs - labs[i], axis=1))
picked = set(sel)
print("seeds", rows[dark]["dev"], rows[light]["dev"], "picked", len(picked),
      "max dE of any card bead to a shipped one %.1f" % mind.max(),
      "median uniformity picked %.2f" % np.median(uni[sel]))

with open(f"{R}/preciosa_rocailles.tsv", "w") as o:
    o.write("# Preciosa Ornela (Czech beads) seed beads, rocailles. Curated %d of the 667 rows on the\n"
            "# Rocailles sample cards for max colour coverage; order = card order. Codes from the PDF\n"
            "# text layer (exact); matt (ART 331 39 001) codes carry a 39- prefix. See README.md.\n" % N)
    o.write("code\tr\tg\tb\tname\tsource\tnote\n")
    for i, r in enumerate(rows):
        if i not in picked: continue
        pre = next(p for k, p in PREFIX + [("", "")] if k in r["section"])
        R_, G, B = r["rgb"]
        nm = (pre + " " + name(R_, G, B)).strip()
        note = "ART %s %s; p%d%s; uniformity %.2f" % (r["art"], r["section"], r["page"],
                                                    "; Gold" if r["star"] else "", r["uniformity"])
        o.write(f"{r['dev']}\t{R_}\t{G}\t{B}\t{nm}\tphoto\t{note}\n")
os.makedirs(f"{R}/sources", exist_ok=True)
with open(f"{R}/sources/preciosa_rocailles_all.tsv", "w") as o:
    o.write("# Every row of the Preciosa Ornela Rocailles sample cards (pages 2-6), 667 rows. Codes\n"
            "# from the PDF text layer (exact; device code adds 39- for matt). shipped=1: in\n"
            "# preciosa_rocailles.tsv. Not read by gen_palettes.py.\n")
    o.write("code\tcard_code\tart\tsection\tpage\tr\tg\tb\tuniformity\tshipped\n")
    for i, r in enumerate(rows):
        R_, G, B = r["rgb"]
        o.write(f"{r['dev']}\t{r['code']}{'*' if r['star'] else ''}\t{r['art']}\t{r['section']}\t{r['page']}"
                f"\t{R_}\t{G}\t{B}\t{r['uniformity']:.2f}\t{int(i in picked)}\n")
