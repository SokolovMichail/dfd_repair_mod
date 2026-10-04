// winhttp.dll - прокси-загрузчик.
//
// Лежит в bin\ игры рядом с game.dll, поэтому подгружается вместо системной winhttp.dll.
// 1. Пересылает все экспорты в настоящую %SystemRoot%\System32\winhttp.dll (переходники в winhttp_exports.S).
// 2. Загружает все плагины bin\plugins\*.dll (по алфавиту). Сам ничего в игре не меняет.
// Лог: bin\plugins\loader.log

#include <windows.h>

#include <algorithm>
#include <string>
#include <vector>

#include "../common/patch.h"

// Адреса настоящих функций; переходники в winhttp_exports.S прыгают по g_orig[i].
static const char* const kExportNames[] = {
#include "winhttp_exports.inc"
};
constexpr size_t kExportCount = sizeof(kExportNames) / sizeof(kExportNames[0]);
extern "C" {
void* g_orig[kExportCount] = {};
}

static dfd::Log g_log;

// На случай, если в системной winhttp.dll какой-то функции нет: вернуть 0/FALSE.
static DWORD_PTR WINAPI missingExport() {
    SetLastError(ERROR_PROC_NOT_FOUND);
    return 0;
}

static std::wstring dirOf(HMODULE m) {
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(m, path, MAX_PATH);
    while (n && path[n - 1] != L'\\') --n;
    return std::wstring(path, n);
}

static bool loadSystemWinhttp() {
    wchar_t sys[MAX_PATH];
    UINT n = GetSystemDirectoryW(sys, MAX_PATH);
    std::wstring path = std::wstring(sys, n) + L"\\winhttp.dll";
    HMODULE real = LoadLibraryW(path.c_str());
    if (!real) {
        g_log("cannot load %ls (error %lu)", path.c_str(), GetLastError());
        return false;
    }
    for (size_t i = 0; i < kExportCount; ++i) {
        FARPROC p = GetProcAddress(real, kExportNames[i]);
        if (!p) g_log("system winhttp.dll has no %s", kExportNames[i]);
        g_orig[i] = p ? reinterpret_cast<void*>(p) : reinterpret_cast<void*>(&missingExport);
    }
    return true;
}

static void loadPlugins(const std::wstring& dir) {
    std::vector<std::wstring> files;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"*.dll").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) {
        g_log("no plugins in %ls", dir.c_str());
        return;
    }
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) files.emplace_back(fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(files.begin(), files.end());

    for (auto& f : files) {
        // LOAD_WITH_ALTERED_SEARCH_PATH: зависимости плагина ищутся сначала в bin\plugins\.
        HMODULE m = LoadLibraryExW((dir + f).c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (m)
            g_log("loaded %ls", f.c_str());
        else
            g_log("FAILED to load %ls (error %lu)", f.c_str(), GetLastError());
    }
}

BOOL WINAPI DllMain(HINSTANCE self, DWORD reason, LPVOID) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(self);

    // Вспомогательные процессы встроенного браузера (CEF: "... --type=renderer" и т.п.) тоже грузят
    // winhttp.dll из bin\. В них только пересылаем вызовы: без плагинов и без лога
    // (иначе они перезаписывают логи основного процесса).
    bool mainProcess = wcsstr(GetCommandLineW(), L"--type=") == nullptr;

    std::wstring plugins = dirOf(self) + L"plugins\\";
    if (mainProcess) {
        CreateDirectoryW(plugins.c_str(), nullptr);
        g_log.open(self, L"plugins\\loader");
        g_log("DFDRepairMod loader started: %ls", GetCommandLineW());
    }

    if (!loadSystemWinhttp()) return FALSE;
    if (!mainProcess) return TRUE;
    // Плагины грузятся прямо здесь (под loader lock), чтобы успеть до logic.dll.
    // Поэтому в DllMain плагина: никаких потоков с ожиданием и LoadLibrary - только регистрация.
    loadPlugins(plugins);
    return TRUE;
}
