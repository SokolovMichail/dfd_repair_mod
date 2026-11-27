# How the HEMTT Spares mod works (HEMTT Spares + HEMTT Repair)

A document for whoever will maintain the mod or build the next one on its model. A summary of engine facts
(addresses, fields, slots) is in [`engine_notes.md`](engine_notes.md); this one covers how the mod is built from them
and why.

## 1. Task and overall layout

The mod adds two supply vehicles. Both work in F4 mode the same way HEMTT Supply hands out ammunition,
they just do something else:

- **HEMTT Spares** (`Fnd_HEMTT_spares`) — hands out **spare parts** to all vehicles in the zone;
- **HEMTT Repair** (`Fnd_HEMTT_repairer`, still a test vehicle) — **repairs** modules of own and allied vehicles in
  the zone and **restores their armor**.

Spare parts are almost ready in the engine: the `SparesRechargerFacet` component (ability `support`) can already find
vehicles in a radius and top up their spare parts, but it was never wired into F4 mode. Repair (`RepairerFacet`, ability
`repair`) is seriously unfinished — see 5.6. Nothing restores armor in combat at all. Data alone cannot fix this,
so the mod has two parts:

```
┌───────────────────────────── data (mods/HEMTT_Spares) ──────────────────────────────┐
│ Fnd_HEMTT_spares:   abilities = ammunition_recharge,support   (HEMTT Spares)          │
│ Fnd_HEMTT_repairer: abilities = ammunition_recharge,repair    (HEMTT Repair)          │
│ campaign shop, skirmish army, localization, icons, zone circle materials              │
└──────────────────────────────────────────────────────────────────────────────────────┘
                      │ the game mounts it as a normal mod (MODS menu)
                      ▼
┌───────────── bin\winhttp.dll (loader) ──────────────┐      ┌──── bin\plugins\SparesSupply.dll ────┐
│ replaces the system winhttp.dll, forwards its 92    │ ───► │ waits for logic.dll, finds the needed │
│ functions and loads all bin\plugins\*.dll           │      │ places in it, patches them in memory  │
└─────────────────────────────────────────────────────┘      └───────────────────────────────────────┘
```

The game files on disk are not changed; everything the plugin does exists only in the memory of the running game.

## 2. Data

Built by the [`mod/build_mod.py`](../mod/build_mod.py) script from the **current** files of the installed game. There
are two mods:

- **framework mini-mod** `mods/SparesSupplyRepairFramework` (SparesSupply & Repair Framework) — zone circles only:
  the materials and textures the plugin uses to draw the spare parts and repair zones, plus `mod.json` and an icon
  (`make_mod_icon.py --framework`). It contains no game tables, so it conflicts with nothing. Any vehicle built on
  the framework needs it — not only the HEMTT Spares vehicles;
- **example mod** `mods/HEMTT_Spares` — the vehicles themselves (the table below, except the circle rows).

The plugin does not know the vehicles: whether a vehicle counts as a spare parts or repair vehicle is decided by its
abilities in the table (`support` / `repair` next to `ammunition_recharge`). So other mods make their own vehicles on
the framework with data alone — see [`MODDING.md`](MODDING.md).

**Where the files come from.** The game mounts layers: `basis.pak → patch_NNN*.pak → unpacked basis/ folder`, and each
next file replaces the previous one entirely. [`mod/tools/gamefiles.py`](../mod/tools/gamefiles.py) finds, for each
needed file, the last source that has it and extracts it (archives are zip with password `DFD_PAK_PASSWORD`).
So the project has no copies of game files, and after a game update rebuilding the mod is enough.

**What is changed.** A mod also replaces files entirely, so the mod gets the full game file with the edits.
The edits are made by [`patch_table.py`](../mod/tools/patch_table.py): it treats Excel-XML as text and changes
only the needed cells; the rest of the file is kept byte for byte.

