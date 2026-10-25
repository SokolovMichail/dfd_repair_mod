// Тесты общего кода плагинов - common/patch.h.
//
// На этих инструментах держится вся установка плагина:
//   Image      - «прочитать» загруженную DLL игры: найти раздел, байтовый шаблон, строку, класс по имени;
//   PatchSet   - внести правки в память игры по принципу «всё или ничего»: если хоть одна правка не
//                совпала с ожиданием (игра обновилась), не меняется ничего и игра работает как без мода;
//   Log        - файл лога рядом с плагином;
//   DllWatcher - узнать, что игра загрузила нужную DLL, раньше, чем та начнёт работать.
// Если после рефакторинга что-то из этого начнёт вести себя иначе, плагин может поставить правки
// не туда или не заметить, что игра изменилась. Поэтому поведение закреплено здесь подробно.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "third_party/doctest.h"

#include "../common/patch.h"
#include "support/fake_pe.h"
#include "support/log_file.h"

using dfd::Image;
using fake::hex;

namespace {

// Лог тестовой программы: patch_h_test.log рядом с ней. Его читает LogTail.
dfd::Log& testLog() {
    static dfd::Log log;
    static bool opened = false;
    if (!opened) {
        log.open(GetModuleHandleW(nullptr), L"patch_h_test");
        opened = true;
    }
    return log;
}
// Путь к нему. Сначала открываем лог: открытие стирает файл, и LogTail должен отсчитывать уже от нового.
std::wstring testLogPath() {
    testLog();
    return fake::logPath(L"patch_h_test");
}


}  // namespace

// ================================================================ Image: шаблоны

TEST_CASE("Шаблон находит все совпадения, ?? и ? - любой байт") {
    // Подготовка: в «коде» два похожих места: 48 8B 05 11 и 48 8B 06 22.
    fake::Pe pe;
    uintptr_t a = pe.put(pe.text, "48 8B 05 11");
    uintptr_t b = pe.put(pe.text, "48 8B 06 22");
    Image img(pe.base());

    // Ожидаем: "48 8B ??" подходит к обоим (??  - любой байт); одиночный ? означает то же самое.
    CHECK(img.find("48 8B ??") == std::vector<uintptr_t>{a, b});
    CHECK(img.find("48 8B ? 11") == std::vector<uintptr_t>{a});
    // Лишние пробелы и строчные буквы не мешают.
    CHECK(img.find("48  8b 06") == std::vector<uintptr_t>{b});
    // Нет такого - пустой список.
    CHECK(img.find("48 8B 07").empty());
}

TEST_CASE("Байт 00 в шаблоне - обычный байт, а не «любой»") {
    // В шаблонах много нулей (например, "C7 45 D4 01 00 00 00"); они должны совпадать только с нулём.
    fake::Pe pe;
    pe.put(pe.text, "48 01 22");
    uintptr_t zero = pe.put(pe.text, "48 00 22");
    Image img(pe.base());
    CHECK(img.find("48 00 22") == std::vector<uintptr_t>{zero});
}

TEST_CASE("Пустой шаблон и шаблон длиннее раздела ничего не находят") {
    fake::Pe pe;
    Image img(pe.base());
    CHECK(img.find("").empty());
    // Шаблон длиннее раздела .text (0x10000 байт) - не ищется вовсе.
    std::string huge;
    for (int i = 0; i < 0x10001; ++i) huge += "?? ";
    CHECK(img.find(huge.c_str()).empty());
}

