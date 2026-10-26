// SparesSupply.dll, HEMTT Repair part (repair.cpp). Shared parts - common.h.
//
// HEMTT Repair (abilities=ammunition_recharge,repair; RepairerFacet - метка, сам ничего не чинит):
//  7. Убирает запрет «repair блокирует ammunition_recharge» (оба сборщика) - кнопка F4.
//  8. Патроны такая машина тоже не раздаёт (п. 4).
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

#include "common.h"

namespace spares_supply {

constexpr const char* kRepairMaterial = "materials/rounds/repairRecharge.material";

// DamageableModulesFacet
constexpr size_t kModulesBegin = 0xd0, kModulesEnd = 0xd8, kModuleSize = 0x148, kModulesTotalHealth = 0xcc;
constexpr size_t kModulesSpares = 0x130;  // запчасти машины (int; максимум - +0x12c), слоты 26-29 vtable
constexpr size_t kModuleDesc = 0x00, kModuleHealth = 0x0c, kModuleArmor = 0x28;     // в модуле
constexpr size_t kDescMaxHealth = 0x28, kDescArmor = 0x90, kArmorLayerSize = 0x10;  // в описании модуля
constexpr int kArmorSides = 6;

// Ремонт: раз в kRepairPeriod секунд каждой машине в зоне.
constexpr float kRepairPeriod = 0.5f;
constexpr float kRepairFraction = 0.02f;  // модулю - 2% максимума (4%/с: от нуля до полного - 25 с)
constexpr float kArmorFraction = 0.02f;   // слою брони - 2% исходной толщины
// Цена - запчасти самой HEMTT Repair (у цели не списываются). Модуль: 1 запчасть за 1 здоровья (как у
// ремонта экипажем). Броня: kSparesPerMm запчастей за единицу толщины (судя по данным - миллиметр).
constexpr float kSparesPerMm = 1.0f;

// std::string MSVC (x64): буфер/указатель, длина, ёмкость; при ёмкости >= 16 строка лежит по указателю.
struct MsvcString {
    const char* ptr;
    char pad[8];
    size_t size, capacity;
};
static_assert(sizeof(MsvcString) == 32);

using IncHealthFn = void (*)(void* modules, size_t index, int amount);
using DiplomacyFn = void* (*)(void* world);

struct Repaired {
    int health = 0;  // сколько здоровья добавлено модулям
    int layers = 0;  // сколько слоёв брони подросло
    int spent = 0;   // сколько запчастей потрачено
};

int& sparesOfModules(void* modules);

// Склад запчастей HEMTT Repair. Дробная цена брони копится в debt и списывается целыми запчастями.
struct Stock {
    int& spares;
    float& debt;
    // Заплатить cost запчастей: цена копится в debt, со склада списывается целая часть (но не больше,
    // чем есть). Потраченное прибавляется к r.spent.
    void pay(float cost, Repaired& r) {
        debt += cost;
        int whole = std::min(spares, static_cast<int>(debt));
        spares -= whole;
        debt -= whole;
        r.spent += whole;
    }
};

Repaired repairVehicle(void* modules, Stock stock);

extern std::unordered_map<void*, float> g_repairTimers;
extern std::unordered_map<void*, float> g_armorDebt;
extern int g_repairLogs;
void tickRepair(void* ammo, float dt);
// Forget the repair state (pulse timer, armor debt) of an ammo component - F4 mode is over.
void forgetRepair(void* ammo);
// Redraw the HEMTT Repair zone circle with the repairRecharge material.
void redrawRepairCircle(void* ammo);

void hookRepairOn(void* r);
void hookRepairOff(void* r);
void hookRepairUpdate(void*, float);

}  // namespace spares_supply
