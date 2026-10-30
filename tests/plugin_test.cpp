// Tests of how the SparesSupply plugin behaves during a game.
//
// What is checked is what the player sees:
//   * HEMTT Spares in F4 mode (SUPPLY MODE) hands out spare parts, not ammo, and draws its own zone circle;
//   * HEMTT Repair in F4 mode repairs modules and restores armor only for its own side and allies, and pays
//     with its own spare parts, not with those of the vehicle being repaired;
//   * the ordinary HEMTT Supply and other vehicles behave exactly as without the mod.
// The plugin does this by hooking methods of game components (a "hook" = our function instead of the game's;
// it usually calls the original too). Here the hooks are called directly - the way the game would call
// them - on fake vehicles (support/fake_engine.h), and the test checks what they did.
//
// The plugin's headers declare its internal functions and state, and its sources are compiled into this test
// program (tests/CMakeLists.txt). If a refactoring moves or renames them, only the includes and the fakes need
// to change - the checks themselves describe the behaviour and should stay as they are.
//
// "QUIRK:" marks behaviour that looks odd but is how things work today. The test pins it so the refactoring
// does not change it unnoticed; change it deliberately, together with the test.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "third_party/doctest.h"

#include "../plugins/spares_supply/logic_dll/common.h"
#include "../plugins/spares_supply/logic_dll/repair.h"
#include "../plugins/spares_supply/logic_dll/spares.h"
#include "support/fake_engine.h"
#include "support/log_file.h"

using namespace fake;
using namespace spares_supply;

namespace {

// The plugin's log in tests - plugin_test.log next to the program (in the game - bin\plugins\SparesSupply.log).
std::wstring logFile() {
    static bool opened = false;
    if (!opened) {
        g_log.open(GetModuleHandleW(nullptr), L"plugin_test");
        opened = true;
    }
    return logPath(L"plugin_test");
}

// Common setup for every test: a fresh "game" and a reset plugin state.
struct Engine {
    Engine() {
        logFile();
        reset();
    }
};

// Typical vehicles. HEMTT Supply - ammo only; HEMTT Spares - ammo (for the F4 button) and spare parts;
// HEMTT Repair - ammo (for F4 and the circle) and repair (the "repair vehicle" marker).
struct Supply {
    Vehicle v;
    Facet& ammo = v.add(g_a.ammoVt);
};
struct Spares {
    Vehicle v;
    Facet& ammo = v.add(g_a.ammoVt);
    Facet& spares = v.add(g_a.sparesVt);
};
struct Repair {
    Vehicle v;
    Facet& repair = v.add(g_a.repairVt);  // in the game the repair component is created before ammo
    Facet& ammo = v.add(g_a.ammoVt);
    Facet& modules;
    explicit Repair(int stock = 10000) : modules(v.addModules({ModuleSpec{}}, stock)) {}
};

// A damaged vehicle: modules `specs` (default - one module at 50/100), with 7 spare parts of its own.
struct Damaged {
    Vehicle v;
    Facet& modules;
    explicit Damaged(int team = 1, std::vector<ModuleSpec> specs = {ModuleSpec{50, 100}})
        : v(team), modules(v.addModules(specs, 7)) {}
};

}  // namespace

// ================================================================ spare parts zone log
// These tests come first: the plugin's "spares active" line counter is shared for the whole game run (and test run).

TEST_CASE_FIXTURE(Engine, "Spare parts zone log: radius and number of vehicles in the zone") {
    // Setup: spare parts on, 3 vehicles in their target list ([+0x28..+0x30]), zone radius 25 m.
    Spares s;
    turnOn(s.spares, kSparesOff);
    void* list[3] = {};
    s.spares.at<void**>(0x28) = list;
    s.spares.at<void**>(0x30) = list + 3;
    g_supplyRadius = 25.0f;
    LogTail log(logFile());

    // Action: one second of the zone working.
    hookSparesUpdate(s.spares.p(), 1.0f);

    // Expected: one log line per second - with the radius and the number of vehicles.
    CHECK(log.has("radius=25.0 targets=3"));
}

TEST_CASE_FIXTURE(Engine, "Spare parts zone log: at most 20 lines per run") {
    Spares s;
    turnOn(s.spares, kSparesOff);
    for (int i = 0; i < 30; ++i) hookSparesUpdate(s.spares.p(), 1.0f);  // 30 seconds of work
    CHECK(countOf(readFile(logFile()), "spares active") == 20);
}

// ================================================================ finding a vehicle's component

