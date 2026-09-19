# v4 Roadmap

- Build the real Scope window, a planetarium view with a circular viewport like looking through a telescope, instead of the inline wireframe cone that currently lives in main, with mouse dragging and dedicated controls for az/el.

- Make the LEO/HEO/GEO and trails checkboxes in the scope sidebar actually do something, right now they are set in the tool panel but never read by any renderer, so they are dead prefs.

- Wire the orbit-arch renderer that already exists but is never called (draw_satellite_orbit_arch) into the scope, and add lock-on so az/el auto-track the selected object.

- Keep beam narrowing in the scope so you can examine satellites that sit close together, and highlight favorites so they stand out among all the active sats.

- Add a real rotator settings window for host, port, formats, protocols and park position, the tool panel has no inputs for any of these today.

- Hook up the rotator overlay draws (RotatorDrawScopeOverlay and RotatorDrawPolarOverlay) that are declared but never called, so the scope and polar plot show which way the rotator is pointing.

- Add proper manual az/el control plus lock-on, and prediction-driven auto tracking that predicts AOS, aligns the rotator, follows the pass, then parks.

- Finish the Doppler panel, the export CSV button body is literally empty, there is no graph and no preset band buttons, only a frequency input and a pass handoff from a right-click.

- Keep Doppler selective and only show it when asked, tied to the selected pass, since it is not a universally needed metric.

- Make the polar plot show the nearest upcoming pass by default unless the user picks a different one, and clicking a satellite should immediately show its current or imminent path.

- Add drag-to-scrub on the polar plot so you can drag the dot and simulate the satellite moving over time, then clean up the surrounding text so it stops looking rough.

- Re-implement the camera auto-rotate toggle that aligns the terminator vertically with the seasons, it is not just disabled, it is completely gone from the code and settings, so build it from scratch.

- Replace the glitchy sun that is drawn as a dot lost inside the skybox with something more photorealistic.

- Redesign the time control bar, it technically works but the whole thing needs a UX overhaul.

- Review and revamp all themes, default, amber and daylight, for readability, aesthetics and general appeal.

- Add the Keplerian fast path to cut per-frame SGP4 and GPU calls, it does not exist yet, all we have is the orbit cache that bakes paths into a vertex buffer.

- Finish eliminating per-frame allocations in the render loop, mostly done but the adaptive orbit cache resolution heuristic is stubbed with unused params, so make that real.

- Rework the backend for better long-term accuracy, atmospheric drag is already fed into SGP4 through B*, but the dev wants it hardened and actually demonstrated to be better.

- Consolidate the long wall of render toggles for earth texture, nightlights, clouds and scattering into cleaner no-bullshit preset views instead of a dozen checkboxes.

- Render data-age badges, the per-source 2h ago amber/red state and a stale badge on sat info, the backend fields already exist but the UI never draws them.

- Hook validate_themes into the Makefile and CI, widen the CI branch gating that currently only fires on extremelywip, and do the final docs pass on the README feature list and CREDITS.

- Land the scope and rotator as dedicated windows rather than squeezing them into the existing sidebar layout, since the left and right panels are done and meant to stay untouched.