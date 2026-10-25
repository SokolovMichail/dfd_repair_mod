// Поддельный загруженный модуль (PE-образ) для тестов.
//
// Зачем. Плагин не знает заранее, где в logic.dll лежат нужные ему функции и таблицы: он ищет их при
// загрузке игры - по характерным последовательностям байтов кода («шаблонам») и по именам классов,
// которые компилятор оставляет в файле (RTTI). Чтобы проверить этот поиск без игры, мы строим в памяти
// маленький «файл», устроенный как настоящая DLL: заголовок и три раздела - код (.text), константы (.rdata)
// и данные (.data). В него можно положить любые байты и «классы», а потом натравить на него код плагина.
//
// Раскладка (смещения от начала образа, как в настоящей DLL после загрузки):
//   0x00000  заголовки (DOS + PE + описания разделов)
//   0x01000  .text   - «код»; свободное место заполнено байтом CC (int3), который не похож ни на один шаблон
//   0x11000  .rdata  - константы, строки, vtable (таблицы методов классов)
//   0x19000  .data   - данные, в том числе имена классов RTTI
#pragma once

#include <windows.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace fake {

// Текст шаблона в байты: "48 8B ?? 05" -> {48, 8B, 00, 05}. На месте ?? ставится 0 - тест потом сам
// впишет туда нужное смещение (как это сделал бы компилятор).
inline std::vector<uint8_t> hex(const char* s) {
    std::vector<uint8_t> out;
    while (*s) {
        if (*s == ' ') {
            ++s;
        } else if (*s == '?') {
            out.push_back(0);
            s += (s[1] == '?') ? 2 : 1;
        } else {
            out.push_back(static_cast<uint8_t>(strtoul(std::string(s, 2).c_str(), nullptr, 16)));
            s += 2;
        }
    }
    return out;
}

// Байты числа (float, указатель и т.п.) - чтобы положить его в образ.
template <class T>
std::vector<uint8_t> bytesOf(const T& v) {
    std::vector<uint8_t> b(sizeof(T));
    memcpy(b.data(), &v, sizeof(T));
    return b;
}

class Pe {
public:
    static constexpr uint32_t kText = 0x1000, kTextSize = 0x10000;
    static constexpr uint32_t kRdata = 0x11000, kRdataSize = 0x8000;
    static constexpr uint32_t kData = 0x19000, kDataSize = 0x8000;
    static constexpr uint32_t kImageSize = 0x21000;
    static constexpr uint32_t kTimeDateStamp = 0x12345678;

    // Раздел образа: границы и «курсор» - куда положить следующие байты.
    struct Sec {
        uintptr_t begin, end, cur;
    };

    Pe() {
        base_ = reinterpret_cast<uintptr_t>(
            VirtualAlloc(nullptr, kImageSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        if (!base_) throw std::runtime_error("VirtualAlloc failed");
        memset(reinterpret_cast<void*>(base_ + kText), 0xCC, kTextSize);
        text = {base_ + kText, base_ + kText + kTextSize, base_ + kText};
        rdata = {base_ + kRdata, base_ + kRdata + kRdataSize, base_ + kRdata};
        data = {base_ + kData, base_ + kData + kDataSize, base_ + kData};

        // Заголовки: ровно то, что читает dfd::Image - адрес PE-заголовка, число и описания разделов,
        // метка времени и размер образа.
        auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base_);
        dos->e_magic = IMAGE_DOS_SIGNATURE;
        dos->e_lfanew = 0x80;
        auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base_ + 0x80);
        nt->Signature = IMAGE_NT_SIGNATURE;
        nt->FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
        nt->FileHeader.NumberOfSections = 3;
        nt->FileHeader.TimeDateStamp = kTimeDateStamp;
        nt->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
        nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
        nt->OptionalHeader.SizeOfImage = kImageSize;
        auto s = IMAGE_FIRST_SECTION(nt);
        section(s[0], ".text", kText, kTextSize);
        section(s[1], ".rdata", kRdata, kRdataSize);
        section(s[2], ".data", kData, kDataSize);
    }
    ~Pe() { VirtualFree(reinterpret_cast<void*>(base_), 0, MEM_RELEASE); }
    Pe(const Pe&) = delete;
    Pe& operator=(const Pe&) = delete;

