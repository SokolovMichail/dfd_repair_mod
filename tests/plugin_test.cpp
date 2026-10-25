// Тесты поведения плагина SparesSupply во время игры.
//
// Что проверяется - то, что видит игрок:
//   * HEMTT Spares в режиме F4 (SUPPLY MODE) раздаёт запчасти, а не патроны, и рисует свой круг зоны;
//   * HEMTT Repair в режиме F4 чинит модули и восстанавливает броню только своим и союзникам, платит
//     своими запчастями, а не запчастями ремонтируемой машины;
//   * обычный HEMTT Supply и прочие машины ведут себя так же, как без мода.
// Плагин делает это, перехватывая методы компонентов игры («перехват» = наша функция вместо игровой;
// она обычно вызывает и исходную). Здесь перехваты вызываются напрямую - так, как их вызвала бы игра, -
// на поддельных машинах (support/fake_engine.h), и проверяется, что они сделали.
//
// Код плагина подключается сюда целиком (#include .cpp), чтобы тесту были видны его внутренние функции.
// После рефакторинга, если функции переедут или переименуются, менять нужно только подключение и
// подделки - сами проверки описывают поведение и должны остаться прежними.
//
// Пометка «ОСОБЕННОСТЬ:» - поведение, которое выглядит странно, но таково сейчас. Тест фиксирует его,
// чтобы рефакторинг не изменил его незаметно; менять - осознанно, вместе с тестом.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "third_party/doctest.h"

#include "../plugins/spares_supply/spares_supply.cpp"
#include "support/fake_engine.h"
#include "support/log_file.h"

using namespace fake;

namespace {

// Лог плагина в тестах - plugin_test.log рядом с программой (в игре - bin\plugins\SparesSupply.log).
std::wstring logFile() {
    static bool opened = false;
    if (!opened) {
        g_log.open(GetModuleHandleW(nullptr), L"plugin_test");
        opened = true;
    }
    return logPath(L"plugin_test");
}

// Общая подготовка каждого теста: свежая «игра» и сброшенное состояние плагина.
struct Engine {
    Engine() {
        logFile();
        reset();
    }
};

// Типичные машины. HEMTT Supply - только боеприпасы; HEMTT Spares - боеприпасы (ради кнопки F4) и
// запчасти; HEMTT Repair - боеприпасы (ради F4 и круга) и ремонт (метка «ремонтная машина»).
struct Supply {
    Vehicle v;
    Facet& ammo = v.add(g_a.ammoVt);
};
struct Spares {
    Vehicle v;
    Facet& ammo = v.add(g_a.ammoVt);
    Facet& spares = v.add(g_a.sparesVt);
};
struct Repair {
    Vehicle v;
    Facet& repair = v.add(g_a.repairVt);  // в игре компонент ремонта создаётся раньше боеприпасов
    Facet& ammo = v.add(g_a.ammoVt);
    Facet& modules;
    explicit Repair(int stock = 10000) : modules(v.addModules({ModuleSpec{}}, stock)) {}
};

// Повреждённая машина: модули specs (по умолчанию - один модуль 50/100), запчастей у неё самой - 7.
struct Damaged {
    Vehicle v;
    Facet& modules;
    explicit Damaged(int team = 1, std::vector<ModuleSpec> specs = {ModuleSpec{50, 100}})
        : v(team), modules(v.addModules(specs, 7)) {}
};

}  // namespace

// ================================================================ лог зоны запчастей
// Эти тесты идут первыми: счётчик строк «spares active» в плагине общий на весь запуск игры (и тестов).

TEST_CASE_FIXTURE(Engine, "Лог зоны запчастей: радиус и сколько машин в зоне") {
    // Подготовка: включённые запчасти, в их списке целей ([+0x28..+0x30]) - 3 машины, радиус зоны 25 м.
    Spares s;
    turnOn(s.spares, kSparesOff);
    void* list[3] = {};
    s.spares.at<void**>(0x28) = list;
    s.spares.at<void**>(0x30) = list + 3;
    g_supplyRadius = 25.0f;
    LogTail log(logFile());

    // Действие: секунда работы зоны.
    hookSparesUpdate(s.spares.p(), 1.0f);

    // Ожидаем: строка в логе раз в секунду - с радиусом и числом машин.
    CHECK(log.has("radius=25.0 targets=3"));
}

TEST_CASE_FIXTURE(Engine, "Лог зоны запчастей: не больше 20 строк за запуск") {
    Spares s;
    turnOn(s.spares, kSparesOff);
    for (int i = 0; i < 30; ++i) hookSparesUpdate(s.spares.p(), 1.0f);  // 30 секунд работы
    CHECK(countOf(readFile(logFile()), "spares active") == 20);
}

// ================================================================ поиск компонента у машины

