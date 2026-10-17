// =============================================================================
//  update.h — nefuOS OTA update (update command + progress UI)
// -----------------------------------------------------------------------------
//  update                         check for an update and report
//  update check                   same as above (alias)
//  update apply [manifest_url]    download iso + llm model, verify SHA-256,
//                                 back up the current version to
//                                 /user/updates/, then install (non-blocking;
//                                 shows a GUI progress window)
//  update force  [manifest_url]   apply even when already at the latest version
//  update status                  show current/last-known version + artifacts
//  update cancel                  abort a running download
//
//  Manifest format (https://nefuos.site.je/update.json):
//    { latest_version, min_supported_version, release_date, channel,
//      iso_url, iso_size_mb, iso_sha256,
//      llm_model_url, llm_model_size_mb, llm_model_sha256,
//      update_notes[], force_update }
//
//  Download guarantees:
//    * HTTP 302 redirects are followed (Range header preserved across hops).
//    * Bytes stream straight into the VFS (geometric buffer growth), never
//      buffered whole in RAM.
//    * SHA-256 of the COMPLETE file is verified against the manifest before
//      install; a mismatch deletes the bad file and aborts the update.
//    * Resume: an interrupted download keeps its partial bytes and the next
//      attempt issues `Range: bytes=<offset>-` instead of restarting.
//    * Progress is reported through a volatile struct painted by a non-blocking
//      GUI window (the download runs on a worker thread).
// =============================================================================
#pragma once

namespace nefu {

struct TermState;

// terminal command entry ("update ...")
void update_cmd(TermState* t, int argc, const char** argv);

// global shortcut entry (Ctrl+Shift+U): starts a fast check-and-apply update
// with the progress GUI; no terminal is required.
void update_global_shortcut();

// headless full-pipeline runner for the host verification hook.
// Runs synchronously in the calling thread, logs via `print(ud, s)`.
// Returns 0 on success, non-zero on failure.
struct UpdateOut {
    void (*print)(void* ud, const char* s);
    void* ud;
};
int update_run_pipeline(const UpdateOut& out, const char* manifest_url,
                        bool force);

} // namespace nefu
