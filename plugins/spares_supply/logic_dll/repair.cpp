// SparesSupply.dll, HEMTT Repair part. Overview - repair.h.

#include "repair.h"

namespace spares_supply {

namespace {

// Пересоздать круг с материалом path вместо ammoRecharge: на время одного вызова 0x120610 строка
// ammoRecharge в таблице материалов подменяется и сразу возвращается обратно.
void showCircleWithPath(void* ammo, const char* path) {
    auto table = reinterpret_cast<char* (*)()>(g_a.materialTable)();  // заодно гарантирует инициализацию
    auto entry = reinterpret_cast<MsvcString*>(table + kMaterialAmmo);
    MsvcString saved = *entry;
    size_t n = strlen(path);
    *entry = MsvcString{path, {}, n, n};
    reinterpret_cast<FacetFn>(g_a.showCircle)(ammo);
    *entry = saved;
}

// Можно ли чинить target машине owner: свой или союзник - та же проверка, что в
// AmmoRechargerFacet::canRecharge. Дипломатия мира (0x541a00) сравнивает команды цели и владельца;
// отношение 2 или 4 - нельзя. Цель без команды (или нет дипломатии) - тоже нельзя.
bool friendly(void* owner, void* target) {
    void* dip = reinterpret_cast<DiplomacyFn>(g_a.diplomacy)(vcall<void*>(owner, 0x78));
    if (!dip) return false;
    auto teamOf = [](void* e) { return *reinterpret_cast<void**>(vcall<char*>(e, 0xb0) + 0x20); };
    void* ot = teamOf(owner);
    void* tt = teamOf(target);
    if (!ot || !tt) return false;
    int rel = vcall<int>(dip, 0xb0, vcall<int>(tt, 0x50), vcall<int>(ot, 0x50));
    return rel != 2 && rel != 4;
}

// Слои брони одной стороны модуля: текущая толщина (vector<float>) и описание (по kArmorLayerSize байт).
struct ArmorSide {
    float* cur;
    const char* lay;
    size_t n;
};
// Слои брони стороны side (0..5) модуля mod с описанием desc. n - сколько слоёв есть и в текущей
// толщине, и в описании (на случай, если длины векторов почему-то разошлись).
ArmorSide armorSide(char* mod, const char* desc, int side) {
    auto cur = reinterpret_cast<float**>(mod + kModuleArmor + side * 0x18);         // begin, end
    auto lay = reinterpret_cast<char* const*>(desc + kDescArmor + side * 0x18);   // begin, end
    size_t n = std::min(static_cast<size_t>(cur[1] - cur[0]), static_cast<size_t>(lay[1] - lay[0]) / kArmorLayerSize);
    return {cur[0], lay[0], n};
}

}  // namespace

// Запас запчастей машины - int в её DamageableModulesFacet ([+0x130]); ссылка, чтобы можно было менять.
int& sparesOfModules(void* modules) {
    return *reinterpret_cast<int*>(static_cast<char*>(modules) + kModulesSpares);
}

// Один импульс ремонта одной машины (modules - её DamageableModulesFacet) за счёт склада stock:
//  - каждому повреждённому модулю +kRepairFraction от максимума (не больше недостающего и не больше
//    остатка склада) через движковый incHealth; запчасти, которые incHealth списал у цели, возвращаем;
//  - каждому изношенному слою брони +kArmorFraction исходной толщины (не выше исходной), по kSparesPerMm
//    запчастей за миллиметр.
// Уничтоженную машину (общее здоровье 0) не трогает. Останавливается, когда склад пуст.
// Возвращает, сколько здоровья и слоёв восстановлено и сколько запчастей потрачено.
Repaired repairVehicle(void* modules, Stock stock) {
    Repaired r;
    auto m = static_cast<char*>(modules);
    if (*reinterpret_cast<int*>(m + kModulesTotalHealth) <= 0) return r;  // уничтожена
    char* begin = *reinterpret_cast<char**>(m + kModulesBegin);
    char* end = *reinterpret_cast<char**>(m + kModulesEnd);
    for (size_t i = 0; begin && begin + (i + 1) * kModuleSize <= end && stock.spares > 0; ++i) {
        char* mod = begin + i * kModuleSize;
        char* desc = *reinterpret_cast<char**>(mod + kModuleDesc);
        if (!desc) continue;
        auto& hp = *reinterpret_cast<int*>(mod + kModuleHealth);
        int maxHp = *reinterpret_cast<int*>(desc + kDescMaxHealth);
        if (hp < maxHp) {
            int add = std::min({std::max(1, static_cast<int>(maxHp * kRepairFraction)), maxHp - hp, stock.spares});
            // incHealth - ремонт экипажем: списывает запчасти у самой цели. Им платит HEMTT Repair - вернуть.
            int targetSpares = sparesOfModules(modules);
            int before = hp;
            reinterpret_cast<IncHealthFn>(g_a.incHealth)(modules, i, add);
            sparesOfModules(modules) = targetSpares;
            int gained = hp - before;
            stock.pay(static_cast<float>(gained), r);
            r.health += gained;
        }
        for (int side = 0; side < kArmorSides && stock.spares > 0; ++side) {
            ArmorSide s = armorSide(mod, desc, side);
            for (size_t k = 0; k < s.n; ++k) {
                float full = *reinterpret_cast<const float*>(s.lay + k * kArmorLayerSize);
                float& t = s.cur[k];
                if (t >= full || full <= 0) continue;
                float next = std::min(full, t + full * kArmorFraction);
                stock.pay((next - t) * kSparesPerMm, r);
                t = next;
                ++r.layers;
            }
        }
    }
    return r;
}

std::unordered_map<void*, float> g_repairTimers;  // компонент боеприпасов -> секунд до следующего импульса
std::unordered_map<void*, float> g_armorDebt;     // компонент боеприпасов -> недосписанная цена брони
int g_repairLogs = 0;

// Режим F4 у HEMTT Repair (вызывается каждый кадр из hookAmmoUpdate): раз в kRepairPeriod секунд -
// импульс ремонта по машинам, которые компонент боеприпасов собрал в зоне ([ammo+0x30]). Пропускает
// саму HEMTT Repair, чужих и машины без модулей (пехоту). Платит запчастями самой HEMTT Repair;
// склад пуст - ничего не делает. Первые 40 импульсов с результатом пишет в лог.
void tickRepair(void* ammo, float dt) {
    float& left = g_repairTimers[ammo];
    left -= dt;
    if (left > 0) return;
    left = kRepairPeriod;

    void* owner = ownerOf(ammo);
    void* own = findFacet(owner, g_a.modulesVt);  // запчасти лежат в модулях самой HEMTT Repair
    if (!own || sparesOfModules(own) <= 0) return;
    Stock stock{sparesOfModules(own), g_armorDebt[ammo]};
    auto list = reinterpret_cast<RefBlock***>(static_cast<char*>(ammo) + kAmmoTargets);  // begin, end
    for (RefBlock** p = list[0]; p && p < list[1] && stock.spares > 0; ++p) {
        void* target = *p ? (*p)->object : nullptr;
        if (!target || target == owner || !friendly(owner, target)) continue;
        void* modules = findFacet(target, g_a.modulesVt);
        if (!modules) continue;
        Repaired r = repairVehicle(modules, stock);
        if ((r.health || r.layers) && g_repairLogs < 40) {
            ++g_repairLogs;
            g_log("repair: target=%p +%d hp, %d armor layers, total hp %d; spent %d spares, %d left", target,
                  r.health, r.layers, *reinterpret_cast<int*>(static_cast<char*>(modules) + kModulesTotalHealth),
                  r.spent, stock.spares);
        }
    }
    if (stock.spares <= 0) trace("repair: out of spare parts", ammo);
}

void forgetRepair(void* ammo) {
    g_repairTimers.erase(ammo);
    g_armorDebt.erase(ammo);
}

void redrawRepairCircle(void* ammo) { showCircleWithPath(ammo, kRepairMaterial); }

// RepairerFacet тоже IRechargerFacet и создаётся раньше боеприпасов, поэтому режим F4 у HEMTT Repair
// включает/выключает именно его. Флаги ремонта ставим как обычно (режим F4 спрашивает isActive), а
// включение дублируем на боеприпасы той же машины: они рисуют круг и собирают цели для tickRepair.
void hookRepairOn(void* r) {
    g_origRepairOn(r);
    void* ammo = findFacet(ownerOf(r), g_a.ammoVt);
    if (ammo && !isOn(ammo, kRechargerOffFlag)) hookAmmoOn(ammo);
}
void hookRepairOff(void* r) {
    g_origRepairOff(r);
    void* ammo = findFacet(ownerOf(r), g_a.ammoVt);
    if (ammo && isOn(ammo, kRechargerOffFlag)) {
        trace("repair OFF (with supply mode)", ammo);
        hookAmmoOff(ammo);
    }
}
// Свой update ремонта недоделан (ищет цели и выбрасывает результат) - не выполняем его вовсе.
void hookRepairUpdate(void*, float) {}

}  // namespace spares_supply