TEST_CASE_FIXTURE(Engine, "findFacet находит компонент точного класса и не портит счётчики ссылок") {
    // Подготовка: машина с боеприпасами и запчастями.
    Spares s;

    // Ожидаем: находится нужный компонент; компонента ремонта нет - пустота; без машины - пустота.
    CHECK(findFacet(&s.v, g_a.sparesVt) == s.spares.p());
    CHECK(findFacet(&s.v, g_a.ammoVt) == s.ammo.p());
    CHECK(findFacet(&s.v, g_a.repairVt) == nullptr);
    CHECK(findFacet(nullptr, g_a.ammoVt) == nullptr);

    // Каждую взятую ссылку плагин вернул: счётчики как были, ничего не освобождено.
    for (auto& b : s.v.blocks) CHECK(b.strong == 1);
    CHECK(g_freed == 0);
    // Каждый итератор удалён, и с флагом 1 (так удаляет их сама игра).
    CHECK(g_iterDeleted == g_iterCreated);
    for (int f : g_iterDeleteFlags) CHECK(f == 1);
}

TEST_CASE_FIXTURE(Engine, "findFacet: класс сравнивается точно, берётся первый подходящий") {
    Vehicle v;
    v.add(vt(g_otherVt));  // другой класс (например, наследник) - не подходит
    Facet& first = v.add(g_a.ammoVt);
    v.add(g_a.ammoVt);
    CHECK(findFacet(&v, g_a.ammoVt) == first.p());
}

TEST_CASE_FIXTURE(Engine, "findFacet: машина не выдала итератор - компонента нет") {
    Spares s;
    s.v.nullIterator = true;
    CHECK(findFacet(&s.v, g_a.sparesVt) == nullptr);
}

// ================================================================ какая это машина

TEST_CASE_FIXTURE(Engine, "kindOf: машину мода узнаём по компонентам") {
    Supply supply;
    Spares spares;
    Repair repair;
    CHECK(kindOf(supply.ammo.p()) == Kind::Ammo);    // обычный HEMTT Supply
    CHECK(kindOf(spares.ammo.p()) == Kind::Spares);  // есть запчасти -> HEMTT Spares
    CHECK(kindOf(repair.ammo.p()) == Kind::Repair);  // есть ремонт -> HEMTT Repair

    // И запчасти, и ремонт - считается HEMTT Spares (запчасти проверяются первыми).
    Vehicle both;
    Facet& ammo = both.add(g_a.ammoVt);
    both.add(g_a.repairVt);
    both.add(g_a.sparesVt);
    CHECK(kindOf(ammo.p()) == Kind::Spares);

    // Компонент без машины-владельца - обычный.
    Facet orphan;
    CHECK(kindOf(orphan.p()) == Kind::Ammo);
}

// ================================================================ HEMTT Spares: режим F4

TEST_CASE_FIXTURE(Engine, "syncSpares переключает запчасти, только если они в другом состоянии") {
    Spares s;
    syncSpares(s.ammo.p(), true);
    syncSpares(s.ammo.p(), true);  // уже включены - второй раз не включаем
    CHECK(calls("spares.on", s.spares.p()) == 1);
    CHECK(isOnFlag(s.spares, kSparesOff));
    syncSpares(s.ammo.p(), false);
    syncSpares(s.ammo.p(), false);
    CHECK(calls("spares.off", s.spares.p()) == 1);
    CHECK_FALSE(isOnFlag(s.spares, kSparesOff));

    // У машины без запчастей - ничего.
    Supply supply;
    syncSpares(supply.ammo.p(), true);
    CHECK(calls("spares.on") == 1);
}

TEST_CASE_FIXTURE(Engine, "HEMTT Spares: F4 включает запчасти и перекрашивает круг в гаечные ключи") {
    // Подготовка: HEMTT Spares, всё выключено.
    Spares s;

    // Действие: игрок нажал F4 - игра включает компонент боеприпасов.
    hookAmmoOn(s.ammo.p());

    // Ожидаем: сначала оригинал (включает боеприпасы и рисует круг патронов), затем включаются запчасти.
    CHECK(callNames() == std::vector<std::string>{"ammo.on", "spares.on"});
    CHECK(isOnFlag(s.spares, kSparesOff));
    // Круг перерисован один раз - материалом запчастей: в lea на время рисования стояло 0xE0
    // (sparesRecharge), строка в таблице материалов - не тронута.
    REQUIRE(g_draws.size() == 1);
    CHECK(g_draws[0].ammo == s.ammo.p());
    CHECK(g_draws[0].lea == 0xE0u);
    CHECK(g_draws[0].material == kAmmoMaterial);
    // После рисования lea возвращена: остальные машины рисуют круг патронов, как обычно.
    CHECK(leaOffset() == 0x120u);
    // Новый круг запомнен как «уже наш».
    CHECK(g_ownCircles.count(circleField(s.ammo)) == 1);
}

TEST_CASE_FIXTURE(Engine, "HEMTT Supply: F4 работает как без мода") {
    Supply s;
    void* circleBefore = nullptr;
    hookAmmoOn(s.ammo.p());
    circleBefore = circleField(s.ammo);
    CHECK(callNames() == std::vector<std::string>{"ammo.on"});
    CHECK(g_draws.empty());                    // круг не перерисован
    CHECK(circleField(s.ammo) == circleBefore);
    CHECK(g_ownCircles.empty());
}

