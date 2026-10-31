// Tests of the shared plugin code - common/patch.h.
//
// The whole plugin installation stands on these tools:
//   Image      - "read" a loaded game DLL: find a section, a byte pattern, a string, a class by name;
//   PatchSet   - apply patches to game memory on an "all or nothing" basis: if even one patch does not match
//                what is expected (the game was updated), nothing changes and the game runs as without the mod;
//   Log        - the log file next to the plugin;
//   DllWatcher - learn that the game loaded the DLL we need, before that DLL starts running.
// If any of this behaves differently after the refactoring, the plugin may patch the wrong place or fail to
// notice that the game changed. That is why the behaviour is pinned here in detail.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "third_party/doctest.h"

#include "../common/patch.h"
#include "support/fake_pe.h"
#include "support/log_file.h"

using dfd::Image;
using fake::hex;

namespace {

// The test program's log: patch_h_test.log next to it. LogTail reads it.
dfd::Log& testLog() {
    static dfd::Log log;
    static bool opened = false;
    if (!opened) {
        log.open(GetModuleHandleW(nullptr), L"patch_h_test");
        opened = true;
    }
    return log;
}
// Its path. The log is opened first: opening truncates the file, and LogTail must count from the new one.
std::wstring testLogPath() {
    testLog();
    return fake::logPath(L"patch_h_test");
}

}  // namespace

// ================================================================ Image: patterns

TEST_CASE("A pattern finds all matches, ?? and ? mean any byte") {
    // Setup: two similar places in the "code": 48 8B 05 11 and 48 8B 06 22.
    fake::Pe pe;
    uintptr_t a = pe.put(pe.text, "48 8B 05 11");
    uintptr_t b = pe.put(pe.text, "48 8B 06 22");
    Image img(pe.base());

    // Expected: "48 8B ??" matches both (?? - any byte); a single ? means the same.
    CHECK(img.find("48 8B ??") == std::vector<uintptr_t>{a, b});
    CHECK(img.find("48 8B ? 11") == std::vector<uintptr_t>{a});
    // Extra spaces and lowercase letters do not matter.
    CHECK(img.find("48  8b 06") == std::vector<uintptr_t>{b});
    // Nothing like it - an empty list.
    CHECK(img.find("48 8B 07").empty());
}

TEST_CASE("Byte 00 in a pattern is an ordinary byte, not \"any\"") {
    // Patterns contain many zeros (e.g. "C7 45 D4 01 00 00 00"); they must match only a zero.
    fake::Pe pe;
    pe.put(pe.text, "48 01 22");
    uintptr_t zero = pe.put(pe.text, "48 00 22");
    Image img(pe.base());
    CHECK(img.find("48 00 22") == std::vector<uintptr_t>{zero});
}

TEST_CASE("An empty pattern and a pattern longer than the section find nothing") {
    fake::Pe pe;
    Image img(pe.base());
    CHECK(img.find("").empty());
    // A pattern longer than the .text section (0x10000 bytes) is not searched for at all.
    std::string huge;
    for (int i = 0; i < 0x10001; ++i) huge += "?? ";
    CHECK(img.find(huge.c_str()).empty());
}

TEST_CASE("The search stays inside its section") {
    // Setup: the same bytes in the constants section and right on the boundary of the code section.
    fake::Pe pe;
    pe.put(pe.rdata, "00 00 00 00");  // the first byte of .rdata is taken by BB from the .text boundary (below)
    uintptr_t inRdata = pe.put(pe.rdata, "DE AD BE EF");
    // The last byte of .text = AA, and the next one (already outside the section) = BB: pattern "AA BB" across the boundary.
    uintptr_t textEnd = pe.base() + fake::Pe::kText + fake::Pe::kTextSize;
    fake::Pe::write(textEnd - 1, {0xAA});
    fake::Pe::write(textEnd, {0xBB});
    Image img(pe.base());

    // Expected: these bytes are not in the code; they are in .rdata; nothing is found across the boundary.
    CHECK(img.find("DE AD BE EF").empty());
    CHECK(img.find("DE AD BE EF", ".rdata") == std::vector<uintptr_t>{inRdata});
    CHECK(img.find("AA BB").empty());
}

TEST_CASE("matches checks the bytes at a specific address") {
    fake::Pe pe;
    uintptr_t a = pe.put(pe.text, "40 53 48 83 EC 20");
    CHECK(Image::matches(a, "40 53 ?? 83"));
    CHECK_FALSE(Image::matches(a, "40 54"));
    // An empty pattern matches anything.
    CHECK(Image::matches(a, ""));
}

