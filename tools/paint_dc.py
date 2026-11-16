"""Paints the GeekQuestAI art sheets (assets/src/*.png) and writes lib/gamecore/src/dc_art.h (tile, sprite and
item ids). Run:  python tools/paint_dc.py && python tools/png2tiles.py

Everything is drawn with palette letters (tools/palette.py). The hero frames and the weather icons are taken
from the GeekQuest sheets kept in assets/base/.
"""
import os
from PIL import Image

from palette import PALETTE, CHAR_TO_INDEX

ROOT = os.path.join(os.path.dirname(__file__), "..")
SRC = os.path.join(ROOT, "assets", "src")
BASE = os.path.join(ROOT, "assets", "base")
RGB = [p[2] for p in PALETTE]
# GeekQuest palette (for reading the base sheets): the three colours that changed
OLD_RGB = {(46, 92, 40): 2, (82, 150, 58): 3, (140, 200, 75): 4}
OLD_RGB.update({rgb: i for i, rgb in enumerate(RGB) if i not in (2, 3, 4)})


class Cell:
    def __init__(self, fill="."):
        self.p = [[fill] * 16 for _ in range(16)]

    def px(self, x, y, c):
        if 0 <= x < 16 and 0 <= y < 16:
            self.p[y][x] = c
        return self

    def rect(self, x, y, w, h, c):
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                self.px(xx, yy, c)
        return self

    def frame(self, x, y, w, h, c):
        for xx in range(x, x + w):
            self.px(xx, y, c); self.px(xx, y + h - 1, c)
        for yy in range(y, y + h):
            self.px(x, yy, c); self.px(x + w - 1, yy, c)
        return self

    def dots(self, pts, c):
        for x, y in pts:
            self.px(x, y, c)
        return self

    def art(self, rows, x=0, y=0):
        for j, r in enumerate(rows):
            for i, ch in enumerate(r):
                if ch != " ":
                    self.px(x + i, y + j, ch)
        return self

    def copy(self):
        c = Cell()
        c.p = [r[:] for r in self.p]
        return c


def base_cell(sheet, idx, cols=8):
    img = Image.open(os.path.join(BASE, sheet)).convert("RGB")
    c = Cell()
    cx, cy = idx % cols * 16, idx // cols * 16
    for y in range(16):
        for x in range(16):
            c.p[y][x] = PALETTE[OLD_RGB[img.getpixel((cx + x, cy + y))]][0]
    return c


TILES, CHARS, ITEMS = [], [], []


def T(name, cell):
    TILES.append((name, cell)); return cell


def C(name, cell):
    CHARS.append((name, cell)); return cell


def I(name, cell):
    ITEMS.append((name, cell)); return cell


def speckle(c, col, seed, n):
    s = seed
    for _ in range(n):
        s = (s * 1103515245 + 12345) & 0x7FFFFFFF
        c.px((s >> 8) % 16, (s >> 16) % 16, col)
    return c


# ------------------------------------------------------------------ floors and walls
f = T("FLOOR", Cell("l")); f.rect(15, 0, 1, 16, "a"); f.rect(0, 15, 16, 1, "a"); f.px(1, 1, "o"); f.px(2, 1, "o")
v = T("VENT", f.copy())
for yy in range(3, 13, 3):
    for xx in range(3, 13, 3):
        v.rect(xx, yy, 2, 1, "a")
w = T("WALLTOP", Cell("d")); w.rect(0, 0, 16, 2, "k"); w.rect(0, 14, 16, 2, "k")
w = T("WALL", Cell("a")); w.rect(0, 0, 16, 1, "k"); w.rect(0, 12, 16, 3, "d"); w.rect(0, 15, 16, 1, "k"); w.rect(7, 1, 1, 11, "l")
d = T("DOOR", Cell("a")); d.rect(0, 0, 16, 1, "k"); d.rect(2, 1, 12, 15, "k"); d.rect(3, 2, 10, 14, "d"); d.rect(5, 4, 6, 4, "f"); d.px(11, 10, "Y"); d.rect(3, 15, 10, 1, "l")
s = T("STAIRS", Cell("a"))
for yy in range(0, 16, 4):
    s.rect(0, yy, 16, 1, "k"); s.rect(0, yy + 1, 16, 1, "o"); s.rect(0, yy + 2, 16, 2, "l")