TEST_CASE_FIXTURE(Engine, "findFacet finds the component of the exact class and keeps reference counts intact") {
    // Setup: a vehicle with ammo and spare parts.
    Spares s;

    // Expected: the right component is found; there is no repair component - nothing; no vehicle - nothing.
    CHECK(findFacet(&s.v, g_a.sparesVt) == s.spares.p());
    CHECK(findFacet(&s.v, g_a.ammoVt) == s.ammo.p());
    CHECK(findFacet(&s.v, g_a.repairVt) == nullptr);
    CHECK(findFacet(nullptr, g_a.ammoVt) == nullptr);

    // The plugin gave back every reference it took: counts unchanged, nothing freed.
    for (auto& b : s.v.blocks) CHECK(b.strong == 1);
    CHECK(g_freed == 0);
    // Every iterator was deleted, with flag 1 (that is how the game itself deletes them).
    CHECK(g_iterDeleted == g_iterCreated);
    for (int f : g_iterDeleteFlags) CHECK(f == 1);
}

TEST_CASE_FIXTURE(Engine, "findFacet: the class is compared exactly, the first match wins") {
    Vehicle v;
    v.add(vt(g_otherVt));  // a different class (e.g. a subclass) - does not match
    Facet& first = v.add(g_a.ammoVt);
    v.add(g_a.ammoVt);
    CHECK(findFacet(&v, g_a.ammoVt) == first.p());
}

TEST_CASE_FIXTURE(Engine, "findFacet: the vehicle gave no iterator - no component") {
    Spares s;
    s.v.nullIterator = true;
    CHECK(findFacet(&s.v, g_a.sparesVt) == nullptr);
}

// ================================================================ what kind of vehicle this is

TEST_CASE_FIXTURE(Engine, "kindOf: a mod vehicle is recognised by its components") {
    Supply supply;
    Spares spares;
    Repair repair;
    CHECK(kindOf(supply.ammo.p()) == Kind::Ammo);    // ordinary HEMTT Supply
    CHECK(kindOf(spares.ammo.p()) == Kind::Spares);  // has spare parts -> HEMTT Spares
    CHECK(kindOf(repair.ammo.p()) == Kind::Repair);  // has repair -> HEMTT Repair

    // Both spare parts and repair - counts as HEMTT Spares (spare parts are checked first).
    Vehicle both;
    Facet& ammo = both.add(g_a.ammoVt);
    both.add(g_a.repairVt);
    both.add(g_a.sparesVt);
    CHECK(kindOf(ammo.p()) == Kind::Spares);

    // A component without an owner vehicle - ordinary.
    Facet orphan;
    CHECK(kindOf(orphan.p()) == Kind::Ammo);
}

// ================================================================ HEMTT Spares: F4 mode

TEST_CASE_FIXTURE(Engine, "syncSpares switches spare parts only if they are in the other state") {
    Spares s;
    syncSpares(s.ammo.p(), true);
    syncSpares(s.ammo.p(), true);  // already on - not switched on a second time
    CHECK(calls("spares.on", s.spares.p()) == 1);
    CHECK(isOnFlag(s.spares, kSparesOff));
    syncSpares(s.ammo.p(), false);
    syncSpares(s.ammo.p(), false);
    CHECK(calls("spares.off", s.spares.p()) == 1);
    CHECK_FALSE(isOnFlag(s.spares, kSparesOff));

    // A vehicle without spare parts - nothing.
    Supply supply;
    syncSpares(supply.ammo.p(), true);
    CHECK(calls("spares.on") == 1);
}

TEST_CASE_FIXTURE(Engine, "HEMTT Spares: F4 switches spare parts on and repaints the circle with wrenches") {
    // Setup: HEMTT Spares, everything off.
    Spares s;

    // Action: the player pressed F4 - the game switches the ammo component on.
    hookAmmoOn(s.ammo.p());

    // Expected: first the original (switches ammo on and draws the ammo circle), then spare parts go on.
    CHECK(callNames() == std::vector<std::string>{"ammo.on", "spares.on"});
    CHECK(isOnFlag(s.spares, kSparesOff));
    // The circle was redrawn once - with the spare parts material: while drawing, the lea held 0xE0
    // (sparesRecharge), and the string in the material table was not touched.
    REQUIRE(g_draws.size() == 1);
    CHECK(g_draws[0].ammo == s.ammo.p());
    CHECK(g_draws[0].lea == 0xE0u);
    CHECK(g_draws[0].material == kAmmoMaterial);
    // After drawing, the lea is restored: other vehicles draw the ammo circle as usual.
    CHECK(leaOffset() == 0x120u);
    // The new circle is remembered as "already ours".
    CHECK(g_ownCircles.count(circleField(s.ammo)) == 1);
}

TEST_CASE_FIXTURE(Engine, "HEMTT Supply: F4 works as without the mod") {
    Supply s;
    void* circleBefore = nullptr;
    hookAmmoOn(s.ammo.p());
    circleBefore = circleField(s.ammo);
    CHECK(callNames() == std::vector<std::string>{"ammo.on"});
    CHECK(g_draws.empty());                    // the circle is not redrawn
    CHECK(circleField(s.ammo) == circleBefore);
    CHECK(g_ownCircles.empty());
}

