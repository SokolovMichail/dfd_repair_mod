// SparesSupply.dll - зонная раздача запчастей в режиме снабжения (F4 «SUPPLY MODE»).
//
// Как устроено в logic.dll (адреса - для GOG 1.09.1221, сам плагин ищет всё по шаблонам/RTTI):
//  * Режим F4 (AiUnpack/AiDo/AiPackRechargeState) берёт у машины ПЕРВЫЙ IRechargerFacet
//    (заправщик RefuellerFacet или боеприпасы AmmoRechargerFacet) и включает/выключает только его
//    через слоты vtable 14 (+0x70) / 13 (+0x68). Компонент запчастей SparesRechargerFacet - другой
//    интерфейс, поэтому в режим F4 не попадает, хотя сам по себе умеет раздавать по радиусу.
//  * При разборе колонки abilities (сборщики описаний машин 0x51c570 и танков 0x521ce0) компонент
//    ammunition_recharge НЕ создаётся, если есть refuel / support / repair (флаг r15b).
//  * Апдейт запчастей (слот 10, 0x439e00) движок сам НЕ вызывает - его тикает только приказ по клику
//    (AiSparesRechargeUnitState 0xffac0: jmp [facet+0x50] с dt). Апдейты заправщика/боеприпасов
//    движок вызывает каждый кадр.
//
// Что делает плагин:
//  1. Убирает запрет «support блокирует ammunition_recharge» (оба сборщика) - машина с
//     abilities=ammunition_recharge,support получает оба компонента и кнопку F4.
//  2. Включение/выключение заправщика и боеприпасов (слоты 14/13) дублируется на компонент
//     запчастей той же машины; из их апдейтов (пока включены, т.е. идёт режим F4) тикаются запчасти.
//  3. Выключение компонента запчастей (слот 13) пропускается, пока у машины активен заправщик или
//     боеприпасы - иначе приказ «пополнить запчасти» по клику выключил бы раздачу по окончании.
//  4. Машина с запчастями не раздаёт патроны: AmmoRechargerFacet::canRecharge(target) (слот 17,
//     +0x88) отвечает «нет». Через него идут и раздача по зоне (update 0x120b90), и приказ по клику
//     (AiFacet canAmmo 0xcdcf0). Компонент боеприпасов нужен такой машине только ради кнопки F4.
//  5. Радиус поиска целей у запчастей (0x439bc0, зашитые 20 м) заменён на глобальный supplyRadius -
//     тот же, что у боеприпасов/заправщика и у нарисованного круга F4.
//  6. Круг зоны F4 у такой машины рисуется материалом sparesRecharge (гаечные ключи - текстура в моде),
//     а не ammoRecharge: AmmoRechargerFacet рисует круг функцией 0x120610, материал берётся из таблицы
//     0xff050 по смещению из инструкции lea r8,[rax+0x120] (0x120687). Плагин на время своего вызова
//     0x120610 подменяет смещение на 0xe0 (sparesRecharge) и перерисовывает круг.
//
// Лог: bin\plugins\SparesSupply.log

#include <windows.h>

#include <cstdint>
#include <unordered_set>

#include "../../common/patch.h"

#ifndef SPARES_SUPPLY_VERSION  // задаётся из CMake (MOD_VERSION)
#define SPARES_SUPPLY_VERSION "dev"
#endif

