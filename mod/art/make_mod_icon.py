"""Иконка мода для менеджера модов (1920x1080, DDS без сжатия, как в mods/mod_example_1).

    DFD_SRC=<файлы игры> python mod/art/make_mod_icon.py <выход.dds> [превью.png]

Две машины мода на силуэте HEMTT Supply из распакованных файлов игры (DFD_SRC): HEMTT Spares с ящиком
и HEMTT Repair с краном (грузы - из make_unit_icons.py), под каждой - круг зоны F4 своими значками
(make_circle_spares.py / make_circle_repair.py).
"""
import math
import os
import struct
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_circle_repair  # noqa: E402
import make_circle_spares  # noqa: E402
import make_unit_icons  # noqa: E402  (тоже требует DFD_SRC)

# Распакованные файлы игры (пути как в игре). Заполняет mod/build_mod.py, путь передаёт через DFD_SRC.
SRC_ROOT = os.environ.get('DFD_SRC') or sys.exit('DFD_SRC не задан: запускайте через mod/build_mod.py')
SRC = os.path.join(SRC_ROOT, 'textures', 'ui', 'pictures', 'tech_pic', 'vehicles_icons_big', 'fnd_hemmit_ammo.dds')
W, H = 1920, 1080
FONT = os.path.join(os.environ.get('SystemRoot', r'C:\Windows'), 'Fonts', 'bahnschrift.ttf')


def background():
    img = Image.new('RGBA', (W, H))
    d = ImageDraw.Draw(img)
    for y in range(H):
        t = y / H
        c = tuple(int(a + (b - a) * t) for a, b in zip((30, 40, 46), (12, 16, 19)))
        d.line([(0, y), (W, y)], fill=c + (255,))
    # диагональная штриховка, как на карте кампании
    hatch = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    hd = ImageDraw.Draw(hatch)
    for x in range(-H, W, 28):
        hd.line([(x, H), (x + H, 0)], fill=(255, 255, 255, 10), width=6)
    img.alpha_composite(hatch)
    # виньетка
    vig = Image.new('L', (W, H), 0)
    ImageDraw.Draw(vig).ellipse([-300, -250, W + 300, H + 250], fill=255)
    vig = vig.filter(ImageFilter.GaussianBlur(160))
    dark = Image.new('RGBA', (W, H), (0, 0, 0, 255))
    dark.putalpha(vig.point(lambda v: 255 - v))
    img.alpha_composite(dark)
    return img


def zone_ring(img, icon, cx, cy, rx, ry, n=16, size=64):
    """Круг зоны F4 из значков icon (RGBA), как в игре: значки по касательной, дальние - мельче и бледнее."""
    icon = icon.resize((size, size), Image.LANCZOS)
    for k in range(n):
        a = 2 * math.pi * k / n
        x, y = cx + rx * math.cos(a), cy + ry * math.sin(a)
        rot = icon.rotate(-math.degrees(a) - 90, resample=Image.BICUBIC, expand=True)
        scale = 0.78 + 0.22 * (math.sin(a) + 1) / 2
        rot = rot.resize((int(rot.width * scale), int(rot.height * scale)), Image.LANCZOS)
        if math.sin(a) < 0:
            r, g, b, al = rot.split()
            rot = Image.merge('RGBA', (r, g, b, al.point(lambda v: int(v * 0.7))))
        img.alpha_composite(rot, (int(x - rot.width / 2), int(y - rot.height / 2)))


