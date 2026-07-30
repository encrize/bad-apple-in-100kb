# Bad Apple!! in a tiny binary

A 64x36 1-bit render of Bad Apple!! embedded in a single executable.
This is the size-optimised rework: **no liblzma, no malloc, no printf.**

## Results (measured, x86-64 Linux, gcc -Os, stripped)

| variant | payload | binary |
|---|---:|---:|
| original (liblzma, .xz payload, dynamic link) | 116 172 | 129 840 |
| **new, 20 fps, libc** | 113 977 | **125 592** |
| **new, 20 fps, `make tiny` (no libc)** | 113 977 | **116 976** |
| new, 15 fps, libc | 90 149 | 101 256 |
| **new, 15 fps, `make tiny`** | 90 149 | **93 144** |

The original README linked liblzma **statically**, which pulls in the whole
encoder, every filter (delta, BCJ x86/ARM/SPARC/...), the CRC tables and
`lzma_index` — that build was ~160 KB. The numbers above use a *dynamic*
liblzma link as the fairest available baseline, and the rework still wins.

In the `tiny` build the executable is only ~3 KB larger than the compressed
video itself. There is essentially nothing left to remove but the video.

## What changed

### 1. Raw LZMA1 instead of an .xz container — lossless, -2 195 B

The payload dropped the xz header, CRC64, index and footer, and switched to
`lc=1 lp=0 pb=0`. Packed 1bpp frames have no 4-byte structure, so the default
`pb=2` only wastes context bits.

```python
filters = [{'id': lzma.FILTER_LZMA1, 'preset': 9 | lzma.PRESET_EXTREME,
            'lc': 1, 'lp': 0, 'pb': 0}]
blob = lzma.compress(stream, format=lzma.FORMAT_RAW, filters=filters)
```

### 2. Own LZMA decoder instead of liblzma — the big win

`src/lzma_dec.c` is a ~2 KB minimal LZMA1 decoder. lc/lp/pb are compile-time
constants, the dictionary is the output buffer itself, so there is no
allocator, no window wrap-around, no encoder, no filter chain.

It is verified byte-exact against the reference LZMA implementation:

```
gcc -O2 -std=gnu99 -Isrc -DVID_LZMA_LC=1 -DVID_LZMA_LP=0 -DVID_LZMA_PB=0 \
    -o test_decode tools/test_decode.c src/lzma_dec.c
./test_decode | md5sum      # matches lzma.decompress() exactly
```

### 3. No printf, no malloc

* the frame counter is formatted by a 10-line `put_u()` instead of dragging in
  the full printf machinery
* the 946 KB decode buffer is `static` (.bss), so it costs **zero** file bytes
  and removes `malloc`/`free`
* the frame body and the status line are composed into one buffer and flushed
  with a single `write()` per frame — one syscall instead of several

### 4. `make tiny`: no libc at all

Linux x86-64 only. `-nostdlib -nostartfiles` with a hand-written `_start` and
inline `syscall` for `write`, `clock_gettime`, `nanosleep` and `exit_group`.
Saves another ~8.6 KB.

## Build

```sh
make                 # standard build -> ./bad_apple
make tiny            # no-libc build  -> ./bad_apple_tiny   (Linux x86-64)
make upx             # optional UPX pass
make sizes           # print the size table
```

Regenerate the payload:

```sh
python3 tools/convert.py bad_apple.mp4 --fps 20      # from a video file
python3 tools/reencode.py old_video_data.h --fps 15  # from an old header
```

## Windows XP support

`src/main.c` builds one executable that runs on **Windows XP through
Windows 11**. The renderer is picked at runtime:

* `SetConsoleMode(h, ... | ENABLE_VIRTUAL_TERMINAL_PROCESSING)` is probed.
  It succeeds on Windows 10 1511+ and the fast ANSI path is used (one
  `WriteFile` per frame).
* On XP the call fails, so the classic Console API path runs instead:
  `WriteConsoleOutputCharacterA` writes straight into the screen buffer.
  It never moves the cursor and never scrolls, so no newlines are emitted
  and there is *less* flicker than on the ANSI path.
