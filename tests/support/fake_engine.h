// Поддельный «движок» для тестов поведения плагина (plugin_test.cpp).
//
// В игре плагин работает с объектами движка, которые ему не принадлежат: машинами, их компонентами
// (боеприпасы, запчасти, ремонт, модули), командами, дипломатией. Он знает о них очень мало - только
// несколько полей по известным смещениям и несколько методов по номерам в таблице методов (vtable).
// Здесь эти объекты сделаны вручную ровно в том объёме, который плагин использует, - по тем же
// смещениям, что записаны в плагине и в docs/engine_notes.md. Каждая подделка записывает, что с ней
// делали, а тест потом проверяет эту запись.
//
// Подключается ПОСЛЕ spares_supply.cpp: пользуется его типами (RefBlock, MsvcString) и глобальными
// переменными (g_a - найденные адреса, g_orig* - исходные функции игры).
#pragma once

// Подделки ниже пользуются типами из безымянного пространства имён плагина (RefBlock) - для тестов это
// нормально, предупреждение компилятора об этом отключаем.
#pragma GCC diagnostic ignored "-Wsubobject-linkage"

#include <array>
#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace fake {

// ---------------------------------------------------------------- журнал вызовов

// Один вызов «исходной функции игры»: что вызвано (например "ammo.on"), для какого объекта, с каким dt.
struct Call {
    std::string what;
    void* obj;
    float dt;
};
inline std::vector<Call> g_calls;

inline void record(const char* what, void* obj, float dt = 0) { g_calls.push_back({what, obj, dt}); }
// Сколько раз вызвано what (для объекта obj; obj = nullptr - для любого).
inline int calls(const std::string& what, void* obj = nullptr) {
    int n = 0;
    for (auto& c : g_calls)
        if (c.what == what && (!obj || c.obj == obj)) ++n;
    return n;
}
// Имена всех вызовов по порядку - чтобы проверить последовательность.
inline std::vector<std::string> callNames() {
    std::vector<std::string> out;
    for (auto& c : g_calls) out.push_back(c.what);
    return out;
}

// Адрес функции как число (так их хранит vtable).
template <class F>
void* fn(F f) {
    return reinterpret_cast<void*>(f);
}

// ---------------------------------------------------------------- блок счётчика ссылок

// Движок держит объекты через «блок ссылки»: счётчик сильных ссылок и указатель на объект. Когда счётчик
// падает до нуля, движок вызывает метод блока «освободить» (слот 2). Плагин, беря ссылку, обязан её
// вернуть - иначе утечка или преждевременное удаление. g_freed считает такие освобождения: в тестах блоки
// живут всё время, и освобождение означало бы ошибку в подсчёте.
inline int g_freed = 0;
inline void rbFree(void*) { ++g_freed; }
inline void* g_rbVtbl[3] = {nullptr, nullptr, fn(&rbFree)};

inline RefBlock makeBlock(void* object) { return RefBlock{g_rbVtbl, 1, 0, object}; }

// ---------------------------------------------------------------- компонент (facet)

// Компонент машины - кусок памяти, в начале которого адрес его vtable (по нему плагин узнаёт класс),
// а дальше поля по известным смещениям. Нужные плагину поля:
//   +0x10  блок ссылки на машину-владельца
//   боеприпасы/заправщик: +0x30..+0x38 список машин в зоне, +0x50 нарисованный круг, +0x59 «выключен»
//   запчасти: +0x28..+0x30 список машин в зоне, +0x51 «выключен»;  ремонт: +0x49 «выключен»
//   модули (DamageableModulesFacet): +0xcc общее здоровье, +0xd0..+0xd8 массив модулей, +0x130 запчасти
struct Facet {
    alignas(16) uint8_t mem[0x200]{};
    template <class T>
    T& at(size_t off) {
        return *reinterpret_cast<T*>(mem + off);
    }
    void* p() { return mem; }
};

