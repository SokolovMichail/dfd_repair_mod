// SparesSupply.dll, HEMTT Repair part (repair.cpp). Shared parts - common.h.
//
// HEMTT Repair (abilities=ammunition_recharge,repair; RepairerFacet - метка, сам ничего не чинит):
//  7. Убирает запрет «repair блокирует ammunition_recharge» (оба сборщика) - кнопка F4.
//  8. Ammo works as for HEMTT Spares (item 4): HEMTT Repair hands out its own stock too.
//     RepairerFacet сам является IRechargerFacet (IRepairerFacet наследует его) и создаётся раньше
//     боеприпасов, поэтому режим F4 включает/выключает именно его. Слоты 14/13 ремонта перехвачены:
//     флаги ставятся как обычно, а включение дублируется на компонент боеприпасов. Собственный update
//     ремонта (слот 10) заглушён.
//  9. Пока включён режим F4, раз в kRepairPeriod берёт машины, которые компонент боеприпасов уже собрал
//     в зоне (список [ammo+0x30], обновляется в его update), отбрасывает себя и чужих (та же проверка
//     дипломатии, что в canRecharge: отношение 2 или 4 - нельзя) и каждой:
//       - чинит все повреждённые модули на kRepairFraction от максимума через
//         DamageableModulesFacet::incHealth (0x225ba0) - с пересчётом общего здоровья и состояния модуля;
//       - добавляет каждому слою брони kArmorFraction исходной толщины (не больше исходной).
//     Платит своими запчастями ([модули+0x130]): 1 за 1 здоровья, kSparesPerMm за 1 мм брони; запчасти,
//     которые incHealth (ремонт экипажем) списывает у цели, возвращаются ей. Запас кончился - ремонт стоит.
//     Модули: [facet+0xd0..+0xd8], по 0x148 байт: +0x00 описание, +0x0c здоровье, +0x28 + сторона*0x18 -
//     текущая толщина слоёв (vector<float>), сторон 6. Описание: +0x28 максимум здоровья,
//     +0x90 + сторона*0x18 - слои брони (по 16 байт, +0 исходная толщина). См. DamageableModulesFacet::
//     initialize 0x226280 и функцию урона 0x310790 (износ брони).
// 10. Круг зоны рисуется материалом materials/rounds/repairRecharge.material (в моде): на время вызова
//     0x120610 строка ammoRecharge в таблице материалов подменяется путём ремонта и сразу возвращается.

#pragma once

#include <unordered_map>

#include "../core/repair.h"
#include "common.h"

namespace spares_supply {

constexpr const char* kRepairMaterial = "materials/rounds/repairRecharge.material";

// DamageableModulesFacet
constexpr size_t kModulesBegin = 0xd0, kModulesEnd = 0xd8, kModuleSize = 0x148, kModulesTotalHealth = 0xcc;
constexpr size_t kModulesSpares = 0x130;  // запчасти машины (int; максимум - +0x12c), слоты 26-29 vtable
constexpr size_t kModuleDesc = 0x00, kModuleHealth = 0x0c, kModuleArmor = 0x28;     // в модуле
constexpr size_t kDescMaxHealth = 0x28, kDescArmor = 0x90, kArmorLayerSize = 0x10;  // в описании модуля
constexpr int kArmorSides = 6;

// std::string MSVC (x64): буфер/указатель, длина, ёмкость; при ёмкости >= 16 строка лежит по указателю.
struct MsvcString {
    const char* ptr;
    char pad[8];
    size_t size, capacity;
};
static_assert(sizeof(MsvcString) == 32);

using IncHealthFn = void (*)(void* modules, size_t index, int amount);
using DiplomacyFn = void* (*)(void* world);

// The vehicle's spare parts: an int in its DamageableModulesFacet ([+0x130]), by reference so it can be changed.
int& sparesOfModules(void* modules);

// Repair state of all stations (pulse timers, armor debt, log line counter) and short names for its parts.
extern core::RepairState g_repair;
extern std::unordered_map<core::Station*, float>& g_repairTimers;
extern std::unordered_map<core::Station*, float>& g_armorDebt;
extern int& g_repairLogs;
// F4 mode of HEMTT Repair on the ammo component `ammo` - core::tickRepair through the logic.dll port.
void tickRepair(void* ammo, float dt);
// core::RepairPort over logic.dll.
core::RepairPort& repairPort();
// Redraw the HEMTT Repair zone circle with the repairRecharge material.
void redrawRepairCircle(void* ammo);

// RepairerFacet hooks (slots 14 / 13 / 10). F4 mode on HEMTT Repair switches the repair component (it comes
// before ammo), so on / off are forwarded to the ammo component, which draws the circle and collects the zone;
// the engine's own unfinished repair update is not run at all.
void hookRepairOn(void* r);
void hookRepairOff(void* r);
void hookRepairUpdate(void*, float);

}  // namespace spares_supply
