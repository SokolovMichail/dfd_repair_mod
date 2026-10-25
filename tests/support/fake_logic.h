// Поддельная logic.dll для тестов установки плагина (plugin_install_test.cpp).
//
// Плагин при загрузке игры ищет в logic.dll всё, что собирается править (функция resolve):
//   * пять классов компонентов по имени (RTTI) - и их таблицы методов (vtable);
//   * места в коде по шаблонам байтов - каждый шаблон должен найтись ровно столько раз, сколько ожидается;
//   * адреса нескольких функций и переменных игры - по инструкциям call/lea/movss внутри найденного кода.
// Здесь собирается образ, где всё это лежит ровно так, как ожидает плагин. Байты берутся из констант
// самого плагина (kSig*, kPrologue*) - то есть образ построен по его собственным предположениям о коде
// игры, а не по настоящей игре. Тесты потом портят по одной детали и смотрят, что плагин это заметил.
//
// Подключается ПОСЛЕ spares_supply.cpp (нужны его константы).
#pragma once

#include "fake_pe.h"

namespace fake {

struct FakeLogic {
    Pe pe;

    // Классы: vtable и адрес имени (чтобы «удалить» класс, испортив имя).
    uintptr_t ammoVt, fuelVt, sparesVt, repairVt, modulesVt;
    uintptr_t ammoName, fuelName, sparesName, repairName, modulesName;

    // «Функции» в слотах vtable: начинаются с ожидаемых плагином байтов, больше ничего в них нет
    // (их никто не выполняет).
    uintptr_t ammoOn, ammoOff, ammoUpdate, ammoCan, fuelOn, fuelOff, fuelUpdate;
    uintptr_t sparesOn, sparesOff, sparesUpdate, repairOn, repairOff, repairUpdate;

    // Места в коде и данные.
    uintptr_t supportSite[2];     // «support блокирует ammunition_recharge»: машины, танки
    uintptr_t repairSite[2];      // «repair блокирует ammunition_recharge»: машины, танки
    uintptr_t refuelSite;         // тот же шаблон флага, но для другого компонента - плагин его не трогает
    uintptr_t repairString;       // строка "[Leonardo.RepairerFacet]", на неё указывают lea в repairSite
    uintptr_t radiusLoad, radiusConst;      // поиск целей запчастей: movss xmm2,[20.0]
    uintptr_t supplyRadiusUse, supplyRadius;  // конструктор боеприпасов читает глобальный supplyRadius
    uintptr_t showCircle, circleLea, materialTable;
    uintptr_t incHealthCall, incHealth, diplomacy;

