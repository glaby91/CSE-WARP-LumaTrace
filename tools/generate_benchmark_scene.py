#!/usr/bin/env python3
"""Generate a deterministic, texture-free 241,260-triangle LumaTrace bench scene."""

import argparse
import json
import struct
from pathlib import Path


TRIANGLE_COUNT = 241_260
FLOOR_QUADS = 120_000
BLOCKER_QUADS = (TRIANGLE_COUNT - FLOOR_QUADS * 2) // 2


def quad_grid(columns: int, rows: int, count: int, width: float, height: float,
              center_x: float, center_y: float, z: float):
    quads = 0
    for row in range(rows):
        for column in range(columns):
            if quads == count:
                return
            x0 = center_x - width / 2 + width * column / columns
            x1 = center_x - width / 2 + width * (column + 1) / columns
            y0 = center_y - height / 2 + height * row / rows
            y1 = center_y - height / 2 + height * (row + 1) / rows
            yield ((x0, y0, z), (x1, y0, z), (x0, y1, z), (x1, y1, z))
            quads += 1


def make_glb(path: Path):
    positions = bytearray()
    normals = bytearray()
    texcoords = bytearray()
    indices = bytearray()
    min_pos = [float("inf")] * 3
    max_pos = [float("-inf")] * 3
    vertex = 0

    grids = (
        quad_grid(400, 300, FLOOR_QUADS, 4.0, 3.0, 0.0, 0.0, 0.0),
        quad_grid(30, 21, BLOCKER_QUADS, 0.72, 0.48, -0.38, 0.38, 0.42),
    )
    for grid in grids:
        for quad in grid:
            for uv, point in zip(((0.0, 0.0), (1.0, 0.0), (0.0, 1.0), (1.0, 1.0)), quad):
                positions.extend(struct.pack("<3f", *point))
                normals.extend(struct.pack("<3f", 0.0, 0.0, 1.0))
                texcoords.extend(struct.pack("<2f", *uv))
                for axis in range(3):
                    min_pos[axis] = min(min_pos[axis], point[axis])
                    max_pos[axis] = max(max_pos[axis], point[axis])
            indices.extend(struct.pack("<6I", vertex, vertex + 1, vertex + 2,
                                       vertex + 1, vertex + 3, vertex + 2))
            vertex += 4

    total_quads = FLOOR_QUADS + BLOCKER_QUADS
    if total_quads * 2 != TRIANGLE_COUNT:
        raise RuntimeError(f"generated {total_quads * 2} triangles, expected {TRIANGLE_COUNT}")

    binary = bytearray()
    views = []
    for payload, target in ((positions, 34962), (normals, 34962), (texcoords, 34962), (indices, 34963)):
        offset = len(binary)
        binary.extend(payload)
        while len(binary) % 4:
            binary.append(0)
        views.append({"buffer": 0, "byteOffset": offset, "byteLength": len(payload), "target": target})

    accessors = [
        {"bufferView": 0, "componentType": 5126, "count": vertex, "type": "VEC3", "min": min_pos, "max": max_pos},
        {"bufferView": 1, "componentType": 5126, "count": vertex, "type": "VEC3"},
        {"bufferView": 2, "componentType": 5126, "count": vertex, "type": "VEC2"},
        {"bufferView": 3, "componentType": 5125, "count": total_quads * 6, "type": "SCALAR"},
    ]
    document = {
        "asset": {"version": "2.0", "generator": "CSE-WARP-LumaTrace deterministic benchmark"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}, "indices": 3, "material": 0}]}],
        "materials": [{"name": "Benchmark warm red", "pbrMetallicRoughness": {"baseColorFactor": [0.65, 0.22, 0.12, 1.0], "metallicFactor": 0.0, "roughnessFactor": 0.72}}],
        "buffers": [{"byteLength": len(binary)}],
        "bufferViews": views,
        "accessors": accessors,
    }
    json_bytes = json.dumps(document, separators=(",", ":")).encode("utf-8")
    json_bytes += b" " * ((-len(json_bytes)) % 4)
    binary += b"\0" * ((-len(binary)) % 4)
    total_length = 12 + 8 + len(json_bytes) + 8 + len(binary)
    with path.open("wb") as output:
        output.write(struct.pack("<4sII", b"glTF", 2, total_length))
        output.write(struct.pack("<I4s", len(json_bytes), b"JSON"))
        output.write(json_bytes)
        output.write(struct.pack("<I4s", len(binary), b"BIN\0"))
        output.write(binary)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out-dir", required=True, type=Path)
    args = parser.parse_args()
    args.out_dir.mkdir(parents=True, exist_ok=True)
    model_path = args.out_dir / "luma_trace_benchmark.glb"
    make_glb(model_path)
    manifest = {
        "format": "lumatrace-scene-v1",
        "name": "Synthetic 241,260 triangle WARP benchmark",
        "units": "meters",
        "objects": [{"id": "benchmark_mesh", "type": "gltf2-model", "file": model_path.name}],
        "environment": {"background": [0.035, 0.04, 0.05]},
    }
    (args.out_dir / "scene.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(f"Generated {TRIANGLE_COUNT} triangles at {model_path}")


if __name__ == "__main__":
    main()
