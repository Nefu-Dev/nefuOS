// modnefu_http.c — nefu_http MicroPython module for the nefu port.
// Bridges HTTPS requests to the host TLS13 stack (win32.cpp
// nefu_http_request, C linkage). Returns (status, headers, body_bytes);
// status < 0 means transport failure.
//
// API:
//   nefu_http.request(method, url, body="", headers="", content_type="application/json")
//                        -> (status, headers, body)
//   nefu_http.post(url, body, headers="")  -> (status, headers, body)
//   nefu_http.get(url, headers="")         -> (status, headers, body)

#include <stdlib.h>
#include <string.h>
#include "py/runtime.h"
#include "py/objstr.h"
#include "py/builtin.h"

extern int nefu_http_request(const char* method, const char* url,
                             const char* body, const char* content_type,
                             const char* extra_headers,
                             int* out_status, char** out_headers, uint32_t* out_headers_len,
                             char** out_body, uint32_t* out_body_len);

static mp_obj_t nefu_http_request_wrap(size_t n_args, const mp_obj_t* args) {
    const char* method = mp_obj_str_get_str(args[0]);
    const char* url = mp_obj_str_get_str(args[1]);
    const char* body = "";
    const char* headers = "";
    const char* ctype = 0; // C side defaults to application/json
    if (n_args > 2 && args[2] != mp_const_none) body = mp_obj_str_get_str(args[2]);
    if (n_args > 3 && args[3] != mp_const_none) headers = mp_obj_str_get_str(args[3]);
    if (n_args > 4 && args[4] != mp_const_none) ctype = mp_obj_str_get_str(args[4]);

    int status = 0;
    char* out_h = 0; uint32_t out_hl = 0;
    char* out_b = 0; uint32_t out_bl = 0;
    int rc = nefu_http_request(method, url, body, ctype, headers,
                               &status, &out_h, &out_hl, &out_b, &out_bl);
    mp_obj_t items[3];
    if (rc != 0) {
        items[0] = mp_obj_new_int(-1);
        items[1] = mp_const_empty_bytes;
        items[2] = mp_const_empty_bytes;
    } else {
        items[0] = mp_obj_new_int(status);
        items[1] = out_h ? mp_obj_new_bytes((const byte*)out_h, out_hl) : mp_const_empty_bytes;
        items[2] = out_b ? mp_obj_new_bytes((const byte*)out_b, out_bl) : mp_const_empty_bytes;
    }
    if (out_h) free(out_h);
    if (out_b) free(out_b);
    return mp_obj_new_tuple(3, items);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(nefu_http_request_obj, 2, 5, nefu_http_request_wrap);

static mp_obj_t nefu_http_post_wrap(size_t n_args, const mp_obj_t* args) {
    mp_obj_t headers = (n_args > 2 && args[2] != mp_const_none) ? args[2] : mp_const_none;
    mp_obj_t all[5] = { mp_obj_new_str("POST", 4), args[0], args[1], headers, mp_const_none };
    return nefu_http_request_wrap(5, all);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(nefu_http_post_obj, 2, 3, nefu_http_post_wrap);

static mp_obj_t nefu_http_get_wrap(size_t n_args, const mp_obj_t* args) {
    mp_obj_t headers = (n_args > 1 && args[1] != mp_const_none) ? args[1] : mp_const_none;
    mp_obj_t all[5] = { mp_obj_new_str("GET", 3), args[0], mp_const_none, headers, mp_const_none };
    return nefu_http_request_wrap(5, all);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(nefu_http_get_obj, 1, 2, nefu_http_get_wrap);

static const mp_rom_map_elem_t nefu_http_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR_request), MP_ROM_PTR(&nefu_http_request_obj) },
    { MP_ROM_QSTR(MP_QSTR_post), MP_ROM_PTR(&nefu_http_post_obj) },
    { MP_ROM_QSTR(MP_QSTR_get), MP_ROM_PTR(&nefu_http_get_obj) },
};
static MP_DEFINE_CONST_DICT(nefu_http_module_globals, nefu_http_module_globals_table);

const mp_obj_module_t nefu_http_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t*)&nefu_http_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_nefu_http, nefu_http_module);
