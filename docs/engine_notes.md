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
  with zero spare parts it still repairs). It has no check on the amount: `incHealth(i, 0)` is valid.
- Module: `+0x00` description (static), `+0x0c` health (int), `+0x28 + side*0x18` — `vector<float>` of the
  current armor layer thickness; 6 sides.
- Module description: `+0x28` maximum health (int), `+0x30` — `vector<uint64>` of effect flags of the damage
  states (see "Module fire"), `+0x48` — a vector of 4 parameter records (0x18 bytes each), `+0x90 + side*0x18` —
  vector of layers, 16 bytes each (`+0` original thickness float, `+8` armor type: pairs of coefficients per damage
  class from `+0x28`), `+0x220` — `burn_effect_name` (`std::string`), `+0x240` — the node name for it.
- Only the damage function `0x310790` (wear: `t = max(t - P/B, 0)`) and `DamageableModulesFacet::initialize`
  `0x226280` (a copy of the original when the unit is created) write the current thickness. The engine has no
  restoring in battle — the HEMTT Repair plugin writes the values directly, and the damage function reads them
  directly too.

## Module fire

What a player sees as a burning vehicle: flames on a module and the crew bailing out.

- **Module damage state** (the same formula in `incHealth` `0x225ba0`, `decHealth` `0x225fd0` and `0x24d50`):
  0 at full health, otherwise the float ratio health / maximum: ≥ `0.66f` (`0x74fda8`) — 1, ≥ `0.33f` (`0x74fd5c`) —
  2, > 0 — 3, exactly 0 — 4. The current state is a byte per module at `[[modules+0x120]+8][i]`.
- **Effects of a state:** the column `module_function` of `modules.xml` — 4 states separated by `;`, effects
  joined by `+`, an optional message in brackets: `speed_penalty(engine_damaged);move(engine_destroyed);
  smoke(inflammation)+unload_crew(bailing_out)+deny_crew_load;vital`. It is parsed by `0x3bbe80` (called from the
  module description parser `0x3ae790`) into the description: `+0x30` — `vector<uint64>` of flags, resized to
  exactly 4 (`0xd7540(vec, 4)`), element `s-1` for state `s`. Name → flag: `0x3bfa00` with the static table
  built in `0x3c3690`: speed_penalty `0x10`, kill_half_cargo `0x20`, kill_cargo `0x40`, kill_crew `0x80`,
  unload_crew `0x100`, unload_crew_morale `0x200`, kill_crew_member `0x400`, deny_crew_load `0x800`,
  scattering_penalty `0x1000`, kill_driver `0x2000`, unload_cargo `0x4000`, **smoke `0x8000`**,
  **burn_smoke `0x10000`** (plus none / vital / move / shoot / reload_penalty).
