# TLEscope Roadmap (v4.0.0)

This is the working plan for the v4.0.0 release. Each section is a self-contained
work item, tagged with a status so it is clear what is done, what is in progress,
and what has not been started. Where it helps, items include acceptance criteria
so a feature is only considered done when it actually behaves the way a user
would expect.

The point of v4.0.0 is not just to finish the remaining features. It is to make
the whole app feel polished, so every new tool looks, behaves, and
persists like it belongs.

Status markers used below:

- `[x]` done
- `[~]` in progress
- `[ ]` planned

---

## 1. Data sources and orbital backend

### 1.1 [x] Multi-source pulling

- Retlector groups, CelesTrak groups, custom URLs, and pasted data can all be
  added to a selection and pulled in one action.
- Pulling clears the slate and re-fetches every selected source (see
  `DrawPanelDataSources` in `src/ui/panels.cpp`).
- Sources are tagged with provenance (`data_meta.source_name`) so the Satellite
  Info panel can show where each object came from.

### 1.2 [~] Data staleness enforcement

- A "data outdated after" threshold exists in Settings, and a warning modal
  (`DrawDataWarning`) fires when loaded data is older than the threshold.
- Gap: the threshold is only checked by a modal at startup. It is not surfaced
  per source in the Data Sources panel, so there is no way to see which source
  is stale at a glance.
- Acceptance criteria:
  - Each selected source row shows its age (for example "2h ago" or "3d ago")
    and turns amber or red when past the threshold.
  - Pulling a source updates its `fetch_time` and clears the stale flag.
  - A notification fires when data is refreshed (see section 8).

### 1.3 [~] SGP4 backend review and Keplerian fallback

- Current state: `calculate_position()` calls `sgp4()` on every invocation.
  Orbit caches (`update_orbit_cache`) reduce per-frame SGP4 calls for the 3D and
  2D orbit paths, but pass prediction, polar traces, Doppler sweeps, and the
  scope view all call `calculate_position()` in tight loops. That is a lot of
  SGP4.
- Goal: fewer SGP4 calls, more reliance on cheap Keplerian propagation, and
  periodic SGP4 correction for drift and atmospheric drag.
- Plan:
  1. Add a Keplerian fast path (`calculate_position_kepler`) that propagates
     from mean elements using the stored `mean_motion`, `mean_anomaly`, and so
     on. This is accurate enough for prediction (passes, Doppler, polar traces)
     and far cheaper than SGP4.
  2. Keep SGP4 as the authoritative position for the current-time snapshot
     (`current_pos`) and for rendering, but re-run it only when the satellite has
     drifted past `orbit_cache_drift_threshold_km` or when the epoch advances
     beyond a configurable interval.
  3. Periodically re-sync the Keplerian model to SGP4 output so drag and
     perturbations accumulate correctly over long time spans.
- Acceptance criteria:
  - Pass prediction over 24h for all active satellites completes without
    noticeable UI stutter.
  - Long-range predictions (days out) still converge to SGP4-accurate AOS/LOS.
  - A debug counter (or the stats overlay) can show SGP4 calls per frame
    trending down.

### 1.4 [ ] Data age and epoch health in Satellite Info

- The Satellite Info panel already shows source and format. Add:
  - Epoch age (how old the orbital elements are).
  - Data age (how long since the source was fetched).
  - A subtle "stale" badge when either exceeds the configured threshold.

---

## 2. Satellite passes

### 2.1 [x] Two pass modes

- Single-satellite mode: compute passes for the currently selected and active
  satellite.
- All-active mode: compute passes for every enabled satellite in the manager.
- Configurable time span in hours, defaulting to 24h.
- `CalculatePasses(sat, start_epoch)` already supports both a single sat
  (`sat != NULL`) and all-active (`sat == NULL`) path, but the UI only exposes
  the "Calculate Passes" button and hardcodes the window. Wire the mode and
  duration into the panel, make the listings actively update to stay current.

### 2.2 [x] Richer pass entries

- Problem: entries currently show only `name - El: X`. When two passes share
  the same max elevation, ImGui sees duplicate IDs, which causes selection bugs. Additionally, the calculate button is not the intended way for it to work; the passes should update in real time. 
