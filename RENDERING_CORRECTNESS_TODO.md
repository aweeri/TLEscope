# Rendering Correctness Audit

- Basis: `aweeri/TLEscope:extremelywip` @ `0edc53cd28291482ecc027a9d0dab542bf4b52da`.

## P1
- 2D satellite picking tests only the base map coordinate; rendered wrap copies at `x +/- map_w` can be visible but not hoverable/clickable.
- Future-orbit sunlight coloring uses one Sun direction from the current epoch for every future sample. Compute it per sample or explicitly document the coloring as approximate.

## P2
- Add regression tests for antimeridian wrap/seams, polar coverage and 3D far-side occlusion.
- Exercise high-DPI/UI scaling and min/max zoom for icon size, line width, labels and picking radius.
- Keep coverage/orbit LOD visual-only; it must not reduce selected-target propagation/tracking accuracy.