namespace {

// ---------------------------------------------------------------- что ищем в logic.dll

constexpr int kSlotUpdate = 10;       // +0x50, void(float dt)
constexpr int kSlotDeactivate = 13;   // +0x68
constexpr int kSlotActivate = 14;     // +0x70
constexpr int kSlotCanRecharge = 17;  // +0x88, bool(target)

// Начала функций в слотах vtable: подтверждают, что это те же функции и те же смещения полей.
constexpr const char* kPrologueRechargerOff = "48 89 5C 24 08 57 48 83 EC 20 33 FF C6 41 59 01";  // ammo, fuel
constexpr const char* kPrologueRechargerOn = "40 53 48 83 EC 20 66 C7 41 58 00 00 48 8B D9";    // ammo, fuel
constexpr const char* kPrologueAmmoUpdate = "41 56 48 81 EC A0 00 00 00 48 83 79 78 00 4C 8B F1";
constexpr const char* kPrologueAmmoCan = "40 55 56 41 57 48 83 EC 50 48 8B E9 48 8B F2 48 8B 49 10";
constexpr const char* kPrologueFuelUpdate = "40 56 48 81 EC 80 00 00 00 48 83 B9 88 00 00 00 00";
constexpr const char* kPrologueSparesUpdate = "40 53 48 83 EC 20 80 79 51 00 48 8B D9 0F 85";
constexpr const char* kPrologueSparesOff = "C6 41 51 01 C3";
constexpr const char* kPrologueSparesOn = "66 C7 41 50 00 00 C3";

// «mov r15b, 1» после найденного support - в сборщике машин и в сборщике танков (ровно 2 совпадения).
constexpr const char* kSigSupportBlocksAmmo =
    "41 B7 01 8B C3 FF C3 48 8D 0D ?? ?? ?? ?? 48 89 4D C8 89 45 D0 C7 45 D4 01 00 00 00 0F B6 86 14 02 00 00 "
    "3C 20 73 ?? 48 FF C0 48 03 C0 0F 10 45 C8 0F 11 04 C6 FE 86 14 02 00 00 4D 8B 4D 00 49 8B 14 24 49 3B D1 "
    "0F 84 ?? ?? ?? ??";
// Поиск целей запчастей: movss xmm2, [rip+disp] - радиус 20.0f.
constexpr const char* kSigSparesRadiusLoad = "F3 0F 10 15 ?? ?? ?? ?? 48 8B D5 FF 50 10 48 8B 6C 24 40 48 3B 6C 24 48";
// Конструктор AmmoRechargerFacet: movss xmm0, [rip+supplyRadius]; movss [r14+0x5c], xmm0.
constexpr const char* kSigSupplyRadiusUse = "F3 0F 10 05 ?? ?? ?? ?? F3 41 0F 11 46 5C 49 8B 4E 10 48 85 C9";
// Круг зоны боеприпасов: функция (пере)создания и lea r8, [rax+0x120] (материал ammoRecharge) в ней.
constexpr const char* kSigShowCircle =
    "48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 41 56 48 83 EC 30 48 83 79 50 00";
constexpr const char* kSigCircleMaterialLea = "4C 8D 80 20 01 00 00 FF D3 8B 4F 5C 48 89 47 50 89 48 3C 48 8B 5F 10";

constexpr size_t kAmmoCircleField = 0x50;
constexpr uint32_t kMaterialAmmo = 0x120, kMaterialSpares = 0xE0;  // materials/rounds/{ammo,spares}Recharge

// Поля компонентов: байт «выключен».
constexpr size_t kRechargerOffFlag = 0x59;  // Ammo/Refueller: isActive = (byte +0x59 == 0)
constexpr size_t kSparesOffFlag = 0x51;     // Spares: isActive = (byte +0x51 == 0)

// Найденные адреса.
struct Addr {
    uintptr_t ammoVt, fuelVt, sparesVt;
    uintptr_t radiusLoad, supplyRadius, showCircle, circleLea;
    std::vector<uintptr_t> supportBlocksAmmo;
} g_a{};

// ---------------------------------------------------------------- модель объектов движка

// Счётчик ссылок движка: [+0x08] сильные ссылки, [+0x10] объект. Компонент хранит владельца
// (машину) как такой блок в [+0x10].
struct RefBlock {
    void** vtbl;
    int32_t strong;
    int32_t weak;
    void* object;
};

template <class R, class... A>
R vcall(void* obj, size_t offset, A... args) {
    auto vtbl = *reinterpret_cast<void***>(obj);
    return reinterpret_cast<R (*)(void*, A...)>(vtbl[offset / 8])(obj, args...);
}

void release(RefBlock* rb) {
    if (rb && --rb->strong == 0) vcall<void>(rb, 0x10);
}

dfd::Log g_log;
int g_events = 0;  // первые события пишем в лог для проверки

using FacetFn = void (*)(void* facet);
using CanRechargeFn = bool (*)(void* facet, void* target);
using UpdateFn = void (*)(void* facet, float dt);
FacetFn g_origAmmoOn, g_origAmmoOff, g_origFuelOn, g_origFuelOff, g_origSparesOn, g_origSparesOff;
CanRechargeFn g_origAmmoCanRecharge;
UpdateFn g_origSparesUpdate, g_origAmmoUpdate, g_origFuelUpdate;

void* ownerOf(void* facet) {
    auto rb = *reinterpret_cast<RefBlock**>(static_cast<char*>(facet) + 0x10);
    return rb ? rb->object : nullptr;
}

bool isOn(void* facet, size_t offFlag) {
    return *(static_cast<uint8_t*>(facet) + offFlag) == 0;
}

// Компонент машины с точным классом (по адресу vtable) - как в logic.dll 0x57490 / 0xce070:
// entity->vfunc(+0x58) отдаёт итератор: +0x18 конец?, +0x30 текущий (блок ссылки), +0x20 следующий,
// +0x00 удаляющий деструктор.
void* findFacet(void* entity, uintptr_t vtbl) {
    if (!entity) return nullptr;
    void* it = nullptr;
    vcall<void*>(entity, 0x58, &it);
    if (!it) return nullptr;
    void* found = nullptr;
    while (!vcall<bool>(it, 0x18)) {
        RefBlock* tmp = nullptr;
        RefBlock* rb = *vcall<RefBlock**>(it, 0x30, &tmp);
        void* facet = rb ? rb->object : nullptr;
        if (facet && *reinterpret_cast<uintptr_t*>(facet) == vtbl) found = facet;
        release(tmp);
        if (found) break;
        vcall<void>(it, 0x20);
    }
    vcall<void>(it, 0x00, 1);
    return found;
}

void* sparesOf(void* facet) {
    return findFacet(ownerOf(facet), g_a.sparesVt);
}

void trace(const char* what, void* facet) {
    if (g_events < 50) {
        ++g_events;
        g_log("%s facet=%p owner=%p", what, facet, ownerOf(facet));
    }
}

// Включить/выключить запчасти у той же машины, что и recharger.
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

// ---------------------------------------------------------------- круг зоны

// Круги, уже перерисованные материалом запчастей.
std::unordered_set<void*> g_sparesCircles;
bool g_circleWritable = false;

void* circleOf(void* ammo) {
    return *reinterpret_cast<void**>(static_cast<char*>(ammo) + kAmmoCircleField);
}

void setCircleMaterial(uint32_t offset) {
    // страница с инструкцией сделана записываемой при установке
    *reinterpret_cast<uint32_t*>(g_a.circleLea + 3) = offset;
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(g_a.circleLea), 7);
}

