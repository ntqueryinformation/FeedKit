// FeedKit - sources.cpp
#include "sources.h"
#include "http.h"
#include "json_lite.h"
#include "util.h"

#include <miniz.h>

#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace fk {

std::wstring fetch_temp_dir() {
    static std::wstring dir;
    if (dir.empty()) {
        dir = local_appdata() + L"\\FeedKit\\downloads";
        make_dirs(dir);
    }
    return dir;
}

namespace {

ProgressFn fanout(const LogFn& log, const char* label) {
    // Per-file progress: show percentage only (totals differ per file).
    return [log, label](uint64_t done, uint64_t total) {
        if (total > 0)
            log(fmt(L"  %s %u%%", label, (unsigned)(done * 100 / total)));
        else
            log(fmt(L"  %s %s...", label, human_size(done).c_str()));
    };
}

bool download(const std::wstring& url, const std::wstring& dest, const ProgressFn& progress,
              std::wstring* err = nullptr) {
    return http_download_to_file(
        url, dest,
        [progress](const HttpProgress& p) {
            if (progress) progress(p.downloaded, p.total);
            return true;
        },
        err);
}

DownloadedFile dl_to(const std::wstring& url, const std::wstring& name, const ProgressFn& progress) {
    DownloadedFile f;
    f.name = name;
    f.local_path = path_combine(fetch_temp_dir(), name);
    std::wstring err;
    if (!download(url, f.local_path, progress, &err))
        throw std::runtime_error(to_utf8(err));
    f.size = file_size(f.local_path);
    return f;
}

// Throw helpers so the fetch pipeline can unwind to the caller's catch.
[[noreturn]] void fail(const std::wstring& msg) { throw std::runtime_error(to_utf8(msg)); }

void check_http_body(const std::wstring& what, const HttpResponse& r) {
    if (!r.ok) fail(what + L": " + r.error);
}

// Wildcard match with a single '*' (or exact match when no '*').
bool wc_match(const std::wstring& pattern, const std::wstring& s) {
    size_t star = pattern.find(L'*');
    if (star == std::wstring::npos) return lower(pattern) == lower(s);
    std::wstring pre = lower(pattern.substr(0, star));
    std::wstring post = lower(pattern.substr(star + 1));
    std::wstring ls = lower(s);
    return starts_with(ls, pre) && ends_with(ls, post) && ls.size() >= pre.size() + post.size();
}

} // namespace

std::vector<std::wstring> zip_extract_matching(const std::wstring& zip_path,
                                               const std::wstring& dest_dir,
                                               const std::vector<std::wstring>& patterns) {
    std::vector<std::wstring> extracted;
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_file(&zip, to_utf8(zip_path).c_str(), 0))
        fail(L"Cannot open zip: " + zip_path);

    int count = (int)mz_zip_reader_get_num_files(&zip);
    for (int i = 0; i < count; i++) {
        char name_buf[512] = {};
        mz_zip_reader_get_filename(&zip, i, name_buf, sizeof(name_buf));
        std::wstring name = to_wide(name_buf);

        bool match = false;
        for (const auto& p : patterns)
            if (wc_match(p, name)) { match = true; break; }
        if (!match) continue;

        // Flatten any internal folder structure.
        std::wstring flat = name;
        size_t slash = flat.find_last_of(L"/\\");
        if (slash != std::wstring::npos) flat = flat.substr(slash + 1);
        if (flat.empty()) continue;

        std::wstring dest = path_combine(dest_dir, flat);
        size_t size = 0;
        void* data = mz_zip_reader_extract_to_heap(&zip, i, &size, 0);
        if (!data) continue;
        bool ok = write_file(dest, data, size);
        mz_free(data);
        if (!ok) {
            mz_zip_reader_end(&zip);
            fail(L"Failed writing " + dest);
        }
        extracted.push_back(dest);
    }
    mz_zip_reader_end(&zip);
    if (extracted.empty())
        fail(L"No matching files in " + path_filename(zip_path) + L" for the given patterns");
    return extracted;
}