- Fix: give every pass a unique ID and show:
  - Satellite name.
  - Start (AOS) time and end (LOS) time.
  - Max elevation.
  - A UTC/local toggle (see section 5).
- Acceptance criteria: every row has a unique ImGui ID (for example derived
  from the pass index plus the AOS epoch), and clicking a pass always selects
  the intended one.

### 2.3 [x] Pass progress bar

- For each pass, show a progress bar that reflects where we are in the AOS to
  LOS window right now.
- If the pass is currently happening, render it as ongoing with a green bar over
  the home location.
- Show start time, end time, and elevation alongside the bar.

### 2.4 [x] Click to open in Polar Plot

- Clicking a pass opens that pass's sky path in the Polar Plot view. The
  `path_pts` are already computed and stored on `SatPass`.

### 2.5 [x] Pass to Doppler handoff

- A pass row offers an "Analyze in Doppler" action that preloads the Doppler
  panel with that satellite and pass window.

### 2.6 [x] Pass list height

- The passes panel currently takes over the whole sidebar. Cap it to roughly
  one-third to one-fourth of the sidebar height (same approach as the Satellite
  Manager list), with an internal scroll region.

---

## 3. Polar plot

### 3.1 [~] Finish the full polar plot

- The polar grid (rings, crosshairs, cardinal labels) and the live satellite
  dot plus future trace already render in `DrawPanelPolarPlot`.
- Remaining work:
  - Render the selected pass path (from `SatPass.path_pts`) as a distinct
    highlighted arc.
  - Show AOS/LOS markers and the max-elevation point on the path.
  - Show the current time position along the pass.
  - Make it clickable to steer the rotator (see section 6).
  - Lunar mode already exists. Keep it, but make it visually distinct from
    satellite mode.

### 3.2 [ ] Polar plot sizing

- The plot is currently capped at 360px. Let it scale with the available
  sidebar width, and consider a floating resizable window option for users who
  want a larger sky view.

---

## 4. Doppler analysis

### 4.1 [~] Finish the Doppler calculator

- `calculate_doppler_freq()` exists and works off range rate.
- Remaining work:
  - A graph of perceived frequency over the pass window (time on X, Hz on Y).
  - Show the baseline frequency, max and min shift, and the zero-shift (TCA)
    point.
  - Export CSV. The button exists but the handler is a TODO. Implement it to
    write time, frequency, range, and range-rate rows at the configured
    resolution.
- Acceptance criteria:
  - Export CSV produces a valid file with a header row and one row per sample.
  - The graph updates when the pass or frequency changes.

### 4.2 [ ] Doppler presets

- Quick-fill common downlink bands (2m, 70cm, 23cm, S-band, X-band) so users do
  not have to type raw Hz.

---

## 5. Time and local/UTC display

### 5.1 [x] Local time support

- Problem: all time strings use `gmtime()`, so everything is UTC. Users want a
  local/UTC toggle.
- Plan:
  - Add a local/UTC toggle (persisted) that switches all displayed times
    (passes, satellite info, bottom bar, stats overlay, log timestamps).
  - Default to the system local timezone (no per-app timezone selection).
  - The bottom-bar time setter round-trips local fields back to a UTC epoch so
    the simulation stays UTC.
- Done:
  - `use_local_time` config field (default `true`), parsed/saved in
    `settings.json`.
  - `SetUseLocalTime()`/`GetUseLocalTime()` + local-aware
    `epoch_to_datetime_str()`/`epoch_to_time_str()` in `astro.cpp` (compact
    `UTC+0200` offset label instead of the long Windows timezone name).
  - `epoch_to_local_fields()`/`local_fields_to_epoch()` for the time-setter
    round-trip.
  - "Use Local Time" toggle in Settings → Display, applied live and persisted.
  - Passes AOS/LOS now show formatted datetimes instead of raw epoch floats.
  - Backend (SGP4, GMST, sun/moon, epoch conversions) remains UTC-only.
- Acceptance criteria:
  - Toggling local/UTC updates every time display in the app immediately.
  - The chosen mode survives a restart.

### 5.2 [ ] Refine time controls