def truck(img, cx, base_y, width, cargo_layer):
    """Силуэт HEMTT Supply без снарядов + груз (ящик или кран) из make_unit_icons, в крупном масштабе."""
    t = Image.open(SRC).convert('RGBA')
    d = ImageDraw.Draw(t)
    for x0, y0, x1, y1 in (make_unit_icons.ERASE, make_unit_icons.BOX):  # снаряды и место под груз
        d.rectangle([x0, y0, x1, y1], fill=(0, 0, 0, 0))
    k = width / t.getbbox()[2]  # масштаб значка 148x92 -> картинка
    t = t.resize((int(t.width * k), int(t.height * k)), Image.LANCZOS)
    # груз рисуется сразу в нужном масштабе: суперсэмплинг make_unit_icons = 2k, без уменьшения
    S, down = make_unit_icons.S, make_unit_icons.downscale
    make_unit_icons.S = max(2, round(k * 2))
    make_unit_icons.downscale = lambda layer, size: layer
    try:
        cargo = cargo_layer((148, 92), (0, 0))
    finally:
        make_unit_icons.S, make_unit_icons.downscale = S, down
    t.alpha_composite(cargo.convert('RGBa').resize(t.size, Image.LANCZOS).convert('RGBA'))
    t = t.crop(t.getbbox())
    # тень
    sh = Image.new('RGBA', t.size, (0, 0, 0, 0))
    sh.putalpha(t.getchannel('A').point(lambda v: int(v * 0.6)))
    sh = sh.filter(ImageFilter.GaussianBlur(12))
    img.alpha_composite(sh, (cx - t.width // 2 + 12, base_y - t.height + 16))
    img.alpha_composite(t, (cx - t.width // 2, base_y - t.height))


def font(size, weight):
    f = ImageFont.truetype(FONT, size)
    f.set_variation_by_name(weight)
    return f


def centered(img, x, y, s, f, fill, glow=False):
    d = ImageDraw.Draw(img)
    tw = d.textlength(s, font=f)
    if glow:
        g = Image.new('RGBA', (W, H), (0, 0, 0, 0))
        ImageDraw.Draw(g).text((x - tw / 2, y), s, font=f, fill=(0, 0, 0, 200))
        img.alpha_composite(g.filter(ImageFilter.GaussianBlur(12)))
    d.text((x - tw / 2, y), s, font=f, fill=fill)


def render():
    img = background()
    cream, amber = (236, 230, 214, 255), (214, 166, 98, 255)
    centered(img, W / 2, 60, 'HEMTT SPARES', font(170, 'Bold'), cream, glow=True)
    centered(img, W / 2, 255, 'SPARE PARTS SUPPLY  +  FIELD REPAIR', font(56, 'SemiBold'), amber)
    units = [  # центр, груз, значок круга, подпись
        (500, make_unit_icons.toolbox_layer, make_circle_spares.render(), 'SPARE PARTS'),
        (1420, make_unit_icons.crane_layer, make_circle_spares.render(make_circle_repair.shape_mask_repair()),
         'REPAIR & ARMOR'),
    ]
    cy = 730
    for cx, cargo, icon, label in units:
        zone_ring(img, icon, cx, cy, 400, 120)
        truck(img, cx, cy + 30, 700, cargo)
        centered(img, cx, cy + 190, label, font(54, 'SemiBold'), cream)
    return img


def save_dds(img, path):
    """A8R8G8B8 без сжатия, полная цепочка мипов - как mods/mod_example_1/basis/mod_icon.dds."""
    levels, cur = [img], img
    while cur.size != (1, 1):
        cur = cur.resize((max(1, cur.width // 2), max(1, cur.height // 2)), Image.LANCZOS)
        levels.append(cur)
    hdr = bytearray(128)
    hdr[0:4] = b'DDS '
    # size, flags(CAPS|HEIGHT|WIDTH|PITCH|PIXELFORMAT|MIPMAPCOUNT), h, w, pitch, depth, mips
    struct.pack_into('<7I', hdr, 4, 124, 0x2100F, img.height, img.width, img.width * 4, 0, len(levels))
    # pixel format: size, flags(ALPHAPIXELS|RGB), fourcc, bits, masks R G B A
    struct.pack_into('<8I', hdr, 76, 32, 0x41, 0, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
    struct.pack_into('<I', hdr, 108, 0x401008)  # COMPLEX | MIPMAP | TEXTURE
    with open(path, 'wb') as f:
        f.write(hdr)
        for lv in levels:
            r, g, b, a = lv.split()
            f.write(Image.merge('RGBA', (b, g, r, a)).tobytes())


if __name__ == '__main__':
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    icon = render()
    save_dds(icon, sys.argv[1])
    if len(sys.argv) > 2:
        icon.convert('RGB').save(sys.argv[2])
