// Tests of the plugin installation: finding addresses in logic.dll and applying the patches.
//
// When the game loads logic.dll, the plugin (onLogicLoaded):
//   1) finds everything it needs in it (resolve) - classes by name, code locations by byte patterns, addresses
//      of game functions from call instructions inside the code found;
//   2) applies all patches as one "all or nothing" set: 4 replacements of "mov r15b,1" with nop (so mod
//      vehicles get the F4 button), the spare parts zone radius, 12 method replacements in vtables.
// The key property: if the game was updated and anything at all does not match, the plugin changes NOTHING
// (the game runs as without it) and logs exactly what was not found. This is checked here on a fake
// logic.dll (support/fake_logic.h): first intact, then spoiled one detail at a time.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "third_party/doctest.h"

#include "../plugins/spares_supply/logic_dll/common.h"
#include "../plugins/spares_supply/logic_dll/repair.h"
#include "../plugins/spares_supply/logic_dll/spares.h"
#include "support/fake_logic.h"
#include "support/log_file.h"

using fake::FakeLogic;
using namespace spares_supply;

namespace {

std::wstring logFile() {
    static bool opened = false;
    if (!opened) {
        g_log.open(GetModuleHandleW(nullptr), L"plugin_install_test");
        opened = true;
    }
    return fake::logPath(L"plugin_install_test");
}

// The plugin in its "just loaded" state: nothing found, no originals saved.
void resetPlugin() {
    g_a = Addr{};
    g_origAmmoOn = g_origAmmoOff = g_origFuelOn = g_origFuelOff = g_origSparesOn = g_origSparesOff = nullptr;
    g_origRepairOn = g_origRepairOff = nullptr;
    g_origAmmoCanRecharge = nullptr;
    g_origSparesUpdate = g_origAmmoUpdate = g_origFuelUpdate = g_origRepairUpdate = nullptr;
    g_circleWritable = false;
}

// A fresh fake logic.dll and a reset plugin; log - everything the plugin writes during this test.
struct Install {
    FakeLogic L;
    fake::LogTail log{logFile()};
    Install() { resetPlugin(); }
    void load() { onLogicLoaded(L.pe.base()); }
    uintptr_t slot(uintptr_t vt, int i) const { return *reinterpret_cast<uintptr_t*>(vt + 8 * i); }
    std::string rvaText(const char* fmt, uintptr_t a) const {
        char buf[160];
        snprintf(buf, sizeof buf, fmt, L.pe.rva(a));
        return buf;
    }
};

template <class F>
uintptr_t addr(F f) {
    return reinterpret_cast<uintptr_t>(f);
}

// The plugin refused to patch: the image did not change by a single byte, the log has the reason `reason`,
// and there is no line about a successful start.
void expectRejected(Install& t, const std::string& reason) {
    auto before = t.L.pe.snapshot();
    t.load();
    CHECK(t.L.pe.snapshot() == before);
    CHECK_MESSAGE(t.log.has(reason), "the log has no \"" << reason << "\":\n" << t.log.text());
    CHECK_FALSE(t.log.has("SparesSupply active"));
    CHECK_FALSE(g_circleWritable);
}

}  // namespace

// ================================================================ everything matches

