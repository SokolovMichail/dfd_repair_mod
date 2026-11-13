# DFDRepairMod — mods for Terminator: Dark Fate – Defiance

The whole project: native plugins (C++), the mods' data build (Python), graphics, release packaging
and engine reverse-engineering tools. The result:

- **SparesSupply & Repair Framework** — a framework: the spare parts zone and repair zone mechanics in F4 mode for any
  vehicle a mod gives the right abilities to;
- **HEMTT Spares** — an example mod on the framework with two supply vehicles: HEMTT Spares (spare parts) and
  HEMTT Repair (repair and armor, still a test vehicle).

| Document | What about |
|---|---|
| [`INSTALL.md`](INSTALL.md) | installing, checking, removing (RU/EN) — for players |
| [`docs/MODDING.md`](docs/MODDING.md) | how to make your own vehicles on the framework (EN) — for modders; goes into every package |
| [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) | how the mod works: data, loader, plugin, build |
| [`docs/engine_notes.md`](docs/engine_notes.md) | what is known about the engine (addresses, fields, slots) |
| [`re/README.md`](re/README.md) | tools for reverse engineering the game's DLLs |

## Vehicles

Both are copies of HEMTT Supply (the same 3D model), deploy with the F4 button (supply mode) and work over a zone.
They hand out no ammunition. Main campaign and the Canada DLC: both in every shop (one of each). Skirmish: Founders.
Resistance campaign — see below.