- The bottom bar already has pause/resume, speed, reset, and a time setter.
- Remaining:
  - Auto-warp to event (next AOS, next pass, next sunrise). The
    `is_auto_warping` and `auto_warp_target` fields exist in `UIContext` but are
    not surfaced in the UI.
  - A status indicator near the time controls showing current sim time versus
    real time, and whether time is paused or warped.

---

## 6. Antenna rotator control

### 6.1 [~] Finish the rotator

- Current state: TCP connect/disconnect, poll, raw command send, park, and
  auto-steer logic exist in `src/io/rotator.cpp`. The panel exposes connect,
  status, az/el, auto-steer, poll, and raw commands.
- Gaps:
  - The rotator settings (host, port, get/set format, custom command, park
    position, lead time, steer mode) are stored in a static struct but are not
    editable in the UI and not persisted.
  - The declared window and overlay drawing functions (`RotatorDrawWindow`,
    `RotatorDrawScopeOverlay`, `RotatorDrawPolarOverlay`,
    `RotatorDrawConnectedItem`) are not implemented.
  - Auto-steer targets the scope az/el or the selected pass, but there is no
    clear visual feedback about what it is steering toward.
- Plan:
  1. Build a rotator settings UI (in the rotator panel and/or Settings modal)
     for host, port, protocols (GS-232, EasyComm, custom), get/set formats,
     park position, lead time, and steer mode.
  2. Persist all rotator settings in `settings.json`.
  3. Implement the connected overlay (a compact az/el readout plus status) and
     the polar/scope steering overlays so the user can see the target.
  4. Add connect/disconnect notifications (see section 8).
- Acceptance criteria:
  - A user can configure a rotator, connect, see live az/el, and have it track
    a selected pass or the scope beam.
  - Settings survive a restart.

---

## 7. Scope view

### 7.1 [~] Redesign the scope view

- Current state: the scope is a 3D cone drawn from the home location
  (`show_scope` in `main.cpp`) plus a `DrawPanelScope` with az/el/beam sliders
  and LEO/HEO/GEO/trails checkboxes. The old circular radar scope view is gone.
- Goal: a proper radio-telescope / spectrum-analyzer scope that shows all
  satellites in the sky, lets the user target and lock onto one, and reveals
  what is inside the beam (potential interference).
- Plan:
  1. Scope window: a large circular sky view, not squeezed into the sidebar.
     Reuse `draw_satellite_orbit_arch()` to render orbit arcs inside the scope.
  2. Targeting and locking: click a satellite in the scope to target it; lock
     keeps the beam on it as it moves.
  3. Beam range visualization: highlight satellites inside the current beam
     cone so the user can spot interference sources.
  4. Layer toggles: LEO, MEO, HEO, GEO orbit trails (the
     `scope_show_leo/heo/geo/trails` flags already exist).
  5. Rotator integration: the scope az/el drives the rotator (already partially
     wired via `ROTATOR_STEER_SCOPE`).
- Acceptance criteria:
  - The scope view is a large, legible window.
  - Clicking a satellite in the scope selects it app-wide.
  - Satellites inside the beam are visually highlighted.

### 7.2 [ ] Scope sizing

- The scope should not be forced into the sidebar. Provide a floating resizable
  scope window (like the old circular scope, but bigger and modern), plus a
  compact sidebar control panel.

---

## 8. Notifications

### 8.1 [x] Toast and notification system

- Goal: a KSP-style notification system, small toasts in the top-right corner
  (the stats overlay is top-left), for:
  - Settings applied or saved.
  - Data updated or sources pulled.
  - New source discovered.
  - Rotator connect and disconnect.
  - Time warp changes.
  - Data staleness warnings.
  - Pass AOS/LOS events (optional).
- Plan:
  - Add a small notification module (a ring buffer of icon, message, level, and
    timestamp) with a timed fade-out.
  - Render it via ImGui in the top-right, stacked, each auto-dismissing after a
    few seconds.
  - Wire it into the existing event points (pull complete, rotator connect,
    settings save, and so on).
