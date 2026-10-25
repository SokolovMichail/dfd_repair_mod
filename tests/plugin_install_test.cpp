// Тесты установки плагина: поиск адресов в logic.dll и установка правок.
//
// Когда игра загружает logic.dll, плагин (onLogicLoaded):
//   1) находит в ней всё нужное (resolve) - классы по имени, места кода по шаблонам байтов, адреса
//      функций игры по инструкциям call внутри найденного кода;
//   2) ставит все правки одним набором «всё или ничего»: 4 замены «mov r15b,1» на nop (чтобы у машин мода
//      появилась кнопка F4), радиус зоны запчастей, 12 подмен методов в vtable.
// Главное свойство: если игра обновилась и хоть что-то не совпало, плагин не меняет НИЧЕГО (игра работает
// как без него) и пишет в лог, что именно не нашлось. Здесь это проверяется на поддельной logic.dll
// (support/fake_logic.h): сначала целой, потом испорченной по одной детали.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "third_party/doctest.h"

#include "../plugins/spares_supply/spares_supply.cpp"
#include "support/fake_logic.h"
#include "support/log_file.h"

using fake::FakeLogic;

namespace {

std::wstring logFile() {
    static bool opened = false;
    if (!opened) {
        g_log.open(GetModuleHandleW(nullptr), L"plugin_install_test");
        opened = true;
    }
    return fake::logPath(L"plugin_install_test");
}

// Плагин в состоянии «только что загружен»: ничего не найдено, исходные функции не запомнены.
void resetPlugin() {
    g_a = Addr{};
    g_origAmmoOn = g_origAmmoOff = g_origFuelOn = g_origFuelOff = g_origSparesOn = g_origSparesOff = nullptr;
    g_origRepairOn = g_origRepairOff = nullptr;
    g_origAmmoCanRecharge = nullptr;
    g_origSparesUpdate = g_origAmmoUpdate = g_origFuelUpdate = g_origRepairUpdate = nullptr;
    g_circleWritable = false;
}

// Свежая поддельная logic.dll и сброшенный плагин; log - всё, что плагин напишет в этом тесте.
struct Install {
    FakeLogic L;
    fake::LogTail log{logFile()};
    Install() { resetPlugin(); }
    void load() { onLogicLoaded(L.pe.base()); }
    uintptr_t slot(uintptr_t vt, int i) const { return *reinterpret_cast<uintptr_t*>(vt + 8 * i); }
    std::string rvaText(const char* fmt, uintptr_t a) const {
        char buf[160];
        snprintf(buf, sizeof buf, fmt, L.pe.rva(a));
        return buf;
    }
};

template <class F>
uintptr_t addr(F f) {
    return reinterpret_cast<uintptr_t>(f);
}

// Плагин отказался ставить правки: образ не изменился ни на байт, в логе - причина reason,
// строки об успешном запуске нет.
void expectRejected(Install& t, const std::string& reason) {
    auto before = t.L.pe.snapshot();
    t.load();
    CHECK(t.L.pe.snapshot() == before);
    CHECK_MESSAGE(t.log.has(reason), "в логе нет «" << reason << "»:\n" << t.log.text());
    CHECK_FALSE(t.log.has("SparesSupply active"));
    CHECK_FALSE(g_circleWritable);
}

}  // namespace

// ================================================================ всё совпало

