// FeedKit - pe_bitness.cpp
#include "pe_bitness.h"
#include "util.h"

#include <windows.h>

namespace fk {

PeArch pe_arch(const std::wstring& exe_path) {
    std::string img;
    if (!read_file(exe_path, img) || img.size() < 0x100)
        return PeArch::Unknown;

    IMAGE_DOS_HEADER dos{};
    memcpy(&dos, img.data(), sizeof(dos));
    if (dos.e_magic != IMAGE_DOS_SIGNATURE) return PeArch::Unknown;

    size_t pe_off = (size_t)dos.e_lfanew;
    if (pe_off + sizeof(uint32_t) + sizeof(IMAGE_FILE_HEADER) > img.size())
        return PeArch::Unknown;

    uint32_t pe_sig = 0;
    memcpy(&pe_sig, img.data() + pe_off, sizeof(pe_sig));
    if (pe_sig != IMAGE_NT_SIGNATURE) return PeArch::Unknown;

    IMAGE_FILE_HEADER coff{};
    memcpy(&coff, img.data() + pe_off + sizeof(uint32_t), sizeof(coff));

    switch (coff.Machine) {
    case IMAGE_FILE_MACHINE_I386:  return PeArch::X86;
    case IMAGE_FILE_MACHINE_AMD64: return PeArch::X64;
    case IMAGE_FILE_MACHINE_ARM64: return PeArch::Arm64;
    default:                       return PeArch::Unknown;
    }
}

const wchar_t* pe_arch_name(PeArch arch) {
    switch (arch) {
    case PeArch::X86:    return L"x86 (32-bit)";
    case PeArch::X64:    return L"x64";
    case PeArch::Arm64:  return L"ARM64";
    default:             return L"unknown";
    }
}

} // namespace fk
