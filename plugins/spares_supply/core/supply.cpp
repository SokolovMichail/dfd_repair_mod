// Common F4 mode rules. Overview - supply.h.

#include "supply.h"

namespace spares_supply::core {

void onSupplyOn(Ports& p, Station* s) {
    syncSpares(p.spares, s, true);
    Kind k = p.supply.kind(s);
    if (k == Kind::Repair) p.supply.event("repair ON  (with supply mode)", s);
    p.supply.showCircle(s, k);
}

void onSupplyOff(Ports& p, Station* s) {
    forgetRepair(p.repairState, s);
    syncSpares(p.spares, s, false);
}

void onSupplyTick(Ports& p, Station* s, float dt) {
    if (!p.supply.isOn(s)) return;
    if (tickSpares(p.spares, s, dt)) {
        p.supply.showCircle(s, Kind::Spares);
    } else if (p.supply.kind(s) == Kind::Repair) {
        tickRepair(p.repair, p.repairState, s, dt);
        p.supply.showCircle(s, Kind::Repair);
    }
}

bool handsOutAmmo(Kind kind) { return kind == Kind::Ammo; }

}  // namespace spares_supply::core
