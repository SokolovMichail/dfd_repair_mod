// SparesSupply.dll, HEMTT Repair part. Overview - repair.h.

#include "repair.h"

#include <memory>
#include <optional>
#include <vector>

namespace spares_supply {

namespace {

// Recreate the circle with material path instead of ammoRecharge: for the time of one 0x120610 call the
// ammoRecharge row in the material table is swapped and then put straight back.
// The swapped row and what it held, while a redraw is in progress - for restoreRepairCircle after a fault.
MsvcString* g_swappedEntry = nullptr;
MsvcString g_savedEntry{};

void showCircleWithPath(void* ammo, const char* path) {
    auto table = reinterpret_cast<char* (*)()>(g_a.materialTable)();  // also makes sure the table is initialized
    auto entry = reinterpret_cast<MsvcString*>(table + kMaterialAmmo);
    g_savedEntry = *entry;
    g_swappedEntry = entry;
    size_t n = strlen(path);
    *entry = MsvcString{path, {}, n, n};
    reinterpret_cast<FacetFn>(g_a.showCircle)(ammo);
    *entry = g_savedEntry;
    g_swappedEntry = nullptr;
}

// Relation of target's team to owner's team - the same way as in AmmoRechargerFacet::canRecharge: world
// diplomacy (0x541a00) compares the teams of the target and the owner. nullopt - no diplomacy, or one of them
// has no team. Whether that relation allows repair is up to the core (core::mayRepair).
std::optional<int> relationOf(void* owner, void* target) {
    void* dip = reinterpret_cast<DiplomacyFn>(g_a.diplomacy)(vcall<void*>(owner, 0x78));
    if (!dip) return std::nullopt;
    auto teamOf = [](void* e) { return *reinterpret_cast<void**>(vcall<char*>(e, kEntityInfo) + 0x20); };
    void* ot = teamOf(owner);
    void* tt = teamOf(target);
    if (!ot || !tt) return std::nullopt;
    return vcall<int>(dip, 0xb0, vcall<int>(tt, 0x50), vcall<int>(ot, 0x50));
}

// The vehicle's TransportHelper (who may board it): entity info block +0x28 is its AiFacet, whose vfunc +0x2a0
// hands out the helper. nullptr - no AiFacet or no helper.
void* transportOf(void* entity) {
    void* ai = *reinterpret_cast<void**>(vcall<char*>(entity, kEntityInfo) + kInfoAi);
    return ai ? vcall<void*>(ai, kAiTransport) : nullptr;
}

// Armor layers of one side of a module: current thickness (vector<float>) and description (kArmorLayerSize
// bytes each).
struct ArmorSide {
    float* cur;
    const char* lay;
    size_t n;
};
// Armor layers of side `side` (0..5) of module mod with description desc. n - how many layers are present both
// in the current thickness and in the description (in case the vector lengths ever differ).
ArmorSide armorSide(char* mod, const char* desc, int side) {
    auto cur = reinterpret_cast<float**>(mod + kModuleArmor + side * 0x18);         // begin, end
    auto lay = reinterpret_cast<char* const*>(desc + kDescArmor + side * 0x18);   // begin, end
    size_t n = std::min(static_cast<size_t>(cur[1] - cur[0]), static_cast<size_t>(lay[1] - lay[0]) / kArmorLayerSize);
    return {cur[0], lay[0], n};
}

// A DamageableModulesFacet seen as core::Modules. Module i: [facet+0xd0] + i*0x148; its description and
// armor layers - see the layout constants in repair.h. The object is a thin view: it keeps only the facet and
// vehicle addresses and reads / writes game memory on every call, so it never goes stale.
class EngineModules final : public core::Modules {
public:
    EngineModules(void* facet, void* entity) : m_(static_cast<char*>(facet)), entity_(entity) {}

