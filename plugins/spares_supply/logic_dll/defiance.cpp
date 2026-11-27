// spares_repair.dll: the same plugin as a DefianceLoader plugin (https://github.com/DefianceLoader/DefianceLoader).
//
// The loader loads the DLL from DefianceLoader\plugins, with spares_repair.plugin.json beside it, and calls
// init() once logic.dll is in the process. init finds the same places as the standalone DLL (install) but
// writes none of them itself: every write is handed to the loader (patch_bytes). The loader checks them
// against the other plugins' writes and makes them all together after init, so the hooks go live only when
// the loader commits its plan. If anything is not found or not as expected, init fails and the loader drops
// everything this plugin asked for.
//
// The rules and the hooks are the standalone DLL's. Only the entry point, the log and the way the patches go
// in differ.

#include <shlobj.h>

#include <cctype>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "../../../third_party/defiance/defiance.h"
#include "common.h"
#include "defiance.h"

#ifndef SPARES_SUPPLY_VERSION  // set by CMake (MOD_VERSION); must match the manifest's "version"
#define SPARES_SUPPLY_VERSION "dev"
#endif

namespace spares_supply {

namespace {

// Must match "id" in spares_repair.plugin.json: the loader checks it, and settings are looked up by it.
constexpr const char kPluginId[] = "sokolov.spares-repair";
// DefianceLoader's own plugin that patches the same spare parts radius instruction (off by default).
constexpr const char kSupplyRadiusId[] = "defiance.supply-radius";
// DefianceLoader's plugin that merges mods' <table>.<mod folder>.patch.xml into the game's tables (off by default).
constexpr const char kTableMergeId[] = "defiance.table-merge";

const DefianceApi* g_api = nullptr;

// Log lines go to DefianceLoader\logs\defiance-loader.log, marked with the plugin's ID by the loader.
void toLoader(const char* line) { g_api->log(DEFIANCE_LOG_INFO, line); }

// A bool setting from the manifest, as the loader validated it ("true" / "false"). fallback - no such value.
bool setting(const char* key, bool fallback) {
    const char* v = g_api->config_get(kPluginId, key);
    return v ? strcmp(v, "true") == 0 : fallback;
}

// The loaded module the code at addr belongs to; nullptr - none (not a module's memory).
HMODULE moduleAt(const void* addr) {
    HMODULE m = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       static_cast<LPCWSTR>(addr), &m);
    return m;
}
// A module's file name without the folder, e.g. "SparesSupply.dll".
std::string moduleName(HMODULE m) {
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(m, path, MAX_PATH);
    const wchar_t* name = path + n;
    while (name > path && name[-1] != L'\\') --name;
    char utf8[MAX_PATH * 3];
    int len = WideCharToMultiByte(CP_UTF8, 0, name, -1, utf8, sizeof utf8, nullptr, nullptr);
    return len > 0 ? utf8 : "?";
}

// The loader's Patcher. apply() checks every write the way dfd::PatchSet does, then asks the loader for each
// one (patch_bytes) instead of writing it. Every write, a vtable slot included, is a plain byte replacement:
// 8 bytes of the slot, from the original function's address to the hook's. The same list becomes the patch
// contract (defiance_patch_contract_v1), so the loader can check that init asked for exactly these writes.
class DefiancePatcher final : public Patcher {
public:
    explicit DefiancePatcher(const Image& img) : img_(img) {}

    void bytes(const char* what, uintptr_t at, std::vector<uint8_t> orig, std::vector<uint8_t> repl) override {
        items_.push_back({what, at, std::move(orig), std::move(repl), nullptr, nullptr, nullptr});
    }
    void slot(const char* what, uintptr_t vtable, int slot, const char* prologue, void* hook,
              void** saved) override {
        uintptr_t at = vtable ? vtable + 8u * slot : 0;
        uintptr_t cur = at ? *reinterpret_cast<uintptr_t*>(at) : 0, repl = reinterpret_cast<uintptr_t>(hook);
        std::vector<uint8_t> o(8), r(8);
        memcpy(o.data(), &cur, 8);
        memcpy(r.data(), &repl, 8);
        items_.push_back({what, at, std::move(o), std::move(r), saved, reinterpret_cast<void*>(cur), prologue});
    }

