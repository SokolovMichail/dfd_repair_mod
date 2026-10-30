// HEMTT Spares rules. Overview - spares.h.

#include "spares.h"

namespace spares_supply::core {

void syncSpares(SparesPort& port, Station* s, bool on) {
    SparesDepot* d = port.depotOf(s);
    if (!d) return;
    if (on && !port.isOn(d)) {
        port.event("spares ON  (with supply mode)", d);
        port.turnOn(d);
    } else if (!on && port.isOn(d)) {
        port.event("spares OFF (with supply mode)", d);
        port.turnOff(d);
    }
}

SparesDepot* tickSpares(SparesPort& port, Station* s, float dt) {
    if (!port.stationOn(s)) return nullptr;
    SparesDepot* d = port.depotOf(s);
    if (d && port.isOn(d)) port.tick(d, dt);
    return d;
}

bool mayTurnSparesOff(SparesPort& port, SparesDepot* d) {
    if (!port.supplyModeActive(d)) return true;
    port.event("spares OFF skipped (supply mode active)", d);
    return false;
}

}  // namespace spares_supply::core