TEST_CASE_FIXTURE(Install, "Целая logic.dll: все правки поставлены, плагин активен") {
    auto before = L.pe.snapshot();

    load();

    REQUIRE(log.has("SparesSupply active"));
    // Найденные адреса.
    CHECK(g_a.ammoVt == L.ammoVt);
    CHECK(g_a.fuelVt == L.fuelVt);
    CHECK(g_a.sparesVt == L.sparesVt);
    CHECK(g_a.repairVt == L.repairVt);
    CHECK(g_a.modulesVt == L.modulesVt);
    CHECK(g_a.supplyRadius == L.supplyRadius);
    CHECK(g_a.showCircle == L.showCircle);
    CHECK(g_a.circleLea == L.circleLea);
    CHECK(g_a.materialTable == L.materialTable);
    CHECK(g_a.incHealth == L.incHealth);
    CHECK(g_a.diplomacy == L.diplomacy);

    // 1) «mov r15b,1» -> nop в 4 местах (support и repair, машины и танки). Порядок - по адресу:
    //    первым идёт сборщик машин, вторым - танков.
    for (uintptr_t site : {L.supportSite[0], L.supportSite[1], L.repairSite[0], L.repairSite[1]})
        CHECK(Image::matches(site, "90 90 90 8B C3 FF C3"));
    // Тот же флаг у другого компонента (refuel) не тронут.
    CHECK(Image::matches(L.refuelSite, "41 B7 01"));

    // 2) Радиус: инструкция та же (movss xmm2), но читает теперь глобальный supplyRadius, а не константу 20.
    CHECK(Image::matches(L.radiusLoad, "F3 0F 10 15"));
    CHECK(Image::ripTarget(L.radiusLoad, 4, 8) == L.supplyRadius);

    // 3) 12 методов в vtable заменены перехватами; исходные функции запомнены.
    CHECK(slot(L.ammoVt, kSlotActivate) == addr(&hookAmmoOn));
    CHECK(slot(L.ammoVt, kSlotDeactivate) == addr(&hookAmmoOff));
    CHECK(slot(L.ammoVt, kSlotUpdate) == addr(&hookAmmoUpdate));
    CHECK(slot(L.ammoVt, kSlotCanRecharge) == addr(&hookAmmoCanRecharge));
    CHECK(slot(L.fuelVt, kSlotActivate) == addr(&hookFuelOn));
    CHECK(slot(L.fuelVt, kSlotDeactivate) == addr(&hookFuelOff));
    CHECK(slot(L.fuelVt, kSlotUpdate) == addr(&hookFuelUpdate));
    CHECK(slot(L.sparesVt, kSlotUpdate) == addr(&hookSparesUpdate));
    CHECK(slot(L.sparesVt, kSlotDeactivate) == addr(&hookSparesOff));
    CHECK(slot(L.repairVt, kSlotActivate) == addr(&hookRepairOn));
    CHECK(slot(L.repairVt, kSlotDeactivate) == addr(&hookRepairOff));
    CHECK(slot(L.repairVt, kSlotUpdate) == addr(&hookRepairUpdate));
    CHECK(addr(g_origAmmoOn) == L.ammoOn);
    CHECK(addr(g_origAmmoOff) == L.ammoOff);
    CHECK(addr(g_origAmmoUpdate) == L.ammoUpdate);
    CHECK(addr(g_origAmmoCanRecharge) == L.ammoCan);
    CHECK(addr(g_origFuelOn) == L.fuelOn);
    CHECK(addr(g_origFuelOff) == L.fuelOff);
    CHECK(addr(g_origFuelUpdate) == L.fuelUpdate);
    CHECK(addr(g_origSparesUpdate) == L.sparesUpdate);
    CHECK(addr(g_origSparesOff) == L.sparesOff);
    CHECK(addr(g_origRepairOn) == L.repairOn);
    CHECK(addr(g_origRepairOff) == L.repairOff);
    CHECK(addr(g_origRepairUpdate) == L.repairUpdate);
    // Включение запчастей не перехватывается: слот прежний, исходная функция запомнена.
    CHECK(slot(L.sparesVt, kSlotActivate) == L.sparesOn);
    CHECK(addr(g_origSparesOn) == L.sparesOn);

    // 4) Инструкция материала круга не изменена (её меняет перехват на время рисования), но
    //    сделана записываемой.
    CHECK(Image::matches(L.circleLea, "4C 8D 80 20 01 00 00"));
    CHECK(g_circleWritable);

    // Кроме перечисленного, в образе не изменилось ничего.
    std::vector<std::pair<uintptr_t, size_t>> allowed = {
        {L.supportSite[0], 3}, {L.supportSite[1], 3}, {L.repairSite[0], 3}, {L.repairSite[1], 3},
        {L.radiusLoad + 4, 4}};
    for (auto [vt, slots] : std::vector<std::pair<uintptr_t, std::vector<int>>>{
             {L.ammoVt, {kSlotActivate, kSlotDeactivate, kSlotUpdate, kSlotCanRecharge}},
             {L.fuelVt, {kSlotActivate, kSlotDeactivate, kSlotUpdate}},
             {L.sparesVt, {kSlotUpdate, kSlotDeactivate}},
             {L.repairVt, {kSlotActivate, kSlotDeactivate, kSlotUpdate}}})
        for (int s : slots) allowed.push_back({vt + 8 * s, 8});
    auto after = L.pe.snapshot();
    int unexpected = 0;
    for (size_t i = 0; i < after.size(); ++i) {
        if (after[i] == before[i]) continue;
        uintptr_t a = L.pe.base() + i;
        bool ok = false;
        for (auto [at, n] : allowed) ok = ok || (a >= at && a < at + n);
        if (!ok) ++unexpected;
    }
    CHECK(unexpected == 0);
}

