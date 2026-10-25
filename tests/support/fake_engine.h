// A fake "game engine" for the plugin behaviour tests (plugin_test.cpp).
//
// In the game the plugin works with engine objects it does not own: vehicles, their components
// (ammo, spare parts, repair, modules), teams, diplomacy. It knows very little about them - a few fields
// at known offsets and a few methods by their number in the method table (vtable). Here those objects
// are built by hand, exactly as far as the plugin uses them, at the same offsets that are written in the
// plugin and in docs/engine_notes.md. Every fake records what was done to it, and the test then checks
// that record.
//
// Include AFTER spares_supply.cpp: it uses the plugin's types (RefBlock, MsvcString) and globals
// (g_a - the addresses found, g_orig* - the original game functions).
#pragma once

// The fakes below use types from the plugin's anonymous namespace (RefBlock). That is fine for tests,
// so the compiler warning about it is turned off.
#pragma GCC diagnostic ignored "-Wsubobject-linkage"

#include <array>
#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace fake {

// ---------------------------------------------------------------- call journal

// One call of an "original game function": what was called (e.g. "ammo.on"), on which object, with what dt.
struct Call {
    std::string what;
    void* obj;
    float dt;
};
inline std::vector<Call> g_calls;

inline void record(const char* what, void* obj, float dt = 0) { g_calls.push_back({what, obj, dt}); }
// How many times `what` was called (on object obj; obj = nullptr - on any object).
inline int calls(const std::string& what, void* obj = nullptr) {
    int n = 0;
    for (auto& c : g_calls)
        if (c.what == what && (!obj || c.obj == obj)) ++n;
    return n;
}
// Names of all calls in order - to check the sequence.
inline std::vector<std::string> callNames() {
    std::vector<std::string> out;
    for (auto& c : g_calls) out.push_back(c.what);
    return out;
}

// A function's address as a plain pointer (that is how a vtable stores it).
template <class F>
void* fn(F f) {
    return reinterpret_cast<void*>(f);
}

// ---------------------------------------------------------------- reference-count block

// The engine holds objects through a "reference block": a strong reference count and a pointer to the
// object. When the count drops to zero, the engine calls the block's "free" method (slot 2). When the
// plugin takes a reference it must give it back - otherwise there is a leak or a premature delete.
// g_freed counts such frees: in the tests the blocks live forever, so a free would mean a counting bug.
inline int g_freed = 0;
inline void rbFree(void*) { ++g_freed; }
inline void* g_rbVtbl[3] = {nullptr, nullptr, fn(&rbFree)};

inline RefBlock makeBlock(void* object) { return RefBlock{g_rbVtbl, 1, 0, object}; }

// ---------------------------------------------------------------- component (facet)

// A vehicle component is a chunk of memory that starts with the address of its vtable (that is how the
// plugin tells the class), followed by fields at known offsets. The fields the plugin needs:
//   +0x10  reference block of the owner vehicle
//   ammo/refueller: +0x30..+0x38 list of vehicles in the zone, +0x50 the drawn circle, +0x59 "off"
//   spare parts: +0x28..+0x30 list of vehicles in the zone, +0x51 "off";  repair: +0x49 "off"
//   modules (DamageableModulesFacet): +0xcc total health, +0xd0..+0xd8 module array, +0x130 spare parts
struct Facet {
    alignas(16) uint8_t mem[0x200]{};
    template <class T>
    T& at(size_t off) {
        return *reinterpret_cast<T*>(mem + off);
    }
    void* p() { return mem; }
};

// Offsets of the "off" flags (1 - off, 0 - on), as in the original game functions.
constexpr size_t kAmmoOff = 0x59, kSparesOff = 0x51, kRepairOff = 0x49;

// ---------------------------------------------------------------- teams and diplomacy

// A team (player): the plugin only needs its number (method in slot 10).
struct Team {
    void** vtbl;
    int id;
};
inline int teamId(void* t) { return static_cast<Team*>(t)->id; }
inline void* g_teamVtbl[11] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                               nullptr, nullptr, nullptr, nullptr, fn(&teamId)};