- **Entering a state** (`0x25910`, called by `decHealth` for every state from the old one + 1 to the new one): for
  each flag it does its effect. `smoke` (`0x26570`) starts the module's `burn_effect_name` (`module_burn:1.5`) at its
  node through `0x45ebf0` with the lifetime **`damagedUnitBlockTime`** (global `0x875ec0`). `deny_crew_load`: AiFacet
  `vfunc(+0x2a0)` → TransportHelper `vfunc(+0x240)(damagedUnitBlockTime)` — boarding is blocked for the same time.
  `invs.xml`: `damagedUnitBlockTime` = 60 ("boarding block time of a damaged unit, also the time the compartment
  smoke effect plays, seconds"; default in the code 30). So **the fire goes out by itself after 60 s**. It does no
  damage.
- In the data (base layer): 228 modules (engines, crew compartments, turrets — `Fnd_abrams`, `Fnd_hamvee`,
  `Fnd_HEMTT_*`, `Res_van`, `Int_tank`, …) have `smoke(inflammation)+unload_crew+deny_crew_load` in state 3; 26 more
  have `smoke` without the ban in state 3 (e.g. Legion turrets), 18 in state 2. `modules.xml` also has leftover rows
  (`t55_*` and others) that no unit uses.
- **Leaving a state** (`0x26150`, called by `incHealth` when the state drops): for every state from the old one down
  to the new one + 1 it undoes that state's flags: `smoke` — `0x45ee30` stops the flames, `deny_crew_load` —
  TransportHelper `vfunc(+0x250)` lifts the ban.
- **`incHealth` always calls `0x25790` first**, before it looks at the states: for every module with a
  `burn_effect_name` it calls `0x45ee30` — the flames go off on all modules with any repair. `0x45ee30(visual,
  &effect name, &node)` finds the effect in the visual's list `[visual+0x1d8..+0x1e0)` by name (the part before `:`)
  and node and stops it; nothing happens if there is no such effect. The visual facet is `0x27200(entity)`, the node
  is `0x27e20([modules+0x120]+0x30, &desc+0x240)`.
- **TransportHelper** (vtable `0x740228`): slot 72 (`+0x240`) `block(t)` — `[+0x250] = max([+0x250], t)`,
  `[+0x51] = 1`; slot 73 (`+0x248`) isBlocked — `[+0x51]`; slot 74 (`+0x250`) unblock — `[+0x51] = 0`; slot 82 —
  update, counts `[+0x250]` down and clears the flag at 0. Reached as `[entity->vfunc(+0xb0) + 0x28]` (AiFacet) →
  `vfunc(+0x2a0)` (`0x252a0`: `[ai+0x190]→[+0x10]`). Only `0x25910` calls `block`: for `deny_crew_load` (60 s) and
  for `burn_smoke` (99999 s).
- **Abandoned vehicles.** `unload_crew` (`0x26620`) empties the vehicle through the TransportHelper, and the vehicle
  ends up on a special team (player) named `abandoned`; there is also `civilian`. A team keeps its name as an MSVC
  `std::string` at `+0x120`, handed out by `vfunc(+0x68)`; the game compares it with `"abandoned"` / `"civilian"`
  itself (TransportHelper code at `0x476041`, `0x46e8d1`). The entity's team is `[entity->vfunc(+0xb0) + 0x20]`.
  World diplomacy does not call `abandoned` friendly, so a burning vehicle whose crew bailed out fails a
  `canRecharge`-style team check.
- **HEMTT Repair puts a fire out** (plugin): a module in its `smoke` state (or a worse one) while boarding is
  blocked — `incHealth(i, 0)` (flames off, sync) + `unblock`. Health and spare parts stay as they are. Vehicles on
  the `abandoned` team are put out and repaired whatever diplomacy says.
- **The engine's real fire, `burn_smoke`** (`0x10000`), is not used by any table of the game. When a module enters
  a state with it, `0x25910` starts the flames with a lifetime of 99999 s, blocks boarding for 99999 s and writes
  the module's index to `AiFacet+0x2a4` (`+0x2a8` = 0; -1 — nothing burns, set only by the AiFacet constructors
  `0xc6f10` / `0xc9df0` / `0xc9fd0`). `AiFacet::update` (slot 10, `0xcd1c0`) then adds `dt * moduleBurnSpeed` (global
  `0x875ec4`, `invs.xml`: 1 hp/s) to `+0x2a8` and takes the whole part from the module through `decHealth`. Nothing
  ever resets `+0x2a4`: such a fire never goes out, not even after a full repair. HEMTT Repair leaves it alone.

## Other

- Lua 5.1, only `base/string/table/math` are open; `console.log` doesn't write to `game.log`. The `game.*` API has no
  repair. `game.disappear` on tractor + trailer pairs crashes the game.
- `cheats.godMode` (`0x52dff0`) blocks all damage to the player's units.
- The difficulty's damage multiplier for infantry is applied only without network sync — it doesn't work in
  skirmish.
- The game doesn't support native plugins: `galileo.dll` loads DLL components by hard-coded names. Hence the
  `winhttp.dll` proxy (`game.dll` and `libcef.dll` import it; the CEF helper processes are started with `--type=`).
