#!/usr/bin/env python3
"""Hardware test runner for the Waveshare ESP32-S3-Touch-AMOLED-2.41 Rev 2.0.

Drives the real board over its USB Serial/JTAG console (/dev/ttyACM0) with the
firmware's serial test keys, and captures the screen with the 'F' framebuffer dump.
It runs the same scenario scripts as the simulator (sim/scripts/*.txt), translated
to keys, plus hardware-only scripts in hw/scripts/.

  hw/hwtest.py boot                     # reset, print the boot log, check it
  hw/hwtest.py shot out.png             # dump the current screen to a PNG
  hw/hwtest.py key G                    # send raw keys (G = edge ruler, t = colour bars)
  hw/hwtest.py run [names...]           # run scenarios, compare with hw/golden
  hw/hwtest.py run --update-golden ...  # (re)write hw/golden/<name>.png
  options: --port /dev/ttyACM0  --no-reset  --list

Each scenario starts from a factory reset ('!Z': NVS erased, reboot), so saved
patterns and settings never leak between tests.
Needs pyserial (in the ESP-IDF Python env). The port is root:uucp: run under
`newgrp uucp` (see hw/run.sh).
"""
import argparse, base64, os, re, struct, sys, time, zlib, glob

import serial  # pyserial

HERE = os.path.dirname(os.path.abspath(__file__))
FW = os.path.dirname(HERE)
CRASH = re.compile(r"Guru Meditation|abort\(\) was called|ESP_ERROR_CHECK failed|assert failed|"
                   r"Stack canary|stack overflow|Backtrace:|rst:0x[0-9a-f]+ \((TG|RTCWDT|INT_WDT)")
READY = "keys: w/a/s/d"            # serial key help line: input layer is up
BOOT_MUST = ["Waveshare 2.41 Rev 2.0", "TCA9554 ok at 0x20", "RM690B0 ready", READY]
BOOT_MUST_NOT = re.compile(r"^E \(", re.M)


class Board:
    def __init__(self, port):
        self.s = serial.Serial(port, 115200, timeout=0.01)
        self.log = []
        self.buf = b""

    def reset(self):
        # USB Serial/JTAG: RTS=1 holds the chip in reset, DTR=0 keeps GPIO0 high.
        self.s.dtr = False; self.s.rts = True; time.sleep(0.1)
        self.s.rts = False
        self.buf = b""; self.log = []

    def pump(self, secs):
        end = time.time() + secs
        while time.time() < end:
            d = self.s.read(max(65536, self.s.in_waiting))
            if d:
                self.buf += d
                *lines, self.buf = self.buf.split(b"\n")
                for l in lines:
                    self.log.append(l.decode("utf-8", "replace").rstrip("\r"))

    def wait_for(self, text, secs):
        end = time.time() + secs
        while time.time() < end:
            if any(text in l for l in self.log):
                return True
            self.pump(0.1)
        return False

    def key(self, k):
        self.s.write(k.encode()); self.s.flush()

    def reopen(self, port, secs=8):
        """After a chip reset the USB device re-enumerates; reopen the port."""
        try: self.s.close()
        except Exception: pass
        end = time.time() + secs
        while time.time() < end:
            try:
                self.s = serial.Serial(port, 115200, timeout=0.01); return True
            except Exception:
                time.sleep(0.2)
        return False

    def shot(self, secs=30, tries=3):
        for k in range(tries):
            try:
                return self._shot(secs)
            except RuntimeError:
                if k == tries - 1: raise
                self.pump(0.5)

    def _shot(self, secs):
        self.pump(0.2)
        start = len(self.log)
        self.key("F")
        end = time.time() + secs
        while time.time() < end:
            self.pump(0.1)
            tail = self.log[start:]
            if any(l.startswith("FBEND") for l in tail):
                break
        tail = self.log[start:]
        try:
            i = next(k for k, l in enumerate(tail) if l.startswith("FBDUMP"))
            j = next(k for k, l in enumerate(tail) if l.startswith("FBEND"))
        except StopIteration:
            raise RuntimeError("no framebuffer dump received")
        _, w, h, _ = tail[i].split()
        w, h = int(w), int(h)
        try:
            data = b"".join(base64.b64decode(l[3:]) for l in tail[i + 1:j] if l.startswith("FB:"))
        except Exception as e:
            del self.log[start:]; raise RuntimeError(f"bad dump line: {e}")
        _, n, crc = tail[j].split()
        if len(data) != int(n) or (zlib.crc32(data) & 0xFFFFFFFF) != int(crc, 16):
            del self.log[start:]
            raise RuntimeError(f"dump corrupt: {len(data)} bytes vs {n}")
        raw = zlib.decompress(data)
        lut = {}
        px = bytearray()
        for k in range(0, len(raw), 2):
            v = raw[k] << 8 | raw[k + 1]
            c = lut.get(v)
            if c is None:
                r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
                c = lut[v] = bytes(((r * 527 + 23) >> 6, (g * 259 + 33) >> 6, (b * 527 + 23) >> 6))
            px += c
        if len(px) != w * h * 3:
            raise RuntimeError("dump size mismatch")
        del self.log[start:]        # keep dumps out of the test log
        return w, h, bytes(px)