TEST_CASE_FIXTURE(Engine, "The circle is redrawn once, not every frame") {
    Spares s;
    hookAmmoOn(s.ammo.p());
    for (int i = 0; i < 5; ++i) hookAmmoUpdate(s.ammo.p(), 0.1f);  // 5 frames
    CHECK(g_draws.size() == 1);
}

TEST_CASE_FIXTURE(Engine, "A circle drawn without the plugin (loading a save) is repainted in update") {
    // Setup: ammo already on and an ammo circle already there - the plugin took no part in it.
    Spares s;
    turnOn(s.ammo, kAmmoOff);
    circleField(s.ammo) = newCircle();

    // Action: an ordinary game frame.
    hookAmmoUpdate(s.ammo.p(), 0.1f);

    // Expected: the circle is repainted with the spare parts material.
    REQUIRE(g_draws.size() == 1);
    CHECK(g_draws[0].lea == 0xE0u);
}

TEST_CASE_FIXTURE(Engine, "No circle - nothing to repaint") {
    Spares s;
    turnOn(s.ammo, kAmmoOff);  // on, but there is no circle ([+0x50])
    hookAmmoUpdate(s.ammo.p(), 0.1f);
    CHECK(g_draws.empty());
}

TEST_CASE_FIXTURE(Engine, "The lea instruction could not be made writable - HEMTT Spares keeps the ammo circle") {
    // QUIRK: HEMTT Repair gets its circle another way (string swap), so it still gets its own.
    g_circleWritable = false;
    Spares s;
    hookAmmoOn(s.ammo.p());
    CHECK(g_draws.empty());
    CHECK(g_ownCircles.empty());

    Repair r;
    hookAmmoOn(r.ammo.p());
    CHECK(g_draws.size() == 1);
}

TEST_CASE_FIXTURE(Engine, "HEMTT Spares: leaving F4 switches spare parts off and forgets the circle") {
    Spares s;
    hookAmmoOn(s.ammo.p());
    void* circle = circleField(s.ammo);
    g_calls.clear();

    hookAmmoOff(s.ammo.p());

    // Expected: first the original (switches ammo off, removes the circle), then spare parts are switched
    // off - directly, bypassing our spare parts "off" hook.
    CHECK(callNames() == std::vector<std::string>{"ammo.off", "spares.off"});
    CHECK_FALSE(isOnFlag(s.spares, kSparesOff));
    CHECK(g_ownCircles.count(circle) == 0);
}

TEST_CASE_FIXTURE(Engine, "Every F4 frame: the original ammo update, then a spare parts tick with the same dt") {
    // The game never calls the spare parts update itself - without the plugin, switched-on spare parts would
    // hand out nothing.
    Spares s;
    hookAmmoOn(s.ammo.p());
    g_calls.clear();

    hookAmmoUpdate(s.ammo.p(), 0.25f);

    REQUIRE(g_calls.size() == 2);
    CHECK(g_calls[0].what == "ammo.update");
    CHECK(g_calls[1].what == "spares.update");
    CHECK(g_calls[1].obj == s.spares.p());
    CHECK(g_calls[1].dt == 0.25f);
}

TEST_CASE_FIXTURE(Engine, "Outside F4 mode the ammo update is only the original") {
    Spares s;
    Repair r;
    hookAmmoUpdate(s.ammo.p(), 0.25f);
    hookAmmoUpdate(r.ammo.p(), 0.25f);
    CHECK(callNames() == std::vector<std::string>{"ammo.update", "ammo.update"});
    CHECK(g_incHealth.empty());
    CHECK(g_draws.empty());
}

TEST_CASE_FIXTURE(Engine, "F4 on but spare parts off - spare parts are not ticked") {
    // QUIRK: the circle is still repainted with the spare parts material.
    Spares s;
    turnOn(s.ammo, kAmmoOff);
    circleField(s.ammo) = newCircle();
    hookAmmoUpdate(s.ammo.p(), 0.25f);
    CHECK(calls("spares.update") == 0);
    CHECK(g_draws.size() == 1);
}

TEST_CASE_FIXTURE(Engine, "The spare parts update hook calls the original with the same dt") {
    Spares s;
    hookSparesUpdate(s.spares.p(), 0.5f);  // off - the original is still called
    REQUIRE(g_calls.size() == 1);
    CHECK(g_calls[0].what == "spares.update");
    CHECK(g_calls[0].dt == 0.5f);
}