* `\033[?25l` is replaced by `SetConsoleCursorInfo`, `\033[2J` by
  `FillConsoleOutputCharacterA`.
* Console window/buffer resizing order was fixed: the window is shrunk
  *before* the buffer, because a buffer may never be smaller than its
  window. The original order failed silently.
* `timeBeginPeriod(1)` is loaded dynamically from `winmm.dll`, because XP's
  default `Sleep()` granularity of ~15.6 ms is very visible at 20 fps. The
  import table stays kernel32-only.

The LZMA decoder is portable C with no syscalls and no 64-bit assumptions,
so it builds unchanged for 32-bit Windows.

### UCRT vs msvcrt: why `make win` is freestanding

A UCRT-based MinGW (MSYS2 UCRT64, and the default on several distros) links
the CRT startup against the Universal CRT and the exe then dies on XP with:

```
This application has failed to start because
api-ms-win-crt-environment-l1-1-0.dll was not found.
```

Those `api-ms-win-crt-*.dll` forwarders do not exist before Windows 10.

Since this program already avoids `malloc`, `printf` and every other libc
facility, `make win` simply drops the CRT entirely: `-nostdlib
-nostartfiles`, a hand-written `mainCRTStartup`, local `memset`/`memcpy`, and
`-lkernel32` as the only import. The resulting exe has **one** DLL dependency,
`kernel32.dll`, which has existed since Windows NT 3.1, so the CRT flavour of
your toolchain stops mattering.

`QueryPerformanceCounter` was also swapped for `GetTickCount`: the former
needs a 64-bit division, which would pull `__udivdi3` out of libgcc. With
`timeBeginPeriod(1)` already active, `GetTickCount` gives ~1 ms resolution,
and a 165 s video cannot overflow a 32-bit millisecond counter.

```sh
make win     # -> bad_apple.exe  freestanding, kernel32 only (recommended)
make win-crt # -> bad_apple.exe  ordinary CRT build, needs msvcrt MinGW
make iso     # -> badapple.iso   pure Python, no mkisofs needed
```

Verify the imports before copying it over:

```sh
i686-w64-mingw32-objdump -p bad_apple.exe | grep 'DLL Name'
# DLL Name: KERNEL32.dll      <- this line and nothing else
```

`make tiny` is Linux-only and does not apply to Windows.

## Getting the exe into a VM

`tools/make_iso.py` writes a plain ISO-9660 image with no external tools:

```sh
python3 tools/make_iso.py -o badapple.iso -V BADAPPLE bad_apple.exe
```

Attach it to QEMU as a second CD drive:

```sh
-drive file=badapple.iso,media=cdrom,index=2
```

## Ideas that were measured and rejected

Every "smart" inter-frame scheme makes the file **bigger**. LZMA already has a
64 MB dictionary and simply matches entire previous frames; delta coding
destroys those long matches.

| scheme | payload | vs 113 977 |
|---|---:|---:|
| XOR delta between frames | 156 215 | +37% |
| bit run-length + LZMA | 126 907 | +11% |
| column-major transpose | 124 527 | +9% |
| dedup identical frames + index | 117 486 | +3% |
| row-diff (flags + payload) | 120 957 | +6% |
| frame-level RLE | 113 920 | -0.05% |
| bzip2 | 166 970 | +47% |

## Quality/size trade-offs

If you want to go below 90 KB, the only remaining lever is the video itself:

| resolution / fps | payload |
|---|---:|
| 64x36 @ 20 fps (default) | 113 977 |
| 64x36 @ 15 fps | 90 149 |
| 64x36 @ 12 fps | 75 376 |
| 48x27 @ 20 fps | 75 754 |
| 64x36 @ 10 fps | 65 150 |
| 40x22 @ 20 fps | 57 523 |
| 48x27 @ 12 fps | 50 915 |
| 32x18 @ 20 fps | 42 597 |
