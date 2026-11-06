"""Synthetic test frames: original drawings, no game content.

gbc.ppm  160x144  a Game Boy Color style scene: white sky (the transparent
                  areas the shader is about), a palette-coloured landscape,
                  black outlines, a text box, near-white pixels and a
                  brightness ramp.
gba.ppm  240x160  a Game Boy Advance style scene: full-colour gradients,
                  white UI, pure black, saturated colours.
gb.ppm   160x144  four-shade Game Boy palette.
"""
import math, random

def save(name, w, h, f):
    px = bytearray()
    for y in range(h):
        for x in range(w):
            px += bytes(f(x, y))
    open(name, "wb").write(b"P6\n%d %d\n255\n" % (w, h) + px)

def gbc(x, y):
    # text box at the bottom with a black border and white inside
    if 112 <= y < 140 and 6 <= x < 154:
        if y in (112, 139) or x in (6, 153):
            return (0, 0, 0)
        if 118 <= y < 134 and 12 <= x < 148 and (x // 3 + y // 4) % 5 == 0:
            return (24, 24, 64)          # "text"
        return (255, 255, 255)
    # brightness ramp strip near the top
    if y < 6:
        v = int(x / 159 * 255)
        return (v, v, v)
    if y < 10:
        return (248, 248, 240) if x % 2 else (232, 240, 255)   # near-white
    # sun
    if (x - 128) ** 2 + (y - 26) ** 2 < 120:
        return (255, 222, 64)
    # hills
    hill = 70 + 14 * math.sin(x / 13.0) + 6 * math.sin(x / 5.0)
    if y > hill:
        if abs(y - hill) < 1.2:
            return (0, 0, 0)
        return (88, 176, 72) if (x + y) % 7 else (56, 120, 48)
    # a little character with an outline
    if 40 <= x < 52 and 48 <= y < 64:
        if x in (40, 51) or y in (48, 63):
            return (0, 0, 0)
        return (232, 88, 80)
    return (255, 255, 255)            # sky

def gba(x, y):
    if y < 12:
        return (255, 255, 255) if (x // 8) % 2 else (240, 240, 248)
    if y >= 150:
        return (0, 0, 0)
    if x < 80:
        return (int(x / 79 * 255), int((y - 12) / 137 * 255), 128)
    if x < 160:
        h = (x - 80) / 80.0
        return tuple(int(255 * (0.5 + 0.5 * math.sin(6.283 * (h + k / 3.0)))) for k in range(3))
    v = int((y - 12) / 137 * 255)
    return (v, v, v) if (x // 4 + y // 4) % 2 else (255 - v, 255 - v, 255 - v)

GB = [(155, 188, 15), (139, 172, 15), (48, 98, 48), (15, 56, 15)]
def gb(x, y):
    hill = 90 + 10 * math.sin(x / 11.0)
    if y > hill:
        return GB[2] if (x + y) % 5 else GB[3]
    if 60 <= x < 76 and 40 <= y < 56:
        return GB[3] if x in (60, 75) or y in (40, 55) else GB[1]
    return GB[0]

def white(x, y):
    return (255, 255, 255)

save("gbc.ppm", 160, 144, gbc)
save("gba.ppm", 240, 160, gba)
save("gb.ppm", 160, 144, gb)
save("white.ppm", 160, 144, white)
