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

// Set the browser environment visible to scripts: current URL, document title
// and viewport size (drives window.location / window.innerWidth / navigator).
void mini_js_set_env(const char* url, const char* title, int w, int h);
void mini_js_set_dom(const char* html);   // seed the DOM shim with the parsed tree

// Drop the JS runtime entirely (fresh globals for a new page). Safe to call
// from the main thread between page loads; the next mini_js_run rebuilds it.
void mini_js_reset(void);

} // namespace nefu