TEST_CASE_FIXTURE(Engine, "Круг перерисовывается один раз, а не каждый кадр") {
    Spares s;
    hookAmmoOn(s.ammo.p());
    for (int i = 0; i < 5; ++i) hookAmmoUpdate(s.ammo.p(), 0.1f);  // 5 кадров
    CHECK(g_draws.size() == 1);
}

TEST_CASE_FIXTURE(Engine, "Круг, нарисованный без плагина (загрузка сохранения), перекрашивается в update") {
    // Подготовка: боеприпасы уже включены и круг патронов уже есть - плагин при этом не участвовал.
    Spares s;
    turnOn(s.ammo, kAmmoOff);
    circleField(s.ammo) = newCircle();

    // Действие: обычный кадр игры.
    hookAmmoUpdate(s.ammo.p(), 0.1f);

    // Ожидаем: круг перекрашен материалом запчастей.
    REQUIRE(g_draws.size() == 1);
    CHECK(g_draws[0].lea == 0xE0u);
}

TEST_CASE_FIXTURE(Engine, "Нет круга - перекрашивать нечего") {
    Spares s;
    turnOn(s.ammo, kAmmoOff);  // включены, но круга ([+0x50]) нет
    hookAmmoUpdate(s.ammo.p(), 0.1f);
    CHECK(g_draws.empty());
}

TEST_CASE_FIXTURE(Engine, "Инструкцию lea не удалось сделать записываемой - у HEMTT Spares остаётся круг патронов") {
    // ОСОБЕННОСТЬ: у HEMTT Repair круг другим способом (подмена строки), поэтому он всё равно свой.
    g_circleWritable = false;
    Spares s;
    hookAmmoOn(s.ammo.p());
    CHECK(g_draws.empty());
    CHECK(g_ownCircles.empty());

    Repair r;
    hookAmmoOn(r.ammo.p());
    CHECK(g_draws.size() == 1);
}

TEST_CASE_FIXTURE(Engine, "HEMTT Spares: выход из F4 выключает запчасти и забывает круг") {
    Spares s;
    hookAmmoOn(s.ammo.p());
    void* circle = circleField(s.ammo);
    g_calls.clear();

    hookAmmoOff(s.ammo.p());

    // Ожидаем: сначала оригинал (выключает боеприпасы, убирает круг), потом выключаются запчасти -
    // напрямую, минуя наш перехват выключения запчастей.
    CHECK(callNames() == std::vector<std::string>{"ammo.off", "spares.off"});
    CHECK_FALSE(isOnFlag(s.spares, kSparesOff));
    CHECK(g_ownCircles.count(circle) == 0);
}

TEST_CASE_FIXTURE(Engine, "Каждый кадр F4: оригинал update боеприпасов, затем тик запчастей с тем же dt") {
    // Игра сама update запчастей не вызывает - без плагина включённые запчасти ничего бы не раздавали.
    Spares s;
    hookAmmoOn(s.ammo.p());
    g_calls.clear();

    hookAmmoUpdate(s.ammo.p(), 0.25f);

    REQUIRE(g_calls.size() == 2);
    CHECK(g_calls[0].what == "ammo.update");
    CHECK(g_calls[1].what == "spares.update");
    CHECK(g_calls[1].obj == s.spares.p());
    CHECK(g_calls[1].dt == 0.25f);
}

TEST_CASE_FIXTURE(Engine, "Вне режима F4 update боеприпасов - только оригинал") {
    Spares s;
    Repair r;
    hookAmmoUpdate(s.ammo.p(), 0.25f);
    hookAmmoUpdate(r.ammo.p(), 0.25f);
    CHECK(callNames() == std::vector<std::string>{"ammo.update", "ammo.update"});
    CHECK(g_incHealth.empty());
    CHECK(g_draws.empty());
}

TEST_CASE_FIXTURE(Engine, "F4 включён, а запчасти выключены - запчасти не тикаются") {
    // ОСОБЕННОСТЬ: круг при этом всё равно перекрашивается в материал запчастей.
    Spares s;
    turnOn(s.ammo, kAmmoOff);
    circleField(s.ammo) = newCircle();
    hookAmmoUpdate(s.ammo.p(), 0.25f);
    CHECK(calls("spares.update") == 0);
    CHECK(g_draws.size() == 1);
}

TEST_CASE_FIXTURE(Engine, "Перехват update запчастей вызывает оригинал с тем же dt") {
    Spares s;
    hookSparesUpdate(s.spares.p(), 0.5f);  // выключены - всё равно оригинал
    REQUIRE(g_calls.size() == 1);
    CHECK(g_calls[0].what == "spares.update");
    CHECK(g_calls[0].dt == 0.5f);
}

TEST_CASE_FIXTURE(Engine, "Выключение запчастей пропускается, пока у машины идёт режим F4") {
    // Приказ «пополнить запчасти» по клику по окончании выключает запчасти; в режиме F4 это оборвало бы
    // раздачу по зоне.
    Spares s;
    turnOn(s.spares, kSparesOff);
    turnOn(s.ammo, kAmmoOff);
    hookSparesOff(s.spares.p());
    CHECK(calls("spares.off") == 0);
    CHECK(isOnFlag(s.spares, kSparesOff));

    // Режим F4 закончился - выключение проходит.
    s.ammo.at<uint8_t>(kAmmoOff) = 1;
    hookSparesOff(s.spares.p());
    CHECK(calls("spares.off") == 1);
}

