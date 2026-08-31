// FeedKit - pe_bitness.h
#pragma once

#include <string>

namespace fk {

enum class PeArch { Unknown, X86, X64, Arm64 };

PeArch pe_arch(const std::wstring& exe_path);
const wchar_t* pe_arch_name(PeArch arch); // L"x64", L"x86 (32-bit)", ... or L"unknown"

} // namespace fk