// ================================================================ Image: strings, addresses, headers

TEST_CASE("findString looks for the whole string, including its terminating zero") {
    // Setup: first "abcd", then "abc". "abc" is a prefix of the first, but without the terminating zero it does
    // not match there - the second one is found.
    fake::Pe pe;
    pe.putString(pe.rdata, "abcd");
    uintptr_t abc = pe.putString(pe.rdata, "abc");
    uintptr_t inData = pe.putString(pe.data, "only-in-data");
    Image img(pe.base());

    CHECK(img.findString("abc") == abc);
    CHECK(img.findString("missing") == 0);
    // By default it searches .rdata; another section has to be named.
    CHECK(img.findString("only-in-data") == 0);
    CHECK(img.findString("only-in-data", ".data") == inData);
}

TEST_CASE("ripTarget counts from the end of the instruction, the offset is signed") {
    // A 7-byte instruction, the offset is 4 bytes starting at byte 3. Target = end of instruction + offset.
    alignas(8) uint8_t insn[16] = {};
    auto at = reinterpret_cast<uintptr_t>(insn);
    int32_t d = 0x100;
    memcpy(insn + 3, &d, 4);
    CHECK(Image::ripTarget(at, 3, 7) == at + 7 + 0x100);
    d = -0x40;  // a backward offset
    memcpy(insn + 3, &d, 4);
    CHECK(Image::ripTarget(at, 3, 7) == at + 7 - 0x40);
}

TEST_CASE("Sections, RVA and header fields") {
    fake::Pe pe;
    Image img(pe.base());
    CHECK(img.base() == pe.base());
    CHECK(img.section(".text").begin == pe.base() + fake::Pe::kText);
    CHECK(img.section(".text").end == pe.base() + fake::Pe::kText + fake::Pe::kTextSize);
    CHECK(img.section(".data").begin == pe.base() + fake::Pe::kData);
    // No such section - an empty range.
    CHECK(img.section(".bss").begin == 0);
    CHECK(img.section(".bss").end == 0);
    // RVA - the offset from the start of the module (that is how addresses are written to the log).
    CHECK(img.rva(pe.base() + 0x1234) == 0x1234u);
    CHECK(img.timeDateStamp() == fake::Pe::kTimeDateStamp);
    CHECK(img.sizeOfImage() == fake::Pe::kImageSize);
}

// ================================================================ Image: vtable by class name

TEST_CASE("vtableOf finds a class's method table by its name") {
    fake::Pe pe;
    uintptr_t ammo = pe.addClass("AmmoRechargerFacet@Leonardo", 20);
    uintptr_t spares = pe.addClass("SparesRechargerFacet@Leonardo", 20);
    Image img(pe.base());
    CHECK(img.vtableOf("AmmoRechargerFacet@Leonardo") == ammo);
    CHECK(img.vtableOf("SparesRechargerFacet@Leonardo") == spares);
    // An unknown class - 0. The name is compared whole: "Ammo" is not the same as "AmmoRechargerFacet".
    CHECK(img.vtableOf("Unknown@Leonardo") == 0);
    CHECK(img.vtableOf("Ammo@Leonardo") == 0);
}

TEST_CASE("vtableOf takes only the primary vtable and only a valid COL") {
    // A class with several base classes has several vtables; the one we need has offset 0. A descriptor (COL)
    // with a wrong self-reference is not a real one and must be skipped.
    fake::Pe pe;
    pe.addClass("OnlySecondary@Leonardo", 4, /*colOffset=*/8);
    pe.addClass("BadSelf@Leonardo", 4, 0, /*badSelf=*/true);
    Image img(pe.base());
    CHECK(img.vtableOf("OnlySecondary@Leonardo") == 0);
    CHECK(img.vtableOf("BadSelf@Leonardo") == 0);
}

// ================================================================ writing to protected memory

TEST_CASE("writeMem writes into a read-only page and restores its protection") {
    // The game's code and vtables live in read-only memory - a patch has to lift the protection for a moment.
    void* page = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    REQUIRE(page);
    DWORD old;
    VirtualProtect(page, 4096, PAGE_READONLY, &old);

    const uint8_t data[3] = {1, 2, 3};
    CHECK(dfd::writeMem(page, data, 3));
    CHECK(memcmp(page, data, 3) == 0);
    MEMORY_BASIC_INFORMATION mbi{};
    VirtualQuery(page, &mbi, sizeof(mbi));
    CHECK(mbi.Protect == PAGE_READONLY);  // the old protection is back
    VirtualFree(page, 0, MEM_RELEASE);
}