TEST_CASE_FIXTURE(Engine, "Выключение запчастей пропускается и при включённом заправщике") {
    Vehicle v;
    Facet& fuel = v.add(g_a.fuelVt);
    Facet& spares = v.add(g_a.sparesVt);
    turnOn(fuel, kAmmoOff);
    turnOn(spares, kSparesOff);
    hookSparesOff(spares.p());
    CHECK(calls("spares.off") == 0);
}

TEST_CASE_FIXTURE(Engine, "Запчасти у машины без боеприпасов и заправщика выключаются как обычно") {
    Vehicle v;
    Facet& spares = v.add(g_a.sparesVt);
    turnOn(spares, kSparesOff);
    hookSparesOff(spares.p());
    CHECK(calls("spares.off") == 1);
}

// ================================================================ заправщик + запчасти

TEST_CASE_FIXTURE(Engine, "Заправщик: F4 включает и выключает запчасти той же машины") {
    // На случай машины с refuel и support: тогда режим F4 включает заправщик.
    Vehicle v;
    Facet& fuel = v.add(g_a.fuelVt);
    Facet& spares = v.add(g_a.sparesVt);

    hookFuelOn(fuel.p());
    CHECK(callNames() == std::vector<std::string>{"fuel.on", "spares.on"});
    CHECK(isOnFlag(spares, kSparesOff));
    g_calls.clear();
    hookFuelOff(fuel.p());
    CHECK(callNames() == std::vector<std::string>{"fuel.off", "spares.off"});
    CHECK(g_draws.empty());  // круг заправщика не трогаем
}

TEST_CASE_FIXTURE(Engine, "Заправщик без запчастей - только оригинал") {
    Vehicle v;
    Facet& fuel = v.add(g_a.fuelVt);
    hookFuelOn(fuel.p());
    hookFuelOff(fuel.p());
    CHECK(callNames() == std::vector<std::string>{"fuel.on", "fuel.off"});
}

TEST_CASE_FIXTURE(Engine, "Заправщик: каждый кадр F4 - оригинал и тик запчастей") {
    Vehicle v;
    Facet& fuel = v.add(g_a.fuelVt);
    Facet& spares = v.add(g_a.sparesVt);

    hookFuelUpdate(fuel.p(), 0.2f);  // выключен - только оригинал
    CHECK(callNames() == std::vector<std::string>{"fuel.update"});

    g_calls.clear();
    turnOn(fuel, kAmmoOff);
    turnOn(spares, kSparesOff);
    hookFuelUpdate(fuel.p(), 0.2f);
    REQUIRE(g_calls.size() == 2);
    CHECK(g_calls[0].what == "fuel.update");
    CHECK(g_calls[1].what == "spares.update");
    CHECK(g_calls[1].dt == 0.2f);
}

// ================================================================ патроны машины мода не раздают

TEST_CASE_FIXTURE(Engine, "canRecharge: HEMTT Spares и HEMTT Repair патроны не раздают никогда") {
    Spares s;
    Repair r;
    Vehicle target;
    CHECK_FALSE(hookAmmoCanRecharge(s.ammo.p(), &target));
    CHECK_FALSE(hookAmmoCanRecharge(r.ammo.p(), &target));
    CHECK(calls("ammo.canRecharge") == 0);  // оригинал даже не спрашивается
}

TEST_CASE_FIXTURE(Engine, "canRecharge: обычному HEMTT Supply отвечает игра") {
    Supply s;
    Vehicle target;
    g_canAnswer = true;
    CHECK(hookAmmoCanRecharge(s.ammo.p(), &target));
    CHECK(g_canTarget == &target);  // та же цель передана оригиналу
    g_canAnswer = false;
    CHECK_FALSE(hookAmmoCanRecharge(s.ammo.p(), &target));
    CHECK(calls("ammo.canRecharge") == 2);
}

// ================================================================ HEMTT Repair: режим F4

TEST_CASE_FIXTURE(Engine, "HEMTT Repair: F4 включает ремонт и боеприпасы, круг - материалом ремонта") {
    // Режим F4 у HEMTT Repair включает компонент ремонта (он у машины первый), а круг рисуют и цели
    // собирают боеприпасы - поэтому включение передаётся им.
    Repair r;

    hookRepairOn(r.repair.p());

    CHECK(callNames() == std::vector<std::string>{"repair.on", "ammo.on"});
    CHECK(isOnFlag(r.repair, kRepairOff));
    CHECK(isOnFlag(r.ammo, kAmmoOff));
    // Круг: на время рисования строка ammoRecharge в таблице подменена путём материала ремонта...
    REQUIRE(g_draws.size() == 1);
    CHECK(g_draws[0].material == "materials/rounds/repairRecharge.material");
    CHECK(g_draws[0].lea == 0x120u);  // lea не трогается
    CHECK(g_materialTableCalls == 1);
    // ...и сразу возвращена обратно.
    CHECK(std::string(ammoMaterialEntry().ptr, ammoMaterialEntry().size) == kAmmoMaterial);
    CHECK(ammoMaterialEntry().capacity == strlen(kAmmoMaterial));
}

