"""Упаковывает релиз: архив со структурой папки игры (+ обложка для публикации). Четыре комплекта - фреймворк
отдельно или вместе с модом-примером HEMTT Spares, в отдельной версии или под DefianceLoader:

    python mod/package.py [--mod <собранная папка HEMTT_Spares>] --framework-mod <собранная папка мини-мода>
                          --winhttp <winhttp.dll> --plugin <SparesSupply.dll>
                          [--game-src <распакованные файлы игры, слой base>] --version 0.1 --out <папка dist>
    python mod/package.py --defiance [--mod <...>] --framework-mod <...> --plugin <spares_repair.dll>
                          --manifest <spares_repair.plugin.json> [--game-src <...>] --version 0.1 --out <папка dist>

- без --mod - только фреймворк: SparesSupplyRepairFramework_v<версия>.zip
  (с --defiance - SparesSupplyRepairFramework_DefianceLoader_v<версия>.zip): плагин + мини-мод фреймворка
  (круги зоны) в mods/SparesSupplyRepairFramework;
- с --mod - фреймворк + HEMTT Spares (mods/HEMTT_Spares): HEMTT_Spares_v<версия>.zip
  (HEMTT_Spares_DefianceLoader_v<версия>.zip), и ещё обложка <out>/HEMTT_Spares_v<версия>_cover.png (1920x1080)
  и превью _banner.png (1300x372) для страницы мода - для этого нужен --game-src.

README для игроков - docs/Dist-readme.md: блоки {IF STANDALONE} / {IF DEFIANCE} и {IF FRAMEWORK} / {IF EXAMPLE}
... {END} (могут быть вложены) попадают только в свой комплект.
"""
import argparse
import os
import subprocess
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
FRAMEWORK = 'SparesSupplyRepairFramework'


def pick_variant(text, tags):
    """Оставить в README блоки {IF <метка>} ... {END}, чья метка есть в tags, остальные выбросить (строки-маркеры -
    тоже). Блоки могут быть вложены: строка остаётся, только если подходят все блоки вокруг неё. Текст вне
    блоков - общий для всех комплектов."""
    out, stack = [], []
    for line in text.splitlines(keepends=True):
        tag = line.strip()
        if tag.startswith('{IF ') and tag.endswith('}'):
            stack.append(tag[4:-1] in tags)
        elif tag == '{END}':
            stack.pop()
        elif all(stack):
            out.append(line)
    if stack:
        sys.exit('README: {IF} без {END}')
    return ''.join(out)


def main():
    """Собрать комплект: архив со структурой папки игры (README - с подставленной версией); для комплекта с
    HEMTT Spares - ещё обложку 1920x1080 и превью 1300x372."""
    ap = argparse.ArgumentParser()
    ap.add_argument('--defiance', action='store_true', help='упаковать плагин DefianceLoader, а не отдельную версию')
    ap.add_argument('--mod', help='HEMTT Spares; без него - комплект только с фреймворком')
    ap.add_argument('--framework-mod', required=True, help='мини-мод фреймворка (круги зоны)')
    ap.add_argument('--winhttp', help='только для отдельной версии')
    ap.add_argument('--plugin', required=True)
    ap.add_argument('--manifest', help='только с --defiance: spares_repair.plugin.json')
    ap.add_argument('--game-src', help='нужен с --mod (обложка)')
    ap.add_argument('--version', required=True)
    ap.add_argument('--out', required=True)
    a = ap.parse_args()
    if a.defiance and not a.manifest:
        ap.error('с --defiance нужен --manifest')
    if not a.defiance and not a.winhttp:
        ap.error('нужен --winhttp (или --defiance)')
    if a.mod and not a.game_src:
        ap.error('с --mod нужен --game-src')

    files = []  # (путь на диске, путь в архиве)

    def add_mod(folder, name):
        """Все файлы собранного мода folder - в архив как mods/<name>/..."""
        folder = os.path.abspath(folder)
        for dirpath, _, names in os.walk(folder):
            for n in sorted(names):
                p = os.path.join(dirpath, n)
                files.append((p, f'mods/{name}/' + os.path.relpath(p, folder).replace('\\', '/')))

    add_mod(a.framework_mod, FRAMEWORK)
    if a.mod:
        add_mod(a.mod, 'HEMTT_Spares')
    if a.defiance:  # DefianceLoader берёт плагин вместе с манифестом из DefianceLoader\plugins
        files += [(a.plugin, 'DefianceLoader/plugins/spares_repair.dll'),
                  (a.manifest, 'DefianceLoader/plugins/spares_repair.plugin.json')]
    else:
        files += [(a.winhttp, 'bin/winhttp.dll'),
                  (a.plugin, 'bin/plugins/SparesSupply.dll')]
    base = 'HEMTT_Spares' if a.mod else FRAMEWORK
    files.append((os.path.join(HERE, '..', 'docs', 'Dist-readme.md'), f'{base}_README.txt'))
    # руководство для моддеров - во всех комплектах
    files.append((os.path.join(HERE, '..', 'docs', 'MODDING.md'), f'{FRAMEWORK}_MODDING.txt'))
    missing = [p for p, _ in files if not os.path.isfile(p)]
    if missing:
        sys.exit('нет файлов: ' + ', '.join(missing))

    tags = {'DEFIANCE' if a.defiance else 'STANDALONE', 'EXAMPLE' if a.mod else 'FRAMEWORK'}
    os.makedirs(a.out, exist_ok=True)
    zip_path = os.path.join(a.out, f'{base}{"_DefianceLoader" if a.defiance else ""}_v{a.version}.zip')
    with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for p, arc in files:
            if arc.endswith('README.txt'):  # версия и вариант README выбираются при упаковке
                text = pick_variant(open(p, encoding='utf-8').read(), tags)
                z.writestr(arc, text.replace('{VERSION}', a.version).encode('utf-8'))
            else:
                z.write(p, arc)
            print(f'{os.path.getsize(p):>10}  {arc}')
    print(f'-> {zip_path} ({os.path.getsize(zip_path)} bytes)')
    if not a.mod:
        return

    cover = os.path.join(a.out, f'HEMTT_Spares_v{a.version}_cover.png')
    tmp_dds = os.path.join(a.out, '_cover.dds')
    env = dict(os.environ, DFD_SRC=os.path.abspath(a.game_src), PYTHONIOENCODING='utf-8')
    subprocess.run([sys.executable, os.path.join(HERE, 'art', 'make_mod_icon.py'), tmp_dds, cover], check=True, env=env)
    os.remove(tmp_dds)
    print(f'-> {cover}')

    banner = os.path.join(a.out, f'HEMTT_Spares_v{a.version}_banner.png')  # превью 1300x372
    subprocess.run([sys.executable, os.path.join(HERE, 'art', 'make_mod_icon.py'), '--banner', banner], check=True,
                   env=env)
    print(f'-> {banner}')


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    main()
