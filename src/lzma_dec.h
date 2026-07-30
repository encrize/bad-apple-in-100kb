/* Minimal LZMA1 raw-stream decoder (no container, no end marker).
 * Replaces liblzma. Decoding only; lc/lp/pb are compile-time constants and the
 * dictionary is the output buffer itself, so there is no allocator, no window
 * wrap-around and no error-recovery code.
 */
#pragma once

/* Decodes exactly out_size bytes. Returns 0 on success, 1 on corrupt input. */
int lzma_raw_decode(const unsigned char *in, unsigned long in_size,
                    unsigned char *out, unsigned long out_size);