**The table-merge build** (`build_mod.py --tables patch`, the `HEMTT_Spares_DefianceLoader_TableMerge` package).
DefianceLoader's `defiance.table-merge` plugin can merge a mod's `<table>.<mod folder>.patch.xml` into the game's
table row by row (matched on `sysname`; an empty patch cell keeps the stock value, a row the table lacks is
appended). So the same edits are made to a scratch copy, and
[`make_table_patch.py`](../mod/tools/make_table_patch.py) writes the difference from the stock table as the patch:
our new rows in full, changed cells only, the stock file's styles, cells copied as they are. It then reads the patch
back, merges it into the stock table the loader's way and fails the build unless the result is exactly our table.
Shops are left out: a shop's vehicle list is one cell, the merge cannot append to it, and two mods adding vehicles to
the same shop would still clash. So is `default_army.toml`, which is not a table. That build therefore defines the
vehicles (cars, encyclopedia, localization in every layer) and puts them in no shop. The DefianceLoader plugin warns
at startup when the merge is off and an enabled mod ships table patches (`logic_dll/defiance.h`).

Both vehicles are described in the `UNITS` list at the top of `build_mod.py` (name, description, table columns, icon,
shop); each table is patched in turn for all vehicles in the list. A new vehicle on the same model is added there too.

| Mod file | What it contains |
|---|---|
| `basis/scripts/species/cars.xml`, `dlc/Resistance/.../cars.xml` | rows `Fnd_HEMTT_spares` and `Fnd_HEMTT_repairer` — copies of `Fnd_HEMTT_ammunition` (its 3D model, handling). Spares: `abilities=ammunition_recharge,support`, `spares=10000`. Repair: `abilities=ammunition_recharge,repair`. Both: `capacity_ammo_recharger=0`, `unit_set=light_vehicle` (test for now), their own `tech_pic`/`garrison_pic`. Two files, because skirmish mounts only the main layer, while the Resistance campaign also mounts the DLC layer. |
| `basis/scripts/default_army.toml` | both vehicles in the Founders list (skirmish) |
| `dlc/Resistance/.../shop_presets.xml` | Spares: 1 unit in `sector_1_reward` + in the rewards that give out HEMTT Supply. Repair: 1 unit in `sector_1_reward` |
| `basis/scripts/species/shop_presets.xml` | main campaign: both vehicles in all shops (the `BASE_SHOPS` list; except the test `test_shop_*` and the demo `resistance_dlc_shop_demo`), one each. Added with a `cars,+=` edit — a comma-separated list element. In the Resistance and Canada campaigns this file is overridden by their own |
| `dlc/Canada/basis/scripts/species/{cars,encyclopedia_dlc,shop_presets}.xml`, `dlc/Canada/localization/en/.../encyclopedia_dlc_loc.xml` | the Canada DLC with its own tables — the same as for Resistance: both vehicles (copies of HEMTT Supply), articles, both in all 5 shops (`CANADA_SHOPS`), one each. The mod layer path `dlc/Canada/...` is taken by analogy with Resistance (that one is verified against `game.log`) |
| `basis/scripts/species/encyclopedia.xml`, `dlc/Resistance/.../encyclopedia_dlc.xml` | articles with their own pictures (main game and Resistance) |
| `localization/en/...`, `dlc/Resistance/localization/en/...` | the names "HEMTT Spares" / "HEMTT Repair" and the article texts |
| `basis/textures/ui/pictures/tech_pic/...fnd_hemmit_spares*.dds` | Spares icons: a HEMTT silhouette with a toolbox instead of shells ([`make_unit_icons.py`](../mod/art/make_unit_icons.py) `--kind spares`) |
| `basis/textures/ui/pictures/tech_pic/...fnd_hemmit_repair*.dds` | Repair icons: a crane boom with a hook (`make_unit_icons.py --kind repair`) |
| **framework:** `basis/materials/rounds/sparesRecharge.material` + `textures/cursor_decals/circle_spares.dds` | the Spares zone circle — wrenches. In the original this material pointed to the featureless `circle_generic.dds` ([`make_circle_spares.py`](../mod/art/make_circle_spares.py)) |
| **framework:** `basis/materials/rounds/repairRecharge.material` + `textures/cursor_decals/circle_repair.dds` | the Repair zone circle — a crossed wrench and hammer ([`make_circle_repair.py`](../mod/art/make_circle_repair.py)). The material is new, the game does not have it: the plugin substitutes its path (5.6) |
| `mod.json`, `basis/mod_icon.dds` (each of the two mods has its own) | description and icon in the MODS menu ([`make_mod_icon.py`](../mod/art/make_mod_icon.py); for the framework — `--framework`: two zone circles without vehicles) |