TEST_CASE_FIXTURE(Install, "Intact logic.dll: all patches applied, the plugin is active") {
    auto before = L.pe.snapshot();

    load();

    REQUIRE(log.has("SparesSupply active"));
    // The addresses found.
    CHECK(g_a.ammoVt == L.ammoVt);
    CHECK(g_a.fuelVt == L.fuelVt);
    CHECK(g_a.sparesVt == L.sparesVt);
    CHECK(g_a.repairVt == L.repairVt);
    CHECK(g_a.modulesVt == L.modulesVt);
    CHECK(g_a.supplyRadius == L.supplyRadius);
    CHECK(g_a.showCircle == L.showCircle);
    CHECK(g_a.circleLea == L.circleLea);
    CHECK(g_a.materialTable == L.materialTable);
    CHECK(g_a.incHealth == L.incHealth);
    CHECK(g_a.diplomacy == L.diplomacy);

    // 1) "mov r15b,1" -> nop in 4 places (support and repair, cars and tanks). The order is by address:
    //    the car builder comes first, the tank builder second.
    for (uintptr_t site : {L.supportSite[0], L.supportSite[1], L.repairSite[0], L.repairSite[1]})
        CHECK(Image::matches(site, "90 90 90 8B C3 FF C3"));
    // The same flag for another component (refuel) is untouched.
    CHECK(Image::matches(L.refuelSite, "41 B7 01"));

    // 2) Radius: the same instruction (movss xmm2), but it now reads the global supplyRadius, not the constant 20.
    CHECK(Image::matches(L.radiusLoad, "F3 0F 10 15"));
    CHECK(Image::ripTarget(L.radiusLoad, 4, 8) == L.supplyRadius);

    // 3) 12 vtable methods replaced with hooks; the originals saved.
    CHECK(slot(L.ammoVt, kSlotActivate) == addr(&hookAmmoOn));
    CHECK(slot(L.ammoVt, kSlotDeactivate) == addr(&hookAmmoOff));
    CHECK(slot(L.ammoVt, kSlotUpdate) == addr(&hookAmmoUpdate));
    CHECK(slot(L.ammoVt, kSlotCanRecharge) == addr(&hookAmmoCanRecharge));
    CHECK(slot(L.fuelVt, kSlotActivate) == addr(&hookFuelOn));
    CHECK(slot(L.fuelVt, kSlotDeactivate) == addr(&hookFuelOff));
    CHECK(slot(L.fuelVt, kSlotUpdate) == addr(&hookFuelUpdate));
    CHECK(slot(L.sparesVt, kSlotUpdate) == addr(&hookSparesUpdate));
    CHECK(slot(L.sparesVt, kSlotDeactivate) == addr(&hookSparesOff));
    CHECK(slot(L.repairVt, kSlotActivate) == addr(&hookRepairOn));
    CHECK(slot(L.repairVt, kSlotDeactivate) == addr(&hookRepairOff));
    CHECK(slot(L.repairVt, kSlotUpdate) == addr(&hookRepairUpdate));
    CHECK(addr(g_origAmmoOn) == L.ammoOn);
    CHECK(addr(g_origAmmoOff) == L.ammoOff);
    CHECK(addr(g_origAmmoUpdate) == L.ammoUpdate);
    CHECK(addr(g_origAmmoCanRecharge) == L.ammoCan);
    CHECK(addr(g_origFuelOn) == L.fuelOn);
    CHECK(addr(g_origFuelOff) == L.fuelOff);
    CHECK(addr(g_origFuelUpdate) == L.fuelUpdate);
    CHECK(addr(g_origSparesUpdate) == L.sparesUpdate);
    CHECK(addr(g_origSparesOff) == L.sparesOff);
    CHECK(addr(g_origRepairOn) == L.repairOn);
    CHECK(addr(g_origRepairOff) == L.repairOff);
    CHECK(addr(g_origRepairUpdate) == L.repairUpdate);
    // Switching spare parts on is not hooked: the slot is unchanged, the original is saved.
    CHECK(slot(L.sparesVt, kSlotActivate) == L.sparesOn);
    CHECK(addr(g_origSparesOn) == L.sparesOn);

    // 4) The circle material instruction is unchanged (the hook changes it while drawing), but it was made
    //    writable.
    CHECK(Image::matches(L.circleLea, "4C 8D 80 20 01 00 00"));
    CHECK(g_circleWritable);

    // Apart from the above, nothing in the image changed.
    std::vector<std::pair<uintptr_t, size_t>> allowed = {
        {L.supportSite[0], 3}, {L.supportSite[1], 3}, {L.repairSite[0], 3}, {L.repairSite[1], 3},
        {L.radiusLoad + 4, 4}};
    for (auto [vt, slots] : std::vector<std::pair<uintptr_t, std::vector<int>>>{
             {L.ammoVt, {kSlotActivate, kSlotDeactivate, kSlotUpdate, kSlotCanRecharge}},
             {L.fuelVt, {kSlotActivate, kSlotDeactivate, kSlotUpdate}},
             {L.sparesVt, {kSlotUpdate, kSlotDeactivate}},
             {L.repairVt, {kSlotActivate, kSlotDeactivate, kSlotUpdate}}})
        for (int s : slots) allowed.push_back({vt + 8 * s, 8});
    auto after = L.pe.snapshot();
    int unexpected = 0;
    for (size_t i = 0; i < after.size(); ++i) {
        if (after[i] == before[i]) continue;
        uintptr_t a = L.pe.base() + i;
        bool ok = false;
        for (auto [at, n] : allowed) ok = ok || (a >= at && a < at + n);
        if (!ok) ++unexpected;
    }
    CHECK(unexpected == 0);
}