TEST_CASE_FIXTURE(Engine, "Switching spare parts off is skipped while the vehicle is in F4 mode") {
    // The click order "resupply spare parts" switches spare parts off when it ends; in F4 mode that would cut
    // the zone resupply short.
    Spares s;
    turnOn(s.spares, kSparesOff);
    turnOn(s.ammo, kAmmoOff);
    hookSparesOff(s.spares.p());
    CHECK(calls("spares.off") == 0);
    CHECK(isOnFlag(s.spares, kSparesOff));

    // F4 mode is over - switching off goes through.
    s.ammo.at<uint8_t>(kAmmoOff) = 1;
    hookSparesOff(s.spares.p());
    CHECK(calls("spares.off") == 1);
}

TEST_CASE_FIXTURE(Engine, "Switching spare parts off is also skipped while the refueller is on") {
    Vehicle v;
    Facet& fuel = v.add(g_a.fuelVt);
    Facet& spares = v.add(g_a.sparesVt);
    turnOn(fuel, kAmmoOff);
    turnOn(spares, kSparesOff);
    hookSparesOff(spares.p());
    CHECK(calls("spares.off") == 0);
}

TEST_CASE_FIXTURE(Engine, "Spare parts on a vehicle without ammo or refueller switch off as usual") {
    Vehicle v;
    Facet& spares = v.add(g_a.sparesVt);
    turnOn(spares, kSparesOff);
    hookSparesOff(spares.p());
    CHECK(calls("spares.off") == 1);
}

// ================================================================ refueller + spare parts

TEST_CASE_FIXTURE(Engine, "Refueller: F4 switches the same vehicle's spare parts on and off") {
    // For a vehicle with refuel and support: then F4 mode switches the refueller.
    Vehicle v;
    Facet& fuel = v.add(g_a.fuelVt);
    Facet& spares = v.add(g_a.sparesVt);

    hookFuelOn(fuel.p());
    CHECK(callNames() == std::vector<std::string>{"fuel.on", "spares.on"});
    CHECK(isOnFlag(spares, kSparesOff));
    g_calls.clear();
    hookFuelOff(fuel.p());
    CHECK(callNames() == std::vector<std::string>{"fuel.off", "spares.off"});
    CHECK(g_draws.empty());  // the refueller's circle is left alone
}

TEST_CASE_FIXTURE(Engine, "Refueller without spare parts - only the original") {
    Vehicle v;
    Facet& fuel = v.add(g_a.fuelVt);
    hookFuelOn(fuel.p());
    hookFuelOff(fuel.p());
    CHECK(callNames() == std::vector<std::string>{"fuel.on", "fuel.off"});
}

TEST_CASE_FIXTURE(Engine, "Refueller: every F4 frame - the original and a spare parts tick") {
    Vehicle v;
    Facet& fuel = v.add(g_a.fuelVt);
    Facet& spares = v.add(g_a.sparesVt);

    hookFuelUpdate(fuel.p(), 0.2f);  // off - only the original
    CHECK(callNames() == std::vector<std::string>{"fuel.update"});

    g_calls.clear();
    turnOn(fuel, kAmmoOff);
    turnOn(spares, kSparesOff);
    hookFuelUpdate(fuel.p(), 0.2f);
    REQUIRE(g_calls.size() == 2);
    CHECK(g_calls[0].what == "fuel.update");
    CHECK(g_calls[1].what == "spares.update");
    CHECK(g_calls[1].dt == 0.2f);
}

// ================================================================ mod vehicles hand out no ammo

TEST_CASE_FIXTURE(Engine, "canRecharge: HEMTT Spares and HEMTT Repair never hand out ammo") {
    Spares s;
    Repair r;
    Vehicle target;
    CHECK_FALSE(hookAmmoCanRecharge(s.ammo.p(), &target));
    CHECK_FALSE(hookAmmoCanRecharge(r.ammo.p(), &target));
    CHECK(calls("ammo.canRecharge") == 0);  // the original is not even asked
}

TEST_CASE_FIXTURE(Engine, "canRecharge: for an ordinary HEMTT Supply the game decides") {
    Supply s;
    Vehicle target;
    g_canAnswer = true;
    CHECK(hookAmmoCanRecharge(s.ammo.p(), &target));
    CHECK(g_canTarget == &target);  // the same target was passed to the original
    g_canAnswer = false;
    CHECK_FALSE(hookAmmoCanRecharge(s.ammo.p(), &target));
    CHECK(calls("ammo.canRecharge") == 2);
}

// ================================================================ HEMTT Repair: F4 mode

