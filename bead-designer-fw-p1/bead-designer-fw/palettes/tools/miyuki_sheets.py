import json, sys
from PIL import Image, ImageDraw, ImageFont
S = sys.argv[1]
feat = {c["id"]: c for c in json.load(open(f"{S}/work/feat.json"))}
ids = json.load(open(f"{S}/work/picked.json"))
ids = sorted(ids, key=lambda i: (feat[i]["page"], feat[i]["cx"], feat[i]["cy"]))
font = ImageFont.load_default(size=26)
PER, COLS = 30, 2
for s in range(0, len(ids), PER):
    chunk = ids[s:s + PER]; rows = (len(chunk) + COLS - 1) // COLS
    W, H = 900, 60
    sheet = Image.new("RGB", (W * COLS, H * rows), "white"); d = ImageDraw.Draw(sheet)
    for k, i in enumerate(chunk):
        c = feat[i]; x, y = (k % COLS) * W, (k // COLS) * H
        im = Image.open(f"{S}/work/crops{c['page']}/{i}.png")
        im = im.crop((0, 0, 620, im.height)).resize((310, 55))   # label only, half size
        sheet.paste(im, (x + 40, y + 2))
        d.text((x + 2, y + 15), "%d" % (s + k), fill="black", font=font)
        d.text((x + 370, y + 15), c["code"], fill="red", font=font)
        d.rectangle((x + 600, y + 5, x + 680, y + 55), fill=tuple(c["rgb"]))
        st = Image.open(f"{S}/work/crops{c['page']}/{i}.png").crop((700, 20, 1200, 95)).resize((200, 30))
        sheet.paste(st, (x + 690, y + 15))
    sheet.save(f"{S}/work/sheet{s // PER:02d}.png")
    print(f"sheet{s // PER:02d}", len(chunk))
