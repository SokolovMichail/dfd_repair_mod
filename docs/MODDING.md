# Making vehicles for the SparesSupply & Repair Framework

The SparesSupply & Repair Framework adds two zone mechanics to Terminator: Dark Fate - Defiance. Any vehicle
can use them; your mod only describes the vehicle in the game's tables. No code is needed.

- **Spare parts zone.** A deployed vehicle (F4, Supply Mode) refills the spare parts of every vehicle in its
  zone, the way HEMTT Supply hands out ammunition. The zone is drawn with wrench icons.
- **Repair zone.** A deployed vehicle repairs own and allied vehicles in its zone, twice a second: every
  damaged module gets 2% of its maximum health, and every worn armor layer 2% of its original thickness. It
  pays with its own spare parts: 1 per health point, 1 per millimetre of armor. The zone is drawn with
  wrench-and-hammer icons.

HEMTT Spares, the framework's example mod, adds one vehicle of each kind.

## What the framework is

| Part | Where | What it does |
|---|---|---|
| Plugin | `bin\winhttp.dll` + `bin\plugins\SparesSupply.dll`, or the DefianceLoader plugin `DefianceLoader\plugins\spares_repair.dll` | the zone mechanics, in the game's `logic.dll` |
| Mini-mod | `mods\SparesSupplyRepairFramework` | the materials and textures of both zone circles |

Players install the framework once, then your mod. In the game's MODS menu they enable both
"SparesSupply & Repair Framework" and your mod.

## Make a vehicle

Add a row to `scripts/species/cars.xml` or `scripts/species/tanks.xml`. Other tables (helicopters, for
example) are not supported. The easiest start is to copy a supply vehicle's row, such as `Fnd_HEMTT_ammunition`,
and change these cells.

### A spare parts vehicle

| Cell | Value | Why |
|---|---|---|
| `abilities` | `ammunition_recharge,support` | `support` holds and hands out spare parts; `ammunition_recharge` gives the F4 button |
| `spares` | the stock, e.g. `10000` | how many spare parts it carries and hands out |
| `capacity_ammo_recharger` | any value, e.g. `500` | needed by the ammunition part; the vehicle never hands out ammunition |

Without the framework the game never builds `ammunition_recharge` next to `support`. Such a vehicle then
only resupplies spare parts on a direct order, and has no F4 zone.

### A repair vehicle

| Cell | Value | Why |
|---|---|---|
| `abilities` | `ammunition_recharge,repair` | `repair` marks it as a repair vehicle; `ammunition_recharge` gives the F4 button |
| `spares` | the stock, e.g. `10000` | what it pays for the repairs with |
| `capacity_ammo_recharger` | any value, e.g. `500` | as above |

The repair vehicle:
- does not repair itself or destroyed vehicles;
- repairs only own and allied vehicles;
- stops when its spare parts run out.

Without the framework it does nothing.

### Rules for both

- **Give a vehicle `support` or `repair`, not both.** With both, it works as a spare parts vehicle.
- **Ammunition.** A vehicle with `support` or `repair` never hands out ammunition, neither in the zone nor
  on a direct order.
- **Radius.** Both zones use the game's supply radius, the same one as ammunition supply.
- **Fuel.** A vehicle with `refuel` and `support` also gets the spare parts zone; F4 then switches the
  refuelling and the spare parts together.
- **Tuning.** The repair speeds and prices are the same for every vehicle; they are not set per vehicle yet.

## Things to know

- **The game replaces whole files.** A mod that ships `scripts/species/cars.xml` replaces the game's
  `cars.xml` and every other mod's. Two vehicle mods that both ship it conflict, HEMTT Spares included: in
  MODS only the higher one wins. Ship only the tables you change. DefianceLoader has an experimental
  "mod table merge" feature that patches tables row by row instead.
- **Do not ship the framework's files.** Do not include its DLLs or `mods\SparesSupplyRepairFramework` in
  your mod; ask players to install the framework. Two copies of the plugin do not work together.
- **Your vehicle also needs:**
  - a shop or army entry, so the player can get it;
  - a localized name;
  - optionally, an encyclopedia article and icons.

  HEMTT Spares does all of these.
- **Check it works.** Look in the framework's log: `bin\plugins\SparesSupply.log`, or the DefianceLoader log
  for `sokolov.spares-repair`. It records the first F4 events with the vehicle's address:
  - `spares ON  (with supply mode)` — a spare parts vehicle went into F4;
  - `repair ON  (with supply mode)` — a repair vehicle did;
  - `repair: target=…` — the first repairs.

## The example: HEMTT Spares

HEMTT Spares clones `Fnd_HEMTT_ammunition` twice. The clones differ from the original only in these cells:

| Vehicle | `abilities` | `spares` | `capacity_ammo_recharger` |
|---|---|---|---|
| HEMTT Spares (`Fnd_HEMTT_spares`) | `ammunition_recharge,support` | `10000` | `500` |
| HEMTT Repair (`Fnd_HEMTT_repairer`) | `ammunition_recharge,repair` | `10000` | `500` |

Plus a new name, icons and `unit_set=light_vehicle`. The rows go into `cars.xml` of the base game and of
both DLCs (Resistance, Canada), and the vehicles into shops, the Founders' skirmish army and the
encyclopedia. Open `mods\HEMTT_Spares` to see every file.
