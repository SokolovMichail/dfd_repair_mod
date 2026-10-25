// An empty DLL for the DllWatcher test (patch_h_test): the test first subscribes to its load, then loads it
// itself and checks that the notification arrived exactly once and with the right address.
#include <windows.h>

extern "C" __declspec(dllexport) int dfd_watcher_test_marker() { return 42; }