TEST_CASE_FIXTURE(Engine, "HEMTT Repair: повторное включение не включает боеприпасы второй раз") {
    Repair r;
    turnOn(r.ammo, kAmmoOff);
    hookRepairOn(r.repair.p());
    CHECK(callNames() == std::vector<std::string>{"repair.on"});
}

TEST_CASE_FIXTURE(Engine, "HEMTT Repair: выход из F4 выключает и боеприпасы") {
    Repair r;
    hookRepairOn(r.repair.p());
    g_calls.clear();
    hookRepairOff(r.repair.p());
    CHECK(callNames() == std::vector<std::string>{"repair.off", "ammo.off"});

    // Боеприпасы уже выключены - только ремонт.
    g_calls.clear();
    hookRepairOff(r.repair.p());
    CHECK(callNames() == std::vector<std::string>{"repair.off"});
}

TEST_CASE_FIXTURE(Engine, "Собственный update ремонта игры не выполняется вовсе") {
    // Он в игре недоделан: ищет цели и выбрасывает результат.
    Repair r;
    hookRepairUpdate(r.repair.p(), 0.5f);
    CHECK(g_calls.empty());
}

TEST_CASE_FIXTURE(Engine, "Выход из F4 сбрасывает таймер и долг ремонта") {
    Repair r;
    g_repairTimers[r.ammo.p()] = 0.3f;
    g_armorDebt[r.ammo.p()] = 0.5f;
    hookAmmoOff(r.ammo.p());
    CHECK(g_repairTimers.count(r.ammo.p()) == 0);
    CHECK(g_armorDebt.count(r.ammo.p()) == 0);
}

// ================================================================ оплата запчастями

TEST_CASE("Оплата: дробная цена копится, со склада списываются целые запчасти") {
    // Подготовка: на складе 10 запчастей, долга нет.
    int spares = 10;
    float debt = 0;
    Stock stock{spares, debt};
    Repaired r;

    // Действие: заплатить 2.5, затем 0.6.
    stock.pay(2.5f, r);
    // Ожидаем: списано 2 (целая часть), 0.5 осталось в долге.
    CHECK(spares == 8);
    CHECK(debt == doctest::Approx(0.5f));
    CHECK(r.spent == 2);
    stock.pay(0.6f, r);
    // 0.5 + 0.6 = 1.1 -> списана ещё 1, в долге 0.1.
    CHECK(spares == 7);
    CHECK(debt == doctest::Approx(0.1f));
    CHECK(r.spent == 3);
}

TEST_CASE("Оплата: списывается не больше, чем есть; остаток остаётся долгом") {
    // ОСОБЕННОСТЬ: долг может стать больше склада - он просто ждёт пополнения.
    int spares = 1;
    float debt = 0;
    Stock stock{spares, debt};
    Repaired r;
    stock.pay(5.0f, r);
    CHECK(spares == 0);
    CHECK(debt == doctest::Approx(4.0f));
    CHECK(r.spent == 1);
}

// ================================================================ ремонт одной машины

namespace {

// Один импульс ремонта машины target за счёт склада stock (долг - debt).
Repaired pulse(Facet& target, int& stock, float& debt) { return repairVehicle(target.p(), Stock{stock, debt}); }

}  // namespace

TEST_CASE_FIXTURE(Engine, "Ремонт модуля: +2% от максимума за импульс, через движковый incHealth") {
    // Подготовка: модуль 50 из 100, на складе 1000.
    Damaged d;
    int stock = 1000;
    float debt = 0;

    Repaired r = pulse(d.modules, stock, debt);

    // Ожидаем: +2 здоровья (2% от 100), через incHealth модуля №0; заплачено 2 запчасти.
    REQUIRE(g_incHealth.size() == 1);
    CHECK(g_incHealth[0].modules == d.modules.p());
    CHECK(g_incHealth[0].index == 0u);
    CHECK(g_incHealth[0].amount == 2);
    CHECK(moduleHp(d.modules, 0) == 52);
    CHECK(r.health == 2);
    CHECK(r.spent == 2);
    CHECK(stock == 998);
    // Общее здоровье машины пересчитал движок (incHealth).
    CHECK(totalHp(d.modules) == 52);
}

TEST_CASE_FIXTURE(Engine, "Ремонт модуля: запчасти ремонтируемой машины не тратятся") {
    // incHealth - это ремонт экипажем, он списывает запчасти у самой машины. Платит HEMTT Repair,
    // поэтому плагин возвращает их цели.
    Damaged d;  // у цели 7 запчастей
    int stock = 1000;
    float debt = 0;
    pulse(d.modules, stock, debt);
    CHECK(sparesIn(d.modules) == 7);
}

