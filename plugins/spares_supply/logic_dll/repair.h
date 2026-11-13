// SparesSupply.dll, HEMTT Repair part (repair.cpp). Shared parts - common.h.
//
// HEMTT Repair (abilities=ammunition_recharge,repair; the RepairerFacet is only a marker, it repairs nothing):
//  7. Removes the "repair blocks ammunition_recharge" ban (in both builders) - the F4 button.
//  8. Ammo works as for HEMTT Spares (item 4); HEMTT Repair has a stock of 0 too.
//     The RepairerFacet is itself an IRechargerFacet (IRepairerFacet inherits it) and is created before the ammo
//     component, so F4 mode switches it on and off. Repair slots 14/13 are hooked: the flags are set as usual,
//     and switching on is forwarded to the ammo component. The repair component's own update (slot 10) is
//     stubbed out.
//  9. While F4 mode is on, every kRepairPeriod it takes the vehicles the ammo component has already collected in
//     its zone (the list [ammo+0x30], refreshed in its update), drops itself and enemies (the same diplomacy check
//     as in canRecharge: relation 2 or 4 - no) and for each of them:
//       - repairs every damaged module by kRepairFraction of its maximum through
//         DamageableModulesFacet::incHealth (0x225ba0) - with total health and module state recalculated;
//       - adds kArmorFraction of the original thickness to every armor layer (not above the original).
//     Pays with its own spare parts ([modules+0x130]): 1 per 1 health, kSparesPerMm per 1 mm of armor; the spare
//     parts incHealth (crew repair) takes from the target are given back to it. Stock ran out - repair stops.
//     Modules: [facet+0xd0..+0xd8], 0x148 bytes each: +0x00 description, +0x0c health, +0x28 + side*0x18 -
//     current layer thickness (vector<float>), 6 sides. Description: +0x28 max health,
//     +0x90 + side*0x18 - armor layers (16 bytes each, +0 original thickness). See DamageableModulesFacet::
//     initialize 0x226280 and the damage function 0x310790 (armor wear).
// 10. The zone circle is drawn with materials/rounds/repairRecharge.material (in the mod): for the time of the
//     0x120610 call the ammoRecharge row in the material table is swapped with the repair path and put back.

#pragma once

#include <unordered_map>

#include "../core/repair.h"
#include "common.h"

namespace spares_supply {

constexpr const char* kRepairMaterial = "materials/rounds/repairRecharge.material";

// DamageableModulesFacet
constexpr size_t kModulesBegin = 0xd0, kModulesEnd = 0xd8, kModuleSize = 0x148, kModulesTotalHealth = 0xcc;
constexpr size_t kModulesSpares = 0x130;  // the vehicle's spare parts (int; maximum at +0x12c), vtable slots 26-29
constexpr size_t kModuleDesc = 0x00, kModuleHealth = 0x0c, kModuleArmor = 0x28;     // in the module
constexpr size_t kDescMaxHealth = 0x28, kDescArmor = 0x90, kArmorLayerSize = 0x10;  // in the module description
constexpr int kArmorSides = 6;

// MSVC std::string (x64): buffer/pointer, size, capacity; with capacity >= 16 the text is behind the pointer.
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
// The material table row back to what it held - after a fault during redrawRepairCircle; nothing if no redraw
// was in progress.
void restoreRepairCircle();

// RepairerFacet hooks (slots 14 / 13 / 10). F4 mode on HEMTT Repair switches the repair component (it comes
// before ammo), so on / off are forwarded to the ammo component, which draws the circle and collects the zone;
// the engine's own unfinished repair update is not run at all.
void hookRepairOn(void* r);
void hookRepairOff(void* r);
void hookRepairUpdate(void*, float);

}  // namespace spares_supply
