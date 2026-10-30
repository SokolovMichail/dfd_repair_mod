// SparesSupply.dll, shared part: engine object model, F4 mode on the ammo component, zone circle,
// finding addresses in logic.dll and installing the patches. The DLL entry point is in standalone.cpp.
// Overview - common.h.

#include "common.h"

#include "repair.h"
#include "spares.h"

namespace spares_supply {

Addr g_a{};

// Отпустить сильную ссылку, полученную от движка (как делает он сам): уменьшить счётчик и, если он
// дошёл до нуля, вызвать освобождение (слот +0x10 блока).
void release(RefBlock* rb) {
    if (rb && --rb->strong == 0) vcall<void>(rb, 0x10);
}

Logger g_log;

// Format the line once, then hand it to the sink or the file. Lines longer than the buffer are cut.
void Logger::operator()(const char* fmt, ...) {
    char line[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (sink_)
        sink_(line);
    else
        file_("%s", line);
}

int g_events = 0;  // первые события пишем в лог для проверки

FacetFn g_origAmmoOn, g_origAmmoOff, g_origFuelOn, g_origFuelOff, g_origSparesOn, g_origSparesOff;
FacetFn g_origRepairOn, g_origRepairOff;
CanRechargeFn g_origAmmoCanRecharge;
UpdateFn g_origSparesUpdate, g_origAmmoUpdate, g_origFuelUpdate, g_origRepairUpdate;

// Машина, которой принадлежит компонент: блок ссылки в [facet+0x10] -> объект. nullptr, если нет.
void* ownerOf(void* facet) {
    auto rb = *reinterpret_cast<RefBlock**>(static_cast<char*>(facet) + 0x10);
    return rb ? rb->object : nullptr;
}

// Включён ли компонент-раздатчик: у них байт «выключен» по смещению offFlag (kRechargerOffFlag /
// kSparesOffFlag), включён - когда он равен 0. Так же устроен их слот 12 isActive.
bool isOn(void* facet, size_t offFlag) {
    return *(static_cast<uint8_t*>(facet) + offFlag) == 0;
}

// Найти у машины компонент точного класса (сравнением адреса vtable с vtbl) - так же, как это делает
// logic.dll в 0x57490 / 0xce070. Возвращает компонент или nullptr (нет машины / нет такого компонента).
// entity->vfunc(+0x58) отдаёт итератор: +0x18 конец?, +0x30 текущий (блок ссылки), +0x20 следующий,
// +0x00 удаляющий деструктор. Итератор освобождаем сами.
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

Kind kindOf(void* ammo) {
    void* owner = ownerOf(ammo);
    if (findFacet(owner, g_a.sparesVt)) return Kind::Spares;
    if (findFacet(owner, g_a.repairVt)) return Kind::Repair;
    return Kind::Ammo;
}

// Записать событие в лог (только первые 50 за запуск, чтобы лог не разрастался): что случилось,
// с каким компонентом и какой машиной.
void trace(const char* what, void* facet) {
    if (g_events < 50) {
        ++g_events;
        g_log("%s facet=%p owner=%p", what, facet, ownerOf(facet));
    }
}

// ---------------------------------------------------------------- круг зоны

// Круги, уже перерисованные своим материалом.
std::unordered_set<void*> g_ownCircles;
bool g_circleWritable = false;

// Объект нарисованного круга зоны у компонента боеприпасов ([ammo+0x50]); nullptr, если круга нет.
void* circleOf(void* ammo) {
    return *reinterpret_cast<void**>(static_cast<char*>(ammo) + kAmmoCircleField);
}

// Если у компонента боеприпасов есть круг, ещё нарисованный материалом патронов, - пересоздать его
// материалом машины мода: Spares - подменой смещения в lea (setCircleMaterial), Repair - подменой
// строки в таблице (showCircleWithPath). Новые круги запоминаются в g_ownCircles, чтобы не
// перерисовывать их каждый кадр. Для обычных машин (Kind::Ammo) ничего не делает.
void convertCircle(void* ammo, Kind kind) {
    void* c = circleOf(ammo);
    if (!c || kind == Kind::Ammo || g_ownCircles.count(c)) return;
    if (kind == Kind::Spares) {
        if (!redrawSparesCircle(ammo)) return;
    } else {
        redrawRepairCircle(ammo);
    }
    if (void* n = circleOf(ammo)) g_ownCircles.insert(n);
    trace(kind == Kind::Spares ? "supply circle -> spares material" : "supply circle -> repair material", ammo);
}

// ---------------------------------------------------------------- перехваты

namespace {

// What core's common F4 rules need, taken from logic.dll. A station is an AmmoRechargerFacet: the component
// F4 mode switches on HEMTT Supply and HEMTT Spares, and the one HEMTT Repair's hooks forward F4 mode to.
class LogicDllSupplyPort final : public core::SupplyPort {
public:
    // By the components the vehicle has: spare parts first, then repair (kindOf).
    Kind kind(core::Station* s) override { return kindOf(s); }
    // "Off" flag, byte +0x59; 0 means switched on.
    bool isOn(core::Station* s) override { return spares_supply::isOn(s, kRechargerOffFlag); }
    // Redraws the circle with our material once and remembers it in g_ownCircles (convertCircle).
    void showCircle(core::Station* s, Kind kind) override { convertCircle(s, kind); }
    // Events go through trace(): the first 50 per run, with the facet and owner addresses.
    void event(const char* what, core::Station* s) override { trace(what, s); }
};

}  // namespace

// One set of ports for the whole plugin. Built on first use (the first F4 event), after the plugin's globals
// exist; the ports keep no state, the repair state lives in g_repair (repair.cpp).
core::Ports& ports() {
    static LogicDllSupplyPort supply;
    static core::Ports p{supply, sparesPort(), repairPort(), g_repair};
    return p;
}

// Перехват AmmoRechargerFacet::activate (слот 14) - режим F4 включён. Сначала оригинал (включает
// компонент и рисует круг патронов), потом: у HEMTT Spares включить запчасти, у машин мода -
// перерисовать круг своим материалом.
void hookAmmoOn(void* f) {
    g_origAmmoOn(f);
    // the mod's part - core::onSupplyOn (core/supply.cpp)
    core::onSupplyOn(ports(), station(f));
}
// Перехват AmmoRechargerFacet::deactivate (слот 13) - режим F4 выключен. Забываем круг и состояние
// ремонта этого компонента, вызываем оригинал (убирает круг), у HEMTT Spares выключаем запчасти.
void hookAmmoOff(void* f) {
    // before the original: it removes the circle, and [ammo+0x50] would already be empty
    if (void* c = circleOf(f)) g_ownCircles.erase(c);
    g_origAmmoOff(f);
    // the mod's part - core::onSupplyOff: forget the repair state, switch spare parts off
    core::onSupplyOff(ports(), station(f));
}

// Перехват AmmoRechargerFacet::update (слот 10), движок вызывает его каждый кадр. Оригинал раздаёт
// патроны (у машин мода canRecharge отвечает «нет») и обновляет список машин в зоне. Пока режим F4
// включён: у HEMTT Spares тикаем запчасти, у HEMTT Repair - ремонт; круг, созданный без нас (например,
// при загрузке сохранения), тоже перекрашиваем.
void hookAmmoUpdate(void* f, float dt) {
    g_origAmmoUpdate(f, dt);
    // the mod's part - core::onSupplyTick (does nothing while F4 mode is off)
    core::onSupplyTick(ports(), station(f), dt);
}

// Перехват AmmoRechargerFacet::canRecharge (слот 17): можно ли дать патроны target. Машины мода патроны
// не раздают никогда - ни по зоне, ни по приказу; остальным - решает оригинал.
bool hookAmmoCanRecharge(void* ammo, void* target) {
    // the rule - core::handsOutAmmo; the same answer for the zone and for the click order
    if (!core::handsOutAmmo(kindOf(ammo))) return false;
    return g_origAmmoCanRecharge(ammo, target);
}

// ---------------------------------------------------------------- поиск и установка

namespace {

// Короткое имя для Image::ripTarget: адрес, на который ссылается rip-относительный операнд инструкции.
uintptr_t ripTarget(uintptr_t insn, size_t dispAt, size_t len) {
    return Image::ripTarget(insn, dispAt, len);
}

// Найти байтовый шаблон sig в коде; он должен встретиться ровно один раз. Иначе пишет в лог, сколько
// совпадений (what - имя для лога), и возвращает 0.
uintptr_t findOne(const Image& img, const char* what, const char* sig) {
    auto hits = img.find(sig);
    if (hits.size() == 1) return hits[0];
    g_log("signature '%s': %zu matches (need 1)", what, hits.size());
    return 0;
}

}  // namespace

// Найти в logic.dll всё, что нужно плагину, и заполнить g_a: vtable компонентов по RTTI, места кода по
// шаблонам, адреса вызываемых функций движка - из инструкций call внутри уже найденного кода.
// Всё найденное пишет в лог. Возвращает true, только если нашлось всё до единого.
bool resolve(const Image& img) {
    g_a.ammoVt = img.vtableOf("AmmoRechargerFacet@Leonardo");
    g_a.fuelVt = img.vtableOf("RefuellerFacet@Leonardo");
    g_a.sparesVt = img.vtableOf("SparesRechargerFacet@Leonardo");
    g_a.repairVt = img.vtableOf("RepairerFacet@Leonardo");
    g_a.modulesVt = img.vtableOf("DamageableModulesFacet@Leonardo");
    auto rva = [&](uintptr_t a) { return a ? img.rva(a) : 0u; };
    g_log("vtables: ammo %08X fuel %08X spares %08X repair %08X modules %08X", rva(g_a.ammoVt), rva(g_a.fuelVt),
          rva(g_a.sparesVt), rva(g_a.repairVt), rva(g_a.modulesVt));

    g_a.supportBlocksAmmo = img.find(kSigSupportBlocksAmmo);
    if (g_a.supportBlocksAmmo.size() != 2)
        g_log("signature 'support blocks ammo': %zu matches (need 2)", g_a.supportBlocksAmmo.size());
    if (uintptr_t name = img.findString("[Leonardo.RepairerFacet]"))
        for (uintptr_t hit : img.find(kSigAbilityFlag))
            if (ripTarget(hit + 7, 3, 7) == name) g_a.repairBlocksAmmo.push_back(hit);
    if (g_a.repairBlocksAmmo.size() != 2)
        g_log("signature 'repair blocks ammo': %zu matches (need 2)", g_a.repairBlocksAmmo.size());

    g_a.radiusLoad = findOne(img, "spares radius load", kSigSparesRadiusLoad);
    if (uintptr_t use = findOne(img, "supplyRadius use", kSigSupplyRadiusUse)) g_a.supplyRadius = ripTarget(use, 4, 8);

    g_a.showCircle = findOne(img, "ammo show circle", kSigShowCircle);
    g_a.circleLea = findOne(img, "circle material lea", kSigCircleMaterialLea);
    // lea должна лежать внутри функции круга (в GOG-сборке - +0x77)
    if (g_a.circleLea && g_a.showCircle && (g_a.circleLea < g_a.showCircle || g_a.circleLea > g_a.showCircle + 0x100)) {
        g_log("circle material lea is not inside the circle function");
        g_a.circleLea = 0;
    }
    if (g_a.circleLea && *reinterpret_cast<const uint8_t*>(g_a.circleLea - kMaterialTableCallBack) == 0xE8)
        g_a.materialTable = ripTarget(g_a.circleLea - kMaterialTableCallBack, 1, 5);
    else
        g_log("material table call not found");

    if (uintptr_t call = findOne(img, "incHealth call", kSigIncHealthCall)) {
        g_a.incHealth = ripTarget(call + 15, 1, 5);
        if (!Image::matches(g_a.incHealth, kPrologueIncHealth)) {
            g_log("incHealth: unexpected prologue");
            g_a.incHealth = 0;
        }
    }
    if (g_a.ammoVt) {
        uintptr_t can = *reinterpret_cast<uintptr_t*>(g_a.ammoVt + 8 * kSlotCanRecharge);
        if (Image::matches(can, kPrologueAmmoCan) && Image::matches(can + kDiplomacyCallAt, kSigDiplomacyCall))
            g_a.diplomacy = ripTarget(can + kDiplomacyCallAt + 6, 1, 5);
        else
            g_log("diplomacy call in canRecharge not found");
    }
    g_log("functions: incHealth %08X diplomacy %08X material table %08X", rva(g_a.incHealth), rva(g_a.diplomacy),
          rva(g_a.materialTable));

    return g_a.ammoVt && g_a.fuelVt && g_a.sparesVt && g_a.repairVt && g_a.modulesVt &&
           g_a.supportBlocksAmmo.size() == 2 && g_a.repairBlocksAmmo.size() == 2 && g_a.radiusLoad &&
           g_a.supplyRadius && g_a.showCircle && g_a.circleLea && g_a.materialTable && g_a.incHealth &&
           g_a.diplomacy;
}

// Находит адреса (resolve) и отдаёт все правки p одним набором: всё или ничего. Не нашлось или не
// совпало хоть что-то - не меняет ничего, пишет причину в лог.
bool install(const Image& img, Patcher& p) {
    uintptr_t base = img.base();
    g_log("logic.dll at %p (TimeDateStamp %08X, SizeOfImage %08X)", reinterpret_cast<void*>(base),
          img.timeDateStamp(), img.sizeOfImage());
    if (!resolve(img)) {
        g_log("this logic.dll build is not supported - nothing patched");
        return false;
    }

    const std::vector<uint8_t> movR15b1{0x41, 0xB7, 0x01}, nop3{0x90, 0x90, 0x90};
    p.bytes("cars: support no longer blocks ammunition_recharge", g_a.supportBlocksAmmo[0], movR15b1, nop3);
    p.bytes("tanks: support no longer blocks ammunition_recharge", g_a.supportBlocksAmmo[1], movR15b1, nop3);
    p.bytes("cars: repair no longer blocks ammunition_recharge", g_a.repairBlocksAmmo[0], movR15b1, nop3);
    p.bytes("tanks: repair no longer blocks ammunition_recharge", g_a.repairBlocksAmmo[1], movR15b1, nop3);
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
    p.vslot("RepairerFacet::activate", g_a.repairVt, kSlotActivate, kPrologueRepairOn, &hookRepairOn, &g_origRepairOn);
    p.vslot("RepairerFacet::deactivate", g_a.repairVt, kSlotDeactivate, kPrologueRepairOff, &hookRepairOff,
            &g_origRepairOff);
    p.vslot("RepairerFacet::update (disabled)", g_a.repairVt, kSlotUpdate, kPrologueRepairUpdate, &hookRepairUpdate,
            &g_origRepairUpdate);
    // активацию запчастей не перехватываем - только проверяем и запоминаем исходную функцию
    uintptr_t sparesOn = *reinterpret_cast<uintptr_t*>(g_a.sparesVt + 8 * kSlotActivate);
    if (!Image::matches(sparesOn, kPrologueSparesOn)) {
        g_log("verify FAILED: SparesRechargerFacet::activate - nothing patched");
        return false;
    }
    g_origSparesOn = reinterpret_cast<FacetFn>(sparesOn);
    const std::vector<uint8_t> leaAmmo{0x4C, 0x8D, 0x80, 0x20, 0x01, 0x00, 0x00};
    p.bytes("supply circle material lea (verify only)", g_a.circleLea, leaAmmo, leaAmmo);
    return p.apply();
}

// The circle material lea is rewritten while the game runs (redrawSparesCircle), so its 7 bytes are made
// writable once, after the patches are in. Fails - g_circleWritable stays false and spares circles keep the
// ammo icons; everything else works.
void unprotectCircle() {
    DWORD old;
    g_circleWritable = VirtualProtect(reinterpret_cast<void*>(g_a.circleLea), 7, PAGE_EXECUTE_READWRITE, &old);
    if (!g_circleWritable)
        g_log("cannot unprotect circle material lea (error %lu) - spares circle keeps ammo icons", GetLastError());
}

namespace {

// The standalone Patcher: dfd::PatchSet checks every write, then makes them all itself.
class PatchSetPatcher final : public Patcher {
public:
    explicit PatchSetPatcher(const Image& img) : set_(img) {}
    void bytes(const char* what, uintptr_t at, std::vector<uint8_t> orig, std::vector<uint8_t> repl) override {
        set_.bytes(what, at, std::move(orig), std::move(repl));
    }
    void slot(const char* what, uintptr_t vtable, int slot, const char* prologue, void* hook,
              void** saved) override {
        set_.vslot(what, vtable, slot, prologue, hook, saved);
    }
    bool apply() override { return set_.apply(g_log.file()); }

private:
    dfd::PatchSet set_;
};

}  // namespace

// Вызывается DllWatcher, когда logic.dll загружена (base - её адрес), до выполнения её кода.
// Ставит все правки сразу (install + PatchSetPatcher). Успех - строка «SparesSupply active».
void onLogicLoaded(uintptr_t base) {
    Image img(base);
    PatchSetPatcher p(img);
    if (!install(img, p)) return;
    unprotectCircle();
    g_log("SparesSupply active");
}

}  // namespace spares_supply