TEST_CASE_FIXTURE(Install, "Intact logic.dll: the log lists what was found and every patch") {
    load();
    char buf[200];
    snprintf(buf, sizeof buf, "logic.dll at %p (TimeDateStamp %08X, SizeOfImage %08X)",
             reinterpret_cast<void*>(L.pe.base()), fake::Pe::kTimeDateStamp, fake::Pe::kImageSize);
    CHECK(log.has(buf));
    snprintf(buf, sizeof buf, "vtables: ammo %08X fuel %08X spares %08X repair %08X modules %08X",
             L.pe.rva(L.ammoVt), L.pe.rva(L.fuelVt), L.pe.rva(L.sparesVt), L.pe.rva(L.repairVt),
             L.pe.rva(L.modulesVt));
    CHECK(log.has(buf));
    snprintf(buf, sizeof buf, "functions: incHealth %08X diplomacy %08X material table %08X",
             L.pe.rva(L.incHealth), L.pe.rva(L.diplomacy), L.pe.rva(L.materialTable));
    CHECK(log.has(buf));
    // 18 patches: 4 nops, the radius, 12 methods and the circle instruction check.
    CHECK(log.count("patched: ") == 18);
    CHECK(log.has(rvaText("patched: cars: support no longer blocks ammunition_recharge (RVA %08X)",
                          L.supportSite[0])));
    CHECK(log.has(rvaText("patched: tanks: repair no longer blocks ammunition_recharge (RVA %08X)",
                          L.repairSite[1])));
    CHECK(log.has(rvaText("patched: spares search radius 20 m -> supplyRadius (RVA %08X)", L.radiusLoad)));
    CHECK(log.has(rvaText("patched: supply circle material lea (verify only) (RVA %08X)", L.circleLea)));
    CHECK(log.has(rvaText("patched: RepairerFacet::update (disabled) (RVA %08X)", L.repairVt + 8 * kSlotUpdate)));
}

// ================================================================ something does not match - change nothing

TEST_CASE("One of the five classes is missing - the plugin applies nothing") {
    for (int which = 0; which < 5; ++which) {
        CAPTURE(which);
        Install t;
        uintptr_t names[5] = {t.L.ammoName, t.L.fuelName, t.L.sparesName, t.L.repairName, t.L.modulesName};
        fake::Pe::write(names[which], {'X'});  // the class name no longer matches
        expectRejected(t, "this logic.dll build is not supported - nothing patched");
    }
}

TEST_CASE("A pattern that must be found exactly once was not found or was found twice") {
    struct Sig {
        const char* name;
        uintptr_t FakeLogic::*at;
        const char* pattern;
    };
    const Sig sigs[] = {
        {"spares radius load", &FakeLogic::radiusLoad, kSigSparesRadiusLoad},
        {"supplyRadius use", &FakeLogic::supplyRadiusUse, kSigSupplyRadiusUse},
        {"ammo show circle", &FakeLogic::showCircle, kSigShowCircle},
        {"circle material lea", &FakeLogic::circleLea, kSigCircleMaterialLea},
        {"incHealth call", &FakeLogic::incHealthCall, kSigIncHealthCall},
    };
    for (const Sig& s : sigs) {
        CAPTURE(s.name);
        {  // none at all
            Install t;
            FakeLogic::spoil(t.L.*s.at);
            expectRejected(t, std::string("signature '") + s.name + "': 0 matches (need 1)");
        }
        {  // two identical places - unclear which one to patch
            Install t;
            t.L.duplicate(s.pattern);
            expectRejected(t, std::string("signature '") + s.name + "': 2 matches (need 1)");
        }
    }
}

TEST_CASE("\"support blocks ammo\": exactly 2 places are needed (cars and tanks)") {
    {
        Install t;
        FakeLogic::spoil(t.L.supportSite[1]);
        expectRejected(t, "signature 'support blocks ammo': 1 matches (need 2)");
    }
    {
        Install t;
        t.L.duplicate(kSigSupportBlocksAmmo);
        expectRejected(t, "signature 'support blocks ammo': 3 matches (need 2)");
    }
}

TEST_CASE("\"repair blocks ammo\": exactly 2 places referring to the RepairerFacet name are needed") {
    {
        Install t;
        FakeLogic::spoil(t.L.repairSite[0]);
        expectRejected(t, "signature 'repair blocks ammo': 1 matches (need 2)");
    }
    {  // no string with the component name - no place found at all
        Install t;
        fake::Pe::write(t.L.repairString, {'X'});
        expectRejected(t, "signature 'repair blocks ammo': 0 matches (need 2)");
    }
    {  // a third place with the same name
        Install t;
        uintptr_t extra = t.L.duplicate(kSigAbilityFlag);
        fake::Pe::aim(extra + 7, 3, 7, t.L.repairString);
        expectRejected(t, "signature 'repair blocks ammo': 3 matches (need 2)");
    }
}