// Смещения флагов «выключен» (1 - выключен, 0 - включён), как в исходных функциях игры.
constexpr size_t kAmmoOff = 0x59, kSparesOff = 0x51, kRepairOff = 0x49;

// ---------------------------------------------------------------- команды и дипломатия

// Команда (игрок): плагину нужен только её номер (метод слота 10).
struct Team {
    void** vtbl;
    int id;
};
inline int teamId(void* t) { return static_cast<Team*>(t)->id; }
inline void* g_teamVtbl[11] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                               nullptr, nullptr, nullptr, nullptr, fn(&teamId)};

// Дипломатия мира: отношение между двумя командами (метод слота 22). Плагин считает «нельзя чинить»
// отношения 2 и 4 (как у HEMTT Supply для патронов). Отношения задаёт тест; по умолчанию - 0.
struct Diplomacy {
    void** vtbl;
};
inline std::map<std::pair<int, int>, int> g_relations;  // (команда цели, команда владельца) -> отношение
inline std::vector<std::pair<int, int>> g_relationAsks;  // какие пары спрашивал плагин
inline int relation(void*, int target, int owner) {
    g_relationAsks.push_back({target, owner});
    auto it = g_relations.find({target, owner});
    return it == g_relations.end() ? 0 : it->second;
}
inline void* g_dipVtbl[23] = {};
inline Diplomacy g_dip{g_dipVtbl};
inline int g_world = 0;               // «мир»: важен только адрес - его машина отдаёт, а дипломатия получает
inline bool g_hasDiplomacy = true;    // false - дипломатии нет (функция игры вернула nullptr)
inline void* g_diplomacyWorld = nullptr;
// Подделка функции игры «дипломатия мира» (в GOG-сборке 0x541a00).
inline void* diplomacyOf(void* world) {
    g_diplomacyWorld = world;
    return g_hasDiplomacy ? &g_dip : nullptr;
}

// ---------------------------------------------------------------- модули и броня

// Описание одного модуля машины (двигатель, башня, ...): текущее и максимальное здоровье, броня по 6
// сторонам - список слоёв, у каждого текущая и исходная (полная) толщина.
struct ModuleSpec {
    int hp = 100, maxHp = 100;
    std::array<std::vector<float>, 6> armor{};      // текущая толщина слоёв
    std::array<std::vector<float>, 6> armorFull{};  // исходная толщина (из описания модуля)
    bool noDesc = false;                            // модуль без описания (плагин его пропускает)
};

// Память модулей в том виде, в каком её читает плагин:
//   модуль - 0x148 байт: +0x00 описание, +0x0c здоровье, +0x28 + сторона*0x18 - текущая толщина
//            (vector<float>: начало, конец, ёмкость);
//   описание: +0x28 максимум здоровья, +0x90 + сторона*0x18 - слои (по 16 байт, первые 4 - исходная толщина).
struct ModuleMemory {
    std::vector<uint8_t> modules;
    std::deque<std::vector<uint8_t>> descs;
    std::deque<std::vector<float>> cur;
    std::deque<std::vector<std::array<float, 4>>> layers;
};

// ---------------------------------------------------------------- машина

struct Iter;
inline void* vehicleFacets(void* self, void** out);
inline void* vehicleWorld(void*);
inline char* vehicleInfo(void* self);
inline void* g_vehicleVtbl[23] = {};

// Машина (или пехотинец): список компонентов, команда, мир. Методы, которые зовёт плагин:
//   слот 11 (+0x58) - итератор по компонентам; слот 15 (+0x78) - мир; слот 22 (+0xb0) - блок
//   сведений, в котором по +0x20 лежит команда.
// Объект нельзя копировать и перемещать: компоненты хранят указатель на его блок ссылки.
struct Vehicle {
    void** vtbl = g_vehicleVtbl;
    alignas(8) uint8_t info[0x30]{};
    RefBlock self = makeBlock(this);
    Team team;
    bool nullIterator = false;  // true - итератор не выдаётся (движок вернул пустоту)
    std::deque<Facet> facets;
    std::deque<RefBlock> blocks;
    std::deque<ModuleMemory> moduleMem;
    std::vector<RefBlock*> targets;  // «машины в зоне» для компонента боеприпасов

