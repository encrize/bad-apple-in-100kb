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
