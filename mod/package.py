"""Packages a release: an archive with the game folder structure (+ a cover for publishing). Four packages - the
framework alone or together with the HEMTT Spares example mod, as the standalone build or for DefianceLoader:

    python mod/package.py [--mod <built HEMTT_Spares folder>] --framework-mod <built mini-mod folder>
                          --winhttp <winhttp.dll> --plugin <SparesSupply.dll>
                          [--game-src <extracted game files, base layer>] --version 1.0 --out <dist folder>
    python mod/package.py --defiance [--mod <...>] --framework-mod <...> --plugin <spares_repair.dll>
                          --manifest <spares_repair.plugin.json> [--game-src <...>] --version 1.0 --out <dist folder>

- without --mod - the framework only: SparesSupplyRepairFramework_v<version>.zip
  (with --defiance - SparesSupplyRepairFramework_DefianceLoader_v<version>.zip): the plugin + the framework mini-mod
  (zone circles) in mods/SparesSupplyRepairFramework;
- with --mod - the framework + HEMTT Spares (mods/HEMTT_Spares): HEMTT_Spares_v<version>.zip
  (HEMTT_Spares_DefianceLoader_v<version>.zip), plus the cover <out>/HEMTT_Spares_v<version>_cover.png (1920x1080)
  and the _banner.png preview (1300x372) for the mod page - this needs --game-src.

The README for players is docs/Dist-readme.md: blocks {IF STANDALONE} / {IF DEFIANCE} and {IF FRAMEWORK} /
{IF EXAMPLE} ... {END} (may be nested) go only into their own package.

The archive is reproducible: entries in a fixed order, with a fixed date and attributes, so the same files give a
byte-identical zip (with the same zlib - the one bundled with the Python used). The sha256 of the archive and of the
plugin files in it go to <out>/SHA256SUMS.txt (lines of other packages already there are kept).
"""
import argparse
import hashlib
import os
import subprocess
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
FRAMEWORK = 'SparesSupplyRepairFramework'
ZIP_DATE = (1980, 1, 1, 0, 0, 0)  # the earliest date a zip can hold; the same in every build
SUMS = 'SHA256SUMS.txt'


def zip_entry(arc):
    """A zip entry with no build-dependent fields: fixed date, a regular rw-r--r-- file, made on "MS-DOS"."""
    info = zipfile.ZipInfo(arc, date_time=ZIP_DATE)
    info.compress_type = zipfile.ZIP_DEFLATED
    info.create_system = 0
    info.external_attr = 0o100644 << 16
    return info


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def update_sums(path, sums):
    """Write sums ({name: sha256}) into the SHA256SUMS file path, keeping the lines of other names; sorted by name."""
    old = {}
    if os.path.isfile(path):
        for line in open(path, encoding='utf-8'):
            h, _, name = line.rstrip('\n').partition('  ')
            if name:
                old[name] = h
    old.update(sums)
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        for name in sorted(old):
            f.write(f'{old[name]}  {name}\n')


def pick_variant(text, tags):
    """Keep the README blocks {IF <tag>} ... {END} whose tag is in tags, drop the others (the marker lines too).
    Blocks may be nested: a line stays only if all the blocks around it match. Text outside the blocks is
    shared by all packages."""
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
        sys.exit('README: {IF} without {END}')
    return ''.join(out)