TEST_CASE_FIXTURE(Install, "Целая logic.dll: лог перечисляет найденное и каждую правку") {
    load();
    char buf[200];
    snprintf(buf, sizeof buf, "logic.dll at %p (TimeDateStamp %08X, SizeOfImage %08X)",
             reinterpret_cast<void*>(L.pe.base()), fake::Pe::kTimeDateStamp, fake::Pe::kImageSize);
    CHECK(log.has(buf));
    snprintf(buf, sizeof buf, "vtables: ammo %08X fuel %08X spares %08X repair %08X modules %08X",
             L.pe.rva(L.ammoVt), L.pe.rva(L.fuelVt), L.pe.rva(L.sparesVt), L.pe.rva(L.repairVt),
             L.pe.rva(L.modulesVt));
    CHECK(log.has(buf));
    snprintf(buf, sizeof buf, "functions: incHealth %08X diplomacy %08X material table %08X",
             L.pe.rva(L.incHealth), L.pe.rva(L.diplomacy), L.pe.rva(L.materialTable));
    CHECK(log.has(buf));
    // Правок 18: 4 nop, радиус, 12 методов и проверка инструкции круга.
    CHECK(log.count("patched: ") == 18);
    CHECK(log.has(rvaText("patched: cars: support no longer blocks ammunition_recharge (RVA %08X)",
                          L.supportSite[0])));
    CHECK(log.has(rvaText("patched: tanks: repair no longer blocks ammunition_recharge (RVA %08X)",
                          L.repairSite[1])));
    CHECK(log.has(rvaText("patched: spares search radius 20 m -> supplyRadius (RVA %08X)", L.radiusLoad)));
    CHECK(log.has(rvaText("patched: supply circle material lea (verify only) (RVA %08X)", L.circleLea)));
    CHECK(log.has(rvaText("patched: RepairerFacet::update (disabled) (RVA %08X)", L.repairVt + 8 * kSlotUpdate)));
}

// ================================================================ что-то не совпало - не меняем ничего

TEST_CASE("Нет одного из пяти классов - плагин не ставит ничего") {
    for (int which = 0; which < 5; ++which) {
        CAPTURE(which);
        Install t;
        uintptr_t names[5] = {t.L.ammoName, t.L.fuelName, t.L.sparesName, t.L.repairName, t.L.modulesName};
        fake::Pe::write(names[which], {'X'});  // имя класса больше не совпадает
        expectRejected(t, "this logic.dll build is not supported - nothing patched");
    }
}

TEST_CASE("Шаблон, который должен найтись ровно один раз, не нашёлся или нашёлся дважды") {
    struct Sig {
        const char* name;
        uintptr_t FakeLogic::*at;
        const char* pattern;
    };
    const Sig sigs[] = {
        {"spares radius load", &FakeLogic::radiusLoad, kSigSparesRadiusLoad},
        {"supplyRadius use", &FakeLogic::supplyRadiusUse, kSigSupplyRadiusUse},
        {"ammo show circle", &FakeLogic::showCircle, kSigShowCircle},
        {"circle material lea", &FakeLogic::circleLea, kSigCircleMaterialLea},
        {"incHealth call", &FakeLogic::incHealthCall, kSigIncHealthCall},
    };
    for (const Sig& s : sigs) {
        CAPTURE(s.name);
        {  // нет ни одного
            Install t;
            FakeLogic::spoil(t.L.*s.at);
            expectRejected(t, std::string("signature '") + s.name + "': 0 matches (need 1)");
        }
        {  // два одинаковых места - непонятно, какое править
            Install t;
            t.L.duplicate(s.pattern);
            expectRejected(t, std::string("signature '") + s.name + "': 2 matches (need 1)");
        }
    }
}

TEST_CASE("«support блокирует ammo»: нужно ровно 2 места (машины и танки)") {
    {
        Install t;
        FakeLogic::spoil(t.L.supportSite[1]);
        expectRejected(t, "signature 'support blocks ammo': 1 matches (need 2)");
    }
    {
        Install t;
        t.L.duplicate(kSigSupportBlocksAmmo);
        expectRejected(t, "signature 'support blocks ammo': 3 matches (need 2)");
    }
}

TEST_CASE("«repair блокирует ammo»: нужно ровно 2 места со ссылкой на имя RepairerFacet") {
    {
        Install t;
        FakeLogic::spoil(t.L.repairSite[0]);
        expectRejected(t, "signature 'repair blocks ammo': 1 matches (need 2)");
    }
    {  // строки с именем компонента нет - не найдено ни одного места
        Install t;
        fake::Pe::write(t.L.repairString, {'X'});
        expectRejected(t, "signature 'repair blocks ammo': 0 matches (need 2)");
    }
    {  // третье место с тем же именем
        Install t;
        uintptr_t extra = t.L.duplicate(kSigAbilityFlag);
        fake::Pe::aim(extra + 7, 3, 7, t.L.repairString);
        expectRejected(t, "signature 'repair blocks ammo': 3 matches (need 2)");
    }
}

