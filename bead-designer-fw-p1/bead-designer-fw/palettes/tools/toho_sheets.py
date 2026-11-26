# Contact sheets for eye-checking Toho codes and care symbols: index, the full ruled cell
# from the card (code, symbols, bead strip), the OCR'd code in red and the sampled colour.
# usage: toho_sheets.py cells.json cropdir outdir
import sys, json, os
from PIL import Image, ImageDraw, ImageFont
J, C, O = sys.argv[1:4]
os.makedirs(O, exist_ok=True)
cells = json.load(open(J))
font = ImageFont.load_default(size=28)
PER, COLS, CW, CH = 20, 2, 1000, 105
for s in range(0, len(cells), PER):
    chunk = cells[s:s + PER]
    rows = (len(chunk) + COLS - 1) // COLS
    sheet = Image.new("RGB", (CW * COLS, CH * rows), "white")
    d = ImageDraw.Draw(sheet)
    for k, c in enumerate(chunk):
        x, y = (k % COLS) * CW, (k // COLS) * CH
        im = Image.open(os.path.join(C, c["id"] + ".png"))
        bx = c["box"]; ox, oy = c["m"] - c["l"] + 4, 4      # crop starts at (l-4, y0-4)
        ImageDraw.Draw(im).rectangle((ox + bx[0], oy + bx[1], ox + bx[2], oy + bx[3]), outline="black")
        im = im.resize((int(im.width * 100 / im.height), 100))
        sheet.paste(im, (x + 60, y + 2))
        d.text((x + 4, y + 35), "%d" % (s + k), fill="black", font=font)
        d.text((x + 60 + im.width + 10, y + 10), c["code"] or "?", fill="red", font=font)
        d.rectangle((x + 60 + im.width + 10, y + 50, x + 60 + im.width + 110, y + 95),
                    fill=tuple(c["rgb"]), outline="black")
    sheet.save(os.path.join(O, "sheet%02d.png" % (s // PER)))
print("sheets", (len(cells) + PER - 1) // PER)
