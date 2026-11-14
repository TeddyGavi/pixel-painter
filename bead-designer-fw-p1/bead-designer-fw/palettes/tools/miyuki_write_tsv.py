import json, sys, os
S, R = sys.argv[1], sys.argv[2]
sys.path.insert(0, R)
from colorname import name
feat = json.load(open(f"{S}/work/feat.json"))
picked = set(json.load(open(f"{S}/work/picked.json")))
order = lambda c: (c["page"], c["cx"], c["cy"])
feat.sort(key=order)
# chart order; note chart card number
ship = [c for c in feat if c["id"] in picked]
assert len(ship) == 254 and len({c["code"] for c in ship}) == 254
with open(f"{R}/miyuki_delica.tsv", "w") as o:
    o.write("# Miyuki Delica 11/0 (DB). Curated 254 of the chart for max colour coverage;\n"
            "# order = Miyuki chart order. Every code eye-verified against the chart. See README.md.\n")
    o.write("code\tr\tg\tb\tname\tsource\tnote\n")
    for c in ship:
        r, g, b = c["rgb"]
        o.write(f"{c['code']}\t{r}\t{g}\t{b}\t{name(r, g, b)}\tphoto\tcard {c['page']}\n")
os.makedirs(f"{R}/sources", exist_ok=True)
with open(f"{R}/sources/miyuki_delica_all.tsv", "w") as o:
    o.write("# Every bead cell found on Miyuki chart N-2 (Delica 11/0), photo-sampled colour.\n"
            "# verified=1: code eye-checked against the chart (the shipped set). verified=0: code\n"
            "# is raw OCR and MAY BE WRONG (5/6/7/8 confusions seen); check it on the chart before\n"
            "# moving it into miyuki_delica.tsv. uniformity = share of bead pixels within dE 15 of\n"
            "# the median (low = AB / metallic / mixed finish). Not read by gen_palettes.py.\n")
    o.write("code\tr\tg\tb\tuniformity\tcard\tverified\n")
    for c in feat:
        r, g, b = c["rgb"]
        o.write(f"{c['code']}\t{r}\t{g}\t{b}\t{c['solid']:.2f}\t{c['page']}\t{int(c['id'] in picked)}\n")
print("ship", len(ship), "all", len(feat))
