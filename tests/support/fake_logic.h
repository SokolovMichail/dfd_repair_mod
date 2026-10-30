// A fake logic.dll for the plugin install tests (plugin_install_test.cpp).
//
// When the game loads, the plugin searches logic.dll for everything it is going to patch (function resolve):
//   * five component classes by name (RTTI) - and their method tables (vtables);
//   * code locations by byte patterns - each pattern must be found exactly as many times as expected;
//   * addresses of several game functions and variables - from call/lea/movss instructions inside the code
//     already found.
// Here we build an image where all of that lies exactly where the plugin expects it. The bytes come from the
// plugin's own constants (kSig*, kPrologue*) - so the image is built from the plugin's own assumptions about
// the game code, not from the real game. The tests then spoil one detail at a time and check that the
// plugin notices.
//
// It uses the plugin's constants (common.h).
#pragma once

#include "../../plugins/spares_supply/logic_dll/common.h"
#include "fake_pe.h"

namespace fake {

using namespace spares_supply;

struct FakeLogic {
    Pe pe;

    // Classes: vtable and name address (to "remove" a class by spoiling its name).
    uintptr_t ammoVt, fuelVt, sparesVt, repairVt, modulesVt;
    uintptr_t ammoName, fuelName, sparesName, repairName, modulesName;

    // "Functions" in vtable slots: they start with the bytes the plugin expects, and contain nothing else
    // (nobody executes them).
    uintptr_t ammoOn, ammoOff, ammoUpdate, ammoCan, fuelOn, fuelOff, fuelUpdate;
    uintptr_t sparesOn, sparesOff, sparesUpdate, repairOn, repairOff, repairUpdate;

    // Code locations and data.
    uintptr_t supportSite[2];     // "support blocks ammunition_recharge": cars, tanks
    uintptr_t repairSite[2];      // "repair blocks ammunition_recharge": cars, tanks
    uintptr_t refuelSite;         // the same flag pattern for another component - the plugin leaves it alone
    uintptr_t repairString;       // the string "[Leonardo.RepairerFacet]" the lea in repairSite points to
    uintptr_t radiusLoad, radiusConst;      // spare parts target search: movss xmm2,[20.0]
    uintptr_t supplyRadiusUse, supplyRadius;  // the ammo constructor reads the global supplyRadius
    uintptr_t showCircle, circleLea, materialTable;
    uintptr_t incHealthCall, incHealth, diplomacy;

    FakeLogic() {
        ammoVt = pe.addClass("AmmoRechargerFacet@Leonardo", 20, 0, false, &ammoName);
        fuelVt = pe.addClass("RefuellerFacet@Leonardo", 20, 0, false, &fuelName);
        sparesVt = pe.addClass("SparesRechargerFacet@Leonardo", 20, 0, false, &sparesName);
        repairVt = pe.addClass("RepairerFacet@Leonardo", 20, 0, false, &repairName);
        modulesVt = pe.addClass("DamageableModulesFacet@Leonardo", 40, 0, false, &modulesName);

        // Game functions whose addresses the plugin takes from a call: no bodies needed, except incHealth's start.
        diplomacy = stub("48 89 5C 24 08 C3");
        materialTable = stub("48 8D 05 00 00 00 00 C3");
        incHealth = stub(kPrologueIncHealth);

        // Methods in the vtables.
        ammoOn = slot(ammoVt, kSlotActivate, kPrologueRechargerOn);
        ammoOff = slot(ammoVt, kSlotDeactivate, kPrologueRechargerOff);
        ammoUpdate = slot(ammoVt, kSlotUpdate, kPrologueAmmoUpdate);
        // canRecharge: its start, and at +0x3a the call to "world diplomacy".
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

        // "mov r15b,1; ...; lea rcx,[component name]" in the car and tank description builders. The lea at the
        // start of the pattern (at offset 7, length 7) points to the component's name string.
        uintptr_t sparesString = pe.putString(pe.rdata, "[Leonardo.SparesRechargerFacet]");
        repairString = pe.putString(pe.rdata, "[Leonardo.RepairerFacet]");
        uintptr_t fuelString = pe.putString(pe.rdata, "[Leonardo.RefuellerFacet]");
        for (auto& site : supportSite) {
            site = pe.put(pe.text, kSigSupportBlocksAmmo);
            Pe::aim(site + 7, 3, 7, sparesString);
        }
        for (auto& site : repairSite) site = abilityFlag(repairString);
        refuelSite = abilityFlag(fuelString);

        // Spare parts search radius: movss xmm2,[rip+disp] -> the constant 20.0 in .rdata.
        radiusConst = pe.put(pe.rdata, bytesOf(20.0f), 4);
        radiusLoad = pe.put(pe.text, kSigSparesRadiusLoad);
        Pe::aim(radiusLoad, 4, 8, radiusConst);
        // The global supplyRadius in .data and the instruction that reads it.
        supplyRadius = pe.put(pe.data, bytesOf(20.0f), 4);
        supplyRadiusUse = pe.put(pe.text, kSigSupplyRadiusUse);
        Pe::aim(supplyRadiusUse, 4, 8, supplyRadius);

        // The zone circle function: its start, the material lea at +0x77, and 12 bytes before the lea - the
        // call to the material table.
        std::vector<uint8_t> circle = hex(kSigShowCircle);
        circle.resize(0x77, 0xCC);
        auto lea = hex(kSigCircleMaterialLea);
        circle.insert(circle.end(), lea.begin(), lea.end());
        showCircle = pe.put(pe.text, circle);
        circleLea = showCircle + 0x77;
        putCall(circleLea - kMaterialTableCallBack, materialTable);

        // RepairerFacet::update: "...; call incHealth" (the call is at byte 15 of the pattern).
        incHealthCall = pe.put(pe.text, kSigIncHealthCall);
        Pe::aim(incHealthCall + 15, 1, 5, incHealth);
    }

    // Write a "call target" instruction (E8 + offset) at address at.
    static void putCall(uintptr_t at, uintptr_t target) {
        Pe::write(at, {0xE8});
        Pe::aim(at, 1, 5, target);
    }
    // Spoil the first byte - the location or function no longer matches its pattern.
    static void spoil(uintptr_t at) { Pe::write(at, {0x00}); }
    // Put another copy of a pattern into the code (so it is found twice). Returns the copy's address.
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
