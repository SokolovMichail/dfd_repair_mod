// HEMTT Spares rules: the vehicle's spare parts component follows F4 mode - it is switched on and off with
// the supply station and ticked with it every frame. Handing out spare parts itself is done by the game.
#pragma once

#include "game.h"

namespace spares_supply::core {

// A vehicle's spare parts component (in logic.dll - SparesRechargerFacet). Opaque.
class SparesDepot;

// What the spare parts rules need from the game.
class SparesPort {
public:
    virtual ~SparesPort() = default;
    // The spare parts component of the station's vehicle; nullptr - it has none.
    virtual SparesDepot* depotOf(Station* s) = 0;
    // Is the station in F4 mode (its supply component switched on)?
    virtual bool stationOn(Station* s) = 0;
    virtual bool isOn(SparesDepot* d) = 0;
    // Switch the component on / off the way the game does it, without going through our own rules again.
    virtual void turnOn(SparesDepot* d) = 0;
    virtual void turnOff(SparesDepot* d) = 0;
    // One update of the component: find vehicles in the zone and resupply them.
    virtual void tick(SparesDepot* d, float dt) = 0;
    // Is any supply station of the component's vehicle in F4 mode right now?
    virtual bool supplyModeActive(SparesDepot* d) = 0;
    // Report a notable event of the component (the binding decides how to log it).
    virtual void event(const char* what, SparesDepot* d) = 0;
};

// F4 mode switched the station on / off: switch the same vehicle's spare parts on / off too. Does nothing if
// there are no spare parts or they are already in that state.
void syncSpares(SparesPort& port, Station* s, bool on);

// Every frame of F4 mode: if the station is on, tick the vehicle's spare parts (if they are on) with the same
// dt. Returns the spare parts component (even if it was off), nullptr - none or the station is off.
SparesDepot* tickSpares(SparesPort& port, Station* s, float dt);

// May the spare parts component be switched off now? Not while F4 mode is running on its vehicle: the click
// order "resupply spare parts" switches them off when it ends, and that would cut the zone resupply short.
bool mayTurnSparesOff(SparesPort& port, SparesDepot* d);

}  // namespace spares_supply::core
