// Tests of the game-agnostic core of the SparesSupply plugin (plugins/spares_supply/core).
//
// The core holds the mod's rules: how much HEMTT Repair repairs and what it costs, whom it may repair, how
// spare parts follow F4 mode, which circle a station shows, who may hand out ammo. It talks to the game only
// through ports, so here the ports are plain test objects: vehicles are simple lists of modules, and every
// port writes down what it was asked to do. No fake engine, no game memory - if a port of the real game
// (logic.dll today, maybe an official API tomorrow) does what its interface says, these rules hold.
//
// This program is built without Windows headers (tests/CMakeLists.txt): if the core ever starts depending
// on Windows or on logic.dll, it stops compiling here.
//
// "QUIRK:" marks behaviour that looks odd but is how things work today; change it deliberately, with the test.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "third_party/doctest.h"

#include <array>
#include <cstdio>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "../plugins/spares_supply/core/supply.h"

using namespace spares_supply::core;

namespace {

// ================================================================ test vehicles

// One module of a test vehicle: health, and armor layers on 6 sides (current and original thickness).
struct ModuleData {
    int hp = 100, maxHp = 100;
    bool described = true;
    std::array<std::vector<float>, 6> armor{};
    std::array<std::vector<float>, 6> full{};
};

// A test vehicle: its modules, its own spare parts and a record of addHealth calls.
struct VehicleData {
    std::vector<ModuleData> modules;
    int spares = 7;
    std::vector<std::pair<size_t, int>> addHealthCalls;  // (module, amount)

    explicit VehicleData(std::vector<ModuleData> m = {}, int stock = 7) : modules(std::move(m)), spares(stock) {}
};

// core::Modules over a VehicleData. addHealth behaves like the game: not above the maximum, and the vehicle
// itself pays nothing. Total health is the sum of the modules' health.
class TestModules final : public Modules {
public:
    explicit TestModules(VehicleData& d) : d_(d) {}
    int totalHealth() const override {
        int t = 0;
        for (auto& m : d_.modules) t += m.hp;
        return t;
    }
    size_t count() const override { return d_.modules.size(); }
    bool described(size_t i) const override { return d_.modules[i].described; }
    int health(size_t i) const override { return d_.modules[i].hp; }
    int maxHealth(size_t i) const override { return d_.modules[i].maxHp; }
    int addHealth(size_t i, int amount) override {
        d_.addHealthCalls.push_back({i, amount});
        ModuleData& m = d_.modules[i];
        int gain = std::min(amount, m.maxHp - m.hp);
        m.hp += gain;
        return gain;
    }
    int sides() const override { return 6; }
    size_t layers(size_t i, int side) const override {
        return std::min(d_.modules[i].armor[side].size(), d_.modules[i].full[side].size());
    }
    float thickness(size_t i, int side, size_t k) const override { return d_.modules[i].armor[side][k]; }
    float fullThickness(size_t i, int side, size_t k) const override { return d_.modules[i].full[side][k]; }
    void setThickness(size_t i, int side, size_t k, float v) override { d_.modules[i].armor[side][k] = v; }
    int spareParts() const override { return d_.spares; }
    void setSpareParts(int v) override { d_.spares = v; }

private:
    VehicleData& d_;
};

// A damaged vehicle with one module at 50 of 100.
VehicleData damaged() { return VehicleData{{ModuleData{50, 100}}}; }

// Opaque handles for test objects (the core never looks inside them).
template <class T>
Vehicle* vehicle(T& x) {
    return reinterpret_cast<Vehicle*>(&x);
}
template <class T>
Station* station(T& x) {
    return reinterpret_cast<Station*>(&x);
}
template <class T>
SparesDepot* depot(T& x) {
    return reinterpret_cast<SparesDepot*>(&x);
}

// One repair pulse of a vehicle paid from stock (debt - debt).
Repaired pulse(VehicleData& v, int& stock, float& debt) {
    TestModules m(v);
    return repairVehicle(m, Stock{stock, debt});
}

// ================================================================ test ports

// The repair port: who owns a station, who is in its zone, teams and relations, modules; records logs.
class TestRepairPort final : public RepairPort {
public:
    std::map<Station*, Vehicle*> owners;
    std::map<Station*, std::vector<Vehicle*>> zones;
    std::map<Vehicle*, int> teams;  // a vehicle missing here has no team
    std::map<std::pair<int, int>, int> relations;  // (target team, owner team) -> relation; default 0
    bool diplomacy = true;
    std::map<Vehicle*, VehicleData*> vehicles;  // a vehicle missing here has no modules (infantry)
    std::vector<std::pair<int, int>> relationAsks;
    std::vector<std::string> lines, events;

