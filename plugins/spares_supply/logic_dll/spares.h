// SparesSupply.dll, HEMTT Spares part (spares.cpp). Shared parts - common.h.
//
// HEMTT Spares (abilities=ammunition_recharge,support):
//  1. Убирает запрет «support блокирует ammunition_recharge» (оба сборщика) - машина получает оба
//     компонента и кнопку F4.
//  2. Включение/выключение заправщика и боеприпасов (слоты 14/13) дублируется на компонент
//     запчастей той же машины; из их апдейтов (пока включены, т.е. идёт режим F4) тикаются запчасти.
//  3. Выключение компонента запчастей (слот 13) пропускается, пока у машины активен заправщик или
//     боеприпасы - иначе приказ «пополнить запчасти» по клику выключил бы раздачу по окончании.
//  4. Ammo is not touched: the vehicle hands out its own ammo stock (capacity_ammo_recharger in the table)
//     like any HEMTT Supply, so it supplies ammo and spare parts at once. A stock of 0 - spare parts only.
//  5. Радиус поиска целей у запчастей (0x439bc0, зашитые 20 м) заменён на глобальный supplyRadius -
//     тот же, что у боеприпасов/заправщика и у нарисованного круга F4.
//  6. Круг зоны F4 рисуется материалом sparesRecharge (гаечные ключи - текстура в моде), а не
//     ammoRecharge: AmmoRechargerFacet рисует круг функцией 0x120610, материал берётся из таблицы
//     0xff050 по смещению из инструкции lea r8,[rax+0x120] (0x120687). Плагин на время своего вызова
//     0x120610 подменяет смещение на 0xe0 (sparesRecharge) и перерисовывает круг.

#pragma once

#include "../core/spares.h"
#include "common.h"

namespace spares_supply {

// Engine pointer -> core handle: a spare parts component is a SparesRechargerFacet.
inline core::SparesDepot* depot(void* facet) { return static_cast<core::SparesDepot*>(facet); }

// core::SparesPort over logic.dll.
core::SparesPort& sparesPort();

// The SparesRechargerFacet of the same vehicle as facet; nullptr - none.
void* sparesOf(void* facet);
// Spare parts of recharger's vehicle follow F4 mode (core::syncSpares).
void syncSpares(void* recharger, bool on);
// One F4 frame of spare parts of recharger's vehicle (core::tickSpares); returns the spare parts component.
void* tickSpares(void* recharger, float dt);
// Redraw the HEMTT Spares zone circle with the sparesRecharge material; false - the circle function
// instruction is not writable, nothing was drawn.
bool redrawSparesCircle(void* ammo);

// RefuellerFacet hooks (slots 14 / 13 / 10): the original, then the same vehicle's spare parts follow - for a
// vehicle with refuel and support, where F4 mode switches the refueller.
void hookFuelOn(void* f);
void hookFuelOff(void* f);
void hookFuelUpdate(void* f, float dt);
// SparesRechargerFacet::update hook (slot 10): the original (resupply vehicles in the radius) + zone diagnostics.
void hookSparesUpdate(void* spares, float dt);
// SparesRechargerFacet::deactivate hook (slot 13): skipped while F4 mode runs (core::mayTurnSparesOff).
void hookSparesOff(void* spares);

}  // namespace spares_supply
