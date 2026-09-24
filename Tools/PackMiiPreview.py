"""Pack one Generic default Mii probe into a validated Apollo preview asset."""

from __future__ import annotations

import argparse
import hashlib
import math
import os
import struct
from pathlib import Path


GEOMETRY_HEADER = struct.Struct("<4I")
PART_HEADER = struct.Struct("<7I9f")
TEXTURE_HEADER = struct.Struct("<4I")
PACKAGE_HEADER = struct.Struct("<4s5I88s32s32s")
PART_TEXTURE = {1: 0, 4: 1, 6: 2, 7: 3, 8: 4}
MAX_FILE_BYTES = 32 * 1024 * 1024


def read_capped(path: Path) -> bytes:
    if path.stat().st_size > MAX_FILE_BYTES:
        raise ValueError(f"oversized input: {path}")
    return path.read_bytes()


def read_geometry(path: Path) -> tuple[int, bytes, set[int]]:
    data = read_capped(path)
    if len(data) < GEOMETRY_HEADER.size:
        raise ValueError("geometry header is truncated")

    magic, version, part_count, reserved = GEOMETRY_HEADER.unpack_from(data)
    if (magic, version, reserved) != (0x474D5041, 1, 0) or not 1 <= part_count <= 9:
        raise ValueError("unsupported geometry header")

    offset = GEOMETRY_HEADER.size
    seen: set[int] = set()
    textured: set[int] = set()

    for _ in range(part_count):
        if len(data) - offset < PART_HEADER.size:
            raise ValueError("part header is truncated")

        values = PART_HEADER.unpack_from(data, offset)
        offset += PART_HEADER.size
        draw_type, modulate, cull, has_texture, position_bytes, uv_bytes, index_count = values[:7]
        vertex_count = position_bytes // 8

        if (
            draw_type in seen
            or draw_type > 8
            or modulate > 5
            or cull > 2
            or has_texture > 1
            or position_bytes == 0
            or position_bytes % 8
            or vertex_count > 65536
            or uv_bytes % 4
            or (has_texture and uv_bytes != vertex_count * 4)
            or (uv_bytes and uv_bytes != vertex_count * 4)
            or index_count == 0
            or index_count % 3
            or not all(math.isfinite(value) for value in values[7:])
        ):
            raise ValueError(f"invalid draw part {draw_type}")

        payload_bytes = position_bytes + uv_bytes + index_count * 2
        if payload_bytes > MAX_FILE_BYTES or len(data) - offset < payload_bytes:
            raise ValueError(f"part {draw_type} payload is truncated or oversized")

        position_end = offset + position_bytes
        uv_end = position_end + uv_bytes
        for position in struct.iter_unpack("<4e", data[offset:position_end]):
            if not all(math.isfinite(value) for value in position):
                raise ValueError(f"part {draw_type} has nonfinite positions")
        for uv in struct.iter_unpack("<2e", data[position_end:uv_end]):
            if not all(math.isfinite(value) for value in uv):
                raise ValueError(f"part {draw_type} has nonfinite UVs")
        for (index,) in struct.iter_unpack("<H", data[uv_end:uv_end + index_count * 2]):
            if index >= vertex_count:
                raise ValueError(f"part {draw_type} has out-of-range indices")

        seen.add(draw_type)
        if has_texture:
            if draw_type not in PART_TEXTURE:
                raise ValueError(f"textured draw part {draw_type} has no texture mapping")
            textured.add(PART_TEXTURE[draw_type])
        offset += payload_bytes

    if offset != len(data) or not {1, 6}.issubset(seen):
        raise ValueError("geometry has trailing bytes or required face parts are absent")

    return part_count, data[GEOMETRY_HEADER.size:], textured


def read_textures(directory: Path, required: set[int]) -> tuple[int, bytes]:
    payload = bytearray()
    for texture_type in sorted(required):
        path = directory / f"MiiView{texture_type}Probe.aptx"
        data = read_capped(path)
        if len(data) < TEXTURE_HEADER.size:
            raise ValueError(f"texture {texture_type} header is truncated")

        magic, width, height, pixel_bytes = TEXTURE_HEADER.unpack_from(data)
        if (
            magic != 0x58545041
            or not 1 <= width <= 2048
            or not 1 <= height <= 2048
            or pixel_bytes != width * height * 4
            or len(data) != TEXTURE_HEADER.size + pixel_bytes
        ):
            raise ValueError(f"texture {texture_type} size or format is invalid")

        payload += TEXTURE_HEADER.pack(texture_type, width, height, pixel_bytes)
        payload += data[TEXTURE_HEADER.size:]

    return len(required), bytes(payload)


def build_package(directory: Path, sdk_root: Path, default_index: int) -> bytes:
    char_info = read_capped(directory / "MiiCharInfoProbe.bin")
    if len(char_info) != 88:
        raise ValueError("CharInfo snapshot must be 88 bytes")

    part_count, parts, required = read_geometry(directory / "MiiGeometryProbe.apmg")
    texture_count, textures = read_textures(directory, required)

    resource_root = sdk_root / "Resources" / "Mii" / "Common" / "resource"
    texture_resource = read_capped(resource_root / "WinGenericTextureLowSRGB.dat")
    shape_resource = read_capped(resource_root / "ShapeMid.dat")
    source_hash = hashlib.sha256(texture_resource + shape_resource).digest()

    body = textures + parts
    total_bytes = PACKAGE_HEADER.size + len(body)
    if total_bytes > MAX_FILE_BYTES:
        raise ValueError("preview package exceeds 32 MiB")

    header = PACKAGE_HEADER.pack(
        b"APMP", 2, total_bytes, part_count, texture_count, default_index,
        char_info, source_hash, hashlib.sha256(body).digest()
    )
    return header + body


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input-dir", type=Path, default=Path("Build/MiiGeometryProbe"))
    parser.add_argument("--sdk-root", type=Path, default=os.environ.get("NINTENDO_SDK_ROOT"))
    parser.add_argument("--output", type=Path, default=Path("Build/MiiGeometryProbe/Default0.apmp"))
    parser.add_argument("--default-index", type=int, required=True, choices=range(6))
    args = parser.parse_args()

    if args.sdk_root is None:
        parser.error("pass --sdk-root or set NINTENDO_SDK_ROOT")

    package = build_package(args.input_dir, args.sdk_root, args.default_index)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.output.with_suffix(args.output.suffix + ".tmp")
    try:
        temporary.write_bytes(package)
        temporary.replace(args.output)
    finally:
        temporary.unlink(missing_ok=True)

    print(f"Packed {args.output}: {len(package)} bytes, SHA-256 {hashlib.sha256(package).hexdigest()}")


if __name__ == "__main__":
    main()
