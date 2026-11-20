# LumaTrace third optimization campaign — four measured passes

Date: 2026-11-20  
Branch: `codex/lumatrace-opt-4passes-r3`  
Scene: Porsche 930, 241,260 triangles.

## Method

Local C++ simulator based on the Porsche triangle set. Twelve repetitions per variant were interleaved in randomized order. Timings below are median CPU BVH build times; all variants used the same deterministic camera rays. The hit signature was identical for every candidate: `12990454221687972218`. This validates the sampled primary intersections, not a GPU timing run or pixel-by-pixel image comparison.

## Passes

| Pass | Candidate | Median BVH build | Decision |
|---|---|---:|---|
| 1 | Reserve 75% of binary-node count for the final BVH4 vector | 280.52 → 276.23 ms (−1.5%) | Keep; also avoids vector growth on this scene. |
| 2 | Reserve 100% of binary-node count | 280.52 → 289.36 ms (+3.2%) | Reject; slower and holds more capacity than needed. |
| 3 | Fixed child arrays plus 75% node reserve | 280.52 → 278.53 ms (−0.7%) | Reject; too small to separate from run-to-run variation. |
| 4 | Fixed child arrays without node reserve | 280.52 → 282.55 ms (+0.7%) | Reject; no measured gain. |

The measured binary tree contains 112,131 nodes and the emitted BVH4 contains 82,794. A 75% reserve gives capacity for 84,098 nodes, enough for this scene; the unreserved vector typically grows to capacity 131,072. The change affects allocation only. BVH topology, child order, intersection counts, and the sampled hit signature are unchanged.

## Applied

Only pass 1 is integrated in `src/render/scene_bvh.cpp`. The Windows DX11 workflow compiles the branch; hardware frame-time still needs a run on the HD 620.
