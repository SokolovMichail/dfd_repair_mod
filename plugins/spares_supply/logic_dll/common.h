// SparesSupply.dll - машины снабжения в режиме F4 («SUPPLY MODE»): HEMTT Spares раздаёт запчасти по зоне,
// HEMTT Repair чинит технику в зоне и восстанавливает ей броню.
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
//  * Ремонт (abilities=repair -> RepairerFacet) недоделан: компонент создаётся выключенным, включает его
//    никто, а его update (0x363790) ищет цели в радиусе и тут же выбрасывает результат - список целей
//    всегда пуст. Команду цели он бы и не проверял. Броню (толщину слоёв) в бою не восстанавливает ничто.
//
// HEMTT Spares - see spares.h, HEMTT Repair - see repair.h.
//
// Лог: bin\plugins\SparesSupply.log

#pragma once

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../../../common/patch.h"

namespace spares_supply {

using dfd::Image;

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
constexpr const char* kPrologueRepairUpdate = "48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 57 41 54 41 55 41 56 41 57";
constexpr const char* kPrologueRepairOff = "C6 41 49 01 C3";
constexpr const char* kPrologueRepairOn = "66 C7 41 48 00 00 C3";

// «mov r15b, 1» после найденного support - в сборщике машин и в сборщике танков (ровно 2 совпадения).
constexpr const char* kSigSupportBlocksAmmo =
    "41 B7 01 8B C3 FF C3 48 8D 0D ?? ?? ?? ?? 48 89 4D C8 89 45 D0 C7 45 D4 01 00 00 00 0F B6 86 14 02 00 00 "
    "3C 20 73 ?? 48 FF C0 48 03 C0 0F 10 45 C8 0F 11 04 C6 FE 86 14 02 00 00 4D 8B 4D 00 49 8B 14 24 49 3B D1 "
    "0F 84 ?? ?? ?? ??";
// То же для любой способности: «mov r15b,1; mov eax,ebx; inc ebx; lea rcx,[rip+имя компонента]».
// Нужные места выбираются по строке в lea («[Leonardo.RepairerFacet]» - ровно 2: машины и танки).
constexpr const char* kSigAbilityFlag = "41 B7 01 8B C3 FF C3 48 8D 0D ?? ?? ?? ??";
// Поиск целей запчастей: movss xmm2, [rip+disp] - радиус 20.0f.
constexpr const char* kSigSparesRadiusLoad = "F3 0F 10 15 ?? ?? ?? ?? 48 8B D5 FF 50 10 48 8B 6C 24 40 48 3B 6C 24 48";
// Конструктор AmmoRechargerFacet: movss xmm0, [rip+supplyRadius]; movss [r14+0x5c], xmm0.
constexpr const char* kSigSupplyRadiusUse = "F3 0F 10 05 ?? ?? ?? ?? F3 41 0F 11 46 5C 49 8B 4E 10 48 85 C9";
// Круг зоны боеприпасов: функция (пере)создания и lea r8, [rax+0x120] (материал ammoRecharge) в ней.
constexpr const char* kSigShowCircle =
    "48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 41 56 48 83 EC 30 48 83 79 50 00";
constexpr const char* kSigCircleMaterialLea = "4C 8D 80 20 01 00 00 FF D3 8B 4F 5C 48 89 47 50 89 48 3C 48 8B 5F 10";
constexpr size_t kMaterialTableCallBack = 0x0c;  // call «таблица материалов» (0xff050) - за 12 байт до lea
// RepairerFacet::update: «r8d = max(1, ...); rdx = индекс модуля; call DamageableModulesFacet::incHealth».
constexpr const char* kSigIncHealthCall = "41 B8 01 00 00 00 85 C0 44 0F 45 C0 48 8B D6 E8 ?? ?? ?? ??";
constexpr const char* kPrologueIncHealth = "48 8B C4 48 89 58 08 48 89 68 18 48 89 70 20 48 89 50 10";
// AmmoRechargerFacet::canRecharge, +0x3a: owner->vfunc(+0x78) (мир) -> call «дипломатия мира» (0x541a00).
constexpr const char* kSigDiplomacyCall = "FF 50 78 48 8B C8 E8 ?? ?? ?? ?? 4C 8B F8 48 85 C0";
constexpr size_t kDiplomacyCallAt = 0x3a;

constexpr size_t kAmmoCircleField = 0x50;
constexpr uint32_t kMaterialAmmo = 0x120, kMaterialSpares = 0xE0;  // materials/rounds/{ammo,spares}Recharge

// Поля компонентов.
constexpr size_t kRechargerOffFlag = 0x59;  // Ammo/Refueller: isActive = (byte +0x59 == 0)
constexpr size_t kSparesOffFlag = 0x51;     // Spares: isActive = (byte +0x51 == 0)
constexpr size_t kAmmoTargets = 0x30;       // Ammo: vector<RefBlock*> машин в зоне

// Найденные адреса.
struct Addr {
    uintptr_t ammoVt, fuelVt, sparesVt, repairVt, modulesVt;
    uintptr_t radiusLoad, supplyRadius, showCircle, circleLea, materialTable, incHealth, diplomacy;
    std::vector<uintptr_t> supportBlocksAmmo, repairBlocksAmmo;
};
extern Addr g_a;

// ---------------------------------------------------------------- модель объектов движка

// Счётчик ссылок движка: [+0x08] сильные ссылки, [+0x10] объект. Компонент хранит владельца
// (машину) как такой блок в [+0x10].
struct RefBlock {
    void** vtbl;
    int32_t strong;
    int32_t weak;
    void* object;
};

// Вызвать виртуальный метод объекта движка по смещению слота в его vtable (offset = 8 * номер слота),
// как это делает сам движок: call [vtbl+offset] с obj первым аргументом. R - тип результата.
template <class R, class... A>
R vcall(void* obj, size_t offset, A... args) {
    auto vtbl = *reinterpret_cast<void***>(obj);
    return reinterpret_cast<R (*)(void*, A...)>(vtbl[offset / 8])(obj, args...);
}

void release(RefBlock* rb);

extern dfd::Log g_log;
extern int g_events;

using FacetFn = void (*)(void* facet);
using CanRechargeFn = bool (*)(void* facet, void* target);
using UpdateFn = void (*)(void* facet, float dt);
extern FacetFn g_origAmmoOn, g_origAmmoOff, g_origFuelOn, g_origFuelOff, g_origSparesOn, g_origSparesOff;
extern FacetFn g_origRepairOn, g_origRepairOff;
extern CanRechargeFn g_origAmmoCanRecharge;
extern UpdateFn g_origSparesUpdate, g_origAmmoUpdate, g_origFuelUpdate, g_origRepairUpdate;

void* ownerOf(void* facet);
bool isOn(void* facet, size_t offFlag);
void* findFacet(void* entity, uintptr_t vtbl);

// Чем на самом деле занимается машина с компонентом боеприпасов (у машин мода он только ради F4):
// Spares - есть компонент запчастей (HEMTT Spares), Repair - есть компонент ремонта (HEMTT Repair),
// Ammo - ни того ни другого (обычный HEMTT Supply и прочие - их не трогаем).
enum class Kind { Ammo, Spares, Repair };
Kind kindOf(void* ammo);

void trace(const char* what, void* facet);

// Zone circle (common.cpp); the materials themselves are drawn by spares.cpp / repair.cpp.
extern std::unordered_set<void*> g_ownCircles;
extern bool g_circleWritable;
void* circleOf(void* ammo);
void convertCircle(void* ammo, Kind kind);

// AmmoRechargerFacet hooks: F4 mode on the ammo component, shared by HEMTT Spares and HEMTT Repair.
void hookAmmoOn(void* f);
void hookAmmoOff(void* f);
void hookAmmoUpdate(void* f, float dt);
bool hookAmmoCanRecharge(void* ammo, void* target);

// Finding addresses in logic.dll and installing the patches.
bool resolve(const Image& img);
void onLogicLoaded(uintptr_t base);

}  // namespace spares_supply