s.rect(0, 0, 1, 16, "k"); s.rect(15, 0, 1, 16, "k")
c = T("CONCRETE", Cell("a")); speckle(c, "l", 7, 14); speckle(c, "d", 99, 6)
g = T("GRAVEL", Cell("s")); speckle(g, "B", 5, 18); speckle(g, "a", 31, 14); speckle(g, "o", 77, 6)
c = T("CARPET", Cell("w")); speckle(c, "d", 3, 7); speckle(c, "W", 19, 3)
a = T("ASPHALT", Cell("d")); speckle(a, "k", 21, 16); speckle(a, "a", 9, 5)
r = T("ROADLINE", a.copy()); r.rect(2, 7, 5, 2, "Y"); r.rect(10, 7, 5, 2, "Y")
wa = T("WATER_A", Cell("W")); wa.rect(2, 3, 5, 1, "f"); wa.rect(9, 8, 5, 1, "f"); wa.rect(3, 12, 4, 1, "f"); wa.rect(11, 1, 3, 1, "w")
wb = T("WATER_B", Cell("W")); wb.rect(4, 4, 5, 1, "f"); wb.rect(11, 9, 4, 1, "f"); wb.rect(1, 13, 4, 1, "f"); wb.rect(9, 2, 3, 1, "w")
T("SKY", Cell("f"))   # drawn by the code in the sky colour of the moment
du = T("DUCT", Cell("d")); du.rect(0, 0, 16, 1, "k"); du.rect(0, 0, 1, 16, "k"); du.dots([(3, 3), (12, 3), (3, 12), (12, 12)], "a")

# ------------------------------------------------------------------ racks (front view, 16 wide)
def rack_shell():
    c = Cell()
    c.rect(1, 0, 14, 16, "k"); c.rect(2, 1, 12, 14, "d"); c.rect(2, 1, 12, 1, "a")
    return c


def rack(unit, led_a, led_b, phase):
    c = rack_shell()
    for i, yy in enumerate(range(3, 14, 2)):
        c.rect(3, yy, 10, 1, unit)
        on = (i + phase) % 3 != 0
        c.px(11, yy, led_a if on else led_b)
        c.px(12, yy, led_a if (i + phase) % 2 else "k")
    return c


e = T("RACK_EMPTY", rack_shell()); e.rect(3, 2, 10, 12, "k"); e.rect(3, 2, 1, 12, "a"); e.rect(12, 2, 1, 12, "a")
T("RACK1_A", rack("a", "G", "Y", 0)); T("RACK1_B", rack("a", "G", "Y", 1))
T("RACK2_A", rack("W", "G", "f", 0)); T("RACK2_B", rack("W", "G", "f", 1))
for ph in (0, 1):
    c = T("RACK3_" + "AB"[ph], rack("o", "f", "W", ph)); c.rect(3, 2, 1, 12, "W"); c.rect(4, 2, 1, 12, "f" if ph else "W")
    c.px(4, 4 + ph * 3, "o"); c.px(4, 10 - ph * 3, "o")
o = T("RACK_OFF", rack("a", "r", "r", 0))
for yy in range(3, 14, 2):
    o.px(12, yy, "k")
sw = T("SWITCH_A", Cell()); sw.rect(0, 2, 16, 12, "k"); sw.rect(1, 3, 14, 10, "d")
for xx in range(2, 14, 3):
    sw.rect(xx, 5, 2, 2, "k"); sw.rect(xx, 9, 2, 2, "k"); sw.px(xx, 4, "G"); sw.px(xx + 1, 8, "G")
sb = T("SWITCH_B", sw.copy())
for xx in range(2, 14, 6):
    sb.px(xx, 4, "Y"); sb.px(xx + 4, 8, "k")
pp = T("PATCH", Cell()); pp.rect(0, 3, 16, 10, "k"); pp.rect(1, 4, 14, 8, "a")
for xx in range(2, 14, 2):
    pp.px(xx, 6, "k"); pp.px(xx, 9, "k")