TEST_CASE("Поиск идёт только внутри своего раздела") {
    // Подготовка: одинаковые байты в разделе констант и на самой границе раздела кода.
    fake::Pe pe;
    pe.put(pe.rdata, "00 00 00 00");  // первый байт .rdata займёт BB с границы .text (ниже)
    uintptr_t inRdata = pe.put(pe.rdata, "DE AD BE EF");
    // Последний байт .text = AA, а следующий за ним (уже вне раздела) = BB: шаблон «AA BB» через границу.
    uintptr_t textEnd = pe.base() + fake::Pe::kText + fake::Pe::kTextSize;
    fake::Pe::write(textEnd - 1, {0xAA});
    fake::Pe::write(textEnd, {0xBB});
    Image img(pe.base());

    // Ожидаем: в коде этих байтов нет; в .rdata - есть; через границу раздела не находится.
    CHECK(img.find("DE AD BE EF").empty());
    CHECK(img.find("DE AD BE EF", ".rdata") == std::vector<uintptr_t>{inRdata});
    CHECK(img.find("AA BB").empty());
}

TEST_CASE("matches проверяет байты по конкретному адресу") {
    fake::Pe pe;
    uintptr_t a = pe.put(pe.text, "40 53 48 83 EC 20");
    CHECK(Image::matches(a, "40 53 ?? 83"));
    CHECK_FALSE(Image::matches(a, "40 54"));
    // Пустой шаблон совпадает с чем угодно.
    CHECK(Image::matches(a, ""));
}

// ================================================================ Image: строки, адреса, заголовки

TEST_CASE("findString ищет строку целиком, вместе с нулём в конце") {
    // Подготовка: сначала "abcd", потом "abc". Строка "abc" - префикс первой, но без завершающего нуля
    // там она не совпадает - находится вторая.
    fake::Pe pe;
    pe.putString(pe.rdata, "abcd");
    uintptr_t abc = pe.putString(pe.rdata, "abc");
    uintptr_t inData = pe.putString(pe.data, "only-in-data");
    Image img(pe.base());

    CHECK(img.findString("abc") == abc);
    CHECK(img.findString("missing") == 0);
    // По умолчанию ищется в .rdata; другой раздел - явно.
    CHECK(img.findString("only-in-data") == 0);
    CHECK(img.findString("only-in-data", ".data") == inData);
}

TEST_CASE("ripTarget считает адрес от конца инструкции, смещение со знаком") {
    // Инструкция длиной 7 байт, смещение - 4 байта с 3-го. Цель = конец инструкции + смещение.
    alignas(8) uint8_t insn[16] = {};
    auto at = reinterpret_cast<uintptr_t>(insn);
    int32_t d = 0x100;
    memcpy(insn + 3, &d, 4);
    CHECK(Image::ripTarget(at, 3, 7) == at + 7 + 0x100);
    d = -0x40;  // смещение назад
    memcpy(insn + 3, &d, 4);
    CHECK(Image::ripTarget(at, 3, 7) == at + 7 - 0x40);
}

TEST_CASE("Разделы, RVA и поля заголовка") {
    fake::Pe pe;
    Image img(pe.base());
    CHECK(img.base() == pe.base());
    CHECK(img.section(".text").begin == pe.base() + fake::Pe::kText);
    CHECK(img.section(".text").end == pe.base() + fake::Pe::kText + fake::Pe::kTextSize);
    CHECK(img.section(".data").begin == pe.base() + fake::Pe::kData);
    // Нет такого раздела - пустой диапазон.
    CHECK(img.section(".bss").begin == 0);
    CHECK(img.section(".bss").end == 0);
    // RVA - смещение от начала модуля (так адреса пишутся в лог).
    CHECK(img.rva(pe.base() + 0x1234) == 0x1234u);
    CHECK(img.timeDateStamp() == fake::Pe::kTimeDateStamp);
    CHECK(img.sizeOfImage() == fake::Pe::kImageSize);
}

// ================================================================ Image: vtable по имени класса

TEST_CASE("vtableOf находит таблицу методов класса по его имени") {
    fake::Pe pe;
    uintptr_t ammo = pe.addClass("AmmoRechargerFacet@Leonardo", 20);
    uintptr_t spares = pe.addClass("SparesRechargerFacet@Leonardo", 20);
    Image img(pe.base());
    CHECK(img.vtableOf("AmmoRechargerFacet@Leonardo") == ammo);
    CHECK(img.vtableOf("SparesRechargerFacet@Leonardo") == spares);
    // Неизвестный класс - 0. Имя сравнивается целиком: «Ammo» - не то же, что «AmmoRechargerFacet».
    CHECK(img.vtableOf("Unknown@Leonardo") == 0);
    CHECK(img.vtableOf("Ammo@Leonardo") == 0);
}