    explicit Vehicle(int teamId = 1) : team{g_teamVtbl, teamId} { setTeam(&team); }
    Vehicle(const Vehicle&) = delete;
    Vehicle& operator=(const Vehicle&) = delete;

    void setTeam(Team* t) { memcpy(info + 0x20, &t, sizeof t); }

    // Добавить компонент класса vt (адрес vtable). Как в игре, компонент создаётся выключенным - флаг
    // «выключен» ставится только тот, что есть у этого класса (у другого класса по тому же смещению может
    // лежать другое поле: например, у боеприпасов +0x50..+0x57 - указатель на круг).
    Facet& add(uintptr_t vt);

    // Компонент модулей (DamageableModulesFacet) с модулями specs и запасом запчастей spares.
    Facet& addModules(const std::vector<ModuleSpec>& specs, int spares);

    // Задать список «машин в зоне» у компонента боеприпасов ([ammo+0x30], [ammo+0x38]).
    void setTargets(Facet& ammo, std::vector<RefBlock*> list) {
        targets = std::move(list);
        ammo.at<RefBlock**>(0x30) = targets.data();
        ammo.at<RefBlock**>(0x38) = targets.data() + targets.size();
    }
};

// Итератор по компонентам машины. Методы: слот 0 - удалить (с флагом 1), слот 3 - «конец?»,
// слот 4 - «следующий», слот 6 - «текущий»: отдаёт блок ссылки, увеличив счётчик (плагин потом его
// уменьшает). g_iterDeleted / g_iterDeleteFlags - сколько итераторов удалено и с какими флагами.
inline int g_iterCreated = 0, g_iterDeleted = 0;
inline std::vector<int> g_iterDeleteFlags;
struct Iter {
    void** vtbl;
    Vehicle* v;
    size_t i;
};
inline void iterDelete(void* it, int flag) {
    ++g_iterDeleted;
    g_iterDeleteFlags.push_back(flag);
    delete static_cast<Iter*>(it);
}
inline bool iterAtEnd(void* it) {
    auto i = static_cast<Iter*>(it);
    return i->i >= i->v->blocks.size();
}
inline void iterNext(void* it) { ++static_cast<Iter*>(it)->i; }
inline RefBlock** iterCurrent(void* it, RefBlock** tmp) {
    auto i = static_cast<Iter*>(it);
    RefBlock* rb = &i->v->blocks[i->i];
    ++rb->strong;
    *tmp = rb;
    return tmp;
}
inline void* g_iterVtbl[7] = {fn(&iterDelete), nullptr, nullptr, fn(&iterAtEnd),
                              fn(&iterNext),   nullptr, fn(&iterCurrent)};

inline void* vehicleFacets(void* self, void** out) {
    auto v = static_cast<Vehicle*>(self);
    if (v->nullIterator) {
        *out = nullptr;
    } else {
        *out = new Iter{g_iterVtbl, v, 0};
        ++g_iterCreated;
    }
    return out;
}
inline void* vehicleWorld(void*) { return &g_world; }
inline char* vehicleInfo(void* self) { return reinterpret_cast<char*>(static_cast<Vehicle*>(self)->info); }

inline Facet& Vehicle::add(uintptr_t vt) {
    Facet& f = facets.emplace_back();
    f.at<uintptr_t>(0) = vt;
    f.at<RefBlock*>(0x10) = &self;
    if (vt == g_a.ammoVt || vt == g_a.fuelVt) f.at<uint8_t>(kAmmoOff) = 1;
    if (vt == g_a.sparesVt) f.at<uint8_t>(kSparesOff) = 1;
    if (vt == g_a.repairVt) f.at<uint8_t>(kRepairOff) = 1;
    blocks.push_back(makeBlock(f.p()));
    return f;
}

