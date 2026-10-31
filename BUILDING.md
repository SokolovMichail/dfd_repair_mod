# Building the release yourself

The SparesSupply & Repair Framework ships native DLLs that patch the game's `logic.dll`. You should not have to
trust a binary from the internet, so the release is reproducible. Build it from this repository with the
toolchain below, and you get the same bytes as the published files. Compare the sha256 sums to confirm it.

- **DLLs and the DefianceLoader manifest: byte-identical on any machine** with the same toolchain. They do not
  depend on the game, the build folder or the build time.
- **Archives: byte-identical** if, besides the toolchain, the game build, Python and its packages, and the
  Windows font below are the same too. The mods in the archives are made from the game's own files and drawn
  with Pillow, so they depend on all of these.

## Toolchain of release 1.0

| Tool | Version | Used for |
|---|---|---|
| GCC (MinGW-w64, `x86_64-w64-mingw32`, POSIX threads) | 15.2.0, bundled with CLion 2026.2.3.1 | the DLLs (`-static -mcrtdll=ucrt`, Release: `-O3 -s`) |
| GNU Binutils (`ld`) | 2.46, bundled with the same CLion | linking (`--no-insert-timestamp`) |
| CMake | 4.3.1, bundled with CLion | the build |
| Ninja | 1.13.2, bundled with CLion | the build |
| Python | 3.12.4 (zlib 1.3.1) | mod data and packaging; zlib compresses the archives |
| Pillow | 12.3.0 | textures, icons, the cover and the banner |
| numpy | 2.5.3 | textures |
| Windows font `bahnschrift.ttf` | sha256 `eaad201da94cb323401fdb8cc760650803ddf8fd704cb9fa56317e6c69bc6fd6` | the text on the MODS menu icons |

Another GCC or Binutils version gives working DLLs, but different bytes.

## Game build

Release 1.0 was built against the GOG build of 2026-09-25 (the build the plugin's patches target):

| File | sha256 |
|---|---|
| `bin\logic.dll` | `1216d627c7288c7db6940168363be582232ed4d3cb860b8b8c8d7489652eca74` |
| `bin\game.dll` | `8ec30a0b59aebf2240f00229d54a0f58e2e338f9ab3511046c9ff36970dd1489` |
| `bin\trm.exe` | `4566b96edafcb35ab890a9c4b3da88cc161efc18da31fe40d748d471efc61179` |

The DLLs do not depend on the game. The mods are made from the game's archives (`*.pak`), and building them
needs the archive password in `DFD_PAK_PASSWORD`. It is not published here.

## Steps

1. Check out the release tag: `git clone https://github.com/SokolovMichail/dfd_repair_mod` and
   `git checkout v1.0`. The repository forces LF line endings (`.gitattributes`), so the texts packed into the
   archives are the same on every checkout.
2. Install Python 3.12 and the packages: `pip install pillow==12.3.0 numpy==2.5.3`.
3. Copy `.env.example` to `.env` and set `DFD_PAK_PASSWORD`. Install the game; if it is not in
   `C:/Games/Terminator Dark Fate - Defiance`, pass `-DGAME_DIR=<folder>` to CMake.
4. Build both releases:
   - **In CLion:** open the project with the bundled MinGW toolchain and run **Build release** and
     **Build release (DefianceLoader)**.
   - **Or from a console**, with CLion's `bin\mingw\bin` in `PATH`:
     ```
     cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=c++
     cmake --build build --target release release_defiance
     ```
   Either way, the DLLs come from a separate nested Release build, whatever profile you configured.
5. Compare `dist\SHA256SUMS.txt` with the list below. To check a single file you downloaded, run
   `certutil -hashfile <file> SHA256` or `Get-FileHash <file>` in PowerShell.

## sha256 of release 1.0

```
5c6e0e97380f4c7aceffd18d749e2c66498dc7a9109b67973a9a8eda794a9686  SparesSupplyRepairFramework_v1.0.zip
19b210fd56d90157ddb025337a7eb4e99de8e4fe6408d426cfd95662f1f27626  SparesSupplyRepairFramework_DefianceLoader_v1.0.zip
6fadee83c53565d42e8d18d8d1c65ec2774119b47db9830225cc0dffe258c874  HEMTT_Spares_v1.0.zip
317f8bcb6c05738c1eb91574ee2a441b88f2acc7c226ee27ddfbbf21bf0a53cf  HEMTT_Spares_DefianceLoader_v1.0.zip
e65413ff56af4095c353d1d6a84030e4f5f0f8c8014c79393d2c36ffdf5aea80  bin/winhttp.dll
bc247a359eac07ce22bba54ac65192f2c09ab3e8bd32548915351859889b5d7f  bin/plugins/SparesSupply.dll
704486cccf1013e90d5f8ec29a2c4d297de5163616c1a17186f536373ca1de83  DefianceLoader/plugins/spares_repair.dll
5ac3191758ce36e29e7ac35276be2a104731e7bf04e851c5e44b86342c82ff1d  DefianceLoader/plugins/spares_repair.plugin.json
```

The plugin files are the same in both archives of a build (framework only, or with HEMTT Spares). The files
inside an archive are listed with the path they have in the game folder.

## What makes the build reproducible

- **DLLs.** They are linked with `-Wl,--no-insert-timestamp`, so the PE header and the export table carry no
  link time (`TimeDateStamp` is 0). Nothing else in them depends on the build: the Release DLLs are stripped
  (`-s`) and hold no source paths.
- **Archives.** `mod/package.py` writes every entry with the same date (1980-01-01), the same attributes and in
  a fixed order. It also writes `dist\SHA256SUMS.txt`.
- **Mod data.** `mod/build_mod.py` extracts the game files afresh and draws the textures and icons the same
  way every time.

Each of these was checked with two independent builds, in different folders and with separate extracted game
files: they gave identical sums.
