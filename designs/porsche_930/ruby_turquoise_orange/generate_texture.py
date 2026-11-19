#!/usr/bin/env python3
"""Bake a separate LumaTrace-ready base-colour texture for Porsche 930 paint.

This script does not edit the model. It reads material 9 (paint), keeps the
original GLB's TEXCOORD_0 atlas, and exports a standalone sRGB PNG.
Dependencies: Python 3, NumPy, Pillow.
"""
from __future__ import annotations
import json
import struct
import argparse
from pathlib import Path
import numpy as np
from PIL import Image

SIZE = 2048
PAINT_MATERIAL = 9

COLORS = {
    "ruby": (158, 0, 30),
    "turquoise": (0, 183, 197),
    "orange": (255, 138, 36),
    "gold_pinstripe": (247, 194, 83),
}
COMPONENT_DTYPES = {5120: "i1", 5121: "u1", 5122: "<i2", 5123: "<u2", 5125: "<u4", 5126: "<f4"}
COMPONENT_COUNTS = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}


def glb_chunks(data: bytes):
    if len(data) < 20 or data[:4] != b"glTF":
        raise ValueError("Input is not a GLB file")
    magic, version, declared = struct.unpack_from("<4sII", data, 0)
    if version != 2 or declared != len(data):
        raise ValueError("Expected a complete glTF 2.0 GLB")
    offset, document, binary = 12, None, None
    while offset < len(data):
        length, chunk_type = struct.unpack_from("<I4s", data, offset)
        offset += 8
        chunk = data[offset:offset + length]
        if len(chunk) != length:
            raise ValueError("Truncated GLB chunk")
        offset += length
        if chunk_type == b"JSON":
            document = json.loads(chunk.rstrip(b" \t\r\n\0"))
        elif chunk_type == b"BIN\0":
            binary = chunk
    if document is None or binary is None:
        raise ValueError("GLB is missing JSON or BIN chunk")
    return document, binary


def matrix_for_node(node: dict) -> np.ndarray:
    if "matrix" in node:
        return np.asarray(node["matrix"], dtype=np.float64).reshape((4, 4), order="F")
    t = np.asarray(node.get("translation", [0, 0, 0]), dtype=np.float64)
    s = np.asarray(node.get("scale", [1, 1, 1]), dtype=np.float64)
    x, y, z, w = node.get("rotation", [0, 0, 0, 1])
    rot = np.array([
        [1 - 2 * (y*y + z*z), 2 * (x*y - z*w), 2 * (x*z + y*w)],
        [2 * (x*y + z*w), 1 - 2 * (x*x + z*z), 2 * (y*z - x*w)],
        [2 * (x*z - y*w), 2 * (y*z + x*w), 1 - 2 * (x*x + y*y)],
    ])
    result = np.eye(4, dtype=np.float64)
    result[:3, :3] = rot @ np.diag(s)
    result[:3, 3] = t
    return result


def accessor(doc: dict, binary: bytes, index: int) -> np.ndarray:
    spec = doc["accessors"][index]
    view = doc["bufferViews"][spec["bufferView"]]
    dtype = np.dtype(COMPONENT_DTYPES[spec["componentType"]])
    count = spec["count"] * COMPONENT_COUNTS[spec["type"]]
    start = view.get("byteOffset", 0) + spec.get("byteOffset", 0)
    values = np.frombuffer(binary, dtype=dtype, count=count, offset=start)
    return values.reshape(spec["count"], COMPONENT_COUNTS[spec["type"]])


