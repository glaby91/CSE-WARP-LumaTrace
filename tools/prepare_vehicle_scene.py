#!/usr/bin/env python3
"""Package a GLB vehicle as a LumaTrace single-object scene bundle.

The source GLB's geometry, accessors, node transforms, materials, and image
bytes are retained. Only embedded image bufferViews are moved to sidecar files
so the existing scene importer can address PBR texture files by path.
"""

from __future__ import annotations

import argparse
import json
import struct
import zipfile
from pathlib import Path

JSON_CHUNK = 0x4E4F534A
BIN_CHUNK = 0x004E4942
MIME_EXT = {"image/png": ".png", "image/jpeg": ".jpg", "image/webp": ".webp"}


def read_glb(path: Path) -> tuple[dict, bytes]:
    data = path.read_bytes()
    if len(data) < 20 or data[:4] != b"glTF":
        raise ValueError("Input is not a binary glTF (GLB).")
    version, total_length = struct.unpack_from("<II", data, 4)
    if version != 2 or total_length != len(data):
        raise ValueError("Expected a complete glTF 2.0 GLB.")
    doc, binary = None, None
    cursor = 12
    while cursor < len(data):
        size, kind = struct.unpack_from("<II", data, cursor)
        cursor += 8
        payload = data[cursor:cursor + size]
        cursor += size
        if len(payload) != size:
            raise ValueError("Truncated GLB chunk.")
        if kind == JSON_CHUNK:
            doc = json.loads(payload.rstrip(b" \0").decode("utf-8"))
        elif kind == BIN_CHUNK:
            binary = payload
    if doc is None or binary is None:
        raise ValueError("GLB must contain JSON and BIN chunks.")
    return doc, binary