- Done:
  - New `src/ui/notifications.{h,cpp}` module: a fixed ring buffer of
    `{level, icon, message, age}` entries with `NotifyPush()`/`NotifyUpdate()`/
    `DrawNotifications()`. Toasts stack top-right, auto-dismiss after a few
    seconds with a timed fade-out, and never block interaction (no-focus,
    no-input windows).
  - Theme-aware: `notif_info/success/warning/error/bg/border` colors added to
    the `Theme` struct, defaults + JSON parsing in `theme.cpp`, and keys added
    to every `themes/*/theme.json`.
  - Rendered at the end of `DrawGUI()` so toasts appear on top of all panels.
  - Wired into: settings save, data pull complete, new source discovered,
    rotator connect/disconnect, time warp/pause/reset, and data staleness.
  - A "Notifications" section in Settings with a master toggle and per-category
    toggles (Info / Success / Warnings / Errors), persisted via the generic
    `tool_settings` store.
- Acceptance criteria:
  - Notifications appear and fade without blocking interaction.
  - They are theme-aware and respect UI scale.

---

## 9. Layers

### 9.1 [x] Apoapsis and periapsis text labels

- The apoapsis and periapsis markers now show a small text label with the
  altitude above sea level (km) next to each marker (for example `A 35786 km`
  and `P 200 km`), drawn in `main.cpp` using the `calc_apogee_km` and
  `calc_perigee_km` helpers.

### 9.2 [ ] Van Allen belts layer

- Add a Van Allen belts layer showing both the inner and outer belt as
  translucent band overlays around Earth.

### 9.3 [ ] Magnetosphere layer

- Add a magnetosphere layer, a simplified dipole field visualization.

### 9.4 [ ] Sensor swath visualizer

- A tool that draws the ground footprint of a selected satellite's sensor on
  the map, driven by a user-entered field of view (FOV).
- The swath can be shown as a scan line, a square, or a circle, depending on
  the sensor shape the user picks.
- Plan:
  - Add a compact "Sensor Swath" block in the Layers panel: a FOV input (in
    degrees), a shape selector (line / square / circle), and a toggle.
  - Keep it to a few controls so it does not take much space. The FOV input and
    shape combo fit on one or two rows, and the toggle reuses the existing
    checkbox row pattern.
  - Render the swath as a translucent overlay on the ground track (2D) and on
    the globe (3D), centered on the selected satellite's sub-satellite point.
  - The footprint math can reuse the existing line-of-sight / coverage helpers
    and the `footprint_bg` / `footprint_border` theme colors.
- Acceptance criteria:
  - Entering a FOV and picking a shape draws the swath around the selected
    satellite.
  - The swath updates as the satellite moves and as the FOV changes.
  - The whole control fits comfortably in the Layers panel without crowding it.

### 9.5 [ ] Layer panel polish

- The Layers panel currently lists toggles. Add the new layers (Van Allen,
  magnetosphere, sensor swath) with icons and tooltips, consistent with the
  existing rows.

---

## 10. Markers and home location

### 10.1 [x] Unified locations system

- Markers and the home location are now one unified, persisting "Locations"
  list in `settings.json` (loaded/saved in `config.cpp`).
- The Settings modal has a Locations section with add, edit (name, lat, lon,
  alt), and remove affordances on every row, plus a "Pick on Map" flow that
  updates the location being edited.
- The hardcoded Cape Canaveral default is gone; first run creates a single
  neutral "Home" location at 0,0.
- Legacy `home_location` and `markers` entries from older settings files are
  migrated into the unified list on load.
- Acceptance criteria met: the Locations list is the single source of truth
  for both markers and home, and locations survive a restart.

### 10.2 [x] Home designation

- "Home" is a flag (`is_home`) on one entry in the unified Locations list
  (`location.cpp`).
- Set home by clicking the house button on a location row in Settings →
  Locations; exactly one location is home at a time (`SetHomeLocation` moves
  the flag).
- All consumers (passes, scope, polar plot, ground tracks, rotator) read the
  home location from the unified list via `GetHomeLocation()`.

---

## 11. Persistence

### 11.1 [x] Persist critical settings

- Goal: whatever the user sets should survive a restart, minimizing setup time.
- Already persisted: theme, window size, FPS, UI scale, display toggles, custom
  sources, retlector groups, custom entries, stale threshold, unified locations
  (markers + home), local/UTC preference, and sidebar/panel layout.