TEST_CASE_FIXTURE(Engine, "HEMTT Repair: F4 switches repair and ammo on, circle with the repair material") {
    // F4 mode on HEMTT Repair switches the repair component (it is the vehicle's first one), but the circle is
    // drawn and the targets collected by ammo - so the switch is passed on to it.
    Repair r;

    hookRepairOn(r.repair.p());

    CHECK(callNames() == std::vector<std::string>{"repair.on", "ammo.on"});
    CHECK(isOnFlag(r.repair, kRepairOff));
    CHECK(isOnFlag(r.ammo, kAmmoOff));
    // Circle: while drawing, the ammoRecharge string in the table was replaced with the repair material path...
    REQUIRE(g_draws.size() == 1);
    CHECK(g_draws[0].material == "materials/rounds/repairRecharge.material");
    CHECK(g_draws[0].lea == 0x120u);  // the lea is not touched
    CHECK(g_materialTableCalls == 1);
    // ...and put back right away.
    CHECK(std::string(ammoMaterialEntry().ptr, ammoMaterialEntry().size) == kAmmoMaterial);
    CHECK(ammoMaterialEntry().capacity == strlen(kAmmoMaterial));
}

TEST_CASE_FIXTURE(Engine, "HEMTT Repair: switching on again does not switch ammo on a second time") {
    Repair r;
    turnOn(r.ammo, kAmmoOff);
    hookRepairOn(r.repair.p());
    CHECK(callNames() == std::vector<std::string>{"repair.on"});
}

TEST_CASE_FIXTURE(Engine, "HEMTT Repair: leaving F4 switches ammo off too") {
    Repair r;
    hookRepairOn(r.repair.p());
    g_calls.clear();
    hookRepairOff(r.repair.p());
    CHECK(callNames() == std::vector<std::string>{"repair.off", "ammo.off"});

    // Ammo is already off - only repair.
    g_calls.clear();
    hookRepairOff(r.repair.p());
    CHECK(callNames() == std::vector<std::string>{"repair.off"});
}

TEST_CASE_FIXTURE(Engine, "The game's own repair update is not run at all") {
    // In the game it is unfinished: it searches for targets and throws the result away.
    Repair r;
    hookRepairUpdate(r.repair.p(), 0.5f);
    CHECK(g_calls.empty());
}

TEST_CASE_FIXTURE(Engine, "Leaving F4 resets the repair timer and debt") {
    Repair r;
    g_repairTimers[station(r.ammo.p())] = 0.3f;
    g_armorDebt[station(r.ammo.p())] = 0.5f;
    hookAmmoOff(r.ammo.p());
    CHECK(g_repairTimers.count(station(r.ammo.p())) == 0);
    CHECK(g_armorDebt.count(station(r.ammo.p())) == 0);
}

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

namespace {

// One repair pulse of vehicle target paid from stock `stock` (debt - debt).
Repaired pulse(Facet& target, int& stock, float& debt) { return repairVehicle(target.p(), Stock{stock, debt}); }

}  // namespace

TEST_CASE_FIXTURE(Engine, "Module repair: +2% of max per pulse, through the engine's incHealth") {
    // Setup: a module at 50 of 100, 1000 in stock.
    Damaged d;
    int stock = 1000;
    float debt = 0;

    Repaired r = pulse(d.modules, stock, debt);

    // Expected: +2 health (2% of 100), via incHealth of module #0; 2 spare parts paid.
    REQUIRE(g_incHealth.size() == 1);
    CHECK(g_incHealth[0].modules == d.modules.p());
    CHECK(g_incHealth[0].index == 0u);
    CHECK(g_incHealth[0].amount == 2);
    CHECK(moduleHp(d.modules, 0) == 52);
    CHECK(r.health == 2);
    CHECK(r.spent == 2);
    CHECK(stock == 998);
    // The vehicle's total health was recalculated by the engine (incHealth).
    CHECK(totalHp(d.modules) == 52);
}

TEST_CASE_FIXTURE(Engine, "Module repair: the repaired vehicle's own spare parts are not spent") {
    // incHealth is crew repair, it takes spare parts from the vehicle itself. HEMTT Repair pays, so the plugin
    // gives them back to the target.
    Damaged d;  // the target has 7 spare parts
    int stock = 1000;
    float debt = 0;
    pulse(d.modules, stock, debt);
    CHECK(sparesIn(d.modules) == 7);
}

TEST_CASE_FIXTURE(Engine, "Module repair: at least 1, no more than missing, no more than in stock") {
    // Each vehicle also has an intact module: a vehicle with total health 0 counts as destroyed.
    const ModuleSpec intact{100, 100};
    int stock = 1000;
    float debt = 0;
    // 2% of 30 = 0.6 -> rounds down to 0, but it is never less than 1.
    Damaged small(1, {ModuleSpec{0, 30}, intact});
    pulse(small.modules, stock, debt);
    CHECK(moduleHp(small.modules, 0) == 1);
    // 1 short of the maximum - 1 is added, not 2.
    Damaged almost(1, {ModuleSpec{99, 100}, intact});
    pulse(almost.modules, stock, debt);
    CHECK(moduleHp(almost.modules, 0) == 100);
    // 2% of 1000 = 20, but only 3 in stock - 3 is added.
    Damaged big(1, {ModuleSpec{0, 1000}, intact});
    int small3 = 3;
    pulse(big.modules, small3, debt);
    CHECK(moduleHp(big.modules, 0) == 3);
    CHECK(small3 == 0);
}

