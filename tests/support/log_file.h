// Reading the plugin's log from tests.
//
// The plugin writes its log to <program folder>\<name>.log (dfd::Log). A test opens the same kind of log for
// the test program and looks at what was appended: LogTail remembers the file size when it is created and
// later returns only the new lines - so each test sees only its own entries.
#pragma once

#include <windows.h>

#include <cstdio>
#include <string>

namespace fake {

// The path where dfd::Log::open(GetModuleHandle(nullptr), name) creates the file: next to the test program.
inline std::wstring logPath(const wchar_t* name) {
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    while (n && path[n - 1] != L'\\') --n;
    return std::wstring(path, n) + name + L".log";
}

// The whole text of a file (empty string if there is no file).
inline std::string readFile(const std::wstring& path) {
    std::string out;
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return out;
    char buf[4096];
    for (size_t n; (n = fread(buf, 1, sizeof buf, f)) > 0;) out.append(buf, n);
    fclose(f);
    return out;
}

// How many times the substring `what` occurs in text.
inline int countOf(const std::string& text, const std::string& what) {
    int n = 0;
    for (size_t p = text.find(what); p != std::string::npos; p = text.find(what, p + what.size())) ++n;
    return n;
}

// The "tail" of a log: everything written after this object was created.
class LogTail {
public:
    explicit LogTail(std::wstring path) : path_(std::move(path)), start_(readFile(path_).size()) {}
    std::string text() const {
        std::string all = readFile(path_);
        return all.size() > start_ ? all.substr(start_) : std::string();
    }
    bool has(const std::string& what) const { return text().find(what) != std::string::npos; }
    int count(const std::string& what) const { return countOf(text(), what); }

private:
    std::wstring path_;
    size_t start_;
};

}  // namespace fake
