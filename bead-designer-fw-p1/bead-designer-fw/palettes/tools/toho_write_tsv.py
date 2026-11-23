# Write palettes/toho_round.tsv from toho_cells.py + toho_color.py output (cells.json).
# Codes, card sections and care symbols below were read by eye from the contact sheets
# (toho_sheets.py) for all 207 cells; OCR is only kept where it matched.
# usage: toho_write_tsv.py cells.json palettes_dir
import json, sys
J, R = sys.argv[1], sys.argv[2]
sys.path.insert(0, R)
from colorname import name

cells = json.load(open(J))
assert len(cells) == 207, len(cells)

# OCR misreads, by cell index (chart order): index -> code as printed on the card.
FIX = {19: "7BD", 60: "47H", 94: "25B", 98: "26B", 107: "29B", 123: "2151S",
       124: "2152S", 125: "2153S", 126: "2154S", 127: "2155S", 128: "2156S",
       184: "108BD", 197: "116B"}

# Card sections (headings on the card), as index ranges [start, end). Tables that start a
# column without a heading continue the section above (the card is read column by column).
SECTIONS = [(0, 46, "Transparent", "Clear"),
            (46, 72, "Opaque", "Opaque"),
            (72, 79, "Ceylon", "Ceylon"),
            (79, 144, "Transparent Silver Lined", "Silver-Lined"),
            (144, 172, "Ceylon Silver Lined", "Ceylon SL"),
            (172, 207, "Transparent Luster", "Luster")]

# Care symbols beside each code (card legend):
#   ● bleach   may change by bleach or oxidation over time
#   ★ friction may come off with friction       ☆ friction+ with strong or prolonged friction
#   ■ solvent  may come off with organic solvent (dry-cleaning, perfume)
#   □ solvent+ with prolonged exposure to organic solvent
#   ▲ UV       may fade with sunlight            ※ light   looks different under different light
BS, BSS = "bleach, solvent+", "bleach, solvent, friction"
SYM = {39: "light", 79: BS}
SYM.update({i: BS for i in range(80, 129)})                        # 22A .. 2156S: ● □
SYM.update({i: BSS for i in range(129, 144)})                      # 38, 2202 .. 2226: ● ■ ★
SYM.update({129: BSS + ", UV", 136: BSS + ", UV", 137: BSS + ", UV"})   # 38, 2215, 2216 add ▲
SYM[144] = BS                                                      # 2100: ● □
SYM.update({i: BSS for i in range(145, 171)})                      # 2101 .. 2126: ● ■ ★
SYM[171] = BS                                                      # 2251: ● □
SYM.update({201: "friction+", 204: "friction+",                    # 120, 629: ☆
            202: "solvent+, UV", 203: "solvent+, UV", 206: "solvent+, UV"})  # 289, 290, 631: □ ▲

rows = []
for i, c in enumerate(cells):
    code = FIX.get(i, c["code"])
    sec, prefix = next((s, p) for a, b, s, p in SECTIONS if a <= i < b)
    r, g, b = c["rgb"]
    note = sec + ("; " + SYM[i] if i in SYM else "") + "; uniformity %.2f" % c["uniformity"]
    rows.append((code, r, g, b, prefix + " " + name(r, g, b), note))
codes = [x[0] for x in rows]
assert len(set(codes)) == len(codes), "duplicate codes"
with open(f"{R}/toho_round.tsv", "w") as o:
    o.write("# Toho Round seed beads 11/0 and 8/0 (sample card 111-1), 207 colours.\n"
            "# Order = card order, column by column. Codes eye-verified against the card;\n"
            "# colour from the bead-strip photo, white-balanced per cell. See README.md.\n")
    o.write("code\tr\tg\tb\tname\tsource\tnote\n")
    for code, r, g, b, nm, note in rows:
        o.write(f"{code}\t{r}\t{g}\t{b}\t{nm}\tphoto\t{note}\n")
print("wrote", len(rows))
