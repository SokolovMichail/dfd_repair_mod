// Пустая DLL для теста DllWatcher (patch_h_test): тест сначала подписывается на её загрузку, потом
// загружает её сам и проверяет, что уведомление пришло ровно один раз и с правильным адресом.
#include <windows.h>

extern "C" __declspec(dllexport) int dfd_watcher_test_marker() { return 42; }
