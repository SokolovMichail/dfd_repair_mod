// SparesSupply.dll - машины снабжения в режиме F4 («SUPPLY MODE»): HEMTT Spares раздаёт запчасти по зоне,
// HEMTT Repair чинит технику в зоне и восстанавливает ей броню.
//
// Как устроено в logic.dll (адреса - для GOG 1.09.1221, сам плагин ищет всё по шаблонам/RTTI):
//  * Режим F4 (AiUnpack/AiDo/AiPackRechargeState) берёт у машины ПЕРВЫЙ IRechargerFacet
//    (заправщик RefuellerFacet или боеприпасы AmmoRechargerFacet) и включает/выключает только его
//    через слоты vtable 14 (+0x70) / 13 (+0x68). Компонент запчастей SparesRechargerFacet - другой
//    интерфейс, поэтому в режим F4 не попадает, хотя сам по себе умеет раздавать по радиусу.
//  * При разборе колонки abilities (сборщики описаний машин 0x51c570 и танков 0x521ce0) компонент
//    ammunition_recharge НЕ создаётся, если есть refuel / support / repair (флаг r15b).
//  * Апдейт запчастей (слот 10, 0x439e00) движок сам НЕ вызывает - его тикает только приказ по клику
//    (AiSparesRechargeUnitState 0xffac0: jmp [facet+0x50] с dt). Апдейты заправщика/боеприпасов
//    движок вызывает каждый кадр.
//  * Ремонт (abilities=repair -> RepairerFacet) недоделан: компонент создаётся выключенным, включает его
//    никто, а его update (0x363790) ищет цели в радиусе и тут же выбрасывает результат - список целей
//    всегда пуст. Команду цели он бы и не проверял. Броню (толщину слоёв) в бою не восстанавливает ничто.
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
//
// HEMTT Repair (abilities=ammunition_recharge,repair; RepairerFacet - метка, сам ничего не чинит):
//  7. Убирает запрет «repair блокирует ammunition_recharge» (оба сборщика) - кнопка F4.
//  8. Патроны такая машина тоже не раздаёт (п. 4).
//     RepairerFacet сам является IRechargerFacet (IRepairerFacet наследует его) и создаётся раньше
//     боеприпасов, поэтому режим F4 включает/выключает именно его. Слоты 14/13 ремонта перехвачены:
//     флаги ставятся как обычно, а включение дублируется на компонент боеприпасов. Собственный update
//     ремонта (слот 10) заглушён.
//  9. Пока включён режим F4, раз в kRepairPeriod берёт машины, которые компонент боеприпасов уже собрал
//     в зоне (список [ammo+0x30], обновляется в его update), отбрасывает себя и чужих (та же проверка
//     дипломатии, что в canRecharge: отношение 2 или 4 - нельзя) и каждой:
//       - чинит все повреждённые модули на kRepairFraction от максимума через
//         DamageableModulesFacet::incHealth (0x225ba0) - с пересчётом общего здоровья и состояния модуля;
//       - добавляет каждому слою брони kArmorFraction исходной толщины (не больше исходной).
//     Платит своими запчастями ([модули+0x130]): 1 за 1 здоровья, kSparesPerMm за 1 мм брони; запчасти,
//     которые incHealth (ремонт экипажем) списывает у цели, возвращаются ей. Запас кончился - ремонт стоит.
//     Модули: [facet+0xd0..+0xd8], по 0x148 байт: +0x00 описание, +0x0c здоровье, +0x28 + сторона*0x18 -
//     текущая толщина слоёв (vector<float>), сторон 6. Описание: +0x28 максимум здоровья,
//     +0x90 + сторона*0x18 - слои брони (по 16 байт, +0 исходная толщина). См. DamageableModulesFacet::
//     initialize 0x226280 и функцию урона 0x310790 (износ брони).
// 10. Круг зоны рисуется материалом materials/rounds/repairRecharge.material (в моде): на время вызова
//     0x120610 строка ammoRecharge в таблице материалов подменяется путём ремонта и сразу возвращается.
//
// Лог: bin\plugins\SparesSupply.log

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

#include "../../common/patch.h"