**Why `ammunition_recharge,support` / `ammunition_recharge,repair`.** The game gives the F4 button only to vehicles
with ammunition or a refueller, and F4 mode itself works only with them. So both vehicles need the ammunition component
as the "carrier" of the mode; their ammo stock is 0, there is nothing to hand out. The plugin does not touch ammo, so
a vehicle with `ammunition_recharge,support,repair` and a real stock hands out ammo and spare parts and repairs, all at
once (5.6).
The F4 button and the circle work with zero stock (verified in game). The refueller (`refuel`) does not fit: it hands out fuel from the vehicle's own tank. On HEMTT Repair
the repair component `RepairerFacet` serves only as a marker "this is a repair vehicle" — the plugin does the repair itself.

## 3. Loader `bin\winhttp.dll`

The game cannot load native plugins, so DLL substitution is used: `game.dll` (and the embedded browser
`libcef.dll`) import `winhttp.dll`, and Windows looks for it next to the program first — in `bin\`.

[`loader/dllmain.cpp`](../loader/dllmain.cpp) on load:

1. Determines whether this is the main process. Helper processes of the embedded browser (CEF, the command line
   contains `--type=`) also load this DLL — in them it only forwards calls, without plugins and without a log.
2. Loads the real `%SystemRoot%\System32\winhttp.dll` and stores the addresses of all its 92 functions in `g_orig[]`.
3. Loads all `bin\plugins\*.dll` in alphabetical order and writes `bin\plugins\loader.log`.

Forwarding is done with assembly thunks ([`loader/winhttp_exports.S`](../loader/winhttp_exports.S)):
each exported function is a single instruction `jmp *g_orig+8*i(%rip)`, so the call goes to the system DLL
with the same registers and stack, knowing nothing about the function's signature. The thunks, the `.def` with the
exports and the list of names are generated by [`tools/gen_winhttp_proxy.py`](../tools/gen_winhttp_proxy.py) from the
export table of the system DLL.

Plugins are loaded directly from the loader's `DllMain`, i.e. under the Windows loader lock. This is done to get in
before `logic.dll` is loaded, but it imposes a rule: a plugin's `DllMain` must not call `LoadLibrary` or wait for other
threads — it may only register (see below).

## 4. Shared plugin code — [`common/patch.h`](../common/patch.h)

| What | Why |
|---|---|
| `Log` | a `<name>.log` file next to the plugin, timestamped lines |
| `DllWatcher` | calls a callback as soon as the needed DLL is loaded into the process: immediately if it is already loaded, otherwise via `LdrRegisterDllNotification`. The notification arrives after the DLL is mapped into memory and its imports are bound, but **before its `DllMain` and before any of its code** — the patches are in place before the game creates its first unit. |
| `Image` | a loaded PE image: sections; `find(pattern)` — search for a byte pattern with `??`; `vtableOf("Class@Leonardo")` — vtable address via RTTI (type name `.?AV...@@` → TypeDescriptor → CompleteObjectLocator → slot before the vtable) |
| `PatchSet` | an "all or nothing" set of patches: `bytes()` — byte replacement with a check of the original bytes, `vslot()` — vtable pointer replacement with a check that the replaced function starts with the expected bytes. `apply()` first checks **all** patches and only then writes; on any mismatch it writes nothing. |

Why search rather than hard-coded addresses: another build of the game (Steam, a patch) will almost certainly shift the
addresses, but the code of the same functions will stay the same. Patterns are the code bytes around the needed spot,
with the address offsets (rip-relative and `call`) replaced by `??`; each pattern must be found exactly once (the
patterns were produced by the [`re/mksig.py`](../re/mksig.py) script). And checking the start of the replaced functions
also confirms that the field offsets the plugin reads are the same.

**Crash guard** — [`common/guard.h`](../common/guard.h) (+ `guard.cpp`, `guard_x64.S`). `dfd::guarded(f)`
runs `f`; if `f` or game code it calls faults (access to a bad address, division by zero, etc.),
the game does not crash: the thread continues right after `guarded`, which returns `true` with the exception code and
address. How it works: `dfd_guard_enter` (assembly) saves the callee-saved registers, `rsp`
and `mxcsr` into a frame on the stack and calls `f`; a vectored exception handler sees the fault first and, if the
thread is inside `guarded`, rewrites the thread context with the saved registers and the "landing" address in
`dfd_guard_enter` — without unwinding the stack through game frames and without `longjmp` from the handler. The handler
leaves everything else alone (C++ exceptions, breakpoints, stack overflow, a fault outside `guarded`). What was skipped
stays undone: destructors between the fault and `guarded` do not run, and the game may be left in the middle of a
change — what to put back is up to the caller. Verified by `tests/guard_test.cpp` on real faults.

## 5. Plugin `SparesSupply.dll` — [`plugins/spares_supply/`](../plugins/spares_supply/)

Two parts; they are built into two DLLs with the same behavior — the DLLs differ only in the entry point and in who
writes the patches into `logic.dll`:

- `SparesSupply.dll` — **standalone**: it is loaded by our `winhttp.dll` from `bin\plugins` and applies the patches
  itself ([`logic_dll/standalone.cpp`](../plugins/spares_supply/logic_dll/standalone.cpp), 5.4);
- `spares_repair.dll` + `spares_repair.plugin.json` — **a [DefianceLoader](https://github.com/DefianceLoader/DefianceLoader) plugin**:
  the same patches via the loader ([`logic_dll/defiance.cpp`](../plugins/spares_supply/logic_dll/defiance.cpp), 5.7).

Parts:

- [`core/`](../plugins/spares_supply/core/) — **the mod's rules**, independent of the game: no `windows.h`, no
  addresses, no engine objects. [`supply.h`](../plugins/spares_supply/core/supply.h) — the shared F4 mode rules (what to
  do on activation, every frame and on deactivation, which circle);
  [`spares.h`](../plugins/spares_supply/core/spares.h) — HEMTT Spares; [`repair.h`](../plugins/spares_supply/core/repair.h) —
  HEMTT Repair: the repair pulse, the cost and the debt, who can be repaired (5.6). The core talks to the game only
  through "port" interfaces (`SupplyPort`, `SparesPort`, `RepairPort`, `Modules`); vehicles are opaque handles to it.
- [`logic_dll/`](../plugins/spares_supply/logic_dll/) — **the binding to the current `logic.dll`**: implements the ports
  through reverse engineering. [`common.h`/`.cpp`](../plugins/spares_supply/logic_dll/common.h) — the engine object
  model, the ammunition hooks, the zone circle, address search and patch installation (5.4);
  [`spares.h`/`.cpp`](../plugins/spares_supply/logic_dll/spares.h) and [`repair.h`/`.cpp`](../plugins/spares_supply/logic_dll/repair.h) —
  the hooks and ports of HEMTT Spares and HEMTT Repair: field offsets, circle substitution, returning the target's spare
  parts after `incHealth` and other workarounds for unfinished parts of the engine.

The hooks in `logic_dll/` are thin: call the game's original and pass the event to the core (`core::onSupplyOn/Off/Tick`).
The mod's own part of each hook runs under the crash guard (`guarded(where, …)` in `common.h`, section 4);
the game's original does not, it always runs. A fault in the mod's own part skips the rest of that part, restores the
zone circle material (the offset in `lea` for HEMTT Spares, the table row for HEMTT Repair — if the fault happened
during a redraw) and is written to the log. After `kMaxFaults` (3) faults the mod's own work is switched off until the
game restarts: the hooks only call the originals, and the vehicles behave the way the game makes them.
If an official modding API appears, a new binding with the same ports will sit next to `logic_dll/`, and
the core and its tests (`tests/core_test.cpp`) will not change.

### 5.1 The engine object model the plugin uses

- **Component** (facet) — a C++ object with a vtable; dispensers share slots: 10 `update(dt)`, 13 `deactivate`,
  14 `activate`, 17 for ammunition — `canRecharge(target)`. The "disabled" flag is byte `+0x59` for
  ammunition/refueller, `+0x51` for spare parts.
- The component's **owner** (vehicle) — `[component+0x10]` points to a reference count block, in which `[+0x10]` is the
  object.
- **A vehicle's components** are enumerated with an iterator: `vehicle->vfunc(+0x58)` returns an iterator with the
  methods "at end?", "current", "next". `findFacet(vehicle, vtable)` — the same way the engine itself does it, but
  comparing the component's vtable with the wanted one (exact class). The engine changes reference counts without
  locks — the game logic is single-threaded, and the plugin runs on the same thread.
- Hooking a method = replacing the pointer in the class vtable (in `.rdata`). This changes the behavior of **all**
  objects of the class, so each hook first decides whether this object concerns it (`kindOf`: does the vehicle have a
  spare parts or repair component), and otherwise just calls the original. The needed methods are called only through the vtable — this was checked across all the code of
  `logic.dll`, there are no direct calls, so replacing the pointer is enough.

### 5.2 What exactly the plugin changes

| # | Where | Patch | Why |
|---|---|---|---|
| 1 | vehicle and tank description builders | `mov r15b,1` after a found `support` and after a found `repair` → `nop` (4 places) | the engine does not create `ammunition_recharge` if the vehicle already has `refuel`/`support`/`repair`. Without this our vehicles have no ammunition component → no F4 button |
| 2 | `AmmoRechargerFacet` / `RefuellerFacet` slots 14 / 13 | `activate` / `deactivate` → call the original + enable/disable the spare parts of the same vehicle | F4 mode enables only the vehicle's first "recharging" component; spare parts are a different interface and do not get into the mode by themselves |
| 3 | the same classes, slot 10 `update` | the original + the spare parts `update` with the same `dt`, while the component is enabled | **the engine never calls the spare parts update by itself** — only the "top up on click" order ticks it. Spare parts that are enabled but not ticked do nothing |
| 4 | `SparesRechargerFacet` slot 13 `deactivate` | do not disable while the vehicle's ammunition/refueller is active | the click order disables spare parts when it finishes — this would cut off distribution in F4 mode |
| 5 | spare parts target search | radius: the constant 20 m → the global `supplyRadius` (`movss` with a different address) | the spare parts zone matches the drawn circle. Currently `supplyRadius` is also 20; the patch is in case the game changes it. The 20.0 constant itself is left alone: other code reads it too |
| 6 | `AmmoRechargerFacet` circle drawing | redraw the circle with the `sparesRecharge` / `repairRecharge` material | see 5.3 and 5.6 |
| 7 | `AmmoRechargerFacet::update` (the same hook 3) | for a vehicle with repair: a repair pulse every 0.5 s (with spare parts — together with them) | repair of modules and armor for vehicles in the zone — see 5.6 |
| 8 | `RepairerFacet` slots 14 / 13 / 10 | `activate` / `deactivate` → the original + the same for ammunition; `update` → nothing | on HEMTT Repair, F4 mode controls the repair component, not the ammunition one — see 5.6 |

### 5.3 F4 mode lifecycle with the plugin

```
player presses F4
  └─ AiUnpack/AiDoRechargeState → AmmoRechargerFacet::activate        (hook 2)
        ├─ original: enable ammunition, draw the circle (ammoRecharge)
        ├─ syncSpares(on): find the vehicle's spare parts → SparesRechargerFacet::activate
        └─ convertCircle: redraw the circle with the sparesRecharge material  (patch 6)
