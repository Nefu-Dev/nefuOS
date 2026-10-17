// stub.cpp - temporary link-time stubs for collaborator WIP interfaces.
// These land in core/lib and are NOT part of the final design: each stub is
// replaced when the collaborator's real implementation lands. Kept out of
// files the collaborator is actively editing to avoid merge conflicts.
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "klib/klib.h"

namespace nefu {

} // namespace nefu

// platform_http_request - declared in core/platform.h (collaborator), host
// impl not landed yet. Return false so npkg/update fall back to
// platform_http_get (which win32 implements).
bool platform_http_request(const char* method, const char* url,
                           const char* headers,
                           const uint8_t* body, uint32_t body_len,
                           int* out_status, uint8_t** out_body, uint32_t* out_body_len) {
    (void)method; (void)url; (void)headers; (void)body; (void)body_len;
    if (out_status) *out_status = 0;
    if (out_body) *out_body = 0;
    if (out_body_len) *out_body_len = 0;
    return false;
}

// platform_http_stream_request - same WIP story; disable streaming requests.
bool platform_http_stream_request(const char* method, const char* url,
                                  const char* headers, void* ctx,
                                  bool (*cb)(void* ctx, const uint8_t* data, uint32_t len),
                                  int* out_status) {
    (void)method; (void)url; (void)headers; (void)ctx; (void)cb;
    if (out_status) *out_status = 0;
    return false;
}

