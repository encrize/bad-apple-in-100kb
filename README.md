# bad-apple-tiny

Bad Apple!! playing as ASCII in a terminal, from a single self-contained
binary. The video is baked into the executable. No assets, no runtime
dependencies, no network.

**96 KB on Windows, 93 KB on Linux** - and it runs on everything from
Windows XP to Windows 11.

```
$ ls -l bad_apple_tiny
-rwxr-xr-x 1 user user 93208 bad_apple_tiny
```

## Build

```sh
make              # Linux, dynamic libc          -> bad_apple      101 KB
make tiny         # Linux, no libc, raw syscalls -> bad_apple_tiny  93 KB
make win          # Windows XP..11, 32-bit       -> bad_apple.exe   ~96 KB
make iso          # wrap the exe into a CD image -> badapple.iso
```

Windows builds need a 32-bit MinGW (`i686-w64-mingw32-gcc`). Nothing else is
required anywhere — no liblzma, no Python at build time, no `mkisofs`.

## How it gets that small

| | payload | binary |
|---|---:|---:|
| naive approach (liblzma, `.xz` payload) | 116 172 | 129 840 |
| **this project** | **90 149** | **93 208** |

Four things do the work:

**A hand-written LZMA1 decoder (~2 KB).** Linking liblzma drags in the entire
encoder, every filter (delta, BCJ x86/ARM/SPARC/...), CRC tables and
`lzma_index`. Only the decoder is needed, so `src/lzma_dec.c` implements just
that, from the LZMA specification.

**A raw LZMA1 stream.** No `.xz` container means no header, no CRC64, no index
and no footer. `lc`/`lp`/`pb` are compile-time constants passed via `-D`, so
the decoder never reads them from the file.

**Encoder parameters chosen by measurement.** A sweep of all 75 valid
`lc`/`lp`/`pb` combinations picked `lc=1 lp=0 pb=0`, worth ~2 KB over the
defaults. Frames are 1 bit per pixel, 288 bytes each.

**No libc.** No `malloc` (the decode buffer is `.bss`, or `VirtualAlloc` on
Windows), no `printf` (integers are formatted by hand), one write syscall per
frame. `make tiny` drops libc entirely and talks to the kernel directly.

## Things that sounded clever and made it bigger

Every inter-frame trick was measured against plain LZMA. All of them lost —
LZMA already finds those redundancies with its match finder, and the transforms
just destroy literal context:

| transform | payload | vs baseline |
|---|---:|---:|
| **none** | **90 149** | — |
| XOR with previous frame | 156 215 | +73% |
| bit-plane / planar layout | 123 019 | +36% |
| vertical 1x8 pixel packing | 126 784 | +41% |
| serpentine scan order | 127 848 | +42% |
| per-frame RLE | 113 920 | +26% |
| bzip2 instead of LZMA | 166 970 | +85% |

The payload is at its entropy floor. Below this, the only lever is the video
itself.

## Trading quality for size

```sh
python3 tools/reencode.py src/video_data.h --fps 12 --out src/video_data.h
```

| resolution / fps | payload |
|---|---:|
| 64x36 @ 20 | 113 977 |
| **64x36 @ 15 (default)** | **90 149** |
| 64x36 @ 12 | 75 376 |
| 48x27 @ 20 | 75 754 |
| 64x36 @ 10 | 65 150 |
| 48x27 @ 12 | 50 915 |
| 32x18 @ 20 | 42 597 |

Add ~3 KB of code to any payload to get the binary size. To start from a video
file instead, use `tools/convert.py input.mp4 --fps 15`.

## Windows notes

One executable covers XP through 11, because the renderer is chosen at runtime
rather than at compile time:

```c
if (SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING))
        g_vt = 1;   /* Windows 10 1511+: ANSI, one WriteFile per frame */
/* otherwise: XP, WriteConsoleOutputCharacterA straight into the buffer */
```

The build is freestanding: `-nostdlib`, a hand-written `mainCRTStartup`, and
`kernel32.dll` as the only import. This is not just for size — a UCRT-based
MinGW would otherwise emit `api-ms-win-crt-*.dll` imports, which do not exist
before Windows 10.

```sh
i686-w64-mingw32-objdump -p bad_apple.exe | grep 'DLL Name'
# DLL Name: KERNEL32.dll     <- this line and nothing else
```

Other XP-specific details: the console window is shrunk *before* the buffer (a
buffer may never be smaller than its window), `timeBeginPeriod(1)` is loaded
dynamically from `winmm.dll` to fix the ~15.6 ms `Sleep` granularity, and
timing uses `GetTickCount` so no 64-bit division pulls in libgcc.

**Do not add `-Wl,--section-alignment=512`.** It looks like free space, but ld
emits an inconsistent header for sub-page alignment and Windows rejects the
image with `is not a valid Win32 application` — the same message it shows for
a 64-bit exe, which makes it easy to misdiagnose. `make win-small` keeps the
safe part of that idea (dropping `.reloc` and the NX/ASLR bits) for ~2 KB.

## Running it in a VM

`tools/make_iso.py` is a small pure-Python ISO-9660 writer, so no `xorriso` or
`genisoimage` is needed:

```sh
make iso
```

Attach it to QEMU as a second CD drive:

```sh
qemu-system-x86_64 -enable-kvm -m 1024 \
  -drive file=disk.qcow2,format=qcow2,if=ide,index=0 \
  -drive file=badapple.iso,media=cdrom,index=3
```

Or swap the disc without restarting the VM, from the QEMU monitor
(`Ctrl+Alt+2`):

```
change ide1-cd0 /path/to/badapple.iso raw
```

## Layout

```
src/main.c          player, platform layer (Linux syscalls / Win32 console)
src/lzma_dec.c      minimal LZMA1 decoder, ~2 KB of code
src/video_data.h    generated: the compressed video
tools/convert.py    video file -> video_data.h
tools/reencode.py   video_data.h -> video_data.h, at a lower fps
tools/make_iso.py   files -> ISO-9660 image
tools/test_decode.c checks the C decoder against Python's lzma module
```

## License

MIT.
