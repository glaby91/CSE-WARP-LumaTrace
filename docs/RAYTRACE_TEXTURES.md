# Textures in LumaTrace ray reflections

The Direct3D 11 compute pass now samples a hit material's base-colour map for reflected rays. This is separate from assigning a new map to the Porsche GLB.

## Hit sampling path

1. The Möller–Trumbore hit test returns the triangle's barycentric coordinates.
2. The shader interpolates `uv0`, `uv1`, and `uv2` from the hit triangle.
3. The triangle material index selects the base-colour SRV bound at `t6` plus that index.
4. The sampled sRGB texel is multiplied by the material's glTF `baseColorFactor` before it contributes to reflected colour.

The renderer binds the same base-colour SRVs used by raster shading. It supports up to 64 materials in this compute path. Shadow rays remain geometric visibility tests; their occlusion does not depend on surface colour. Alpha-mask transparency during ray hits remains outside this change.

The Porsche ruby/turquoise/orange texture remains a standalone design asset. When it is later assigned to material `paint`, keep the original UV set `TEXCOORD_0` and set `baseColorFactor` to white so the texture colours are preserved.
