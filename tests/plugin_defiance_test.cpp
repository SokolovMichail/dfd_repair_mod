// Tests of the plugin as a DefianceLoader plugin (spares_repair.dll, logic_dll/defiance.cpp).
//
// Under DefianceLoader the plugin does not write to logic.dll itself. The loader calls its init() once
// logic.dll is loaded; init finds the same places as the standalone DLL and hands every write to the loader
// (patch_bytes). The loader makes them later, together with the other plugins' writes, after checking that
// nobody else writes the same bytes. init also declares the same list as its "patch contract", so the loader
// can check it got exactly what was declared.
//
// Here the loader is a fake (FakeLoader below): it records the writes init asks for and makes none of them,
// unless a test "commits" them. logic.dll is the same fake image as in plugin_install_test
// (support/fake_logic.h). The checks: the plugin asks for exactly the standalone set of writes, writes nothing
// itself, refuses cleanly when something does not match, and handles the two conflicts the loader can
// bring: the standalone SparesSupply.dll installed as well, and the loader's own supply-radius plugin.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "third_party/doctest.h"

#include <fstream>
#include <map>
#include <sstream>
#include <string>

#include "../plugins/spares_supply/logic_dll/common.h"
#include "../plugins/spares_supply/logic_dll/repair.h"
#include "../plugins/spares_supply/logic_dll/spares.h"
#include "../third_party/defiance/defiance.h"
#include "support/fake_logic.h"

// The two exports of logic_dll/defiance.cpp - what the loader looks for in the DLL.
extern "C" const DefiancePlugin* defiance_plugin();
extern "C" const DefiancePatchContractV1* defiance_patch_contract_v1(const DefianceApi* api);

using fake::FakeLogic;
using namespace spares_supply;

namespace {

// One write the plugin asked the loader for: at address `at`, the bytes `before` should become `after`.
struct Request {
    uintptr_t at;
    std::vector<uint8_t> before, after;
};

// A fake DefianceLoader: the API table the plugin's init gets. It answers only what the plugin uses: the log,
// where logic.dll is, settings, and byte writes - which it records instead of making.
struct FakeLoader {
    static FakeLoader* current;  // the API is plain C functions, so they find the loader through this

    DefianceApi api{};
    uintptr_t logic = 0;                         // what module_base("logic.dll") answers; 0 - not loaded
    std::map<std::string, std::string> config;   // "plugin.id/key" -> value, as the loader validated it
    std::vector<std::string> lines;              // the plugin's log lines
    std::vector<Request> staged;                 // the writes the plugin asked for, in order
    int refuseAt = -1;                           // patch_bytes call number to refuse (-1 - accept all)

    FakeLoader() {
        current = this;
        api.abi_version = DEFIANCE_ABI_VERSION;
        api.log = &log;
        api.module_base = &moduleBase;
        api.config_get = &configGet;
        api.patch_bytes = &patchBytes;
    }
    ~FakeLoader() { current = nullptr; }

    // What the real loader does after init succeeds: make every staged write.
    void commit() const {
        for (auto& r : staged) memcpy(reinterpret_cast<void*>(r.at), r.after.data(), r.after.size());
    }
    // A staged write at address at; nullptr - none.
    const Request* at(uintptr_t a) const {
        for (auto& r : staged)
            if (r.at == a) return &r;
        return nullptr;
    }
    bool logged(const std::string& part) const {
        for (auto& l : lines)
            if (l.find(part) != std::string::npos) return true;
        return false;
    }
    std::string text() const {
        std::string all;
        for (auto& l : lines) all += l + "\n";
        return all;
    }

private:
    static void log(uint32_t, const char* message) { current->lines.emplace_back(message); }
    static void* moduleBase(const char* name) {
        return _stricmp(name, "logic.dll") == 0 ? reinterpret_cast<void*>(current->logic) : nullptr;
    }
    static const char* configGet(const char* id, const char* key) {
        auto it = current->config.find(std::string(id) + "/" + key);
        return it == current->config.end() ? nullptr : it->second.c_str();
    }
    static int32_t patchBytes(void* target, const uint8_t* before, const uint8_t* after, size_t length) {
        if (static_cast<int>(current->staged.size()) == current->refuseAt) return 1;
        current->staged.push_back({reinterpret_cast<uintptr_t>(target), {before, before + length},
                                   {after, after + length}});
        return 0;
    }
};
FakeLoader* FakeLoader::current = nullptr;

template <class F>
uintptr_t addr(F f) {
    return reinterpret_cast<uintptr_t>(f);
}

// 8 bytes of a pointer, as a vtable slot holds it.
std::vector<uint8_t> pointerBytes(uintptr_t p) {
    std::vector<uint8_t> b(8);
    memcpy(b.data(), &p, 8);
    return b;
}

// The plugin in its "just loaded" state, and a fresh fake logic.dll and loader.
struct Plugin {
    FakeLogic L;
    FakeLoader loader;

