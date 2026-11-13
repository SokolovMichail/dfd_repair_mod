# Notes on the Terminator: Dark Fate – Defiance engine

Everything is checked on GOG 1.09.1221 (`logic.dll` TimeDateStamp `6AB513E0`). Addresses are RVAs inside the DLL
(image base `0x180000000`; for `.text` the file offset = RVA − `0xC00`). The plugins don't hard-code addresses, they
find them by patterns; the addresses here are for finding the place quickly with the tools in `re/`.

`docs/TDF_HEMTT_Spares_Handoff.md` is the original plan (from before the implementation), kept for history.

## Data and layers

- `.pak` files are zip archives (ZipCrypto, password in `DFD_PAK_PASSWORD`). Tables are Excel-XML.
- Every patch contains a file **whole**; a mod also replaces a file whole.
- Layer order: `basis.pak → patch_NNN*.pak → basis/ (unpacked) → localization → mods/<mod>/basis (prio 24)
  → … → dlc → mods/<mod>/dlc/<DLC>/basis (prio 34) → DLC localization`. The mounted layers are visible in `game.log`
  (lines `FsMounter ... Mounted`).
- **Skirmish mounts only the main layer**, the Resistance campaign mounts both. The skirmish factions' vehicle lists
  are in `scripts/default_army.toml`.
- Unit localization: the name is in `locale/cars.xml` (column `content`), the article in `encyclopedia_loc.xml` /
  `encyclopedia_dlc_loc.xml` (by `sysname`).
- Icons: `tech_pic` → `textures/ui/pictures/tech_pic/<name>.dds`; the game finds the `_preselected` / `_selected`
  variants itself (`game.dll`: `ui/pictures/tech_pic/{}_preselected`). Vehicle icons are DDS A8B8G8R8
  (R mask = `0xFF`, bytes RGBA!), the mod icon is A8R8G8B8 (bytes BGRA).
- A mod: `mods/<folder>/mod.json` (`name`, `description`, `icon` — a path relative to the mod folder).
  Enabled mods are in `Documents\My Games\Terminator Dark Fate\mods\mods.json` (for the user — in OneDrive).

## Abilities (column `abilities`) → components (`logic.dll`)

| abilities | component | vtable (GOG) |
|---|---|---|
| `refuel` | `RefuellerFacet` | `0x733d20` |
| `ammunition_recharge` | `AmmoRechargerFacet` | `0x7108b0` |
| `support` | `SparesRechargerFacet` (spare parts) | `0x73d958` |
| `repair` | `RepairerFacet` (module repair, unfinished) | `0x733fd8` |

- Description builders: cars `0x51c570`, tanks `0x521ce0`. **`ammunition_recharge` is not created if there is
  `refuel`/`support`/`repair`** (`mov r15b,1` after each of them; support — `0x51ce31` / `0x52238f`,
  repair — `0x51ced2` / `0x522432`). After `mov r15b,1` comes `lea rcx,[rip+"[Leonardo.<Component>]"]` — that string
  tells the places apart.
- Common slots of the "dispenser" components: 10 `update(dt)`, 12 `isActive`, 13 `deactivate`, 14 `activate`.
  The "off" flag: Ammo/Refueller — byte `+0x59`, Spares — `+0x51`. They are called only through the vtable.
- The component's owner (the vehicle) is `[facet+0x10]→[+0x10]`. Walking a vehicle's components: `entity->vfunc(+0x58)`
  hands out an iterator (`+0x18` end?, `+0x30` current, `+0x20` next, `+0x00` deleting destructor),
  see `0x57490` / `0xce070`.

## F4 supply mode ("SUPPLY MODE")

- The button is the `Pack` ability (id 7). It is given in `0x12a3c6` if the vehicle has ammunition or a refueller
  (AiFacet slots 87 / 86, the answers are cached in `+0x1b8..+0x1bf`). Spare parts play no part in the condition.
- The AI states `AiUnpackRechargeState` / `AiDoRechargeState` (ctor `0xbfad0`) / `AiPackRechargeState` take the
  vehicle's **first `IRechargerFacet`** (`0x57490`) and switch only it on and off.
- The engine calls the Ammo/Refueller updates (`0x120b90` / `0x35f3f0`) every frame.
  **The engine does not call the spare parts update `0x439e00`** — only the click order ticks it
  (`AiSparesRechargeUnitState::update` `0xffac0`, `jmp [facet+0x50]`).
- Spare parts target search — `0x439bc0`, radius — the constant 20.0 (`0x74ff44`, shared with other code!).
  `supplyRadius` (global `0x875f4c`) is 20 in the game too. `speedRechargeSpares` = 100/s (`invs.xml`).
- `AmmoRechargerFacet::canRecharge(target)` — slot 17 (`0x120900`); both the zone and the click order go through it.
  Its team check: `dip = 0x541a00(owner->vfunc(+0x78))` (world diplomacy); an entity's team is
  `[entity->vfunc(+0xb0) + 0x20]`, its id is `team->vfunc(+0x50)`; `dip->vfunc(+0xb0)(target id, owner id)` = 2 or
  4 — not allowed (which of them is "enemy" and which is "neutral" was not found out).
