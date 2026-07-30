/* Bad Apple!! in a tiny binary.
 *
 * No liblzma, no malloc, no printf. The decoded frame buffer lives in .bss
 * (costs nothing in the file), and each frame is pushed out with a single
 * write per frame.
 *
 * Windows: works on XP through Windows 11. ANSI escape sequences are used
 * when the console supports them (Windows 10 1511+), otherwise the code falls
 * back to the classic Console API, which is what Windows XP needs.
 *
 * Build NOLIBC=1 on x86-64 Linux to drop libc entirely (raw syscalls).
 */
#include "video_data.h"
#include "lzma_dec.h"

#define OUT_SIZE  ((unsigned long)VID_FRAMES * VID_FRAME_BYTES)
#define FRAME_US  (1000000L / VID_FPS)
#define CELLS     (VID_WIDTH * VID_HEIGHT)

/* composed output buffer: frame rows + newlines + status line */
static char obuf[(VID_WIDTH + 1) * VID_HEIGHT + 64];

/* unsigned -> decimal, returns digits written. Replaces printf("%d"). */
static unsigned put_u(char *d, unsigned v)
{
	char t[10];
	unsigned n = 0, i;
	do {
		t[n++] = (char)('0' + v % 10);
		v /= 10;
	} while (v);
	for (i = 0; i < n; i++)
		d[i] = t[n - 1 - i];
	return n;
}

static unsigned put_s(char *d, const char *s)
{
	unsigned n = 0;
	while (s[n]) {
		d[n] = s[n];
		n++;
	}
	return n;
}

/* ------------------------------------------------------------------ */
/* platform layer                                                      */
/* ------------------------------------------------------------------ */

#if defined(_WIN32)

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501     /* Windows XP */
#endif
#include <windows.h>

/* Missing from pre-Windows-10 SDK headers */
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif

static HANDLE g_out;
static int g_vt;                /* 1 = console understands ANSI escapes */

typedef MMRESULT (WINAPI *TIMEPERIODFN)(UINT);
static TIMEPERIODFN g_tbp, g_tep;

static void plat_write(const char *p, unsigned n)
{
	DWORD w;
	WriteFile(g_out, p, n, &w, NULL);
}

static void plat_init(void)
{
	CONSOLE_SCREEN_BUFFER_INFO csbi;
	CONSOLE_CURSOR_INFO ci;
	COORD size;
	SMALL_RECT rect;
	DWORD mode = 0;
	HMODULE winmm;

	g_out = GetStdHandle(STD_OUTPUT_HANDLE);

	/* Shrink the WINDOW first, then the BUFFER. The buffer may never be
	 * smaller than the window, so the original order failed silently. */
	rect.Left = 0;
	rect.Top = 0;
	rect.Right = 0;
	rect.Bottom = 0;
	SetConsoleWindowInfo(g_out, TRUE, &rect);

	size.X = VID_WIDTH;
	size.Y = VID_HEIGHT + 1;    /* frame + status line, no scrollback */
	SetConsoleScreenBufferSize(g_out, size);

	rect.Right = (SHORT)(size.X - 1);
	rect.Bottom = (SHORT)(size.Y - 1);
	SetConsoleWindowInfo(g_out, TRUE, &rect);

	/* Probe for ANSI support. Fails on XP/Vista/7/8 -> use Console API. */
	g_vt = 0;
	if (GetConsoleMode(g_out, &mode)) {
		if (SetConsoleMode(g_out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING))
			g_vt = 1;
	} else {
		g_vt = 1;   /* redirected to a file/pipe: emit escapes as-is */
	}

	ci.dwSize = 25;
	ci.bVisible = FALSE;
	SetConsoleCursorInfo(g_out, &ci);

	if (GetConsoleScreenBufferInfo(g_out, &csbi)) {
		DWORD written;
		COORD home;
		home.X = 0;
		home.Y = 0;
		FillConsoleOutputCharacterA(g_out, ' ',
		                            csbi.dwSize.X * csbi.dwSize.Y,
		                            home, &written);
	}

	/* Sleep() granularity is ~15.6 ms by default, which is visible at 20 fps.
	 * Load winmm dynamically so the import table stays at kernel32 only. */
	winmm = LoadLibraryA("winmm.dll");
	if (winmm) {
		g_tbp = (TIMEPERIODFN)GetProcAddress(winmm, "timeBeginPeriod");
		g_tep = (TIMEPERIODFN)GetProcAddress(winmm, "timeEndPeriod");
		if (g_tbp)
			g_tbp(1);
	}
}

