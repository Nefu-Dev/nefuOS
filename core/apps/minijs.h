/*
 * nefuOS Copyright (C) 2026 nefu-dev
 * Licensed under GNU GPLv2 <https://www.gnu.org/licenses/gpl-2.0.html>
 * See COPYING for full license text.
 */
// nefuOS mini JS engine - a small embeddable JavaScript-like interpreter.
// Implements a pragmatic subset (numbers are integers, strings, var/let,
// expressions, if/else, while, for, built-in print/alert/document.write/len).
// Used by the browser for <script> blocks and by the terminal `js` command.
// Reference note: parser structure inspired by the classic "Let's Build a
// Simple Interpreter" (Ruslan Spivak, MIT) and tiny-js (MIT); this is a fresh
// minimal implementation for the nefuOS kernel (no heap beyond fixed buffers).
#pragma once

namespace nefu {

// Run a small JS-like script. Output of print/alert/document.write is appended
// to out (up to out_sz-1 bytes). Returns 0 on success, non-zero on error.
int mini_js_run(const char* script, char* out, int out_sz,
                   char* html, int html_sz);

// Bound the wall-clock of the NEXT mini_js_run call to `ms` milliseconds
// (one shot; cleared after use). 0 restores the default budget.
void mini_js_set_run_deadline(unsigned int ms);

// Ask the running script to give up the eval slot within `after_ms`. Used when
// a user gesture is parked behind a long page script: the gesture must not
// starve, but a normal bootstrap must not be cut either. The deadline is
// cleared as soon as it fires or when mini_js_clear_preempt() is called.
void mini_js_request_preempt(unsigned int after_ms);
void mini_js_clear_preempt(void);

// Set the browser environment visible to scripts: current URL, document title
// and viewport size (drives window.location / window.innerWidth / navigator).
void mini_js_set_env(const char* url, const char* title, int w, int h);

// Absolute URL of the script about to run: the base for ES dynamic import()
// chunk resolution (bloxd.io loads webpack chunks this way).
void mini_js_set_module_base(const char* url);
void mini_js_set_dom(const char* html);   // seed the DOM shim with the parsed tree

// Drop the JS runtime entirely (fresh globals for a new page). Safe to call
// from the main thread between page loads; the next mini_js_run rebuilds it.
void mini_js_reset(void);

// True while a runtime exists (i.e. the page booted a script).
bool mini_js_alive(void);

// Per-frame pump: runs queued requestAnimationFrame callbacks, due timers and
// armed intervals, drains promise microtasks, and re-serializes the DOM shim
// into `html` when a listener mutated the tree. GL / DOM commands produced by
// the callbacks are appended to `out`. Returns the number of callbacks run.
// Must be called from the thread that owns the runtime (the resident worker).
int mini_js_tick(char* out, int out_sz, char* html, int html_sz);

// Deliver one DOM event to the page's listeners (bubbling element -> document
// -> window). `idx` is the data-nfu index of the hit-tested element, or -1 for
// document/window scope only. `key` is a keysym/character string, `mods` the
// modifier bitmask (bit0 shift, bit1 ctrl, bit2 alt, bit3 meta).
int mini_js_dispatch(const char* type, int idx, int x, int y,
                     const char* key, int mods);

} // namespace nefu

