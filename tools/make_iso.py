#!/usr/bin/env python3
"""Build a plain ISO-9660 CD image from a few files. No mkisofs/xorriso needed.

Usage:
    python3 tools/make_iso.py -o badapple.iso bad_apple.exe [more files...]

Flat layout (no subdirectories), ISO-9660 Level 1 names (8.3, uppercase),
which is exactly what Windows XP mounts without any fuss.
"""
import argparse
import os
import struct
import sys
import time

SECTOR = 2048


def both16(v):
    return struct.pack("<H", v) + struct.pack(">H", v)


def both32(v):
    return struct.pack("<I", v) + struct.pack(">I", v)


def pad(b, n, fill=b" "):
    if len(b) > n:
        return b[:n]
    return b + fill * (n - len(b))


def sectors_for(nbytes):
    return (nbytes + SECTOR - 1) // SECTOR


def dt7(t=None):
    lt = time.localtime(t if t is not None else time.time())
    return struct.pack("BBBBBBb", lt.tm_year - 1900, lt.tm_mon, lt.tm_mday,
                       lt.tm_hour, lt.tm_min, lt.tm_sec, 0)


def dt17(t=None):
    lt = time.localtime(t if t is not None else time.time())
    s = "%04d%02d%02d%02d%02d%02d00" % (lt.tm_year, lt.tm_mon, lt.tm_mday,
                                        lt.tm_hour, lt.tm_min, lt.tm_sec)
    return s.encode("ascii") + b"\x00"


def iso_name(path):
    base = os.path.basename(path).upper()
    stem, dot, ext = base.partition(".")

    def clean(s):
        return "".join(c if (c.isalnum() or c == "_") else "_" for c in s)

    stem = clean(stem)[:8] or "FILE"
    ext = clean(ext)[:3]
    return ("%s.%s;1" % (stem, ext)) if ext else ("%s.;1" % stem)


def dir_record(name_bytes, lba, length, is_dir, stamp):
    rec = bytearray()
    rec += b"\x00"
    rec += b"\x00"
    rec += both32(lba)
    rec += both32(length)
    rec += stamp
    rec += bytes([0x02 if is_dir else 0x00])
    rec += b"\x00"
    rec += b"\x00"
    rec += both16(1)
    rec += bytes([len(name_bytes)])
    rec += name_bytes
    if len(rec) % 2:
        rec += b"\x00"
    rec[0] = len(rec)
    return bytes(rec)


def build(files, out_path, volume_id):
    stamp = dt7()
    stamp17 = dt17()

    entries = []
    for p in files:
        if not os.path.isfile(p):
            sys.exit("not a file: %s" % p)
        entries.append((iso_name(p), p, os.path.getsize(p)))

    names = [e[0] for e in entries]
    if len(set(names)) != len(names):
        sys.exit("8.3 name collision: %s" % names)

    pvd_lba = 16
    path_l_lba = 18
    path_m_lba = 19
    root_lba = 20

    root_body = bytearray()
    root_body += dir_record(b"\x00", root_lba, SECTOR, True, stamp)
    root_body += dir_record(b"\x01", root_lba, SECTOR, True, stamp)

    lba = root_lba + 1
    placed = []
    for name, path, size in entries:
        placed.append((path, lba, size))
        root_body += dir_record(name.encode("ascii"), lba, size, False, stamp)
        lba += sectors_for(size)

    if len(root_body) > SECTOR:
        sys.exit("too many files for a single-sector root directory")

    total_sectors = lba

    pt_l = struct.pack("<BBIH", 1, 0, root_lba, 1) + b"\x00\x00"
    pt_m = struct.pack(">BBIH", 1, 0, root_lba, 1) + b"\x00\x00"
    pt_size = 10

    root_rec = pad(dir_record(b"\x00", root_lba, SECTOR, True, stamp), 34,
                   b"\x00")

    pvd = bytearray()
    pvd += b"\x01" + b"CD001" + b"\x01"
    pvd += b"\x00"
    pvd += pad(b"", 32)
    pvd += pad(volume_id.upper().encode("ascii"), 32)
    pvd += b"\x00" * 8
    pvd += both32(total_sectors)
    pvd += b"\x00" * 32
    pvd += both16(1)
    pvd += both16(1)
    pvd += both16(SECTOR)
    pvd += both32(pt_size)
    pvd += struct.pack("<I", path_l_lba)
    pvd += struct.pack("<I", 0)
    pvd += struct.pack(">I", path_m_lba)
    pvd += struct.pack(">I", 0)
    pvd += root_rec
    pvd += pad(b"", 128)
    pvd += pad(b"", 128)
    pvd += pad(b"", 128)
    pvd += pad(b"MAKE_ISO.PY", 128)
    pvd += pad(b"", 37)
    pvd += pad(b"", 37)
    pvd += pad(b"", 37)
    pvd += stamp17
    pvd += stamp17
    pvd += b"\x00" * 17
    pvd += stamp17
    pvd += b"\x01"
    pvd += b"\x00"
    pvd += b"\x00" * 512
    pvd += b"\x00" * 653
    assert len(pvd) == SECTOR, len(pvd)

    term = pad(b"\xff" + b"CD001" + b"\x01", SECTOR, b"\x00")

    with open(out_path, "wb") as f:
        f.write(b"\x00" * SECTOR * 16)
        f.write(bytes(pvd))
        f.write(term)
        f.write(pad(pt_l, SECTOR, b"\x00"))
        f.write(pad(pt_m, SECTOR, b"\x00"))
        f.write(pad(bytes(root_body), SECTOR, b"\x00"))
        for path, _lba, size in placed:
            with open(path, "rb") as src:
                data = src.read()
            f.write(data)
            rem = (-len(data)) % SECTOR
            if rem:
                f.write(b"\x00" * rem)

    print("wrote %s  (%d sectors, %d bytes)"
          % (out_path, total_sectors, total_sectors * SECTOR))
    for name, path, size in entries:
        print("  %-14s %8d B   <- %s" % (name, size, path))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("files", nargs="+")
    ap.add_argument("-o", "--out", default="badapple.iso")
    ap.add_argument("-V", "--volume-id", default="BADAPPLE")
    a = ap.parse_args()
    build(a.files, a.out, a.volume_id)


if __name__ == "__main__":
    main()
