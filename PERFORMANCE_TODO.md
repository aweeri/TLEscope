# Performance Audit

- Basis: `aweeri/TLEscope:extremelywip` @ `a5cf0d753868ce5b2dffca204470613d76cc6ba8`.

## P1
- Benchmark 1, 100, 1k, 5k and 15k active satellites in 2D/3D with labels and coverage; record frame time and RAM before tuning.
- Current positions are propagated for every active satellite every frame; separate propagation cadence from render cadence while always prioritizing selected/tracked targets.
- Satellite Manager rebuilds a favorites-first list with two full catalogue scans, repeated linear favorite lookups and no `ImGuiListClipper`; cache the filtered order and clip visible rows.
- Label generation scans all active satellites and builds candidates before applying its display cap; reduce candidates before expensive layout.
- 2D coverage traverses all active satellites; cull earlier and measure before adding more LOD complexity.
- 2D future ground tracks propagate many samples for selected/favorite/dimmed tracks every frame; cache or throttle track generation.
- Each async fetch result allocates a full `MAX_SATELLITES` buffer; reuse or right-size result storage.
- All-satellite and favorites pass prediction is brute-force and synchronous; move it off the UI thread and support cancellation/bounds.

## P2
- Keep the current adaptive orbit-detail logic, but tune it from measurements rather than satellite-count thresholds alone.