# ------------------------------------------------------------------ dock
cr = T("CRATE", Cell()); cr.rect(1, 3, 14, 13, "k"); cr.rect(2, 4, 12, 11, "B"); cr.rect(2, 4, 12, 2, "s"); cr.rect(7, 4, 2, 11, "s"); cr.rect(10, 9, 3, 3, "o"); cr.px(11, 10, "k")
cp = T("CRATE_PANEL", Cell()); cp.rect(0, 6, 16, 10, "k"); cp.rect(1, 7, 14, 8, "B"); cp.rect(1, 7, 14, 1, "s"); cp.rect(3, 10, 6, 3, "W"); cp.rect(4, 11, 4, 1, "f")
pl = T("PALLET", Cell()); pl.rect(0, 9, 16, 7, "k")
for xx in (1, 6, 11):
    pl.rect(xx, 10, 4, 5, "B"); pl.rect(xx, 10, 4, 1, "s")
sh = T("SHUTTER", Cell("a")); sh.rect(0, 0, 16, 1, "k")
for yy in range(2, 16, 3):
    sh.rect(0, yy, 16, 1, "d")
sh.rect(0, 0, 1, 16, "k"); sh.rect(15, 0, 1, 16, "k")
# truck: 3 tiles wide, 2 high (cab on the left)
truck = [
    "................................................",
    "................kkkkkkkkkkkkkkkkkkkkkkkkkkkkkkk.",
    "................koooooooooooooooooooooooooooook.",
    "................koooooooooooooooooooooooooooook.",
    "....kkkkkkkkkkk.koooWWWoooWoooWWWoooooooooooook.",
    "...kooooooooook.kooWoooWooWooWoooWooooooooooook.",
    "..kofffffffook..kooWoooWooWooWoooWooooooooooook.",
    ".koffffffffook..kooWWWWWooWooWoooWooooooooooook.",
    ".koffffffffook..kooWoooWooWooWWWWWooooooooooook.",
    ".koffffffffook..kooWoooWooWooWoooWooooooooooook.",
    ".kooooooooooook.koooooooooooooooooooooooooooook.",
    ".koooooooooooook.kWWWWWWWWWWWWWWWWWWWWWWWWWWWWk.",
    ".kooooooooooooookkoooooooooooooooooooooooooooook",
    ".kYoooooooooooookkoooooooooooooooooooooooooooook",
    ".kooooooooooooookkaaaaaaaaaaaaaaaaaaaaaaaaaaaaak",
    ".kaaaaaaaaaaaaaakkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkk",
    ".kkkkkkkkkkkkkkkk....kkkkkk..........kkkkkk.....",
    "....kkkkkk..........kkaakkkk........kkaakkkk....",
    "...kkaakkkk.........kaaookkk........kaaookkk....",
    "...kaaookkk.........kaookkkk........kaookkkk....",
    "...kaookkkk.........kkkkkkkk........kkkkkkkk....",
    "...kkkkkkkk..........kkkkkk..........kkkkkk.....",
    "....kkkkkk......................................",
]
truck = truck + ["." * 48] * (32 - len(truck))
for ty in range(2):
    for tx in range(3):
        c = T("TRUCK_%d%d" % (ty, tx), Cell())
        c.art([row[tx * 16:tx * 16 + 16] for row in truck[ty * 16:ty * 16 + 16]])

# ------------------------------------------------------------------ water plant
def tower_top(ph):
    c = Cell()
    c.rect(2, 2, 12, 14, "k"); c.rect(3, 3, 10, 13, "a"); c.rect(3, 3, 3, 13, "l")
    c.rect(1, 1, 14, 5, "k"); c.rect(2, 2, 12, 3, "d")
    if ph == 0:
        c.rect(3, 3, 10, 1, "o")
    elif ph == 1:
        c.rect(7, 2, 2, 3, "o")
    return c


T("TOWER_TOP_A", tower_top(0)); T("TOWER_TOP_B", tower_top(1))
tb = T("TOWER_BOT", Cell()); tb.rect(2, 0, 12, 14, "k"); tb.rect(3, 0, 10, 13, "a"); tb.rect(3, 0, 3, 13, "l"); tb.rect(5, 6, 6, 5, "k"); tb.rect(6, 7, 4, 3, "W"); tb.px(7, 8, "f")
tb.rect(1, 13, 14, 2, "k")


def pump(light):
    c = Cell()
    c.rect(1, 9, 14, 6, "k"); c.rect(2, 10, 12, 4, "a")
    c.rect(3, 3, 9, 8, "k"); c.rect(4, 4, 7, 6, "W"); c.rect(4, 4, 7, 1, "f")
    c.rect(11, 5, 4, 4, "k"); c.rect(12, 6, 3, 2, "a")
    c.rect(6, 1, 3, 3, "k"); c.px(7, 2, light)
    return c


