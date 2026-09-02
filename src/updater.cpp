// FeedKit - updater.cpp
#include "updater.h"
#include "http.h"
#include "json_lite.h"
#include "util.h"
#include "version.h"

#include <windows.h>
#include <shellapi.h>
#include <bcrypt.h>

#include <regex>
#include <vector>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "bcrypt.lib")

#ifndef NT_SUCCESS
#define NT_SUCCESS(s) (((NTSTATUS)(s)) >= 0)
#endif

namespace fk {

namespace {
[[noreturn]] void fail(const std::wstring& msg) { throw std::runtime_error(to_utf8(msg)); }


const wchar_t kReleasesApi[] = L"https://api.github.com/repos/ntqueryinformation/FeedKit/releases/latest";
const wchar_t kReleaseExeName[] = L"FeedKit.exe";

// "v1.4.0" -> "1.4.0"
std::wstring strip_v(const std::wstring& v) {
    return (!v.empty() && (v[0] == L'v' || v[0] == L'V')) ? v.substr(1) : v;
}

// Pulls the 64-hex SHA-256 that release notes state after "SHA-256".
std::wstring sha_from_release_body(const std::string& body) {
    std::regex re("SHA-256[^`]*`([0-9a-fA-F]{64})`");
    std::smatch m;
    if (std::regex_search(body, m, re))
        return to_wide(m[1].str());
    return {};
}

// SHA-256 of a file via Windows CNG. Empty result = hash unavailable (skip check).
std::wstring sha256_of_file(const std::wstring& path) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (FAILED(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0)))
        return {};
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD hash_len = 0, cb = 0;
    std::wstring hex;
    if (NT_SUCCESS(BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0)) &&
        NT_SUCCESS(BCryptGetProperty(hash, BCRYPT_HASH_LENGTH, (PUCHAR)&hash_len, sizeof(hash_len), &cb, 0)) &&
        hash_len > 0 && hash_len <= 64) {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            std::vector<unsigned char> buf(256 * 1024);
            std::vector<unsigned char> digest(hash_len);
            DWORD read = 0;
            bool ok = true;
            while (true) {
                if (!ReadFile(h, buf.data(), (DWORD)buf.size(), &read, nullptr)) { ok = false; break; }
                if (read == 0) break;
                if (!NT_SUCCESS(BCryptHashData(hash, buf.data(), read, 0))) { ok = false; break; }
            }
            if (ok && NT_SUCCESS(BCryptFinishHash(hash, digest.data(), hash_len, 0))) {
                static const wchar_t* hexd = L"0123456789abcdef";
                for (unsigned char b : digest) {
                    hex += hexd[b >> 4];
                    hex += hexd[b & 0xF];
                }
            }
            CloseHandle(h);
        }
    }
    if (hash) BCryptDestroyHash(hash);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    return hex;
}

} // namespace

void check_latest_version(
    const std::wstring& local_version,
    const std::function<void(UpdateState state, const std::wstring& version_or_error)>& report) {
    try {
        HttpResponse r = http_get(kReleasesApi);
        if (!r.ok)
            fail(r.error.empty() ? L"cannot reach github.com" : r.error);

        Json j;
        if (!Json::parse(r.body, j))
            fail(L"unexpected GitHub API response");

        std::wstring tag = j.get_str(L"tag_name");
        std::wstring remote = strip_v(tag);
        if (remote.empty())
            fail(L"release has no tag name");

        if (version_compare(local_version, remote) >= 0) {
            report(UpdateState::UpToDate, remote);
            return;
        }
        report(UpdateState::Available, remote);
    } catch (const std::exception& e) {
        report(UpdateState::Failed, to_wide(e.what()));
    }
}

void perform_update(
    const std::wstring& local_version,
    const std::function<void(const std::wstring& log)>& log,
    const std::function<void(bool ok, const std::wstring& msg, bool restart_requested)>& done,
    const std::function<bool(const std::wstring& question)>& ask) {
    (void)ask;
    wchar_t exe_path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe_path, MAX_PATH);

    try {
        log(L"Checking the latest release...");
        HttpResponse r = http_get(kReleasesApi);
        if (!r.ok)
            fail(r.error.empty() ? L"cannot reach github.com" : r.error);

        Json j;
        if (!Json::parse(r.body, j))
            fail(L"unexpected GitHub API response");

        std::wstring remote = strip_v(j.get_str(L"tag_name"));
        if (remote.empty())
            fail(L"release has no tag name");

        if (version_compare(local_version, remote) >= 0) {
            done(true, L"Already running the latest version (" + remote + L").", false);
            return;
        }

        // Locate the FeedKit.exe asset.
        std::wstring asset_url;
        const Json* assets = j.find(L"assets");
        if (assets && assets->is(Json::Type::Array)) {
            for (const auto& a : assets->items) {
                if (lower(a.get_str(L"name")) == lower(kReleaseExeName)) {
                    asset_url = a.get_str(L"browser_download_url");
                    break;
                }
            }
        }
        if (asset_url.empty())
            fail(L"The latest release does not contain a FeedKit.exe asset.");

        // Integrity: release notes state the SHA-256 of the attached exe.
        std::wstring expected_sha = sha_from_release_body(to_utf8(j.get_str(L"body")));

        log(L"Downloading FeedKit " + remote + L"...");
        std::wstring new_exe = std::wstring(exe_path) + L".new";
        std::wstring dl_err;
        unsigned status = 0;
        if (!http_download_to_file(asset_url, new_exe, nullptr, &dl_err, &status))
            fail(L"Download failed: " + (dl_err.empty() ? fmt(L"HTTP %u", status) : dl_err));

        // Integrity: verify the downloaded exe against the SHA-256 stated in
        // the release notes (skipped only if the hash could not be computed).
        if (!expected_sha.empty()) {
            std::wstring actual = sha256_of_file(new_exe);
            if (actual.empty())
                fail(L"Could not hash the downloaded update - refusing to install it.");
            if (lower(actual) != lower(expected_sha))
                fail(L"Downloaded update hash mismatch - the download is corrupt or "
                     L"was tampered with. Expected " + expected_sha + L", got " + actual + L".");
            log(L"Download verified (SHA-256 OK).");
        }

        // Swap: rename the running exe away, then move the new one into place.
        std::wstring old_exe = std::wstring(exe_path) + L".old";
        delete_file(old_exe);
        if (!MoveFileExW(exe_path, old_exe.c_str(), MOVEFILE_REPLACE_EXISTING))
            fail(L"Cannot rename the running FeedKit.exe: error " + std::to_wstring(GetLastError()));
        if (!MoveFileExW(new_exe.c_str(), exe_path, MOVEFILE_REPLACE_EXISTING)) {
            MoveFileExW(old_exe.c_str(), exe_path, MOVEFILE_REPLACE_EXISTING); // undo
            fail(L"Cannot move the new FeedKit.exe into place: error " + std::to_wstring(GetLastError()));
        }
        log(L"FeedKit " + remote + L" installed. It applies on the next start.");
        done(true, L"FeedKit " + remote + L" has been installed. Restart FeedKit to use it.", true);
    } catch (const std::exception& e) {
        done(false, to_wide(e.what()), false);
    }
}

} // namespace fk