TEST_CASE("vtableOf берёт только основную vtable и только правильный COL") {
    // У класса с несколькими базовыми классами несколько vtable; нужная - с offset 0. Описание (COL)
    // с неверной ссылкой на себя - не настоящее, его надо пропустить.
    fake::Pe pe;
    pe.addClass("OnlySecondary@Leonardo", 4, /*colOffset=*/8);
    pe.addClass("BadSelf@Leonardo", 4, 0, /*badSelf=*/true);
    Image img(pe.base());
    CHECK(img.vtableOf("OnlySecondary@Leonardo") == 0);
    CHECK(img.vtableOf("BadSelf@Leonardo") == 0);
}

// ================================================================ запись в защищённую память

TEST_CASE("writeMem пишет в страницу только для чтения и возвращает ей права") {
    // Код и vtable игры лежат в памяти «только чтение» - правка должна временно снять защиту.
    void* page = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    REQUIRE(page);
    DWORD old;
    VirtualProtect(page, 4096, PAGE_READONLY, &old);

    const uint8_t data[3] = {1, 2, 3};
    CHECK(dfd::writeMem(page, data, 3));
    CHECK(memcmp(page, data, 3) == 0);
    MEMORY_BASIC_INFORMATION mbi{};
    VirtualQuery(page, &mbi, sizeof(mbi));
    CHECK(mbi.Protect == PAGE_READONLY);  // права вернулись прежние
    VirtualFree(page, 0, MEM_RELEASE);
}

// ================================================================ PatchSet: всё или ничего

namespace {

using Fn = void (*)();

// Образ с двумя местами для замены байтов и классом, у которого в слоте 2 «функция» с известным началом.
struct PatchScene {
    fake::Pe pe;
    uintptr_t siteA = pe.put(pe.text, "41 B7 01 11");
    uintptr_t siteB = pe.put(pe.text, "41 B7 01 22");
    uintptr_t fn = pe.put(pe.text, "40 53 48 83 EC 20");  // «функция» - нужно только её начало
    uintptr_t vt = pe.addClass("Facet@Leonardo", 4);
    Image img{pe.base()};
    PatchScene() { fake::Pe::setSlot(vt, 2, fn); }
};

void hookFn() {}
const Fn kSentinel = reinterpret_cast<Fn>(static_cast<uintptr_t>(0xDEAD));

}  // namespace

TEST_CASE("PatchSet: всё проверилось - всё записано, исходные адреса сохранены, каждая правка в логе") {
    // Подготовка: две замены «mov r15b,1 -> nop» и одна подмена метода в vtable.
    PatchScene s;
    fake::LogTail log(testLogPath());
    dfd::PatchSet p(s.img);
    p.bytes("site A", s.siteA, {0x41, 0xB7, 0x01}, {0x90, 0x90, 0x90});
    p.bytes("site B", s.siteB, {0x41, 0xB7, 0x01}, {0x90, 0x90, 0x90});
    Fn saved = kSentinel;
    p.vslot("Facet::slot2", s.vt, 2, "40 53 48", &hookFn, &saved);

    // Действие и ожидание: применилось, байты заменены, в слоте - наш метод, а исходный запомнен,
    // чтобы перехват мог вызывать оригинал.
    CHECK(p.apply(testLog()));
    CHECK(Image::matches(s.siteA, "90 90 90 11"));
    CHECK(Image::matches(s.siteB, "90 90 90 22"));
    CHECK(*reinterpret_cast<uintptr_t*>(s.vt + 16) == reinterpret_cast<uintptr_t>(&hookFn));
    CHECK(reinterpret_cast<uintptr_t>(saved) == s.fn);
    // В логе - по строке на правку, с RVA (смещением от начала модуля) в 8 hex-цифрах.
    char line[96];
    snprintf(line, sizeof line, "patched: site A (RVA %08X)", s.pe.rva(s.siteA));
    CHECK(log.has(line));
    snprintf(line, sizeof line, "patched: Facet::slot2 (RVA %08X)", s.pe.rva(s.vt + 16));
    CHECK(log.has(line));
    CHECK(log.count("patched: ") == 3);
}