    bool apply() override {
        // First check everything: a write the loader would refuse half way leaves init failed anyway, but a
        // clear reason here is easier to read than the loader's.
        for (auto& it : items_) {
            if (!it.at) {
                g_log("not found: %s - nothing patched", it.what);
                return false;
            }
            bool ok = memcmp(reinterpret_cast<const void*>(it.at), it.orig.data(), it.orig.size()) == 0;
            if (ok && it.prologue)
                ok = it.savedValue && Image::matches(reinterpret_cast<uintptr_t>(it.savedValue), it.prologue);
            if (!ok) {
                // A slot that points outside logic.dll was redirected by another mod - most likely the
                // standalone SparesSupply.dll, which patches logic.dll the moment it loads, before any plugin.
                HMODULE owner = it.prologue && it.savedValue ? moduleAt(it.savedValue) : nullptr;
                if (owner && reinterpret_cast<uintptr_t>(owner) != img_.base())
                    g_log("verify FAILED: %s at RVA %08X already points into %s - another mod patched it first "
                          "(both SparesSupply builds installed? remove bin\\plugins\\SparesSupply.dll) - nothing "
                          "patched",
                          it.what, img_.rva(it.at), moduleName(owner).c_str());
                else
                    g_log("verify FAILED: %s at RVA %08X - nothing patched", it.what, img_.rva(it.at));
                return false;
            }
        }
        for (auto& it : items_) {
            if (g_api->patch_bytes(reinterpret_cast<void*>(it.at), it.orig.data(), it.repl.data(), it.orig.size()) !=
                0) {
                // init fails; the loader drops what this plugin staged so far
                g_log("refused by the loader: %s at RVA %08X - nothing patched", it.what, img_.rva(it.at));
                return false;
            }
            if (it.saved) *it.saved = it.savedValue;
            g_log("staged: %s (RVA %08X)", it.what, img_.rva(it.at));
            contract_.push_back({"logic.dll", img_.rva(it.at), DEFIANCE_PATCH_KIND_BYTES, it.orig.data(),
                                 it.orig.size(), it.repl.data(), it.repl.size()});
        }
        return true;
    }

    // The writes apply() staged, for the patch contract. Points into this object.
    const std::vector<DefiancePatchContractEntryV1>& contract() const { return contract_; }

private:
    struct Item {
        const char* what;
        uintptr_t at;
        std::vector<uint8_t> orig, repl;
        void** saved;
        void* savedValue;
        const char* prologue;
    };
    Image img_;
    std::vector<Item> items_;
    std::vector<DefiancePatchContractEntryV1> contract_;
};

// Kept for the process: the contract points into the patcher.
std::unique_ptr<DefiancePatcher> g_patcher;

// The folder a path is in: everything before its last '\' (or '/'); empty - no folder part.
std::wstring parentOf(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

// The folder this DLL is in (DefianceLoader\plugins); empty - not known.
std::wstring ownFolder() {
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(moduleAt(reinterpret_cast<const void*>(&ownFolder)), path, MAX_PATH);
    return n && n < MAX_PATH ? parentOf(path) : std::wstring();
}

// Documents\My Games\Terminator Dark Fate\mods\mods.json - where the game keeps its mod list. Documents is asked
// from Windows (it may be redirected, e.g. to OneDrive); empty - Windows does not name it.
std::wstring modsJson() {
    PWSTR docs = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs)))
        out = std::wstring(docs) + L"\\My Games\\Terminator Dark Fate\\mods\\mods.json";
    CoTaskMemFree(docs);
    return out;
}