    uintptr_t base() const { return base_; }
    uint32_t rva(uintptr_t a) const { return static_cast<uint32_t>(a - base_); }

    // Положить байты в раздел (по курсору, с выравниванием align) и вернуть их адрес.
    uintptr_t put(Sec& s, const std::vector<uint8_t>& b, size_t align = 16) {
        s.cur = (s.cur + align - 1) / align * align;
        if (s.cur + b.size() > s.end) throw std::runtime_error("fake PE section is full");
        uintptr_t at = s.cur;
        memcpy(reinterpret_cast<void*>(at), b.data(), b.size());
        s.cur += b.size();
        return at;
    }
    uintptr_t put(Sec& s, const char* pattern, size_t align = 16) { return put(s, hex(pattern), align); }
    // Строка с завершающим нулём.
    uintptr_t putString(Sec& s, const char* str) {
        return put(s, std::vector<uint8_t>(str, str + strlen(str) + 1), 1);
    }

    // Записать байты по адресу (подделать/испортить уже положенное).
    static void write(uintptr_t at, const std::vector<uint8_t>& b) {
        memcpy(reinterpret_cast<void*>(at), b.data(), b.size());
    }
    // Вписать в инструкцию insn длиной len rip-относительное смещение (4 байта по смещению dispAt) так,
    // чтобы инструкция указывала на target. Так процессор (и Image::ripTarget) считает адрес:
    // конец инструкции + смещение.
    static void aim(uintptr_t insn, size_t dispAt, size_t len, uintptr_t target) {
        int32_t d = static_cast<int32_t>(static_cast<intptr_t>(target) - static_cast<intptr_t>(insn + len));
        memcpy(reinterpret_cast<void*>(insn + dispAt), &d, 4);
    }

    // Класс с RTTI, как его оставляет компилятор MSVC, и его vtable на slots методов (пока пустых).
    //   .data:  TypeDescriptor - 16 байт служебных полей и имя ".?AV<cls>@@"
    //   .rdata: CompleteObjectLocator (COL) - 6 чисел: подпись 1, offset (0 - основная vtable), ...,
    //           RVA TypeDescriptor, ..., RVA самого COL
    //   .rdata: указатель на COL, сразу за ним - vtable
    // colOffset != 0 или badSelf - «неправильный» COL, который Image::vtableOf должен пропустить.
    // Возвращает адрес vtable. Адрес имени (для порчи в тестах) - в *nameAt.
    uintptr_t addClass(const char* cls, int slots, uint32_t colOffset = 0, bool badSelf = false,
                       uintptr_t* nameAt = nullptr) {
        std::string name = std::string(".?AV") + cls + "@@";
        std::vector<uint8_t> td(16, 0);
        td.insert(td.end(), name.begin(), name.end());
        td.push_back(0);
        uintptr_t tdAt = put(data, td, 16);
        if (nameAt) *nameAt = tdAt + 16;
        uintptr_t colAt = put(rdata, std::vector<uint8_t>(24, 0), 8);
        uint32_t col[6] = {1, colOffset, 0, rva(tdAt), 0, rva(colAt) + (badSelf ? 4u : 0u)};
        memcpy(reinterpret_cast<void*>(colAt), col, sizeof(col));
        std::vector<uint8_t> vt = bytesOf(static_cast<uint64_t>(colAt));
        vt.resize(8 + 8 * slots, 0);
        return put(rdata, vt, 8) + 8;
    }
    // Записать в слот vtable адрес «функции».
    static void setSlot(uintptr_t vtable, int slot, uintptr_t fn) {
        *reinterpret_cast<uintptr_t*>(vtable + 8 * slot) = fn;
    }

    // Копия всего образа - чтобы потом проверить, что в нём ничего (или только ожидаемое) не изменилось.
    std::vector<uint8_t> snapshot() const {
        auto p = reinterpret_cast<const uint8_t*>(base_);
        return std::vector<uint8_t>(p, p + kImageSize);
    }

    Sec text{}, rdata{}, data{};

private:
    static void section(IMAGE_SECTION_HEADER& h, const char* name, uint32_t va, uint32_t size) {
        memcpy(h.Name, name, strlen(name));
        h.VirtualAddress = va;
        h.Misc.VirtualSize = size;
    }

    uintptr_t base_ = 0;
};

}  // namespace fake
