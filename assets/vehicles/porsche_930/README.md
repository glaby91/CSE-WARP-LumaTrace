# Porsche 911 (930) Turbo, 1975

The repository now contains the complete 1K-texture asset used by LumaTrace:

- `scene/porsche_930.glb` — 241,260 triangles, 75 primitives, 14 materials.
- `scene/textures/image_00.png` through `image_26.png` — all 27 original model textures referenced by the GLB.
- `scene/import_report.json` — import counts and source glTF extensions.
- `scene/scene.json` — the LumaTrace scene manifest and the paint-material color override.

The scene manifest keeps the source model intact and applies the custom ruby-red paint texture with turquoise and orange accents to the material named `paint`. That override is stored separately at `scene/textures/porsche_930_ruby_turquoise_orange.png`; it uses the model's UV0 coordinates. No road, ground plane, or other props are included.

## Source and attribution

Karol Miklas / **Lionsharp Studios**, *1975 Porsche 911 (930) Turbo*: [Lionsharp Studios model page](https://www.lionsharpstudios.com/portfolio/1975-porsche-911-930-turbo/) · [Sketchfab listing](https://sketchfab.com/3d-models/free-1975-porsche-911-930-turbo-8568d9d14a994b9cae59499f0dbed21e).

The source listing identifies the model as Creative Commons Attribution. Retain this attribution when redistributing the source model and check the original listing for the applicable license terms.

## Use

Open `scene/scene.json` with LumaTrace. The loader reads the GLB, its original external textures, and the manifest's separate paint override. The source GLB preserves its node hierarchy, geometry buffers, normals, UVs, metallic-roughness data, alpha modes, and glTF extensions.
