"""Short descriptive colour names from RGB (unofficial; used where a brand publishes
no names). Lightness prefix + hue family, with special cases for neutrals, browns,
beiges and olives. Deterministic, so names stay stable across regenerations."""
import colorsys

HUES = [  # (upper bound in degrees, name)
    (12, "Red"), (24, "Vermilion"), (38, "Orange"), (48, "Amber"), (62, "Yellow"),
    (80, "Chartreuse"), (100, "Lime"), (156, "Green"), (168, "Mint"), (185, "Teal"),
    (198, "Cyan"), (212, "Sky"), (232, "Blue"), (250, "Indigo"), (270, "Violet"),
    (292, "Purple"), (318, "Magenta"), (338, "Pink"), (352, "Rose"), (360, "Red"),
]


def name(r, g, b):
    h, l, s = colorsys.rgb_to_hls(r / 255, g / 255, b / 255)
    hd = h * 360
    if s < 0.10 or (max(r, g, b) - min(r, g, b)) < 14:          # neutrals
        if l > 0.96: return "White"
        if l < 0.10: return "Black"
        return ("Light Grey" if l > 0.68 else "Grey" if l > 0.42
                else "Dark Grey" if l > 0.2 else "Charcoal")
    hue = next(n for lim, n in HUES if hd < lim)
    # earthy tones: low-to-mid saturation or dark warm hues
    if 15 <= hd < 50 and (s < 0.55 or l < 0.42):
        if l > 0.78: return "Cream" if hd > 38 else "Beige"
        if l > 0.62: return "Sand" if hd > 30 else "Peach"
        if l > 0.45: return "Tan"
        return "Brown" if l > 0.25 else "Dark Brown"
    if 50 <= hd < 80 and s < 0.55 and l < 0.55:
        return "Olive" if l > 0.25 else "Dark Olive"
    if l > 0.82: pre = "Pale "
    elif l > 0.66: pre = "Light "
    elif l < 0.22: pre = "Deep "
    elif l < 0.36: pre = "Dark "
    else: pre = ""
    if pre == "" and s < 0.35: pre = "Dusty "
    return pre + hue


# Colour families for browsing on the device (family jump). Index = family id;
# the firmware has the same list (palettes.c FAMILY_NAMES).
FAMILIES = ["Neutrals", "Browns", "Reds", "Oranges", "Yellows", "Greens", "Teals",
            "Blues", "Purples", "Pinks"]


def family(r, g, b):
    h, l, s = colorsys.rgb_to_hls(r / 255, g / 255, b / 255)
    hd = h * 360
    if s < 0.10 or (max(r, g, b) - min(r, g, b)) < 14:
        return 0                                             # neutrals
    if 15 <= hd < 50 and (s < 0.55 or l < 0.42):
        return 1                                             # browns / beiges
    if 50 <= hd < 80 and s < 0.55 and l < 0.55:
        return 5                                             # olives -> greens
    if (hd >= 330 or hd < 12) and l > 0.75:
        return 9                                             # pale red hues read as pink
    for lim, fam in ((12, 2), (40, 3), (68, 4), (160, 5), (198, 6), (250, 7),
                     (300, 8), (345, 9), (360, 2)):
        if hd < lim:
            return fam