// ================================================================ PatchSet: all or nothing

namespace {

using Fn = void (*)();

// An image with two places for byte replacement and a class whose slot 2 holds a "function" with a known start.
struct PatchScene {
    fake::Pe pe;
    uintptr_t siteA = pe.put(pe.text, "41 B7 01 11");
    uintptr_t siteB = pe.put(pe.text, "41 B7 01 22");
    uintptr_t fn = pe.put(pe.text, "40 53 48 83 EC 20");  // a "function" - only its start matters
    uintptr_t vt = pe.addClass("Facet@Leonardo", 4);
    Image img{pe.base()};
    PatchScene() { fake::Pe::setSlot(vt, 2, fn); }
};

void hookFn() {}
const Fn kSentinel = reinterpret_cast<Fn>(static_cast<uintptr_t>(0xDEAD));

}  // namespace

TEST_CASE("PatchSet: everything verified - everything written, originals saved, each patch logged") {
    // Setup: two "mov r15b,1 -> nop" replacements and one method replacement in a vtable.
    PatchScene s;
    fake::LogTail log(testLogPath());
    dfd::PatchSet p(s.img);
    p.bytes("site A", s.siteA, {0x41, 0xB7, 0x01}, {0x90, 0x90, 0x90});
    p.bytes("site B", s.siteB, {0x41, 0xB7, 0x01}, {0x90, 0x90, 0x90});
    Fn saved = kSentinel;
    p.vslot("Facet::slot2", s.vt, 2, "40 53 48", &hookFn, &saved);

    // Action and expected result: applied, bytes replaced, our method in the slot, and the original saved so
    // the hook can call it.
    CHECK(p.apply(testLog()));
    CHECK(Image::matches(s.siteA, "90 90 90 11"));
    CHECK(Image::matches(s.siteB, "90 90 90 22"));
    CHECK(*reinterpret_cast<uintptr_t*>(s.vt + 16) == reinterpret_cast<uintptr_t>(&hookFn));
    CHECK(reinterpret_cast<uintptr_t>(saved) == s.fn);
    // The log has one line per patch, with the RVA (offset from the module start) as 8 hex digits.
    char line[96];
    snprintf(line, sizeof line, "patched: site A (RVA %08X)", s.pe.rva(s.siteA));
    CHECK(log.has(line));
    snprintf(line, sizeof line, "patched: Facet::slot2 (RVA %08X)", s.pe.rva(s.vt + 16));
    CHECK(log.has(line));
    CHECK(log.count("patched: ") == 3);
}

TEST_CASE("PatchSet: one patch did not match - nothing is written") {
    // Setup: the first patch is correct, the second expects different original bytes (as after a game update).
    PatchScene s;
    fake::LogTail log(testLogPath());
    auto before = s.pe.snapshot();
    dfd::PatchSet p(s.img);
    Fn saved = kSentinel;
    p.vslot("Facet::slot2", s.vt, 2, "40 53 48", &hookFn, &saved);
    p.bytes("site A", s.siteA, {0x41, 0xB7, 0x01}, {0x90, 0x90, 0x90});
    p.bytes("site B", s.siteB, {0x41, 0xB7, 0x02}, {0x90, 0x90, 0x90});

    // Expected: refusal; the image did not change by a single byte - not even the patches listed before the bad
    // one; the original was not saved; the log says which patch and where.
    CHECK_FALSE(p.apply(testLog()));
    CHECK(s.pe.snapshot() == before);
    CHECK(saved == kSentinel);
    char line[96];
    snprintf(line, sizeof line, "verify FAILED: site B at RVA %08X - nothing patched", s.pe.rva(s.siteB));
    CHECK(log.has(line));
    CHECK(log.count("patched: ") == 0);
}