    Vehicle* owner(Station* s) override { return owners[s]; }
    std::vector<Vehicle*> inZone(Station* s) override { return zones[s]; }
    std::optional<int> relation(Vehicle* owner, Vehicle* target) override {
        if (!diplomacy || !teams.count(owner) || !teams.count(target)) return std::nullopt;
        relationAsks.push_back({teams[target], teams[owner]});
        auto it = relations.find({teams[target], teams[owner]});
        return it == relations.end() ? 0 : it->second;
    }
    std::unique_ptr<Modules> modulesOf(Vehicle* v) override {
        auto it = vehicles.find(v);
        return it == vehicles.end() ? nullptr : std::make_unique<TestModules>(*it->second);
    }
    void log(const char* line) override { lines.push_back(line); }
    void event(const char* what, Station*) override { events.push_back(what); }
};

// HEMTT Repair (team 1, 10000 spare parts) with its station, ready to repair vehicles in its zone.
struct RepairScene {
    TestRepairPort port;
    RepairState state;
    VehicleData truck{{ModuleData{}}, 10000};
    int stationObject = 0;
    Station* s = station(stationObject);
    RepairScene() {
        port.owners[s] = vehicle(truck);
        port.vehicles[vehicle(truck)] = &truck;
        port.teams[vehicle(truck)] = 1;
    }
    // Put vehicle v of team `team` into the zone (team < 0 - no team).
    void add(VehicleData& v, int team = 1) {
        port.zones[s].push_back(vehicle(v));
        port.vehicles[vehicle(v)] = &v;
        if (team >= 0) port.teams[vehicle(v)] = team;
    }
    void tick(float dt) { tickRepair(port, state, s, dt); }
};

// The spare parts port: one vehicle with a station and maybe a spare parts component; records calls.
class TestSparesPort final : public SparesPort {
public:
    bool hasDepot = true, stationIsOn = false, depotIsOn = false, supplyActive = false;
    int depotObject = 0;
    std::vector<std::string> calls;  // "on", "off", "tick <dt>", "event <what>"

    SparesDepot* depotOf(Station*) override { return hasDepot ? depot(depotObject) : nullptr; }
    bool stationOn(Station*) override { return stationIsOn; }
    bool isOn(SparesDepot*) override { return depotIsOn; }
    void turnOn(SparesDepot*) override {
        calls.push_back("on");
        depotIsOn = true;
    }
    void turnOff(SparesDepot*) override {
        calls.push_back("off");
        depotIsOn = false;
    }
    void tick(SparesDepot*, float dt) override {
        char buf[32];
        snprintf(buf, sizeof buf, "tick %.2f", dt);
        calls.push_back(buf);
    }
    bool supplyModeActive(SparesDepot*) override { return supplyActive; }
    void event(const char* what, SparesDepot*) override { calls.push_back(std::string("event ") + what); }
};

// The common F4 port: the station's kind and state; records circles and events.
class TestSupplyPort final : public SupplyPort {
public:
    Kind k = Kind::Ammo;
    bool on = true;
    std::vector<std::string> calls;  // "circle <kind>", "event <what>"

    Kind kind(Station*) override { return k; }
    bool isOn(Station*) override { return on; }
    void showCircle(Station*, Kind kind) override {
        calls.push_back(kind == Kind::Spares ? "circle spares" : kind == Kind::Repair ? "circle repair" : "circle ammo");
    }
    void event(const char* what, Station*) override { calls.push_back(std::string("event ") + what); }
};

// A station with all three ports.
struct SupplyScene {
    TestSupplyPort supply;
    TestSparesPort spares;
    RepairScene repair;
    Ports ports{supply, spares, repair.port, repair.state};
    Station* s = repair.s;
};

}  // namespace

// ================================================================ paying with spare parts

