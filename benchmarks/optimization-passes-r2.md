# LumaTrace second optimization campaign — measured passes

Date: 2026-11-20  
Branch: `codex/lumatrace-opt-4passes-r2`  
Scene: Porsche 930, 241,260 triangles; 14,400 deterministic primary rays plus representative shadow rays.

## Measurement method

A local C++ simulator builds the scene BVH and runs the same ray batch for each variant. It records median CPU build and traversal time over repeated runs. The signature over primary camera-ray hit IDs and hit distances was identical for all variants (`12990454221687972218`). These are CPU simulation measurements; they are not Direct3D GPU frame times, and the signature is not a pixel-by-pixel rendered-image comparison.

## Four passes

| Pass | Variant | Median build | Median trace | Nodes | AABB tests | Triangle tests | Decision |
|---|---|---:|---:|---:|---:|---:|---|
| 1 | BVH temporary-vector reserve: 2N/leaf → 3N/leaf (capped) | 276.31 → 258.44 ms | unchanged | 112,131 binary | unchanged | unchanged | **Keep**; 6.5% faster build in the simulator, same hierarchy and hit signature, lower peak vector capacity on this scene. |
| 2 | SAH bins: 12 → 16 | 269.96 → 286.77 ms | 9.75 → 9.60 ms | 82,794 → 82,955 | 398,653 → 406,565 | 67,176 → 65,809 | Reject; tiny traversal change is within noise, while build and box tests rise. |
| 3 | Binary leaf size: 6 → 4 | 269.96 → 311.43 ms | 9.75 → 10.14 ms | 82,794 → 112,436 | 398,653 → 409,300 | 67,176 → 55,028 | Reject; fewer triangle tests do not compensate for more nodes and boxes in measured traversal time. |
| 4 | BVH4, 16 bins and leaf size 8 | 269.96 → 270.80 ms | 9.75 → 9.93 ms | 82,794 → 62,975 | 398,653 → 388,697 | 67,176 → 86,329 | Reject; fewer nodes/boxes but substantially more triangle tests and no measured traversal gain. |

## Applied change

Only pass 1 is applied to `src/render/scene_bvh.cpp`. The reserve estimate is capped at the maximum size of a full binary hierarchy so small leaf-size requests do not allocate more than that bound. All other experiments remain unpromoted.

The Windows DX11 workflow is used to verify that the source still compiles. A run on the target HD 620 is still needed to measure actual GPU frame-time impact.
