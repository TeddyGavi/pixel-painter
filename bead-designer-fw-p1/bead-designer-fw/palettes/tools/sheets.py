# Contact sheets for reading labels by eye: id + (band, code) crop at 4x.
import sys, json, os
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter
chart, tiles, outdir, per = sys.argv[1], json.load(open(sys.argv[2])), sys.argv[3], int(sys.argv[4])
os.makedirs(outdir, exist_ok=True)
A = Image.open(chart).convert("RGB")
font = ImageFont.load_default(size=28)
cells = []
for t in tiles:
    top = (t["band"][0] - 3) if t["band"] else t["cy"] - 40
    crop = A.crop((t["cx"] - 52, top, t["cx"] + 52, t["cy"] + 20))
    crop = crop.resize((crop.width * 4, crop.height * 4), Image.LANCZOS).filter(ImageFilter.UnsharpMask(2, 120, 2))
    tag = Image.new("RGB", (crop.width, 40), "white")
    ImageDraw.Draw(tag).text((6, 4), "r%02dc%02d" % (t["row"], t["col"]), fill="red", font=font)
    cell = Image.new("RGB", (crop.width + 10, crop.height + 50), "white")
    cell.paste(tag, (0, 0)); cell.paste(crop, (0, 44)); cells.append(cell)
cols = 3
for si in range(0, len(cells), per):
    group = cells[si:si + per]
    w = max(c.width for c in group); h = max(c.height for c in group)
    rows = (len(group) + cols - 1) // cols
    sheet = Image.new("RGB", (w * cols, h * rows), "white")
    for i, c in enumerate(group): sheet.paste(c, ((i % cols) * w, (i // cols) * h))
    sheet.save(os.path.join(outdir, "sheet%02d.png" % (si // per)))
print("sheets", (len(cells) + per - 1) // per, "cell", cells[0].size)
