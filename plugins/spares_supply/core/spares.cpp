// HEMTT Spares rules. Overview - spares.h.
//
// Handing out spare parts in a radius is already in the game (its spare parts component can do it); it only
// was never connected to F4 mode. These rules connect it: the component follows the station.

#include "spares.h"

namespace spares_supply::core {

// Called when F4 mode is switched on or off; turnOn / turnOff are the game's originals, so our own "may it be
// switched off" check (mayTurnSparesOff) does not stand in the way here.
void syncSpares(SparesPort& port, Station* s, bool on) {
    SparesDepot* d = port.depotOf(s);
    if (!d) return;  // not HEMTT Spares
    if (on && !port.isOn(d)) {
        port.event("spares ON  (with supply mode)", d);
        port.turnOn(d);
    } else if (!on && port.isOn(d)) {
        port.event("spares OFF (with supply mode)", d);
        port.turnOff(d);
    }
}

// Called every frame for the station (from onSupplyTick, and for a refueller - by the binding directly).
SparesDepot* tickSpares(SparesPort& port, Station* s, float dt) {
    if (!port.stationOn(s)) return nullptr;
    SparesDepot* d = port.depotOf(s);
    // A switched-off component is not ticked, but still returned: the caller uses it to tell HEMTT Spares
    // apart (QUIRK: so its circle is repainted even then).
    if (d && port.isOn(d)) port.tick(d, dt);
    return d;
}

// Called when the game is about to switch the spare parts component off.
bool mayTurnSparesOff(SparesPort& port, SparesDepot* d) {
    if (!port.supplyModeActive(d)) return true;
    port.event("spares OFF skipped (supply mode active)", d);
    return false;
}

}  // namespace spares_supply::core
