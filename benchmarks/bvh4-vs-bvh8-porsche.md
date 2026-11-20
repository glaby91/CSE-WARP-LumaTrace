# LumaTrace optimization pass 4 — BVH4 vs BVH8

Date: 2026-11-20  
Branch: `codex/lumatrace-opt-4passes`  
Reference scene: Porsche 930 GLB, 241,260 triangles.

## Experiment

A standalone CPU traversal simulator built the same scene into a 12-bin SAH hierarchy, collapsed once to BVH4 and once to BVH8, and traced the same deterministic set of 14,400 rays. This is a comparative structural experiment, not a GPU benchmark and not an image-equivalence proof.

| Metric | BVH4 | BVH8 | Change |
|---|---:|---:|---:|
| Nodes | 82,794 | 72,803 | -12.1% |
| AABB tests | 398,653 | 530,208 | +33.0% |
| Triangle tests | 67,176 | 66,774 | -0.6% |
| CPU build time in this simulator | 296.3 ms | 285.3 ms | -3.7% |

Approximate GPU node storage at 56 bytes/node for BVH4 and 72 bytes/node for BVH8 would be 4.64 MB and 5.24 MB respectively. The wider nodes reduce node count but increase estimated storage by about 13%.

## Decision

Do not replace BVH4 with BVH8 in this pass. The small reduction in triangle tests does not offset the additional AABB tests and estimated node bandwidth. The simulator is useful for rejecting this candidate, but only a GPU run on the target Intel HD 620 can establish actual frame-time impact.

This pass preserves the existing BVH4 renderer and records why BVH8 was not promoted. The other three optimization commits on this branch contain the implemented changes and are built by the Windows DX11 workflow.