namespace {

// Remove the material instruction from its place (+0x77) and put its only copy (with the material table call
// before it) `distance` bytes past the start of the circle function. Returns the copy's address.
uintptr_t moveCircleLea(FakeLogic& L, size_t distance) {
    FakeLogic::spoil(L.circleLea);
    uintptr_t at = L.showCircle + distance;
    fake::Pe::write(at, fake::hex(kSigCircleMaterialLea));
    FakeLogic::putCall(at - kMaterialTableCallBack, L.materialTable);
    return at;
}

}  // namespace

TEST_CASE_FIXTURE(Install, "The circle material instruction is more than 0x100 bytes from the circle function start - refused") {
    // "Inside the function" for the plugin means no further than 0x100 bytes from its start (it is at +0x77 in
    // the GOG build).
    L.pe.text.cur = L.showCircle + 0x200;  // free up room after the function
    moveCircleLea(L, 0x180);
    expectRejected(*this, "circle material lea is not inside the circle function");
}

TEST_CASE_FIXTURE(Install, "The circle material instruction moved, but within 0x100 bytes - accepted") {
    L.pe.text.cur = L.showCircle + 0x200;
    uintptr_t at = moveCircleLea(L, 0xF0);
    load();
    CHECK(log.has("SparesSupply active"));
    CHECK(g_a.circleLea == at);
}

TEST_CASE_FIXTURE(Install, "No material table call before the material instruction") {
    fake::Pe::write(L.circleLea - kMaterialTableCallBack, {0x90});
    expectRejected(*this, "material table call not found");
}

TEST_CASE_FIXTURE(Install, "incHealth does not start as expected") {
    FakeLogic::spoil(L.incHealth);
    expectRejected(*this, "incHealth: unexpected prologue");
}

TEST_CASE("The diplomacy call was not found in canRecharge") {
    {  // the start of canRecharge changed
        Install t;
        FakeLogic::spoil(t.L.ammoCan);
        expectRejected(t, "diplomacy call in canRecharge not found");
    }
    {  // the wrong code at +0x3a
        Install t;
        FakeLogic::spoil(t.L.ammoCan + kDiplomacyCallAt);
        expectRejected(t, "diplomacy call in canRecharge not found");
    }
}

TEST_CASE("A hooked method does not start as expected") {
    // The function's start confirms it is the same function and the object's fields are where the plugin thinks.
    struct Method {
        const char* name;
        uintptr_t FakeLogic::*fn;
    };
    const Method methods[] = {
        {"AmmoRechargerFacet::activate", &FakeLogic::ammoOn},
        {"AmmoRechargerFacet::deactivate", &FakeLogic::ammoOff},
        {"AmmoRechargerFacet::update", &FakeLogic::ammoUpdate},
        {"RefuellerFacet::activate", &FakeLogic::fuelOn},
        {"RefuellerFacet::deactivate", &FakeLogic::fuelOff},
        {"RefuellerFacet::update", &FakeLogic::fuelUpdate},
        {"SparesRechargerFacet::update", &FakeLogic::sparesUpdate},
        {"SparesRechargerFacet::deactivate", &FakeLogic::sparesOff},
        {"RepairerFacet::activate", &FakeLogic::repairOn},
        {"RepairerFacet::deactivate", &FakeLogic::repairOff},
        {"RepairerFacet::update (disabled)", &FakeLogic::repairUpdate},
        // canRecharge is checked earlier - when looking for the diplomacy call (test above).
    };
    for (const Method& m : methods) {
        CAPTURE(m.name);
        Install t;
        FakeLogic::spoil(t.L.*m.fn);
        expectRejected(t, std::string("verify FAILED: ") + m.name + " at RVA");
        CHECK(addr(g_origAmmoOn) == 0);  // originals not saved - no hooks installed
    }
}

TEST_CASE_FIXTURE(Install, "Switching spare parts on does not start as expected") {
    // The plugin does not hook it but calls it itself - so it checks it too.
    FakeLogic::spoil(L.sparesOn);
    expectRejected(*this, "verify FAILED: SparesRechargerFacet::activate - nothing patched");
}

TEST_CASE_FIXTURE(Install, "A second search without a reset fails") {
    // QUIRK: resolve is meant to be called once per run - the "repair blocks ammo" places found are appended to
    // those already found, so the second call ends up with 4. In the game logic.dll is loaded once, and
    // DllWatcher runs the installation once too.
    Image img(L.pe.base());
    REQUIRE(resolve(img));
    CHECK_FALSE(resolve(img));
    CHECK(g_a.repairBlocksAmmo.size() == 4);
}