every frame
  └─ engine → AmmoRechargerFacet::update                                (hook 3)
        ├─ original: hand out ammo from the vehicle's stock
        └─ component enabled → SparesRechargerFacet::update(dt):
              every 1/rate s → find vehicles within supplyRadius         (patch 5)
              → top up spare parts, deduct from own stock
player folds up the mode
  └─ AmmoRechargerFacet::deactivate → original (remove the circle) → spare parts off
```

**Circle (patch 6).** The zone circle is drawn by a function of the ammunition component: it takes the material from the
material table at the offset hard-coded into the instruction `lea r8,[rax+0x120]` (`0x120` — ammoRecharge, `0xE0` —
sparesRecharge). The plugin keeps this instruction writable and on a redraw substitutes `0xE0` for the duration of one
function call, then restores `0x120` — the other vehicles still draw their own circles. Redrawn circles are remembered
(`g_ownCircles`) so that they are not redrawn every frame. The redraw is called from both `activate` and
`update` — the latter is needed for a circle created without the plugin (when loading a save).

### 5.4 Installing the patches

Installation is shared by both DLLs — `install(img, patcher)`; where the patches go is decided by `Patcher`. For the
standalone DLL this is `PatchSet` (writes by itself, immediately), for the DefianceLoader plugin it is requests to the
loader (5.7).

The standalone DLL's `DllMain` only opens the log and registers a `DllWatcher` on `logic.dll`. When that one loads:

1. `resolve()` — search for all addresses:
   - five vtables via RTTI: ammunition, refueller, spare parts, repair, modules (`DamageableModulesFacet`);
   - code spots by patterns, each exactly once; "support blocks ammo" — exactly 2;
   - "repair blocks ammo" — the common pattern `mov r15b,1; …; lea rcx,[component name]` occurs 5 times; the needed
     2 are picked by the string `[Leonardo.RepairerFacet]` that the `lea` points to (`Image::findString`);
   - addresses of the functions the plugin calls itself — from a `call` inside already found code: `incHealth` (from
     `RepairerFacet::update`), "world diplomacy" (from `canRecharge`, whose start is checked), "material table"
     (12 bytes before the circle material `lea`);
   - the `supplyRadius` address — from the rip-relative instruction that reads it.
2. `Patcher` (here `PatchSet`): 4 byte replacements, the radius, 11 vtable slot replacements (each with a check of the
   function start), a check of the circle instruction. `apply()` — all or nothing.
3. The page with the circle instruction is made writable (for patch 6, `unprotectCircle`), and the log gets
   `SparesSupply active`.

If something is not found or does not match, the log says what exactly, and the game runs unchanged. The mod data
is still loaded: HEMTT Spares exists and tops up spare parts on click, just without the F4 button;
HEMTT Repair drives, but repairs nothing.

### 5.5 Diagnostics

`bin\plugins\SparesSupply.log`: version, `logic.dll` address, found vtables and functions, the list of applied
patches with RVAs. During the game — the first 50 events (spare parts and repair on/off, circle redraw), the first
20 seconds of the spare parts zone's work (`radius`, `targets` — how many vehicles the component found in the zone) and
the first 40 repair pulses (`repair: target=… +N hp, M armor layers, total hp …`, or `repair: fire out target=…`), and
the number of vehicles the game put into the repair zone whenever it changes (`repair: N vehicles in the zone`, up to
40 lines; who gets into that list is up to the game's own collector `0x120470`). Faults are always logged, even with
`log = false`: `fault in F4 tick: exception C0000005 at logic.dll+0012ABCD - skipped (fault 1 of 3)` — in which
part, the exception code, the module and the offset in it; after the third — `too many faults: the mod's supply work is off …`.