// Если у боеприпасов есть круг с материалом патронов - пересоздать его с материалом запчастей.
void convertCircle(void* ammo) {
    void* c = circleOf(ammo);
    if (!c || !g_circleWritable || g_sparesCircles.count(c)) return;
    setCircleMaterial(kMaterialSpares);
    reinterpret_cast<FacetFn>(g_a.showCircle)(ammo);
    setCircleMaterial(kMaterialAmmo);
    if (void* n = circleOf(ammo)) g_sparesCircles.insert(n);
    trace("supply circle -> spares material", ammo);
}

// ---------------------------------------------------------------- перехваты

void hookSparesUpdate(void* spares, float dt);

void hookAmmoOn(void* f) {
    g_origAmmoOn(f);
    syncSpares(f, true);
    if (sparesOf(f)) convertCircle(f);
}
void hookAmmoOff(void* f) {
    if (void* c = circleOf(f)) g_sparesCircles.erase(c);
    g_origAmmoOff(f);
    syncSpares(f, false);
}
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

void hookAmmoUpdate(void* f, float dt) {
    g_origAmmoUpdate(f, dt);
    // круг, созданный без нас (например, при загрузке сохранения), тоже перекрашиваем
    if (tickSpares(f, dt)) convertCircle(f);
}
void hookFuelUpdate(void* f, float dt) {
    g_origFuelUpdate(f, dt);
    tickSpares(f, dt);
}