FeederBundle fetch_feeder(const LogFn& log, const ProgressFn& progress) {
    FeederBundle out;
    const std::wstring base = L"https://github.com/jlrouzies-fr/DLSS5-Feeder/releases/latest/download";

    // Resolve the tag for display (best effort - downloads themselves use the
    // stable /latest/ URL).
    HttpResponse r = http_get(L"https://api.github.com/repos/jlrouzies-fr/DLSS5-Feeder/releases/latest");
    if (r.ok) {
        Json j;
        if (Json::parse(r.body, j)) out.release_tag = j.get_str(L"tag_name");
    }
    log(L"DLSS5-Feeder latest release: " + (out.release_tag.empty() ? L"<unknown>" : out.release_tag));

    out.addon64 = dl_to(base + L"/dlss5-feed.addon64", L"dlss5-feed.addon64", progress);
    out.addon32 = dl_to(base + L"/dlss5-feed.addon32", L"dlss5-feed.addon32", progress);
    out.fx_shader = dl_to(base + L"/DLSS5_Feed.fx", L"DLSS5_Feed.fx", progress);
    out.host64_exe = dl_to(base + L"/dlss5-feed-host64.exe", L"dlss5-feed-host64.exe", progress);

    // Optional component: absent from some releases.
    std::wstring vk = path_combine(fetch_temp_dir(), L"feed-vk-layer.zip");
    if (download(base + L"/feed-vk-layer.zip", vk, progress)) {
        out.vk_layer_zip = DownloadedFile{L"feed-vk-layer.zip", vk, file_size(vk)};
    } else {
        log(L"  feed-vk-layer.zip not published in this release, Vulkan layer unavailable");
    }

    out.ok = true;
    return out;
}

RenoDxBundle fetch_renodx_dlss5(const LogFn& log, const ProgressFn& progress) {
    RenoDxBundle out;
    log(L"Querying RankFTW/rhi-repo for the newest renodx-dlss5 release...");

    HttpResponse r = http_get(L"https://api.github.com/repos/RankFTW/rhi-repo/releases?per_page=100");
    check_http_body(L"GitHub API (rhi-repo releases)", r);

    Json j;
    if (!Json::parse(r.body, j) || !j.is(Json::Type::Array))
        fail(L"Unexpected GitHub API response (rhi-repo releases)");

    // Newest release whose tag starts with renodx-dlss5-, newest by published_at
    // is authoritative but list order can shuffle; sort by tag version.
    struct Rel { std::wstring tag, version, zip_url; };
    std::vector<Rel> rels;
    for (const auto& rel : j.items) {
        std::wstring tag = rel.get_str(L"tag_name");
        if (!starts_with(lower(tag), L"renodx-dlss5-")) continue;
        const Json* assets = rel.find(L"assets");
        if (!assets || !assets->is(Json::Type::Array)) continue;
        for (const auto& a : assets->items) {
            std::wstring name = a.get_str(L"name");
            if (!ends_with(lower(name), L".zip")) continue;
            Rel e;
            e.tag = tag;
            e.version = tag.substr(wcslen(L"renodx-dlss5-"));
            e.zip_url = a.get_str(L"browser_download_url");
            rels.push_back(std::move(e));
            break;
        }
    }
    if (rels.empty()) fail(L"No renodx-dlss5 releases found in RankFTW/rhi-repo");
    std::sort(rels.begin(), rels.end(), [](const Rel& a, const Rel& b) {
        return version_compare(a.version, b.version) > 0;
    });
    const Rel& newest = rels.front();
    out.version = newest.version;
    log(L"renodx-dlss5 newest release: " + newest.version);

    std::wstring zip = path_combine(fetch_temp_dir(), L"renodx-dlss5.zip");
    download(newest.zip_url, zip, progress);

    auto files = zip_extract_matching(zip, fetch_temp_dir(), {L"*.addon64"});
    // Prefer the canonical name if several addons ship in the zip.
    for (const auto& f : files)
        if (lower(path_filename(f)) == L"renodx-dlss5.addon64") { out.addon64_path = f; break; }
    if (out.addon64_path.empty()) out.addon64_path = files.front();
    out.ok = true;
    return out;
}

namespace {

// Pick the newest entry from a dlss_manifest array. For dlssnr, prefer the
// ShortFuse builds (version contains "SF"), matching RHI's shipped default.
std::wstring pick_from_manifest(const Json& arr, bool prefer_sf, std::wstring& version_out) {
    if (!arr.is(Json::Type::Array) || arr.items.empty()) return {};
    const Json* best = nullptr;
    std::wstring best_ver;
    for (const auto& e : arr.items) {
        std::wstring ver = e.get_str(L"version");
        std::wstring url = e.get_str(L"url");
        if (ver.empty() || url.empty()) continue;
        if (!best) { best = &e; best_ver = ver; continue; }
        bool better = version_compare(ver, best_ver) > 0;
        if (prefer_sf) {
            bool e_sf = ver.find(L"SF") != std::wstring::npos;
            bool b_sf = best_ver.find(L"SF") != std::wstring::npos;
            if (e_sf && !b_sf) better = true;
            if (!e_sf && b_sf) better = false;
        }
        if (better) { best = &e; best_ver = ver; }
    }
    version_out = best_ver;
    return best ? best->get_str(L"url") : std::wstring();
}

std::wstring pick_from_manifest_by_key(const Json& manifest, const wchar_t* key, std::wstring& version_out) {
    const Json* arr = manifest.find(key);
    if (!arr) return {};
    return pick_from_manifest(*arr, wcscmp(key, L"dlssnr") == 0, version_out);
}

} // namespace

