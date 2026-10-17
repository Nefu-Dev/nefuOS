// aes128.h - minimal AES-128 (CBC decrypt) for nefuOS (no STL).
// Used by npkg to pass the InfinityFree JS anti-bot cookie challenge.
#pragma once
#include <stdint.h>

namespace nefu {

// AES-128-CBC decrypt. len must be a multiple of 16.
// out may equal in. Padding is NOT stripped (caller handles the 16-byte token).
void aes128_cbc_decrypt(const uint8_t key[16], const uint8_t iv[16],
                        const uint8_t* in, uint32_t len, uint8_t* out);

} // namespace nefu