    Plugin() {
        g_a = Addr{};
        g_origAmmoOn = g_origAmmoOff = g_origFuelOn = g_origFuelOff = g_origSparesOn = g_origSparesOff = nullptr;
        g_origRepairOn = g_origRepairOff = nullptr;
        g_origAmmoCanRecharge = nullptr;
        g_origSparesUpdate = g_origAmmoUpdate = g_origFuelUpdate = g_origRepairUpdate = nullptr;
        g_circleWritable = g_circleDeferred = false;
        g_sparesRadius = g_logEvents = true;
        g_events = 0;
        loader.logic = L.pe.base();
    }
    ~Plugin() { g_log.sendTo(nullptr); }  // the next test's log goes nowhere until its init

    int32_t init() { return defiance_plugin()->init(&loader.api); }
    uintptr_t slotAt(uintptr_t vt, int i) const { return vt + 8u * i; }
};

// The init refused: it returned non-zero, asked for no write, changed nothing in the image, declared no
// contract, and the log has the reason.
void expectRefused(Plugin& t, const std::string& reason) {
    auto before = t.L.pe.snapshot();
    CHECK(t.init() != 0);
    CHECK(t.L.pe.snapshot() == before);
    CHECK(t.loader.staged.empty());
    CHECK(defiance_patch_contract_v1(&t.loader.api) == nullptr);
    CHECK_MESSAGE(t.loader.logged(reason), "the log has no \"" << reason << "\":\n" << t.loader.text());
    CHECK_FALSE(t.loader.logged("SparesSupply staged"));
}

std::string readFile(const char* path) {
    std::ifstream f(path);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

// A function in the place of the zone circle function: counts its calls.
int g_circleCalls = 0;
void fakeShowCircle(void*) { ++g_circleCalls; }

}  // namespace

// ================================================================ the plugin's identity

TEST_CASE("The exported plugin matches its manifest: ABI 5, the same ID and version") {
    const DefiancePlugin* p = defiance_plugin();
    REQUIRE(p != nullptr);
    CHECK(p->abi_version == 5u);
    CHECK(std::string(p->name) == "sokolov.spares-repair");
    REQUIRE(p->init != nullptr);
    REQUIRE(p->stop != nullptr);

    // The loader refuses a DLL whose ID, version or file name differ from its manifest.
    std::string manifest = readFile(SPARES_REPAIR_MANIFEST);
    REQUIRE_FALSE(manifest.empty());
    CHECK(manifest.find("\"id\": \"sokolov.spares-repair\"") != std::string::npos);
    CHECK(manifest.find("\"dll\": \"spares_repair.dll\"") != std::string::npos);
    CHECK(manifest.find("\"version\": \"" + std::string(p->version) + "\"") != std::string::npos);
    CHECK(manifest.find("\"abi\": 5") != std::string::npos);
    CHECK(manifest.find("\"patch_contract\": true") != std::string::npos);
    CHECK(manifest.find("\"hot_reload\": false") != std::string::npos);
}

// ================================================================ everything matches

TEST_CASE_FIXTURE(Plugin, "Intact logic.dll: init asks for the standalone set of writes and makes none itself") {
    auto before = L.pe.snapshot();

    REQUIRE(init() == 0);

    // Nothing written by the plugin: the loader writes, after init.
    CHECK(L.pe.snapshot() == before);
    CHECK(loader.logged("SparesSupply staged"));

    // 4 flags + the spare parts radius + 12 vtable slots + the circle lea (claimed, not changed) = 18.
    CHECK(loader.staged.size() == 18);
    for (auto& r : loader.staged)  // each write starts from what is there now
        CHECK(memcmp(reinterpret_cast<void*>(r.at), r.before.data(), r.before.size()) == 0);

    // 1) "mov r15b,1" -> nop in the 4 places.
    for (uintptr_t site : {L.supportSite[0], L.supportSite[1], L.repairSite[0], L.repairSite[1]}) {
        const Request* r = loader.at(site);
        REQUIRE(r != nullptr);
        CHECK(r->after == std::vector<uint8_t>{0x90, 0x90, 0x90});
    }
    CHECK(loader.at(L.refuelSite) == nullptr);

    // 2) The radius instruction, rewritten to read supplyRadius.
    REQUIRE(loader.at(L.radiusLoad) != nullptr);

    // 3) The 12 slots: from the stock function to the hook.
    struct Slot {
        uintptr_t vt;
        int index;
        uintptr_t stock, hook;
    };
    for (const Slot& s : {Slot{L.ammoVt, kSlotActivate, L.ammoOn, addr(&hookAmmoOn)},
                          Slot{L.ammoVt, kSlotDeactivate, L.ammoOff, addr(&hookAmmoOff)},
                          Slot{L.ammoVt, kSlotUpdate, L.ammoUpdate, addr(&hookAmmoUpdate)},
                          Slot{L.ammoVt, kSlotCanRecharge, L.ammoCan, addr(&hookAmmoCanRecharge)},
                          Slot{L.fuelVt, kSlotActivate, L.fuelOn, addr(&hookFuelOn)},
                          Slot{L.fuelVt, kSlotDeactivate, L.fuelOff, addr(&hookFuelOff)},
                          Slot{L.fuelVt, kSlotUpdate, L.fuelUpdate, addr(&hookFuelUpdate)},
                          Slot{L.sparesVt, kSlotUpdate, L.sparesUpdate, addr(&hookSparesUpdate)},
                          Slot{L.sparesVt, kSlotDeactivate, L.sparesOff, addr(&hookSparesOff)},
                          Slot{L.repairVt, kSlotActivate, L.repairOn, addr(&hookRepairOn)},
                          Slot{L.repairVt, kSlotDeactivate, L.repairOff, addr(&hookRepairOff)},
                          Slot{L.repairVt, kSlotUpdate, L.repairUpdate, addr(&hookRepairUpdate)}}) {
        const Request* r = loader.at(slotAt(s.vt, s.index));
        REQUIRE(r != nullptr);
        CHECK(r->before == pointerBytes(s.stock));
        CHECK(r->after == pointerBytes(s.hook));
    }
    // Switching spare parts on is not hooked.
    CHECK(loader.at(slotAt(L.sparesVt, kSlotActivate)) == nullptr);

    // 4) The circle lea: claimed with the same bytes, so no other plugin takes it; the plugin rewrites it
    //    itself while drawing.
    const Request* lea = loader.at(L.circleLea);
    REQUIRE(lea != nullptr);
    CHECK(lea->after == lea->before);

    // The originals are saved already, for when the hooks go live.
    CHECK(addr(g_origAmmoOn) == L.ammoOn);
    CHECK(addr(g_origAmmoUpdate) == L.ammoUpdate);
    CHECK(addr(g_origAmmoCanRecharge) == L.ammoCan);
    CHECK(addr(g_origSparesOn) == L.sparesOn);
    CHECK(addr(g_origRepairUpdate) == L.repairUpdate);
}

TEST_CASE_FIXTURE(Plugin, "The patch contract lists exactly the staged writes") {
    REQUIRE(init() == 0);
    const DefiancePatchContractV1* c = defiance_patch_contract_v1(&loader.api);
    REQUIRE(c != nullptr);
    CHECK(c->version == 1u);
    CHECK(c->size >= sizeof(DefiancePatchContractV1));
    REQUIRE(c->count == loader.staged.size());
    for (size_t i = 0; i < c->count; ++i) {
        const DefiancePatchContractEntryV1& e = c->entries[i];
        const Request& r = loader.staged[i];
        CHECK(std::string(e.module) == "logic.dll");
        CHECK(e.rva == L.pe.rva(r.at));
        CHECK(e.kind == DEFIANCE_PATCH_KIND_BYTES);
        CHECK(std::vector<uint8_t>(e.before, e.before + e.before_len) == r.before);
        CHECK(std::vector<uint8_t>(e.after, e.after + e.after_len) == r.after);
    }
}

TEST_CASE_FIXTURE(Plugin, "After the loader commits, the image is patched like the standalone one") {
    REQUIRE(init() == 0);
    loader.commit();

    for (uintptr_t site : {L.supportSite[0], L.supportSite[1], L.repairSite[0], L.repairSite[1]})
        CHECK(Image::matches(site, "90 90 90 8B C3 FF C3"));
    CHECK(Image::ripTarget(L.radiusLoad, 4, 8) == L.supplyRadius);
    CHECK(*reinterpret_cast<uintptr_t*>(slotAt(L.ammoVt, kSlotActivate)) == addr(&hookAmmoOn));
    CHECK(*reinterpret_cast<uintptr_t*>(slotAt(L.repairVt, kSlotUpdate)) == addr(&hookRepairUpdate));
    CHECK(Image::matches(L.circleLea, "4C 8D 80 20 01 00 00"));
}

TEST_CASE_FIXTURE(Plugin, "The circle lea is made writable on the first spares circle, not during init") {
    REQUIRE(init() == 0);
    // During init the loader has written nothing yet: the lea stays as it is.
    CHECK_FALSE(g_circleWritable);
    CHECK(g_circleDeferred);

    loader.commit();
    g_a.showCircle = addr(&fakeShowCircle);
    g_circleCalls = 0;
    int ammo = 0;
    CHECK(redrawSparesCircle(&ammo));

    CHECK(g_circleWritable);
    CHECK_FALSE(g_circleDeferred);
    CHECK(g_circleCalls == 1);
    // Drawn with the spare parts material, then switched back to ammo.
    CHECK(Image::matches(L.circleLea, "4C 8D 80 20 01 00 00"));
}

// ================================================================ refusals

TEST_CASE_FIXTURE(Plugin, "A loader with another ABI: init refuses before touching anything") {
    loader.api.abi_version = 4;
    auto before = L.pe.snapshot();
    CHECK(init() != 0);
    CHECK(L.pe.snapshot() == before);
    CHECK(loader.staged.empty());
}

TEST_CASE_FIXTURE(Plugin, "A loader API with reserved != 0: init refuses") {
    loader.api.reserved = 1;
    CHECK(init() != 0);
    CHECK(loader.staged.empty());
}

TEST_CASE_FIXTURE(Plugin, "logic.dll not loaded: init refuses") {
    loader.logic = 0;
    CHECK(init() != 0);
    CHECK(loader.staged.empty());
    CHECK(loader.logged("logic.dll is not loaded"));
}

TEST_CASE_FIXTURE(Plugin, "An unknown logic.dll build: init asks for nothing") {
    FakeLogic::spoil(L.showCircle);
    expectRefused(*this, "this logic.dll build is not supported");
}

TEST_CASE_FIXTURE(Plugin, "A function with another start in a slot: init asks for nothing") {
    FakeLogic::spoil(L.fuelUpdate);
    expectRefused(*this, "verify FAILED: RefuellerFacet::update");
}

TEST_CASE_FIXTURE(Plugin, "The loader refuses one write: init fails and declares no contract") {
    loader.refuseAt = 5;
    CHECK(init() != 0);
    CHECK(loader.logged("refused by the loader"));
    CHECK_FALSE(loader.logged("SparesSupply staged"));
    CHECK(defiance_patch_contract_v1(&loader.api) == nullptr);
}

// ================================================================ conflicts

TEST_CASE_FIXTURE(Plugin, "Standalone SparesSupply already patched the slots: init refuses and names it") {
    // The standalone DLL patches logic.dll the moment it loads, before the loader's init: the slot points into
    // another module (here - this test program) instead of logic.dll.
    fake::Pe::setSlot(L.ammoVt, kSlotActivate, addr(&hookAmmoOn));
    expectRefused(*this, "already points into plugin_defiance_test.exe");
    CHECK(loader.logged("remove bin\\plugins\\SparesSupply.dll"));
}

TEST_CASE_FIXTURE(Plugin, "supply-radius enabled: the radius is left to it, the rest is staged") {
    loader.config["defiance.supply-radius/enabled"] = "true";
    // It may have rewritten the instruction already (a plugin committed earlier): no pattern there now.
    FakeLogic::spoil(L.radiusLoad);

    REQUIRE(init() == 0);
    CHECK(loader.staged.size() == 17);
    CHECK(loader.at(L.radiusLoad) == nullptr);
    CHECK(loader.logged("defiance.supply-radius is enabled"));
    CHECK(loader.logged("spares search radius left to another mod"));
    CHECK_FALSE(loader.logged("signature 'spares radius load'"));
}

TEST_CASE_FIXTURE(Plugin, "supply-radius installed but disabled: the radius is patched as usual") {
    loader.config["defiance.supply-radius/enabled"] = "false";
    REQUIRE(init() == 0);
    CHECK(loader.staged.size() == 18);
    CHECK(loader.at(L.radiusLoad) != nullptr);
}

// ================================================================ settings

TEST_CASE_FIXTURE(Plugin, "log = false: install lines only, no event lines") {
    loader.config["sokolov.spares-repair/log"] = "false";
    REQUIRE(init() == 0);
    CHECK_FALSE(g_logEvents);
    CHECK(loader.logged("event log off"));

    size_t lines = loader.lines.size();
    alignas(8) uint8_t facet[0x20] = {};
    trace("F4 event", facet);
    CHECK(loader.lines.size() == lines);
}

TEST_CASE_FIXTURE(Plugin, "log not set: events are logged, into the loader's log") {
    REQUIRE(init() == 0);
    CHECK(g_logEvents);
    alignas(8) uint8_t facet[0x20] = {};
    trace("F4 event", facet);
    CHECK(loader.logged("F4 event facet="));
}