#ifndef SPARES_SUPPLY_VERSION  // задаётся из CMake (MOD_VERSION)
#define SPARES_SUPPLY_VERSION "dev"
#endif

namespace {

using dfd::Image;

// ---------------------------------------------------------------- что ищем в logic.dll

constexpr int kSlotUpdate = 10;       // +0x50, void(float dt)
constexpr int kSlotDeactivate = 13;   // +0x68
constexpr int kSlotActivate = 14;     // +0x70
constexpr int kSlotCanRecharge = 17;  // +0x88, bool(target)

// Начала функций в слотах vtable: подтверждают, что это те же функции и те же смещения полей.
constexpr const char* kPrologueRechargerOff = "48 89 5C 24 08 57 48 83 EC 20 33 FF C6 41 59 01";  // ammo, fuel
constexpr const char* kPrologueRechargerOn = "40 53 48 83 EC 20 66 C7 41 58 00 00 48 8B D9";    // ammo, fuel
constexpr const char* kPrologueAmmoUpdate = "41 56 48 81 EC A0 00 00 00 48 83 79 78 00 4C 8B F1";
constexpr const char* kPrologueAmmoCan = "40 55 56 41 57 48 83 EC 50 48 8B E9 48 8B F2 48 8B 49 10";
constexpr const char* kPrologueFuelUpdate = "40 56 48 81 EC 80 00 00 00 48 83 B9 88 00 00 00 00";
constexpr const char* kPrologueSparesUpdate = "40 53 48 83 EC 20 80 79 51 00 48 8B D9 0F 85";
constexpr const char* kPrologueSparesOff = "C6 41 51 01 C3";
constexpr const char* kPrologueSparesOn = "66 C7 41 50 00 00 C3";
constexpr const char* kPrologueRepairUpdate = "48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 57 41 54 41 55 41 56 41 57";
constexpr const char* kPrologueRepairOff = "C6 41 49 01 C3";
constexpr const char* kPrologueRepairOn = "66 C7 41 48 00 00 C3";

// «mov r15b, 1» после найденного support - в сборщике машин и в сборщике танков (ровно 2 совпадения).
constexpr const char* kSigSupportBlocksAmmo =
    "41 B7 01 8B C3 FF C3 48 8D 0D ?? ?? ?? ?? 48 89 4D C8 89 45 D0 C7 45 D4 01 00 00 00 0F B6 86 14 02 00 00 "
    "3C 20 73 ?? 48 FF C0 48 03 C0 0F 10 45 C8 0F 11 04 C6 FE 86 14 02 00 00 4D 8B 4D 00 49 8B 14 24 49 3B D1 "
    "0F 84 ?? ?? ?? ??";
// То же для любой способности: «mov r15b,1; mov eax,ebx; inc ebx; lea rcx,[rip+имя компонента]».
// Нужные места выбираются по строке в lea («[Leonardo.RepairerFacet]» - ровно 2: машины и танки).
constexpr const char* kSigAbilityFlag = "41 B7 01 8B C3 FF C3 48 8D 0D ?? ?? ?? ??";
// Поиск целей запчастей: movss xmm2, [rip+disp] - радиус 20.0f.
constexpr const char* kSigSparesRadiusLoad = "F3 0F 10 15 ?? ?? ?? ?? 48 8B D5 FF 50 10 48 8B 6C 24 40 48 3B 6C 24 48";
// Конструктор AmmoRechargerFacet: movss xmm0, [rip+supplyRadius]; movss [r14+0x5c], xmm0.
constexpr const char* kSigSupplyRadiusUse = "F3 0F 10 05 ?? ?? ?? ?? F3 41 0F 11 46 5C 49 8B 4E 10 48 85 C9";
// Круг зоны боеприпасов: функция (пере)создания и lea r8, [rax+0x120] (материал ammoRecharge) в ней.
constexpr const char* kSigShowCircle =
    "48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 41 56 48 83 EC 30 48 83 79 50 00";
constexpr const char* kSigCircleMaterialLea = "4C 8D 80 20 01 00 00 FF D3 8B 4F 5C 48 89 47 50 89 48 3C 48 8B 5F 10";
constexpr size_t kMaterialTableCallBack = 0x0c;  // call «таблица материалов» (0xff050) - за 12 байт до lea
// RepairerFacet::update: «r8d = max(1, ...); rdx = индекс модуля; call DamageableModulesFacet::incHealth».
constexpr const char* kSigIncHealthCall = "41 B8 01 00 00 00 85 C0 44 0F 45 C0 48 8B D6 E8 ?? ?? ?? ??";
constexpr const char* kPrologueIncHealth = "48 8B C4 48 89 58 08 48 89 68 18 48 89 70 20 48 89 50 10";
// AmmoRechargerFacet::canRecharge, +0x3a: owner->vfunc(+0x78) (мир) -> call «дипломатия мира» (0x541a00).
constexpr const char* kSigDiplomacyCall = "FF 50 78 48 8B C8 E8 ?? ?? ?? ?? 4C 8B F8 48 85 C0";
constexpr size_t kDiplomacyCallAt = 0x3a;

constexpr size_t kAmmoCircleField = 0x50;
constexpr uint32_t kMaterialAmmo = 0x120, kMaterialSpares = 0xE0;  // materials/rounds/{ammo,spares}Recharge
constexpr const char* kRepairMaterial = "materials/rounds/repairRecharge.material";

// Поля компонентов.
constexpr size_t kRechargerOffFlag = 0x59;  // Ammo/Refueller: isActive = (byte +0x59 == 0)
constexpr size_t kSparesOffFlag = 0x51;     // Spares: isActive = (byte +0x51 == 0)
constexpr size_t kAmmoTargets = 0x30;       // Ammo: vector<RefBlock*> машин в зоне

// DamageableModulesFacet
constexpr size_t kModulesBegin = 0xd0, kModulesEnd = 0xd8, kModuleSize = 0x148, kModulesTotalHealth = 0xcc;
constexpr size_t kModulesSpares = 0x130;  // запчасти машины (int; максимум - +0x12c), слоты 26-29 vtable
constexpr size_t kModuleDesc = 0x00, kModuleHealth = 0x0c, kModuleArmor = 0x28;     // в модуле
constexpr size_t kDescMaxHealth = 0x28, kDescArmor = 0x90, kArmorLayerSize = 0x10;  // в описании модуля
constexpr int kArmorSides = 6;

// Ремонт: раз в kRepairPeriod секунд каждой машине в зоне.
constexpr float kRepairPeriod = 0.5f;
constexpr float kRepairFraction = 0.02f;  // модулю - 2% максимума (4%/с: от нуля до полного - 25 с)
constexpr float kArmorFraction = 0.02f;   // слою брони - 2% исходной толщины
// Цена - запчасти самой HEMTT Repair (у цели не списываются). Модуль: 1 запчасть за 1 здоровья (как у
// ремонта экипажем). Броня: kSparesPerMm запчастей за единицу толщины (судя по данным - миллиметр).
constexpr float kSparesPerMm = 1.0f;

// Найденные адреса.
struct Addr {
    uintptr_t ammoVt, fuelVt, sparesVt, repairVt, modulesVt;
    uintptr_t radiusLoad, supplyRadius, showCircle, circleLea, materialTable, incHealth, diplomacy;
    std::vector<uintptr_t> supportBlocksAmmo, repairBlocksAmmo;
} g_a{};

// ---------------------------------------------------------------- модель объектов движка

// Счётчик ссылок движка: [+0x08] сильные ссылки, [+0x10] объект. Компонент хранит владельца
// (машину) как такой блок в [+0x10].
struct RefBlock {
    void** vtbl;
    int32_t strong;
    int32_t weak;
    void* object;
};

// Вызвать виртуальный метод объекта движка по смещению слота в его vtable (offset = 8 * номер слота),
// как это делает сам движок: call [vtbl+offset] с obj первым аргументом. R - тип результата.
template <class R, class... A>
R vcall(void* obj, size_t offset, A... args) {
    auto vtbl = *reinterpret_cast<void***>(obj);
    return reinterpret_cast<R (*)(void*, A...)>(vtbl[offset / 8])(obj, args...);
}

// Отпустить сильную ссылку, полученную от движка (как делает он сам): уменьшить счётчик и, если он
// дошёл до нуля, вызвать освобождение (слот +0x10 блока).
void release(RefBlock* rb) {
    if (rb && --rb->strong == 0) vcall<void>(rb, 0x10);
}

dfd::Log g_log;
int g_events = 0;  // первые события пишем в лог для проверки

using FacetFn = void (*)(void* facet);
using CanRechargeFn = bool (*)(void* facet, void* target);
using UpdateFn = void (*)(void* facet, float dt);
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

// Компонент запчастей (SparesRechargerFacet) той же машины, что и facet; nullptr, если его нет.
void* sparesOf(void* facet) {
    return findFacet(ownerOf(facet), g_a.sparesVt);
}

// Чем на самом деле занимается машина с компонентом боеприпасов (у машин мода он только ради F4):
// Spares - есть компонент запчастей (HEMTT Spares), Repair - есть компонент ремонта (HEMTT Repair),
// Ammo - ни того ни другого (обычный HEMTT Supply и прочие - их не трогаем).
enum class Kind { Ammo, Spares, Repair };
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

// Включить (on) или выключить запчасти у той же машины, что и recharger (боеприпасы или заправщик).
// Вызывается при включении/выключении режима F4. Если запчастей нет или они уже в нужном состоянии -
// ничего не делает. Вызывает исходные функции, минуя перехват hookSparesOff.
void syncSpares(void* recharger, bool on) {
    void* spares = sparesOf(recharger);
    if (!spares) return;
    if (on && !isOn(spares, kSparesOffFlag)) {
        trace("spares ON  (with supply mode)", spares);
        g_origSparesOn(spares);
    } else if (!on && isOn(spares, kSparesOffFlag)) {
        trace("spares OFF (with supply mode)", spares);
        g_origSparesOff(spares);
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

// Подставить в инструкцию lea r8,[rax+disp32] функции круга смещение строки материала в таблице:
// kMaterialSpares (0xE0) перед перерисовкой круга HEMTT Spares, kMaterialAmmo (0x120) - вернуть обратно.
void setCircleMaterial(uint32_t offset) {
    // страница с инструкцией сделана записываемой при установке
    *reinterpret_cast<uint32_t*>(g_a.circleLea + 3) = offset;
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(g_a.circleLea), 7);
}

// std::string MSVC (x64): буфер/указатель, длина, ёмкость; при ёмкости >= 16 строка лежит по указателю.
struct MsvcString {
    const char* ptr;
    char pad[8];
    size_t size, capacity;
};
static_assert(sizeof(MsvcString) == 32);

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

// Если у компонента боеприпасов есть круг, ещё нарисованный материалом патронов, - пересоздать его
// материалом машины мода: Spares - подменой смещения в lea (setCircleMaterial), Repair - подменой
// строки в таблице (showCircleWithPath). Новые круги запоминаются в g_ownCircles, чтобы не
// перерисовывать их каждый кадр. Для обычных машин (Kind::Ammo) ничего не делает.
void convertCircle(void* ammo, Kind kind) {
    void* c = circleOf(ammo);
    if (!c || kind == Kind::Ammo || g_ownCircles.count(c)) return;
    if (kind == Kind::Spares) {
        if (!g_circleWritable) return;
        setCircleMaterial(kMaterialSpares);
        reinterpret_cast<FacetFn>(g_a.showCircle)(ammo);
        setCircleMaterial(kMaterialAmmo);
    } else {
        showCircleWithPath(ammo, kRepairMaterial);
    }
    if (void* n = circleOf(ammo)) g_ownCircles.insert(n);
    trace(kind == Kind::Spares ? "supply circle -> spares material" : "supply circle -> repair material", ammo);
}

// ---------------------------------------------------------------- ремонт

using IncHealthFn = void (*)(void* modules, size_t index, int amount);
using DiplomacyFn = void* (*)(void* world);

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

struct Repaired {
    int health = 0;  // сколько здоровья добавлено модулям
    int layers = 0;  // сколько слоёв брони подросло
    int spent = 0;   // сколько запчастей потрачено
};

// Запас запчастей машины - int в её DamageableModulesFacet ([+0x130]); ссылка, чтобы можно было менять.
int& sparesOfModules(void* modules) {
    return *reinterpret_cast<int*>(static_cast<char*>(modules) + kModulesSpares);
}

// Склад запчастей HEMTT Repair. Дробная цена брони копится в debt и списывается целыми запчастями.
struct Stock {
    int& spares;
    float& debt;
    // Заплатить cost запчастей: цена копится в debt, со склада списывается целая часть (но не больше,
    // чем есть). Потраченное прибавляется к r.spent.
    void pay(float cost, Repaired& r) {
        debt += cost;
        int whole = std::min(spares, static_cast<int>(debt));
        spares -= whole;
        debt -= whole;
        r.spent += whole;
    }
};

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

// ---------------------------------------------------------------- перехваты

void hookSparesUpdate(void* spares, float dt);

// Перехват AmmoRechargerFacet::activate (слот 14) - режим F4 включён. Сначала оригинал (включает
// компонент и рисует круг патронов), потом: у HEMTT Spares включить запчасти, у машин мода -
// перерисовать круг своим материалом.
void hookAmmoOn(void* f) {
    g_origAmmoOn(f);
    syncSpares(f, true);
    Kind k = kindOf(f);
    if (k == Kind::Repair) trace("repair ON  (with supply mode)", f);
    convertCircle(f, k);
}
// Перехват AmmoRechargerFacet::deactivate (слот 13) - режим F4 выключен. Забываем круг и состояние
// ремонта этого компонента, вызываем оригинал (убирает круг), у HEMTT Spares выключаем запчасти.
void hookAmmoOff(void* f) {
    if (void* c = circleOf(f)) g_ownCircles.erase(c);
    g_repairTimers.erase(f);
    g_armorDebt.erase(f);
    g_origAmmoOff(f);
    syncSpares(f, false);
}
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

// Перехваты RefuellerFacet::activate / deactivate (слоты 14 / 13): оригинал + то же для запчастей
// машины (на случай машины с refuel и support - режим F4 тогда берёт заправщик).
void hookFuelOn(void* f) {
    g_origFuelOn(f);
    syncSpares(f, true);
}
void hookFuelOff(void* f) {
    g_origFuelOff(f);
    syncSpares(f, false);
}

// Режим F4: recharger включён -> тикнуть запчасти той же машины тем же dt. Возвращает компонент запчастей.
void* tickSpares(void* recharger, float dt) {
    if (!isOn(recharger, kRechargerOffFlag)) return nullptr;
    void* spares = sparesOf(recharger);
    if (spares && isOn(spares, kSparesOffFlag)) hookSparesUpdate(spares, dt);
    return spares;
}

// Перехват AmmoRechargerFacet::update (слот 10), движок вызывает его каждый кадр. Оригинал раздаёт
// патроны (у машин мода canRecharge отвечает «нет») и обновляет список машин в зоне. Пока режим F4
// включён: у HEMTT Spares тикаем запчасти, у HEMTT Repair - ремонт; круг, созданный без нас (например,
// при загрузке сохранения), тоже перекрашиваем.
void hookAmmoUpdate(void* f, float dt) {
    g_origAmmoUpdate(f, dt);
    if (!isOn(f, kRechargerOffFlag)) return;
    // круг, созданный без нас (например, при загрузке сохранения), тоже перекрашиваем
    if (tickSpares(f, dt)) {
        convertCircle(f, Kind::Spares);
    } else if (findFacet(ownerOf(f), g_a.repairVt)) {
        tickRepair(f, dt);
        convertCircle(f, Kind::Repair);
    }
}
// Перехват RefuellerFacet::update (слот 10): оригинал + тик запчастей той же машины в режиме F4.
void hookFuelUpdate(void* f, float dt) {
    g_origFuelUpdate(f, dt);
    tickSpares(f, dt);
}

// Перехват AmmoRechargerFacet::canRecharge (слот 17): можно ли дать патроны target. Машины мода патроны
// не раздают никогда - ни по зоне, ни по приказу; остальным - решает оригинал.
bool hookAmmoCanRecharge(void* ammo, void* target) {
    if (kindOf(ammo) != Kind::Ammo) return false;
    return g_origAmmoCanRecharge(ammo, target);
}

// Перехват SparesRechargerFacet::update (слот 10). Сам движок его не вызывает - его вызываем мы
// (tickSpares) и приказ «пополнить» по клику. Оригинал ищет машины в радиусе и пополняет им запчасти.
// Диагностика: первые 20 секунд работы зоны - радиус и число целей в ней (список +0x28..+0x30).
void hookSparesUpdate(void* spares, float dt) {
    g_origSparesUpdate(spares, dt);
    static float acc = 0;
    static int lines = 0;
    if (!isOn(spares, kSparesOffFlag) || lines >= 20) return;
    acc += dt;
    if (acc < 1.0f) return;
    acc = 0;
    ++lines;
    auto b = *reinterpret_cast<char**>(static_cast<char*>(spares) + 0x28);
    auto e = *reinterpret_cast<char**>(static_cast<char*>(spares) + 0x30);
    g_log("spares active facet=%p radius=%.1f targets=%d", spares, *reinterpret_cast<float*>(g_a.supplyRadius),
          static_cast<int>((e - b) / 8));
}

// Перехват SparesRechargerFacet::deactivate (слот 13). Приказ «пополнить» по клику по окончании
// выключает запчасти - если в это время у машины включён режим F4 (боеприпасы или заправщик), выключение
// пропускаем, иначе раздача по зоне оборвалась бы.
void hookSparesOff(void* spares) {
    void* owner = ownerOf(spares);
    for (uintptr_t vt : {g_a.ammoVt, g_a.fuelVt}) {
        void* r = findFacet(owner, vt);
        if (r && isOn(r, kRechargerOffFlag)) {
            trace("spares OFF skipped (supply mode active)", spares);
            return;
        }
    }
    g_origSparesOff(spares);
}

// ---------------------------------------------------------------- поиск и установка

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

// Вызывается DllWatcher, когда logic.dll загружена (base - её адрес), до выполнения её кода.
// Находит адреса (resolve) и ставит все правки одним набором PatchSet: всё или ничего. Не нашлось или не
// совпало хоть что-то - не меняет ничего, пишет причину в лог. Успех - строка «SparesSupply active».
void onLogicLoaded(uintptr_t base) {
    Image img(base);
    g_log("logic.dll at %p (TimeDateStamp %08X, SizeOfImage %08X)", reinterpret_cast<void*>(base),
          img.timeDateStamp(), img.sizeOfImage());
    if (!resolve(img)) {
        g_log("this logic.dll build is not supported - nothing patched");
        return;
    }

    const std::vector<uint8_t> movR15b1{0x41, 0xB7, 0x01}, nop3{0x90, 0x90, 0x90};
    dfd::PatchSet p(img);
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
        return;
    }
    g_origSparesOn = reinterpret_cast<FacetFn>(sparesOn);
    const std::vector<uint8_t> leaAmmo{0x4C, 0x8D, 0x80, 0x20, 0x01, 0x00, 0x00};
    p.bytes("supply circle material lea (verify only)", g_a.circleLea, leaAmmo, leaAmmo);
    if (!p.apply(g_log)) return;

    DWORD old;
    g_circleWritable = VirtualProtect(reinterpret_cast<void*>(g_a.circleLea), 7, PAGE_EXECUTE_READWRITE, &old);
    if (!g_circleWritable)
        g_log("cannot unprotect circle material lea (error %lu) - spares circle keeps ammo icons", GetLastError());
    g_log("SparesSupply active");
}

dfd::DllWatcher g_watcher;

}  // namespace

// Точка входа DLL. Её загружает winhttp.dll (загрузчик) из своего DllMain, т.е. под loader lock Windows:
// здесь только открываем лог и подписываемся на загрузку logic.dll - вся работа в onLogicLoaded.
BOOL WINAPI DllMain(HINSTANCE self, DWORD reason, LPVOID) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(self);
    g_log.open(self, L"SparesSupply");
    g_log("SparesSupply " SPARES_SUPPLY_VERSION " loaded, waiting for logic.dll");
    if (!g_watcher.watch(L"logic.dll", &onLogicLoaded)) g_log("cannot register DLL notification");
    return TRUE;
}