### 5.6 HEMTT Repair: repair and armor

**Why not the engine's repair.** The `RepairerFacet` component (ability `repair`) is unfinished in the engine:

- it is created disabled, and nobody enables it;
- its `update` (`0x363790`) searches for vehicles in a radius, but throws the search result away at once — the target
  list it then repairs from is always empty;
- it does not check the target's team at all (the search filter excludes only the vehicle itself) — it would repair
  enemies too;
- it repairs one module per pulse and does not touch armor.

So the plugin does the repair, and `RepairerFacet` serves as a marker. But it cannot be dropped entirely:
`IRepairerFacet` inherits `IRechargerFacet`, and the repair component is created before the ammunition one — F4 mode
takes the vehicle's first dispenser and enables/disables **it**. So the plugin hooks slots 14/13 of `RepairerFacet`: the
flags are set as usual (F4 mode asks "is it enabled"), and enabling is mirrored to the ammunition component.
The repair component's own `update` (slot 10) is stubbed out.
Repair takes its targets from the ammunition component: in its `update` that one already collects the vehicles within
`supplyRadius` (the list `[ammo+0x30]`, with the same search as the repair one) — and draws exactly this circle.

**Spare parts, ammo and repair on one vehicle** (`ammunition_recharge,support,repair` with an ammo stock). F4
enables the repair component (it is the first dispenser), it enables ammunition, ammunition enables spare parts. Every
frame, after the ammunition `update` (which hands out ammo), the spare parts are ticked and, since the vehicle has
repair, a repair pulse runs (`SupplyPort::repairs`). There is one circle — with wrenches (`kindOf` gives `Spares`).
Repair pays with the vehicle's spare parts (`[modules+0x130]`); whether this is the same stock the spare parts component
hands out from is not verified. Fuel is not part of this: the engine never creates `ammunition_recharge` next to
`refuel`.