TEST_CASE("Payment: a fractional price accumulates, whole spare parts are taken from stock") {
    // Setup: 10 spare parts in stock, no debt.
    int spares = 10;
    float debt = 0;
    Stock stock{spares, debt};
    Repaired r;

    // Action: pay 2.5, then 0.6.
    stock.pay(2.5f, r);
    // Expected: 2 taken (the whole part), 0.5 left as debt.
    CHECK(spares == 8);
    CHECK(debt == doctest::Approx(0.5f));
    CHECK(r.spent == 2);
    stock.pay(0.6f, r);
    // 0.5 + 0.6 = 1.1 -> one more taken, 0.1 left as debt.
    CHECK(spares == 7);
    CHECK(debt == doctest::Approx(0.1f));
    CHECK(r.spent == 3);
}

TEST_CASE("Payment: no more is taken than there is; the rest stays as debt") {
    // QUIRK: the debt can grow larger than the stock - it just waits for a resupply.
    int spares = 1;
    float debt = 0;
    Stock stock{spares, debt};
    Repaired r;
    stock.pay(5.0f, r);
    CHECK(spares == 0);
    CHECK(debt == doctest::Approx(4.0f));
    CHECK(r.spent == 1);
}

// ================================================================ repairing one vehicle

TEST_CASE("Module repair: +2% of max per pulse, through the game's own module repair") {
    // Setup: a module at 50 of 100, 1000 in stock.
    VehicleData v = damaged();
    int stock = 1000;
    float debt = 0;

    Repaired r = pulse(v, stock, debt);

    // Expected: +2 health (2% of 100) via addHealth of module #0; 2 spare parts paid.
    REQUIRE(v.addHealthCalls.size() == 1);
    CHECK(v.addHealthCalls[0] == std::pair<size_t, int>{0, 2});
    CHECK(v.modules[0].hp == 52);
    CHECK(r.health == 2);
    CHECK(r.spent == 2);
    CHECK(stock == 998);
    CHECK(v.spares == 7);  // the repaired vehicle's own spare parts are not spent
}

TEST_CASE("Module repair: at least 1, no more than missing, no more than in stock") {
    // Each vehicle also has an intact module: a vehicle with total health 0 counts as destroyed.
    const ModuleData intact{100, 100};
    int stock = 1000;
    float debt = 0;
    // 2% of 30 = 0.6 -> rounds down to 0, but it is never less than 1.
    VehicleData small{{ModuleData{0, 30}, intact}};
    pulse(small, stock, debt);
    CHECK(small.modules[0].hp == 1);
    // 1 short of the maximum - 1 is added, not 2.
    VehicleData almost{{ModuleData{99, 100}, intact}};
    pulse(almost, stock, debt);
    CHECK(almost.modules[0].hp == 100);
    // 2% of 1000 = 20, but only 3 in stock - 3 is added.
    VehicleData big{{ModuleData{0, 1000}, intact}};
    int three = 3;
    pulse(big, three, debt);
    CHECK(big.modules[0].hp == 3);
    CHECK(three == 0);
}

TEST_CASE("Repair: intact modules are left alone, all damaged ones are repaired in one pulse") {
    VehicleData v{{ModuleData{100, 100}, ModuleData{10, 100}, ModuleData{20, 50}}};
    int stock = 1000;
    float debt = 0;
    Repaired r = pulse(v, stock, debt);
    REQUIRE(v.addHealthCalls.size() == 2);  // module #0 is intact
    CHECK(v.addHealthCalls[0].first == 1u);
    CHECK(v.addHealthCalls[1].first == 2u);
    CHECK(v.modules[1].hp == 12);  // +2% of 100
    CHECK(v.modules[2].hp == 21);  // +2% of 50 = 1
    CHECK(r.health == 3);
}