    // [+0xcc]: the engine keeps it up to date itself (incHealth recalculates it).
    int totalHealth() const override { return *reinterpret_cast<int*>(m_ + kModulesTotalHealth); }
    // The modules are a vector of 0x148-byte records [+0xd0, +0xd8); a broken or empty vector - no modules.
    size_t count() const override {
        char* b = begin();
        char* e = *reinterpret_cast<char**>(m_ + kModulesEnd);
        return b && e > b ? static_cast<size_t>(e - b) / kModuleSize : 0;
    }
    // Module record +0x00: pointer to the module description (maximum health, original armor); may be null.
    bool described(size_t i) const override { return desc(i) != nullptr; }
    int health(size_t i) const override { return *reinterpret_cast<int*>(mod(i) + kModuleHealth); }
    int maxHealth(size_t i) const override { return *reinterpret_cast<int*>(desc(i) + kDescMaxHealth); }
    // Through the engine's own DamageableModulesFacet::incHealth - the same path crew repair takes, so the
    // engine recalculates total health, the module state ("damaged" / "destroyed"), visuals and sync itself.
    int addHealth(size_t i, int amount) override {
        int before = health(i);
        incHealth(i, amount);
        return health(i) - before;
    }
    int sides() const override { return kArmorSides; }
    // Armor: current thickness - vector<float> per side in the module record, original thickness - 16-byte
    // layer records per side in the description. The game has no "restore armor" function: the damage function
    // reads the current thickness directly, so writing it is enough for the armor to work again.
    size_t layers(size_t i, int side) const override { return armorSide(mod(i), desc(i), side).n; }
    float thickness(size_t i, int side, size_t k) const override { return armorSide(mod(i), desc(i), side).cur[k]; }
    float fullThickness(size_t i, int side, size_t k) const override {
        return *reinterpret_cast<const float*>(armorSide(mod(i), desc(i), side).lay + k * kArmorLayerSize);
    }
    void setThickness(size_t i, int side, size_t k, float value) override {
        armorSide(mod(i), desc(i), side).cur[k] = value;
    }
    // [+0x130]: the vehicle's spare parts (the engine's own field; the maximum is at +0x12c).
    int spareParts() const override { return sparesOfModules(m_); }
    void setSpareParts(int value) override { sparesOfModules(m_) = value; }

    // The description keeps the effect flags of states 1..4 (element s-1 for state s, as the game applies them
    // in 0x25910); the first state with "smoke" is where the module catches fire.
    int burnState(size_t i) const override {
        auto flags = reinterpret_cast<const uint64_t* const*>(desc(i) + kDescStateEffects);  // begin, end
        size_t n = flags[0] && flags[1] > flags[0] ? static_cast<size_t>(flags[1] - flags[0]) : 0;
        for (size_t s = 0; s < n && s < 4; ++s)
            if (flags[0][s] & kEffectSmoke) return static_cast<int>(s) + 1;
        return 0;
    }
    // TransportHelper::isBlocked: the ban the game sets on deny_crew_load for damagedUnitBlockTime - as long as
    // the flames of the same state play.
    bool boardingBlocked() const override {
        void* t = transportOf(entity_);
        return t && vcall<bool>(t, kTransportIsBlocked);
    }
    // incHealth with amount 0 adds nothing and costs nothing, but it always starts by taking the flames off the
    // vehicle's modules (0x25790) and marks the vehicle for sync - this is how crew repair puts a fire out. The
    // boarding ban is lifted by the game only when a module leaves its state, so it is lifted here directly.
    void putOutFire(size_t i) override {
        incHealth(i, 0);
        if (void* t = transportOf(entity_)) vcall<void>(t, kTransportUnblock);
    }

private:
    char* begin() const { return *reinterpret_cast<char**>(m_ + kModulesBegin); }
    char* mod(size_t i) const { return begin() + i * kModuleSize; }
    char* desc(size_t i) const { return *reinterpret_cast<char**>(mod(i) + kModuleDesc); }
    // incHealth is crew repair: it takes spare parts from the target itself. HEMTT Repair pays instead, so they
    // are given back.
    void incHealth(size_t i, int amount) {
        int targetSpares = sparesOfModules(m_);
        reinterpret_cast<IncHealthFn>(g_a.incHealth)(m_, i, amount);
        sparesOfModules(m_) = targetSpares;
    }

