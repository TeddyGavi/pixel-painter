# Per-tile extraction from the Artkal S chart: code (OCR), RGB label (OCR of the blue
# band, if present) and a sampled swatch colour for cross-checking / unlabelled beads.
# usage: extract_tiles.py chart.png out.json cropdir
import sys, re, json, subprocess, os
import numpy as np
from PIL import Image
img = Image.open(sys.argv[1]).convert("RGB"); A = np.asarray(img).astype(int)
out_json, cropdir = sys.argv[2], sys.argv[3]; os.makedirs(cropdir, exist_ok=True)
COLS = [77, 181, 284, 388, 491, 594, 697, 800, 903, 1007, 1109, 1213, 1316, 1420]
ROWS = [387 + 175 * i for i in range(19)]

def ocr(im, wl, psm=7):
    p = os.path.join(cropdir, "_tmp.png"); im.save(p)
    r = subprocess.run(["tesseract", p, "-", "--psm", str(psm), "-c",
                        "tessedit_char_whitelist=" + wl], capture_output=True, text=True)
    return r.stdout.strip()

def is_blue(px):  # label band blue (~ 0,150,220)
    r, g, b = px[..., 0], px[..., 1], px[..., 2]
    return (b > 170) & (r < 90) & (g > 110) & (g < 210)

res = []
for ri, cy in enumerate(ROWS):
    for ci, cx in enumerate(COLS):
        x0, x1 = cx - 50, cx + 50
        code_box = A[cy - 18:cy + 18, cx - 42:cx + 42]
        if (code_box.min(axis=2) < 100).sum() < 30:      # no dark text: empty slot
            continue
        cim = Image.fromarray(code_box.astype(np.uint8)).resize((84 * 3, 36 * 3), Image.LANCZOS)
        code = ocr(cim, "SETLGNPUVD0123456789")
        # blue band: scan rows above the code
        band = None
        for y in range(cy - 20, cy - 70, -1):
            if is_blue(A[y, x0 + 10:x1 - 10]).mean() > 0.3:
                y1 = y
                y0 = y
                while is_blue(A[y0 - 1, x0 + 10:x1 - 10]).mean() > 0.3: y0 -= 1
                band = (y0, y1 + 1); break
        label = None; rgb = None; has_text = False
        if band:
            bx = A[band[0]:band[1], x0 - 2:x1 + 2]
            lum = bx[..., 0] * 0.3 + bx[..., 1] * 0.59 + bx[..., 2] * 0.11
            text = lum > 180          # pale cyan/white glyphs (~220) vs band blue (~115)
            has_text = text.sum() > 15
            # find blue horizontal extent
            cols_blue = np.where(is_blue(bx).mean(axis=0) > 0.5)[0]
            bx = bx[:, cols_blue.min():cols_blue.max() + 1]
            g = 255 - text * 255                            # dark text on white for OCR
            bim = Image.fromarray(g.astype(np.uint8)).resize((bx.shape[1] * 5, bx.shape[0] * 5), Image.LANCZOS)
            bim = Image.fromarray(np.pad(np.asarray(bim), 20, constant_values=255))
            label = ocr(bim, "RGB:0123456789 ") if has_text else ""
            m = re.search(r"R\s*:?\s*(\d{1,3})\s*G\s*:?\s*(\d{1,3})\s*B\s*:?\s*(\d{1,3})", label)
            if m: rgb = [int(v) for v in m.groups()]
            Image.fromarray(A[band[0] - 2:band[1] + 2, x0:x1].astype(np.uint8)).resize((400, (band[1] - band[0] + 4) * 4), Image.NEAREST).save(os.path.join(cropdir, f"r{ri:02d}c{ci:02d}_band.png"))
        # swatch: area above band (or above code)
        top = (band[0] if band else cy - 20) - 6
        sw = A[top - 75:top, cx - 36:cx + 36].reshape(-1, 3)
        light = (sw.min(axis=1) > 225) & (sw.max(axis=1) - sw.min(axis=1) < 18)
        pick = sw[~light] if (~light).sum() > 200 else sw
        samp = np.median(pick, axis=0).round().astype(int).tolist()
        Image.fromarray(A[top - 80:cy + 20, x0:x1].astype(np.uint8)).save(os.path.join(cropdir, f"r{ri:02d}c{ci:02d}_tile.png"))
        res.append({"row": ri, "col": ci, "cx": cx, "cy": cy, "band": list(band) if band else None,
                    "has_text": bool(band and has_text), "code": code, "label": label,
                    "rgb": rgb, "sampled": samp})
json.dump(res, open(out_json, "w"), indent=1)
labelled = [r for r in res if r["rgb"]]
print("tiles", len(res), "with rgb", len(labelled), "band-but-unparsed", sum(1 for r in res if r["label"] is not None and not r["rgb"]))
bad = [r["code"] for r in res if not re.fullmatch(r"S[A-Z]{0,2}\d{1,3}", r["code"] or "")]
print("bad codes", bad)
from collections import Counter
print("dupes", [k for k, v in Counter(r["code"] for r in res).items() if v > 1])