- Done:
  - Active satellite selection. `SaveSatSelection()`/`LoadSatSelection()` now
    persist the set of active NORAD ids to `sat_selection.json` and re-apply
    them to freshly loaded satellites on startup.
  - Data source selections. The shopping-cart selections in the Data Sources
    panel (`g_data_selections`) are persisted to `data_selections.json` via
    `SaveDataSelections()`/`LoadDataSelections()`.
  - Rotator settings (see section 6). A `RotatorSettings` struct is stored in
    `settings.json` and applied to the live rotator via
    `RotatorSaveSettings()`/`RotatorLoadSettings()`.
  - Pass settings (min elevation, time span, mode) remain to be persisted.
- Acceptance criteria:
  - Restarting the app restores the user's active satellites, data-source
    selections, rotator config, and time preference.

---

## 12. UI and theming polish

### 12.1 [x] Sidebar notch visibility

- Problem: the sidebar show/hide notches exist but are not visually apparent,
  and dragging the resize strip can accidentally snap-hide the sidebar.
- Plan:
  - Make the notches theme-aware and visually distinct (icon plus hover state).
  - Make snap-hide deliberate, for example only when dragging past the screen
    edge with a clear affordance, not by accident.
  - Ensure the notch colors come from the theme, not hardcoded grays.
- Done:
  - Notch background, border, and chevron icon colors now come from the theme
    (`ui_primary`, `window_border`, `text_secondary`) with hover states.
  - The resize strip, drag grip, and reorder drop indicator are theme-aware
    (`window_border`, `ui_accent`, `text_secondary`).
  - Snap-hide is deliberate: it only triggers when the cursor is dragged fully
    past the screen edge (not merely within 8px of it).

### 12.2 [x] Theme review

- Audit all hardcoded colors in the UI (notches, polar grid, log rows, stats
  overlay, scope) and route them through the theme where possible.
- Verify the theme applies consistently across the nav bar, sidebars, bottom
  bar, modals, and the new tools (scope, polar, Doppler, notifications).
- Done:
  - Polar plot grid, rings, labels, background, satellite dot, trace, and
    labels now use theme colors (`text_secondary`, `window_bg`, `ui_accent`).
  - Log alternating row backgrounds use the theme `frame_bg`.
  - The settings modal remove-button uses the theme `notif_error` color.
  - The settings modal dim overlay uses the theme `modal_dim` color.
  - The stats overlay already used theme colors; marker labels remain white
    deliberately for legibility against the map.

### 12.3 [x] Settings expansion

- Add the new settings surfaced by the features above:
  - Local/UTC default.
  - Rotator settings.
  - Marker management.
  - Notification toggles (opt-out for TRXDB, and so on).
  - Pass defaults (min elevation, default time span).
  - Scope defaults (beam width, default layers).
- Done:
  - Notification toggles (master + per-category) in Settings → Notifications,
    persisted via `tool_settings`.
  - Pass defaults: min elevation and time span sliders in the Passes panel,
    persisted via `tool_settings` and consumed by `CalculatePasses()`.
  - Scope defaults: beam width and layer toggles persisted via `tool_settings`.
  - Local/UTC, rotator settings, and marker management were already surfaced.

---

## 13. TRXDB integration

### 13.1 [ ] TRXDB transponder database (nice to have, opt-out)

- Goal: show transponder info for a selected satellite, including an image (if
  available) and transponder details such as notes and activation status.
- Design notes:
  - Simpler API than the main data sources. Fetch on demand per satellite.
  - Opt-out. Disabled by default in Settings, so users who do not want network
    calls to TRXDB can turn it off.
  - UX: a collapsible "Transponder" section in the Satellite Info panel, or a
    dedicated small panel.
  - Needs careful thought about caching (do not hammer the API), offline
    behavior, and how it looks in the sidebar.
- Acceptance criteria:
  - Selecting a satellite shows its transponder info when available.
  - Disabling the feature stops all TRXDB network calls.

---

## 14. Extra tools to make v4.0.0 stand out

These go beyond the notes above to make v4.0.0 genuinely impressive. Each is
scoped to be self-contained and to reuse existing infrastructure.

### 14.1 [ ] Coverage footprint toggle

- A coverage footprint overlay (the line-of-sight circle under a selected
  satellite) is partially referenced in the theme (`footprint_bg`,
  `footprint_border`) but not exposed. Add a Layers toggle and render it in 2D
  and 3D.

