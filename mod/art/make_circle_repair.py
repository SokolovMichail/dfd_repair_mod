"""Рисует textures/cursor_decals/circle_repair.dds - скрещённые гаечный ключ и молоток для круга ремонта.

Стиль и формат - как у circle_spares (make_circle_spares.py): 64x64, DXT5, 7 мипов.

    python mod/art/make_circle_repair.py <выход.dds> [превью.png]
"""
import os
import sys

from PIL import Image, ImageChops, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_circle_spares import S, W, render, save_dds, shape_mask  # noqa: E402


def hammer_mask():
    """Молоток вертикально: боёк сверху, рукоять вниз."""
    m = Image.new('L', (W, W), 0)
    d = ImageDraw.Draw(m)
    s = lambda v: v * S
    d.rounded_rectangle([s(19), s(5), s(45), s(16)], radius=s(2), fill=255)     # боёк
    d.rectangle([s(42), s(7), s(48), s(14)], fill=255)                          # ударная часть
    d.rounded_rectangle([s(28.5), s(14), s(35.5), s(59)], radius=s(3), fill=255)  # рукоять
    return m


def shape_mask_repair():
    """Маска значка: гаечный ключ и молоток (по 86% размера), повёрнутые на ±45° и наложенные крест-накрест."""
    # ключ (головкой вверх) и молоток, повёрнутые навстречу друг другу; немного меньше, чтобы углы влезли
    wrench = shape_mask().resize((int(W * 0.86),) * 2, Image.LANCZOS)
    hammer = hammer_mask().resize((int(W * 0.86),) * 2, Image.LANCZOS)
    out = Image.new('L', (W, W), 0)
    for part, angle in ((wrench, 45), (hammer, -45)):
        layer = Image.new('L', (W, W), 0)
        layer.paste(part, ((W - part.width) // 2, (W - part.height) // 2))
        out = ImageChops.lighter(out, layer.rotate(angle, Image.BICUBIC))
    return out.point(lambda v: 255 if v > 127 else 0)


if __name__ == '__main__':
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    icon = render(shape_mask_repair())
    save_dds(icon, sys.argv[1])
    if len(sys.argv) > 2:
        prev = icon.resize((512, 512), Image.NEAREST)
        bg = Image.new('RGBA', prev.size, (60, 60, 60, 255))
        bg.alpha_composite(prev)
        bg.convert('RGB').save(sys.argv[2])
