// Чтение лога плагина из тестов.
//
// Плагин пишет лог в файл <папка программы>\<имя>.log (dfd::Log). Тест открывает такой же лог для
// тестовой программы и смотрит, что в него дописалось: LogTail запоминает размер файла при создании и
// потом отдаёт только новые строки - так каждый тест видит лишь свои записи.
#pragma once

#include <windows.h>

#include <cstdio>
#include <string>

namespace fake {

// Путь, по которому dfd::Log::open(GetModuleHandle(nullptr), name) создаёт файл: рядом с тестовой программой.
inline std::wstring logPath(const wchar_t* name) {
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    while (n && path[n - 1] != L'\\') --n;
    return std::wstring(path, n) + name + L".log";
}

// Весь текст файла (пустая строка, если файла нет).
inline std::string readFile(const std::wstring& path) {
    std::string out;
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return out;
    char buf[4096];
    for (size_t n; (n = fread(buf, 1, sizeof buf, f)) > 0;) out.append(buf, n);
    fclose(f);
    return out;
}

// Сколько раз подстрока what встречается в text.
inline int countOf(const std::string& text, const std::string& what) {
    int n = 0;
    for (size_t p = text.find(what); p != std::string::npos; p = text.find(what, p + what.size())) ++n;
    return n;
}

// «Хвост» лога: всё, что записано после создания объекта.
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
