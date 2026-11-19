# Textures in LumaTrace ray reflections

The Direct3D 11 compute pass now samples a hit material's base-colour map for reflected rays. This is separate from assigning a new map to the Porsche GLB.

## Hit sampling path

1. The Möller–Trumbore hit test returns the triangle's barycentric coordinates.
2. The shader interpolates `uv0`, `uv1`, and `uv2` from the hit triangle.
3. The triangle material index selects a layer in the base-colour `Texture2DArray` bound at `t6`.
4. The sampled sRGB texel is multiplied by the material's glTF `baseColorFactor` before it contributes to reflected colour.

The renderer builds that array from the same source images used by raster shading, in sRGB, resampled to 512 × 512 for the reflection pass. This uses shader-model-5-compatible dynamic layer selection and supports up to 64 materials. Shadow rays remain geometric visibility tests; their occlusion does not depend on surface colour. Alpha-mask transparency during ray hits remains outside this change.

The Porsche scene manifest assigns `textures/porsche_930_ruby_turquoise_orange.png` to the material named `paint` through `materialOverrides`. The loader appends the sidecar image to the scene texture list, preserves the GLB's metallic, roughness, and extension values, and sets `baseColorFactor` to white. This override uses the model's original `TEXCOORD_0` and leaves the GLB unchanged.