**Repair pulse** (`tickRepair`, every `kRepairPeriod` = 0.5 s while F4 mode is on). For each vehicle
in the list:

1. Skips HEMTT Repair itself and non-friendlies. "Friendly" means the same as in `AmmoRechargerFacet::canRecharge`:
   world diplomacy (`0x541a00`) compares the teams of the target and the owner; relation 2 or 4 — not allowed. A target
   without a team — not allowed either. So HEMTT Repair repairs the same vehicles HEMTT Supply would give ammo to.
   **Except abandoned vehicles:** when the crew leaves a vehicle (bails out of a fire, for one), the game moves it to
   its own team named `abandoned` (team `vfunc(+0x68)` is the name), which diplomacy does not call friendly. Such a
   vehicle is kept without asking diplomacy: anyone may capture it, and whose it was is not known.
2. Finds the vehicle's `DamageableModulesFacet` (infantry has none — skip). A vehicle with total health 0
   (destroyed) is skipped.
   **Fire first.** If a module is in its burning damage state (the first state whose effects in the module
   description include `smoke`, flag `0x8000`) while the game still bans the crew from boarding (TransportHelper
   `isBlocked`), the vehicle is on fire: the flames and the ban start together and last the same
   `damagedUnitBlockTime` (60 s). The pulse puts it out and does nothing else to this vehicle: `incHealth(module, 0)`
   (it adds nothing but always takes the flames off first, `0x25790`) and TransportHelper `unblock`. Free of charge,
   so it also works with an empty stock. Repair goes on from the next pulse. A fire that has burned out by itself
   (no ban any more) is simply repaired; so are modules that burn without a crew ban — their flames go off with
   the first `incHealth` anyway. Details — `engine_notes.md`, "Module fire".
