# Porsche 930 LumaTrace scene bundle

This bundle contains one vehicle object and a neutral background. It contains no road, ground mesh, or extra props.

- `scene.json`: LumaTrace scene descriptor with one `gltf2-model` object.
- `porsche_930.glb`: glTF 2.0 vehicle geometry/material data; embedded image data has been moved to external files.
- `textures/`: all 27 source images, byte-for-byte extracted from the input GLB.
- `import_report.json`: source extensions and conversion counts.

The source asset is the 1K-texture variant of the 1975 Porsche 911 (930) Turbo by Karol Miklas / Lionsharp Studios. The Sketchfab listing states Creative Commons Attribution; keep the attribution in `../README.md` with redistributions.

The geometry buffers, node transforms, normals, UV0, metallic-roughness material definitions, alpha modes, and glTF material extensions are retained. LumaTrace needs a scene-loader adapter for the `gltf2-model` descriptor before the application can open this scene; the V17.551 `m3d-scene-v16` loader does not consume this descriptor directly.