def collect_paint_triangles(doc: dict, binary: bytes):
    nodes, meshes = doc["nodes"], doc["meshes"]
    triangles = []
    def visit(node_index: int, parent: np.ndarray):
        node = nodes[node_index]
        world = parent @ matrix_for_node(node)
        if "mesh" in node:
            mesh = meshes[node["mesh"]]
            for primitive in mesh["primitives"]:
                if primitive.get("material") != PAINT_MATERIAL:
                    continue
                attrs = primitive["attributes"]
                if "TEXCOORD_0" not in attrs:
                    raise ValueError("Paint primitive has no TEXCOORD_0 UVs")
                positions = accessor(doc, binary, attrs["POSITION"]).astype(np.float64)
                uv = accessor(doc, binary, attrs["TEXCOORD_0"]).astype(np.float64)
                if "indices" in primitive:
                    indices = accessor(doc, binary, primitive["indices"]).reshape(-1).astype(np.int64)
                else:
                    indices = np.arange(len(positions), dtype=np.int64)
                if len(indices) % 3:
                    raise ValueError("Paint primitive index count is not a triangle list")
                for tri_idx in indices.reshape(-1, 3):
                    p = positions[tri_idx]
                    world_p = p @ world[:3, :3].T + world[:3, 3]
                    normal = np.cross(world_p[1] - world_p[0], world_p[2] - world_p[0])
                    norm = np.linalg.norm(normal)
                    if norm < 1e-10:
                        continue
                    normal /= norm
                    triangles.append((uv[tri_idx], world_p, normal))
        for child in node.get("children", []):
            visit(child, world)
    for root in doc["scenes"][doc.get("scene", 0)]["nodes"]:
        visit(root, np.eye(4, dtype=np.float64))
    if not triangles:
        raise ValueError(f"No primitives use paint material {PAINT_MATERIAL}")
    return triangles


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input_glb", type=Path, help="Original Porsche 930 GLB")
    parser.add_argument("output_png", type=Path, help="Output base-colour PNG")
    args = parser.parse_args()
    doc, binary = glb_chunks(args.input_glb.read_bytes())
    triangles = collect_paint_triangles(doc, binary)
    # Fill unused atlas space with the ruby base too: linear filtering at UV
    # island boundaries then stays in the body palette rather than black.
    pixels = np.empty((SIZE, SIZE, 3), dtype=np.uint8)
    pixels[:] = COLORS["ruby"]
    for uv, world_p, normal in triangles:
        points = np.column_stack((uv[:, 0] * (SIZE - 1), (1.0 - uv[:, 1]) * (SIZE - 1)))
        x0 = max(0, int(np.floor(points[:, 0].min())))
        x1 = min(SIZE - 1, int(np.ceil(points[:, 0].max())))
        y0 = max(0, int(np.floor(points[:, 1].min())))
        y1 = min(SIZE - 1, int(np.ceil(points[:, 1].max())))
        if x1 < x0 or y1 < y0:
            continue
        p0, p1, p2 = points
        denom = (p1[1] - p2[1]) * (p0[0] - p2[0]) + (p2[0] - p1[0]) * (p0[1] - p2[1])
        if abs(denom) < 1e-8:
            continue
        yy, xx = np.mgrid[y0:y1 + 1, x0:x1 + 1]
        xx = xx + 0.5
        yy = yy + 0.5
        w0 = ((p1[1] - p2[1]) * (xx - p2[0]) + (p2[0] - p1[0]) * (yy - p2[1])) / denom
        w1 = ((p2[1] - p0[1]) * (xx - p2[0]) + (p0[0] - p2[0]) * (yy - p2[1])) / denom
        w2 = 1.0 - w0 - w1
        inside = (w0 >= -1e-5) & (w1 >= -1e-5) & (w2 >= -1e-5)
        if not inside.any():
            continue
        pos = w0[..., None] * world_p[0] + w1[..., None] * world_p[1] + w2[..., None] * world_p[2]
        red = np.zeros(inside.shape, dtype=bool)
        cyan = np.zeros(inside.shape, dtype=bool)
        orange = np.zeros(inside.shape, dtype=bool)
        gold = np.zeros(inside.shape, dtype=bool)
        x, y, z = pos[..., 0], pos[..., 1], pos[..., 2]
        nx, ny = normal[0], normal[1]
        if abs(nx) > 0.48 and abs(float(world_p[:, 0].mean())) < 2.0:
            side = (np.abs(pos[..., 0]) > 0.66) & inside
            ribbon_center = 0.795 + 0.025 * np.sin(z * 0.85)
            cyan = side & (np.abs(y - ribbon_center) <= 0.035)
            orange = side & (np.abs(y - (ribbon_center - 0.075)) <= 0.030)
            gold = side & (np.abs(y - (ribbon_center - 0.132)) <= 0.009)
        if abs(ny) > 0.64:
            top = (np.abs(x) < 0.22) & (np.abs(z) > 1.52) & inside
            cyan |= top & (np.abs(x) >= 0.065) & (np.abs(x) <= 0.102)
            orange |= top & (np.abs(x) >= 0.125) & (np.abs(x) <= 0.155)
        region = pixels[y0:y1 + 1, x0:x1 + 1]
        region[inside] = COLORS["ruby"]
        region[cyan] = COLORS["turquoise"]
        region[orange] = COLORS["orange"]
        region[gold] = COLORS["gold_pinstripe"]
    image = Image.fromarray(pixels, mode="RGB")
    # PNG RGB is interpreted as sRGB by LumaTrace's base-colour texture path.
    image.save(args.output_png, format="PNG", optimize=True)
    print(f"wrote={args.output_png} size={SIZE}x{SIZE} triangles={len(triangles)} bytes={args.output_png.stat().st_size}")
    print("material=9:paint uv=TEXCOORD_0 format=RGB8-sRGB factor_when_applied=[1,1,1,1]")

if __name__ == "__main__":
    main()
