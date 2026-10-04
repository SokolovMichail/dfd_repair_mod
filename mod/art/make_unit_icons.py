"""Значки машин мода для интерфейса: из значков HEMTT Supply убираются снаряды и ставится груз -
ящик с инструментами (HEMTT Spares, --kind spares) или кран с крюком (HEMTT Repair, --kind repair).

    DFD_SRC=<файлы игры> python mod/art/make_unit_icons.py <папка textures мода> [--kind spares|repair] [превью.png]

Пишет (пути как в игре; <имя> - fnd_hemmit_spares или fnd_hemmit_repair):
    ui/pictures/tech_pic/vehicles_icons_big/<имя>.dds                       (garrison_pic, энциклопедия)
    ui/pictures/tech_pic/vehicles_icons_small/<имя>{,_preselected,_selected}.dds   (tech_pic)
Формат - как у оригиналов (A8R8G8B8 без сжатия, без мипов): заголовок DDS копируется из исходника.
"""
import argparse
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
# кран: тумба на кузове, стрела назад-вверх, трос с крюком
CRANE_BASE = (64, 36, 73, 47)
CRANE_BOOM = ((68.5, 38), (93, 21.5))
CRANE_BOOM_W = 3.6
CRANE_ROPE = ((93, 21.5), (93, 33))
CRANE_HOOK = (90, 31.5, 96, 37.5)   # кольцо крюка (рисуется дугой)


def cargo_base(size, off, shape):
    """Общая часть слоя груза: маска shape(md, P) -> светлый силуэт с тёмным ореолом (с суперсэмплингом)."""
    w, h = size
    dx, dy = off
    P = lambda x, y: ((x + dx) * S, (y + dy) * S)
    mask = Image.new('L', (w * S, h * S), 0)
    shape(ImageDraw.Draw(mask), P)
    halo = mask.filter(ImageFilter.MaxFilter(2 * 2 * S + 1)).filter(ImageFilter.GaussianBlur(0.4 * S))
    layer = Image.new('RGBA', mask.size, HALO + (0,))
    layer.putalpha(halo.point(lambda v: v * 150 // 255))
    body = Image.new('RGBA', mask.size, LIGHT + (255,))
    body.putalpha(mask)
    layer.alpha_composite(body)
    return layer, P


def downscale(layer, size):
    # уменьшение с предумноженной альфой, чтобы края не темнели
    return layer.convert('RGBa').resize(size, Image.BOX).convert('RGBA')


def toolbox_layer(size, off):
    """RGBA-слой ящика в разрешении значка."""
    def shape(md, P):
        md.rounded_rectangle([*P(BOX[0], BOX[1]), *P(BOX[2], BOX[3])], radius=1.2 * S, fill=255)
        md.rounded_rectangle([*P(*HANDLE[:2]), *P(*HANDLE[2:])], radius=1.0 * S, outline=255, width=int(1.6 * S))

    layer, P = cargo_base(size, off, shape)
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
    return downscale(layer, size)


def crane_layer(size, off):
    """RGBA-слой крана в разрешении значка."""
    def shape(md, P):
        md.rounded_rectangle([*P(*CRANE_BASE[:2]), *P(*CRANE_BASE[2:])], radius=1.2 * S, fill=255)
        (x0, y0), (x1, y1) = CRANE_BOOM
        md.line([*P(x0, y0), *P(x1, y1)], fill=255, width=int(CRANE_BOOM_W * S))
        for x, y in CRANE_BOOM:
            r = CRANE_BOOM_W / 2
            md.ellipse([*P(x - r, y - r), *P(x + r, y + r)], fill=255)
        md.line([*P(*CRANE_ROPE[0]), *P(*CRANE_ROPE[1])], fill=255, width=int(1.0 * S))
        md.arc([*P(*CRANE_HOOK[:2]), *P(*CRANE_HOOK[2:])], start=-60, end=200, fill=255, width=int(1.5 * S))

    layer, P = cargo_base(size, off, shape)
    d = ImageDraw.Draw(layer)
    # поворотный круг тумбы и ось стрелы
    bx0, by0, bx1, by1 = CRANE_BASE
    d.line([*P(bx0 + 1.2, by0 + 6), *P(bx1 - 1.2, by0 + 6)], fill=DETAIL + (255,), width=int(0.9 * S))
    (x0, y0), _ = CRANE_BOOM
    d.ellipse([*P(x0 - 1, y0 - 1), *P(x0 + 1, y0 + 1)], fill=DETAIL + (255,))
    return downscale(layer, size)


KINDS = {'spares': ('fnd_hemmit_spares', toolbox_layer), 'repair': ('fnd_hemmit_repair', crane_layer)}


def make(sub, suffix, off, cargo_layer=toolbox_layer):
    src = os.path.join(SRC, sub, f'fnd_hemmit_ammo{suffix}.dds')
    img = Image.open(src).convert('RGBA')
    # прозрачность варианта (обычный маленький значок - 204/255): по самому светлому пикселю корпуса
    body_alpha = max(a for r, g, b, a in img.get_flattened_data() if (r, g, b) == LIGHT)
    dx, dy = off
    d = ImageDraw.Draw(img)
    for x0, y0, x1, y1 in (ERASE, BOX):  # снаряды и то, что окажется под ящиком (иначе просвечивает)
        d.rectangle([x0 + dx, y0 + dy, x1 + dx, y1 + dy], fill=(0, 0, 0, 0))
    layer = cargo_layer(img.size, off)
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
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('textures')
    ap.add_argument('--kind', choices=KINDS, default='spares')
    ap.add_argument('preview', nargs='?')
    a = ap.parse_intermixed_args()
    name, cargo = KINDS[a.kind]
    out_root = os.path.join(a.textures, 'ui', 'pictures', 'tech_pic')
    made = []
    for sub, suffix, off in ICONS:
        img, header = make(sub, suffix, off, cargo)
        save(img, header, os.path.join(out_root, sub, f'{name}{suffix}.dds'))
        made.append(img)
    if a.preview:
        z = 4
        W = sum(i.width * z + 24 for i in made)
        sheet = Image.new('RGBA', (W, max(i.height * z for i in made)), (45, 45, 45, 255))
        x = 0
        for i in made:
            sheet.alpha_composite(i.resize((i.width * z, i.height * z), Image.NEAREST), (x, 0))
            x += i.width * z + 24
        sheet.convert('RGB').save(a.preview)
