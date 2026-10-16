"""Рисует textures/cursor_decals/circle_spares.dds - значок гаечного ключа для круга запчастей.

Стиль скопирован с circle_ammo.dds (64x64, DXT5, 7 мипов): мягкая чёрная тень, тёмный контур,
светлый ободок, полупрозрачная бежевая заливка с вертикальным градиентом.

    python mod/art/make_circle_spares.py <выход.dds> [превью.png]
"""
import io
import os
import struct
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

N = 64      # итоговый размер
S = 8       # суперсэмплинг
W = N * S


def shape_mask():
    """Ключ вертикально: открытый зев сверху, рукоять вниз, скруглённый конец."""
    m = Image.new('L', (W, W), 0)
    d = ImageDraw.Draw(m)
    s = lambda v: v * S
    # головка
    cx, cy, r = 32, 15, 11.5
    d.ellipse([s(cx - r), s(cy - r), s(cx + r), s(cy + r)], fill=255)
    # шейка + рукоять
    d.rounded_rectangle([s(27.5), s(18), s(36.5), s(58)], radius=s(4.5), fill=255)
    # зев: вырез сверху, слегка расширяется кверху
    d.polygon([(s(28.5), s(2)), (s(35.5), s(2)), (s(35), s(14)), (s(29), s(14))], fill=0)
    d.ellipse([s(29), s(11), s(35), s(17)], fill=0)
    # отверстие на конце рукояти
    d.ellipse([s(30), s(50), s(34), s(54)], fill=0)
    return m


def grow(m, px):
    """Расширить маску на px пикселей итогового размера (MaxFilter в суперсэмплинге) - для контура и тени."""
    k = 2 * int(px * S) + 1
    return m.filter(ImageFilter.MaxFilter(k)) if k > 1 else m


def shrink(m, px):
    """Сузить маску на px пикселей итогового размера - для внутренней заливки."""
    k = 2 * int(px * S) + 1
    return m.filter(ImageFilter.MinFilter(k)) if k > 1 else m


def render(M=None):
    """Значок по маске формы (по умолчанию - ключ) в стиле circle_ammo."""
    M = M if M is not None else shape_mask()
    inner = shrink(M, 2)                      # заливка
    outline = grow(M, 2)                      # тёмный контур
    shadow = grow(M, 5).filter(ImageFilter.GaussianBlur(3 * S))
    f = lambda im: np.asarray(im, dtype=np.float32) / 255.0
    m, inn, out, sh = f(M), f(inner), f(outline), f(shadow)

    rgb = np.zeros((W, W, 3), np.float32)
    a = np.zeros((W, W), np.float32)
    # тень
    a = np.maximum(a, sh * 90)
    # контур
    sel = out > 0.5
    rgb[sel] = (16, 12, 8)
    a[sel] = 190
    # ободок
    sel = m > 0.5
    rgb[sel] = (205, 160, 98)
    a[sel] = 203
    # заливка с градиентом (как у патрона: сверху светлее и плотнее)
    t = np.linspace(0, 1, W, dtype=np.float32)[:, None]
    top, bot = np.array((247, 190, 115), np.float32), np.array((214, 162, 90), np.float32)
    grad = top + (bot - top) * t[..., None]
    sel = inn > 0.5
    rgb[sel] = np.broadcast_to(grad, (W, W, 3))[sel]
    a[sel] = np.broadcast_to(140 + (72 - 140) * t, (W, W))[sel]

    big = Image.fromarray(np.dstack([rgb, a]).clip(0, 255).astype(np.uint8), 'RGBA')
    return big.resize((N, N), Image.LANCZOS)


def dxt5_payload(img):
    """Сжать картинку в DXT5 и вернуть только данные - без 128-байтного заголовка DDS."""
    buf = io.BytesIO()
    img.save(buf, 'DDS', pixel_format='DXT5')
    return buf.getvalue()[128:]


def save_dds(img, path):
    """Записать DDS DXT5 с полной цепочкой мипов, как у игровых circle_*.dds: заголовок от Pillow с
    исправленными флагами и числом мипов, затем все уровни подряд."""
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    levels, cur = [], img
    while True:
        levels.append(cur)
        if cur.size == (1, 1):
            break
        cur = cur.resize((max(1, cur.size[0] // 2), max(1, cur.size[1] // 2)), Image.LANCZOS)
    b = io.BytesIO()
    img.save(b, 'DDS', pixel_format='DXT5')
    hdr = bytearray(b.getvalue()[:128])
    # dwFlags |= DDSD_MIPMAPCOUNT, dwMipMapCount, dwCaps |= COMPLEX|MIPMAP
    flags = struct.unpack_from('<I', hdr, 8)[0] | 0x20000
    struct.pack_into('<I', hdr, 8, flags)
    struct.pack_into('<I', hdr, 28, len(levels))
    struct.pack_into('<II', hdr, 20, len(dxt5_payload(img)), 1)  # linear size верхнего уровня, depth - как в оригинале
    caps = struct.unpack_from('<I', hdr, 108)[0] | 0x8 | 0x400000
    struct.pack_into('<I', hdr, 108, caps)
    with open(path, 'wb') as f:
        f.write(hdr)
        for lv in levels:
            f.write(dxt5_payload(lv))


if __name__ == '__main__':
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    icon = render()
    save_dds(icon, sys.argv[1])
    if len(sys.argv) > 2:
        prev = icon.resize((512, 512), Image.NEAREST)
        bg = Image.new('RGBA', prev.size, (60, 60, 60, 255))
        bg.alpha_composite(prev)
        bg.convert('RGB').save(sys.argv[2])
