import json, re, sys
from collections import Counter
S = sys.argv[1]
cells = []
for n in range(1, 8): cells += json.load(open(f"{S}/work/p{n}.json"))
PAT = re.compile(r"(DBC?)-(\d{1,4})(-1|L|F)?")
def num(c):
    m = PAT.fullmatch(c); return int(m.group(2)) if m else None
good = [c for c in cells if PAT.fullmatch(c["code"])]
bad = [c for c in cells if not PAT.fullmatch(c["code"])]
print("cells", len(cells), "good", len(good), "bad", len(bad))
# junk = no bead strip
print("bad but filled:", [(c["id"], c["code"], c["nbead"]) for c in bad if c["filled"]])
print("good but not filled:", [(c["id"], c["code"], c["nbead"]) for c in good if not c["filled"]])
d = Counter(c["code"] for c in good); print("dupes", [k for k, v in d.items() if v > 1])
# monotonic within column
from itertools import groupby
for key, grp in groupby(sorted(good, key=lambda c: (c["page"], c["cx"], c["cy"])), key=lambda c: (c["page"], c["cx"])):
    g = list(grp)
    for a, b, cc in zip(g, g[1:], g[2:]):
        if a["code"][:3] == b["code"][:3] == cc["code"][:3] and not (num(a["code"]) <= num(b["code"]) <= num(cc["code"])):
            print("seq?", b["id"], a["code"], b["code"], cc["code"])
json.dump(cells, open(f"{S}/work/all.json", "w"), indent=0)