inline Facet& Vehicle::addModules(const std::vector<ModuleSpec>& specs, int spares) {
    Facet& f = add(g_a.modulesVt);
    ModuleMemory& m = moduleMem.emplace_back();
    m.modules.assign(specs.size() * 0x148, 0);
    int total = 0;
    for (size_t i = 0; i < specs.size(); ++i) {
        const ModuleSpec& s = specs[i];
        uint8_t* mod = m.modules.data() + i * 0x148;
        std::vector<uint8_t>& desc = m.descs.emplace_back(0x120, 0);
        char* descPtr = s.noDesc ? nullptr : reinterpret_cast<char*>(desc.data());
        memcpy(mod + 0x00, &descPtr, 8);
        memcpy(mod + 0x0c, &s.hp, 4);
        memcpy(desc.data() + 0x28, &s.maxHp, 4);
        total += s.hp;
        for (int side = 0; side < 6; ++side) {
            std::vector<float>& cur = m.cur.emplace_back(s.armor[side]);
            float* cb[3] = {cur.data(), cur.data() + cur.size(), cur.data() + cur.size()};
            memcpy(mod + 0x28 + side * 0x18, cb, sizeof cb);
            auto& lay = m.layers.emplace_back();
            for (float full : s.armorFull[side]) lay.push_back({full, 0, 0, 0});
            auto lb = reinterpret_cast<char*>(lay.data());
            char* lp[3] = {lb, lb + lay.size() * 16, lb + lay.size() * 16};
            memcpy(desc.data() + 0x90 + side * 0x18, lp, sizeof lp);
        }
    }
    f.at<int>(0xcc) = total;
    f.at<uint8_t*>(0xd0) = m.modules.data();
    f.at<uint8_t*>(0xd8) = m.modules.data() + m.modules.size();
    f.at<int>(0x130) = spares;
    return f;
}

// Чтение модулей обратно - для проверок.
inline int& moduleHp(Facet& modules, size_t i) {
    return *reinterpret_cast<int*>(modules.at<uint8_t*>(0xd0) + i * 0x148 + 0x0c);
}
inline float armorAt(Facet& modules, size_t i, int side, size_t k) {
    auto cur = reinterpret_cast<float**>(modules.at<uint8_t*>(0xd0) + i * 0x148 + 0x28 + side * 0x18);
    return cur[0][k];
}
inline int& sparesIn(Facet& modules) { return modules.at<int>(0x130); }
inline int& totalHp(Facet& modules) { return modules.at<int>(0xcc); }

// ---------------------------------------------------------------- функции игры, которые зовёт плагин

// DamageableModulesFacet::incHealth(модули, номер модуля, сколько) - ремонт модуля экипажем. Подделка ведёт
// себя как движок: добавляет здоровье (не выше максимума), пересчитывает общее здоровье и списывает
// запчасти у самой машины (плагин должен их вернуть - HEMTT Repair платит за ремонт сама).
struct IncHealthCall {
    void* modules;
    size_t index;
    int amount;
};
inline std::vector<IncHealthCall> g_incHealth;
inline void incHealth(void* modules, size_t index, int amount) {
    g_incHealth.push_back({modules, index, amount});
    auto m = static_cast<char*>(modules);
    char* mod = *reinterpret_cast<char**>(m + 0xd0) + index * 0x148;
    int& hp = *reinterpret_cast<int*>(mod + 0x0c);
    int maxHp = *reinterpret_cast<int*>(*reinterpret_cast<char**>(mod) + 0x28);
    int gain = std::min(amount, maxHp - hp);
    hp += gain;
    *reinterpret_cast<int*>(m + 0xcc) += gain;
    *reinterpret_cast<int*>(m + 0x130) -= gain;
}

// Таблица материалов игры: по смещению 0x120 - строка (std::string MSVC) с путём материала круга патронов.
inline const char* const kAmmoMaterial = "materials/rounds/ammoRecharge.material";
alignas(16) inline char g_materialTable[0x200];
inline int g_materialTableCalls = 0;
inline char* materialTable() {
    ++g_materialTableCalls;
    return g_materialTable;
}
inline MsvcString& ammoMaterialEntry() { return *reinterpret_cast<MsvcString*>(g_materialTable + 0x120); }

