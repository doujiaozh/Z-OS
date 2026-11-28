import hashlib
import os
import struct
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build"
DISK = ROOT / "disk"
ASSETS = Path(os.environ.get("TINYGPT_ASSETS", r"D:\Code\TinyGPT-C\assets"))

IMAGE_SIZE = 512 * 1024 * 1024
SECTOR = 512
PART_LBA = 2048
PART_SECTORS = (IMAGE_SIZE // SECTOR) - PART_LBA
MAGIC = b"ZDISK001"
NAME_MAX = 32
MAX_FILES = 32


def convert_weights(source, destination):
    if destination.exists() and destination.stat().st_size > 100 * 1024 * 1024:
        print("fp16 weights present", destination.stat().st_size, flush=True)
        return destination
    output = destination.with_suffix(".part")
    total = 0
    tensors = 0
    with open(source, "rb") as handle, open(output, "wb") as target:
        magic = handle.read(8)
        if magic != b"TGPTBIN1":
            raise SystemExit("unexpected model magic")
        target.write(magic)
        header = handle.read(32)
        version, layers, heads, embd, ctx, vocab, inner, count = struct.unpack("<8I", header)
        target.write(header)
        for _ in range(count):
            name_length = struct.unpack("<I", handle.read(4))[0]
            name = handle.read(name_length)
            target.write(struct.pack("<I", name_length))
            target.write(name)
            ndim = struct.unpack("<I", handle.read(4))[0]
            dims = []
            for _ in range(ndim):
                dims.append(struct.unpack("<Q", handle.read(8))[0])
            dtype = struct.unpack("<I", handle.read(4))[0]
            nbytes = struct.unpack("<Q", handle.read(8))[0]
            if dtype != 0:
                raise SystemExit("unexpected dtype")
            data = np.fromfile(handle, dtype="<f4", count=nbytes // 4).reshape(tuple(dims))
            if len(dims) == 2 and name not in (b"wte.weight", b"wpe.weight"):
                data = np.ascontiguousarray(data.T)
                dims = [dims[1], dims[0]]
            half = data.astype("<f2").tobytes()
            target.write(struct.pack("<I", ndim))
            for value in dims:
                target.write(struct.pack("<Q", value))
            target.write(struct.pack("<I", 1))
            target.write(struct.pack("<Q", len(half)))
            target.write(half)
            total += len(half)
            tensors += 1
            if tensors % 16 == 0:
                print("converted", tensors, count, total, flush=True)
    os.replace(output, destination)
    print("fp16 weights written", destination, destination.stat().st_size, "bytes", flush=True)
    return destination


def build_vocab(source, destination):
    import json
    vocabulary = json.load(open(source, encoding="utf-8"))
    order = list(range(ord("!"), ord("~") + 1)) + list(range(0xA1, 0xAD)) + list(range(0xAE, 0x100))
    codes = order[:]
    extra = 0
    for byte in range(256):
        if byte not in order:
            order.append(byte)
            codes.append(256 + extra)
            extra += 1
    decode_map = {chr(code): byte for byte, code in zip(order, codes)}
    inverse = {identifier: token for token, identifier in vocabulary.items()}
    blobs = []
    for identifier in range(50257):
        token = inverse[identifier]
        blobs.append(bytes(decode_map[character] for character in token))
    table = bytearray()
    payload = bytearray()
    offset = 0
    for blob in blobs:
        table += struct.pack("<II", offset, len(blob))
        payload += blob
        offset += len(blob)
    header = struct.pack("<8sII", b"VOCB0001", len(blobs), len(payload))
    destination.write_bytes(header + bytes(table) + bytes(payload))
    print("vocab bytes", destination.stat().st_size, "tokens", len(blobs), flush=True)
    return destination


def build_directory(files):
    entries = []
    lba = None
    table = bytearray(SECTOR)
    for index, (name, size, first_lba) in enumerate(files):
        offset = index * 64
        if offset + 64 > SECTOR:
            raise SystemExit("directory overflow")
        encoded = name.encode("utf-8")
        if len(encoded) >= NAME_MAX:
            raise SystemExit("name too long")
        struct.pack_into("<%ds" % NAME_MAX, table, offset, encoded)
        struct.pack_into("<Q", table, offset + NAME_MAX, size)
        struct.pack_into("<Q", table, offset + NAME_MAX + 8, first_lba)
        struct.pack_into("<I", table, offset + NAME_MAX + 16, 0)
        struct.pack_into("<I", table, offset + NAME_MAX + 20, 0)
    return bytes(table)


def superblock(total_sectors, file_count, dir_lba, data_lba):
    data = bytearray(SECTOR)
    data[0:8] = MAGIC
    struct.pack_into("<I", data, 8, 1)
    struct.pack_into("<I", data, 12, SECTOR)
    struct.pack_into("<Q", data, 16, total_sectors)
    struct.pack_into("<I", data, 24, file_count)
    struct.pack_into("<I", data, 28, 0)
    struct.pack_into("<Q", data, 32, dir_lba)
    struct.pack_into("<Q", data, 40, data_lba)
    return bytes(data)


def main():
    BUILD.mkdir(parents=True, exist_ok=True)
    DISK.mkdir(parents=True, exist_ok=True)
    weights_source = ASSETS / "model.bin"
    if not weights_source.exists():
        raise SystemExit("missing model.bin at " + str(weights_source))
    weights_fp16 = DISK / "weights.f16"
    convert_weights(weights_source, weights_fp16)
    vocab_source = ASSETS / "vocab.json"
    if not vocab_source.exists():
        raise SystemExit("missing vocab.json at " + str(vocab_source))
    vocab_binary = DISK / "vocab.bin"
    build_vocab(vocab_source, vocab_binary)
    payloads = []
    payloads.append(("WEIGHTS.BIN", weights_fp16))
    payloads.append(("VOCAB.BIN", vocab_binary))
    note = DISK / "hd_note.txt"
    note.write_bytes(b"Z-OS large weight loading test payload\n")
    payloads.append(("NOTE.TXT", note))
    dir_lba = 1
    data_lba = 2
    cursor = data_lba
    files = []
    for name, path in payloads:
        size = path.stat().st_size
        sectors = (size + SECTOR - 1) // SECTOR
        files.append((name, size, cursor))
        cursor += sectors
    image_path = DISK / "hd.img"
    with open(image_path, "wb") as image:
        image.truncate(IMAGE_SIZE)
        stage1 = (BUILD / "stage1.bin").read_bytes()
        stage2 = (BUILD / "stage2.bin").read_bytes()
        kernel = (BUILD / "z-os.bin").read_bytes()
        image.seek(0)
        image.write(stage1)
        image.write(stage2)
        image.seek(33 * SECTOR)
        image.write(kernel)
        image.seek(PART_LBA * SECTOR)
        image.write(superblock(PART_SECTORS, len(files), dir_lba, data_lba))
        image.seek((PART_LBA + dir_lba) * SECTOR)
        image.write(build_directory(files))
        for name, path in payloads:
            entry = next(item for item in files if item[0] == name)
            image.seek((PART_LBA + entry[2]) * SECTOR)
            with open(path, "rb") as handle:
                remaining = entry[1]
                while remaining > 0:
                    chunk = handle.read(min(8 * 1024 * 1024, remaining))
                    if not chunk:
                        break
                    image.write(chunk)
                    remaining -= len(chunk)
    digest = hashlib.sha256(image_path.read_bytes()).hexdigest()
    print("hd image", image_path, image_path.stat().st_size, flush=True)
    print("partition lba", PART_LBA, "sectors", PART_SECTORS, flush=True)
    print("files", [(name, size) for name, size, _ in files], flush=True)
    print("weights bytes", weights_fp16.stat().st_size, flush=True)
    print("weights sha256", hashlib.sha256(weights_fp16.read_bytes()).hexdigest(), flush=True)
    print("image sha256", digest, flush=True)


if __name__ == "__main__":
    main()