T("PUMP_OK", pump("G")); pb = T("PUMP_BAD", pump("r")); pb.dots([(13, 2), (14, 1), (12, 1), (2, 2), (1, 3)], "Y")
ph = T("PIPE_H", Cell()); ph.rect(0, 5, 16, 6, "k"); ph.rect(0, 6, 16, 4, "a"); ph.rect(0, 6, 16, 1, "o"); ph.rect(7, 5, 2, 6, "d")
pv = T("PIPE_V", Cell()); pv.rect(5, 0, 6, 16, "k"); pv.rect(6, 0, 4, 16, "a"); pv.rect(6, 0, 1, 16, "o"); pv.rect(5, 7, 6, 2, "d")


def valve(col):
    c = Cell()
    c.rect(0, 9, 16, 6, "k"); c.rect(0, 10, 16, 4, "a"); c.rect(0, 10, 16, 1, "o")
    c.rect(7, 5, 2, 5, "k")
    c.rect(3, 1, 10, 5, "k"); c.rect(4, 2, 8, 3, col); c.rect(6, 3, 4, 1, "k")
    return c


T("VALVE", valve("r")); T("VALVE_OK", valve("G"))

# ------------------------------------------------------------------ roof
def solar():
    c = Cell()
    c.rect(0, 1, 16, 14, "k"); c.rect(1, 2, 14, 12, "w")
    for xx in (5, 10):
        c.rect(xx, 2, 1, 12, "W")
    for yy in (5, 9):
        c.rect(1, yy, 14, 1, "W")
    c.dots([(2, 3), (3, 3), (2, 4)], "f")
    return c


so = T("SOLAR_OK", solar())
sd = T("SOLAR_DIRTY", solar()); speckle(sd, "s", 41, 26); speckle(sd, "B", 17, 12)
sd.rect(0, 1, 16, 1, "k"); sd.rect(0, 14, 16, 1, "k"); sd.rect(0, 1, 1, 14, "k"); sd.rect(15, 1, 1, 14, "k")
sc = T("SOLAR_CRACK", solar()); sc.dots([(4, 3), (5, 4), (6, 5), (7, 6), (7, 7), (8, 8), (9, 9), (9, 10), (10, 11), (11, 12), (6, 6), (5, 7), (4, 8), (9, 7), (10, 6), (11, 5)], "o")
sc.dots([(8, 7), (7, 8)], "k")
mo = T("MOUNT", Cell()); mo.rect(1, 3, 14, 1, "a"); mo.rect(1, 12, 14, 1, "a"); mo.rect(2, 3, 1, 10, "a"); mo.rect(13, 3, 1, 10, "a"); mo.dots([(2, 3), (13, 3), (2, 12), (13, 12)], "k")
iv = T("INVERTER", Cell()); iv.rect(2, 1, 12, 14, "k"); iv.rect(3, 2, 10, 12, "o"); iv.rect(3, 11, 10, 3, "a"); iv.art(["..YY", ".YY.", "YYYY", ".YY.", "YY.."], 6, 4); iv.px(11, 3, "G")
ac = T("AC", Cell()); ac.rect(1, 3, 14, 12, "k"); ac.rect(2, 4, 12, 10, "a"); ac.rect(2, 4, 12, 1, "o"); ac.rect(4, 6, 8, 6, "k"); ac.rect(5, 7, 6, 4, "d"); ac.rect(7, 7, 2, 4, "l"); ac.rect(5, 8, 6, 2, "l")
pa = T("PARAPET", Cell()); pa.rect(0, 8, 16, 8, "k"); pa.rect(0, 9, 16, 3, "o"); pa.rect(0, 12, 16, 3, "a")
for xx in (3, 11):
    pa.rect(xx, 12, 1, 3, "d")