TEST_CASE_FIXTURE(Engine, "Ремонт модуля: минимум 1, не больше недостающего, не больше склада") {
    // У каждой машины есть ещё целый модуль: машина с общим здоровьем 0 считается уничтоженной.
    const ModuleSpec intact{100, 100};
    int stock = 1000;
    float debt = 0;
    // 2% от 30 = 0.6 -> округляется до 0, но меньше 1 не бывает.
    Damaged small(1, {ModuleSpec{0, 30}, intact});
    pulse(small.modules, stock, debt);
    CHECK(moduleHp(small.modules, 0) == 1);
    // Не хватает 1 до максимума - добавляется 1, а не 2.
    Damaged almost(1, {ModuleSpec{99, 100}, intact});
    pulse(almost.modules, stock, debt);
    CHECK(moduleHp(almost.modules, 0) == 100);
    // 2% от 1000 = 20, но на складе 3 - добавляется 3.
    Damaged big(1, {ModuleSpec{0, 1000}, intact});
    int small3 = 3;
    pulse(big.modules, small3, debt);
    CHECK(moduleHp(big.modules, 0) == 3);
    CHECK(small3 == 0);
}

TEST_CASE_FIXTURE(Engine, "Ремонт: целые модули не трогаются, повреждённые чинятся все за один импульс") {
    Damaged d(1, {ModuleSpec{100, 100}, ModuleSpec{10, 100}, ModuleSpec{20, 50}});
    int stock = 1000;
    float debt = 0;
    Repaired r = pulse(d.modules, stock, debt);
    REQUIRE(g_incHealth.size() == 2);  // модуль №0 целый
    CHECK(g_incHealth[0].index == 1u);
    CHECK(g_incHealth[1].index == 2u);
    CHECK(moduleHp(d.modules, 1) == 12);  // +2% от 100
    CHECK(moduleHp(d.modules, 2) == 21);  // +2% от 50 = 1
    CHECK(r.health == 3);
}

TEST_CASE_FIXTURE(Engine, "Броня: каждому изношенному слою +2% исходной толщины, 1 запчасть за 1 мм") {
    // Подготовка: модуль цел; сторона 0 - слои 50 и 100 мм при исходных 100 и 100; сторона 5 - слой
    // 99 из 100 мм.
    ModuleSpec m;
    m.armor[0] = {50, 100};
    m.armorFull[0] = {100, 100};
    m.armor[5] = {99};
    m.armorFull[5] = {100};
    Damaged d(1, {m});
    int stock = 1000;
    float debt = 0;

    Repaired r = pulse(d.modules, stock, debt);

    // Ожидаем: 50 -> 52 (+2% от 100); целый слой не тронут; 99 -> 100 (не выше исходной).
    CHECK(armorAt(d.modules, 0, 0, 0) == 52.0f);
    CHECK(armorAt(d.modules, 0, 0, 1) == 100.0f);
    CHECK(armorAt(d.modules, 0, 5, 0) == 100.0f);
    CHECK(r.layers == 2);
    // Цена: 2 мм + 1 мм = 3 запчасти.
    CHECK(r.spent == 3);
    CHECK(stock == 997);
    CHECK(g_incHealth.empty());  // здоровье модуля полное
}

TEST_CASE_FIXTURE(Engine, "Броня: дробная цена копится в долге") {
    // Слой с исходной толщиной 25 мм: +0.5 мм за импульс - 0.5 запчасти.
    ModuleSpec m;
    m.armor[0] = {10};
    m.armorFull[0] = {25};
    Damaged d(1, {m});
    int stock = 100;
    float debt = 0;
    Repaired r = pulse(d.modules, stock, debt);
    CHECK(armorAt(d.modules, 0, 0, 0) == 10.5f);
    CHECK(r.spent == 0);  // пока не списано ничего
    CHECK(debt == doctest::Approx(0.5f));
    r = pulse(d.modules, stock, debt);
    CHECK(r.spent == 1);  // второй импульс: 0.5 + 0.5 = 1 запчасть
    CHECK(stock == 99);
}

TEST_CASE_FIXTURE(Engine, "Броня: слои с нулевой исходной толщиной и лишние текущие слои пропускаются") {
    // Сторона 0: в текущей толщине 3 слоя, а в описании модуля - 2 (второй с толщиной 0). Берутся только
    // слои, которые есть в обоих списках; слой с исходной толщиной 0 не чинится.
    ModuleSpec m;
    m.armor[0] = {10, 0, 10};
    m.armorFull[0] = {100, 0};
    Damaged d(1, {m});
    int stock = 100;
    float debt = 0;
    Repaired r = pulse(d.modules, stock, debt);
    CHECK(armorAt(d.modules, 0, 0, 0) == 12.0f);
    CHECK(armorAt(d.modules, 0, 0, 1) == 0.0f);
    CHECK(armorAt(d.modules, 0, 0, 2) == 10.0f);  // третьего слоя нет в описании - не тронут
    CHECK(r.layers == 1);
}

TEST_CASE_FIXTURE(Engine, "Уничтоженная машина (общее здоровье 0) не чинится") {
    ModuleSpec m{0, 100};
    m.armor[0] = {10};
    m.armorFull[0] = {100};
    Damaged d(1, {m});
    REQUIRE(totalHp(d.modules) == 0);
    int stock = 100;
    float debt = 0;
    Repaired r = pulse(d.modules, stock, debt);
    CHECK(g_incHealth.empty());
    CHECK(armorAt(d.modules, 0, 0, 0) == 10.0f);
    CHECK(r.health == 0);
    CHECK(stock == 100);
}

