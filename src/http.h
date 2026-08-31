// FeedKit - http.h
// WinHTTP wrapper: GET to memory or streaming to a file, with progress.
#pragma once

#include <string>
#include <functional>
#include <cstdint>
#include <optional>

namespace fk {

struct HttpProgress {
    uint64_t downloaded = 0;
    uint64_t total = 0; // 0 = unknown
};

struct HttpResponse {
    bool ok = false;
    unsigned status = 0;
    std::string body;
    std::wstring error;
};

// Callback returns false to abort the transfer.
using HttpCallback = std::function<bool(const HttpProgress&)>;

HttpResponse http_get(const std::wstring& url, const HttpCallback& cb = nullptr);

// Streams to disk. Returns true on HTTP 200 and full write.
bool http_download_to_file(const std::wstring& url, const std::wstring& dest_path,
                           const HttpCallback& cb = nullptr, std::wstring* error = nullptr);

} // namespace fk
