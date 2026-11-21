import json, sys
from PIL import Image, ImageDraw, ImageFont
S = sys.argv[1]
r = json.load(open(f"{S}/work_hama/hama.json"))
f = ImageFont.load_default(size=22)
W, H = 300, 170
for half in (0, 1):
    part = r[half * 44:(half + 1) * 44]
    sheet = Image.new("RGB", (W * 4, H * 11), "white"); d = ImageDraw.Draw(sheet)
    for k, t in enumerate(part):
        x, y = (k % 4) * W, (k // 4) * H
        im = Image.open(f"{S}/work_hama/crops/{t['code']}.png").resize((160, 163))
        sheet.paste(im, (x + 4, y + 3))
        d.rectangle((x + 170, y + 10, x + 290, y + 90), fill=tuple(t["rgb"]), outline="black")
        d.text((x + 172, y + 100), t["code"] + (" M" if t["mini"] else "") + (" B" if t["bio"] else ""), fill="black", font=f)
        d.text((x + 172, y + 128), "u%.2f" % t["uniformity"], fill="gray", font=f)
    sheet.save(f"{S}/work_hama/sheet{half}.png")