# ------------------------------------------------------------------ office
cl = T("COT_L", Cell()); cl.rect(1, 3, 15, 11, "k"); cl.rect(2, 4, 14, 9, "o"); cl.rect(3, 5, 5, 6, "f"); cl.rect(10, 4, 6, 9, "W"); cl.rect(1, 14, 2, 2, "k")
cq = T("COT_R", Cell()); cq.rect(0, 3, 15, 11, "k"); cq.rect(0, 4, 14, 9, "W"); cq.rect(0, 4, 14, 1, "f"); cq.rect(4, 7, 3, 3, "w"); cq.rect(13, 14, 2, 2, "k")
de = T("DESK", Cell()); de.rect(0, 7, 16, 9, "k"); de.rect(1, 8, 14, 4, "B"); de.rect(1, 8, 14, 1, "s"); de.rect(1, 12, 2, 3, "b"); de.rect(13, 12, 2, 3, "b")
de.rect(3, 0, 10, 8, "k"); de.rect(4, 1, 8, 5, "w"); de.rect(5, 2, 4, 1, "G"); de.rect(5, 4, 6, 1, "f"); de.rect(5, 9, 6, 2, "a")
co = T("COFFEE", Cell()); co.rect(3, 1, 10, 14, "k"); co.rect(4, 2, 8, 12, "d"); co.rect(5, 3, 6, 3, "a"); co.px(10, 4, "r"); co.rect(6, 8, 4, 4, "k"); co.rect(7, 9, 2, 3, "o"); co.rect(4, 13, 8, 1, "a")
pt = T("PLANT", Cell()); pt.rect(5, 10, 6, 6, "k"); pt.rect(6, 11, 4, 4, "B"); pt.art(["..G..G..", ".GGG.GG.", "GGGGGGGG", ".GGGGGG.", "..GGGG..", "...GG..."], 4, 4); pt.dots([(6, 6), (9, 5)], "l")
wi = T("WINDOW", Cell("a")); wi.rect(0, 0, 16, 1, "k"); wi.rect(0, 12, 16, 3, "d"); wi.rect(0, 15, 16, 1, "k"); wi.rect(2, 2, 12, 9, "k"); wi.rect(3, 3, 10, 7, "."); wi.rect(7, 3, 1, 7, "k")
ch = T("CHARGER", Cell()); ch.rect(1, 9, 14, 6, "k")
for xx in range(2, 14, 4):
    ch.rect(xx, 10, 2, 4, "Y")
ch.rect(6, 2, 4, 8, "k"); ch.rect(7, 3, 2, 6, "a"); ch.px(7, 4, "G"); ch.px(8, 4, "G")

# ------------------------------------------------------------------ mission pieces
def jbox(col):
    c = Cell()
    c.rect(2, 2, 12, 13, "k"); c.rect(3, 3, 10, 11, "a"); c.rect(3, 3, 10, 1, "o"); c.rect(5, 6, 6, 5, "k"); c.rect(6, 7, 4, 3, col)
    c.px(11, 4, col)
    return c


T("JBOX", jbox("r")); T("JBOX_OK", jbox("G"))
sp = T("SPOOL", Cell()); sp.rect(2, 2, 12, 13, "k"); sp.rect(3, 3, 10, 11, "B"); sp.rect(3, 5, 10, 7, "Y"); sp.rect(3, 7, 10, 1, "B"); sp.rect(3, 10, 10, 1, "B"); sp.rect(7, 3, 2, 2, "k")
cn = T("CONE", Cell()); cn.art(["...kk...", "..krrk..", "..krrk..", ".koooook", ".krrrrk.", "krrrrrrk", "koooooo k", "krrrrrrk"], 4, 3); cn.rect(2, 11, 12, 3, "k"); cn.rect(3, 12, 10, 1, "r")
ba = T("BARREL", Cell()); ba.rect(3, 2, 10, 13, "k"); ba.rect(4, 3, 8, 11, "W"); ba.rect(4, 3, 2, 11, "f"); ba.rect(4, 6, 8, 1, "w"); ba.rect(4, 10, 8, 1, "w")


def console(col):
    c = Cell()
    c.rect(0, 2, 16, 13, "k"); c.rect(1, 3, 14, 11, "d"); c.rect(2, 4, 5, 4, "W"); c.rect(9, 4, 5, 4, "W"); c.rect(3, 5, 3, 1, "f"); c.rect(10, 5, 3, 1, "f")
    c.rect(6, 10, 4, 3, "k"); c.rect(7, 11, 2, 1, col); c.dots([(2, 11), (4, 11), (11, 11), (13, 11)], "a")
    return c


