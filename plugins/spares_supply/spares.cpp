// SparesSupply.dll, HEMTT Spares part. Overview - spares.h.

#include "spares.h"

namespace spares_supply {

namespace {

// Подставить в инструкцию lea r8,[rax+disp32] функции круга смещение строки материала в таблице:
// kMaterialSpares (0xE0) перед перерисовкой круга HEMTT Spares, kMaterialAmmo (0x120) - вернуть обратно.
void setCircleMaterial(uint32_t offset) {
    // страница с инструкцией сделана записываемой при установке
    *reinterpret_cast<uint32_t*>(g_a.circleLea + 3) = offset;
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(g_a.circleLea), 7);
}

}  // namespace

// Компонент запчастей (SparesRechargerFacet) той же машины, что и facet; nullptr, если его нет.
void* sparesOf(void* facet) {
    return findFacet(ownerOf(facet), g_a.sparesVt);
}

// Включить (on) или выключить запчасти у той же машины, что и recharger (боеприпасы или заправщик).
// Вызывается при включении/выключении режима F4. Если запчастей нет или они уже в нужном состоянии -
// ничего не делает. Вызывает исходные функции, минуя перехват hookSparesOff.
void syncSpares(void* recharger, bool on) {
    void* spares = sparesOf(recharger);
    if (!spares) return;
    if (on && !isOn(spares, kSparesOffFlag)) {
        trace("spares ON  (with supply mode)", spares);
        g_origSparesOn(spares);
    } else if (!on && isOn(spares, kSparesOffFlag)) {
        trace("spares OFF (with supply mode)", spares);
        g_origSparesOff(spares);
    }
}

bool redrawSparesCircle(void* ammo) {
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

// Режим F4: recharger включён -> тикнуть запчасти той же машины тем же dt. Возвращает компонент запчастей.
void* tickSpares(void* recharger, float dt) {
    if (!isOn(recharger, kRechargerOffFlag)) return nullptr;
    void* spares = sparesOf(recharger);
    if (spares && isOn(spares, kSparesOffFlag)) hookSparesUpdate(spares, dt);
    return spares;
}

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
    if (!isOn(spares, kSparesOffFlag) || lines >= 20) return;
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
    void* owner = ownerOf(spares);
    for (uintptr_t vt : {g_a.ammoVt, g_a.fuelVt}) {
        void* r = findFacet(owner, vt);
        if (r && isOn(r, kRechargerOffFlag)) {
            trace("spares OFF skipped (supply mode active)", spares);
            return;
        }
    }
    g_origSparesOff(spares);
}

}  // namespace spares_supply