static void plat_present(const char *cells, const char *status, unsigned slen)
{
	unsigned pos = 0, x, y, i;

	if (g_vt) {
		pos += put_s(obuf + pos, "\033[H");
		for (y = 0; y < VID_HEIGHT; y++) {
			for (x = 0; x < VID_WIDTH; x++)
				obuf[pos++] = cells[y * VID_WIDTH + x];
			obuf[pos++] = '\n';
		}
		pos += put_s(obuf + pos, "\033[90m");
		for (i = 0; i < slen; i++)
			obuf[pos++] = status[i];
		pos += put_s(obuf + pos, "\033[0m");
		plat_write(obuf, pos);
		return;
	}

	/* Windows XP path: write straight into the screen buffer. This never
	 * moves the cursor and never scrolls, so there are no newlines and no
	 * flicker. Buffer width == VID_WIDTH, so rows wrap on their own. */
	{
		DWORD written;
		COORD at;
		at.X = 0;
		at.Y = 0;
		WriteConsoleOutputCharacterA(g_out, cells, CELLS, at, &written);
		at.Y = VID_HEIGHT;
		for (i = 0; i < slen; i++)
			obuf[i] = status[i];
		while (i < VID_WIDTH)
			obuf[i++] = ' ';
		WriteConsoleOutputCharacterA(g_out, obuf, i, at, &written);
	}
}

static void plat_shutdown(void)
{
	CONSOLE_CURSOR_INFO ci;
	ci.dwSize = 25;
	ci.bVisible = TRUE;
	SetConsoleCursorInfo(g_out, &ci);
	if (g_tep)
		g_tep(1);
}

/* GetTickCount instead of QueryPerformanceCounter: it needs no 64-bit
 * division (which would drag __udivdi3 out of libgcc in the freestanding
 * build), it lives in kernel32, and timeBeginPeriod(1) already brought its
 * resolution down to ~1 ms. The whole video is ~165 s, so 32-bit ms cannot
 * overflow mid-playback. */
static DWORD g_t0;

static void clock_start(void)
{
	g_t0 = GetTickCount();
}

static long clock_us(void)
{
	return (long)(GetTickCount() - g_t0) * 1000L;
}

static void sleep_us(long us)
{
	Sleep((DWORD)((us + 500) / 1000));
}

static void plat_exit(int c)
{
	ExitProcess((UINT)c);
}

#else   /* ------------------------------ POSIX ------------------------- */

#if defined(NOLIBC)

struct ts {
	long sec;
	long nsec;
};

static long sys3(long n, long a, long b, long c)
{
	long r;
	__asm__ volatile("syscall"
	                 : "=a"(r)
	                 : "a"(n), "D"(a), "S"(b), "d"(c)
	                 : "rcx", "r11", "memory");
	return r;
}

static void plat_write(const char *p, unsigned n)
{
	while (n) {
		long r = sys3(1, 1, (long)p, (long)n);
		if (r <= 0)
			break;
		p += r;
		n -= (unsigned)r;
	}
}

static struct ts g_t0;

static void clock_start(void)
{
	sys3(228, 1 /*CLOCK_MONOTONIC*/, (long)&g_t0, 0);
}

static long clock_us(void)
{
	struct ts t;
	sys3(228, 1, (long)&t, 0);
	return (t.sec - g_t0.sec) * 1000000L + (t.nsec - g_t0.nsec) / 1000L;
}

static void sleep_us(long us)
{
	struct ts t;
	t.sec = us / 1000000L;
	t.nsec = (us % 1000000L) * 1000L;
	sys3(35, (long)&t, 0, 0);
}

static void plat_exit(int c)
{
	sys3(231, c, 0, 0);
	__builtin_unreachable();
}

#else

#include <time.h>
#include <unistd.h>

static void plat_write(const char *p, unsigned n)
{
	while (n) {
		long r = (long)write(1, p, n);
		if (r <= 0)
			break;
		p += r;
		n -= (unsigned)r;
	}
}

static struct timespec g_t0;

static void clock_start(void)
{
	clock_gettime(CLOCK_MONOTONIC, &g_t0);
}

static long clock_us(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (t.tv_sec - g_t0.tv_sec) * 1000000L +
	       (t.tv_nsec - g_t0.tv_nsec) / 1000L;
}

static void sleep_us(long us)
{
	struct timespec t;
	t.tv_sec = us / 1000000L;
	t.tv_nsec = (us % 1000000L) * 1000L;
	nanosleep(&t, NULL);
}

