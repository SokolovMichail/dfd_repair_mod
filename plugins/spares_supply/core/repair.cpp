// HEMTT Repair rules. Overview - repair.h.

#include "repair.h"

#include <cstdio>

namespace spares_supply::core {

bool mayRepair(std::optional<int> relation) { return relation && *relation != 2 && *relation != 4; }

Repaired repairVehicle(Modules& modules, Stock stock) {
    Repaired r;
    if (modules.totalHealth() <= 0) return r;  // destroyed
    for (size_t i = 0; i < modules.count() && stock.spares > 0; ++i) {
        if (!modules.described(i)) continue;
        int hp = modules.health(i);
        int maxHp = modules.maxHealth(i);
        if (hp < maxHp) {
            int add = std::min({std::max(1, static_cast<int>(maxHp * kRepairFraction)), maxHp - hp, stock.spares});
            int gained = modules.addHealth(i, add);
            stock.pay(static_cast<float>(gained), r);
            r.health += gained;
        }
        for (int side = 0; side < modules.sides() && stock.spares > 0; ++side) {
            size_t n = modules.layers(i, side);
            for (size_t k = 0; k < n; ++k) {
                float full = modules.fullThickness(i, side, k);
                float t = modules.thickness(i, side, k);
                if (t >= full || full <= 0) continue;
                float next = std::min(full, t + full * kArmorFraction);
                stock.pay((next - t) * kSparesPerMm, r);
                modules.setThickness(i, side, k, next);
                ++r.layers;
            }
        }
    }
    return r;
}

void tickRepair(RepairPort& port, RepairState& state, Station* s, float dt) {
    float& left = state.timers[s];
    left -= dt;
    if (left > 0) return;
    left = kRepairPeriod;

    Vehicle* owner = port.owner(s);
    std::unique_ptr<Modules> own = owner ? port.modulesOf(owner) : nullptr;  // the stock is HEMTT Repair's own
    if (!own || own->spareParts() <= 0) return;
    int spares = own->spareParts();
    Stock stock{spares, state.debt[s]};
    for (Vehicle* target : port.inZone(s)) {
        if (stock.spares <= 0) break;
        if (!target || target == owner || !mayRepair(port.relation(owner, target))) continue;
        std::unique_ptr<Modules> modules = port.modulesOf(target);
        if (!modules) continue;
        Repaired r = repairVehicle(*modules, stock);
        if ((r.health || r.layers) && state.logLines < kRepairLogLines) {
            ++state.logLines;
            char line[256];
            snprintf(line, sizeof line, "repair: target=%p +%d hp, %d armor layers, total hp %d; spent %d spares, %d left",
                     static_cast<void*>(target), r.health, r.layers, modules->totalHealth(), r.spent, stock.spares);
            port.log(line);
        }
    }
    own->setSpareParts(spares);
    if (stock.spares <= 0) port.event("repair: out of spare parts", s);
}

void forgetRepair(RepairState& state, Station* s) {
    state.timers.erase(s);
    state.debt.erase(s);
}

}  // namespace spares_supply::core