// World diplomacy: the relation between two teams (method in slot 22). The plugin treats relations 2 and
// 4 as "must not repair" (same as HEMTT Supply does for ammo). The test sets relations; default is 0.
struct Diplomacy {
    void** vtbl;
};
inline std::map<std::pair<int, int>, int> g_relations;  // (target team, owner team) -> relation
inline std::vector<std::pair<int, int>> g_relationAsks;  // which pairs the plugin asked about
inline int relation(void*, int target, int owner) {
    g_relationAsks.push_back({target, owner});
    auto it = g_relations.find({target, owner});
    return it == g_relations.end() ? 0 : it->second;
}
inline void* g_dipVtbl[23] = {};
inline Diplomacy g_dip{g_dipVtbl};
inline int g_world = 0;               // the "world": only its address matters - a vehicle returns it, diplomacy receives it
inline bool g_hasDiplomacy = true;    // false - there is no diplomacy (the game function returned nullptr)
inline void* g_diplomacyWorld = nullptr;
// Fake of the game function "world diplomacy" (0x541a00 in the GOG build).
inline void* diplomacyOf(void* world) {
    g_diplomacyWorld = world;
    return g_hasDiplomacy ? &g_dip : nullptr;
}

// ---------------------------------------------------------------- modules and armor

// One vehicle module (engine, turret, ...): current and maximum health, armor on 6 sides - a list of
// layers, each with a current and an original (full) thickness.
struct ModuleSpec {
    int hp = 100, maxHp = 100;
    std::array<std::vector<float>, 6> armor{};      // current layer thickness
    std::array<std::vector<float>, 6> armorFull{};  // original thickness (from the module description)
    bool noDesc = false;                            // module without a description (the plugin skips it)
};

// Module memory laid out the way the plugin reads it:
//   module - 0x148 bytes: +0x00 description, +0x0c health, +0x28 + side*0x18 - current thickness
//            (vector<float>: begin, end, capacity);
//   description: +0x28 max health, +0x90 + side*0x18 - layers (16 bytes each, first 4 - original thickness).
struct ModuleMemory {
    std::vector<uint8_t> modules;
    std::deque<std::vector<uint8_t>> descs;
    std::deque<std::vector<float>> cur;
    std::deque<std::vector<std::array<float, 4>>> layers;
};

// ---------------------------------------------------------------- vehicle

struct Iter;
inline void* vehicleFacets(void* self, void** out);
inline void* vehicleWorld(void*);
inline char* vehicleInfo(void* self);
inline void* g_vehicleVtbl[23] = {};

// A vehicle (or an infantryman): list of components, team, world. Methods the plugin calls:
//   slot 11 (+0x58) - iterator over components; slot 15 (+0x78) - world; slot 22 (+0xb0) - info block
//   with the team at +0x20.
// The object cannot be copied or moved: components keep a pointer to its reference block.
struct Vehicle {
    void** vtbl = g_vehicleVtbl;
    alignas(8) uint8_t info[0x30]{};
    RefBlock self = makeBlock(this);
    Team team;
    bool nullIterator = false;  // true - no iterator is handed out (the engine returned nothing)
    std::deque<Facet> facets;
    std::deque<RefBlock> blocks;
    std::deque<ModuleMemory> moduleMem;
    std::vector<RefBlock*> targets;  // "vehicles in the zone" for the ammo component

    explicit Vehicle(int teamId = 1) : team{g_teamVtbl, teamId} { setTeam(&team); }
    Vehicle(const Vehicle&) = delete;
    Vehicle& operator=(const Vehicle&) = delete;

    void setTeam(Team* t) { memcpy(info + 0x20, &t, sizeof t); }

    // Add a component of class vt (vtable address). As in the game, it is created switched off - only the
    // "off" flag of this class is set (another class may keep a different field at the same offset: e.g.
    // ammo has the circle pointer at +0x50..+0x57).
    Facet& add(uintptr_t vt);

    // A modules component (DamageableModulesFacet) with modules `specs` and `spares` spare parts in stock.
    Facet& addModules(const std::vector<ModuleSpec>& specs, int spares);

    // Set the "vehicles in the zone" list of an ammo component ([ammo+0x30], [ammo+0x38]).
    void setTargets(Facet& ammo, std::vector<RefBlock*> list) {
        targets = std::move(list);
        ammo.at<RefBlock**>(0x30) = targets.data();
        ammo.at<RefBlock**>(0x38) = targets.data() + targets.size();
    }
};

