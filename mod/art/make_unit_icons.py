"""Значки HEMTT Spares для интерфейса: из значков HEMTT Supply убираются снаряды и ставится ящик с инструментами.

    DFD_SRC=<файлы игры> python mod/art/make_unit_icons.py <папка textures мода> [превью.png]

Пишет (пути как в игре):
    ui/pictures/tech_pic/vehicles_icons_big/fnd_hemmit_spares.dds                 (garrison_pic, энциклопедия)
    ui/pictures/tech_pic/vehicles_icons_small/fnd_hemmit_spares{,_preselected,_selected}.dds   (tech_pic)
Формат - как у оригиналов (A8R8G8B8 без сжатия, без мипов): заголовок DDS копируется из исходника.
"""
import os
import struct
import sys

from PIL import Image, ImageDraw, ImageFilter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_circle_spares  # noqa: E402

# Распакованные файлы игры (пути как в игре). Заполняет mod/build_mod.py, путь передаёт через DFD_SRC.
SRC_ROOT = os.environ.get('DFD_SRC') or sys.exit('DFD_SRC не задан: запускайте через mod/build_mod.py')
SRC = os.path.join(SRC_ROOT, 'textures', 'ui', 'pictures', 'tech_pic')
S = 8  # суперсэмплинг

LIGHT = (200, 222, 230)
DETAIL = (134, 167, 182)
HALO = (26, 42, 51)

# (подпапка, суффикс, смещение относительно большого значка). Геометрия задана в координатах большого.
ICONS = [
    ('vehicles_icons_big', '', (0, 0)),
    ('vehicles_icons_small', '', (-6, -9)),
    ('vehicles_icons_small', '_preselected', (-6, -9)),
    ('vehicles_icons_small', '_selected', (-6, -9)),
]
ERASE = (61, 20, 97, 44)        # стойка со снарядами
BOX = (62, 33, 95, 48)          # ящик, низ заходит на бортик кузова
HANDLE = (73, 28.5, 84, 34)
LATCH_Y = 37
EMBLEM = (72, 40.5, 13, 4.5)    # x, y, ширина, высота ключа на крышке


def toolbox_layer(size, off):
    """RGBA-слой ящика в разрешении значка (рисуется с суперсэмплингом)."""
    w, h = size
    dx, dy = off
    P = lambda x, y: ((x + dx) * S, (y + dy) * S)
    mask = Image.new('L', (w * S, h * S), 0)
    md = ImageDraw.Draw(mask)
    md.rounded_rectangle([*P(BOX[0], BOX[1]), *P(BOX[2], BOX[3])], radius=1.2 * S, fill=255)
    md.rounded_rectangle([*P(*HANDLE[:2]), *P(*HANDLE[2:])], radius=1.0 * S, outline=255, width=int(1.6 * S))

    halo = mask.filter(ImageFilter.MaxFilter(2 * 2 * S + 1)).filter(ImageFilter.GaussianBlur(0.4 * S))
    layer = Image.new('RGBA', mask.size, HALO + (0,))
    layer.putalpha(halo.point(lambda v: v * 150 // 255))
    body = Image.new('RGBA', mask.size, LIGHT + (255,))
    body.putalpha(mask)
    layer.alpha_composite(body)

    d = ImageDraw.Draw(layer)
    d.line([*P(BOX[0] + 1.5, LATCH_Y), *P(BOX[2] - 1.5, LATCH_Y)], fill=DETAIL + (255,), width=int(0.9 * S))
    for x in (BOX[0] + 4, BOX[2] - 4):
        d.rectangle([*P(x - 1, LATCH_Y - 1.4), *P(x + 1, LATCH_Y + 1.4)], fill=DETAIL + (255,))
    ex, ey, ew, eh = EMBLEM
    wr = make_circle_spares.shape_mask().crop((22 * 8, 2 * 8, 42 * 8, 60 * 8))
    wr = wr.rotate(-90, expand=True).resize((int(ew * S), int(eh * S)), Image.LANCZOS)
    emblem = Image.new('RGBA', wr.size, DETAIL + (255,))
    emblem.putalpha(wr)
    layer.alpha_composite(emblem, tuple(int(v) for v in P(ex, ey)))
    # уменьшение с предумноженной альфой, чтобы края не темнели
    return layer.convert('RGBa').resize(size, Image.BOX).convert('RGBA')


def make(sub, suffix, off):
    src = os.path.join(SRC, sub, f'fnd_hemmit_ammo{suffix}.dds')
    img = Image.open(src).convert('RGBA')
    # прозрачность варианта (обычный маленький значок - 204/255): по самому светлому пикселю корпуса
    body_alpha = max(a for r, g, b, a in img.get_flattened_data() if (r, g, b) == LIGHT)
    dx, dy = off
    d = ImageDraw.Draw(img)
    for x0, y0, x1, y1 in (ERASE, BOX):  # снаряды и то, что окажется под ящиком (иначе просвечивает)
        d.rectangle([x0 + dx, y0 + dy, x1 + dx, y1 + dy], fill=(0, 0, 0, 0))
    layer = toolbox_layer(img.size, off)
    if body_alpha < 255:
        r, g, b, a = layer.split()
        layer = Image.merge('RGBA', (r, g, b, a.point(lambda v: v * body_alpha // 255)))
    img.alpha_composite(layer)
    header = open(src, 'rb').read(128)
    return img, header


def save(img, header, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    # порядок каналов - по маскам заголовка (у значков техники R = 0x000000FF, т.е. байты RGBA)
    rmask = struct.unpack_from('<I', header, 92)[0]
    r, g, b, a = img.split()
    data = (img if rmask == 0x000000FF else Image.merge('RGBA', (b, g, r, a))).tobytes()
    with open(path, 'wb') as f:
        f.write(header)
        f.write(data)


if __name__ == '__main__':
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    out_root = os.path.join(sys.argv[1], 'ui', 'pictures', 'tech_pic')
    made = []
    for sub, suffix, off in ICONS:
        img, header = make(sub, suffix, off)
        save(img, header, os.path.join(out_root, sub, f'fnd_hemmit_spares{suffix}.dds'))
        made.append(img)
    if len(sys.argv) > 2:
        z = 4
        W = sum(i.width * z + 24 for i in made)
        sheet = Image.new('RGBA', (W, max(i.height * z for i in made)), (45, 45, 45, 255))
        x = 0
        for i in made:
            sheet.alpha_composite(i.resize((i.width * z, i.height * z), Image.NEAREST), (x, 0))
            x += i.width * z + 24
        sheet.convert('RGB').save(sys.argv[2])