// Инструкция «lea r8,[rax+0x120]» из функции круга: смещение 0x120 - строка ammoRecharge в таблице,
// 0xE0 - sparesRecharge. Плагин на время перерисовки круга HEMTT Spares вписывает сюда 0xE0.
alignas(8) inline uint8_t g_circleLea[8];
inline uint32_t leaOffset() {
    uint32_t v;
    memcpy(&v, g_circleLea + 3, 4);
    return v;
}

// Нарисованный круг - просто уникальный объект; [ammo+0x50] указывает на него.
inline std::deque<int> g_circleObjects;
inline void* newCircle() { return &g_circleObjects.emplace_back(0); }
inline void*& circleField(Facet& ammo) { return ammo.at<void*>(0x50); }

// Функция игры «(пере)создать круг зоны» компонента боеприпасов (в GOG-сборке 0x120610). Подделка
// запоминает, каким материалом круг нарисован бы: смещение из lea и строку в таблице материалов.
struct CircleDraw {
    void* ammo;
    uint32_t lea;
    std::string material;
};
inline std::vector<CircleDraw> g_draws;
inline void showCircle(void* ammo) {
    const MsvcString& e = ammoMaterialEntry();
    g_draws.push_back({ammo, leaOffset(), std::string(e.ptr, e.size)});
    *reinterpret_cast<void**>(static_cast<char*>(ammo) + 0x50) = newCircle();
}

// ---------------------------------------------------------------- исходные функции перехваченных методов

// Что делают оригиналы в игре, в том объёме, который важен плагину: включение/выключение ставят флаг,
// включение боеприпасов рисует круг, выключение - убирает. Каждый вызов записывается в журнал.
inline void setFlag(void* f, size_t off, uint8_t v) { static_cast<uint8_t*>(f)[off] = v; }
inline void origAmmoOn(void* f) {
    record("ammo.on", f);
    setFlag(f, kAmmoOff, 0);
    *reinterpret_cast<void**>(static_cast<char*>(f) + 0x50) = newCircle();
}
inline void origAmmoOff(void* f) {
    record("ammo.off", f);
    setFlag(f, kAmmoOff, 1);
    *reinterpret_cast<void**>(static_cast<char*>(f) + 0x50) = nullptr;
}
inline void origFuelOn(void* f) {
    record("fuel.on", f);
    setFlag(f, kAmmoOff, 0);
}
inline void origFuelOff(void* f) {
    record("fuel.off", f);
    setFlag(f, kAmmoOff, 1);
}
inline void origSparesOn(void* f) {
    record("spares.on", f);
    setFlag(f, kSparesOff, 0);
}
inline void origSparesOff(void* f) {
    record("spares.off", f);
    setFlag(f, kSparesOff, 1);
}
inline void origRepairOn(void* f) {
    record("repair.on", f);
    setFlag(f, kRepairOff, 0);
}
inline void origRepairOff(void* f) {
    record("repair.off", f);
    setFlag(f, kRepairOff, 1);
}
inline void origAmmoUpdate(void* f, float dt) { record("ammo.update", f, dt); }
inline void origFuelUpdate(void* f, float dt) { record("fuel.update", f, dt); }
inline void origSparesUpdate(void* f, float dt) { record("spares.update", f, dt); }
inline void origRepairUpdate(void* f, float dt) { record("repair.update", f, dt); }
inline bool g_canAnswer = true;  // что ответит исходный canRecharge
inline void* g_canTarget = nullptr;
inline bool origAmmoCanRecharge(void* f, void* target) {
    record("ammo.canRecharge", f);
    g_canTarget = target;
    return g_canAnswer;
}

