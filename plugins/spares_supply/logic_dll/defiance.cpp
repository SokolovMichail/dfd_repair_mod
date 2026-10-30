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

#include <memory>
#include <string>

#include "../../../third_party/defiance/defiance.h"
#include "common.h"

#ifndef SPARES_SUPPLY_VERSION  // set by CMake (MOD_VERSION); must match the manifest's "version"
#define SPARES_SUPPLY_VERSION "dev"
#endif

namespace spares_supply {

namespace {

// Must match "id" in spares_repair.plugin.json: the loader checks it, and settings are looked up by it.
constexpr const char kPluginId[] = "sokolov.spares-repair";
// DefianceLoader's own plugin that patches the same spare parts radius instruction (off by default).
constexpr const char kSupplyRadiusId[] = "defiance.supply-radius";

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