TEST_CASE_FIXTURE(Engine, "Repair: intact modules are left alone, all damaged ones are repaired in one pulse") {
    Damaged d(1, {ModuleSpec{100, 100}, ModuleSpec{10, 100}, ModuleSpec{20, 50}});
    int stock = 1000;
    float debt = 0;
    Repaired r = pulse(d.modules, stock, debt);
    REQUIRE(g_incHealth.size() == 2);  // module #0 is intact
    CHECK(g_incHealth[0].index == 1u);
    CHECK(g_incHealth[1].index == 2u);
    CHECK(moduleHp(d.modules, 1) == 12);  // +2% of 100
    CHECK(moduleHp(d.modules, 2) == 21);  // +2% of 50 = 1
    CHECK(r.health == 3);
}

TEST_CASE_FIXTURE(Engine, "Armor: every worn layer gets +2% of its original thickness, 1 spare part per 1 mm") {
    // Setup: the module is intact; side 0 - layers of 50 and 100 mm with originals of 100 and 100; side 5 -
    // a layer of 99 of 100 mm.
    ModuleSpec m;
    m.armor[0] = {50, 100};
    m.armorFull[0] = {100, 100};
    m.armor[5] = {99};
    m.armorFull[5] = {100};
    Damaged d(1, {m});
    int stock = 1000;
    float debt = 0;

    Repaired r = pulse(d.modules, stock, debt);

    // Expected: 50 -> 52 (+2% of 100); the intact layer untouched; 99 -> 100 (not above the original).
    CHECK(armorAt(d.modules, 0, 0, 0) == 52.0f);
    CHECK(armorAt(d.modules, 0, 0, 1) == 100.0f);
    CHECK(armorAt(d.modules, 0, 5, 0) == 100.0f);
    CHECK(r.layers == 2);
    // Price: 2 mm + 1 mm = 3 spare parts.
    CHECK(r.spent == 3);
    CHECK(stock == 997);
    CHECK(g_incHealth.empty());  // the module's health is full
}

TEST_CASE_FIXTURE(Engine, "Armor: a fractional price accumulates as debt") {
    // A layer with an original thickness of 25 mm: +0.5 mm per pulse - 0.5 spare parts.
    ModuleSpec m;
    m.armor[0] = {10};
    m.armorFull[0] = {25};
    Damaged d(1, {m});
    int stock = 100;
    float debt = 0;
    Repaired r = pulse(d.modules, stock, debt);
    CHECK(armorAt(d.modules, 0, 0, 0) == 10.5f);
    CHECK(r.spent == 0);  // nothing taken yet
    CHECK(debt == doctest::Approx(0.5f));
    r = pulse(d.modules, stock, debt);
    CHECK(r.spent == 1);  // second pulse: 0.5 + 0.5 = 1 spare part
    CHECK(stock == 99);
}

TEST_CASE_FIXTURE(Engine, "Armor: layers with zero original thickness and extra current layers are skipped") {
    // Side 0: the current thickness has 3 layers, but the module description has 2 (the second with thickness
    // 0). Only layers present in both lists are used; a layer with original thickness 0 is not repaired.
    ModuleSpec m;
    m.armor[0] = {10, 0, 10};
    m.armorFull[0] = {100, 0};
    Damaged d(1, {m});
    int stock = 100;
    float debt = 0;
    Repaired r = pulse(d.modules, stock, debt);
    CHECK(armorAt(d.modules, 0, 0, 0) == 12.0f);
    CHECK(armorAt(d.modules, 0, 0, 1) == 0.0f);
    CHECK(armorAt(d.modules, 0, 0, 2) == 10.0f);  // the third layer is not in the description - untouched
    CHECK(r.layers == 1);
}

TEST_CASE_FIXTURE(Engine, "A destroyed vehicle (total health 0) is not repaired") {
    ModuleSpec m{0, 100};
    m.armor[0] = {10};
    m.armorFull[0] = {100};
    Damaged d(1, {m});
    REQUIRE(totalHp(d.modules) == 0);
    int stock = 100;
    float debt = 0;
    Repaired r = pulse(d.modules, stock, debt);
    CHECK(g_incHealth.empty());
    CHECK(armorAt(d.modules, 0, 0, 0) == 10.0f);
    CHECK(r.health == 0);
    CHECK(stock == 100);
}

TEST_CASE_FIXTURE(Engine, "A module without a description is skipped, the next one is repaired") {
    ModuleSpec broken{10, 100};
    broken.noDesc = true;
    Damaged d(1, {broken, ModuleSpec{10, 100}});
    int stock = 100;
    float debt = 0;
    pulse(d.modules, stock, debt);
    REQUIRE(g_incHealth.size() == 1);
    CHECK(g_incHealth[0].index == 1u);
}

