import sys, os, lzma
import numpy as np
try:
    import cv2
except ImportError:
    print("ERROR: pip install opencv-python numpy"); sys.exit(1)

WIDTH, HEIGHT, THRESHOLD, TARGET_FPS, MAX_FRAMES = 64, 36, 128, 15, 999999

def pack_bits(arr2d):
    flat = arr2d.flatten()
    n = (len(flat) + 7) // 8
    out = bytearray(n)
    for i, b in enumerate(flat):
        if b: out[i >> 3] |= 1 << (i & 7)
    return bytes(out)

def convert(path):
    cap = cv2.VideoCapture(path)
    if not cap.isOpened():
        print(f"Cannot open: {path}"); sys.exit(1)
    src_fps = cap.get(cv2.CAP_PROP_FPS) or 30.0
    step = max(1, round(src_fps / TARGET_FPS))
    BPF = (WIDTH * HEIGHT + 7) // 8
    print(f"Source fps={src_fps:.1f}  step={step}  output ~{TARGET_FPS}fps")

    frames = []
    ri = 0
    print("Reading frames: ", end="", flush=True)
    while True:
        ok, img = cap.read()
        if not ok: break
        if ri % step == 0:
            gray  = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
            small = cv2.resize(gray, (WIDTH, HEIGHT), interpolation=cv2.INTER_AREA)
            _, bw  = cv2.threshold(small, THRESHOLD, 1, cv2.THRESH_BINARY)
            frames.append(pack_bits(bw.astype(np.uint8)))
            if len(frames) % 200 == 0:
                print(len(frames), end=" ", flush=True)
            if len(frames) >= MAX_FRAMES:
                break
        ri += 1
    cap.release()
    print(f"\nTotal frames: {len(frames)}")

    stream = b''.join(frames)
    print(f"Raw stream:   {len(stream):,} bytes")

    compressed = lzma.compress(stream, format=lzma.FORMAT_XZ, preset=9)
    print(f"Compressed:   {len(compressed):,} bytes ({len(compressed)//1024} KB,  {100*len(compressed)/len(stream):.1f}%)")

    os.makedirs("../src", exist_ok=True)
    with open("../src/video_data.h", "w") as f:
        f.write("/* by Gibsy */\n#pragma once\n\n")
        f.write(f"#define VID_WIDTH       {WIDTH}\n")
        f.write(f"#define VID_HEIGHT      {HEIGHT}\n")
        f.write(f"#define VID_FPS         {TARGET_FPS}\n")
        f.write(f"#define VID_FRAMES      {len(frames)}\n")
        f.write(f"#define VID_FRAME_BYTES {BPF}\n")
        f.write(f"#define VID_DATA_SIZE   {len(compressed)}\n\n")
        f.write("static const unsigned char video_data[VID_DATA_SIZE] = {\n")
        for i in range(0, len(compressed), 16):
            c = compressed[i:i+16]
            f.write("    " + ", ".join(f"0x{b:02X}" for b in c) + ",\n")
        f.write("};\n")
    print(f"\nReady, output: src/video_data.h\n")

if len(sys.argv) < 2:
    print("Usage: python convert.py <video.mp4>"); sys.exit(1)
convert(sys.argv[1])