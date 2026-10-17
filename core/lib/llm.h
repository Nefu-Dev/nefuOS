// nefuLLM -- freestanding int8 GPT-Neo inference engine (Q16.16, no FPU/no libc)
// Loads a NEFL blob (see convert_model.py) and generates English text.
// Integer-only: activations Q16.16, weights int8 + per-row Q16.16 scale.
#pragma once
#include <stdint.h>

namespace nefu {
namespace llm {

struct Model;

// Parse a NEFL blob that is already resident in memory. Weights stay in the
// blob (no copy). Tokenizer tables + scale/bias fix arrays are built here.
// Returns 0 on success, non-zero on error. `workspace` (of `ws_size` bytes)
// is used for the tokenizer hash tables; it must stay alive while the model
// is in use.
int  llm_load(const uint8_t* blob, size_t blob_size, void* workspace,
              size_t ws_size, Model** out);

// Number of workspace bytes llm_load needs.
size_t llm_workspace_needed(const uint8_t* blob, size_t blob_size);

// Generate a reply to `prompt` (UTF-8). Writes NUL-terminated text to `out`
// (up to outcap bytes). `scratch` of `scratch_size` holds activations.
// Returns number of bytes written (>=0) or negative error.
int  llm_generate(Model* m, const char* prompt, char* out, int outcap,
                  void* scratch, size_t scratch_size, int max_new);

// Set generation knobs (Q16.16): temperature (default 0.80) and repetition
// penalty (default 1.30). Take effect on the next generation call.
void llm_set_sampling(Model* m, int32_t temp_q16, int32_t rep_q16);

// debug entry
int llm_debug_topk(Model* m, const char* prompt, uint32_t* topids, int32_t* topvals,
                   int k, void* scratch, size_t scratch_size);

size_t llm_scratch_needed(Model* m);

// ---------------------------------------------------------------------------
// Stepwise (non-blocking) generation. Generates ONE token per call so a UI can
// repaint between tokens instead of freezing for the whole reply. All mutable
// state lives in `gen_buf` (handle + token sequence); the activation buffers
// live in `scratch`. Both must stay alive until llm_gen_end.
struct Gen;

// Bytes needed for the Gen handle buffer (adds the token sequence).
size_t llm_gen_buf_needed(Model* m);

// Begin stepwise generation of `prompt`. Returns 0 on success and sets *out to
// the handle; caller must keep `scratch` and `gen_buf` alive while stepping.
int  llm_gen_begin(Model* m, const char* prompt,
                   void* scratch, size_t scratch_size,
                   void* gen_buf, size_t gen_size, Gen** out);

// Produce the next token. Writes its decoded text (NUL-terminated) to `tok`
// (up to tokcap bytes). Returns 1 = a token was produced (call again),
// 0 = generation done (EOS / token limit reached), <0 = error.
int  llm_gen_step(Gen* g, char* tok, int tokcap);

// Full generated text so far (prompt excluded), for incremental display.
int  llm_gen_text(Gen* g, char* out, int outcap);

// End the session (no-op; memory lives in gen_buf/scratch).
void llm_gen_end(Gen* g);

// Free the workspace (tokenizer tables) built by llm_load.
void llm_unload(Model* m);

} // namespace llm
} // namespace nefu