TEST_CASE_FIXTURE(Engine, "Модуль без описания пропускается, следующий чинится") {
    ModuleSpec broken{10, 100};
    broken.noDesc = true;
    Damaged d(1, {broken, ModuleSpec{10, 100}});
    int stock = 100;
    float debt = 0;
    pulse(d.modules, stock, debt);
    REQUIRE(g_incHealth.size() == 1);
    CHECK(g_incHealth[0].index == 1u);
}

TEST_CASE_FIXTURE(Engine, "Склад кончился - следующие модули не чинятся") {
    Damaged d(1, {ModuleSpec{10, 100}, ModuleSpec{10, 100}});
    int stock = 2;
    float debt = 0;
    pulse(d.modules, stock, debt);
    CHECK(moduleHp(d.modules, 0) == 12);
    CHECK(moduleHp(d.modules, 1) == 10);
    CHECK(stock == 0);
}

TEST_CASE_FIXTURE(Engine, "Склад кончился посреди стороны - броня этой стороны дочинивается в долг") {
    // ОСОБЕННОСТЬ: запас проверяется перед каждой стороной брони, а не перед каждым слоем. Поэтому слои
    // начатой стороны чинятся до конца, а недостающая цена уходит в долг.
    ModuleSpec m;
    m.armor[0] = {50, 50, 50};
    m.armorFull[0] = {100, 100, 100};
    m.armor[1] = {50};
    m.armorFull[1] = {100};
    Damaged d(1, {m});
    int stock = 1;
    float debt = 0;
    Repaired r = pulse(d.modules, stock, debt);
    // Все 3 слоя стороны 0 выросли на 2 мм (цена 6), заплачена 1 запчасть, 5 - в долге.
    for (size_t k = 0; k < 3; ++k) CHECK(armorAt(d.modules, 0, 0, k) == 52.0f);
    CHECK(r.layers == 3);
    CHECK(r.spent == 1);
    CHECK(stock == 0);
    CHECK(debt == doctest::Approx(5.0f));
    // Сторона 1 уже не тронута.
    CHECK(armorAt(d.modules, 0, 1, 0) == 50.0f);
}

// ================================================================ импульс ремонта по зоне

namespace {

// HEMTT Repair в режиме F4 с машинами в зоне.
struct RepairZone : Engine {
    Repair r;
    RepairZone() { turnOn(r.ammo, kAmmoOff); }
    void zone(std::vector<RefBlock*> list) { r.v.setTargets(r.ammo, std::move(list)); }
    void tick(float dt) { tickRepair(r.ammo.p(), dt); }
};

}  // namespace

TEST_CASE_FIXTURE(RepairZone, "Импульс ремонта раз в 0.5 секунды") {
    Damaged d;
    zone({&d.v.self});

    tick(0.25f);  // первый кадр - сразу импульс
    CHECK(moduleHp(d.modules, 0) == 52);
    tick(0.25f);  // через 0.25 с - рано
    CHECK(moduleHp(d.modules, 0) == 52);
    tick(0.25f);  // через 0.5 с - следующий импульс
    CHECK(moduleHp(d.modules, 0) == 54);
}

TEST_CASE_FIXTURE(RepairZone, "Ремонт платит запчастями HEMTT Repair") {
    Damaged d;
    zone({&d.v.self});
    tick(0.1f);
    CHECK(sparesIn(r.modules) == 10000 - 2);
    CHECK(sparesIn(d.modules) == 7);
}

TEST_CASE_FIXTURE(RepairZone, "Ремонт: свои и союзники - да, враги (отношение 2 или 4) - нет") {
    // Подготовка: HEMTT Repair - команда 1. В зоне машины команд 1, 2, 3, 4, 5; отношения команд 3 и 4
    // к команде 1 - «враги» (2 и 4), остальные - 0, 1, 3.
    Damaged own(1), team2(2), enemyA(3), enemyB(4), team5(5);
    g_relations[{1, 1}] = 0;
    g_relations[{2, 1}] = 1;
    g_relations[{3, 1}] = 2;
    g_relations[{4, 1}] = 4;
    g_relations[{5, 1}] = 3;
    zone({&own.v.self, &team2.v.self, &enemyA.v.self, &enemyB.v.self, &team5.v.self});

    tick(0.1f);

    CHECK(moduleHp(own.modules, 0) == 52);
    CHECK(moduleHp(team2.modules, 0) == 52);
    CHECK(moduleHp(enemyA.modules, 0) == 50);
    CHECK(moduleHp(enemyB.modules, 0) == 50);
    CHECK(moduleHp(team5.modules, 0) == 52);
    // Дипломатию спрашивали про (команда цели, команда HEMTT Repair), у мира самой HEMTT Repair.
    CHECK(g_relationAsks.front() == std::pair<int, int>{1, 1});
    CHECK(g_relationAsks[2] == std::pair<int, int>{3, 1});
    CHECK(g_diplomacyWorld == &g_world);
}