// vtable классов компонентов: плагин сравнивает с ними адрес в начале компонента, а сами методы через
// них не вызывает (исходные функции он берёт из g_orig*). Поэтому это просто разные адреса.
inline void* g_ammoVt[20], *g_fuelVt[20], *g_sparesVt[20], *g_repairVt[20], *g_modulesVt[40], *g_otherVt[20];
inline uintptr_t vt(void** table) { return reinterpret_cast<uintptr_t>(table); }

inline float g_supplyRadius = 20.0f;  // глобальный радиус зоны снабжения игры

// Привести «игру» и плагин в исходное состояние: адреса найдены, исходные функции - подделки выше,
// журналы пусты, состояние плагина (круги, таймеры ремонта, долги, счётчики лога) сброшено.
inline void reset() {
    g_calls.clear();
    g_freed = 0;
    g_iterCreated = g_iterDeleted = 0;
    g_iterDeleteFlags.clear();
    g_relations.clear();
    g_relationAsks.clear();
    g_hasDiplomacy = true;
    g_diplomacyWorld = nullptr;
    g_incHealth.clear();
    g_draws.clear();
    g_materialTableCalls = 0;
    g_canAnswer = true;
    g_canTarget = nullptr;
    g_supplyRadius = 20.0f;

    g_vehicleVtbl[11] = fn(&vehicleFacets);
    g_vehicleVtbl[15] = fn(&vehicleWorld);
    g_vehicleVtbl[22] = fn(&vehicleInfo);
    g_dipVtbl[22] = fn(&relation);

    memset(g_materialTable, 0, sizeof g_materialTable);
    ammoMaterialEntry() = MsvcString{kAmmoMaterial, {}, strlen(kAmmoMaterial), strlen(kAmmoMaterial)};
    const uint8_t lea[8] = {0x4C, 0x8D, 0x80, 0x20, 0x01, 0x00, 0x00, 0xCC};  // lea r8,[rax+0x120]
    memcpy(g_circleLea, lea, sizeof lea);

    g_a = Addr{};
    g_a.ammoVt = vt(g_ammoVt);
    g_a.fuelVt = vt(g_fuelVt);
    g_a.sparesVt = vt(g_sparesVt);
    g_a.repairVt = vt(g_repairVt);
    g_a.modulesVt = vt(g_modulesVt);
    g_a.supplyRadius = reinterpret_cast<uintptr_t>(&g_supplyRadius);
    g_a.showCircle = reinterpret_cast<uintptr_t>(&showCircle);
    g_a.circleLea = reinterpret_cast<uintptr_t>(g_circleLea);
    g_a.materialTable = reinterpret_cast<uintptr_t>(&materialTable);
    g_a.incHealth = reinterpret_cast<uintptr_t>(&incHealth);
    g_a.diplomacy = reinterpret_cast<uintptr_t>(&diplomacyOf);

    g_origAmmoOn = &origAmmoOn;
    g_origAmmoOff = &origAmmoOff;
    g_origFuelOn = &origFuelOn;
    g_origFuelOff = &origFuelOff;
    g_origSparesOn = &origSparesOn;
    g_origSparesOff = &origSparesOff;
    g_origRepairOn = &origRepairOn;
    g_origRepairOff = &origRepairOff;
    g_origAmmoCanRecharge = &origAmmoCanRecharge;
    g_origAmmoUpdate = &origAmmoUpdate;
    g_origFuelUpdate = &origFuelUpdate;
    g_origSparesUpdate = &origSparesUpdate;
    g_origRepairUpdate = &origRepairUpdate;

    g_ownCircles.clear();
    g_circleWritable = true;
    g_repairTimers.clear();
    g_armorDebt.clear();
    g_events = 0;
    g_repairLogs = 0;
}

// Включить компонент так, как это сделала бы игра без участия плагина (флаг «выключен» = 0).
inline void turnOn(Facet& f, size_t offFlag) { f.at<uint8_t>(offFlag) = 0; }
inline bool isOnFlag(Facet& f, size_t offFlag) { return f.at<uint8_t>(offFlag) == 0; }

}  // namespace fake
