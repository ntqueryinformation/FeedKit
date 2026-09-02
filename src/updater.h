// FeedKit - updater.h
// Built-in updater: checks github releases and self-swaps the running exe.
#pragma once

#include <functional>
#include <string>

namespace fk {

// Update states reported back to the UI.
enum class UpdateState {
    Checking = 0,   // query in flight
    UpToDate = 1,   // local >= latest
    Available = 2,  // newer release on GitHub (version = remote version)
    Failed = 3,     // check failed (version = error text)
    Downloading = 4,
};

// Queries the latest GitHub release and reports the resulting state.
// Network errors are reported as Failed with the error text.
void check_latest_version(
    const std::wstring& local_version,
    const std::function<void(UpdateState state, const std::wstring& version_or_error)>& report);

// Downloads the latest release, verifies its SHA-256 against the release notes,
// and swaps it in place of the running executable. Calls done() when finished:
//   ok == true  -> msg describes the installed update, restart_requested tells
//                  whether a restart is needed for it to take effect.
//   ok == false -> msg is the error text.
// The restart callback should start the new exe and terminate the app; it is
// only invoked when the caller passes restart_now = true from done().
void perform_update(
    const std::wstring& local_version,
    const std::function<void(const std::wstring& log)>& log,
    const std::function<void(bool ok, const std::wstring& msg, bool restart_requested)>& done,
    const std::function<bool(const std::wstring& question)>& ask);

} // namespace fk