    char* m_;        // the DamageableModulesFacet
    void* entity_;   // the vehicle it belongs to
};

// What core::tickRepair needs, taken from logic.dll. A station is the AmmoRechargerFacet of HEMTT Repair (it,
// not the RepairerFacet, draws the circle and collects the vehicles in the zone); a vehicle is the engine's
// entity object the facets belong to.
class LogicDllRepairPort final : public core::RepairPort {
public:
    // The facet keeps its owner as a reference block at [facet+0x10].
    core::Vehicle* owner(core::Station* s) override { return vehicle(ownerOf(s)); }
    // The ammo component's original update refreshes this list every frame, with the global supplyRadius -
    // the same radius as the drawn circle. We only read it.
    std::vector<core::Vehicle*> inZone(core::Station* s) override {
        // vehicles the ammo component collected in its zone: vector<RefBlock*> at [ammo+0x30]
        auto list = reinterpret_cast<RefBlock***>(reinterpret_cast<char*>(s) + kAmmoTargets);  // begin, end
        std::vector<core::Vehicle*> out;
        for (RefBlock** p = list[0]; p && p < list[1]; ++p) out.push_back(vehicle(*p ? (*p)->object : nullptr));
        return out;
    }
    std::optional<int> relation(core::Vehicle* owner, core::Vehicle* target) override {
        return relationOf(owner, target);
    }
    // Infantry and other entities without a DamageableModulesFacet - nullptr.
    std::unique_ptr<core::Modules> modulesOf(core::Vehicle* v) override {
        void* f = findFacet(v, g_a.modulesVt);
        return f ? std::make_unique<EngineModules>(f, v) : nullptr;
    }
    void log(const char* line) override {
        if (g_logEvents) g_log("%s", line);
    }
    // Events go through trace(): the first 50 per run, with the facet and owner addresses.
    void event(const char* what, core::Station* s) override { trace(what, s); }
};

// The single repair port of the plugin; the core keeps no state of its own, so one instance serves all stations.
LogicDllRepairPort g_repairPort;

}  // namespace

// The vehicle's stock of spare parts - an int in its DamageableModulesFacet ([+0x130]); a reference, so it can be
// changed.
int& sparesOfModules(void* modules) {
    return *reinterpret_cast<int*>(static_cast<char*>(modules) + kModulesSpares);
}

// Repair state of all HEMTT Repair stations, keyed by their ammo component (core::Station*): seconds to the next
// pulse and the unpaid armor price. Lives for the whole game run; an entry is dropped when F4 mode ends.
core::RepairState g_repair;
// Short names for the parts of g_repair (the tests use them).
std::unordered_map<core::Station*, float>& g_repairTimers = g_repair.timers;
std::unordered_map<core::Station*, float>& g_armorDebt = g_repair.debt;
int& g_repairLogs = g_repair.logLines;

// The ammo component of HEMTT Repair in F4 mode: one frame of core::tickRepair through the logic.dll port.
void tickRepair(void* ammo, float dt) { core::tickRepair(g_repairPort, g_repair, station(ammo), dt); }

core::RepairPort& repairPort() { return g_repairPort; }

// HEMTT Repair's circle: the material table has no row of our own, so the ammoRecharge row is swapped with the
// path to repairRecharge.material for one call of the circle function (showCircleWithPath above).
void redrawRepairCircle(void* ammo) { showCircleWithPath(ammo, kRepairMaterial); }

void restoreRepairCircle() {
    if (!g_swappedEntry) return;
    *g_swappedEntry = g_savedEntry;
    g_swappedEntry = nullptr;
}

// The RepairerFacet is an IRechargerFacet too and is created before the ammo component, so F4 mode on HEMTT
// Repair switches exactly it on and off. The repair flags are set as usual (F4 mode asks isActive), and switching
// on is repeated on the ammo component of the same vehicle: it draws the circle and collects targets for
// tickRepair.
void hookRepairOn(void* r) {
    g_origRepairOn(r);
    guarded("repair F4 on", [&] {
        void* ammo = findFacet(ownerOf(r), g_a.ammoVt);
        // hookAmmoOn runs the game's ammo activation and then core::onSupplyOn - the rules of F4 mode
        if (ammo && !isOn(ammo, kRechargerOffFlag)) hookAmmoOn(ammo);
    });
}
void hookRepairOff(void* r) {
    g_origRepairOff(r);
    guarded("repair F4 off", [&] {
        void* ammo = findFacet(ownerOf(r), g_a.ammoVt);
        if (ammo && isOn(ammo, kRechargerOffFlag)) {
            trace("repair OFF (with supply mode)", ammo);
            hookAmmoOff(ammo);
        }
    });
}
// The repair component's own update is unfinished (it looks for targets and throws the result away) - it is not
// run at all.
void hookRepairUpdate(void*, float) {}

}  // namespace spares_supply
