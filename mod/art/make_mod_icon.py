"""Иконка мода для менеджера модов (1920x1080, DDS без сжатия, как в mods/mod_example_1).

    DFD_SRC=<файлы игры> python mod/art/make_mod_icon.py <выход.dds> [превью.png]

Берёт силуэт HEMTT Supply из распакованных файлов игры (DFD_SRC) и рисует
под ним «зону снабжения» из гаечных ключей (значок из make_circle_spares.py).
"""
import math
import os
import struct
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_circle_spares  # noqa: E402

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


def wrench_ring(img, cx, cy, rx, ry, n=22, size=74):
    icon = make_circle_spares.render().resize((size, size), Image.LANCZOS)
    for k in range(n):
        a = 2 * math.pi * k / n
        x, y = cx + rx * math.cos(a), cy + ry * math.sin(a)
        # значок смотрит по касательной к окружности, как в игре; дальние - бледнее
        rot = icon.rotate(-math.degrees(a) - 90, resample=Image.BICUBIC, expand=True)
        scale = 0.78 + 0.22 * (math.sin(a) + 1) / 2
        rot = rot.resize((int(rot.width * scale), int(rot.height * scale)), Image.LANCZOS)
        if math.sin(a) < 0:
            r, g, b, al = rot.split()
            rot = Image.merge('RGBA', (r, g, b, al.point(lambda v: int(v * 0.7))))
        img.alpha_composite(rot, (int(x - rot.width / 2), int(y - rot.height / 2)))


def truck(img, cx, base_y, width):
    t = Image.open(SRC).convert('RGBA')
    # в кузове HEMTT Supply - снаряды на стойке; убираем их (координаты исходника 148x92)
    ImageDraw.Draw(t).rectangle([61, 20, 97, 44], fill=(0, 0, 0, 0))
    bbox = t.getbbox()
    t = t.crop(bbox)
    k = width / t.width
    t = t.resize((width, int(t.height * k)), Image.LANCZOS)
    # ...и ставим ящик с инструментами в той же плоской манере
    d = ImageDraw.Draw(t)
    X = lambda x: (x - bbox[0]) * k
    Y = lambda y: (y - bbox[1]) * k
    light, detail = (200, 222, 230, 255), (112, 138, 150, 255)
    d.rounded_rectangle([X(62), Y(33), X(95), Y(48)], radius=k * 1.2, fill=light)
    d.rounded_rectangle([X(73), Y(28.5), X(84), Y(34)], radius=k * 1.0, outline=light, width=int(k * 1.6))
    d.line([X(63.5), Y(37), X(93.5), Y(37)], fill=detail, width=int(k * 0.9))
    for x in (66, 91):
        d.rectangle([X(x - 1), Y(35.6), X(x + 1), Y(38.4)], fill=detail)
    # эмблема: гаечный ключ на крышке
    wr = make_circle_spares.shape_mask().crop((22 * 8, 2 * 8, 42 * 8, 60 * 8))
    wr = wr.rotate(-90, expand=True).resize((int(k * 13), int(k * 4.5)), Image.LANCZOS)
    emblem = Image.new('RGBA', wr.size, detail)
    emblem.putalpha(wr)
    t.alpha_composite(emblem, (int(X(72)), int(Y(40.5))))
    h = t.height
    # тень
    sh = Image.new('RGBA', t.size, (0, 0, 0, 0))
    sh.putalpha(t.getchannel('A').point(lambda v: int(v * 0.6)))
    sh = sh.filter(ImageFilter.GaussianBlur(14))
    img.alpha_composite(sh, (cx - width // 2 + 14, base_y - h + 18))
    img.alpha_composite(t, (cx - width // 2, base_y - h))


def text(img):
    d = ImageDraw.Draw(img)
    title = ImageFont.truetype(FONT, 190)
    title.set_variation_by_name('Bold')
    sub = ImageFont.truetype(FONT, 60)
    sub.set_variation_by_name('SemiBold')
    t, s = 'HEMTT SPARES', 'SPARE PARTS SUPPLY TRUCK'
    tw = d.textlength(t, font=title)
    sw = d.textlength(s, font=sub)
    glow = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    ImageDraw.Draw(glow).text(((W - tw) / 2, 70), t, font=title, fill=(0, 0, 0, 200))
    img.alpha_composite(glow.filter(ImageFilter.GaussianBlur(12)))
    d.text(((W - tw) / 2, 70), t, font=title, fill=(236, 230, 214, 255))
    d.text(((W - sw) / 2, 285), s, font=sub, fill=(214, 166, 98, 255))


def render():
    img = background()
    cx, cy = W // 2, 800
    wrench_ring(img, cx, cy, 760, 190)
    truck(img, cx, cy + 40, 1060)
    text(img)
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