TEST_CASE("Armor: every worn layer gets +2% of its original thickness, 1 spare part per 1 mm") {
    // Setup: the module is intact; side 0 - layers of 50 and 100 mm with originals of 100 and 100; side 5 -
    // a layer of 99 of 100 mm.
    ModuleData m;
    m.armor[0] = {50, 100};
    m.full[0] = {100, 100};
    m.armor[5] = {99};
    m.full[5] = {100};
    VehicleData v{{m}};
    int stock = 1000;
    float debt = 0;

    Repaired r = pulse(v, stock, debt);

    // Expected: 50 -> 52 (+2% of 100); the intact layer untouched; 99 -> 100 (not above the original).
    CHECK(v.modules[0].armor[0][0] == 52.0f);
    CHECK(v.modules[0].armor[0][1] == 100.0f);
    CHECK(v.modules[0].armor[5][0] == 100.0f);
    CHECK(r.layers == 2);
    // Price: 2 mm + 1 mm = 3 spare parts.
    CHECK(r.spent == 3);
    CHECK(stock == 997);
    CHECK(v.addHealthCalls.empty());  // the module's health is full
}

TEST_CASE("Armor: a fractional price accumulates as debt") {
    // A layer with an original thickness of 25 mm: +0.5 mm per pulse - 0.5 spare parts.
    ModuleData m;
    m.armor[0] = {10};
    m.full[0] = {25};
    VehicleData v{{m}};
    int stock = 100;
    float debt = 0;
    Repaired r = pulse(v, stock, debt);
    CHECK(v.modules[0].armor[0][0] == 10.5f);
    CHECK(r.spent == 0);  // nothing taken yet
    CHECK(debt == doctest::Approx(0.5f));
    r = pulse(v, stock, debt);
    CHECK(r.spent == 1);  // second pulse: 0.5 + 0.5 = 1 spare part
    CHECK(stock == 99);
}

TEST_CASE("Armor: layers with zero original thickness and extra current layers are skipped") {
    // Side 0: the current thickness has 3 layers, but the module description has 2 (the second with thickness
    // 0). Only layers present in both lists are used; a layer with original thickness 0 is not repaired.
    ModuleData m;
    m.armor[0] = {10, 0, 10};
    m.full[0] = {100, 0};
    VehicleData v{{m}};
    int stock = 100;
    float debt = 0;
    Repaired r = pulse(v, stock, debt);
    CHECK(v.modules[0].armor[0][0] == 12.0f);
    CHECK(v.modules[0].armor[0][1] == 0.0f);
    CHECK(v.modules[0].armor[0][2] == 10.0f);  // the third layer is not in the description - untouched
    CHECK(r.layers == 1);
}

TEST_CASE("A destroyed vehicle (total health 0) is not repaired") {
    ModuleData m{0, 100};
    m.armor[0] = {10};
    m.full[0] = {100};
    VehicleData v{{m}};
    int stock = 100;
    float debt = 0;
    Repaired r = pulse(v, stock, debt);
    CHECK(v.addHealthCalls.empty());
    CHECK(v.modules[0].armor[0][0] == 10.0f);
    CHECK(r.health == 0);
    CHECK(stock == 100);
}

TEST_CASE("A module the game cannot describe is skipped, the next one is repaired") {
    ModuleData broken{10, 100};
    broken.described = false;
    VehicleData v{{broken, ModuleData{10, 100}}};
    int stock = 100;
    float debt = 0;
    pulse(v, stock, debt);
    REQUIRE(v.addHealthCalls.size() == 1);
    CHECK(v.addHealthCalls[0].first == 1u);
}

TEST_CASE("Stock ran out - the following modules are not repaired") {
    VehicleData v{{ModuleData{10, 100}, ModuleData{10, 100}}};
    int stock = 2;
    float debt = 0;
    pulse(v, stock, debt);
    CHECK(v.modules[0].hp == 12);
    CHECK(v.modules[1].hp == 10);
    CHECK(stock == 0);
}

TEST_CASE("Stock ran out in the middle of a side - that side's armor is finished on debt") {
    // QUIRK: stock is checked before each armor side, not before each layer. So the layers of a side that was
    // started are all repaired, and the missing price goes into debt.
    ModuleData m;
    m.armor[0] = {50, 50, 50};
    m.full[0] = {100, 100, 100};
    m.armor[1] = {50};
    m.full[1] = {100};
    VehicleData v{{m}};
    int stock = 1;
    float debt = 0;
    Repaired r = pulse(v, stock, debt);
    // All 3 layers of side 0 grew by 2 mm (price 6), 1 spare part paid, 5 in debt.
    for (size_t k = 0; k < 3; ++k) CHECK(v.modules[0].armor[0][k] == 52.0f);
    CHECK(r.layers == 3);
    CHECK(r.spent == 1);
    CHECK(stock == 0);
    CHECK(debt == doctest::Approx(5.0f));
    // Side 1 is no longer touched.
    CHECK(v.modules[0].armor[1][0] == 50.0f);
}

