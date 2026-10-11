"""Упаковывает релиз HEMTT Spares: архив со структурой папки игры + обложка для публикации.

    python mod/package.py --mod <собранная папка мода> --winhttp <winhttp.dll> --plugin <SparesSupply.dll>
                          --game-src <распакованные файлы игры, слой base> --version 0.1 --out <папка dist>

Результат: <out>/HEMTT_Spares_v<версия>.zip и <out>/HEMTT_Spares_v<версия>_cover.png
"""
import argparse
import os
import subprocess
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--mod', required=True)
    ap.add_argument('--winhttp', required=True)
    ap.add_argument('--plugin', required=True)
    ap.add_argument('--game-src', required=True)
    ap.add_argument('--version', required=True)
    ap.add_argument('--out', required=True)
    a = ap.parse_args()

    files = []  # (путь на диске, путь в архиве)
    mod = os.path.abspath(a.mod)
    for dirpath, _, names in os.walk(mod):
        for n in sorted(names):
            p = os.path.join(dirpath, n)
            files.append((p, 'mods/HEMTT_Spares/' + os.path.relpath(p, mod).replace('\\', '/')))
    files += [(a.winhttp, 'bin/winhttp.dll'),
              (a.plugin, 'bin/plugins/SparesSupply.dll'),
              (os.path.join(HERE, '..', 'docs', 'Dist-readme.md'), 'HEMTT_Spares_README.txt')]
    missing = [p for p, _ in files if not os.path.isfile(p)]
    if missing:
        sys.exit('нет файлов: ' + ', '.join(missing))

    os.makedirs(a.out, exist_ok=True)
    zip_path = os.path.join(a.out, f'HEMTT_Spares_v{a.version}.zip')
    with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for p, arc in files:
            if arc.endswith('README.txt'):  # номер версии подставляется при упаковке
                z.writestr(arc, open(p, encoding='utf-8').read().replace('{VERSION}', a.version).encode('utf-8'))
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


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    main()
