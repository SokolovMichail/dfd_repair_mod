// HEMTT Repair rules. Overview - repair.h.
//
// Everything here works through the RepairPort and Modules interfaces, so the same rules run on top of the
// reverse-engineered logic.dll today (logic_dll/repair.cpp) and on top of plain test objects in core_test.

#include "repair.h"

#include <cstdio>

namespace spares_supply::core {

// The same check HEMTT Supply uses before handing out ammo (AmmoRechargerFacet::canRecharge): world diplomacy
// gives the relation of the target's team to the owner's team, and 2 and 4 mean "hostile". No diplomacy or a
// vehicle without a team (nullopt) - not allowed either.
bool mayRepair(std::optional<int> relation) { return relation && *relation != 2 && *relation != 4; }

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

    // The stock is HEMTT Repair's own spare parts, kept in its own modules. No modules or an empty stock - no
    // pulse at all (and nothing is logged: this is not "ran out during the pulse").
    Vehicle* owner = port.owner(s);
    std::unique_ptr<Modules> own = owner ? port.modulesOf(owner) : nullptr;
    if (!own || own->spareParts() <= 0) return;
    // The stock is read once and written back once at the end of the pulse; the armor debt is per station and
    // survives between pulses.
    int spares = own->spareParts();
    Stock stock{spares, state.debt[s]};
    // The zone is the list the game already collected for the station (the same radius as the drawn circle).
    for (Vehicle* target : port.inZone(s)) {
        if (stock.spares <= 0) break;  // the rest of the zone waits for a resupply
        // Skip empty entries, HEMTT Repair itself, enemies and vehicles without a team.
        if (!target || target == owner || !mayRepair(port.relation(owner, target))) continue;
        std::unique_ptr<Modules> modules = port.modulesOf(target);
        if (!modules) continue;  // infantry: nothing to repair
        Repaired r = repairVehicle(*modules, stock);
        // Diagnostics: the first kRepairLogLines pulses that did something, so the log does not grow forever.
        if ((r.health || r.layers) && state.logLines < kRepairLogLines) {
            ++state.logLines;
            char line[256];
            snprintf(line, sizeof line, "repair: target=%p +%d hp, %d armor layers, total hp %d; spent %d spares, %d left",
                     static_cast<void*>(target), r.health, r.layers, modules->totalHealth(), r.spent, stock.spares);
            port.log(line);
        }
    }
    own->setSpareParts(spares);
    // The stock ran out during this pulse - report it (the binding limits how often events get logged).
    if (stock.spares <= 0) port.event("repair: out of spare parts", s);
}

// F4 mode is over: the next time it is switched on, the pulse comes at once and the debt starts from zero.
void forgetRepair(RepairState& state, Station* s) {
    state.timers.erase(s);
    state.debt.erase(s);
}

}  // namespace spares_supply::core
