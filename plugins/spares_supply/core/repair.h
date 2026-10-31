// HEMTT Repair rules: while F4 mode is on, every kRepairPeriod seconds repair the modules and restore the
// armor of friendly vehicles in the zone, paying with the repair truck's own spare parts.
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

// What one repair pulse did to one vehicle.
struct Repaired {
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

// Per-station repair state: seconds to the next pulse and the unpaid armor price; plus the log line counter.
struct RepairState {
    std::unordered_map<Station*, float> timers;
    std::unordered_map<Station*, float> debt;
    int logLines = 0;
};

// May HEMTT Repair repair a vehicle in this relation? Own and allied - yes; relation 2 or 4 (as for
// HEMTT Supply's ammo) or unknown - no.
bool mayRepair(std::optional<int> relation);

// One repair pulse of one vehicle paid from stock:
//  - every damaged module gets kRepairFraction of its maximum (at least 1, no more than is missing and no
//    more than is left in stock);
//  - every worn armor layer gets kArmorFraction of its original thickness (not above the original), at
//    kSparesPerMm spare parts per millimetre.
// A destroyed vehicle (total health 0) is left alone. Stops when the stock is empty.
Repaired repairVehicle(Modules& modules, Stock stock);

// F4 mode of HEMTT Repair, called every frame: once every kRepairPeriod seconds - a repair pulse over the
// vehicles in the station's zone. Skips HEMTT Repair itself, enemies and vehicles without modules. Pays
// with HEMTT Repair's own spare parts; an empty stock - does nothing. The first kRepairLogLines pulses
// with a result go to the log.
void tickRepair(RepairPort& port, RepairState& state, Station* s, float dt);

// F4 mode is over: forget the station's pulse timer and armor debt.
void forgetRepair(RepairState& state, Station* s);

}  // namespace spares_supply::core