    FakeLogic() {
        ammoVt = pe.addClass("AmmoRechargerFacet@Leonardo", 20, 0, false, &ammoName);
        fuelVt = pe.addClass("RefuellerFacet@Leonardo", 20, 0, false, &fuelName);
        sparesVt = pe.addClass("SparesRechargerFacet@Leonardo", 20, 0, false, &sparesName);
        repairVt = pe.addClass("RepairerFacet@Leonardo", 20, 0, false, &repairName);
        modulesVt = pe.addClass("DamageableModulesFacet@Leonardo", 40, 0, false, &modulesName);

        // Функции игры, адреса которых плагин достаёт из call: тела не нужны, кроме начала incHealth.
        diplomacy = stub("48 89 5C 24 08 C3");
        materialTable = stub("48 8D 05 00 00 00 00 C3");
        incHealth = stub(kPrologueIncHealth);

        // Методы в vtable.
        ammoOn = slot(ammoVt, kSlotActivate, kPrologueRechargerOn);
        ammoOff = slot(ammoVt, kSlotDeactivate, kPrologueRechargerOff);
        ammoUpdate = slot(ammoVt, kSlotUpdate, kPrologueAmmoUpdate);
        // canRecharge: начало, а на +0x3a - вызов «дипломатии мира».
        std::vector<uint8_t> can = hex(kPrologueAmmoCan);
        can.resize(kDiplomacyCallAt, 0xCC);
        auto call = hex(kSigDiplomacyCall);
        can.insert(can.end(), call.begin(), call.end());
        ammoCan = pe.put(pe.text, can);
        Pe::aim(ammoCan + kDiplomacyCallAt + 6, 1, 5, diplomacy);
        Pe::setSlot(ammoVt, kSlotCanRecharge, ammoCan);
        fuelOn = slot(fuelVt, kSlotActivate, kPrologueRechargerOn);
        fuelOff = slot(fuelVt, kSlotDeactivate, kPrologueRechargerOff);
        fuelUpdate = slot(fuelVt, kSlotUpdate, kPrologueFuelUpdate);
        sparesOn = slot(sparesVt, kSlotActivate, kPrologueSparesOn);
        sparesOff = slot(sparesVt, kSlotDeactivate, kPrologueSparesOff);
        sparesUpdate = slot(sparesVt, kSlotUpdate, kPrologueSparesUpdate);
        repairOn = slot(repairVt, kSlotActivate, kPrologueRepairOn);
        repairOff = slot(repairVt, kSlotDeactivate, kPrologueRepairOff);
        repairUpdate = slot(repairVt, kSlotUpdate, kPrologueRepairUpdate);

        // «mov r15b,1; ...; lea rcx,[имя компонента]» в сборщиках описаний машин и танков. lea в начале
        // шаблона (со смещения 7, длина 7) указывает на строку-имя компонента.
        uintptr_t sparesString = pe.putString(pe.rdata, "[Leonardo.SparesRechargerFacet]");
        repairString = pe.putString(pe.rdata, "[Leonardo.RepairerFacet]");
        uintptr_t fuelString = pe.putString(pe.rdata, "[Leonardo.RefuellerFacet]");
        for (auto& site : supportSite) {
            site = pe.put(pe.text, kSigSupportBlocksAmmo);
            Pe::aim(site + 7, 3, 7, sparesString);
        }
        for (auto& site : repairSite) site = abilityFlag(repairString);
        refuelSite = abilityFlag(fuelString);

        // Радиус поиска целей запчастей: movss xmm2,[rip+disp] -> константа 20.0 в .rdata.
        radiusConst = pe.put(pe.rdata, bytesOf(20.0f), 4);
        radiusLoad = pe.put(pe.text, kSigSparesRadiusLoad);
        Pe::aim(radiusLoad, 4, 8, radiusConst);
        // Глобальный supplyRadius в .data и инструкция, которая его читает.
        supplyRadius = pe.put(pe.data, bytesOf(20.0f), 4);
        supplyRadiusUse = pe.put(pe.text, kSigSupplyRadiusUse);
        Pe::aim(supplyRadiusUse, 4, 8, supplyRadius);

        // Функция круга зоны: её начало, на +0x77 - lea материала, за 12 байт до lea - call таблицы материалов.
        std::vector<uint8_t> circle = hex(kSigShowCircle);
        circle.resize(0x77, 0xCC);
        auto lea = hex(kSigCircleMaterialLea);
        circle.insert(circle.end(), lea.begin(), lea.end());
        showCircle = pe.put(pe.text, circle);
        circleLea = showCircle + 0x77;
        putCall(circleLea - kMaterialTableCallBack, materialTable);

        // RepairerFacet::update: «...; call incHealth» (call - с 15-го байта шаблона).
        incHealthCall = pe.put(pe.text, kSigIncHealthCall);
        Pe::aim(incHealthCall + 15, 1, 5, incHealth);
    }

    // Вписать по адресу at инструкцию «call target» (E8 + смещение).
    static void putCall(uintptr_t at, uintptr_t target) {
        Pe::write(at, {0xE8});
        Pe::aim(at, 1, 5, target);
    }
    // Испортить первый байт - место или функция больше не совпадает со своим шаблоном.
    static void spoil(uintptr_t at) { Pe::write(at, {0x00}); }
    // Положить в код ещё одну копию шаблона (чтобы он нашёлся дважды). Возвращает её адрес.
    uintptr_t duplicate(const char* pattern) { return pe.put(pe.text, pattern); }

private:
    uintptr_t stub(const char* prologue) { return pe.put(pe.text, prologue); }
    uintptr_t slot(uintptr_t vt, int index, const char* prologue) {
        uintptr_t f = stub(prologue);
        Pe::setSlot(vt, index, f);
        return f;
    }
    uintptr_t abilityFlag(uintptr_t nameString) {
        uintptr_t site = pe.put(pe.text, kSigAbilityFlag);
        Pe::aim(site + 7, 3, 7, nameString);
        return site;
    }
};

}  // namespace fake
