// FeedKit - util.cpp
#include "util.h"

#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <shlobj.h>

#include <cstdarg>
#include <cstdio>
#include <algorithm>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")

namespace fk {

std::wstring to_wide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr, 0);
    if (n <= 0) {
        n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
        if (n <= 0) return {};
    }
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

std::string to_utf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), ::towlower);
    return s;
}

std::wstring trim(const std::wstring& s) {
    size_t b = s.find_first_not_of(L" \t\r\n");
    if (b == std::wstring::npos) return {};
    size_t e = s.find_last_not_of(L" \t\r\n");
    return s.substr(b, e - b + 1);
}

bool starts_with(const std::wstring& s, const std::wstring& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

bool ends_with(const std::wstring& s, const std::wstring& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::vector<std::wstring> split(const std::wstring& s, wchar_t sep) {
    std::vector<std::wstring> out;
    size_t start = 0;
    while (true) {
        size_t p = s.find(sep, start);
        if (p == std::wstring::npos) { out.push_back(s.substr(start)); break; }
        out.push_back(s.substr(start, p - start));
        start = p + 1;
    }
    return out;
}

std::wstring fmt(const wchar_t* f, ...) {
    va_list args1, args2;
    va_start(args1, f);
    va_copy(args2, args1);
    int n = _vscwprintf(f, args1);
    va_end(args1);
    std::wstring out(n > 0 ? n : 0, L'\0');
    if (n > 0) _vsnwprintf_s(out.data(), out.size() + 1, _TRUNCATE, f, args2);
    va_end(args2);
    return out;
}

std::wstring human_size(uint64_t bytes) {
    wchar_t buf[64];
    if (bytes >= 1024ull * 1024 * 1024)
        swprintf_s(buf, L"%.1f GB", (double)bytes / (1024.0 * 1024 * 1024));
    else if (bytes >= 1024ull * 1024)
        swprintf_s(buf, L"%.1f MB", (double)bytes / (1024.0 * 1024));
    else if (bytes >= 1024)
        swprintf_s(buf, L"%.1f KB", (double)bytes / 1024.0);
    else
        swprintf_s(buf, L"%llu B", (unsigned long long)bytes);
    return buf;
}

std::wstring timestamp_now() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    return fmt(L"%04u-%02u-%02u %02u:%02u:%02u", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
}

std::wstring exe_dir() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return path_parent(buf);
}

std::wstring path_combine(const std::wstring& a, const std::wstring& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    wchar_t buf[MAX_PATH];
    PathCombineW(buf, a.c_str(), b.c_str());
    return buf;
}

std::wstring path_parent(const std::wstring& p) {
    wchar_t drive[_MAX_DRIVE] = {}, dir[_MAX_DIR] = {};
    _wsplitpath_s(p.c_str(), drive, _MAX_DRIVE, dir, _MAX_DIR, nullptr, 0, nullptr, 0);
    std::wstring out = std::wstring(drive) + dir;
    while (!out.empty() && (out.back() == L'\\' || out.back() == L'/')) out.pop_back();
    return out;
}

std::wstring path_filename(const std::wstring& p) {
    wchar_t name[_MAX_FNAME] = {}, ext[_MAX_EXT] = {};
    _wsplitpath_s(p.c_str(), nullptr, 0, nullptr, 0, name, _MAX_FNAME, ext, _MAX_EXT);
    return std::wstring(name) + ext;
}

std::wstring path_extension(const std::wstring& p) {
    size_t dot = p.find_last_of(L'.');
    size_t slash = p.find_last_of(L"\\/");
    if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash)) return {};
    return lower(p.substr(dot));
}

std::wstring local_appdata() {
    wchar_t* p = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p))) {
        std::wstring out = p;
        CoTaskMemFree(p);
        return out;
    }
    return exe_dir();
}

bool file_exists(const std::wstring& path) {
    return PathFileExistsW(path.c_str()) && !PathIsDirectoryW(path.c_str());
}

bool dir_exists(const std::wstring& path) {
    return PathIsDirectoryW(path.c_str());
}

bool make_dirs(const std::wstring& path) {
    // Walk slash positions and create each prefix. Avoids PathCombine's
    // drive-relative pitfall ("C:" + "Users" -> "C:Users").
    std::wstring p = path;
    while (!p.empty() && (p.back() == L'\\' || p.back() == L'/')) p.pop_back();
    if (p.size() < 2) return dir_exists(p);

    size_t start = 0;
    if (p.size() >= 2 && p[1] == L':') {
        start = 3; // after "X:\"
    } else if (starts_with(p, L"\\\\")) {
        // UNC: don't try to create \\server\share itself
        size_t n = p.find(L'\\', 2);
        if (n == std::wstring::npos) return dir_exists(p);
        n = p.find(L'\\', n + 1);
        if (n == std::wstring::npos) return dir_exists(p);
        start = n + 1;
    } else if (!p.empty() && p[0] == L'\\') {
        start = 1;
    }

    for (size_t i = start; i <= p.size(); i++) {
        if (i != p.size() && p[i] != L'\\' && p[i] != L'/') continue;
        std::wstring cur = p.substr(0, i);
        if (cur.empty() || (cur.size() == 2 && cur[1] == L':'))
            continue; // drive root
        if (!dir_exists(cur) && !CreateDirectoryW(cur.c_str(), nullptr) &&
            GetLastError() != ERROR_ALREADY_EXISTS)
            return false;
    }
    return dir_exists(p);
}

