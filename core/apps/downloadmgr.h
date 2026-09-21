// nefuOS download manager - public API
// SPDX-License-Identifier: MIT

#pragma once

namespace nefu {

// Download status enum
enum DownloadStatus {
    DL_PENDING = 0,
    DL_ACTIVE,
    DL_COMPLETE,
    DL_FAILED,
    DL_PAUSED
};

// Download item structure
struct DownloadItem {
    const char* url;
    const char* filename;
    const char* local_path;
    unsigned int total_bytes;
    unsigned int received_bytes;
    int status;
};

// Download manager functions
int download_count();
DownloadItem* download_get(int idx);
DownloadItem* download_add(const char* url, const char* filename = 0);
void download_start(DownloadItem* item);
void download_cancel(DownloadItem* item);
void download_clear_completed();

// Download list window
void download_list_launch();

} // namespace nefu