3. **Modules:** each damaged one gets `+max(1, 2% of maximum)` via `DamageableModulesFacet::incHealth`
   (`0x225ba0`). This is the same path as crew repair: the engine itself recalculates total health, the module state
   ("damaged" / "destroyed"), visuals and sync. From zero to full takes about 25 s.
4. **Armor:** each layer on each of the 6 sides of each module gets `+2%` of the original thickness, but no more than
   the original. The current thickness is a `vector<float>` in the module state, the original one is in the module
   description (layout in `engine_notes.md`). The damage function reads the current thickness directly, so restored
   armor works again immediately. The engine has no "restore armor" function of its own — the values are written
   directly.

Speeds are the constants `kRepairPeriod`, `kRepairFraction`, `kArmorFraction` at the top of the plugin.
**The cost is HEMTT Repair's own spare parts** (it has 10000; HEMTT Spares can top them up). A module costs 1 spare
part per 1 restored health, armor — `kSparesPerMm` (1) spare part per 1 mm of thickness; the fractional armor cost
accumulates and is deducted in whole spare parts. `incHealth` is crew repair, and it deducts spare parts from the
**target** by itself (`[modules+0x130]` minus the health gain); the plugin remembers the target's stock before the call
and restores it afterwards — the target does not pay. The gain per module is limited by the remaining stock; when the
stock is empty, the pulse repairs nothing (`repair: out of spare parts` in the log) and only puts out fires.

**Zone circle.** The material is taken from a table where we have no row of our own, and the offset in `lea` (as for
spare parts) cannot point beyond the table. So for the duration of one circle function call the plugin replaces the
`ammoRecharge` string itself in the table (MSVC `std::string`: pointer, length, capacity) with the path
`materials/rounds/repairRecharge.material` and immediately restores it. Before this, the table is initialized by
calling its own function; the path string is static in the plugin, so it lives for the whole game session.

### 5.7 DefianceLoader plugin — `spares_repair.dll`