TEST_CASE("PatchSet: одна правка не совпала - не записано ничего") {
    // Подготовка: первая правка верная, вторая ждёт других исходных байтов (как после обновления игры).
    PatchScene s;
    fake::LogTail log(testLogPath());
    auto before = s.pe.snapshot();
    dfd::PatchSet p(s.img);
    Fn saved = kSentinel;
    p.vslot("Facet::slot2", s.vt, 2, "40 53 48", &hookFn, &saved);
    p.bytes("site A", s.siteA, {0x41, 0xB7, 0x01}, {0x90, 0x90, 0x90});
    p.bytes("site B", s.siteB, {0x41, 0xB7, 0x02}, {0x90, 0x90, 0x90});

    // Ожидаем: отказ; образ не изменился ни на байт - даже правки, стоявшие раньше неверной;
    // исходный адрес не записан; в логе - какая правка и где.
    CHECK_FALSE(p.apply(testLog()));
    CHECK(s.pe.snapshot() == before);
    CHECK(saved == kSentinel);
    char line[96];
    snprintf(line, sizeof line, "verify FAILED: site B at RVA %08X - nothing patched", s.pe.rva(s.siteB));
    CHECK(log.has(line));
    CHECK(log.count("patched: ") == 0);
}

TEST_CASE("PatchSet: место не найдено (адрес 0) - не записано ничего") {
    PatchScene s;
    fake::LogTail log(testLogPath());
    auto before = s.pe.snapshot();
    dfd::PatchSet p(s.img);
    p.bytes("site A", s.siteA, {0x41, 0xB7, 0x01}, {0x90, 0x90, 0x90});
    p.bytes("lost site", 0, {0x41}, {0x90});
    CHECK_FALSE(p.apply(testLog()));
    CHECK(s.pe.snapshot() == before);
    CHECK(log.has("not found: lost site - nothing patched"));
}

TEST_CASE("PatchSet: vtable не найдена - подмена метода считается ненайденной") {
    PatchScene s;
    fake::LogTail log(testLogPath());
    dfd::PatchSet p(s.img);
    Fn saved = kSentinel;
    p.vslot("Missing::slot2", 0, 2, "40 53", &hookFn, &saved);
    CHECK_FALSE(p.apply(testLog()));
    CHECK(log.has("not found: Missing::slot2 - nothing patched"));
    CHECK(saved == kSentinel);
}

TEST_CASE("PatchSet: метод в слоте начинается не так, как ожидалось - не записано ничего") {
    // Начало функции - это проверка, что в слоте та же функция, что и в проверенной версии игры
    // (а значит, и поля объектов там же).
    PatchScene s;
    fake::LogTail log(testLogPath());
    auto before = s.pe.snapshot();
    dfd::PatchSet p(s.img);
    Fn saved = kSentinel;
    p.vslot("Facet::slot2", s.vt, 2, "48 89 5C", &hookFn, &saved);
    CHECK_FALSE(p.apply(testLog()));
    CHECK(s.pe.snapshot() == before);
    CHECK(log.has("verify FAILED: Facet::slot2"));
}

TEST_CASE("PatchSet: пустой слот vtable не проходит проверку начала функции") {
    PatchScene s;
    fake::LogTail log(testLogPath());
    dfd::PatchSet p(s.img);
    Fn saved = kSentinel;
    p.vslot("Facet::slot3", s.vt, 3, "", &hookFn, &saved);  // слот 3 пуст (0)
    CHECK_FALSE(p.apply(testLog()));
    CHECK(log.has("verify FAILED: Facet::slot3"));
}