T("CONSOLE", console("r")); T("CONSOLE_OK", console("G"))
dw = T("DUCTWALL", Cell("k")); dw.rect(1, 1, 14, 14, "d"); dw.rect(1, 1, 14, 1, "a"); dw.rect(3, 6, 10, 1, "a"); dw.rect(3, 10, 10, 1, "a")

# ------------------------------------------------------------------ characters
for i in range(16):
    C("HERO_%d" % i, base_cell("chars_gq.png", i))

ROBOTS = {
    "CRATEBOT": ([
        "................",
        "................",
        "....kkkkkkkk....",
        "...kllllllllk...",
        "...klkkkkkklk...",
        "...klkGGGGklk...",
        "...klkkkkkklk...",
        "..kkllllllllkk..",
        ".kakYYYYYYYYkak.",
        ".kakllllllllkak.",
        ".kakaaaaaaaakak.",
        ".kk.kaaaaaak.kk.",
        "...kkkkkkkkkk...",
        "...kdkdkkdkdk...",
        "...kkkkkkkkkk...",
        "................",
    ], [(4, 13, "a"), (6, 13, "a"), (9, 13, "a"), (11, 13, "a")]),
    "SPIDER": ([
        "................",
        "................",
        "................",
        "..k...k..k...k..",
        "...k..k..k..k...",
        "....kkkkkkkk....",
        "...kddddddddk...",
        ".kkkdGGddGGdkkk.",
        "...kddddddddk...",
        "...kdaaaaaadk...",
        "....kkkkkkkk....",
        "...k..k..k..k...",
        "..k...k..k...k..",
        "................",
        "................",
        "................",
    ], [(2, 4, "k"), (13, 4, "k"), (2, 3, "."), (13, 3, "."), (2, 11, "k"), (13, 11, "k"), (2, 12, "."), (13, 12, ".")]),
    "SWEEPER": ([
        "................",
        "................",
        "................",
        "................",
        ".....kkkkkk.....",
        "...kkllllllkk...",
        "..kllllllllllk..",
        "..kllkGGGGkllk..",
        ".kalllkkkklllak.",
        ".kaallllllllaak.",
        "..kaaaaaaaaaak..",
        "..kWkaaaaaakWk..",
        "..kWWkkkkkkWWk..",
        "...kk......kk...",
        "................",
        "................",
    ], [(3, 12, "f"), (12, 12, "f"), (3, 11, "k"), (12, 11, "k")]),
    "DRONE": ([
        "................",
        "................",
        ".aaaaa....aaaaa.",
        "...k........k...",
        "...kk......kk...",
        "....kkkkkkkk....",
        "....kddddddk....",
        "....kdGGGGdk....",
        "....kddddddk....",
        "....kkkkkkkk....",
        "...kk......kk...",
        "...k........k...",
        ".aaaaa....aaaaa.",
        "................",
        "................",
        "................",
    ], [(1, 2, "."), (5, 2, "."), (10, 2, "."), (14, 2, "."), (1, 12, "."), (5, 12, "."), (10, 12, "."), (14, 12, ".")]),
    "OVERSEER": ([
        ".......kk.......",
        ".......kYk......",
        "....kkkkkkkk....",
        "...koooooooook..",
        "...kokkkkkkok...",
        "...kokGGGGkok...",
        "...kokkkkkkok...",
        "..kkoooooooookk.",
        ".kYkaaaaaaaakYk.",
        ".kakaoooooaakak.",
        ".kakaoYYYoaakak.",
        ".kk.kaoooak.kk..",
        "....kaaaaak.....",
        "....kak.kak.....",
        "...kkak.kakk....",
        "...kkkk.kkkk....",
    ], [(8, 1, "r"), (6, 10, "k"), (8, 10, "k")]),
}
for name in ("CRATEBOT", "SPIDER", "SWEEPER", "DRONE", "OVERSEER"):
    rows, alt = ROBOTS[name]
    a = C(name + "_A", Cell().art([r.replace(".", " ") for r in rows]))
    b = C(name + "_B", a.copy())
    for x, y, col in alt:
        b.px(x, y, col)