// Iterator over a vehicle's components. Methods: slot 0 - delete (with flag 1), slot 3 - "at end?",
// slot 4 - "next", slot 6 - "current": hands out a reference block with its count incremented (the plugin
// decrements it later). g_iterDeleted / g_iterDeleteFlags - how many iterators were deleted, with what flags.
inline int g_iterCreated = 0, g_iterDeleted = 0;
inline std::vector<int> g_iterDeleteFlags;
struct Iter {
    void** vtbl;
    Vehicle* v;
    size_t i;
};
inline void iterDelete(void* it, int flag) {
    ++g_iterDeleted;
    g_iterDeleteFlags.push_back(flag);
    delete static_cast<Iter*>(it);
}
inline bool iterAtEnd(void* it) {
    auto i = static_cast<Iter*>(it);
    return i->i >= i->v->blocks.size();
}
inline void iterNext(void* it) { ++static_cast<Iter*>(it)->i; }
inline RefBlock** iterCurrent(void* it, RefBlock** tmp) {
    auto i = static_cast<Iter*>(it);
    RefBlock* rb = &i->v->blocks[i->i];
    ++rb->strong;
    *tmp = rb;
    return tmp;
}
inline void* g_iterVtbl[7] = {fn(&iterDelete), nullptr, nullptr, fn(&iterAtEnd),
                              fn(&iterNext),   nullptr, fn(&iterCurrent)};

inline void* vehicleFacets(void* self, void** out) {
    auto v = static_cast<Vehicle*>(self);
    if (v->nullIterator) {
        *out = nullptr;
    } else {
        *out = new Iter{g_iterVtbl, v, 0};
        ++g_iterCreated;
    }
    return out;
}
inline void* vehicleWorld(void*) { return &g_world; }
inline char* vehicleInfo(void* self) { return reinterpret_cast<char*>(static_cast<Vehicle*>(self)->info); }

inline Facet& Vehicle::add(uintptr_t vt) {
    Facet& f = facets.emplace_back();
    f.at<uintptr_t>(0) = vt;
    f.at<RefBlock*>(0x10) = &self;
    if (vt == g_a.ammoVt || vt == g_a.fuelVt) f.at<uint8_t>(kAmmoOff) = 1;
    if (vt == g_a.sparesVt) f.at<uint8_t>(kSparesOff) = 1;
    if (vt == g_a.repairVt) f.at<uint8_t>(kRepairOff) = 1;
    blocks.push_back(makeBlock(f.p()));
    return f;
}

inline Facet& Vehicle::addModules(const std::vector<ModuleSpec>& specs, int spares) {
    Facet& f = add(g_a.modulesVt);
    ModuleMemory& m = moduleMem.emplace_back();
    m.modules.assign(specs.size() * 0x148, 0);
    int total = 0;
    for (size_t i = 0; i < specs.size(); ++i) {
        const ModuleSpec& s = specs[i];
        uint8_t* mod = m.modules.data() + i * 0x148;
        std::vector<uint8_t>& desc = m.descs.emplace_back(0x120, 0);
        char* descPtr = s.noDesc ? nullptr : reinterpret_cast<char*>(desc.data());
        memcpy(mod + 0x00, &descPtr, 8);
        memcpy(mod + 0x0c, &s.hp, 4);
        memcpy(desc.data() + 0x28, &s.maxHp, 4);
        total += s.hp;
        for (int side = 0; side < 6; ++side) {
            std::vector<float>& cur = m.cur.emplace_back(s.armor[side]);
            float* cb[3] = {cur.data(), cur.data() + cur.size(), cur.data() + cur.size()};
            memcpy(mod + 0x28 + side * 0x18, cb, sizeof cb);
            auto& lay = m.layers.emplace_back();
            for (float full : s.armorFull[side]) lay.push_back({full, 0, 0, 0});
            auto lb = reinterpret_cast<char*>(lay.data());
            char* lp[3] = {lb, lb + lay.size() * 16, lb + lay.size() * 16};
            memcpy(desc.data() + 0x90 + side * 0x18, lp, sizeof lp);
        }
    }
    f.at<int>(0xcc) = total;
    f.at<uint8_t*>(0xd0) = m.modules.data();
    f.at<uint8_t*>(0xd8) = m.modules.data() + m.modules.size();
    f.at<int>(0x130) = spares;
    return f;
}

// Reading modules back - for the checks.
inline int& moduleHp(Facet& modules, size_t i) {
    return *reinterpret_cast<int*>(modules.at<uint8_t*>(0xd0) + i * 0x148 + 0x0c);
}
inline float armorAt(Facet& modules, size_t i, int side, size_t k) {
    auto cur = reinterpret_cast<float**>(modules.at<uint8_t*>(0xd0) + i * 0x148 + 0x28 + side * 0x18);
    return cur[0][k];
}
inline int& sparesIn(Facet& modules) { return modules.at<int>(0x130); }
inline int& totalHp(Facet& modules) { return modules.at<int>(0xcc); }

// ---------------------------------------------------------------- game functions the plugin calls