// ================================================================ whom HEMTT Repair repairs

TEST_CASE("mayRepair: own side and allies - yes; relation 2 or 4, or unknown - no") {
    CHECK(mayRepair(0));
    CHECK(mayRepair(1));
    CHECK(mayRepair(3));
    CHECK_FALSE(mayRepair(2));
    CHECK_FALSE(mayRepair(4));
    CHECK_FALSE(mayRepair(std::nullopt));  // no diplomacy or no team
}

// ================================================================ repair pulse over the zone

TEST_CASE_FIXTURE(RepairScene, "A repair pulse every 0.5 seconds") {
    VehicleData d = damaged();
    add(d);

    tick(0.25f);  // first frame - a pulse right away
    CHECK(d.modules[0].hp == 52);
    tick(0.25f);  // 0.25 s later - too early
    CHECK(d.modules[0].hp == 52);
    tick(0.25f);  // 0.5 s later - the next pulse
    CHECK(d.modules[0].hp == 54);
}

TEST_CASE_FIXTURE(RepairScene, "Repair is paid with HEMTT Repair's spare parts") {
    VehicleData d = damaged();
    add(d);
    tick(0.1f);
    CHECK(truck.spares == 10000 - 2);
    CHECK(d.spares == 7);
}

TEST_CASE_FIXTURE(RepairScene, "Repair: own side and allies - yes, enemies (relation 2 or 4) - no") {
    // Setup: HEMTT Repair is team 1. In the zone are vehicles of teams 1, 2, 3, 4, 5; teams 3 and 4 relate to
    // team 1 as "enemies" (2 and 4), the others as 0, 1, 3.
    VehicleData own = damaged(), team2 = damaged(), enemyA = damaged(), enemyB = damaged(), team5 = damaged();
    add(own, 1);
    add(team2, 2);
    add(enemyA, 3);
    add(enemyB, 4);
    add(team5, 5);
    port.relations[{2, 1}] = 1;
    port.relations[{3, 1}] = 2;
    port.relations[{4, 1}] = 4;
    port.relations[{5, 1}] = 3;

    tick(0.1f);

    CHECK(own.modules[0].hp == 52);
    CHECK(team2.modules[0].hp == 52);
    CHECK(enemyA.modules[0].hp == 50);
    CHECK(enemyB.modules[0].hp == 50);
    CHECK(team5.modules[0].hp == 52);
    // The relation was asked as (target team, HEMTT Repair's team).
    CHECK(port.relationAsks.front() == std::pair<int, int>{1, 1});
    CHECK(port.relationAsks[2] == std::pair<int, int>{3, 1});
}

TEST_CASE_FIXTURE(RepairScene, "Repair: HEMTT Repair itself, vehicles without a team and without modules are skipped") {
    // Setup: in the zone are HEMTT Repair itself (damaged), a vehicle without a team, an infantryman (no
    // modules), an empty entry and an ordinary damaged vehicle.
    truck.modules[0].hp = 10;
    port.zones[s].push_back(vehicle(truck));
    VehicleData noTeam = damaged();
    add(noTeam, -1);
    int infantry = 0;
    port.zones[s].push_back(vehicle(infantry));
    port.teams[vehicle(infantry)] = 1;
    port.zones[s].push_back(nullptr);
    VehicleData ok = damaged();
    add(ok);

    tick(0.1f);

    CHECK(truck.modules[0].hp == 10);    // does not repair itself
    CHECK(noTeam.modules[0].hp == 50);   // no team - not allowed
    CHECK(ok.modules[0].hp == 52);       // this does not stop the others
}

TEST_CASE_FIXTURE(RepairScene, "Repair: no diplomacy in the world - repairs nobody") {
    VehicleData d = damaged();
    add(d);
    port.diplomacy = false;
    tick(0.1f);
    CHECK(d.modules[0].hp == 50);
}

