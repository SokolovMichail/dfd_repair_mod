"""Собирает папку мода HEMTT Spares из актуальных файлов установленной игры.

    python mod/build_mod.py --game <папка игры> --out <папка мода> [--cache <кэш>] [--version 1.0]

Новые машины - копии HEMTT Supply (Fnd_HEMTT_ammunition), который не меняется (список - UNITS):
Fnd_HEMTT_spares («HEMTT Spares», запчасти) и Fnd_HEMTT_repairer («HEMTT Repair», ремонт и броня).
Файлы игры каждый раз берутся заново (gamefiles.py: последний архив/папка слоя, где файл есть),
поэтому после обновления игры достаточно пересобрать.

Без плагина SparesSupply.dll ammunition_recharge рядом с support/repair не создаётся (нет кнопки F4):
HEMTT Spares пополняет запчасти только по клику, HEMTT Repair не делает ничего.
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

TEMPLATE = 'Fnd_HEMTT_ammunition'
COMMON_DESC = ('A versatile four-axle truck platform with space for various truck beds and modules. ')
TOW_DESC = '&#10;&#10;Can tow light and heavy trailers except those requiring a fifth wheel mount.'

# Машины мода - копии HEMTT Supply. ammunition_recharge (500) у обеих только ради кнопки F4:
# патроны они не раздают (плагин). unit_set=light_vehicle - пока тест.
UNITS = [
    dict(
        sysname='Fnd_HEMTT_spares',   # в версии 0.1 - Fnd_HEMTT_repair (сохранения 0.1 с этой машиной не загрузятся)
        name='HEMTT Spares',
        icon='fnd_hemmit_spares', icon_kind='spares',
        desc=COMMON_DESC + 'This version carries spare parts for field repairs.&#10;&#10;'
             'Can be quickly deployed into a supply depot that resupplies nearby vehicles with spare parts.' + TOW_DESC,
        stats=['abilities=ammunition_recharge,support', 'capacity_ammo_recharger=500', 'spares=10000'],
        # магазин кампании Resistance: 1 шт. в первом секторе + там же, где выдаётся HEMTT Supply
        shop=[('sector_1_reward', ':1'), ('sector_11_reward', ''), ('sector_14_reward_1', ''),
              ('sector_16_reward_2', '')],
    ),
    dict(
        sysname='Fnd_HEMTT_repairer',
        name='HEMTT Repair',
        icon='fnd_hemmit_repair', icon_kind='repair',
        desc=COMMON_DESC + 'This version is a mobile repair workshop.&#10;&#10;'
             'Can be quickly deployed into a repair station that repairs damaged modules and restores the armor '
             'of nearby friendly vehicles, using its own stock of spare parts.' + TOW_DESC,
        # чинит из своего запаса запчастей (плагин): 1 за 1 здоровья модуля, 1 за 1 мм брони
        stats=['abilities=ammunition_recharge,repair', 'capacity_ammo_recharger=500', 'spares=10000'],
        shop=[('sector_1_reward', ':1')],   # пока тест: 1 шт. в первом секторе
    ),
]
# Основная кампания: все магазины (кроме тестовых test_shop_* и демо resistance_dlc_shop_demo) -
# по одной машине каждого вида из UNITS.
BASE_SHOPS = ['taos_shop_1', 'taos_shop_2', 'taos_shop_3', 'albuquerque_shop', 'res_sup_base_0', 'res_sup_base_1',
              'vega_1_r', 'vega_1_m', 'tortuga_1', 'tortuga_2', 'tortuga_3', 'vega_2_r', 'vega_2_m',
              'resistance_camp', 'integrators_main_1', 'integrators_main_2', 'integrators_main_2a',
              'integrators_main_3', 'integrators_main_4', 'res_forward_post_0', 'integrators_scavengers_1',
              'integrators_scavengers_2', 'resistance_far_recon']
# DLC Canada (свои cars.xml, магазины, энциклопедия): все магазины - по одной машине каждого вида.
CANADA_SHOPS = ['camp_1_shop', 'camp_2_shop', 'camp_3_shop', 'niagara_shop', 'resistance_camp']
for u in UNITS:
    u['stats'] = u['stats'] + ['unit_set=light_vehicle',
                               'tech_pic=vehicles_icons_small\\' + u['icon'],   # _preselected/_selected игра ищет сама
                               'garrison_pic=vehicles_icons_big\\' + u['icon']]

# что брать из игры: слой -> файлы
SOURCES = {
    'base': ['scripts/species/cars.xml', 'scripts/default_army.toml', 'scripts/species/shop_presets.xml',
             'scripts/species/encyclopedia.xml'] +
            [f'textures/ui/pictures/tech_pic/vehicles_icons_{v}' for v in (
                'big/fnd_hemmit_ammo.dds', 'small/fnd_hemmit_ammo.dds',
                'small/fnd_hemmit_ammo_preselected.dds', 'small/fnd_hemmit_ammo_selected.dds')],
    'resistance': ['scripts/species/cars.xml', 'scripts/species/encyclopedia_dlc.xml',
                   'scripts/species/shop_presets.xml'],
    'loc_en': ['locale/cars.xml', 'locale/encyclopedia_loc.xml'],
    'resistance_loc_en': ['locale/encyclopedia_dlc_loc.xml'],
    'canada': ['scripts/species/cars.xml', 'scripts/species/encyclopedia_dlc.xml', 'scripts/species/shop_presets.xml'],
    'canada_loc_en': ['locale/encyclopedia_dlc_loc.xml'],
}

MOD_JSON = {
    'name': 'HEMTT Spares',
    # аннотация мода (~300 символов); перед ней сборка дописывает «Version X.»
    'description': (
        'This mod adds two new mechanics: zone resupply of spare parts (like ammunition) and zone repair with '
        'armor restoration, as well as two new vehicles to accommodate them - HEMTT Spares and HEMTT Repair, '
        'respectively.'),
    'icon': 'basis/mod_icon.dds',
}

# Материалы кругов зоны F4 (плагин рисует ими круг вместо ammoRecharge): имя файла -> текстура.
CIRCLES = {'sparesRecharge': 'circle_spares', 'repairRecharge': 'circle_repair'}
CIRCLE_MATERIAL = '''{
    "Colors": null,
    "Floats": {
        "segment_scale_x" : 0.6,
        "segment_scale_y" : 0.6,
        "segment_interval" : 2.0
    },
    "IsTransparent": true,
    "PS": "standard",
    "Textures": {
        "albedo": "textures/cursor_decals/{texture}.dds"
    },
    "VS": "standard",
    "_Material": "StandardMaterial",
    "notAffectedTextureSetting": true
}
'''


def run(script, *args, env=None):
    """Запустить скрипт проекта (путь относительно mod/) текущим Python; ошибка - исключение."""
    env = dict(env or os.environ, PYTHONIOENCODING='utf-8')  # консоль CMake/cmd - не UTF-8
    subprocess.run([sys.executable, os.path.join(HERE, script), *args], check=True, env=env)


def table(src, dst, row, *edits):
    """Одна правка таблицы игры через tools/patch_table.py: src -> dst, строка row, правки edits."""
    run('tools/patch_table.py', src, dst, row, *edits)


def main():
    """Собрать папку мода: достать актуальные файлы игры, сгенерировать графику и материалы кругов, поправить
    таблицы всех слоёв (основная игра, Resistance, Canada) и локализацию, записать mod.json."""
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

    print('== unit icons, supply circles')
    textures = os.path.join(out, 'basis', 'textures')
    for u in UNITS:
        run('art/make_unit_icons.py', textures, '--kind', u['icon_kind'], env=env)
    decals = os.path.join(textures, 'cursor_decals')
    run('art/make_circle_spares.py', os.path.join(decals, 'circle_spares.dds'))
    run('art/make_circle_repair.py', os.path.join(decals, 'circle_repair.dds'))
    materials = os.path.join(out, 'basis', 'materials', 'rounds')
    os.makedirs(materials)
    for name, texture in CIRCLES.items():
        with open(os.path.join(materials, f'{name}.material'), 'w', encoding='utf-8', newline='\n') as f:
            f.write(CIRCLE_MATERIAL.replace('{texture}', texture))

    # таблица заменяется целиком: каждая следующая правка - поверх результата предыдущей
    def tables(src, dst, edits):
        """Скопировать таблицу src в dst и применить к копии правки edits по очереди (каждая - аргументы
        patch_table.py, первым идёт sysname строки)."""
        shutil.copy(src, dst)
        for row, *args in edits:
            table(dst, dst + '.tmp', row, *args)
            os.replace(dst + '.tmp', dst)

    clone = lambda extra: [(TEMPLATE, '--clone', u['sysname'], *extra(u)) for u in UNITS]

    print('== Resistance: units, encyclopedia, shop')
    res = os.path.join(out, 'dlc', 'Resistance', 'basis', 'scripts', 'species')
    os.makedirs(res)
    tables(S('resistance', 'scripts/species/cars.xml'), os.path.join(res, 'cars.xml'), clone(lambda u: u['stats']))
    tables(S('resistance', 'scripts/species/encyclopedia_dlc.xml'), os.path.join(res, 'encyclopedia_dlc.xml'),
           clone(lambda u: [f'image=vehicles_icons_big/{u["icon"]}']))
    tables(S('resistance', 'scripts/species/shop_presets.xml'), os.path.join(res, 'shop_presets.xml'),
           [(row, f'cars+=,{u["sysname"]}{count}') for u in UNITS for row, count in u['shop']])

    print('== base game (main campaign, skirmish): units, shops, encyclopedia, Founders army')
    base = os.path.join(out, 'basis', 'scripts', 'species')
    os.makedirs(base)
    tables(S('base', 'scripts/species/cars.xml'), os.path.join(base, 'cars.xml'), clone(lambda u: u['stats']))
    tables(S('base', 'scripts/species/shop_presets.xml'), os.path.join(base, 'shop_presets.xml'),
           [(row, f'cars,+={u["sysname"]}') for row in BASE_SHOPS for u in UNITS])
    tables(S('base', 'scripts/species/encyclopedia.xml'), os.path.join(base, 'encyclopedia.xml'),
           clone(lambda u: [f'image=vehicles_icons_big/{u["icon"]}']))
    run('tools/patch_army_toml.py', S('base', 'scripts/default_army.toml'),
        os.path.join(out, 'basis', 'scripts', 'default_army.toml'),
        *[f'add-car:founders:{TEMPLATE}:{u["sysname"]}' for u in UNITS])

    print('== Canada: units, encyclopedia, shops')
    can = os.path.join(out, 'dlc', 'Canada', 'basis', 'scripts', 'species')
    os.makedirs(can)
    tables(S('canada', 'scripts/species/cars.xml'), os.path.join(can, 'cars.xml'), clone(lambda u: u['stats']))
    tables(S('canada', 'scripts/species/encyclopedia_dlc.xml'), os.path.join(can, 'encyclopedia_dlc.xml'),
           clone(lambda u: [f'image=vehicles_icons_big/{u["icon"]}']))
    tables(S('canada', 'scripts/species/shop_presets.xml'), os.path.join(can, 'shop_presets.xml'),
           [(row, f'cars,+={u["sysname"]}') for row in CANADA_SHOPS for u in UNITS])

    print('== localization (en)')
    loc = os.path.join(out, 'localization', 'en', 'basis_en', 'locale')
    res_loc = os.path.join(out, 'dlc', 'Resistance', 'localization', 'en', 'basis_en', 'locale')
    os.makedirs(loc)
    os.makedirs(res_loc)
    article = lambda u: [f'name={u["name"]}', f'description={u["desc"]}']
    tables(S('loc_en', 'locale/cars.xml'), os.path.join(loc, 'cars.xml'), clone(lambda u: [f'content={u["name"]}']))
    tables(S('loc_en', 'locale/encyclopedia_loc.xml'), os.path.join(loc, 'encyclopedia_loc.xml'), clone(article))
    tables(S('resistance_loc_en', 'locale/encyclopedia_dlc_loc.xml'),
           os.path.join(res_loc, 'encyclopedia_dlc_loc.xml'), clone(article))
    can_loc = os.path.join(out, 'dlc', 'Canada', 'localization', 'en', 'basis_en', 'locale')
    os.makedirs(can_loc)
    tables(S('canada_loc_en', 'locale/encyclopedia_dlc_loc.xml'),
           os.path.join(can_loc, 'encyclopedia_dlc_loc.xml'), clone(article))

    print(f'built {out}')


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    main()