def pack_glb(doc: dict, binary: bytes, path: Path) -> None:
    json_chunk = json.dumps(doc, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    json_chunk += b" " * ((-len(json_chunk)) % 4)
    binary += b"\0" * ((-len(binary)) % 4)
    total = 12 + 8 + len(json_chunk) + 8 + len(binary)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as out:
        out.write(struct.pack("<4sII", b"glTF", 2, total))
        out.write(struct.pack("<II", len(json_chunk), JSON_CHUNK))
        out.write(json_chunk)
        out.write(struct.pack("<II", len(binary), BIN_CHUNK))
        out.write(binary)


def export_scene(source: Path, output_dir: Path, scene_name: str) -> dict:
    doc, binary = read_glb(source)
    buffer_views = doc.get("bufferViews", [])
    image_views = set()
    extracted = []
    tex_dir = output_dir / "textures"
    tex_dir.mkdir(parents=True, exist_ok=True)

    for index, image in enumerate(doc.get("images", [])):
        if "bufferView" not in image:
            if "uri" not in image:
                raise ValueError(f"Image {index} has neither URI nor bufferView.")
            continue
        view_index = image["bufferView"]
        view = buffer_views[view_index]
        if view.get("buffer", 0) != 0:
            raise ValueError("Multiple GLB buffers are not supported by this packer.")
        start = view.get("byteOffset", 0)
        end = start + view["byteLength"]
        mime = image.get("mimeType")
        if mime not in MIME_EXT:
            raise ValueError(f"Unsupported image MIME type {mime!r} at index {index}.")
        filename = f"image_{index:02d}{MIME_EXT[mime]}"
        (tex_dir / filename).write_bytes(binary[start:end])
        image_views.add(view_index)
        image.pop("bufferView")
        image.pop("mimeType", None)
        image["uri"] = f"textures/{filename}"
        extracted.append({"index": index, "file": filename, "mimeType": mime,
                          "byteLength": end - start})

    # Compact the remaining geometry/index bufferViews without changing their
    # indices. Accessors therefore remain valid and binary geometry is intact.
    compact = bytearray()
    compact_views = []
    view_remap = {}
    for index, view in enumerate(buffer_views):
        if index in image_views:
            continue
        compact.extend(b"\0" * ((-len(compact)) % 4))
        start = view.get("byteOffset", 0)
        end = start + view["byteLength"]
        updated_view = dict(view)
        updated_view["byteOffset"] = len(compact)
        view_remap[index] = len(compact_views)
        compact_views.append(updated_view)
        compact.extend(binary[start:end])
    for accessor in doc.get("accessors", []):
        if "bufferView" in accessor:
            accessor["bufferView"] = view_remap[accessor["bufferView"]]
        sparse = accessor.get("sparse", {})
        for part in (sparse.get("indices", {}), sparse.get("values", {})):
            if "bufferView" in part:
                part["bufferView"] = view_remap[part["bufferView"]]
    doc["bufferViews"] = compact_views
    doc["buffers"][0]["byteLength"] = len(compact)

    glb_path = output_dir / "porsche_930.glb"
    pack_glb(doc, bytes(compact), glb_path)

    mesh_triangles = 0
    triangles_by_mesh = []
    primitive_count = 0
    material_ids = set()
    for mesh in doc.get("meshes", []):
        mesh_triangle_count = 0
        for primitive in mesh.get("primitives", []):
            if primitive.get("mode", 4) != 4:
                raise ValueError("Non-triangle primitive found; refusing to discard it.")
            primitive_count += 1
            attrs = primitive.get("attributes", {})
            if "POSITION" not in attrs or "NORMAL" not in attrs:
                raise ValueError("A primitive is missing POSITION or NORMAL.")
            material_ids.add(primitive.get("material", -1))
            if "indices" in primitive:
                accessor = doc["accessors"][primitive["indices"]]
                mesh_triangle_count += accessor["count"] // 3
            else:
                mesh_triangle_count += doc["accessors"][attrs["POSITION"]]["count"] // 3
        triangles_by_mesh.append(mesh_triangle_count)

    # Count mesh instances in the selected glTF scene, not only unique mesh
    # definitions. Some source nodes deliberately instance the same geometry.
    nodes = doc.get("nodes", [])
    roots = (doc.get("scenes", [{}])[doc.get("scene", 0)].get("nodes", [])
             if doc.get("scenes") else
             [i for i in range(len(nodes))
              if not any(i in parent.get("children", []) for parent in nodes)])
    visited_path = set()
    instance_count = 0

    def walk(node_index: int) -> None:
        nonlocal mesh_triangles, instance_count
        if node_index in visited_path:
            raise ValueError("Cycle detected in glTF node hierarchy.")
        visited_path.add(node_index)
        node = nodes[node_index]
        if "mesh" in node:
            mesh_triangles += triangles_by_mesh[node["mesh"]]
            instance_count += 1
        for child in node.get("children", []):
            walk(child)
        visited_path.remove(node_index)

    for root in roots:
        walk(root)

    scene = {
        "format": "lumatrace-scene-v1",
        "name": scene_name,
        "units": "source-glTF",
        "objects": [{
            "id": "porsche_930",
            "type": "gltf2-model",
            "file": "porsche_930.glb",
            "transform": "preserve-source-node-hierarchy",
        }],
        "environment": {"background": [0.035, 0.04, 0.05]},
        "import": {
            "triangles": mesh_triangles,
            "meshPrimitives": primitive_count,
            "meshInstances": instance_count,
            "materials": len(doc.get("materials", [])),
            "materialIdsUsed": sorted(material_ids),
            "textures": len(extracted),
            "textureFiles": [item["file"] for item in extracted],
            "preserveNormals": True,
            "preserveTexcoord0": True,
            "preserveMetallicRoughness": True,
            "preserveAlphaAndGltfExtensions": True,
        },
    }
    (output_dir / "scene.json").write_text(json.dumps(scene, indent=2) + "\n", encoding="utf-8")
    report = {
        "source": source.name,
        "scene": scene,
        "extractedImages": extracted,
        "sourceExtensionsUsed": doc.get("extensionsUsed", []),
        "sourceExtensionsRequired": doc.get("extensionsRequired", []),
        "outputBytes": glb_path.stat().st_size,
    }
    (output_dir / "import_report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--name", default="Porsche 911 (930) Turbo — LumaTrace")
    parser.add_argument("--zip", dest="zip_path", type=Path)
    args = parser.parse_args()
    report = export_scene(args.source, args.output, args.name)
    if args.zip_path:
        args.zip_path.parent.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(args.zip_path, "w", zipfile.ZIP_DEFLATED) as archive:
            for path in sorted(args.output.rglob("*")):
                if path.is_file():
                    archive.write(path, path.relative_to(args.output.parent))
    print(json.dumps({"scene": str(args.output / "scene.json"),
                      "triangles": report["scene"]["import"]["triangles"],
                      "textures": report["scene"]["import"]["textures"],
                      "materials": report["scene"]["import"]["materials"],
                      "zip": str(args.zip_path) if args.zip_path else None}, indent=2))


if __name__ == "__main__":
    main()