bool read_file(const std::wstring& path, std::string& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{};
    bool ok = GetFileSizeEx(h, &sz) && sz.QuadPart >= 0;
    if (ok) {
        out.resize((size_t)sz.QuadPart);
        DWORD read = 0;
        ok = ReadFile(h, out.data(), (DWORD)out.size(), &read, nullptr) && read == out.size();
    }
    CloseHandle(h);
    return ok;
}

bool write_file(const std::wstring& path, const void* data, size_t size) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(h, data, (DWORD)size, &written, nullptr) && written == size;
    CloseHandle(h);
    return ok;
}

bool copy_file(const std::wstring& from, const std::wstring& to, bool overwrite) {
    return CopyFileW(from.c_str(), to.c_str(), !overwrite) != FALSE;
}

bool delete_file(const std::wstring& path) {
    return DeleteFileW(path.c_str()) != FALSE || GetLastError() == ERROR_FILE_NOT_FOUND;
}

bool delete_dir_recursive(const std::wstring& path) {
    std::wstring spec = path + L"\\*";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(spec.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    do {
        std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        std::wstring full = path_combine(path, name);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            delete_dir_recursive(full);
        else
            delete_file(full);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return RemoveDirectoryW(path.c_str()) != FALSE;
}

bool is_dir_empty(const std::wstring& path) {
    std::wstring spec = path + L"\\*";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(spec.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return true;
    bool empty = true;
    do {
        std::wstring name = fd.cFileName;
        if (name != L"." && name != L"..") { empty = false; break; }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return empty;
}

bool remove_dir_if_empty(const std::wstring& path) {
    return is_dir_empty(path) && RemoveDirectoryW(path.c_str()) != FALSE;
}

uint64_t file_size(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa)) return 0;
    return ((uint64_t)fa.nFileSizeHigh << 32) | fa.nFileSizeLow;
}

std::vector<std::wstring> list_files_recursive(const std::wstring& dir) {
    std::vector<std::wstring> out;
    std::wstring spec = dir + L"\\*";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(spec.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        std::wstring full = path_combine(dir, name);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            for (auto& f : list_files_recursive(full)) out.push_back(std::move(f));
        } else {
            out.push_back(std::move(full));
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return out;
}

bool file_is_writable(const std::wstring& path) {
    if (!file_exists(path)) return true;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    CloseHandle(h);
    return true;
}

bool open_folder(const std::wstring& path) {
    return (uintptr_t)ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL) > 32;
}

bool open_url(const std::wstring& url) {
    return (uintptr_t)ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL) > 32;
}

int version_compare(const std::wstring& a, const std::wstring& b) {
    size_t ia = 0, ib = 0;
    auto next_token = [](const std::wstring& s, size_t& i, bool& is_num) {
        while (i < s.size() && !iswalnum(s[i])) i++;
        size_t start = i;
        if (i < s.size() && iswdigit(s[i])) {
            while (i < s.size() && iswdigit(s[i])) i++;
            is_num = true;
        } else {
            while (i < s.size() && iswalpha(s[i])) i++;
            is_num = false;
        }
        if (start == i) return std::wstring();
        return s.substr(start, i - start);
    };
    while (ia < a.size() || ib < b.size()) {
        bool na = false, nb = false;
        std::wstring ta = next_token(a, ia, na);
        std::wstring tb = next_token(b, ib, nb);
        if (ta.empty() && tb.empty()) break;
        if (ta.empty()) return -1;
        if (tb.empty()) return 1;
        if (na && nb) {
            unsigned long long va = wcstoull(ta.c_str(), nullptr, 10);
            unsigned long long vb = wcstoull(tb.c_str(), nullptr, 10);
            if (va != vb) return va < vb ? -1 : 1;
        } else if (na != nb) {
            // numeric tokens sort above alpha tokens ("310.8.0" > "310.8.SF" handled by caller's SF preference;
            // here digits > letters so 310.8.SF-v2 vs 310.8.0 compares .SF > .0 which matches upstream intent).
            return na ? 1 : -1;
        } else {
            int c = _wcsicmp(ta.c_str(), tb.c_str());
            if (c != 0) return c < 0 ? -1 : 1;
        }
    }
    return 0;
}

} // namespace fk
