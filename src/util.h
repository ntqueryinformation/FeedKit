// FeedKit - util.h
// Small helpers: string conversion, file/path ops, version parsing.
#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>

namespace fk {

// --- string helpers ---
std::wstring to_wide(const std::string& s);
std::string  to_utf8(const std::wstring& w);
std::wstring lower(std::wstring s);
std::wstring trim(const std::wstring& s);
bool starts_with(const std::wstring& s, const std::wstring& prefix);
bool ends_with(const std::wstring& s, const std::wstring& suffix);
std::vector<std::wstring> split(const std::wstring& s, wchar_t sep);

// --- formatting ---
std::wstring fmt(const wchar_t* fmt, ...);
std::wstring human_size(uint64_t bytes);

// --- timestamps ---
std::wstring timestamp_now(); // "2026-08-30 12:34:56"

// --- paths ---
std::wstring exe_dir();                        // directory of FeedKit.exe
std::wstring path_combine(const std::wstring& a, const std::wstring& b);
std::wstring path_parent(const std::wstring& p);
std::wstring path_filename(const std::wstring& p);
std::wstring path_extension(const std::wstring& p); // includes dot, lowercased
std::wstring local_appdata();                  // %LOCALAPPDATA%

// --- file ops ---
bool file_exists(const std::wstring& path);
bool dir_exists(const std::wstring& path);
bool make_dirs(const std::wstring& path);      // creates all missing components
bool read_file(const std::wstring& path, std::string& out);
bool write_file(const std::wstring& path, const void* data, size_t size);
bool copy_file(const std::wstring& from, const std::wstring& to, bool overwrite);
bool delete_file(const std::wstring& path);
bool delete_dir_recursive(const std::wstring& path);
bool is_dir_empty(const std::wstring& path);
bool remove_dir_if_empty(const std::wstring& path);
uint64_t file_size(const std::wstring& path);

// All files under `dir`, recursively, as absolute paths.
std::vector<std::wstring> list_files_recursive(const std::wstring& dir);

// Returns true if the file can be opened for exclusive write access.
bool file_is_writable(const std::wstring& path);

// Shell helpers
bool open_folder(const std::wstring& path);
bool open_url(const std::wstring& url);

// --- version compare ---
// Tokenizes on non-alphanumerics; digit runs compare numerically, letter runs
// lexically, digits sort above letters so 310.8.0 > 310.7.129 and 310.8.SF-v2 > 310.8.0.
int version_compare(const std::wstring& a, const std::wstring& b);

} // namespace fk
