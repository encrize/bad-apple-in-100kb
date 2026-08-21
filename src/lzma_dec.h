#pragma once

int lzma_raw_decode(const unsigned char *in, unsigned long in_size,
                    unsigned char *out, unsigned long out_size);
