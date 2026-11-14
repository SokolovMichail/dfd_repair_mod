// HEMTT Repair rules. Overview - repair.h.
//
// Everything here works through the RepairPort and Modules interfaces, so the same rules run on top of the
// reverse-engineered logic.dll today (logic_dll/repair.cpp) and on top of plain test objects in core_test.

#include "repair.h"

#include <cstdio>

namespace spares_supply::core {

// An abandoned vehicle is on the game's own team "abandoned" - not ours, so diplomacy would turn it away; but its
// crew has only left it (bailing out of a fire, for one) and anyone may take it back, so it is served. Everyone
// else gets the same check HEMTT Supply uses before handing out ammo (AmmoRechargerFacet::canRecharge): world
// diplomacy gives the relation of the target's team to the owner's team, and 2 and 4 mean "hostile". No
// diplomacy or a vehicle without a team (nullopt) - not allowed either.
bool mayRepair(RepairPort& port, Vehicle* owner, Vehicle* target) {
    if (!target || target == owner) return false;
    if (port.abandoned(target)) return true;
    std::optional<int> relation = port.relation(owner, target);
    return relation && *relation != 2 && *relation != 4;
}

// The game's formula (DamageableModulesFacet, the same in incHealth, decHealth and 0x24d50): full health is
// state 0, otherwise the float ratio health / maximum picks the state. A ratio of exactly 0 is "destroyed".
int moduleState(int hp, int maxHp) {
    if (hp >= maxHp) return 0;
    float ratio = static_cast<float>(hp) / static_cast<float>(maxHp);
    if (ratio == 0.0f) return 4;
    if (ratio < kHeavyDamage) return 3;
    return ratio < kLightDamage ? 2 : 1;
}

// A module burns from its burnState on: the flames come with entering that state, and the game takes them off
// only when the module is repaired out of it (or when they burn out after damagedUnitBlockTime). So a module in
// burnState or a worse one, while boarding is still blocked, is on fire.
std::vector<size_t> burningModules(const Modules& modules) {
    std::vector<size_t> out;
    if (modules.totalHealth() <= 0 || !modules.boardingBlocked()) return out;
    for (size_t i = 0; i < modules.count(); ++i) {
        if (!modules.described(i)) continue;  // no description - no maximum health, no effects
        int burn = modules.burnState(i);
        if (burn > 0 && moduleState(modules.health(i), modules.maxHealth(i)) >= burn) out.push_back(i);
    }
    return out;
}

// The list is taken first: putting out one module also lifts the vehicle's boarding ban, and the rest must still
// be put out.
int extinguish(Modules& modules) {
    std::vector<size_t> burning = burningModules(modules);
    for (size_t i : burning) modules.putOutFire(i);
    return static_cast<int>(burning.size());
}

// One repair pulse of one vehicle. Modules are repaired in order; inside a module - health first, then the
// armor of all its sides. The pulse stops as soon as the stock is empty (see the side-by-side check below).
Repaired repairVehicle(Modules& modules, Stock stock) {
    Repaired r;
    if (modules.totalHealth() <= 0) return r;  // destroyed: a wreck is not repaired
    for (size_t i = 0; i < modules.count() && stock.spares > 0; ++i) {
        if (!modules.described(i)) continue;  // no description - no maximum health, no original armor
        int hp = modules.health(i);
        int maxHp = modules.maxHealth(i);
        if (hp < maxHp) {
            // kRepairFraction of the maximum, but at least 1 (small modules would otherwise get 0), no more than
            // is missing and no more than the stock can pay for.
            int add = std::min({std::max(1, static_cast<int>(maxHp * kRepairFraction)), maxHp - hp, stock.spares});
            // The game decides how much really gets added; the price is what was really added, 1 per health.
            int gained = modules.addHealth(i, add);
            stock.pay(static_cast<float>(gained), r);
            r.health += gained;
        }
        // QUIRK: the stock is checked before each side, not before each layer: a side that was started is repaired
        // completely, and what the stock cannot pay goes into debt.
        for (int side = 0; side < modules.sides() && stock.spares > 0; ++side) {
            size_t n = modules.layers(i, side);
            for (size_t k = 0; k < n; ++k) {
                float full = modules.fullThickness(i, side, k);
                float t = modules.thickness(i, side, k);
                if (t >= full || full <= 0) continue;  // intact, or a layer with no original thickness
                // kArmorFraction of the original thickness, never above the original; the price is per millimetre
                // actually restored, so it is usually fractional and accumulates in the debt.
                float next = std::min(full, t + full * kArmorFraction);
                stock.pay((next - t) * kSparesPerMm, r);
                modules.setThickness(i, side, k, next);
                ++r.layers;
            }
        }
    }
    return r;
}

// Called every frame of F4 mode for a HEMTT Repair station (from onSupplyTick).
void tickRepair(RepairPort& port, RepairState& state, Station* s, float dt) {
    // Pulse timer: the first frame after F4 is switched on gives a pulse at once (the timer starts at 0), then
    // one pulse every kRepairPeriod seconds.
    float& left = state.timers[s];
    left -= dt;
    if (left > 0) return;
    left = kRepairPeriod;

    // The stock is HEMTT Repair's own spare parts, kept in its own modules. No modules - no pulse at all. An
    // empty stock still lets the pulse put out fires (that is free), only nothing gets repaired.
    Vehicle* owner = port.owner(s);
    std::unique_ptr<Modules> own = owner ? port.modulesOf(owner) : nullptr;
    if (!own) return;
    // The stock is read once and written back once at the end of the pulse; the armor debt is per station and
    // survives between pulses.
    int spares = own->spareParts();
    const bool hadSpares = spares > 0;
    Stock stock{spares, state.debt[s]};
    // The zone is the list the game already collected for the station (the same radius as the drawn circle).
    std::vector<Vehicle*> zone = port.inZone(s);
    // Diagnostics: who the game put into the zone is decided by the game, not by these rules (its collector has
    // filters of its own), so the size of the list is logged whenever it changes.
    size_t vehicles = 0;
    for (Vehicle* v : zone) vehicles += v != nullptr;
    auto known = state.zoneSizes.find(s);
    if ((known == state.zoneSizes.end() || known->second != vehicles) && state.zoneLogLines < kRepairLogLines) {
        ++state.zoneLogLines;
        char line[64];
        snprintf(line, sizeof line, "repair: %zu vehicles in the zone", vehicles);
        port.log(line);
    }
    state.zoneSizes[s] = vehicles;
    for (Vehicle* target : zone) {
        if (!mayRepair(port, owner, target)) continue;  // whom HEMTT Repair serves - all in mayRepair
        std::unique_ptr<Modules> modules = port.modulesOf(target);
        if (!modules) continue;  // infantry: nothing to repair
        // Put out first, then repair: a vehicle on fire gets only that in this pulse. Without spare parts the
        // rest of the zone waits for a resupply (but its fires are still put out).
        Repaired r;
        r.fires = extinguish(*modules);
        if (!r.fires && stock.spares > 0) r = repairVehicle(*modules, stock);
        // Diagnostics: the first kRepairLogLines pulses that did something, so the log does not grow forever.
        if ((r.fires || r.health || r.layers) && state.logLines < kRepairLogLines) {
            ++state.logLines;
            char line[256];
            if (r.fires)
                snprintf(line, sizeof line, "repair: fire out target=%p, %d burning modules", static_cast<void*>(target),
                         r.fires);
            else
                snprintf(line, sizeof line,
                         "repair: target=%p +%d hp, %d armor layers, total hp %d; spent %d spares, %d left",
                         static_cast<void*>(target), r.health, r.layers, modules->totalHealth(), r.spent, stock.spares);
            port.log(line);
        }
    }
    own->setSpareParts(spares);
    // The stock ran out during this pulse - report it (the binding limits how often events get logged). A stock
    // that was already empty is not news, so it is not reported.
    if (hadSpares && stock.spares <= 0) port.event("repair: out of spare parts", s);
}

// F4 mode is over: the next time it is switched on, the pulse comes at once, the debt starts from zero and the
// zone size is logged again.
void forgetRepair(RepairState& state, Station* s) {
    state.timers.erase(s);
    state.debt.erase(s);
    state.zoneSizes.erase(s);
}

}  // namespace spares_supply::core
