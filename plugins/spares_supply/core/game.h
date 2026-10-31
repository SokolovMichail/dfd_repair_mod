// Game-agnostic core of the SparesSupply plugin: the rules of HEMTT Spares and HEMTT Repair.
//
// The core never touches game memory. It sees the game only through "ports" - small interfaces declared
// next to the rules that need them (spares.h, repair.h, supply.h) and implemented by a binding: today
// logic_dll/ (reverse-engineered logic.dll), later possibly an official modding API. Vehicles and
// supply stations are opaque handles: the core passes them back to the port, but never looks inside.
#pragma once

namespace spares_supply::core {

// A vehicle (or an infantryman) in the game world. Opaque: only the binding knows what it points to.
class Vehicle;

// A vehicle's supply component that runs F4 mode (in logic.dll - its AmmoRechargerFacet or
// RefuellerFacet). Per-station state such as repair timers is keyed by it. Opaque.
class Station;

// What a supply vehicle actually does: Ammo - an ordinary HEMTT Supply (and anything that is not ours),
// Spares - HEMTT Spares, Repair - HEMTT Repair.
enum class Kind { Ammo, Spares, Repair };

}  // namespace spares_supply::core