TEST_CASE_FIXTURE(Engine, "Stock ran out - the following modules are not repaired") {
    Damaged d(1, {ModuleSpec{10, 100}, ModuleSpec{10, 100}});
    int stock = 2;
    float debt = 0;
    pulse(d.modules, stock, debt);
    CHECK(moduleHp(d.modules, 0) == 12);
    CHECK(moduleHp(d.modules, 1) == 10);
    CHECK(stock == 0);
}

TEST_CASE_FIXTURE(Engine, "Stock ran out in the middle of a side - that side's armor is finished on debt") {
    // QUIRK: stock is checked before each armor side, not before each layer. So the layers of a side that was
    // started are all repaired, and the missing price goes into debt.
    ModuleSpec m;
    m.armor[0] = {50, 50, 50};
    m.armorFull[0] = {100, 100, 100};
    m.armor[1] = {50};
    m.armorFull[1] = {100};
    Damaged d(1, {m});
    int stock = 1;
    float debt = 0;
    Repaired r = pulse(d.modules, stock, debt);
    // All 3 layers of side 0 grew by 2 mm (price 6), 1 spare part paid, 5 in debt.
    for (size_t k = 0; k < 3; ++k) CHECK(armorAt(d.modules, 0, 0, k) == 52.0f);
    CHECK(r.layers == 3);
    CHECK(r.spent == 1);
    CHECK(stock == 0);
    CHECK(debt == doctest::Approx(5.0f));
    // Side 1 is no longer touched.
    CHECK(armorAt(d.modules, 0, 1, 0) == 50.0f);
}

// ================================================================ repair pulse over the zone

namespace {

// HEMTT Repair in F4 mode with vehicles in the zone.
struct RepairZone : Engine {
    Repair r;
    RepairZone() { turnOn(r.ammo, kAmmoOff); }
    void zone(std::vector<RefBlock*> list) { r.v.setTargets(r.ammo, std::move(list)); }
    void tick(float dt) { tickRepair(r.ammo.p(), dt); }
};

}  // namespace

TEST_CASE_FIXTURE(RepairZone, "A repair pulse every 0.5 seconds") {
    Damaged d;
    zone({&d.v.self});

    tick(0.25f);  // first frame - a pulse right away
    CHECK(moduleHp(d.modules, 0) == 52);
    tick(0.25f);  // 0.25 s later - too early
    CHECK(moduleHp(d.modules, 0) == 52);
    tick(0.25f);  // 0.5 s later - the next pulse
    CHECK(moduleHp(d.modules, 0) == 54);
}

TEST_CASE_FIXTURE(RepairZone, "Repair is paid with HEMTT Repair's spare parts") {
    Damaged d;
    zone({&d.v.self});
    tick(0.1f);
    CHECK(sparesIn(r.modules) == 10000 - 2);
    CHECK(sparesIn(d.modules) == 7);
}

TEST_CASE_FIXTURE(RepairZone, "Repair: own side and allies - yes, enemies (relation 2 or 4) - no") {
    // Setup: HEMTT Repair is team 1. In the zone are vehicles of teams 1, 2, 3, 4, 5; teams 3 and 4 relate to
    // team 1 as "enemies" (2 and 4), the others as 0, 1, 3.
    Damaged own(1), team2(2), enemyA(3), enemyB(4), team5(5);
    g_relations[{1, 1}] = 0;
    g_relations[{2, 1}] = 1;
    g_relations[{3, 1}] = 2;
    g_relations[{4, 1}] = 4;
    g_relations[{5, 1}] = 3;
    zone({&own.v.self, &team2.v.self, &enemyA.v.self, &enemyB.v.self, &team5.v.self});

    tick(0.1f);

    CHECK(moduleHp(own.modules, 0) == 52);
    CHECK(moduleHp(team2.modules, 0) == 52);
    CHECK(moduleHp(enemyA.modules, 0) == 50);
    CHECK(moduleHp(enemyB.modules, 0) == 50);
    CHECK(moduleHp(team5.modules, 0) == 52);
    // Diplomacy was asked about (target team, HEMTT Repair's team), from HEMTT Repair's own world.
    CHECK(g_relationAsks.front() == std::pair<int, int>{1, 1});
    CHECK(g_relationAsks[2] == std::pair<int, int>{3, 1});
    CHECK(g_diplomacyWorld == &g_world);
}

