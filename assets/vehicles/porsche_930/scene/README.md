# Porsche 930 LumaTrace scene

This folder contains a single-car scene with a neutral background. It has no road, ground mesh, or extra props.

- `scene.json` points to `porsche_930.glb` and applies the ruby-red paint texture with turquoise and orange accents to the material named `paint`.
- `porsche_930.glb` contains the vehicle geometry and glTF material data; its 27 image maps are external files in `textures/`.
- `textures/image_00.png` … `textures/image_26.png` are the original maps extracted from the source GLB.
- `textures/porsche_930_ruby_turquoise_orange.png` is the custom paint map used by the manifest override.
- `import_report.json` records the geometry and import counts.

The model has 241,260 triangles, 75 primitives, and 14 materials. The manifest override does not modify the source GLB: it uses the model's existing UV0 coordinates and keeps the other material properties.

Source: Karol Miklas / Lionsharp Studios, *1975 Porsche 911 (930) Turbo*. See the attribution and license note in `../README.md`.