TEST_CASE_FIXTURE(RepairScene, "Repair: a station without an owner, or an owner without modules - nothing happens") {
    VehicleData d = damaged();
    add(d);
    port.vehicles.erase(vehicle(truck));  // HEMTT Repair has no modules (and so no stock)
    tick(0.1f);
    port.owners[s] = nullptr;
    state.timers.clear();
    tick(0.1f);
    CHECK(d.modules[0].hp == 50);
}

TEST_CASE_FIXTURE(RepairScene, "Repair: empty stock - nothing happens") {
    VehicleData d = damaged();
    add(d);
    truck.spares = 0;
    tick(0.1f);
    CHECK(d.modules[0].hp == 50);
    CHECK(d.addHealthCalls.empty());
    CHECK(port.events.empty());  // an empty stock at the start of a pulse is not reported
}

TEST_CASE_FIXTURE(RepairScene, "Repair: stock ran out during a pulse - the rest wait, and the event is reported") {
    VehicleData a = damaged(), b = damaged();
    add(a);
    add(b);
    truck.spares = 2;
    tick(0.1f);
    CHECK(a.modules[0].hp == 52);
    CHECK(b.modules[0].hp == 50);
    CHECK(truck.spares == 0);
    CHECK(port.events == std::vector<std::string>{"repair: out of spare parts"});
}

TEST_CASE("Repair: each station has its own armor debt, and it survives between pulses") {
    // Two HEMTT Repairs each repair a 25 mm layer (+0.5 mm = 0.5 spare parts per pulse).
    ModuleData m;
    m.armor[0] = {10};
    m.full[0] = {25};
    RepairScene r1, r2;
    VehicleData d1{{m}}, d2{{m}};
    r1.add(d1);
    r2.add(d2);
    RepairState shared;  // both stations in one state, as in the game

    tickRepair(r1.port, shared, r1.s, 0.5f);  // r1: debt 0.5
    tickRepair(r2.port, shared, r2.s, 0.5f);  // r2: debt 0.5
    CHECK(r1.truck.spares == 10000);
    tickRepair(r1.port, shared, r1.s, 0.5f);  // r1: 0.5 + 0.5 = 1 -> 1 taken
    CHECK(r1.truck.spares == 9999);
    CHECK(r2.truck.spares == 10000);  // r2 has its own debt, still 0.5
    CHECK(shared.debt[r2.s] == doctest::Approx(0.5f));
}

TEST_CASE_FIXTURE(RepairScene, "forgetRepair drops the station's timer and debt") {
    state.timers[s] = 0.3f;
    state.debt[s] = 0.5f;
    forgetRepair(state, s);
    CHECK(state.timers.count(s) == 0);
    CHECK(state.debt.count(s) == 0);
}

TEST_CASE_FIXTURE(RepairScene, "Repair log: what was done and what it cost; at most 40 lines") {
    VehicleData d{{ModuleData{0, 1000}, ModuleData{100, 100}}};  // total health 100
    add(d);
    tick(0.5f);
    char line[160];
    snprintf(line, sizeof line, "repair: target=%p +20 hp, 0 armor layers, total hp 120; spent 20 spares, 9980 left",
             static_cast<void*>(&d));
    REQUIRE(port.lines.size() == 1);
    CHECK(port.lines[0] == line);
    for (int i = 0; i < 50; ++i) tick(0.5f);
    CHECK(port.lines.size() == 40u);
}

// ================================================================ HEMTT Spares rules

TEST_CASE("syncSpares switches spare parts only if they are in the other state") {
    TestSparesPort port;
    int st = 0;
    syncSpares(port, station(st), true);
    syncSpares(port, station(st), true);  // already on - not switched on a second time
    syncSpares(port, station(st), false);
    syncSpares(port, station(st), false);
    CHECK(port.calls == std::vector<std::string>{"event spares ON  (with supply mode)", "on",
                                                 "event spares OFF (with supply mode)", "off"});

    // A vehicle without spare parts - nothing.
    TestSparesPort none;
    none.hasDepot = false;
    syncSpares(none, station(st), true);
    CHECK(none.calls.empty());
}

