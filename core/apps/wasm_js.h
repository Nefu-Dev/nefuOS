/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv2 <https://www.gnu.org/licenses/gpl-2.0.html>
 * See COPYING for full license text.
 */
// WebAssembly object for the browser JS engine.
//
// bloxd.io refuses to boot without one: chunk 73's doInitialSetup calls
// `provideWasmModule(module)` and bails out with "Bloxd couldn't start in
// this browser" when the module is null. The engine it wants is the Rust
// physics build served as bloxd_wasm_bg.<hash>.wasm, and its import list is
// six wasm-bindgen glue functions under the `ogZX` namespace, all i32/f64.
//
// wasm3 (MIT, vendored in third_party/wasm3) interprets the module; this file
// exposes WebAssembly.Module / Instance / instantiate / instantiateStreaming /
// Memory / RuntimeError over that interpreter and forwards the six imports
// back into JavaScript so the glue keeps working unchanged.
#ifndef NEFU_WASM_JS_H
#define NEFU_WASM_JS_H

#include "quickjs/quickjs.h"

namespace nefu {

// Install `WebAssembly` on the global object. Idempotent; returns false when
// the interpreter could not be brought up (the caller logs and continues).
bool wasm_js_install(JSContext* ctx, JSValue global);

// Release per-context interpreter state. Safe to call with no install.
void wasm_js_shutdown(JSContext* ctx);

}  // namespace nefu

#endif
