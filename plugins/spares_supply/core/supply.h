// Common F4 mode ("SUPPLY MODE") rules for the mod's supply vehicles: what happens when a station is switched
// on, every frame while it is on and when it is switched off, which zone circle it shows and whether it may
// hand out ammo. The vehicle-specific work is delegated to the spares and repair rules.
#pragma once

#include "game.h"
#include "repair.h"
#include "spares.h"

namespace spares_supply::core {

// What the common F4 rules need from the game.
class SupplyPort {
public:
    virtual ~SupplyPort() = default;
    // What the station's vehicle is: HEMTT Spares, HEMTT Repair or anything else (Ammo).
    virtual Kind kind(Station* s) = 0;
    // Is the station in F4 mode (its supply component switched on)?
    virtual bool isOn(Station* s) = 0;
    // Show the station's zone circle with the material of this kind of vehicle (Ammo - leave it as it is).
    virtual void showCircle(Station* s, Kind kind) = 0;
    // Report a notable event of the station (the binding decides how to log it).
    virtual void event(const char* what, Station* s) = 0;
};

// Everything the F4 rules work with: the three ports and the repair state.
struct Ports {
    SupplyPort& supply;
    SparesPort& spares;
    RepairPort& repair;
    RepairState& repairState;
};

// The game has just switched the station on (F4 mode on): spare parts follow, the circle gets our material.
void onSupplyOn(Ports& p, Station* s);

// The game has just switched the station off (F4 mode off): forget its repair state, spare parts follow.
void onSupplyOff(Ports& p, Station* s);

// Every frame after the game's own update of the station: while F4 mode is on, HEMTT Spares ticks its spare
// parts and HEMTT Repair repairs; the circle is repainted if it was created without us (loading a save).
void onSupplyTick(Ports& p, Station* s, float dt);

// May a vehicle of this kind hand out ammo? The mod's vehicles never do - neither in the zone nor by order;
// they carry the ammo component only for the F4 button.
bool handsOutAmmo(Kind kind);

}  // namespace spares_supply::core