bool hookAmmoCanRecharge(void* ammo, void* target) {
    if (sparesOf(ammo)) return false;
    return g_origAmmoCanRecharge(ammo, target);
}

// Диагностика: первые секунды работы зоны - радиус и число целей в ней (список +0x28..+0x30).
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

// ---------------------------------------------------------------- поиск и установка

// rip-относительный адрес из инструкции: disp32 по смещению dispAt, инструкция длиной len.
uintptr_t ripTarget(uintptr_t insn, size_t dispAt, size_t len) {
    return insn + len + *reinterpret_cast<const int32_t*>(insn + dispAt);
}

uintptr_t findOne(const dfd::Image& img, const char* what, const char* sig) {
    auto hits = img.find(sig);
    if (hits.size() == 1) return hits[0];
    g_log("signature '%s': %zu matches (need 1)", what, hits.size());
    return 0;
}

bool resolve(const dfd::Image& img) {
    g_a.ammoVt = img.vtableOf("AmmoRechargerFacet@Leonardo");
    g_a.fuelVt = img.vtableOf("RefuellerFacet@Leonardo");
    g_a.sparesVt = img.vtableOf("SparesRechargerFacet@Leonardo");
    g_log("vtables: ammo %08X fuel %08X spares %08X", g_a.ammoVt ? img.rva(g_a.ammoVt) : 0,
          g_a.fuelVt ? img.rva(g_a.fuelVt) : 0, g_a.sparesVt ? img.rva(g_a.sparesVt) : 0);

    g_a.supportBlocksAmmo = img.find(kSigSupportBlocksAmmo);
    if (g_a.supportBlocksAmmo.size() != 2)
        g_log("signature 'support blocks ammo': %zu matches (need 2)", g_a.supportBlocksAmmo.size());
    g_a.radiusLoad = findOne(img, "spares radius load", kSigSparesRadiusLoad);
    if (uintptr_t use = findOne(img, "supplyRadius use", kSigSupplyRadiusUse)) g_a.supplyRadius = ripTarget(use, 4, 8);
    g_a.showCircle = findOne(img, "ammo show circle", kSigShowCircle);
    g_a.circleLea = findOne(img, "circle material lea", kSigCircleMaterialLea);
    // lea должна лежать внутри функции круга (в GOG-сборке - +0x77)
    if (g_a.circleLea && g_a.showCircle && (g_a.circleLea < g_a.showCircle || g_a.circleLea > g_a.showCircle + 0x100)) {
        g_log("circle material lea is not inside the circle function");
        g_a.circleLea = 0;
    }
    return g_a.ammoVt && g_a.fuelVt && g_a.sparesVt && g_a.supportBlocksAmmo.size() == 2 && g_a.radiusLoad &&
           g_a.supplyRadius && g_a.showCircle && g_a.circleLea;
}

