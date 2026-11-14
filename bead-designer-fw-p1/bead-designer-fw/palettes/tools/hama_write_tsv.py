# Write palettes/hama.tsv from hama_extract.py output. One global white balance:
# the photos have no paper in frame, so bead 01 (White) is the neutral reference and is
# mapped to 245 grey (the same target as Miyuki's per-strip paper balance).
# usage: hama_write_tsv.py hama.json palettes_dir
import json, sys
J, R = sys.argv[1], sys.argv[2]
sys.path.insert(0, R)
from colorname import name
rows = json.load(open(J))
ref = next(r for r in rows if r["code"] == "01")["rgb"]
gain = [245.0 / v for v in ref]
assert len(rows) == 88 and len({r["code"] for r in rows}) == 88
with open(f"{R}/hama.tsv", "w") as o:
    o.write("# Hama midi (5 mm), 88 colours. Order = Hama chart order. Codes from the chart's\n"
            "# text layer (exact); colour from the swatch photo, white-balanced on 01. See README.md.\n")
    o.write("code\tr\tg\tb\tname\tsource\tnote\n")
    for r in rows:
        R_, G, B = (min(255, int(round(v * g))) for v, g in zip(r["rgb"], gain))
        avail = ", ".join(k for k, f in (("Mini", r["mini"]), ("Bio", r["bio"])) if f)
        note = ("also " + avail if avail else "midi only") + "; uniformity %.2f" % r["uniformity"]
        o.write(f"{r['code']}\t{R_}\t{G}\t{B}\t{name(R_, G, B)}\tphoto\t{note}\n")
print("gain", [round(g, 3) for g in gain])