namespace {

// Убрать инструкцию материала с её места (+0x77) и положить единственную копию (с call таблицы
// материалов перед ней) на distance байт дальше начала функции круга. Возвращает адрес копии.
uintptr_t moveCircleLea(FakeLogic& L, size_t distance) {
    FakeLogic::spoil(L.circleLea);
    uintptr_t at = L.showCircle + distance;
    fake::Pe::write(at, fake::hex(kSigCircleMaterialLea));
    FakeLogic::putCall(at - kMaterialTableCallBack, L.materialTable);
    return at;
}

}  // namespace

TEST_CASE_FIXTURE(Install, "Инструкция материала круга дальше 0x100 байт от начала функции круга - отказ") {
    // «Внутри функции» для плагина - не дальше 0x100 байт от её начала (в GOG-сборке она на +0x77).
    L.pe.text.cur = L.showCircle + 0x200;  // освободить место за функцией
    moveCircleLea(L, 0x180);
    expectRejected(*this, "circle material lea is not inside the circle function");
}

TEST_CASE_FIXTURE(Install, "Инструкция материала круга сдвинулась, но в пределах 0x100 байт - принимается") {
    L.pe.text.cur = L.showCircle + 0x200;
    uintptr_t at = moveCircleLea(L, 0xF0);
    load();
    CHECK(log.has("SparesSupply active"));
    CHECK(g_a.circleLea == at);
}

TEST_CASE_FIXTURE(Install, "Перед инструкцией материала нет вызова таблицы материалов") {
    fake::Pe::write(L.circleLea - kMaterialTableCallBack, {0x90});
    expectRejected(*this, "material table call not found");
}

TEST_CASE_FIXTURE(Install, "incHealth начинается не так, как ожидалось") {
    FakeLogic::spoil(L.incHealth);
    expectRejected(*this, "incHealth: unexpected prologue");
}

TEST_CASE("В canRecharge не нашёлся вызов дипломатии") {
    {  // изменилось начало canRecharge
        Install t;
        FakeLogic::spoil(t.L.ammoCan);
        expectRejected(t, "diplomacy call in canRecharge not found");
    }
    {  // на +0x3a не тот код
        Install t;
        FakeLogic::spoil(t.L.ammoCan + kDiplomacyCallAt);
        expectRejected(t, "diplomacy call in canRecharge not found");
    }
}

TEST_CASE("Перехватываемый метод начинается не так, как ожидалось") {
    // Начало функции подтверждает, что это та же функция и поля объекта лежат там же, где думает плагин.
    struct Method {
        const char* name;
        uintptr_t FakeLogic::*fn;
    };
    const Method methods[] = {
        {"AmmoRechargerFacet::activate", &FakeLogic::ammoOn},
        {"AmmoRechargerFacet::deactivate", &FakeLogic::ammoOff},
        {"AmmoRechargerFacet::update", &FakeLogic::ammoUpdate},
        {"RefuellerFacet::activate", &FakeLogic::fuelOn},
        {"RefuellerFacet::deactivate", &FakeLogic::fuelOff},
        {"RefuellerFacet::update", &FakeLogic::fuelUpdate},
        {"SparesRechargerFacet::update", &FakeLogic::sparesUpdate},
        {"SparesRechargerFacet::deactivate", &FakeLogic::sparesOff},
        {"RepairerFacet::activate", &FakeLogic::repairOn},
        {"RepairerFacet::deactivate", &FakeLogic::repairOff},
        {"RepairerFacet::update (disabled)", &FakeLogic::repairUpdate},
        // canRecharge проверяется раньше - при поиске вызова дипломатии (тест выше).
    };
    for (const Method& m : methods) {
        CAPTURE(m.name);
        Install t;
        FakeLogic::spoil(t.L.*m.fn);
        expectRejected(t, std::string("verify FAILED: ") + m.name + " at RVA");
        CHECK(addr(g_origAmmoOn) == 0);  // исходные функции не запомнены - перехваты не стоят
    }
}

TEST_CASE_FIXTURE(Install, "Включение запчастей начинается не так, как ожидалось") {
    // Его плагин не перехватывает, а вызывает сам - поэтому тоже проверяет.
    FakeLogic::spoil(L.sparesOn);
    expectRejected(*this, "verify FAILED: SparesRechargerFacet::activate - nothing patched");
}

TEST_CASE_FIXTURE(Install, "Повторный поиск без сброса не проходит") {
    // ОСОБЕННОСТЬ: resolve рассчитан на один вызов за запуск - найденные места «repair блокирует ammo»
    // добавляются к уже найденным, и при втором вызове их оказывается 4. В игре logic.dll загружается
    // один раз, и DllWatcher вызывает установку тоже один раз.
    Image img(L.pe.base());
    REQUIRE(resolve(img));
    CHECK_FALSE(resolve(img));
    CHECK(g_a.repairBlocksAmmo.size() == 4);
}