[DefianceLoader](https://github.com/DefianceLoader/DefianceLoader) is a plugin loader for this game
(`bin\dxgi.dll`). Plugins live in `DefianceLoader\plugins`, each with a `.plugin.json` manifest next to it. Its main
rule: **every byte of the game belongs to one plugin**. All writes are done by the loader itself; two plugins
writing to the same place cannot coexist — the one that starts later is disabled entirely.

So `spares_repair.dll` writes nothing into `logic.dll` itself:

1. The loader calls `init` (export `defiance_plugin`) when `logic.dll` is already loaded. The plugin ID is
   `sokolov.spares-repair`, which is also `name` in `DefiancePlugin`; the version is `MOD_VERSION`, as in the manifest
   (CMake generates the manifest from [`spares_repair.plugin.json.in`](../plugins/spares_supply/spares_repair.plugin.json.in)).
2. `install()` — the same search and the same checks as in the standalone DLL (5.4), but `Patcher` is
   `DefiancePatcher`: each patch is a `patch_bytes` request (replace these bytes with those). A vtable slot is also just
   8 bytes: game function address → hook address. The loader writes everything after `init`, together with the patches
   of other plugins.
3. The same list is returned by the `defiance_patch_contract_v1` export, and the manifest has `"patch_contract": true`:
   the loader checks that `init` asked for exactly this, and otherwise does not start the plugin.
4. The plugin changes the circle instruction (patch 6) itself during the game — the loader does not know about it. So
   that another plugin does not claim it, `init` requests it from the loader "unchanged" (bytes before = bytes after).
   Making it writable during `init` is not possible: the loader has not written yet and may restore the page's previous
   protection. So `unprotectCircle` is deferred until the first redraw of the HEMTT Spares circle (`g_circleDeferred`).

If anything at all is not found or does not match, `init` returns an error, the loader discards all its requests, and
the game runs unchanged. The log is in `DefianceLoader\logs\defiance-loader.log` (lines with the plugin ID).

**Both DLLs at once.** The standalone DLL patches `logic.dll` at the moment it loads, before any loader plugin.
Then `init` sees addresses from a foreign module in the slots, refuses and logs which module:
`… already points into SparesSupply.dll … remove bin\plugins\SparesSupply.dll`. The standalone DLL works.

**`defiance.supply-radius`** (a loader plugin, disabled by default) rewrites the same spare parts radius instruction
(patch 5). If it is enabled, `init` does not search for or touch the radius (`g_sparesRadius = false`): that plugin
controls the spare parts zone radius, and everything else works as usual.

**Settings** (section `[sokolov.spares-repair]` in `DefianceLoader\config\settings.ini`): `enabled` — checked by the
loader itself; `log` — whether to write event lines (5.5); installation lines are always written.

## 6. Build and release

```
cmake --build <build> --target release
  ├─ nested Release build: winhttp.dll, SparesSupply.dll (-O3, -s, static, UCRT)
  ├─ mod/build_mod.py   → <build>/release/stage/mods/{SparesSupplyRepairFramework,HEMTT_Spares}
  ├─ mod/package.py         → dist/SparesSupplyRepairFramework_v<MOD_VERSION>.zip   (framework only)
  └─ mod/package.py --mod   → dist/HEMTT_Spares_v<MOD_VERSION>.zip  (framework + example, + _cover.png, _banner.png)
cmake --build <build> --target release_defiance
  ├─ the same with spares_repair.dll: SparesSupplyRepairFramework_DefianceLoader_v…zip, HEMTT_Spares_DefianceLoader_v…zip
  ├─ mod/build_mod.py --tables patch → <build>/release/stage/mods_tablemerge/{…,HEMTT_Spares}
  └─ mod/package.py --defiance --tablemerge --mod → HEMTT_Spares_DefianceLoader_TableMerge_v…zip
```

Five bundles — for a single mod page: a player takes the framework alone (if installing someone else's vehicles built
on it) or together with HEMTT Spares, in the standalone version or under DefianceLoader; under DefianceLoader HEMTT
Spares also comes as the table-merge build (vehicles only, see section 2). The player README is the same
for all of them ([`Dist-readme.md`](Dist-readme.md), variants as `{IF ...}` blocks), and the modder guide
([`MODDING.md`](MODDING.md)) goes into all of them.

The DLLs are linked fully statically, with the system UCRT — they need only `KERNEL32.dll` and `api-ms-win-crt-*`,
which exist in any Windows 10/11 (in the MinGW bundled with CLion, the static `libwinpthread` is built against UCRT, so
`-mcrtdll=ucrt` is mandatory). Targets and environment setup are in the main [`README.md`](../README.md).

## 7. Limitations

- Single player only: the patches change the game logic, multiplayer would desync.
- HEMTT Repair is a test vehicle: light vehicle class, repair speeds not tuned, tested offline only.
- Conflicts with mods that replace the same files (list in [`INSTALL.md`](../INSTALL.md)), and with other
  programs that use `bin\winhttp.dll`. The table-merge build does not replace tables, but it adds the vehicles to no
  shop or army.
- If the game updates and the code changes, the plugin will refuse to work (see the log); the patterns must be updated:
  find the new spot in `re/` and get a pattern with `re/mksig.py`.
