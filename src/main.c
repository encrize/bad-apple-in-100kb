#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <lzma.h>

#ifdef _WIN32
#include <windows.h>
#define SLEEP_US(us) Sleep((DWORD)((us) / 1000))

static void platform_init(void) {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);

    COORD size = { 65, 37 };
    SetConsoleScreenBufferSize(hOut, size);

    SMALL_RECT rect = { 0, 0, size.X - 1, size.Y - 1 };
    SetConsoleWindowInfo(hOut, TRUE, &rect);

    DWORD mode = 0;
    GetConsoleMode(hOut, &mode);
    SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
}
#else
#define SLEEP_US(us) do { \
    struct timespec _ts = { (us)/1000000L, ((us)%1000000L)*1000L }; \
    nanosleep(&_ts, NULL); \
} while(0)
static void platform_init(void) {}
#endif

#include "video_data.h"

/*  Renderer  */
static void render(const unsigned char *frame) {
    static char buf[(VID_WIDTH + 1) * VID_HEIGHT + 8];
    int pos = 0;

    buf[pos++] = '\033';
    buf[pos++] = '[';
    buf[pos++] = 'H';

    for (int y = 0; y < VID_HEIGHT; y++) {
        for (int x = 0; x < VID_WIDTH; x++) {
            int idx = y * VID_WIDTH + x;
            buf[pos++] = ((frame[idx >> 3] >> (idx & 7)) & 1) ? '#' : ' ';
        }
        buf[pos++] = '\n';
    }

    fwrite(buf, 1, pos, stdout);
    fflush(stdout);
}

/*  Main  */
int main(void) {
    platform_init();

    size_t raw_size = (size_t)VID_FRAMES * VID_FRAME_BYTES;
    unsigned char *raw = (unsigned char *)malloc(raw_size);

    if (!raw) {
        fputs("Out of memory\n", stderr);
        return 1;
    }

    lzma_stream strm = LZMA_STREAM_INIT;
    lzma_ret ret = lzma_stream_decoder(&strm, UINT64_MAX, 0);
    if (ret != LZMA_OK) {
        fprintf(stderr, "lzma_stream_decoder init failed: %d\n", (int)ret);
        free(raw);
        return 1;
    }

    strm.next_in   = video_data;
    strm.avail_in  = VID_DATA_SIZE;
    strm.next_out  = raw;
    strm.avail_out = raw_size;

    ret = lzma_code(&strm, LZMA_FINISH);
    size_t out_pos = raw_size - strm.avail_out;
    lzma_end(&strm);

    if (ret != LZMA_STREAM_END || out_pos != raw_size) {
        fprintf(stderr,
                "Decompress failed: lzma error %d\n"
                "expected %zu bytes, got %zu\n",
                (int)ret, raw_size, out_pos);
        free(raw);
        return 1;
    }

    fputs("\033[?25l", stdout);      
    fputs("\033[2J\033[H", stdout);  /* clear screen */
    fflush(stdout);

    long frame_us = 1000000L / VID_FPS;

#ifdef _WIN32
    LARGE_INTEGER freq, t0, t1;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);
#else
    struct timespec ts0, ts1;
    clock_gettime(CLOCK_MONOTONIC, &ts0);
#endif

    for (int f = 0; f < VID_FRAMES; f++) {
        const unsigned char *frame = raw + (size_t)f * VID_FRAME_BYTES;

        render(frame);

        /* frame counter */
        printf("\033[%dH\033[90m frame %d / %d \033[0m",
               VID_HEIGHT + 1, f + 1, VID_FRAMES);
        fflush(stdout);

#ifdef _WIN32
        QueryPerformanceCounter(&t1);
        long long elapsed =
            (t1.QuadPart - t0.QuadPart) * 1000000LL / freq.QuadPart;
        long long target = (long long)(f + 1) * frame_us;
        if (target > elapsed)
            SLEEP_US((long)(target - elapsed));
#else
        clock_gettime(CLOCK_MONOTONIC, &ts1);
        long elapsed =
            (ts1.tv_sec  - ts0.tv_sec)  * 1000000L +
            (ts1.tv_nsec - ts0.tv_nsec) / 1000L;
        long target = (long)(f + 1) * frame_us;
        if (target > elapsed)
            SLEEP_US(target - elapsed);
#endif
    }

    fputs("\033[?25h\n", stdout); 
    printf("\033[1;32m Bad Apple!! - fin \033[0m\n");

    free(raw);
    return 0;
}