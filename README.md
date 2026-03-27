
```
gcc -Os -s   -ffunction-sections -fdata-sections -Wl,--gc-sections   -fno-unwind-tables -fno-asynchronous-unwind-tables   -fno-stack-protector -fomit-frame-pointer   -Wl,--build-id=none   -o bad_apple.exe src/main.c   -Wl,-Bstatic -llzma -Wl,-Bdynamic
```

```
strip bad_apple.exe
```

```
upx --best --lzma bad_apple.exe
```