TEST_CASE_FIXTURE(RepairZone, "Ремонт: сама HEMTT Repair, машины без команды и без модулей пропускаются") {
    // Подготовка: в зоне сама HEMTT Repair (повреждённая), машина без команды, пехотинец (нет модулей),
    // пустая запись и обычная повреждённая машина.
    moduleHp(r.modules, 0) = 10;
    Damaged noTeam;
    noTeam.v.setTeam(nullptr);
    Vehicle infantry;
    RefBlock empty = makeBlock(nullptr);
    Damaged ok;
    zone({&r.v.self, &noTeam.v.self, &infantry.self, nullptr, &empty, &ok.v.self});

    tick(0.1f);

    CHECK(moduleHp(r.modules, 0) == 10);       // себя не чинит
    CHECK(moduleHp(noTeam.modules, 0) == 50);  // без команды - нельзя
    CHECK(moduleHp(ok.modules, 0) == 52);      // остальным это не мешает
}

TEST_CASE_FIXTURE(RepairZone, "Ремонт: у HEMTT Repair нет команды или в мире нет дипломатии - не чинит никого") {
    Damaged d;
    zone({&d.v.self});
    g_hasDiplomacy = false;
    tick(0.1f);
    CHECK(moduleHp(d.modules, 0) == 50);

    g_hasDiplomacy = true;
    r.v.setTeam(nullptr);
    g_repairTimers.clear();
    tick(0.1f);
    CHECK(moduleHp(d.modules, 0) == 50);
}

TEST_CASE_FIXTURE(RepairZone, "Ремонт: склад пуст - ничего не происходит") {
    Damaged d;
    zone({&d.v.self});
    sparesIn(r.modules) = 0;
    tick(0.1f);
    CHECK(moduleHp(d.modules, 0) == 50);
    CHECK(g_incHealth.empty());
}

TEST_CASE_FIXTURE(RepairZone, "Ремонт: склад кончился во время импульса - остальные ждут, в логе об этом запись") {
    Damaged a, b;
    zone({&a.v.self, &b.v.self});
    sparesIn(r.modules) = 2;
    LogTail log(logFile());
    tick(0.1f);
    CHECK(moduleHp(a.modules, 0) == 52);
    CHECK(moduleHp(b.modules, 0) == 50);
    CHECK(sparesIn(r.modules) == 0);
    CHECK(log.has("repair: out of spare parts"));
}

TEST_CASE_FIXTURE(Engine, "Ремонт: долг за броню свой у каждой HEMTT Repair и переживает импульсы") {
    // Две HEMTT Repair чинят по слою 25 мм (+0.5 мм = 0.5 запчасти за импульс).
    ModuleSpec m;
    m.armor[0] = {10};
    m.armorFull[0] = {25};
    Repair r1, r2;
    Damaged d1(1, {m}), d2(1, {m});
    turnOn(r1.ammo, kAmmoOff);
    turnOn(r2.ammo, kAmmoOff);
    r1.v.setTargets(r1.ammo, {&d1.v.self});
    r2.v.setTargets(r2.ammo, {&d2.v.self});

    tickRepair(r1.ammo.p(), 0.5f);  // r1: долг 0.5
    tickRepair(r2.ammo.p(), 0.5f);  // r2: долг 0.5
    CHECK(sparesIn(r1.modules) == 10000);
    tickRepair(r1.ammo.p(), 0.5f);  // r1: 0.5 + 0.5 = 1 -> списана 1
    CHECK(sparesIn(r1.modules) == 9999);
    CHECK(sparesIn(r2.modules) == 10000);  // у r2 долг свой, всё ещё 0.5
    CHECK(g_armorDebt[r2.ammo.p()] == doctest::Approx(0.5f));
}

TEST_CASE_FIXTURE(RepairZone, "Ремонт через режим F4: update боеприпасов HEMTT Repair делает импульс") {
    Damaged d;
    zone({&d.v.self});
    circleField(r.ammo) = newCircle();
    hookAmmoUpdate(r.ammo.p(), 0.1f);
    CHECK(callNames() == std::vector<std::string>{"ammo.update"});  // оригинал - первым
    CHECK(moduleHp(d.modules, 0) == 52);
    REQUIRE(g_draws.size() == 1);  // и круг ремонта
    CHECK(g_draws[0].material == "materials/rounds/repairRecharge.material");
}

// ================================================================ лог

TEST_CASE_FIXTURE(RepairZone, "Лог ремонта: что сделано и сколько потрачено; не больше 40 строк") {
    Damaged d(1, {ModuleSpec{0, 1000}, ModuleSpec{100, 100}});  // общее здоровье 100
    zone({&d.v.self});
    LogTail log(logFile());
    tick(0.5f);
    char line[160];
    snprintf(line, sizeof line, "repair: target=%p +20 hp, 0 armor layers, total hp 120; spent 20 spares, 9980 left",
             static_cast<void*>(&d.v));
    CHECK(log.has(line));
    for (int i = 0; i < 50; ++i) tick(0.5f);
    CHECK(log.count("repair: target=") == 40);
}

TEST_CASE_FIXTURE(Engine, "Лог событий: не больше 50 за запуск") {
    Spares s;
    LogTail log(logFile());
    for (int i = 0; i < 60; ++i) trace("test event", s.ammo.p());
    CHECK(log.count("test event facet=") == 50);
}
