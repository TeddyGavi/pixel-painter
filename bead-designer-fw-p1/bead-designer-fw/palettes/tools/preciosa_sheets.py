# Contact sheets for checking Preciosa rows: index, the row from the card (code + strip,
# sampled band outlined in magenta), the text-layer code, ART group, and sampled colour.
# usage: preciosa_sheets.py po.json cropdir outdir
import sys, json, os
from PIL import Image, ImageDraw, ImageFont
J, C, O = sys.argv[1:4]
os.makedirs(O, exist_ok=True)
rows = json.load(open(J))
font = ImageFont.load_default(size=22)
PER, COLS, CW, CH = 60, 3, 900, 62
for s in range(0, len(rows), PER):
    chunk = rows[s:s + PER]
    n = (len(chunk) + COLS - 1) // COLS
    sheet = Image.new("RGB", (CW * COLS, CH * n), "white"); d = ImageDraw.Draw(sheet)
    for k, r in enumerate(chunk):
        x, y = (k // n) * CW, (k % n) * CH
        im = Image.open(os.path.join(C, r["id"] + ".png"))
        im = im.resize((int(im.width * 58 / im.height), 58))
        sheet.paste(im, (x + 50, y + 2))
        d.text((x + 2, y + 18), "%d" % (s + k), fill="black", font=font)
        tx = x + 60 + im.width
        d.text((tx, y + 4), r["code"] + ("  M" if r["art"].startswith("331 39") else ""), fill="red", font=font)
        d.text((tx, y + 32), r["section"][:22], fill="grey", font=ImageFont.load_default(size=16))
        d.rectangle((x + CW - 70, y + 6, x + CW - 10, y + 56), fill=tuple(r["rgb"]), outline="black")
    sheet.save(os.path.join(O, "sheet%02d.png" % (s // PER)))
print("sheets", (len(rows) + PER - 1) // PER)
