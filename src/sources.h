// FeedKit - sources.h
// Discovery + download of everything FeedKit installs. Nothing is bundled:
// every install pulls the current files from their upstream sources.
#pragma once

#include <string>
#include <vector>
#include <functional>

namespace fk {

using LogFn = std::function<void(const std::wstring&)>;
using ProgressFn = std::function<void(uint64_t done, uint64_t total)>; // total 0 = unknown

struct DownloadedFile {
    std::wstring name;        // file name (in the temp dir)
    std::wstring local_path;  // absolute path in temp
    uint64_t size = 0;
};

struct FeederBundle {
    std::wstring release_tag;
    DownloadedFile addon64;
    DownloadedFile addon32;
    DownloadedFile fx_shader;
    DownloadedFile host64_exe;
    DownloadedFile vk_layer_zip; // may be missing (empty path) in older releases
    bool ok = false;
};

struct RenoDxBundle {
    std::wstring version;
    std::wstring addon64_path;  // extracted .addon64 in temp
    bool ok = false;
};

struct NgxBundle {
    std::wstring nr_version;
    std::wstring nr_dll_path;
    std::wstring sr_version;
    std::wstring sr_dll_path;
    bool ok = false;
};

struct ReshadeBundle {
    std::wstring version;
    std::wstring setup_exe_path;
    bool ok = false;
};

// LumeniteFX motion-vector provider shaders, staged with their final
// reshade-shaders\ relative layout so they can be copied into a game folder.
struct LumeniteBundle {
    std::wstring branch;      // default branch the zipball came from
    std::wstring staging_dir; // contains reshade-shaders\Shaders\... and ...\Textures\...
    std::vector<std::wstring> files; // staged files, absolute paths
    bool ok = false;
};

// Standard ReShade headers (ReShade.fxh, ReShadeUI.fxh, DrawText.fxh). Setup's
// headless mode skips the standard effects package that normally provides them,
// so FeedKit fetches them from crosire/reshade-shaders itself.
struct ReshadeHeaders {
    std::wstring fxh_path;
    std::wstring ui_fxh_path;
    std::wstring drawtext_path;
    bool ok = false;
};

// Temporary working directory for this session's downloads.
std::wstring fetch_temp_dir();

// GitHub latest release of DLSS5-Feeder: all payloads via stable
// releases/latest/download/<name> URLs so every install gets the newest build.
FeederBundle fetch_feeder(const LogFn& log, const ProgressFn& progress);

// Newest renodx-dlss5 release zip from RankFTW/rhi-repo (found via GitHub API,
// since RHI ships the add-on there), .addon64 extracted.
RenoDxBundle fetch_renodx_dlss5(const LogFn& log, const ProgressFn& progress);

// Newest DLSS NR (ShortFuse build preferred, matching RHI's default) and DLSS SR
// DLLs from the dlss_manifest.json RHI publishes, DLLs extracted from zips.
NgxBundle fetch_ngx_dlls(const LogFn& log, const ProgressFn& progress);

// Latest ReShade Addon setup from reshade.me (version scraped from the site).
ReshadeBundle fetch_reshade(const LogFn& log, const ProgressFn& progress);

// LumeniteFX (github.com/umar-afzaal/LumeniteFX) repo zipball of the default
// branch, extracted into a staging dir mirroring reshade-shaders\ layout:
//   Shaders/**  -> staging\reshade-shaders\Shaders\**
//   Textures/** -> staging\reshade-shaders\Textures\**
LumeniteBundle fetch_lumenite(const LogFn& log, const ProgressFn& progress);

// Standard ReShade shader headers from crosire/reshade-shaders (default branch).
ReshadeHeaders fetch_reshade_headers(const LogFn& log, const ProgressFn& progress);

// Extract files from a zip whose name matches any of `patterns` (simple wildcard
// with *). Returns absolute paths of extracted files.
std::vector<std::wstring> zip_extract_matching(const std::wstring& zip_path,
                                               const std::wstring& dest_dir,
                                               const std::vector<std::wstring>& patterns);

} // namespace fk
