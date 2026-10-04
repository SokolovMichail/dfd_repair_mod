"""Собирает папку мода HEMTT Spares из актуальных файлов установленной игры.

    python mod/build_mod.py --game <папка игры> --out <папка мода> [--cache <кэш>] [--version 1.0]

Новая машина Fnd_HEMTT_repair («HEMTT Spares») - копия HEMTT Supply (Fnd_HEMTT_ammunition), который не меняется.
Файлы игры каждый раз берутся заново (gamefiles.py: последний архив/папка слоя, где файл есть),
поэтому после обновления игры достаточно пересобрать.

Без плагина SparesSupply.dll машина работает только частично: ammunition_recharge рядом с support не
создаётся (нет кнопки F4), режим F4 не раздаёт запчасти; пополнение по клику работает.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, 'tools'))
from gamefiles import Game  # noqa: E402

UNIT = 'Fnd_HEMTT_repair'
TEMPLATE = 'Fnd_HEMTT_ammunition'
NAME = 'HEMTT Spares'
DESC = ('A versatile four-axle truck platform with space for various truck beds and modules. '
        'This version carries spare parts for field repairs.&#10;&#10;'
        'Can be quickly deployed into a supply depot that resupplies nearby vehicles with spare parts.&#10;&#10;'
        'Can tow light and heavy trailers except those requiring a fifth wheel mount.')
# запчасти (support, 10000) + ammunition_recharge (500) только ради кнопки F4 - патроны не раздаёт (плагин)
STATS = ['abilities=ammunition_recharge,support', 'capacity_ammo_recharger=500', 'spares=10000',
         'unit_set=light_vehicle',
         'tech_pic=vehicles_icons_small\\fnd_hemmit_spares',     # _preselected/_selected игра ищет сама
         'garrison_pic=vehicles_icons_big\\fnd_hemmit_spares']
# магазин кампании Resistance: 1 шт. в первом секторе + там же, где выдаётся HEMTT Supply
SHOP = [('sector_1_reward', f',{UNIT}:1'), ('sector_11_reward', f',{UNIT}'),
        ('sector_14_reward_1', f',{UNIT}'), ('sector_16_reward_2', f',{UNIT}')]

# что брать из игры: слой -> файлы
SOURCES = {
    'base': ['scripts/species/cars.xml', 'scripts/default_army.toml'] +
            [f'textures/ui/pictures/tech_pic/vehicles_icons_{v}' for v in (
                'big/fnd_hemmit_ammo.dds', 'small/fnd_hemmit_ammo.dds',
                'small/fnd_hemmit_ammo_preselected.dds', 'small/fnd_hemmit_ammo_selected.dds')],
    'resistance': ['scripts/species/cars.xml', 'scripts/species/encyclopedia_dlc.xml',
                   'scripts/species/shop_presets.xml'],
    'loc_en': ['locale/cars.xml', 'locale/encyclopedia_loc.xml'],
    'resistance_loc_en': ['locale/encyclopedia_dlc_loc.xml'],
}

MOD_JSON = {
    'name': NAME,
    'description': (
        'New vehicle HEMTT Spares - a HEMTT that resupplies spare parts to vehicles, '
        'the way HEMTT Supply does with ammunition.\n'
        'Deploy it with F4 (Supply Mode) to resupply all vehicles in the zone, or order it to resupply a single vehicle.\n\n'
        'Resistance campaign: one in the first sector shop, more in the same sector rewards as HEMTT Supply.\n'
        'Skirmish: available to Founders.\n\n'
        'Zone resupply needs the native plugin from the archive (bin\\winhttp.dll and bin\\plugins\\SparesSupply.dll). '
        'Single player only.'),
    'icon': 'basis/mod_icon.dds',
}

SPARES_MATERIAL = '''{
    "Colors": null,
    "Floats": {
        "segment_scale_x" : 0.6,
        "segment_scale_y" : 0.6,
        "segment_interval" : 2.0
    },
    "IsTransparent": true,
    "PS": "standard",
    "Textures": {
        "albedo": "textures/cursor_decals/circle_spares.dds"
    },
    "VS": "standard",
    "_Material": "StandardMaterial",
    "notAffectedTextureSetting": true
}
'''


def run(script, *args, env=None):
    env = dict(env or os.environ, PYTHONIOENCODING='utf-8')  # консоль CMake/cmd - не UTF-8
    subprocess.run([sys.executable, os.path.join(HERE, script), *args], check=True, env=env)


def table(src, dst, row, *edits):
    run('tools/patch_table.py', src, dst, row, *edits)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--game', required=True)
    ap.add_argument('--out', required=True, help='папка мода, например .../mods/HEMTT_Spares (пересоздаётся)')
    ap.add_argument('--cache', help='куда распаковать файлы игры (по умолчанию <out>/../_game_src)')
    ap.add_argument('--version', default='dev')
    a = ap.parse_args()

    out = os.path.abspath(a.out)
    cache = os.path.abspath(a.cache or os.path.join(out, '..', '_game_src'))
    game = Game(a.game)

    print('== game files')
    src = {}
    for layer, files in SOURCES.items():
        for rel in files:
            path, origin = game.extract(layer, rel, os.path.join(cache, layer))
            src[(layer, rel)] = path
            print(f'  {layer:18} {rel:70} <- {origin}')
    S = lambda layer, rel: src[(layer, rel)]

    if os.path.isdir(out):
        shutil.rmtree(out)
    os.makedirs(os.path.join(out, 'basis'))
    env = dict(os.environ, DFD_SRC=os.path.join(cache, 'base'), PYTHONIOENCODING='utf-8')

    print('== mod.json, icon')
    meta = dict(MOD_JSON, description=f'Version {a.version}.\n' + MOD_JSON['description'])
    with open(os.path.join(out, 'mod.json'), 'w', encoding='utf-8') as f:
        json.dump(meta, f, ensure_ascii=False, indent=4)
    run('art/make_mod_icon.py', os.path.join(out, 'basis', 'mod_icon.dds'), env=env)

    print('== unit icons, supply circle')
    run('art/make_unit_icons.py', os.path.join(out, 'basis', 'textures'), env=env)
    run('art/make_circle_spares.py', os.path.join(out, 'basis', 'textures', 'cursor_decals', 'circle_spares.dds'))
    mat = os.path.join(out, 'basis', 'materials', 'rounds', 'sparesRecharge.material')
    os.makedirs(os.path.dirname(mat))
    with open(mat, 'w', encoding='utf-8', newline='\n') as f:
        f.write(SPARES_MATERIAL)

    print('== Resistance: unit, encyclopedia, shop')
    res = os.path.join(out, 'dlc', 'Resistance', 'basis', 'scripts', 'species')
    os.makedirs(res)
    table(S('resistance', 'scripts/species/cars.xml'), os.path.join(res, 'cars.xml'), TEMPLATE, '--clone', UNIT, *STATS)
    table(S('resistance', 'scripts/species/encyclopedia_dlc.xml'), os.path.join(res, 'encyclopedia_dlc.xml'),
          TEMPLATE, '--clone', UNIT, 'image=vehicles_icons_big/fnd_hemmit_spares')
    shop = os.path.join(res, 'shop_presets.xml')
    shutil.copy(S('resistance', 'scripts/species/shop_presets.xml'), shop)
    for row, add in SHOP:
        table(shop, shop + '.tmp', row, f'cars+={add}')
        os.replace(shop + '.tmp', shop)

    print('== base game (skirmish): unit, Founders army')
    base = os.path.join(out, 'basis', 'scripts', 'species')
    os.makedirs(base)
    table(S('base', 'scripts/species/cars.xml'), os.path.join(base, 'cars.xml'), TEMPLATE, '--clone', UNIT, *STATS)
    run('tools/patch_army_toml.py', S('base', 'scripts/default_army.toml'),
        os.path.join(out, 'basis', 'scripts', 'default_army.toml'), f'add-car:founders:{TEMPLATE}:{UNIT}')

    print('== localization (en)')
    loc = os.path.join(out, 'localization', 'en', 'basis_en', 'locale')
    res_loc = os.path.join(out, 'dlc', 'Resistance', 'localization', 'en', 'basis_en', 'locale')
    os.makedirs(loc)
    os.makedirs(res_loc)
    table(S('loc_en', 'locale/cars.xml'), os.path.join(loc, 'cars.xml'), TEMPLATE, '--clone', UNIT, f'content={NAME}')
    table(S('loc_en', 'locale/encyclopedia_loc.xml'), os.path.join(loc, 'encyclopedia_loc.xml'),
          TEMPLATE, '--clone', UNIT, f'name={NAME}', f'description={DESC}')
    table(S('resistance_loc_en', 'locale/encyclopedia_dlc_loc.xml'), os.path.join(res_loc, 'encyclopedia_dlc_loc.xml'),
          TEMPLATE, '--clone', UNIT, f'name={NAME}', f'description={DESC}')

    print(f'built {out}')


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    main()