### 14.2 [ ] Sensor swath visualizer refinement

- The basic sensor swath is in section 9.4. Follow-ons: let the swath follow a
  selected pass (so users can plan a pass and watch the scanned area sweep
  across the map), and allow a configurable sensor look angle / off-nadir
  offset.

### 14.2 [ ] Next pass strip

- A compact "Next Pass" readout (next AOS, satellite, max elevation) pinned near
  the bottom bar or in the Satellite Info panel, so users do not have to open
  the passes panel to know what is coming.

### 14.3 [ ] Orbit classification badges

- Auto-classify each satellite (LEO, MEO, GEO, HEO, Molniya) and show a colored
  badge in the Satellite Manager and Satellite Info. This reuses the
  semi-major-axis and eccentricity data already stored.

### 14.4 [ ] Sun, Moon, and eclipse readout

- A compact solar system readout (sun position, moon phase, next eclipse event)
  in the Satellite Info or a small panel, reusing `calculate_sun_position` and
  `calculate_moon_position`.

### 14.5 [ ] Pass favorites and watchlist

- Let users star satellites they care about. Starred satellites get a visual
  marker in the manager and are prioritized in the pass list and the "next
  event" strip.

### 14.6 [ ] Session summary on exit

- On exit, show a small session summary (satellites tracked, passes predicted,
  data pulled, time spent). A nice touch that reinforces the app's identity.

### 14.7 [ ] Keyboard shortcut cheat-sheet

- The Help modal lists shortcuts. Add a quick-reference overlay (hold a key to
  see all shortcuts) and make sure every new tool has a shortcut.

---

## 15. Release checklist for v4.0.0

Use this as the definition of done for the release.

### Functional

- [ ] Data sources pull correctly from all four source types.
- [ ] Staleness is enforced and surfaced per source.
- [x] Pass prediction works in both single-sat and all-active modes, with a
      configurable time span and correct unique IDs.
- [x] Pass progress bar shows ongoing passes over the home location.
- [x] Clicking a pass opens it in the polar plot.
- [ ] Doppler shows a frequency graph and exports a valid CSV.
- [ ] Rotator connects, is configurable, persists settings, and auto-steers.
- [ ] Scope view is a large window with targeting, locking, beam highlight, and
      layer toggles.
- [x] Local/UTC toggle works everywhere and persists.
- [x] Notifications appear for the key events.
- [ ] Van Allen and magnetosphere layers render.
- [ ] Sensor swath visualizer draws a line / square / circle footprint from a
      user FOV around the selected satellite.
- [x] Apoapsis and periapsis show altitude labels.
- [x] Markers are manageable (add, remove, persist) and Cape Canaveral is gone.
- [x] Persistence restores active satellites, source selections, rotator, and
      time preference.

### Polish

- [x] No hardcoded colors that should be themed.
- [x] Sidebar notches are visible and deliberate.
- [ ] Every panel is capped to a reasonable sidebar height.
- [x] Settings covers all new features.
- [ ] No duplicate-ID UI bugs in lists.

### Performance

- [ ] SGP4 calls are reduced via the Keplerian fast path.
- [ ] Pass prediction over 24h for all active satellites is smooth.
- [ ] No per-frame allocations in the render loop.

### Docs

- [ ] README feature list updated.
- [ ] CREDITS updated for new contributors and attributions.
- [ ] Settings documented.

---

## Implementation order


1. Persistence (section 11). Active satellites, data selections, rotator, time
   preference. Everything else builds on this.
2. Passes (section 2). Modes, unique IDs, progress, polar handoff.
3. Polar plot (section 3) and Doppler (section 4). Pass path, graph, CSV.
4. Rotator (section 6). Settings UI, persistence, overlays.
5. Scope (section 7). The big window, targeting, beam, layers.
6. Layers (section 9). Van Allen, magnetosphere, sensor swath (apsis labels
   are done).
7. Notifications (section 8). Wire into the events from steps 1 through 6.
8. Theming polish (section 12). Notch visibility, color audit.
9. TRXDB (section 13). Opt-out transponder info.
10. Extra tools (section 14). Each independently shippable.
11. Backend performance (section 1.3). Keplerian fast path, can be done in
    parallel with the UI work.