void onLogicLoaded(uintptr_t base) {
    dfd::Image img(base);
    g_log("logic.dll at %p (TimeDateStamp %08X, SizeOfImage %08X)", reinterpret_cast<void*>(base),
          img.timeDateStamp(), img.sizeOfImage());
    if (!resolve(img)) {
        g_log("this logic.dll build is not supported - nothing patched");
        return;
    }

    const std::vector<uint8_t> movR15b1{0x41, 0xB7, 0x01}, nop3{0x90, 0x90, 0x90};
    dfd::PatchSet p(img);
    p.bytes("cars: support no longer blocks ammunition_recharge", g_a.supportBlocksAmmo[0], movR15b1, nop3);
    p.bytes("tanks: support no longer blocks ammunition_recharge", g_a.supportBlocksAmmo[1], movR15b1, nop3);
    // movss xmm2, [rip+disp32]: F3 0F 10 15 <disp32>, disp от конца инструкции (+8)
    auto movss = [&](uintptr_t target) {
        auto v = static_cast<uint32_t>(target - (g_a.radiusLoad + 8));
        return std::vector<uint8_t>{0xF3, 0x0F, 0x10, 0x15, uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16),
                                    uint8_t(v >> 24)};
    };
    p.bytes("spares search radius 20 m -> supplyRadius", g_a.radiusLoad, movss(ripTarget(g_a.radiusLoad, 4, 8)),
            movss(g_a.supplyRadius));

    p.vslot("AmmoRechargerFacet::activate", g_a.ammoVt, kSlotActivate, kPrologueRechargerOn, &hookAmmoOn,
            &g_origAmmoOn);
    p.vslot("AmmoRechargerFacet::deactivate", g_a.ammoVt, kSlotDeactivate, kPrologueRechargerOff, &hookAmmoOff,
            &g_origAmmoOff);
    p.vslot("AmmoRechargerFacet::update", g_a.ammoVt, kSlotUpdate, kPrologueAmmoUpdate, &hookAmmoUpdate,
            &g_origAmmoUpdate);
    p.vslot("AmmoRechargerFacet::canRecharge", g_a.ammoVt, kSlotCanRecharge, kPrologueAmmoCan, &hookAmmoCanRecharge,
            &g_origAmmoCanRecharge);
    p.vslot("RefuellerFacet::activate", g_a.fuelVt, kSlotActivate, kPrologueRechargerOn, &hookFuelOn, &g_origFuelOn);
    p.vslot("RefuellerFacet::deactivate", g_a.fuelVt, kSlotDeactivate, kPrologueRechargerOff, &hookFuelOff,
            &g_origFuelOff);
    p.vslot("RefuellerFacet::update", g_a.fuelVt, kSlotUpdate, kPrologueFuelUpdate, &hookFuelUpdate,
            &g_origFuelUpdate);
    p.vslot("SparesRechargerFacet::update", g_a.sparesVt, kSlotUpdate, kPrologueSparesUpdate, &hookSparesUpdate,
            &g_origSparesUpdate);
    p.vslot("SparesRechargerFacet::deactivate", g_a.sparesVt, kSlotDeactivate, kPrologueSparesOff, &hookSparesOff,
            &g_origSparesOff);
    // активацию запчастей не перехватываем - только проверяем и запоминаем исходную функцию
    uintptr_t sparesOn = *reinterpret_cast<uintptr_t*>(g_a.sparesVt + 8 * kSlotActivate);
    if (!dfd::Image::matches(sparesOn, kPrologueSparesOn)) {
        g_log("verify FAILED: SparesRechargerFacet::activate - nothing patched");
        return;
    }
    g_origSparesOn = reinterpret_cast<FacetFn>(sparesOn);
    const std::vector<uint8_t> leaAmmo{0x4C, 0x8D, 0x80, 0x20, 0x01, 0x00, 0x00};
    p.bytes("supply circle material lea (verify only)", g_a.circleLea, leaAmmo, leaAmmo);
    if (!p.apply(g_log)) return;

    DWORD old;
    g_circleWritable = VirtualProtect(reinterpret_cast<void*>(g_a.circleLea), 7, PAGE_EXECUTE_READWRITE, &old);
    if (!g_circleWritable)
        g_log("cannot unprotect circle material lea (error %lu) - circle keeps ammo icons", GetLastError());
    g_log("SparesSupply active");
}

dfd::DllWatcher g_watcher;

}  // namespace

BOOL WINAPI DllMain(HINSTANCE self, DWORD reason, LPVOID) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(self);
    g_log.open(self, L"SparesSupply");
    g_log("SparesSupply " SPARES_SUPPLY_VERSION " loaded, waiting for logic.dll");
    if (!g_watcher.watch(L"logic.dll", &onLogicLoaded)) g_log("cannot register DLL notification");
    return TRUE;
}
