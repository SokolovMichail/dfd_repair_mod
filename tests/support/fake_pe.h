// A fake loaded module (PE image) for tests.
//
// Why. The plugin does not know in advance where the functions and tables it needs live in logic.dll: it
// searches for them when the game loads - by characteristic byte sequences of code ("patterns") and by
// class names the compiler leaves in the file (RTTI). To test that search without the game, we build a
// small "file" in memory laid out like a real DLL: headers and three sections - code (.text), constants
// (.rdata) and data (.data). Any bytes and "classes" can be put into it, and then the plugin code is run on it.
//
// Layout (offsets from the image start, as in a real DLL after loading):
//   0x00000  headers (DOS + PE + section headers)
//   0x01000  .text   - "code"; free space is filled with CC (int3), which matches none of the patterns
//   0x11000  .rdata  - constants, strings, vtables (class method tables)
//   0x19000  .data   - data, including RTTI class names
#pragma once

#include <windows.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace fake {

// Pattern text to bytes: "48 8B ?? 05" -> {48, 8B, 00, 05}. A ?? becomes 0 - the test then writes the
// needed offset there itself (as the compiler would have).
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

// The bytes of a value (float, pointer, ...) - to put it into the image.
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

    // An image section: its bounds and a "cursor" - where to put the next bytes.
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

        // Headers: exactly what dfd::Image reads - where the PE header is, the number and descriptions of
        // the sections, the timestamp and the image size. The "MZ" and "PE" signatures are deliberately left
        // out: Image never checks them, and an in-memory image carrying them looks like a manually loaded DLL
        // to antivirus heuristics (Kaspersky flagged the tests as a trojan).
        auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base_);
        dos->e_lfanew = 0x80;
        auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base_ + 0x80);
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

    // Put bytes into a section (at the cursor, aligned to `align`) and return their address.
    uintptr_t put(Sec& s, const std::vector<uint8_t>& b, size_t align = 16) {
        s.cur = (s.cur + align - 1) / align * align;
        if (s.cur + b.size() > s.end) throw std::runtime_error("fake PE section is full");
        uintptr_t at = s.cur;
        memcpy(reinterpret_cast<void*>(at), b.data(), b.size());
        s.cur += b.size();
        return at;
    }
    uintptr_t put(Sec& s, const char* pattern, size_t align = 16) { return put(s, hex(pattern), align); }
    // A string with its terminating zero.
    uintptr_t putString(Sec& s, const char* str) {
        return put(s, std::vector<uint8_t>(str, str + strlen(str) + 1), 1);
    }

    // Write bytes at an address (to fake or spoil something already placed).
    static void write(uintptr_t at, const std::vector<uint8_t>& b) {
        memcpy(reinterpret_cast<void*>(at), b.data(), b.size());
    }
    // Write a rip-relative offset (4 bytes at offset dispAt) into instruction insn of length len, so that the
    // instruction points to target. That is how the CPU (and Image::ripTarget) computes the address:
    // end of instruction + offset.
    static void aim(uintptr_t insn, size_t dispAt, size_t len, uintptr_t target) {
        int32_t d = static_cast<int32_t>(static_cast<intptr_t>(target) - static_cast<intptr_t>(insn + len));
        memcpy(reinterpret_cast<void*>(insn + dispAt), &d, 4);
    }

    // A class with RTTI, as MSVC leaves it, and its vtable with `slots` methods (empty for now).
    //   .data:  TypeDescriptor - 16 bytes of service fields and the name ".?AV<cls>@@"
    //   .rdata: CompleteObjectLocator (COL) - 6 numbers: signature 1, offset (0 - the primary vtable), ...,
    //           RVA of the TypeDescriptor, ..., RVA of the COL itself
    //   .rdata: a pointer to the COL, directly followed by the vtable
    // colOffset != 0 or badSelf - a "wrong" COL that Image::vtableOf must skip.
    // Returns the vtable address. The name's address (for spoiling it in tests) goes to *nameAt.
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
    // Write the address of a "function" into a vtable slot.
    static void setSlot(uintptr_t vtable, int slot, uintptr_t fn) {
        *reinterpret_cast<uintptr_t*>(vtable + 8 * slot) = fn;
    }

    // A copy of the whole image - to check later that nothing (or only what was expected) changed.
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
