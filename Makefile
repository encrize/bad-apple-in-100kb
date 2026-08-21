# Bad Apple!! - size-obsessed build
#
#   make            standard build (libc, no printf/malloc/liblzma)
#   make tiny       x86-64 Linux only: no libc at all, raw syscalls
#   make win        cross-compile bad_apple.exe for Windows XP and later
#   make iso        wrap bad_apple.exe into badapple.iso for a VM CD drive
#   make upx        pack the standard build with UPX (if installed)
#   make sizes      build everything and print a size table
#   make data       regenerate src/video_data.h from tools/reencode.py

CC      ?= gcc
UPX     ?= upx
WINCC   ?= i686-w64-mingw32-gcc
FPS     ?= 0            # 0 = keep source fps, or set e.g. FPS=15
OLDHDR  ?= src/video_data.h

LZMA_DEFS = -DVID_LZMA_LC=1 -DVID_LZMA_LP=0 -DVID_LZMA_PB=0

CFLAGS  = -Os -std=gnu99 -Isrc $(LZMA_DEFS) \
          -ffunction-sections -fdata-sections \
          -fno-unwind-tables -fno-asynchronous-unwind-tables \
          -fno-stack-protector -fomit-frame-pointer \
          -fno-ident -fmerge-all-constants -fno-plt

LDFLAGS = -s -Wl,--gc-sections -Wl,--build-id=none -Wl,-z,norelro \
          -Wl,--hash-style=sysv

TINYLDFLAGS = $(LDFLAGS) -Wl,-n

SRC = src/main.c src/lzma_dec.c
DEPS = src/video_data.h src/lzma_dec.h

.PHONY: all tiny win win-small win-crt iso upx sizes data clean

all: bad_apple

bad_apple: $(SRC) $(DEPS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRC)
	@strip --strip-all --remove-section=.comment --remove-section=.note $@ 2>/dev/null || true

tiny: $(SRC) $(DEPS)
	$(CC) $(CFLAGS) -DNOLIBC -nostdlib -nostartfiles -static \
	      $(TINYLDFLAGS) -o bad_apple_tiny $(SRC)
	@strip --strip-all --remove-section=.comment --remove-section=.note bad_apple_tiny 2>/dev/null || true

WINCFLAGS = -Os -std=gnu99 -Isrc $(LZMA_DEFS) -D_WIN32_WINNT=0x0501 \
            -ffreestanding -ffunction-sections \
            -fno-unwind-tables -fno-asynchronous-unwind-tables \
            -fno-stack-protector -fomit-frame-pointer -fno-ident

WINLDFLAGS = -s -Wl,--gc-sections

win: $(SRC) $(DEPS)
	$(WINCC) $(WINCFLAGS) -DNOLIBC -nostdlib -nostartfiles -mconsole \
	         $(WINLDFLAGS) -o bad_apple.exe $(SRC) \
	         -lkernel32 -static-libgcc -lgcc

win-small: $(SRC) $(DEPS)
	$(WINCC) $(WINCFLAGS) -DNOLIBC -nostdlib -nostartfiles -mconsole \
	         $(WINLDFLAGS) -Wl,--file-alignment=512 \
	         -Wl,--disable-reloc-section -Wl,--disable-nxcompat \
	         -Wl,--disable-dynamicbase \
	         -o bad_apple.exe $(SRC) -lkernel32 -static-libgcc -lgcc

# Fallback: ordinary CRT build. Needs an msvcrt-based (NOT UCRT) MinGW.
win-crt: $(SRC) $(DEPS)
	$(WINCC) $(WINCFLAGS) -mconsole $(WINLDFLAGS) \
	         -o bad_apple.exe $(SRC)

iso: bad_apple.exe
	python3 tools/make_iso.py -o badapple.iso -V BADAPPLE bad_apple.exe

upx: bad_apple
	cp bad_apple bad_apple_upx
	$(UPX) --best --lzma bad_apple_upx

data:
	python3 tools/reencode.py $(OLDHDR) $(if $(filter-out 0,$(FPS)),--fps $(FPS),) --out src/video_data.h

sizes: bad_apple tiny
	@echo
	@ls -l bad_apple bad_apple_tiny 2>/dev/null | awk '{printf "%-20s %8d bytes\n", $$9, $$5}'

clean:
	rm -f bad_apple bad_apple_tiny bad_apple_upx