static void plat_exit(int c)
{
	_exit(c);
}

#endif  /* NOLIBC */

static void plat_init(void)
{
	plat_write("\033[?25l\033[2J", 10);
}

static void plat_present(const char *cells, const char *status, unsigned slen)
{
	unsigned pos = 0, x, y, i;

	pos += put_s(obuf + pos, "\033[H");
	for (y = 0; y < VID_HEIGHT; y++) {
		for (x = 0; x < VID_WIDTH; x++)
			obuf[pos++] = cells[y * VID_WIDTH + x];
		obuf[pos++] = '\n';
	}
	pos += put_s(obuf + pos, "\033[90m");
	for (i = 0; i < slen; i++)
		obuf[pos++] = status[i];
	pos += put_s(obuf + pos, "\033[0m");
	plat_write(obuf, pos);
}

static void plat_shutdown(void)
{
	plat_write("\033[?25h\n", 7);
}

#endif  /* _WIN32 */

/* ------------------------------------------------------------------ */

/* The 946 KB decode buffer.
 *
 * On ELF a plain .bss array costs zero file bytes. On PE it is not that
 * simple: with -fdata-sections gcc emits it as an orphan `.bss.raw` section,
 * and the PE linker does not recognise orphans as uninitialised, so it writes
 * out 946 KB of zeros into the exe. That is exactly how a 125 KB binary
 * turned into 1.02 MB on Windows.
 *
 * VirtualAlloc sidesteps the whole question: the buffer does not exist in the
 * image at all, and VirtualAlloc lives in kernel32, so the freestanding build
 * stays CRT-free. Pages come back zeroed.
 */
#if defined(_WIN32)
static unsigned char *raw;

static int alloc_raw(void)
{
	raw = (unsigned char *)VirtualAlloc(NULL, OUT_SIZE,
	                                    MEM_COMMIT | MEM_RESERVE,
	                                    PAGE_READWRITE);
	return raw != NULL;
}
#else
static unsigned char raw[OUT_SIZE];

static int alloc_raw(void)
{
	return 1;
}
#endif

static char cells[CELLS];
static char status[64];

int main(void)
{
	unsigned f;

	plat_init();

	if (!alloc_raw()) {
		plat_write("out of memory\n", 14);
		plat_exit(1);
	}

	if (lzma_raw_decode(video_data, VID_DATA_SIZE, raw, OUT_SIZE)) {
		plat_write("decode failed\n", 14);
		plat_exit(1);
	}

	clock_start();

	for (f = 0; f < VID_FRAMES; f++) {
		const unsigned char *fr = raw + (unsigned long)f * VID_FRAME_BYTES;
		unsigned i, slen;
		long target, elapsed;

		for (i = 0; i < CELLS; i++)
			cells[i] = ((fr[i >> 3] >> (i & 7)) & 1) ? '#' : ' ';

		slen = put_s(status, " frame ");
		slen += put_u(status + slen, f + 1);
		slen += put_s(status + slen, " / ");
		slen += put_u(status + slen, VID_FRAMES);
		slen += put_s(status + slen, " ");

		plat_present(cells, status, slen);

		target = (long)(f + 1) * FRAME_US;
		elapsed = clock_us();
		if (target > elapsed)
			sleep_us(target - elapsed);
	}

	plat_shutdown();
	return 0;
}

#if defined(NOLIBC) && !defined(_WIN32)
void _start(void)
{
	plat_exit(main());
}
#endif

#if defined(NOLIBC) && defined(_WIN32)
/* Freestanding Windows build: no CRT at all, only kernel32.dll.
 *
 * This is what makes the exe run on Windows XP no matter which MinGW you
 * have. A UCRT-based toolchain would otherwise emit an import for
 * api-ms-win-crt-*.dll, which simply does not exist before Windows 10. */

void __main(void) {}       /* gcc emits a call to this at the top of main() */

void *memset(void *d, int c, size_t n)
{
	unsigned char *p = (unsigned char *)d;
	while (n--)
		*p++ = (unsigned char)c;
	return d;
}

void *memcpy(void *d, const void *s, size_t n)
{
	unsigned char *a = (unsigned char *)d;
	const unsigned char *b = (const unsigned char *)s;
	while (n--)
		*a++ = *b++;
	return d;
}

/* Default PE entry point for a console app; no -e flag needed. */
void mainCRTStartup(void)
{
	plat_exit(main());
}
#endif
