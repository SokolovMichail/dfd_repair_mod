// SparesSupply.dll on its own: the entry point when our winhttp.dll loads the plugin from bin\plugins. It
// waits for logic.dll and patches it the moment it is loaded (onLogicLoaded, common.cpp).
//
// This file is the only one built into the standalone DLL and not into the tests: they call onLogicLoaded
// themselves.

#include "common.h"

#ifndef SPARES_SUPPLY_VERSION  // задаётся из CMake (MOD_VERSION)
#define SPARES_SUPPLY_VERSION "dev"
#endif

namespace spares_supply {

dfd::DllWatcher g_watcher;

}  // namespace spares_supply

using namespace spares_supply;

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
