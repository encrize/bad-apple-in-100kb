/* Verifies lzma_raw_decode against the reference: dumps the decoded stream. */
#include <unistd.h>
#include "video_data.h"
#include "lzma_dec.h"

#define OUT_SIZE ((unsigned long)VID_FRAMES * VID_FRAME_BYTES)
static unsigned char raw[OUT_SIZE];

int main(void)
{
	unsigned long off = 0;
	if (lzma_raw_decode(video_data, VID_DATA_SIZE, raw, OUT_SIZE))
		return 1;
	while (off < OUT_SIZE) {
		long n = write(1, raw + off, OUT_SIZE - off);
		if (n <= 0)
			return 2;
		off += (unsigned long)n;
	}
	return 0;
}
