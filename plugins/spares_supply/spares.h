// SparesSupply.dll, HEMTT Spares part (spares.cpp). Shared parts - common.h.
//
// HEMTT Spares (abilities=ammunition_recharge,support):
//  1. Убирает запрет «support блокирует ammunition_recharge» (оба сборщика) - машина получает оба
//     компонента и кнопку F4.
//  2. Включение/выключение заправщика и боеприпасов (слоты 14/13) дублируется на компонент
//     запчастей той же машины; из их апдейтов (пока включены, т.е. идёт режим F4) тикаются запчасти.
//  3. Выключение компонента запчастей (слот 13) пропускается, пока у машины активен заправщик или
//     боеприпасы - иначе приказ «пополнить запчасти» по клику выключил бы раздачу по окончании.
//  4. Такая машина не раздаёт патроны: AmmoRechargerFacet::canRecharge(target) (слот 17, +0x88)
//     отвечает «нет». Через него идут и раздача по зоне (update 0x120b90), и приказ по клику
//     (AiFacet canAmmo 0xcdcf0). Компонент боеприпасов нужен ей только ради кнопки F4.
//  5. Радиус поиска целей у запчастей (0x439bc0, зашитые 20 м) заменён на глобальный supplyRadius -
//     тот же, что у боеприпасов/заправщика и у нарисованного круга F4.
//  6. Круг зоны F4 рисуется материалом sparesRecharge (гаечные ключи - текстура в моде), а не
//     ammoRecharge: AmmoRechargerFacet рисует круг функцией 0x120610, материал берётся из таблицы
//     0xff050 по смещению из инструкции lea r8,[rax+0x120] (0x120687). Плагин на время своего вызова
//     0x120610 подменяет смещение на 0xe0 (sparesRecharge) и перерисовывает круг.

#pragma once

#include "common.h"

namespace spares_supply {

void* sparesOf(void* facet);
void syncSpares(void* recharger, bool on);
void* tickSpares(void* recharger, float dt);
// Redraw the HEMTT Spares zone circle with the sparesRecharge material; false - the circle function
// instruction is not writable, nothing was drawn.
bool redrawSparesCircle(void* ammo);

void hookFuelOn(void* f);
void hookFuelOff(void* f);
void hookFuelUpdate(void* f, float dt);
void hookSparesUpdate(void* spares, float dt);
void hookSparesOff(void* spares);

}  // namespace spares_supply
