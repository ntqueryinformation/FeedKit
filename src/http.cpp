// FeedKit - http.cpp
#include "http.h"
#include "util.h"

#include <windows.h>
#include <winhttp.h>

#include <fstream>

#pragma comment(lib, "winhttp.lib")

namespace fk {

namespace {

struct UrlParts {
    std::wstring host;
    std::wstring path;
    INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT;
    bool secure = true;
};

bool crack_url(const std::wstring& url, UrlParts& out) {
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = {};
    wchar_t path[2048] = {};
    uc.lpszHostName = host;
    uc.dwHostNameLength = 255;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 2047;
    if (!WinHttpCrackUrl(url.c_str(), (DWORD)url.size(), 0, &uc)) return false;
    out.host = host;
    out.path = path;
    out.port = uc.nPort;
    out.secure = (uc.nScheme == INTERNET_SCHEME_HTTPS);
    if (out.path.empty()) out.path = L"/";
    return true;
}

// Shared request runner. `sink` receives chunks; returns false to abort.
template <typename Sink>
bool run_request(const std::wstring& url, const HttpCallback& cb, Sink& sink,
                 unsigned* status_out, std::wstring* error_out) {
    UrlParts parts;
    if (!crack_url(url, parts)) {
        if (error_out) *error_out = L"Invalid URL: " + url;
        return false;
    }

    bool ok = false;
    HINTERNET session = nullptr, connect = nullptr, request = nullptr;
    do {
        session = WinHttpOpen(L"FeedKit/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session) break;
        // GitHub + reshade.me are modern TLS endpoints; restrict to TLS 1.2+.
        DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
        WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));

        connect = WinHttpConnect(session, parts.host.c_str(), parts.port, 0);
        if (!connect) break;

        DWORD flags = parts.secure ? WINHTTP_FLAG_SECURE : 0;
        request = WinHttpOpenRequest(connect, L"GET", parts.path.c_str(), nullptr,
                                     WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (!request) break;

        // Redirects (releases/latest -> objects.githubusercontent.com) are followed by default.
        if (parts.secure)
            WinHttpSetOption(request, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));

        if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
            break;
        if (!WinHttpReceiveResponse(request, nullptr)) break;

        DWORD status = 0, size = sizeof(status);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
        if (status_out) *status_out = status;
        if (status != 200) {
            if (error_out)
                *error_out = fmt(L"HTTP %u for %s", status, url.c_str());
            break;
        }

        uint64_t total = 0;
        wchar_t len_buf[32] = {};
        DWORD len_size = sizeof(len_buf);
        if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX,
                                len_buf, &len_size, WINHTTP_NO_HEADER_INDEX))
            total = _wcstoui64(len_buf, nullptr, 10);

        HttpProgress prog;
        prog.total = total;
        char buf[64 * 1024];
        ok = true;
        for (;;) {
            DWORD avail = 0;
            if (!WinHttpQueryDataAvailable(request, &avail)) { ok = false; break; }
            if (avail == 0) break;
            if (avail > sizeof(buf)) avail = sizeof(buf);
            DWORD read = 0;
            if (!WinHttpReadData(request, buf, avail, &read)) { ok = false; break; }
            if (read == 0) break;
            if (!sink(buf, read)) { ok = false; if (error_out) *error_out = L"Cancelled"; break; }
            prog.downloaded += read;
            if (cb && !cb(prog)) { ok = false; if (error_out) *error_out = L"Cancelled"; break; }
        }
    } while (false);

    if (!ok && error_out && error_out->empty()) {
        *error_out = fmt(L"WinHTTP error %u for %s", GetLastError(), url.c_str());
    }
    if (request) WinHttpCloseHandle(request);
    if (connect) WinHttpCloseHandle(connect);
    if (session) WinHttpCloseHandle(session);
    return ok;
}

} // namespace

HttpResponse http_get(const std::wstring& url, const HttpCallback& cb) {
    HttpResponse res;
    std::string body;
    auto sink = [&](const char* data, DWORD size) {
        body.append(data, size);
        return true;
    };
    res.ok = run_request(url, cb, sink, &res.status, &res.error);
    res.body = std::move(body);
    return res;
}

bool http_download_to_file(const std::wstring& url, const std::wstring& dest_path,
                           const HttpCallback& cb, std::wstring* error, unsigned* status_out) {
    if (status_out) *status_out = 0;
    std::ofstream out(dest_path, std::ios::binary | std::ios::trunc);
    if (!out) {
        if (error) *error = L"Cannot create file: " + dest_path;
        return false;
    }
    bool file_ok = true;
    auto sink = [&](const char* data, DWORD size) {
        out.write(data, size);
        if (!out) { file_ok = false; return false; }
        return true;
    };
    unsigned status = 0;
    bool ok = run_request(url, cb, sink, &status, error);
    if (status_out) *status_out = status;
    out.close();
    if (!file_ok) {
        if (error) *error = L"Disk write failed: " + dest_path;
        return false;
    }
    if (!ok) {
        delete_file(dest_path);
        return false;
    }
    return true;
}

} // namespace fk