TEST_CASE("PatchSet: a location was not found (address 0) - nothing is written") {
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

TEST_CASE("PatchSet: vtable not found - the method replacement counts as not found") {
    PatchScene s;
    fake::LogTail log(testLogPath());
    dfd::PatchSet p(s.img);
    Fn saved = kSentinel;
    p.vslot("Missing::slot2", 0, 2, "40 53", &hookFn, &saved);
    CHECK_FALSE(p.apply(testLog()));
    CHECK(log.has("not found: Missing::slot2 - nothing patched"));
    CHECK(saved == kSentinel);
}

TEST_CASE("PatchSet: the method in the slot does not start as expected - nothing is written") {
    // The function's start is the check that the slot holds the same function as in the tested game version
    // (and so the object fields are in the same places too).
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

TEST_CASE("PatchSet: an empty vtable slot fails the function start check") {
    PatchScene s;
    fake::LogTail log(testLogPath());
    dfd::PatchSet p(s.img);
    Fn saved = kSentinel;
    p.vslot("Facet::slot3", s.vt, 3, "", &hookFn, &saved);  // slot 3 is empty (0)
    CHECK_FALSE(p.apply(testLog()));
    CHECK(log.has("verify FAILED: Facet::slot3"));
}

TEST_CASE("PatchSet: a replacement without a start check (prologue = nullptr) compares only the pointer itself") {
    PatchScene s;
    dfd::PatchSet p(s.img);
    Fn saved = kSentinel;
    p.vslot("Facet::slot3", s.vt, 3, nullptr, &hookFn, &saved);  // slot 3 is empty: the original value is 0
    CHECK(p.apply(testLog()));
    CHECK(saved == nullptr);
    CHECK(*reinterpret_cast<uintptr_t*>(s.vt + 24) == reinterpret_cast<uintptr_t>(&hookFn));
}

// ================================================================ Log

TEST_CASE("Log: a line with time HH:MM:SS, printf-style text, a line break") {
    dfd::Log log;
    log.open(GetModuleHandleW(nullptr), L"patch_h_test_format");
    log("value=%d name=%s", 5, "x");
    std::string text = fake::readFile(fake::logPath(L"patch_h_test_format"));
    // Expected: exactly "HH:MM:SS value=5 name=x" and a line break. The file is opened in text mode, so the line
    // break in it is the Windows one, two bytes \r\n.
    REQUIRE(text.size() == 8 + 15 + 2);
    for (int i : {0, 1, 3, 4, 6, 7}) CHECK(isdigit(static_cast<unsigned char>(text[i])));
    CHECK(text[2] == ':');
    CHECK(text[5] == ':');
    CHECK(text.substr(8) == " value=5 name=x\r\n");
}

TEST_CASE("Log: file not open - writing silently does nothing") {
    dfd::Log log;
    log("nothing %d", 1);  // must not crash
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

// A watcher is registered with Windows forever (there is no unsubscribe), so the object must live until the
// program ends - just like in the plugin, where it is global.
dfd::DllWatcher g_kernelWatcher, g_dllWatcher;

}  // namespace

TEST_CASE("DllWatcher: the DLL is already loaded - the callback fires right away, once") {
    g_fired = 0;
    CHECK(g_kernelWatcher.watch(L"kernel32.dll", &onLoaded));
    CHECK(g_fired == 1);
    CHECK(g_firedBase == reinterpret_cast<uintptr_t>(GetModuleHandleW(L"kernel32.dll")));
}

TEST_CASE("DllWatcher: the DLL is loaded later - the callback fires on load, only the first time") {
    // Setup: subscribe to the test DLL, which is not in the process yet.
    std::wstring name(WATCHER_DLL_NAME, WATCHER_DLL_NAME + strlen(WATCHER_DLL_NAME));
    REQUIRE(GetModuleHandleW(name.c_str()) == nullptr);
    g_fired = 0;
    // The name is compared case-insensitively.
    std::wstring upper = name;
    for (auto& c : upper) c = towupper(c);
    REQUIRE(g_dllWatcher.watch(upper.c_str(), &onLoaded));
    CHECK(g_fired == 0);  // not loaded yet - nothing

    // Action: load the DLL (by full path: it is next to the test).
    std::wstring path = fake::logPath(L"x");  // program folder + "x.log"
    path = path.substr(0, path.size() - 5) + name;
    HMODULE h = LoadLibraryW(path.c_str());
    REQUIRE(h);
    // Expected: the callback fired once, with the loaded DLL's address.
    CHECK(g_fired == 1);
    CHECK(g_firedBase == reinterpret_cast<uintptr_t>(h));

    // Unloaded and loaded again - the callback is not called any more.
    FreeLibrary(h);
    h = LoadLibraryW(path.c_str());
    REQUIRE(h);
    CHECK(g_fired == 1);
    FreeLibrary(h);
}