TEST_CASE_FIXTURE(RepairZone, "Repair: HEMTT Repair itself, vehicles without a team and without modules are skipped") {
    // Setup: in the zone are HEMTT Repair itself (damaged), a vehicle without a team, an infantryman (no
    // modules), an empty entry and an ordinary damaged vehicle.
    moduleHp(r.modules, 0) = 10;
    Damaged noTeam;
    noTeam.v.setTeam(nullptr);
    Vehicle infantry;
    RefBlock empty = makeBlock(nullptr);
    Damaged ok;
    zone({&r.v.self, &noTeam.v.self, &infantry.self, nullptr, &empty, &ok.v.self});

    tick(0.1f);

    CHECK(moduleHp(r.modules, 0) == 10);       // does not repair itself
    CHECK(moduleHp(noTeam.modules, 0) == 50);  // no team - not allowed
    CHECK(moduleHp(ok.modules, 0) == 52);      // this does not stop the others
}

TEST_CASE_FIXTURE(RepairZone, "Repair: HEMTT Repair has no team or the world has no diplomacy - repairs nobody") {
    Damaged d;
    zone({&d.v.self});
    g_hasDiplomacy = false;
    tick(0.1f);
    CHECK(moduleHp(d.modules, 0) == 50);

    g_hasDiplomacy = true;
    r.v.setTeam(nullptr);
    g_repairTimers.clear();
    tick(0.1f);
    CHECK(moduleHp(d.modules, 0) == 50);
}

TEST_CASE_FIXTURE(RepairZone, "Repair: empty stock - nothing happens") {
    Damaged d;
    zone({&d.v.self});
    sparesIn(r.modules) = 0;
    tick(0.1f);
    CHECK(moduleHp(d.modules, 0) == 50);
    CHECK(g_incHealth.empty());
}

TEST_CASE_FIXTURE(RepairZone, "Repair: stock ran out during a pulse - the rest wait, and the log says so") {
    Damaged a, b;
    zone({&a.v.self, &b.v.self});
    sparesIn(r.modules) = 2;
    LogTail log(logFile());
    tick(0.1f);
    CHECK(moduleHp(a.modules, 0) == 52);
    CHECK(moduleHp(b.modules, 0) == 50);
    CHECK(sparesIn(r.modules) == 0);
    CHECK(log.has("repair: out of spare parts"));
}

TEST_CASE_FIXTURE(Engine, "Repair: each HEMTT Repair has its own armor debt, and it survives between pulses") {
    // Two HEMTT Repairs each repair a 25 mm layer (+0.5 mm = 0.5 spare parts per pulse).
    ModuleSpec m;
    m.armor[0] = {10};
    m.armorFull[0] = {25};
    Repair r1, r2;
    Damaged d1(1, {m}), d2(1, {m});
    turnOn(r1.ammo, kAmmoOff);
    turnOn(r2.ammo, kAmmoOff);
    r1.v.setTargets(r1.ammo, {&d1.v.self});
    r2.v.setTargets(r2.ammo, {&d2.v.self});

    tickRepair(r1.ammo.p(), 0.5f);  // r1: debt 0.5
    tickRepair(r2.ammo.p(), 0.5f);  // r2: debt 0.5
    CHECK(sparesIn(r1.modules) == 10000);
    tickRepair(r1.ammo.p(), 0.5f);  // r1: 0.5 + 0.5 = 1 -> 1 taken
    CHECK(sparesIn(r1.modules) == 9999);
    CHECK(sparesIn(r2.modules) == 10000);  // r2 has its own debt, still 0.5
    CHECK(g_armorDebt[station(r2.ammo.p())] == doctest::Approx(0.5f));
}

TEST_CASE_FIXTURE(RepairZone, "Repair through F4 mode: HEMTT Repair's ammo update makes the pulse") {
    Damaged d;
    zone({&d.v.self});
    circleField(r.ammo) = newCircle();
    hookAmmoUpdate(r.ammo.p(), 0.1f);
    CHECK(callNames() == std::vector<std::string>{"ammo.update"});  // the original goes first
    CHECK(moduleHp(d.modules, 0) == 52);
    REQUIRE(g_draws.size() == 1);  // and the repair circle
    CHECK(g_draws[0].material == "materials/rounds/repairRecharge.material");
}

// ================================================================ log

TEST_CASE_FIXTURE(RepairZone, "Repair log: what was done and what it cost; at most 40 lines") {
    Damaged d(1, {ModuleSpec{0, 1000}, ModuleSpec{100, 100}});  // total health 100
    zone({&d.v.self});
    LogTail log(logFile());
    tick(0.5f);
    char line[160];
    snprintf(line, sizeof line, "repair: target=%p +20 hp, 0 armor layers, total hp 120; spent 20 spares, 9980 left",
             static_cast<void*>(&d.v));
    CHECK(log.has(line));
    for (int i = 0; i < 50; ++i) tick(0.5f);
    CHECK(log.count("repair: target=") == 40);
}

TEST_CASE_FIXTURE(Engine, "Event log: at most 50 per run") {
    Spares s;
    LogTail log(logFile());
    for (int i = 0; i < 60; ++i) trace("test event", s.ammo.p());
    CHECK(log.count("test event facet=") == 50);
}
