"""Упаковывает релиз HEMTT Spares: архив со структурой папки игры + обложка для публикации.

Отдельная версия (наш winhttp.dll + SparesSupply.dll):

    python mod/package.py --mod <собранная папка HEMTT_Spares> --framework-mod <собранная папка мини-мода фреймворка>
                          --winhttp <winhttp.dll> --plugin <SparesSupply.dll>
                          --game-src <распакованные файлы игры, слой base> --version 0.1 --out <папка dist>

Плагин DefianceLoader (--defiance: spares_repair.dll + его манифест, без winhttp.dll):

    python mod/package.py --defiance --mod <...> --framework-mod <...> --plugin <spares_repair.dll>
                          --manifest <spares_repair.plugin.json> --game-src <...> --version 0.1 --out <папка dist>

В архив идут оба мода: мини-мод фреймворка (круги зоны) - в mods/SparesSupplyRepairFramework, HEMTT Spares - в
mods/HEMTT_Spares.

Результат: <out>/HEMTT_Spares_v<версия>.zip (с --defiance - HEMTT_Spares_DefianceLoader_v<версия>.zip),
<out>/HEMTT_Spares_v<версия>_cover.png (1920x1080) и <out>/HEMTT_Spares_v<версия>_banner.png (1300x372) - обложка
и превью у обеих версий общие. README для игроков - docs/Dist-readme.md: блоки {IF STANDALONE} / {IF DEFIANCE}
... {END} попадают только в архив своей версии.
"""
import argparse
import os
import subprocess
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))


def pick_variant(text, variant):
    """Оставить в README блоки {IF <variant>} ... {END} этой версии, блоки другой выбросить (строки-маркеры -
    тоже). Текст вне блоков - общий для обеих версий."""
    out, keep = [], True
    for line in text.splitlines(keepends=True):
        tag = line.strip()
        if tag.startswith('{IF ') and tag.endswith('}'):
            keep = tag[4:-1] == variant
        elif tag == '{END}':
            keep = True
        elif keep:
            out.append(line)
    return ''.join(out)


def main():
    """Собрать релиз: архив со структурой папки игры (README - с подставленной версией), обложку 1920x1080
    и превью 1300x372."""
    ap = argparse.ArgumentParser()
    ap.add_argument('--defiance', action='store_true', help='упаковать плагин DefianceLoader, а не отдельную версию')
    ap.add_argument('--mod', required=True)
    ap.add_argument('--framework-mod', required=True, help='мини-мод фреймворка (круги зоны)')
    ap.add_argument('--winhttp', help='только для отдельной версии')
    ap.add_argument('--plugin', required=True)
    ap.add_argument('--manifest', help='только с --defiance: spares_repair.plugin.json')
    ap.add_argument('--game-src', required=True)
    ap.add_argument('--version', required=True)
    ap.add_argument('--out', required=True)
    a = ap.parse_args()
    if a.defiance and not a.manifest:
        ap.error('с --defiance нужен --manifest')
    if not a.defiance and not a.winhttp:
        ap.error('нужен --winhttp (или --defiance)')

    files = []  # (путь на диске, путь в архиве)

    def add_mod(folder, name):
        """Все файлы собранного мода folder - в архив как mods/<name>/..."""
        folder = os.path.abspath(folder)
        for dirpath, _, names in os.walk(folder):
            for n in sorted(names):
                p = os.path.join(dirpath, n)
                files.append((p, f'mods/{name}/' + os.path.relpath(p, folder).replace('\\', '/')))

    add_mod(a.framework_mod, 'SparesSupplyRepairFramework')
    add_mod(a.mod, 'HEMTT_Spares')
    if a.defiance:  # DefianceLoader берёт плагин вместе с манифестом из DefianceLoader\plugins
        files += [(a.plugin, 'DefianceLoader/plugins/spares_repair.dll'),
                  (a.manifest, 'DefianceLoader/plugins/spares_repair.plugin.json')]
    else:
        files += [(a.winhttp, 'bin/winhttp.dll'),
                  (a.plugin, 'bin/plugins/SparesSupply.dll')]
    files.append((os.path.join(HERE, '..', 'docs', 'Dist-readme.md'), 'HEMTT_Spares_README.txt'))
    missing = [p for p, _ in files if not os.path.isfile(p)]
    if missing:
        sys.exit('нет файлов: ' + ', '.join(missing))

    os.makedirs(a.out, exist_ok=True)
    name = 'HEMTT_Spares_DefianceLoader' if a.defiance else 'HEMTT_Spares'
    zip_path = os.path.join(a.out, f'{name}_v{a.version}.zip')
    with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for p, arc in files:
            if arc.endswith('README.txt'):  # версия и вариант README выбираются при упаковке
                text = pick_variant(open(p, encoding='utf-8').read(), 'DEFIANCE' if a.defiance else 'STANDALONE')
                z.writestr(arc, text.replace('{VERSION}', a.version).encode('utf-8'))
            else:
                z.write(p, arc)
            print(f'{os.path.getsize(p):>10}  {arc}')
    print(f'-> {zip_path} ({os.path.getsize(zip_path)} bytes)')

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