# ------------------------------------------------------------------ items
I("BUBBLE", base_cell("items_gq.png", 5)); I("ZZZ", base_cell("items_gq.png", 6)); I("ALERT", base_cell("items_gq.png", 35))
I("WRENCH", Cell().art(["kk..kk", "kak.kak", "kaakaak", ".kaaak.", "..kak..", "..kak..", "..kak..", "..kak..", "..kak..", "..kkk.."], 5, 3))
I("SNIPS", Cell().art(["k.....k", "kk...kk", ".kk.kk.", "..kkk..", "..kkk..", ".krkrk.", "kr.k.rk", "kr.k.rk", ".k...k."], 4, 3))
I("BROOM", Cell().art(["......kk", ".....kBk", "....kBk.", "...kBk..", "..kBk...", ".kYYk...", "kYYYYk..", "kYYYYk..", "kYkYkk..", ".k.k...."], 4, 3))
I("SPARK_A", Cell().art(["...Y...", ".Y.Y.Y.", "..YoY..", "YYoooYY", "..YoY..", ".Y.Y.Y.", "...Y..."], 4, 4))
I("SPARK_B", Cell().art(["Y.....Y", ".Y...Y.", "...o...", "..ooo..", "...o...", ".Y...Y.", "Y.....Y"], 4, 4))
for i in range(8):
    I("WX_%d" % i, base_cell("items_gq.png", 40 + i))
I("CARRY_CRATE", Cell().art(["kkkkkkkkkk", "kssssssssk", "kBBBssBBBk", "kBBBssBBBk", "kBBBssBBBk", "kBBBssBBBk", "kkkkkkkkkk"], 3, 5))
I("CARRY_PANEL", Cell().art(["kkkkkkkkkkkk", "kwwwWwwwWwwk", "kWWWWWWWWWWk", "kwwwWwwwWwwk", "kkkkkkkkkkkk"], 2, 7))
I("CARRY_SERVER", Cell().art(["kkkkkkkkkkkk", "kaaaaaaaaGGk", "kddddddddddk", "kaaaaaaaaGYk", "kkkkkkkkkkkk"], 2, 7))
I("CUP", Cell().art([".o.o..", "..o...", "kkkkkk.", "kooookk", "kooookk", "kooook.", ".kkkk.."], 5, 4))
I("CLIPBOARD", Cell().art(["..kkkk..", "kkkaakkk", "koooooook", "kokkkkok", "koooooook", "kokkkook", "koooooook", "kokkkkok", "koooooook", "kkkkkkkk"], 4, 3))
I("STEAM_A", Cell().art(["..oo....", ".oooo...", ".ooooo..", "..ooo.o.", "....ooo.", "...ooo..", "..oo....", "...o...."], 4, 4))
I("STEAM_B", Cell().art(["....oo..", "...oooo.", "..ooooo.", ".o.ooo..", ".ooo....", "..ooo...", "....oo..", "....o..."], 4, 4))
I("BOLT", Cell().art(["...kkkk", "..kYYk.", ".kYYk..", "kYYYkkk", "kkkYYYk", "..kYYk.", ".kYYk..", ".kYk...", "kkk...."], 5, 3))


# ------------------------------------------------------------------ output
def sheet(cells, cols, path):
    rows = (len(cells) + cols - 1) // cols
    img = Image.new("RGB", (cols * 16, rows * 16), RGB[0])
    for n, (_, c) in enumerate(cells):
        for y in range(16):
            for x in range(16):
                img.putpixel((n % cols * 16 + x, n // cols * 16 + y), RGB[CHAR_TO_INDEX[c.p[y][x]]])
    img.save(path)


def main():
    os.makedirs(SRC, exist_ok=True)
    sheet(TILES, 16, os.path.join(SRC, "tiles.png"))
    sheet(CHARS, 8, os.path.join(SRC, "chars.png"))
    sheet(ITEMS, 8, os.path.join(SRC, "items.png"))
    with open(os.path.join(ROOT, "lib", "gamecore", "src", "dc_art.h"), "w") as f:
        f.write("// GENERATED by tools/paint_dc.py - do not edit by hand.\n#pragma once\n")
        for pre, cells in (("T_", TILES), ("C_", CHARS), ("I_", ITEMS)):
            f.write("enum {\n")
            for n, (name, _) in enumerate(cells):
                f.write("  %s%s = %d,\n" % (pre, name, n))
            f.write("  %sCOUNT = %d\n};\n" % (pre, len(cells)))
    print("tiles %d, chars %d, items %d" % (len(TILES), len(CHARS), len(ITEMS)))


if __name__ == "__main__":
    main()