def main():
    """Build a package: an archive with the game folder structure (README with the version filled in); for the package
    with HEMTT Spares, also the 1920x1080 cover and the 1300x372 preview."""
    ap = argparse.ArgumentParser()
    ap.add_argument('--defiance', action='store_true', help='pack the DefianceLoader plugin, not the standalone build')
    ap.add_argument('--mod', help='HEMTT Spares; without it - a framework-only package')
    ap.add_argument('--framework-mod', required=True, help='framework mini-mod (zone circles)')
    ap.add_argument('--winhttp', help='standalone build only')
    ap.add_argument('--plugin', required=True)
    ap.add_argument('--manifest', help='only with --defiance: spares_repair.plugin.json')
    ap.add_argument('--game-src', help='needed with --mod (cover)')
    ap.add_argument('--version', required=True)
    ap.add_argument('--out', required=True)
    a = ap.parse_args()
    if a.defiance and not a.manifest:
        ap.error('--defiance needs --manifest')
    if not a.defiance and not a.winhttp:
        ap.error('--winhttp (or --defiance) is required')
    if a.mod and not a.game_src:
        ap.error('--mod needs --game-src')

    files = []  # (path on disk, path in the archive)

    def add_mod(folder, name):
        """All files of the built mod folder - into the archive as mods/<name>/..."""
        folder = os.path.abspath(folder)
        for dirpath, dirs, names in os.walk(folder):
            dirs.sort()  # a fixed entry order, whatever order the file system lists folders in
            for n in sorted(names):
                p = os.path.join(dirpath, n)
                files.append((p, f'mods/{name}/' + os.path.relpath(p, folder).replace('\\', '/')))

    add_mod(a.framework_mod, FRAMEWORK)
    if a.mod:
        add_mod(a.mod, 'HEMTT_Spares')
    if a.defiance:  # DefianceLoader takes the plugin together with its manifest from DefianceLoader\plugins
        files += [(a.plugin, 'DefianceLoader/plugins/spares_repair.dll'),
                  (a.manifest, 'DefianceLoader/plugins/spares_repair.plugin.json')]
    else:
        files += [(a.winhttp, 'bin/winhttp.dll'),
                  (a.plugin, 'bin/plugins/SparesSupply.dll')]
    base = 'HEMTT_Spares' if a.mod else FRAMEWORK
    files.append((os.path.join(HERE, '..', 'docs', 'Dist-readme.md'), f'{base}_README.txt'))
    # the modding guide - in all packages
    files.append((os.path.join(HERE, '..', 'docs', 'MODDING.md'), f'{FRAMEWORK}_MODDING.txt'))
    missing = [p for p, _ in files if not os.path.isfile(p)]
    if missing:
        sys.exit('missing files: ' + ', '.join(missing))

    tags = {'DEFIANCE' if a.defiance else 'STANDALONE', 'EXAMPLE' if a.mod else 'FRAMEWORK'}
    os.makedirs(a.out, exist_ok=True)
    zip_path = os.path.join(a.out, f'{base}{"_DefianceLoader" if a.defiance else ""}_v{a.version}.zip')
    sums = {}
    with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for p, arc in files:
            if arc.endswith('README.txt'):  # the README version and variant are chosen at packaging time
                text = pick_variant(open(p, encoding='utf-8').read(), tags)
                data = text.replace('{VERSION}', a.version).encode('utf-8')
            else:
                data = open(p, 'rb').read()
            z.writestr(zip_entry(arc), data)
            if arc.endswith(('.dll', '.plugin.json')):  # the plugin itself: players compare these with SHA256SUMS
                sums[arc] = sha256(data)
            print(f'{len(data):>10}  {arc}')
    sums[os.path.basename(zip_path)] = sha256(open(zip_path, 'rb').read())
    update_sums(os.path.join(a.out, SUMS), sums)
    print(f'-> {zip_path} ({os.path.getsize(zip_path)} bytes), sha256 {sums[os.path.basename(zip_path)]}')
    if not a.mod:
        return

    cover = os.path.join(a.out, f'HEMTT_Spares_v{a.version}_cover.png')
    tmp_dds = os.path.join(a.out, '_cover.dds')
    env = dict(os.environ, DFD_SRC=os.path.abspath(a.game_src), PYTHONIOENCODING='utf-8')
    subprocess.run([sys.executable, os.path.join(HERE, 'art', 'make_mod_icon.py'), tmp_dds, cover], check=True, env=env)
    os.remove(tmp_dds)
    print(f'-> {cover}')

    banner = os.path.join(a.out, f'HEMTT_Spares_v{a.version}_banner.png')  # 1300x372 preview
    subprocess.run([sys.executable, os.path.join(HERE, 'art', 'make_mod_icon.py'), '--banner', banner], check=True,
                   env=env)
    print(f'-> {banner}')


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    main()