// UTF-8 <-> UTF-16 for paths (the game's mods.json is UTF-8, Windows paths are wide).
std::wstring wide(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(n > 0 ? n : 0, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}
std::string utf8(const std::wstring& s) {
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(n > 0 ? n : 0, '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr, nullptr);
    return out;
}

// ASCII letters to lower case (folder names the table merge accepts are plain ASCII; other bytes stay).
std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// A whole file's bytes; nullopt - it cannot be opened.
std::optional<std::string> readAll(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return std::nullopt;
    std::string out;
    char buf[4096];
    DWORD got = 0;
    while (ReadFile(h, buf, sizeof buf, &got, nullptr) && got) out.append(buf, got);
    CloseHandle(h);
    return out;
}

// Every file under folder (recursively), as paths relative to it with '\' between parts.
void filesUnder(const std::wstring& folder, const std::wstring& rel, std::vector<std::wstring>& out) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((folder + L"\\" + rel + (rel.empty() ? L"*" : L"\\*")).c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        std::wstring path = rel.empty() ? name : rel + L"\\" + name;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) filesUnder(folder, path, out);
        } else {
            out.push_back(path);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

// The string value after "key": in a JSON object's text, or nullopt; plain strings only (no escapes in folder names
// the game writes).
std::optional<std::string> jsonString(const std::string& object, const char* key) {
    size_t k = object.find('"' + std::string(key) + '"');
    if (k == std::string::npos) return std::nullopt;
    size_t colon = object.find(':', k);
    size_t open = colon == std::string::npos ? colon : object.find('"', colon);
    size_t close = open == std::string::npos ? open : object.find('"', open + 1);
    if (close == std::string::npos) return std::nullopt;
    return object.substr(open + 1, close - open - 1);
}

// Whether "key": true is in a JSON object's text.
bool jsonTrue(const std::string& object, const char* key) {
    size_t k = object.find('"' + std::string(key) + '"');
    if (k == std::string::npos) return false;
    size_t colon = object.find(':', k);
    if (colon == std::string::npos) return false;
    size_t v = object.find_first_not_of(" \t\r\n", colon + 1);
    return v != std::string::npos && object.compare(v, 4, "true") == 0;
}

// Called by the loader on its startup thread, logic.dll already loaded. 0 - every write is staged.
int32_t init(const DefianceApi* api) {
    if (!api || api->abi_version != DEFIANCE_ABI_VERSION || api->reserved != 0) return 1;
    g_api = api;
    g_log.sendTo(&toLoader);
    g_logEvents = setting("log", true);
    g_log("SparesSupply " SPARES_SUPPLY_VERSION " as a DefianceLoader plugin; event log %s",
          g_logEvents ? "on" : "off");
    // defiance.supply-radius rewrites the same radius instruction; when it is on, it owns the radius and we
    // leave it alone (two writes to one place would get one of the plugins refused). Not installed - null.
    const char* supplyRadius = api->config_get(kSupplyRadiusId, "enabled");
    g_sparesRadius = !(supplyRadius && strcmp(supplyRadius, "true") == 0);
    if (!g_sparesRadius) g_log("%s is enabled: it sets the spare parts radius", kSupplyRadiusId);
    // A mod that ships table patches adds nothing while the table merge is off - name such mods (defiance.h). The
    // game's mods folder is two levels up from DefianceLoader\plugins.
    std::wstring game = parentOf(parentOf(ownFolder()));
    if (!game.empty()) warnIgnoredTablePatches(api->config_get(kTableMergeId, "enabled"), modsJson(), game + L"\\mods");

    void* base = api->module_base("logic.dll");
    if (!base) {
        g_log("logic.dll is not loaded - nothing patched");
        return 1;
    }
    Image img(reinterpret_cast<uintptr_t>(base));
    g_patcher = std::make_unique<DefiancePatcher>(img);
    if (!install(img, *g_patcher)) {
        g_patcher.reset();  // no contract for a failed init
        return 1;
    }
    // The loader writes after init; the circle lea is made writable when it is first needed (redrawSparesCircle).
    g_circleDeferred = true;
    g_log("SparesSupply staged: active once DefianceLoader commits its patch plan");
    return 0;
}

// The loader restores every write it made for this plugin; nothing of ours to undo.
void stop() { g_log("SparesSupply stopped"); }

const DefiancePlugin kPlugin = {DEFIANCE_ABI_VERSION, kPluginId, SPARES_SUPPLY_VERSION, &init, &stop};

}  // namespace

// mods.json is an array of objects; each object is read on its own, so other keys (and their order) do not matter.
std::vector<std::string> enabledMods(const std::string& json) {
    std::vector<std::string> out;
    for (size_t open = json.find('{'); open != std::string::npos; open = json.find('{', open + 1)) {
        size_t close = json.find('}', open);
        if (close == std::string::npos) break;
        std::string object = json.substr(open, close - open + 1);
        std::optional<std::string> folder = jsonString(object, "folder");
        if (folder && !folder->empty() && jsonTrue(object, "enabled")) out.push_back(*folder);
        open = close;
    }
    return out;
}

// The table merge asks for <table>.<folder in lower case>.patch with the providers' extensions (.xml, .txt); the
// folder name is compared without case, as Windows and the game compare it.
std::vector<std::string> tablePatches(const std::wstring& modFolder, const std::string& folder) {
    std::vector<std::string> out;
    const std::string tail = "." + lower(folder) + ".patch";
    std::vector<std::wstring> files;
    filesUnder(modFolder, L"", files);
    for (const std::wstring& rel : files) {
        std::string path = utf8(rel);
        size_t slash = path.find_last_of('\\');
        std::string name = lower(slash == std::string::npos ? path : path.substr(slash + 1));
        for (const char* ext : {".xml", ".txt"}) {
            std::string end = tail + ext;
            if (name.size() > end.size() && name.compare(name.size() - end.size(), end.size(), end) == 0) {
                for (char& c : path)
                    if (c == '\\') c = '/';
                out.push_back(path);
                break;
            }
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

// Only a warning: whatever goes wrong while looking (no mods.json, an odd folder), the plugin starts as usual.
int warnIgnoredTablePatches(const char* tableMerge, const std::wstring& modsJson, const std::wstring& modsDir) {
    if (tableMerge && strcmp(tableMerge, "true") == 0) return 0;  // the merge is on: patches are merged
    int named = 0;
    try {
        std::optional<std::string> json = readAll(modsJson);
        if (!json) return 0;
        for (const std::string& folder : enabledMods(*json)) {
            std::vector<std::string> found = tablePatches(modsDir + L"\\" + wide(folder), folder);
            if (found.empty()) continue;
            ++named;
            g_log("mod %s ships %zu table patches (%s ...) that are IGNORED: %s is %s - set enabled = true under "
                  "[%s] in DefianceLoader\\config\\settings.ini and restart",
                  folder.c_str(), found.size(), found.front().c_str(), kTableMergeId,
                  tableMerge ? "off" : "not installed", kTableMergeId);
        }
    } catch (const std::exception&) {
        // an odd path or a file system error: no warning, nothing else changes
    }
    return named;
}

}  // namespace spares_supply

using namespace spares_supply;

// The export the loader looks for. The struct lives for the process.
extern "C" __declspec(dllexport) const DefiancePlugin* defiance_plugin() { return &kPlugin; }

// The writes init staged, for the loader to compare with what it got. null - init did not succeed.
extern "C" __declspec(dllexport) const DefiancePatchContractV1* defiance_patch_contract_v1(const DefianceApi*) {
    static DefiancePatchContractV1 contract{};
    if (!g_patcher) return nullptr;
    const auto& entries = g_patcher->contract();
    contract = {1, sizeof contract, entries.data(), entries.size()};
    return &contract;
}
