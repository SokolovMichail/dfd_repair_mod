// SparesSupply.dll, HEMTT Spares part. Overview - spares.h.

#include "spares.h"

namespace spares_supply {

namespace {

// Подставить в инструкцию lea r8,[rax+disp32] функции круга смещение строки материала в таблице:
// kMaterialSpares (0xE0) перед перерисовкой круга HEMTT Spares, kMaterialAmmo (0x120) - вернуть обратно.
void setCircleMaterial(uint32_t offset) {
    // the instruction was made writable by unprotectCircle: at install (standalone) or just before the first
    // redraw (DefianceLoader, see redrawSparesCircle)
    *reinterpret_cast<uint32_t*>(g_a.circleLea + 3) = offset;
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(g_a.circleLea), 7);
}

// What core's spare parts rules need, taken from logic.dll. A station is an AmmoRechargerFacet or a
// RefuellerFacet (F4 mode switches the first of them a vehicle has), a depot - a SparesRechargerFacet.
class LogicDllSparesPort final : public core::SparesPort {
public:
    // The SparesRechargerFacet of the same vehicle, found by its exact class (vtable).
    core::SparesDepot* depotOf(core::Station* s) override { return depot(sparesOf(s)); }
    // "Off" flags: ammo and refueller - byte +0x59, spare parts - byte +0x51; 0 means switched on.
    bool stationOn(core::Station* s) override { return spares_supply::isOn(s, kRechargerOffFlag); }
    bool isOn(core::SparesDepot* d) override { return spares_supply::isOn(d, kSparesOffFlag); }
    // the originals, bypassing our hookSparesOff
    void turnOn(core::SparesDepot* d) override { g_origSparesOn(d); }
    void turnOff(core::SparesDepot* d) override { g_origSparesOff(d); }
    // Through our hook, so the zone diagnostics in the log (radius, targets) are written too.
    void tick(core::SparesDepot* d, float dt) override { hookSparesUpdate(d, dt); }
    // F4 mode runs on the vehicle if its ammo component or its refueller is switched on.
    bool supplyModeActive(core::SparesDepot* d) override {
        void* owner = ownerOf(d);
        for (uintptr_t vt : {g_a.ammoVt, g_a.fuelVt}) {
            void* r = findFacet(owner, vt);
            if (r && spares_supply::isOn(r, kRechargerOffFlag)) return true;
        }
        return false;
    }
    // Events go through trace(): the first 50 per run, with the facet and owner addresses.
    void event(const char* what, core::SparesDepot* d) override { trace(what, d); }
};

// The single spare parts port of the plugin; it keeps no state, so one instance serves all vehicles.
LogicDllSparesPort g_sparesPort;

}  // namespace

core::SparesPort& sparesPort() { return g_sparesPort; }

// Компонент запчастей (SparesRechargerFacet) той же машины, что и facet; nullptr, если его нет.
void* sparesOf(void* facet) {
    return findFacet(ownerOf(facet), g_a.sparesVt);
}

// Spare parts of the same vehicle as recharger (ammo or refueller) follow F4 mode - core::syncSpares. Used
// by the refueller hooks below; the ammo hooks go through core::onSupplyOn / onSupplyOff, which call it too.
void syncSpares(void* recharger, bool on) { core::syncSpares(g_sparesPort, station(recharger), on); }

// HEMTT Spares' circle: the circle function takes its material from the material table at the offset written
// into its "lea r8,[rax+0x120]" (0x120 - ammoRecharge, 0xE0 - sparesRecharge). For one call of the function
// the offset is switched to 0xE0 and back, so every other vehicle keeps drawing its own circle. false - the
// instruction could not be made writable at install time, the ammo circle stays.
bool redrawSparesCircle(void* ammo) {
    if (g_circleDeferred) {
        g_circleDeferred = false;
        unprotectCircle();
    }
    if (!g_circleWritable) return false;
    setCircleMaterial(kMaterialSpares);
    reinterpret_cast<FacetFn>(g_a.showCircle)(ammo);
    setCircleMaterial(kMaterialAmmo);
    return true;
}

// Перехваты RefuellerFacet::activate / deactivate (слоты 14 / 13): оригинал + то же для запчастей
// машины (на случай машины с refuel и support - режим F4 тогда берёт заправщик).
void hookFuelOn(void* f) {
    g_origFuelOn(f);
    syncSpares(f, true);
}
void hookFuelOff(void* f) {
    g_origFuelOff(f);
    syncSpares(f, false);
}

// F4 mode: tick the spare parts of the same vehicle as recharger - core::tickSpares. Returns the spare parts
// component (even a switched-off one), nullptr - none or recharger is off.
void* tickSpares(void* recharger, float dt) { return core::tickSpares(g_sparesPort, station(recharger), dt); }

// Перехват RefuellerFacet::update (слот 10): оригинал + тик запчастей той же машины в режиме F4.
void hookFuelUpdate(void* f, float dt) {
    g_origFuelUpdate(f, dt);
    tickSpares(f, dt);
}

// Перехват SparesRechargerFacet::update (слот 10). Сам движок его не вызывает - его вызываем мы
// (tickSpares) и приказ «пополнить» по клику. Оригинал ищет машины в радиусе и пополняет им запчасти.
// Диагностика: первые 20 секунд работы зоны - радиус и число целей в ней (список +0x28..+0x30).
void hookSparesUpdate(void* spares, float dt) {
    g_origSparesUpdate(spares, dt);
    static float acc = 0;
    static int lines = 0;
    if (!g_logEvents || !isOn(spares, kSparesOffFlag) || lines >= 20) return;
    acc += dt;
    if (acc < 1.0f) return;
    acc = 0;
    ++lines;
    auto b = *reinterpret_cast<char**>(static_cast<char*>(spares) + 0x28);
    auto e = *reinterpret_cast<char**>(static_cast<char*>(spares) + 0x30);
    g_log("spares active facet=%p radius=%.1f targets=%d", spares, *reinterpret_cast<float*>(g_a.supplyRadius),
          static_cast<int>((e - b) / 8));
}

// Перехват SparesRechargerFacet::deactivate (слот 13). Приказ «пополнить» по клику по окончании
// выключает запчасти - если в это время у машины включён режим F4 (боеприпасы или заправщик), выключение
// пропускаем, иначе раздача по зоне оборвалась бы.
void hookSparesOff(void* spares) {
    // the rule itself (and its log line) - core::mayTurnSparesOff
    if (!core::mayTurnSparesOff(g_sparesPort, depot(spares))) return;
    g_origSparesOff(spares);
}

}  // namespace spares_supply