- Ammo zone targets: `0x120470` (from `update`) fills `[ammo+0x30..+0x38)` with `RefBlock*` of the entities within
  `[ammo+0x5c]` (= `supplyRadius`) that have `[vfunc(+0xb0)+0x28]` and `[+0x38]->vfunc(+0xa8)` = true.
  The physics query filter (vtable `0x74c4a0`) drops only the vehicle itself.
- Zone circle: `AmmoRechargerFacet` draws it with the function `0x120610`, the material comes from the table `0xff050`
  (`+0xe0` sparesRecharge, `+0x100` fuel, `+0x120` ammo; the instruction `lea r8,[rax+0x120]` at `0x120687`).
  The circle object is `[facet+0x50]`. The vanilla `sparesRecharge.material` points to the plain `circle_generic.dds`.
  The table `0xff050` is 11 `std::string` rows of 0x20 bytes (`materials/rounds/*.material`: maxAttack, minAttack,
  tnt, emp, smoke, …, sparesRecharge, fuelRecharge, ammoRecharge, …), initialized on the first call.
  The circle function passes the path itself by reference into `[scene]+0x890`.

## Repair (`RepairerFacet`) — unfinished

- vtable `0x733fd8`: 10 `update` `0x363790`, 11 `[+0x48]`, 12 isActive (`[+0x49]==0`), 13 deactivate
  (`[+0x49]=1`), 14 activate (`word [+0x48]=0`), 15 radius `[+0x5c]`.
- It is created switched off (`66 C7 47 48 00 01` at `0x261b2a` / `0x261cc5`); nobody calls the switch-on (`+0x70`).
  Two factories: `0x261b90` (full: `[+0x38]`, `[+0x40]`, period 0.5 s, radius `supplyRadius`) and `0x261a30`
  (`[+0x38]=0` — `update` would crash); loading from a save — `0x363290`.
- `update` `0x363790`: a pulse every `[+0x4c]` s; a physics query within `[+0x5c]` puts its result into a local
  vector and **frees it right away**, while the repair goes over the list `[+0x20..+0x28)` (0x18 bytes each: `+8`
  entity, `+0x10` its `DamageableModulesFacet`), which nobody fills. If it were filled: targets with
  `IDamageableModulesFacet` (`0x3630e0`), no team check; one damaged module per pulse by
  `max(1, 1%)` (`0x74fcb0` = 0.01) through `incHealth` — that is, paid with the target's own spare parts (see below).
- `AiFacet[89]` hasRepairer / `[90]` canRepair are the only users of `IRepairerFacet` (typeid `0xc5c40`;
  `0xc5d30` — `RepairerFacet`).
- **`IRepairerFacet` inherits `IRechargerFacet`** (typeid `0x320f0`, the one `0x57490` looks for). Components are
  created in the order refuel, support, repair, …, ammunition_recharge — on a vehicle with `ammunition_recharge,repair`
  the first dispenser is the repair one, and F4 mode switches it on, not the ammunition.

## Modules and armor (`DamageableModulesFacet`)

- vtable `0x723170` (the second one, `0x723160`, is the `+0xa0` subobject). Slot 19 (`+0x98`) — number of modules,
  `+0xa0` — health of module `i`; `getModuleMaxHealth` `0x225810`, `incHealth(i, amount)` `0x225ba0`
  (clamps to the maximum, updates `+0xcc`, the module state, visuals, network sync `[+0xc0]`).
- Fields: `+0xc8` maximum total health, `+0xcc` current total, `+0xd0..+0xd8` — vector of modules, `0x148` bytes each,
  `+0x130` the vehicle's spare parts (int), `+0x12c` their maximum. Slots 26/27 — spare parts / maximum, 28 — take
  (`0x2281b0`, not below 0), 29 — add (`0x2281d0`, not above the maximum).
- `incHealth` is crew repair: **it takes as many spare parts from `+0x130` as it added health** (not below 0;
  with zero spare parts it still repairs).
- Module: `+0x00` description (static), `+0x0c` health (int), `+0x28 + side*0x18` — `vector<float>` of the
  current armor layer thickness; 6 sides.
- Module description: `+0x28` maximum health (int), `+0x90 + side*0x18` — vector of layers, 16 bytes each
  (`+0` original thickness float, `+8` armor type: pairs of coefficients per damage class from `+0x28`).
- Only the damage function `0x310790` (wear: `t = max(t - P/B, 0)`) and `DamageableModulesFacet::initialize`
  `0x226280` (a copy of the original when the unit is created) write the current thickness. The engine has no
  restoring in battle — the HEMTT Repair plugin writes the values directly, and the damage function reads them
  directly too.

## Other

- Lua 5.1, only `base/string/table/math` are open; `console.log` doesn't write to `game.log`. The `game.*` API has no
  repair. `game.disappear` on tractor + trailer pairs crashes the game.
- `cheats.godMode` (`0x52dff0`) blocks all damage to the player's units.
- The difficulty's damage multiplier for infantry is applied only without network sync — it doesn't work in
  skirmish.
- The game doesn't support native plugins: `galileo.dll` loads DLL components by hard-coded names. Hence the
  `winhttp.dll` proxy (`game.dll` and `libcef.dll` import it; the CEF helper processes are started with `--type=`).