NgxBundle fetch_ngx_dlls(const LogFn& log, const ProgressFn& progress) {
    NgxBundle out;
    log(L"Fetching RHI dlss_manifest.json (DLSS NR / SR sources)...");

    HttpResponse r = http_get(L"https://raw.githubusercontent.com/RankFTW/RHI/main/dlss_manifest.json");
    check_http_body(L"dlss_manifest.json", r);

    Json manifest;
    if (!Json::parse(r.body, manifest) || !manifest.is(Json::Type::Object))
        fail(L"Unexpected dlss_manifest.json content");

    // Neural rendering DLL (nvngx_dlssnr.dll)
    std::wstring nr_ver, sr_ver;
    std::wstring nr_url = pick_from_manifest_by_key(manifest, L"dlssnr", nr_ver);
    if (nr_url.empty()) fail(L"No dlssnr entries in dlss_manifest.json");
    out.nr_version = nr_ver;
    log(L"nvngx_dlssnr newest version: " + nr_ver);
    std::wstring nr_zip = path_combine(fetch_temp_dir(), L"nvngx_dlssnr.zip");
    download(nr_url, nr_zip, progress);
    out.nr_dll_path = zip_extract_matching(nr_zip, fetch_temp_dir(), {L"*.dll"}).front();

    // DLSS SR DLL (nvngx_dlss.dll)
    std::wstring sr_url = pick_from_manifest_by_key(manifest, L"dlss", sr_ver);
    if (sr_url.empty()) fail(L"No dlss entries in dlss_manifest.json");
    out.sr_version = sr_ver;
    log(L"nvngx_dlss newest version: " + sr_ver);
    std::wstring sr_zip = path_combine(fetch_temp_dir(), L"nvngx_dlss.zip");
    download(sr_url, sr_zip, progress);
    out.sr_dll_path = zip_extract_matching(sr_zip, fetch_temp_dir(), {L"*.dll"}).front();

    out.ok = true;
    return out;
}

ReshadeBundle fetch_reshade(const LogFn& log, const ProgressFn& progress) {
    ReshadeBundle out;
    log(L"Checking reshade.me for the current version...");

    HttpResponse r = http_get(L"https://reshade.me/");
    check_http_body(L"reshade.me", r);

    std::wstring page = to_wide(r.body);
    // Find ReShade_Setup_<ver>_Addon.exe
    std::wstring version;
    size_t pos = 0;
    while (true) {
        pos = lower(page).find(L"reshade_setup_", pos);
        if (pos == std::wstring::npos) break;
        size_t vstart = pos + wcslen(L"reshade_setup_");
        size_t vend = vstart;
        while (vend < page.size() && (iswdigit(page[vend]) || page[vend] == L'.')) vend++;
        if (page.compare(vend, wcslen(L"_addon"), L"_Addon") == 0 || page.compare(vend, wcslen(L"_addon"), L"_addon") == 0) {
            std::wstring v = page.substr(vstart, vend - vstart);
            if (version.empty() || version_compare(v, version) > 0) version = v;
        }
        pos = vend;
    }
    if (version.empty())
        fail(L"Could not find a ReShade_Setup_*_Addon.exe link on reshade.me - the site layout may have changed. "
             L"Download ReShade with add-on support manually from https://reshade.me and run its setup on the game.");

    out.version = version;
    log(L"ReShade latest version: " + version);

    std::wstring url = fmt(L"https://reshade.me/downloads/ReShade_Setup_%s_Addon.exe", version.c_str());
    out.setup_exe_path = path_combine(fetch_temp_dir(), fmt(L"ReShade_Setup_%s_Addon.exe", version.c_str()));
    download(url, out.setup_exe_path, progress);
    out.ok = true;
    return out;
}