| Vehicle | sysname | What it does | Zone circle | Campaign |
|---|---|---|---|---|
| **HEMTT Spares** | `Fnd_HEMTT_spares` | refills the spare parts of every vehicle in the zone; or an order for one vehicle | wrenches | 1 in the first sector shop + in the same rewards as HEMTT Supply |
| **HEMTT Repair** (test) | `Fnd_HEMTT_repairer` | every 0.5 s repairs all damaged modules of own and allied vehicles in the zone (+2% of the maximum) and restores the armor (+2% of each layer's original thickness); does not repair itself or destroyed vehicles; pays with its own spare parts (10000): 1 per health point, 1 per 1 mm of armor, takes nothing from the target | wrench and hammer | 1 in the first sector shop |

Three parts; the first two are the framework, the third is the example:

- **the framework mini-mod** — `mods/SparesSupplyRepairFramework`: only the materials and textures of the zone circles
  (wrenches; wrench and hammer) and the mod icon. It has no game tables, so it conflicts with nothing;
- **the example data** — `mods/HEMTT_Spares`: both vehicles (copies of `Fnd_HEMTT_ammunition`; Spares —
  `abilities=ammunition_recharge,support`, 10000 spare parts; Repair — `abilities=ammunition_recharge,repair`;
  both — 0 ammunition, light vehicles), shop, skirmish army, localization (en), icons, mod icon;
- **the framework plugin** — it doesn't know the mod's vehicles: the behaviour is switched on by the vehicle's
  abilities in the table. Without it the engine can't do this (in detail — at the top of
  [`logic_dll/common.h`](plugins/spares_supply/logic_dll/common.h), [`logic_dll/spares.h`](plugins/spares_supply/logic_dll/spares.h),
  [`logic_dll/repair.h`](plugins/spares_supply/logic_dll/repair.h) and in
  [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)):
  it allows `ammunition_recharge` next to `support` / `repair` (the F4 button), in F4 mode ticks the spare parts
  component (Spares) and/or itself repairs modules and armor of vehicles in the zone (Repair — the
  engine's repair is unfinished), draws the circles with the mini-mod's materials. It doesn't touch ammunition: a
  vehicle with a real stock hands it out together with spare parts and repair.

The release is four packages on one mod page: the framework alone or together with HEMTT Spares, each in the
standalone version (our `winhttp.dll`) or for DefianceLoader (see "Build targets").

Checked on GOG 1.09.1221. Addresses in `logic.dll` are not hard-coded — the plugin finds them by byte patterns and
RTTI, so it should work on Steam too. If anything is not found or doesn't match, it changes nothing and writes to the
log. Single player only.

## Layout

```
CMakeLists.txt         build targets (see below), the mod version MOD_VERSION
.env.example           template of local settings -> copy to .env
.run/                  ready CLion run configurations: Build release / Deploy to game / Build mod data,
                       Build release (DefianceLoader) / Deploy to game (DefianceLoader),
                       Build SparesSupply (standalone) / Build spares_repair (DefianceLoader)
common/patch.h         shared by the plugins: log, pattern and RTTI search, patch set, waiting for a DLL to load
loader/                winhttp.dll: proxy of the system winhttp.dll + loader of bin\plugins\*.dll
plugins/spares_supply/ SparesSupply.dll (standalone) and spares_repair.dll (DefianceLoader plugin) - from the same sources:
  core/                the mod's rules without ties to the game: supply.* (F4 mode), spares.* (HEMTT Spares),
                       repair.* (HEMTT Repair); they talk to the game only through port interfaces
  logic_dll/           binding to today's logic.dll: common.* (object model, ammunition hooks,
                       patch installation), spares.*, repair.* (hooks and port implementations);
                       standalone.cpp - entry point of SparesSupply.dll, defiance.cpp - entry point of spares_repair.dll
  spares_repair.plugin.json.in  DefianceLoader plugin manifest (CMake fills in the version)
third_party/defiance/  defiance.h - C header of the DefianceLoader API (v1.9.0, MIT)
tools/                 gen_winhttp_proxy.py - generates the winhttp forwarders (rarely needed)
mod/
  build_mod.py         builds both mods from the current game files: the framework mini-mod and HEMTT Spares
                       (what comes from where - at the top of the file)
  package.py           release packages + cover (the players' README - docs/Dist-readme.md)
  art/                 graphics: circles (make_circle_spares, make_circle_repair), vehicle icons
                       (make_unit_icons --kind spares|repair), mod icons (make_mod_icon [--framework])
  tools/               gamefiles.py (finds the current version of a file in the game's layers), patch_table.py (edits
                       Excel-XML tables), patch_army_toml.py, pak_extract.py (manual unpacking), table_grep.py
re/                    tools for reverse engineering the game's DLLs (see re/README.md)
tests/                 plugin tests (doctest + CTest), the game is not needed for them - see "Tests"
docs/                  engine_notes.md - what is known about the engine; Dist-readme.md - the players' README in the archive;
                       MODDING.md - the modders' guide (in the archive); the original HEMTT Spares plan
dist/                  the result of "build the release" (not in git)
```

## Setup

1. **CLion** with its bundled MinGW (GCC 15) — or any x86_64 MinGW-w64 with UCRT.
2. **Python 3.12+** in PATH and the packages: `pip install pillow numpy` (graphics), `capstone` — only for `re/`.
3. **The game** installed. The path is the CMake variable `GAME_DIR`
   (default `C:/Games/Terminator Dark Fate - Defiance`; to change it: Settings → Build → CMake → CMake options
   `-DGAME_DIR=...`).
4. **`.env`** in the root (copy from `.env.example`): `DFD_PAK_PASSWORD` — the game archives' password; without it
   the mod build can't get the game files. It can also be an environment variable. `.env` is not put into git.
   For `re/` you can add `DFD_GAME_DIR`.

## Build targets

| Target | What it does |
|---|---|
| `winhttp`, `SparesSupply` | The DLLs (CLion profile, usually Debug). |
| `spares_repair` | The same plugin for DefianceLoader: `spares_repair.dll` + `spares_repair.plugin.json` in the build folder. |
| `mod` | Builds both mods into `<build>/stage/mods/`: the framework mini-mod `SparesSupplyRepairFramework` and the example `HEMTT_Spares`. The game files are taken anew every time — after a game update a rebuild is enough. |
| `deploy` | Installs into the game: `bin\winhttp.dll`, `bin\plugins\SparesSupply.dll`, `mods\SparesSupplyRepairFramework`, `mods\HEMTT_Spares`. Removes the DefianceLoader version's plugin (`DefianceLoader\plugins\spares_repair.*`) if it was there. The game must be closed; both mods have to be enabled once in the MODS menu. |
| `deploy_defiance` | The same for DefianceLoader: `DefianceLoader\plugins\spares_repair.dll` + manifest, both mods; removes `bin\plugins\SparesSupply.dll` (`bin\winhttp.dll`, DefianceLoader itself and other plugins stay). So `deploy` / `deploy_defiance` switch the game between the two versions of the mod — handy for checking (see "DefianceLoader"). |
| `release` | **Build the release**: a separate Release build of the DLLs (optimized, no debug info) + both mods + two standalone packages: `dist/SparesSupplyRepairFramework_v<version>.zip` (the framework only: DLLs and mini-mod) and `dist/HEMTT_Spares_v<version>.zip` (framework + HEMTT Spares), plus `dist/HEMTT_Spares_v<version>_cover.png` (1920×1080) and `_banner.png` (1300×372). |
| `release_defiance` | The same for DefianceLoader: `dist/SparesSupplyRepairFramework_DefianceLoader_v<version>.zip` and `dist/HEMTT_Spares_DefianceLoader_v<version>.zip` (`DefianceLoader/plugins/spares_repair.dll` + manifest instead of `bin/...`); cover and preview are shared with `release`. Packaging — `mod/package.py [--defiance] [--mod ...]`; the players' README is one `docs/Dist-readme.md`, the blocks `{IF STANDALONE}` / `{IF DEFIANCE}` and `{IF FRAMEWORK}` / `{IF EXAMPLE}` (may be nested) go only into their own package; `docs/MODDING.md` goes into all of them. |

In CLion: pick the **Build release** configuration (or **Deploy to game**; for DefianceLoader — the same ones marked
**(DefianceLoader)**) and press Run — or Build → Build Target.
From a console:

```bash
cmake --build cmake-build-debug --target release
```

The version is `MOD_VERSION` in `CMakeLists.txt`: it goes into the archive name, the mod description and the plugin log.

The release is reproducible: the DLLs build byte for byte the same, the archives too, with the same game build. Every
packaging run appends to `dist/SHA256SUMS.txt`. Tools, steps and the release sums are in [`BUILDING.md`](BUILDING.md)
(in English, for anyone who wants to build it themselves and compare).

## Tests

The tests pin down the behaviour of the `SparesSupply.dll` plugin and the shared `common/patch.h`, so that a refactoring
breaks nothing unnoticed. The game is not needed for them: instead of `logic.dll` and engine objects there are fakes
(`tests/support/`), built on the same assumptions the plugin has. So a game update doesn't break the tests.

| Program | What it checks |
|---|---|
| `patch_h_test` | pattern and RTTI search, the "all or nothing" patch set, log, waiting for a DLL to load |
| `plugin_test` | F4 mode of HEMTT Spares / Repair / Supply, the zone circle, module and armor repair, paying with spare parts |
| `plugin_install_test` | finding addresses and installing patches in a fake `logic.dll`; on any mismatch nothing changes |
| `plugin_defiance_test` | the DefianceLoader plugin on a fake loader: asks for exactly the same patches, writes nothing itself, the contract matches, refuses on a mismatch, both DLLs at once, `supply-radius`, the `log` setting |
| `core_test` | the core's rules (`core/`) on simple test ports, without a fake engine; the core builds without `windows.h` |

The tests build together with the rest (`-DDFD_TESTS=OFF` turns them off). In CLion — the **All CTest**
configuration; from a console:

```bash
cmake --build cmake-build-debug && ctest --test-dir cmake-build-debug --output-on-failure
```

The `QUIRK:` mark in the tests is current behaviour that looks odd; the test pins it down so that it doesn't change
by accident.

## Checking in the game

`bin\plugins\loader.log` — `loaded SparesSupply.dll`;
`bin\plugins\SparesSupply.log` — `SparesSupply <version> loaded` … `SparesSupply active`; while HEMTT Repair works —
lines `repair: target=… +N hp, M armor layers`.
If it says `not supported`, the game has different code: see which pattern was not found and update it with
`re/mksig.py` (see `docs/engine_notes.md`).

With DefianceLoader — `DefianceLoader\logs\defiance-loader.log`, lines `sokolov.spares-repair`:
`SparesSupply <version> as a DefianceLoader plugin` … `SparesSupply staged`, then the plugin state `active`.

## DefianceLoader

The mod works both on its own (our `winhttp.dll`) and as a [DefianceLoader](https://github.com/DefianceLoader/DefianceLoader)
plugin (`bin\dxgi.dll`). The rules and hooks are the same; the plugin just hands its patches to the loader, and the
loader makes sure plugins don't write to the same place (in detail — `docs/ARCHITECTURE.md`, 5.7).

Installing with DefianceLoader: `spares_repair.dll` and `spares_repair.plugin.json` go to `DefianceLoader\plugins\`,
the mods to `mods\SparesSupplyRepairFramework` and `mods\HEMTT_Spares` (target `deploy_defiance`). `bin\winhttp.dll`
and `bin\plugins\SparesSupply.dll` are then **not needed** — if they are left, the standalone DLL works, and the
loader plugin refuses and writes to the log what to remove. The loader plugin `defiance.core` is needed (it comes with
the loader). Single player only: the loader won't let you into a network game while the plugin is enabled. To turn it
off — `enabled = false` in the `[sokolov.spares-repair]` section of `DefianceLoader\config\settings.ini`;
`log = false` — no event lines in the log.

If the loader plugin `defiance.supply-radius` is enabled, it sets the spare parts zone radius (we skip our radius
patch).

**Where the plugin writes** (all in `logic.dll`, `patch_bytes` byte patches; RVAs are GOG 2026-09-25, on other
builds the addresses differ, the plugin finds them at startup):

| Place | Patch | RVA (GOG 2026-09-25) |
| --- | --- | --- |
| car description builder: flag after `support` | `mov r15b,1` → `nop` (3 bytes) | 0x51ce31 |
| tank description builder: flag after `support` | the same | 0x52238f |
| car description builder: flag after `repair` | the same | 0x51ced2 |
| tank description builder: flag after `repair` | the same | 0x522432 |
| spare parts target search: `movss xmm2, [20.0]` | → `movss xmm2, [supplyRadius]` (8 bytes; skipped with `defiance.supply-radius`) | 0x439c67 |
| `AmmoRechargerFacet` slots 10 / 13 / 14 (`update`, `deactivate`, `activate`) | vtable slot, 8 bytes | 0x710900, 0x710918, 0x710920 |
| `RefuellerFacet` slots 10 / 13 / 14 | vtable slot | 0x733d70, 0x733d88, 0x733d90 |
| `SparesRechargerFacet` slots 10 / 13 | vtable slot | 0x73d9a8, 0x73d9c0 |
| `RepairerFacet` slots 10 / 13 / 14 | vtable slot | 0x734028, 0x734040, 0x734048 |
| `AmmoRechargerFacet` circle function: `lea r8,[rax+0x120]` (material) | declared unchanged; the plugin changes it itself while the circle is redrawn (7 bytes) | 0x120687 |

## A new plugin

1. `plugins/<name>/<name>.cpp`, include `common/patch.h`.
2. In `DllMain` — open the log and `DllWatcher::watch(L"logic.dll", cb)`. In `cb`: `dfd::Image img(base)` →
   find the addresses (`img.find(pattern)`, `img.vtableOf("Class@Leonardo")`) → `PatchSet` (`bytes` / `vslot`
   with a pattern of the function start) → `apply`.
3. In `CMakeLists.txt`: `add_library(<name> SHARED ...)`, `dfd_dll(<name>)`, copying in `deploy` and in `package.py`.

A plugin's `DllMain` runs under the loader lock (the loader's `DllMain` calls it): no `LoadLibrary` and no waiting
for other threads. The `DllWatcher` callback is called after `logic.dll` is mapped, before its code runs.

## Removing from the game

Disable both mods in MODS, delete `mods\HEMTT_Spares`, `mods\SparesSupplyRepairFramework`, `bin\winhttp.dll` and
`bin\plugins`. With DefianceLoader — instead of the last two, delete `DefianceLoader\plugins\spares_repair.dll` and
`spares_repair.plugin.json`.