// DamageableModulesFacet::incHealth(modules, module index, amount) - crew repair of a module. The fake
// behaves like the engine: adds health (not above the maximum), recalculates total health and takes spare
// parts from the vehicle itself (the plugin must give them back - HEMTT Repair pays for the repair).
struct IncHealthCall {
    void* modules;
    size_t index;
    int amount;
};
inline std::vector<IncHealthCall> g_incHealth;
inline void incHealth(void* modules, size_t index, int amount) {
    g_incHealth.push_back({modules, index, amount});
    auto m = static_cast<char*>(modules);
    char* mod = *reinterpret_cast<char**>(m + 0xd0) + index * 0x148;
    int& hp = *reinterpret_cast<int*>(mod + 0x0c);
    int maxHp = *reinterpret_cast<int*>(*reinterpret_cast<char**>(mod) + 0x28);
    int gain = std::min(amount, maxHp - hp);
    hp += gain;
    *reinterpret_cast<int*>(m + 0xcc) += gain;
    *reinterpret_cast<int*>(m + 0x130) -= gain;
}

// The game's material table: at offset 0x120 - a string (MSVC std::string) with the ammo circle material path.
inline const char* const kAmmoMaterial = "materials/rounds/ammoRecharge.material";
alignas(16) inline char g_materialTable[0x200];
inline int g_materialTableCalls = 0;
inline char* materialTable() {
    ++g_materialTableCalls;
    return g_materialTable;
}
inline MsvcString& ammoMaterialEntry() { return *reinterpret_cast<MsvcString*>(g_materialTable + 0x120); }

// The "lea r8,[rax+0x120]" instruction from the circle function: offset 0x120 - the ammoRecharge string in
// the table, 0xE0 - sparesRecharge. The plugin writes 0xE0 here while it redraws the HEMTT Spares circle.
alignas(8) inline uint8_t g_circleLea[8];
inline uint32_t leaOffset() {
    uint32_t v;
    memcpy(&v, g_circleLea + 3, 4);
    return v;
}

// A drawn circle is just a unique object; [ammo+0x50] points to it.
inline std::deque<int> g_circleObjects;
inline void* newCircle() { return &g_circleObjects.emplace_back(0); }
inline void*& circleField(Facet& ammo) { return ammo.at<void*>(0x50); }

// The game function "(re)create the zone circle" of the ammo component (0x120610 in the GOG build). The fake
// remembers which material the circle would be drawn with: the offset in the lea and the table string.
struct CircleDraw {
    void* ammo;
    uint32_t lea;
    std::string material;
};
inline std::vector<CircleDraw> g_draws;
inline void showCircle(void* ammo) {
    const MsvcString& e = ammoMaterialEntry();
    g_draws.push_back({ammo, leaOffset(), std::string(e.ptr, e.size)});
    *reinterpret_cast<void**>(static_cast<char*>(ammo) + 0x50) = newCircle();
}

// ---------------------------------------------------------------- originals of the hooked methods

// What the originals do in the game, as far as it matters to the plugin: on/off set the flag, turning ammo
// on draws a circle, turning it off removes it. Every call is recorded in the journal.
inline void setFlag(void* f, size_t off, uint8_t v) { static_cast<uint8_t*>(f)[off] = v; }
inline void origAmmoOn(void* f) {
    record("ammo.on", f);
    setFlag(f, kAmmoOff, 0);
    *reinterpret_cast<void**>(static_cast<char*>(f) + 0x50) = newCircle();
}
inline void origAmmoOff(void* f) {
    record("ammo.off", f);
    setFlag(f, kAmmoOff, 1);
    *reinterpret_cast<void**>(static_cast<char*>(f) + 0x50) = nullptr;
}
inline void origFuelOn(void* f) {
    record("fuel.on", f);
    setFlag(f, kAmmoOff, 0);
}
inline void origFuelOff(void* f) {
    record("fuel.off", f);
    setFlag(f, kAmmoOff, 1);
}
inline void origSparesOn(void* f) {
    record("spares.on", f);
    setFlag(f, kSparesOff, 0);
}
inline void origSparesOff(void* f) {
    record("spares.off", f);
    setFlag(f, kSparesOff, 1);
}
inline void origRepairOn(void* f) {
    record("repair.on", f);
    setFlag(f, kRepairOff, 0);
}
inline void origRepairOff(void* f) {
    record("repair.off", f);
    setFlag(f, kRepairOff, 1);
}
inline void origAmmoUpdate(void* f, float dt) { record("ammo.update", f, dt); }
inline void origFuelUpdate(void* f, float dt) { record("fuel.update", f, dt); }
inline void origSparesUpdate(void* f, float dt) { record("spares.update", f, dt); }
inline void origRepairUpdate(void* f, float dt) { record("repair.update", f, dt); }
inline bool g_canAnswer = true;  // what the original canRecharge answers
inline void* g_canTarget = nullptr;
inline bool origAmmoCanRecharge(void* f, void* target) {
    record("ammo.canRecharge", f);
    g_canTarget = target;
    return g_canAnswer;
}