LumeniteBundle fetch_lumenite(const LogFn& log, const ProgressFn& progress) {
    LumeniteBundle out;
    log(L"Fetching LumeniteFX (motion-vector provider) from github.com/umar-afzaal/LumeniteFX...");

    // Resolve the default branch so renames don't break us.
    std::wstring branch = L"main";
    HttpResponse meta = http_get(L"https://api.github.com/repos/umar-afzaal/LumeniteFX");
    if (meta.ok) {
        Json j;
        if (Json::parse(meta.body, j)) {
            std::wstring b = j.get_str(L"default_branch");
            if (!b.empty()) branch = b;
        }
    }
    out.branch = branch;
    log(L"  source branch: " + branch);

    std::wstring zip = path_combine(fetch_temp_dir(), L"LumeniteFX.zip");
    std::wstring url = fmt(L"https://codeload.github.com/umar-afzaal/LumeniteFX/zip/refs/heads/%s", branch.c_str());
    download(url, zip, progress);

    out.staging_dir = path_combine(fetch_temp_dir(), L"lumenite_stage");
    if (dir_exists(out.staging_dir))
        delete_dir_recursive(out.staging_dir);
    make_dirs(out.staging_dir);

    mz_zip_archive z{};
    if (!mz_zip_reader_init_file(&z, to_utf8(zip).c_str(), 0))
        fail(L"Cannot open LumeniteFX zipball");

    int count = (int)mz_zip_reader_get_num_files(&z);
    for (int i = 0; i < count; i++) {
        char name_buf[512] = {};
        mz_zip_reader_get_filename(&z, i, name_buf, sizeof(name_buf));
        std::wstring name = to_wide(name_buf);
        if (mz_zip_reader_is_file_a_directory(&z, (mz_uint)i)) continue;

        // Strip the repo-root prefix ("LumeniteFX-<branch>/").
        size_t slash = name.find(L'/');
        if (slash == std::wstring::npos) continue;
        std::wstring rel = name.substr(slash + 1);
        if (rel.empty()) continue;

        // Map to the reshade-shaders layout, preserving subfolders.
        std::wstring dest_rel;
        if (starts_with(rel, L"Shaders/"))
            dest_rel = std::wstring(L"reshade-shaders\\Shaders\\") + rel.substr(wcslen(L"Shaders/"));
        else if (starts_with(rel, L"Textures/"))
            dest_rel = std::wstring(L"reshade-shaders\\Textures\\") + rel.substr(wcslen(L"Textures/"));
        else
            continue; // README, LICENSE, NOTICE, ...
        for (auto& c : dest_rel)
            if (c == L'/') c = L'\\';

        std::wstring dest = path_combine(out.staging_dir, dest_rel);
        if (!make_dirs(path_parent(dest))) {
            mz_zip_reader_end(&z);
            fail(L"Cannot create directory: " + path_parent(dest));
        }
        size_t size = 0;
        void* data = mz_zip_reader_extract_to_heap(&z, i, &size, 0);
        if (!data) continue;
        bool ok = write_file(dest, data, size);
        mz_free(data);
        if (!ok) {
            mz_zip_reader_end(&z);
            fail(L"Failed writing " + dest);
        }
        out.files.push_back(std::move(dest));
    }
    mz_zip_reader_end(&z);

    if (out.files.empty())
        fail(L"LumeniteFX zipball contained no shader files - repo layout may have changed.");
    log(fmt(L"  staged %u files (lumenite_*.fx, include\\*.fxh, Textures\\lumenite_bluenoise256.png)",
            (unsigned)out.files.size()));
    out.ok = true;
    return out;
}

ReshadeHeaders fetch_reshade_headers(const LogFn& log, const ProgressFn& progress) {
    ReshadeHeaders out;
    log(L"Fetching standard ReShade shader headers (ReShade.fxh, ReShadeUI.fxh, DrawText.fxh)...");

    // Resolve the default branch so renames don't break us.
    std::wstring branch = L"slim";
    HttpResponse meta = http_get(L"https://api.github.com/repos/crosire/reshade-shaders");
    if (meta.ok) {
        Json j;
        if (Json::parse(meta.body, j)) {
            std::wstring b = j.get_str(L"default_branch");
            if (!b.empty()) branch = b;
        }
    }

    std::wstring base = fmt(L"https://raw.githubusercontent.com/crosire/reshade-shaders/%s/Shaders/", branch.c_str());
    out.fxh_path = path_combine(fetch_temp_dir(), L"ReShade.fxh");
    out.ui_fxh_path = path_combine(fetch_temp_dir(), L"ReShadeUI.fxh");
    out.drawtext_path = path_combine(fetch_temp_dir(), L"DrawText.fxh");
    download(base + L"ReShade.fxh", out.fxh_path, progress);
    download(base + L"ReShadeUI.fxh", out.ui_fxh_path, progress);
    download(base + L"DrawText.fxh", out.drawtext_path, progress);

    if (file_size(out.fxh_path) < 1000 || file_size(out.ui_fxh_path) < 1000)
        fail(L"Downloaded ReShade headers look invalid (branch '" + branch + L"').");
    out.ok = true;
    return out;
}

} // namespace fk