TEST_CASE("tickSpares: ticks switched-on spare parts while the station is on") {
    TestSparesPort port;
    int st = 0;
    // Station off - nothing, no component returned.
    CHECK(tickSpares(port, station(st), 0.25f) == nullptr);
    // Station on, spare parts off - the component is returned, but not ticked.
    port.stationIsOn = true;
    CHECK(tickSpares(port, station(st), 0.25f) == depot(port.depotObject));
    CHECK(port.calls.empty());
    // Both on - ticked with the same dt.
    port.depotIsOn = true;
    tickSpares(port, station(st), 0.25f);
    CHECK(port.calls == std::vector<std::string>{"tick 0.25"});
}

TEST_CASE("Spare parts may not be switched off while F4 mode runs on their vehicle") {
    TestSparesPort port;
    port.supplyActive = true;
    CHECK_FALSE(mayTurnSparesOff(port, depot(port.depotObject)));
    CHECK(port.calls == std::vector<std::string>{"event spares OFF skipped (supply mode active)"});
    port.supplyActive = false;
    CHECK(mayTurnSparesOff(port, depot(port.depotObject)));
}

// ================================================================ common F4 rules

TEST_CASE_FIXTURE(SupplyScene, "F4 on: spare parts follow, the circle gets the vehicle's material") {
    // HEMTT Spares: spare parts on, spares circle.
    supply.k = Kind::Spares;
    onSupplyOn(ports, s);
    CHECK(spares.calls == std::vector<std::string>{"event spares ON  (with supply mode)", "on"});
    CHECK(supply.calls == std::vector<std::string>{"circle spares"});

    // HEMTT Repair (no spare parts component): an event and the repair circle.
    SupplyScene r;
    r.supply.k = Kind::Repair;
    r.spares.hasDepot = false;
    onSupplyOn(r.ports, r.s);
    CHECK(r.supply.calls == std::vector<std::string>{"event repair ON  (with supply mode)", "circle repair"});

    // An ordinary HEMTT Supply: asked to keep its own circle.
    SupplyScene a;
    a.spares.hasDepot = false;
    onSupplyOn(a.ports, a.s);
    CHECK(a.supply.calls == std::vector<std::string>{"circle ammo"});
}

TEST_CASE_FIXTURE(SupplyScene, "F4 off: the repair state is forgotten, spare parts follow") {
    repair.state.timers[s] = 0.3f;
    repair.state.debt[s] = 0.5f;
    spares.depotIsOn = true;
    onSupplyOff(ports, s);
    CHECK(repair.state.timers.count(s) == 0);
    CHECK(repair.state.debt.count(s) == 0);
    CHECK(spares.calls == std::vector<std::string>{"event spares OFF (with supply mode)", "off"});
}

TEST_CASE_FIXTURE(SupplyScene, "Every F4 frame: HEMTT Spares ticks spare parts, HEMTT Repair repairs") {
    // Station off - nothing at all.
    supply.on = false;
    onSupplyTick(ports, s, 0.25f);
    CHECK(spares.calls.empty());
    CHECK(supply.calls.empty());

    // HEMTT Spares: spare parts ticked, the circle checked (repainted if it was created without us).
    supply.on = true;
    supply.k = Kind::Spares;
    spares.stationIsOn = spares.depotIsOn = true;
    onSupplyTick(ports, s, 0.25f);
    CHECK(spares.calls == std::vector<std::string>{"tick 0.25"});
    CHECK(supply.calls == std::vector<std::string>{"circle spares"});

    // HEMTT Repair: a repair pulse and the repair circle.
    SupplyScene r;
    r.supply.k = Kind::Repair;
    r.spares.hasDepot = false;
    r.spares.stationIsOn = true;
    VehicleData d = damaged();
    r.repair.add(d);
    onSupplyTick(r.ports, r.s, 0.25f);
    CHECK(d.modules[0].hp == 52);
    CHECK(r.supply.calls == std::vector<std::string>{"circle repair"});

    // An ordinary HEMTT Supply: nothing.
    SupplyScene a;
    a.spares.hasDepot = false;
    a.spares.stationIsOn = true;
    onSupplyTick(a.ports, a.s, 0.25f);
    CHECK(a.supply.calls.empty());
}

TEST_CASE("Only an ordinary HEMTT Supply hands out ammo") {
    CHECK(handsOutAmmo(Kind::Ammo));
    CHECK_FALSE(handsOutAmmo(Kind::Spares));
    CHECK_FALSE(handsOutAmmo(Kind::Repair));
}