def write_png(path, w, h, rgb):
    raw = b"".join(b"\0" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    def chunk(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def read_png_rgb(path):
    d = open(path, "rb").read(); i = 8; idat = b""; w = h = 0
    while i < len(d):
        n = struct.unpack(">I", d[i:i + 4])[0]; t = d[i + 4:i + 8]; c = d[i + 8:i + 8 + n]
        if t == b"IHDR": w, h = struct.unpack(">II", c[:8])
        if t == b"IDAT": idat += c
        i += 12 + n
    raw = zlib.decompress(idat)
    return w, h, b"".join(raw[y * (w * 3 + 1) + 1:(y + 1) * (w * 3 + 1)] for y in range(h))


# ---- scenario scripts --------------------------------------------------------------
# sim/scripts lines: "<t> uart0 <key>" | "<t> knob cw|ccw <n>" | "<t> press sw|menu|boot <ms>"
#                    "<t> adc ..." (sim only) | "<t> poke ..." (sim only) | "<t> stop"
SIM_ONLY = re.compile(r"\b(poke)\b|\badc 27 (?!3\.3)")
PRESS = {"sw": "p", "menu": "m", "boot": "o"}
# Screens that animate in real time (the screen saver): pixels can't match a golden
# run to run on hardware, so check that the saver is up instead.
ANIMATED = {"saver", "saver_cycle"}
# Screens with a scrolling text line (marquee): compare everything above the name
# line exactly; the line itself only has to show text (its scroll offset is timing).
MARQUEE_ROWS = {"marquee": 265}


def saver_check(w, h, rgb):
    n = w * h
    black = sum(1 for i in range(0, len(rgb), 3) if rgb[i] < 8 and rgb[i + 1] < 8 and rgb[i + 2] < 8)
    lit = n - black
    if black < 0.3 * n: return f"saver not showing ({100 * black // n}% black)"
    if lit < 200: return f"saver shows nothing ({lit} lit px)"
    return ""


def parse(path):
    steps = []
    for line in open(path):
        line = line.split("#", 1)[0].strip()
        if not line: continue
        p = line.split()
        t = float(p[0]); cmd = p[1] if len(p) > 1 else ""
        if cmd == "uart0": steps.append((t, p[2]))
        elif cmd == "knob": steps.append((t, ("l" if p[2] == "cw" else "j") * int(p[3])))
        elif cmd == "press": steps.append((t, PRESS[p[2]]))
        elif cmd == "stop": steps.append((t, None))
        elif cmd == "key": steps.append((t, p[2]))       # hw scripts: any key string
    return steps


def scenarios(names):
    found = {}
    for d in (os.path.join(FW, "sim", "scripts"), os.path.join(HERE, "scripts")):
        for f in sorted(glob.glob(os.path.join(d, "*.txt"))):
            found[os.path.splitext(os.path.basename(f))[0]] = f
    if names: return [(n, found.get(n)) for n in names]
    return sorted(found.items())


def boot(b, port, factory=True):
    if factory:
        b.key("!"); time.sleep(0.05); b.key("Z")
        time.sleep(1.0); b.reopen(port)
    else:
        b.reset(); time.sleep(0.5); b.reopen(port)
    b.log = []; b.buf = b""
    ok = wait_ready(b, 12)
    if not ok and factory:          # no answer (hung?): hardware reset, then factory reset
        b.reset(); time.sleep(0.5); b.reopen(port)
        if wait_ready(b, 12):
            return boot(b, port, factory=True)
    b.pump(0.8)                     # let the first screen finish drawing
    return ok


def wait_ready(b, secs):
    """The boot lines can go out while the USB port is re-enumerating, so ask for the
    key-help line ('?') until it shows: then the input layer is certainly up."""
    end = time.time() + secs
    while time.time() < end:
        if b.wait_for(READY, 0.5): return True
        try: b.key("?")
        except Exception: b.reopen(b.s.port, 2)
    return False


def check_boot_log(log):
    text = "\n".join(log); probs = []
    for m in BOOT_MUST:
        if m not in text: probs.append(f"missing '{m}'")
    if CRASH.search(text): probs.append("crash: " + CRASH.search(text).group(0))
    for l in log:
        if BOOT_MUST_NOT.match(l): probs.append("error line: " + l)
    return probs


def run(args):
    b = Board(args.port)
    out = os.path.join(HERE, "out"); os.makedirs(out, exist_ok=True)
    fail = 0
    for name, path in scenarios(args.names):
        if not path:
            print(f"FAIL {name}: no such scenario"); fail = 1; continue
        src = open(path).read()
        if SIM_ONLY.search(src):
            print(f"SKIP {name}: simulator-only inputs (ADC ladder / register poke)"); continue
        if re.search(r"press sw (\d{4,}|[5-9]\d\d)\b", src):
            print(f"SKIP {name}: simulator-only press-and-turn timing (encoder held, accelerated detents)"); continue
        steps = parse(path)
        if not boot(b, args.port):
            print(f"FAIL {name}: board did not come up"); fail = 1; continue
        boot_log = list(b.log)
        t0 = time.time(); base = steps[0][0] if steps else 0
        for t, k in steps:
            delay = (t - base) - (time.time() - t0)
            if delay > 0: b.pump(delay)
            if k is None: break
            b.key(k)
        b.pump(0.6)
        log = b.log
        open(os.path.join(out, name + ".log"), "w").write("\n".join(log) + "\n")
        try:
            w, h, rgb = b.shot()
        except RuntimeError as e:
            open(os.path.join(out, name + ".log"), "a").write("\n".join(b.log) + "\n")
            print(f"FAIL {name}: {e}"); fail = 1; continue
        png = os.path.join(out, name + ".png")
        write_png(png, w, h, rgb)
        open(os.path.join(out, name + ".log"), "w").write("\n".join(log) + "\n")
        status, why = "PASS", ""
        probs = [p for p in check_boot_log(boot_log) if not p.startswith("missing")] + \
                ([("crash: " + CRASH.search("\n".join(log)).group(0))] if CRASH.search("\n".join(log)) else [])
        if probs: status, why = "FAIL", probs[0]
        gold = os.path.join(HERE, "golden", name + ".png")
        if status == "PASS" and name in ANIMATED:
            why = saver_check(w, h, rgb)
            status, why = ("FAIL", why) if why else ("PASS", "animated: saver-active check")
        elif status == "PASS":
            if args.update_golden:
                write_png(gold, w, h, rgb); why = "golden updated"
            elif os.path.exists(gold) and name in MARQUEE_ROWS:
                gw, gh, grgb = read_png_rgb(gold)
                cut = MARQUEE_ROWS[name] * w * 3
                lit = sum(1 for i in range(cut, len(rgb), 3) if rgb[i] > 110)   # text (dimmed ~123) vs texture (99, 49)
                if (gw, gh) != (w, h) or grgb[:cut] != rgb[:cut]:
                    status, why = "FAIL", f"screen above the marquee line differs (see hw/out/{name}.png)"
                elif lit < 50:
                    status, why = "FAIL", "marquee line shows no text"
                else:
                    why = "marquee line checked for text only"
            elif os.path.exists(gold):
                gw, gh, grgb = read_png_rgb(gold)
                if (gw, gh, grgb) != (w, h, rgb):
                    diff = sum(1 for i in range(0, len(rgb), 3) if rgb[i:i + 3] != grgb[i:i + 3]) if len(grgb) == len(rgb) else -1
                    status, why = "FAIL", f"screen differs from golden ({diff} px; see hw/out/{name}.png)"
            else:
                why = "no golden yet (run with --update-golden after checking hw/out)"
        print(f"{status} {name}{': ' + why if why else ''}", flush=True)
        if status != "PASS": fail = 1
    return fail


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=["boot", "shot", "run", "list", "key"])
    ap.add_argument("names", nargs="*")
    ap.add_argument("--port", default="/dev/ttyACM0")
    ap.add_argument("--update-golden", action="store_true")
    args = ap.parse_args()
    if args.cmd == "list":
        for n, p in scenarios([]): print(n, os.path.relpath(p, FW)); return 0
    if args.cmd == "run": return run(args)
    b = Board(args.port)
    if args.cmd == "boot":
        for attempt in range(3):     # early boot lines can be lost while USB re-enumerates
            b.reset(); time.sleep(0.5); b.reopen(args.port)
            ok = wait_ready(b, 12)
            b.key("?"); b.pump(1.0)      # the board reprints its status line ("board: ...")
            if ok and not [p for p in check_boot_log(b.log) if p.startswith("missing")]: break
        print("\n".join(b.log))
        probs = check_boot_log(b.log)
        print("BOOT", "PASS" if ok and not probs else "FAIL", "; ".join(probs))
        return 0 if ok and not probs else 1
    if args.cmd == "key":                       # hw/run.sh key G   (send raw keys)
        for k in "".join(args.names): b.key(k); time.sleep(0.15)
        b.pump(1.0); print("\n".join(b.log[-5:])); return 0
    if args.cmd == "shot":
        w, h, rgb = b.shot()
        path = args.names[0] if args.names else os.path.join(HERE, "out", "shot.png")
        os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
        write_png(path, w, h, rgb); print("wrote", path, w, h); return 0


if __name__ == "__main__":
    sys.exit(main())
