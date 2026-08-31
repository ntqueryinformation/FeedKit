// FeedKit - ini_text.h
// Exact-case text INI editing for ReShade.ini. The Windows profile APIs are
// case-insensitive, but ReShade's own parser is not - keys that differ only by
// case are distinct to it, so edits must be done at the text level.
#pragma once

#include <string>

namespace fk {

// Reads the value of `key` in `[section]` with exact key-case match.
bool ini_get_exact(const std::wstring& file, const std::wstring& section,
                   const std::wstring& key, std::wstring& value);

// Sets (or inserts) `key` in `[section]` with exact case preserved.
// An empty `value` deletes the key line instead (no-op if absent).
bool ini_set_exact(const std::wstring& file, const std::wstring& section,
                   const std::wstring& key, const std::wstring& value);

} // namespace fk
