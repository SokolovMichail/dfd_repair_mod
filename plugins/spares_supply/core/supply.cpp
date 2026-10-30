// Common F4 mode rules. Overview - supply.h.
//
// The game itself switches the station on and off and runs its own per-frame update; the binding calls these
// functions right after the game's original, so here we only add what the mod changes.

#include "supply.h"

namespace spares_supply::core {

// F4 pressed: the game has switched the station on and drawn the ordinary ammo circle.
void onSupplyOn(Ports& p, Station* s) {
    // HEMTT Spares: its spare parts component is a different kind of component, F4 mode does not reach it on
    // its own - switch it on together with the station. Vehicles without spare parts are left alone.
    syncSpares(p.spares, s, true);
    Kind k = p.supply.kind(s);
    if (k == Kind::Repair) p.supply.event("repair ON  (with supply mode)", s);
    // Redraw the zone circle with the vehicle's own material (spanners / wrench and hammer); an ordinary
    // HEMTT Supply keeps the ammo circle.
    p.supply.showCircle(s, k);
}

// F4 mode is over: the game has switched the station off and removed its circle.
void onSupplyOff(Ports& p, Station* s) {
    forgetRepair(p.repairState, s);
    syncSpares(p.spares, s, false);
}

// Every frame, after the game's own update of the station (which, for the mod's vehicles, hands out nothing:
// handsOutAmmo is false for them, but still collects the vehicles in the zone).
void onSupplyTick(Ports& p, Station* s, float dt) {
    if (!p.supply.isOn(s)) return;
    // The game never updates spare parts by itself (only the click order "resupply" does), so switched-on spare
    // parts would hand out nothing - tick them here. A vehicle with spare parts counts as HEMTT Spares even if
    // it also has a repair component, same as kind().
    if (tickSpares(p.spares, s, dt)) {
        // also catches a circle the game created without us, e.g. when a save is loaded
        p.supply.showCircle(s, Kind::Spares);
    } else if (p.supply.kind(s) == Kind::Repair) {
        // The game's own repair component is unfinished and does nothing useful - the repair is ours.
        tickRepair(p.repair, p.repairState, s, dt);
        p.supply.showCircle(s, Kind::Repair);
    }
}

// The mod's vehicles carry the ammo component only to get the F4 button; their 500 rounds go nowhere.
bool handsOutAmmo(Kind kind) { return kind == Kind::Ammo; }

}  // namespace spares_supply::core