// Component class vtables: the plugin compares the address at the start of a component with them, but never
// calls methods through them (it takes the originals from g_orig*). So these are just distinct addresses.
inline void* g_ammoVt[20], *g_fuelVt[20], *g_sparesVt[20], *g_repairVt[20], *g_modulesVt[40], *g_otherVt[20];
inline uintptr_t vt(void** table) { return reinterpret_cast<uintptr_t>(table); }

inline float g_supplyRadius = 20.0f;  // the game's global supply zone radius

// Put the "game" and the plugin into the initial state: addresses found, originals are the fakes above,
// journals empty, plugin state (circles, repair timers, debts, log counters) cleared.
inline void reset() {
    g_calls.clear();
    g_freed = 0;
    g_iterCreated = g_iterDeleted = 0;
    g_iterDeleteFlags.clear();
    g_relations.clear();
    g_relationAsks.clear();
    g_hasDiplomacy = true;
    g_diplomacyWorld = nullptr;
    g_incHealth.clear();
    g_draws.clear();
    g_materialTableCalls = 0;
    g_canAnswer = true;
    g_canTarget = nullptr;
    g_supplyRadius = 20.0f;

    g_vehicleVtbl[11] = fn(&vehicleFacets);
    g_vehicleVtbl[15] = fn(&vehicleWorld);
    g_vehicleVtbl[22] = fn(&vehicleInfo);
    g_dipVtbl[22] = fn(&relation);

    memset(g_materialTable, 0, sizeof g_materialTable);
    ammoMaterialEntry() = MsvcString{kAmmoMaterial, {}, strlen(kAmmoMaterial), strlen(kAmmoMaterial)};
    const uint8_t lea[8] = {0x4C, 0x8D, 0x80, 0x20, 0x01, 0x00, 0x00, 0xCC};  // lea r8,[rax+0x120]
    memcpy(g_circleLea, lea, sizeof lea);

    g_a = Addr{};
    g_a.ammoVt = vt(g_ammoVt);
    g_a.fuelVt = vt(g_fuelVt);
    g_a.sparesVt = vt(g_sparesVt);
    g_a.repairVt = vt(g_repairVt);
    g_a.modulesVt = vt(g_modulesVt);
    g_a.supplyRadius = reinterpret_cast<uintptr_t>(&g_supplyRadius);
    g_a.showCircle = reinterpret_cast<uintptr_t>(&showCircle);
    g_a.circleLea = reinterpret_cast<uintptr_t>(g_circleLea);
    g_a.materialTable = reinterpret_cast<uintptr_t>(&materialTable);
    g_a.incHealth = reinterpret_cast<uintptr_t>(&incHealth);
    g_a.diplomacy = reinterpret_cast<uintptr_t>(&diplomacyOf);

    g_origAmmoOn = &origAmmoOn;
    g_origAmmoOff = &origAmmoOff;
    g_origFuelOn = &origFuelOn;
    g_origFuelOff = &origFuelOff;
    g_origSparesOn = &origSparesOn;
    g_origSparesOff = &origSparesOff;
    g_origRepairOn = &origRepairOn;
    g_origRepairOff = &origRepairOff;
    g_origAmmoCanRecharge = &origAmmoCanRecharge;
    g_origAmmoUpdate = &origAmmoUpdate;
    g_origFuelUpdate = &origFuelUpdate;
    g_origSparesUpdate = &origSparesUpdate;
    g_origRepairUpdate = &origRepairUpdate;

    g_ownCircles.clear();
    g_circleWritable = true;
    g_repairTimers.clear();
    g_armorDebt.clear();
    g_events = 0;
    g_repairLogs = 0;
}

// Switch a component on the way the game would without the plugin (the "off" flag = 0).
inline void turnOn(Facet& f, size_t offFlag) { f.at<uint8_t>(offFlag) = 0; }
inline bool isOnFlag(Facet& f, size_t offFlag) { return f.at<uint8_t>(offFlag) == 0; }

}  // namespace fake
