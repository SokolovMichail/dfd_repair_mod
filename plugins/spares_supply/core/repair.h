// HEMTT Repair rules: while F4 mode is on, every kRepairPeriod seconds put out the fires of friendly vehicles in
// the zone, or, if a vehicle is not on fire, repair its modules and restore its armor, paying with the repair
// truck's own spare parts.
#pragma once

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include "game.h"

namespace spares_supply::core {

// Repair: once every kRepairPeriod seconds, to every vehicle in the zone.
constexpr float kRepairPeriod = 0.5f;
constexpr float kRepairFraction = 0.02f;  // a module gets 2% of its maximum (4%/s: from zero to full in 25 s)
constexpr float kArmorFraction = 0.02f;   // an armor layer gets 2% of its original thickness
// The price is paid with HEMTT Repair's own spare parts (the target pays nothing). Module: 1 spare part per
// 1 health (as crew repair). Armor: kSparesPerMm spare parts per unit of thickness (a millimetre, judging by
// the data).
constexpr float kSparesPerMm = 1.0f;
constexpr int kRepairLogLines = 40;  // repair pulses written to the log per run

// Module damage states as the game counts them from health (DamageableModulesFacet::incHealth / decHealth):
// 0 - intact (full health), 1 - from kLightDamage of the maximum, 2 - from kHeavyDamage, 3 - below that,
// 4 - destroyed (0 health). The game divides as floats and compares with these float constants.
constexpr float kLightDamage = 0.66f;
constexpr float kHeavyDamage = 0.33f;
int moduleState(int hp, int maxHp);

// What one repair pulse did to one vehicle.
struct Repaired {
    int fires = 0;   // burning modules put out (then nothing else is done to the vehicle in this pulse)
    int health = 0;  // health added to modules
    int layers = 0;  // armor layers that grew
    int spent = 0;   // spare parts spent
};

// HEMTT Repair's stock of spare parts. A fractional armor price accumulates in debt and is taken in whole
// spare parts.
struct Stock {
    int& spares;
    float& debt;
    // Pay cost spare parts: the price accumulates in debt, the whole part is taken from stock (but no more
    // than there is). What was spent is added to r.spent.
    void pay(float cost, Repaired& r) {
        debt += cost;
        int whole = std::min(spares, static_cast<int>(debt));
        spares -= whole;
        debt -= whole;
        r.spent += whole;
    }
};

// A vehicle's damageable modules (engine, turret, ...) as the repair rules see them: health and armor
// layers, each with a current and an original thickness, on every side of every module.
class Modules {
public:
    virtual ~Modules() = default;
    virtual int totalHealth() const = 0;  // 0 - the vehicle is destroyed
    virtual size_t count() const = 0;
    // A module the game cannot describe (no description) is skipped.
    virtual bool described(size_t i) const = 0;
    virtual int health(size_t i) const = 0;
    virtual int maxHealth(size_t i) const = 0;
    // Add health to module i the way the game repairs it; returns how much was really added. The vehicle
    // itself pays nothing for it.
    virtual int addHealth(size_t i, int amount) = 0;
    virtual int sides() const = 0;
    virtual size_t layers(size_t i, int side) const = 0;
    virtual float thickness(size_t i, int side, size_t k) const = 0;
    virtual float fullThickness(size_t i, int side, size_t k) const = 0;
    virtual void setThickness(size_t i, int side, size_t k, float value) = 0;
    // The vehicle's own stock of spare parts.
    virtual int spareParts() const = 0;
    virtual void setSpareParts(int value) = 0;

    // Fire. A module catches fire when it reaches a damage state with the game's "smoke" effect: flames, and
    // (for most modules) the crew bails out and may not board for a while. The flames and the boarding ban last
    // the same time (the game's damagedUnitBlockTime, 60 s), then the fire goes out by itself.
    // The lowest damage state (1..4, see moduleState) in which module i burns; 0 - it never burns.
    virtual int burnState(size_t i) const = 0;
    // The game does not let the crew board the vehicle right now.
    virtual bool boardingBlocked() const = 0;
    // Put out the fire of module i the game's way: flames off, the crew may board again. No health is added
    // and nobody pays for it.
    virtual void putOutFire(size_t i) = 0;
};

// What the repair rules need from the game.
class RepairPort {
public:
    virtual ~RepairPort() = default;
    // The vehicle the station belongs to (nullptr if none).
    virtual Vehicle* owner(Station* s) = 0;
    // Vehicles in the station's zone, as the game collected them; entries may be nullptr.
    virtual std::vector<Vehicle*> inZone(Station* s) = 0;
    // Relation of target's team to owner's team in world diplomacy; nullopt - no diplomacy or no team.
    virtual std::optional<int> relation(Vehicle* owner, Vehicle* target) = 0;
    // The vehicle's damageable modules; nullptr - it has none (infantry).
    virtual std::unique_ptr<Modules> modulesOf(Vehicle* v) = 0;
    // Write a ready line to the log.
    virtual void log(const char* line) = 0;
    // Report a notable event of the station (the binding decides how to log it).
    virtual void event(const char* what, Station* s) = 0;
};

// Per-station repair state: seconds to the next pulse, the unpaid armor price and how many vehicles the zone held
// at the last pulse; plus the log line counters.
struct RepairState {
    std::unordered_map<Station*, float> timers;
    std::unordered_map<Station*, float> debt;
    std::unordered_map<Station*, size_t> zoneSizes;
    int logLines = 0;
    int zoneLogLines = 0;
};

// May HEMTT Repair repair a vehicle in this relation? Own and allied - yes; relation 2 or 4 (as for
// HEMTT Supply's ammo) or unknown - no.
bool mayRepair(std::optional<int> relation);

// The modules that are on fire now: in their burning state (burnState) while the game still blocks boarding -
// the ban runs exactly as long as the flames, so with the ban over the fire has burned out by itself. Modules
// that burn without a boarding ban (no crew to bail out) are never "on fire" here: their flames are only a
// visual, and the game removes them with the first repair anyway. A destroyed vehicle has no fire to put out.
std::vector<size_t> burningModules(const Modules& modules);

// Put out every module that is on fire (burningModules); returns how many. Free: no spare parts, no health.
int extinguish(Modules& modules);

// One repair pulse of one vehicle paid from stock:
//  - every damaged module gets kRepairFraction of its maximum (at least 1, no more than is missing and no
//    more than is left in stock);
//  - every worn armor layer gets kArmorFraction of its original thickness (not above the original), at
//    kSparesPerMm spare parts per millimetre.
// A destroyed vehicle (total health 0) is left alone. Stops when the stock is empty.
Repaired repairVehicle(Modules& modules, Stock stock);

// F4 mode of HEMTT Repair, called every frame: once every kRepairPeriod seconds - a repair pulse over the
// vehicles in the station's zone. Skips HEMTT Repair itself, enemies and vehicles without modules. A vehicle
// on fire is put out first and gets nothing else in that pulse (repair goes on from the next one); putting out
// is free, so it works with an empty stock too. Repair is paid with HEMTT Repair's own spare parts; with an
// empty stock nothing is repaired. The first kRepairLogLines pulses with a result go to the log, and so does the
// number of vehicles in the zone whenever it changes (up to kRepairLogLines such lines, counted separately).
void tickRepair(RepairPort& port, RepairState& state, Station* s, float dt);

// F4 mode is over: forget the station's pulse timer, armor debt and zone size.
void forgetRepair(RepairState& state, Station* s);

}  // namespace spares_supply::core