TEST_CASE("PatchSet: подмена без проверки начала (prologue = nullptr) сравнивает только сам указатель") {
    PatchScene s;
    dfd::PatchSet p(s.img);
    Fn saved = kSentinel;
    p.vslot("Facet::slot3", s.vt, 3, nullptr, &hookFn, &saved);  // слот 3 пуст: исходное значение - 0
    CHECK(p.apply(testLog()));
    CHECK(saved == nullptr);
    CHECK(*reinterpret_cast<uintptr_t*>(s.vt + 24) == reinterpret_cast<uintptr_t>(&hookFn));
}

// ================================================================ Log

TEST_CASE("Log: строка с временем ЧЧ:ММ:СС, текст в стиле printf, перевод строки") {
    dfd::Log log;
    log.open(GetModuleHandleW(nullptr), L"patch_h_test_format");
    log("value=%d name=%s", 5, "x");
    std::string text = fake::readFile(fake::logPath(L"patch_h_test_format"));
    // Ожидаем ровно "ЧЧ:ММ:СС value=5 name=x" и перевод строки. Файл открыт в текстовом режиме, поэтому
    // перевод строки в нём - как принято в Windows, два байта \r\n.
    REQUIRE(text.size() == 8 + 15 + 2);
    for (int i : {0, 1, 3, 4, 6, 7}) CHECK(isdigit(static_cast<unsigned char>(text[i])));
    CHECK(text[2] == ':');
    CHECK(text[5] == ':');
    CHECK(text.substr(8) == " value=5 name=x\r\n");
}

TEST_CASE("Log: файл не открыт - запись молча ничего не делает") {
    dfd::Log log;
    log("nothing %d", 1);  // не должно упасть
    CHECK(true);
}

// ================================================================ DllWatcher

namespace {

int g_fired = 0;
uintptr_t g_firedBase = 0;
void onLoaded(uintptr_t base) {
    ++g_fired;
    g_firedBase = base;
}

// Наблюдатель регистрируется в Windows навсегда (отписки нет), поэтому объект должен жить до конца
// программы - как и в плагине, где он глобальный.
dfd::DllWatcher g_kernelWatcher, g_dllWatcher;

}  // namespace

TEST_CASE("DllWatcher: DLL уже загружена - колбэк сразу, один раз") {
    g_fired = 0;
    CHECK(g_kernelWatcher.watch(L"kernel32.dll", &onLoaded));
    CHECK(g_fired == 1);
    CHECK(g_firedBase == reinterpret_cast<uintptr_t>(GetModuleHandleW(L"kernel32.dll")));
}

TEST_CASE("DllWatcher: DLL загружается позже - колбэк при загрузке, только первый раз") {
    // Подготовка: подписываемся на тестовую DLL, которой ещё нет в процессе.
    std::wstring name(WATCHER_DLL_NAME, WATCHER_DLL_NAME + strlen(WATCHER_DLL_NAME));
    REQUIRE(GetModuleHandleW(name.c_str()) == nullptr);
    g_fired = 0;
    // Имя сравнивается без учёта регистра.
    std::wstring upper = name;
    for (auto& c : upper) c = towupper(c);
    REQUIRE(g_dllWatcher.watch(upper.c_str(), &onLoaded));
    CHECK(g_fired == 0);  // пока не загружена - ничего

    // Действие: загружаем DLL (по полному пути: рядом с тестом).
    std::wstring path = fake::logPath(L"x");  // папка программы + "x.log"
    path = path.substr(0, path.size() - 5) + name;
    HMODULE h = LoadLibraryW(path.c_str());
    REQUIRE(h);
    // Ожидаем: колбэк один раз, с адресом загруженной DLL.
    CHECK(g_fired == 1);
    CHECK(g_firedBase == reinterpret_cast<uintptr_t>(h));

    // Выгрузили и загрузили снова - колбэк больше не вызывается.
    FreeLibrary(h);
    h = LoadLibraryW(path.c_str());
    REQUIRE(h);
    CHECK(g_fired == 1);
    FreeLibrary(h);
}
