"""Builds two mods from the current files of the installed game:

    python mod/build_mod.py --game <game folder> --out <HEMTT_Spares folder> --framework-out <framework folder>
                            [--cache <cache>] [--version 1.0] [--tables whole|patch]

- the SparesSupply & Repair Framework mini-mod (--framework-out, mods\\SparesSupplyRepairFramework):
  only the materials and textures of the F4 zone circles the plugin draws the spare parts and repair zones with.
  It has no tables, so it conflicts with nothing; every vehicle built on the framework needs it;
- the HEMTT Spares example mod (--out, mods\\HEMTT_Spares): the vehicles themselves - tables, shops, army,
  encyclopedia, localization, icons. Without the framework mini-mod the circles of its vehicles are not drawn.

The new vehicles are copies of HEMTT Supply (Fnd_HEMTT_ammunition), which stays unchanged (the list is UNITS):
Fnd_HEMTT_spares ("HEMTT Spares", spare parts) and Fnd_HEMTT_repairer ("HEMTT Repair", repair and armor).
The game files are taken anew every time (gamefiles.py: the last archive/folder of the layer that has the file),
so after a game update a rebuild is enough.

Without the SparesSupply.dll plugin, ammunition_recharge next to support/repair is not created (no F4 button):
HEMTT Spares refills spare parts only on click, HEMTT Repair does nothing.

--tables chooses how HEMTT Spares carries its table edits:
- whole (default): each edited game table whole, as the game loads mod files - works everywhere, but it replaces
  the table, so another mod's copy of the same table and ours cannot both work;
- patch: for DefianceLoader's defiance.table-merge plugin - each table as <table>.hemtt_spares.patch.xml with only
  our rows (tools/make_table_patch.py), merged into the game's table as it loads. Data only: the vehicles, their
  encyclopedia articles and localization. Shop lists (shop_presets.xml, campaign rewards included) are single
  cells another mod may change too, and the skirmish army (default_army.toml) is not a table at all, so this build
  leaves both out: the vehicles are in the game, and getting them into shops or armies is up to other mods.
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

# Mod vehicles are copies of HEMTT Supply. ammunition_recharge gives them the F4 button and runs their zone;
# with capacity_ammo_recharger=0 they hand out no ammo. unit_set=light_vehicle - a test for now.
UNITS = [
    dict(
        sysname='Fnd_HEMTT_spares',   # Fnd_HEMTT_repair in version 0.1 (0.1 saves with this vehicle will not load)
        name='HEMTT Spares',
        icon='fnd_hemmit_spares', icon_kind='spares',
        desc=COMMON_DESC + 'This version carries spare parts for field repairs.&#10;&#10;'
             'Can be quickly deployed into a supply depot that resupplies nearby vehicles with spare parts.' + TOW_DESC,
        stats=['abilities=ammunition_recharge,support', 'capacity_ammo_recharger=0', 'spares=10000'],
        # Resistance campaign shop: 1 unit in the first sector + wherever HEMTT Supply is given out
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
        # repairs from its own stock of spare parts (plugin): 1 per 1 module health, 1 per 1 mm of armor
        stats=['abilities=ammunition_recharge,repair', 'capacity_ammo_recharger=0', 'spares=10000'],
        shop=[('sector_1_reward', ':1')],   # a test for now: 1 unit in the first sector
    ),
]
# Main campaign: all shops (except the test ones test_shop_* and the demo resistance_dlc_shop_demo) -
# one vehicle of each kind from UNITS.
BASE_SHOPS = ['taos_shop_1', 'taos_shop_2', 'taos_shop_3', 'albuquerque_shop', 'res_sup_base_0', 'res_sup_base_1',
              'vega_1_r', 'vega_1_m', 'tortuga_1', 'tortuga_2', 'tortuga_3', 'vega_2_r', 'vega_2_m',
              'resistance_camp', 'integrators_main_1', 'integrators_main_2', 'integrators_main_2a',
              'integrators_main_3', 'integrators_main_4', 'res_forward_post_0', 'integrators_scavengers_1',
              'integrators_scavengers_2', 'resistance_far_recon']
# DLC Canada (its own cars.xml, shops, encyclopedia): all shops - one vehicle of each kind.
CANADA_SHOPS = ['camp_1_shop', 'camp_2_shop', 'camp_3_shop', 'niagara_shop', 'resistance_camp']
for u in UNITS:
    u['stats'] = u['stats'] + ['unit_set=light_vehicle',
                               'tech_pic=vehicles_icons_small\\' + u['icon'],   # the game finds _preselected/_selected
                               'garrison_pic=vehicles_icons_big\\' + u['icon']]

# what to take from the game: layer -> files
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

# The example mod's folder in mods\. The table merge finds a mod's patches by it: <table>.<folder, lower case>.patch.xml.
MOD_FOLDER = 'HEMTT_Spares'
MOD_JSON = {
    'name': 'HEMTT Spares',
    # mod summary (~300 characters); the build prepends "Version X." to it
    'description': (
        'This mod adds two new mechanics: zone resupply of spare parts (like ammunition) and zone repair with '
        'armor restoration, as well as two new vehicles to accommodate them - HEMTT Spares and HEMTT Repair, '
        'respectively. Requires the SparesSupply & Repair Framework mod (included): enable both.'),
    'icon': 'basis/mod_icon.dds',
}
# --tables patch: appended to the description, so the mod list says what this build needs and lacks.
TABLEMERGE_NOTE = (' Table-merge build: needs DefianceLoader with defiance.table-merge enabled; the vehicles are not '
                   'added to shops or the skirmish army.')
# Framework mini-mod: only the zone circles (CIRCLES).
FRAMEWORK_JSON = {
    'name': 'SparesSupply & Repair Framework',
    'description': (
        'Zone circles for spare parts supply and repair vehicles. Needed by every mod built on the SparesSupply & '
        'Repair Framework (such as HEMTT Spares); the zone mechanics themselves come from its plugin. Adds no '
        'vehicles and changes no game tables.'),
    'icon': 'basis/mod_icon.dds',
}

# F4 zone circle materials (the plugin draws the circle with them instead of ammoRecharge): file name -> texture.
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
    """Run a project script (path relative to mod/) with the current Python; an error raises an exception."""
    env = dict(env or os.environ, PYTHONIOENCODING='utf-8')  # the CMake/cmd console is not UTF-8
    subprocess.run([sys.executable, os.path.join(HERE, script), *args], check=True, env=env)


def table(src, dst, row, *edits):
    """One edit of a game table via tools/patch_table.py: src -> dst, row row, edits edits."""
    run('tools/patch_table.py', src, dst, row, *edits)


def main():
    """Build the mod folder: extract the current game files, generate the graphics and the circle materials, patch
    the tables of all layers (base game, Resistance, Canada) and the localization, write mod.json."""
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--game', required=True)
    ap.add_argument('--out', required=True, help='mod folder, e.g. .../mods/HEMTT_Spares (recreated)')
    ap.add_argument('--framework-out', required=True,
                    help='framework mini-mod folder, e.g. .../mods/SparesSupplyRepairFramework (recreated)')
    ap.add_argument('--cache', help='where to extract the game files (default <out>/../_game_src)')
    ap.add_argument('--version', default='dev')
    ap.add_argument('--tables', choices=('whole', 'patch'), default='whole',
                    help='whole edited tables (default), or table-merge patches with the vehicles only')
    a = ap.parse_args()
    patches = a.tables == 'patch'

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

    fw = os.path.abspath(a.framework_out)
    for d in (out, fw):
        if os.path.isdir(d):
            shutil.rmtree(d)
        os.makedirs(os.path.join(d, 'basis'))
    env = dict(os.environ, DFD_SRC=os.path.join(cache, 'base'), PYTHONIOENCODING='utf-8')

    def mod_json(folder, meta):
        """The mod's mod.json: the description starts with "Version X."."""
        meta = dict(meta, description=f'Version {a.version}.\n' + meta['description'])
        with open(os.path.join(folder, 'mod.json'), 'w', encoding='utf-8') as f:
            json.dump(meta, f, ensure_ascii=False, indent=4)

    print('== framework: mod.json, icon, supply circles')
    mod_json(fw, FRAMEWORK_JSON)
    run('art/make_mod_icon.py', '--framework', os.path.join(fw, 'basis', 'mod_icon.dds'), env=env)
    decals = os.path.join(fw, 'basis', 'textures', 'cursor_decals')
    run('art/make_circle_spares.py', os.path.join(decals, 'circle_spares.dds'))
    run('art/make_circle_repair.py', os.path.join(decals, 'circle_repair.dds'))
    materials = os.path.join(fw, 'basis', 'materials', 'rounds')
    os.makedirs(materials)
    for name, texture in CIRCLES.items():
        with open(os.path.join(materials, f'{name}.material'), 'w', encoding='utf-8', newline='\n') as f:
            f.write(CIRCLE_MATERIAL.replace('{texture}', texture))

    print('== HEMTT Spares: mod.json, icon, unit icons' + (' (table-merge build)' if patches else ''))
    mod_json(out, dict(MOD_JSON, description=MOD_JSON['description'] + TABLEMERGE_NOTE) if patches else MOD_JSON)
    run('art/make_mod_icon.py', os.path.join(out, 'basis', 'mod_icon.dds'), env=env)
    textures = os.path.join(out, 'basis', 'textures')
    for u in UNITS:
        run('art/make_unit_icons.py', textures, '--kind', u['icon_kind'], env=env)

    # the table is replaced as a whole: each next edit goes on top of the result of the previous one
    def tables(src, dst, edits, shop=False):
        """Copy table src to dst and apply edits to the copy one by one (each is patch_table.py arguments, the row
        sysname first). --tables patch: the edited copy is only a step - what goes into the mod is its patch,
        <table>.<mod folder>.patch.xml beside where the table would be; shop tables are left out."""
        if patches and shop:
            print(f'  {os.path.basename(dst)}: left out (table-merge build adds no shops)')
            return
        full = dst + '.full' if patches else dst
        shutil.copy(src, full)
        for row, *args in edits:
            table(full, full + '.tmp', row, *args)
            os.replace(full + '.tmp', full)
        if patches:
            name = os.path.splitext(dst)[0] + f'.{MOD_FOLDER.lower()}.patch.xml'
            run('tools/make_table_patch.py', src, full, name)
            os.remove(full)

    clone = lambda extra: [(TEMPLATE, '--clone', u['sysname'], *extra(u)) for u in UNITS]

    print('== Resistance: units, encyclopedia, shop')
    res = os.path.join(out, 'dlc', 'Resistance', 'basis', 'scripts', 'species')
    os.makedirs(res)
    tables(S('resistance', 'scripts/species/cars.xml'), os.path.join(res, 'cars.xml'), clone(lambda u: u['stats']))
    tables(S('resistance', 'scripts/species/encyclopedia_dlc.xml'), os.path.join(res, 'encyclopedia_dlc.xml'),
           clone(lambda u: [f'image=vehicles_icons_big/{u["icon"]}']))
    tables(S('resistance', 'scripts/species/shop_presets.xml'), os.path.join(res, 'shop_presets.xml'),
           [(row, f'cars+=,{u["sysname"]}{count}') for u in UNITS for row, count in u['shop']], shop=True)

    print('== base game (main campaign, skirmish): units, shops, encyclopedia, Founders army')
    base = os.path.join(out, 'basis', 'scripts', 'species')
    os.makedirs(base)
    tables(S('base', 'scripts/species/cars.xml'), os.path.join(base, 'cars.xml'), clone(lambda u: u['stats']))
    tables(S('base', 'scripts/species/shop_presets.xml'), os.path.join(base, 'shop_presets.xml'),
           [(row, f'cars,+={u["sysname"]}') for row in BASE_SHOPS for u in UNITS], shop=True)
    tables(S('base', 'scripts/species/encyclopedia.xml'), os.path.join(base, 'encyclopedia.xml'),
           clone(lambda u: [f'image=vehicles_icons_big/{u["icon"]}']))
    if patches:  # not a table: the merge cannot patch it, and a whole copy would clash with other mods
        print('  default_army.toml: left out (table-merge build adds no skirmish army)')
    else:
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
           [(row, f'cars,+={u["sysname"]}') for row in CANADA_SHOPS for u in UNITS], shop=True)

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

    print(f'built {fw}')
    print(f'built {out}')


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    main()
