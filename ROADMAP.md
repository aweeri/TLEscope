# TLEscope v4.0.0 Roadmap

---

## HIGH PRIORITY (Performance & Half-Done Features)

### Data & Performance
- [IN PROGRESS] **Data staleness:** Add per-source age display ("2h ago") in Data Sources panel with amber/red warnings
- [IN PROGRESS] **SGP4 performance:** Add `calculate_position_kepler()` fast path for predictions, reduce SGP4 calls

### UI Tools (In Progress)
- [IN PROGRESS] **Polar plot:** Add clickable rotator steering, max-elevation marker, time position indicator on pass path
- [IN PROGRESS] **Doppler:** Add frequency graph over time (X=time, Y=Hz), implement CSV export handler
- [IN PROGRESS] **Rotator:** Add settings UI (host/port/protocols), persist config, implement overlay drawing, visual feedback
- [IN PROGRESS] **Scope view:** Redesign as large circular window with targeting/locking, beam visualization, layer toggles

---

## MEDIUM PRIORITY (Polish & UX)

### Data & Info
- [TODO] **Sat Info:** Show epoch age and data age with "stale" badge when threshold exceeded

### UI Refinements
- [TODO] **Polar plot:** Add floating window option (currently capped at 360px)
- [TODO] **Doppler:** Add preset frequency buttons (2m, 70cm, 23cm, S-band, X-band)
- [TODO] **Time controls:** Surface auto-warp to event (next AOS/pass/sunrise) in UI
- [TODO] **Scope:** Add floating resizable window option

---

## LOW PRIORITY (New Features - Post v4.0.0)

### Layers
- [TODO] Van Allen belts layer (translucent bands)
- [TODO] Magnetosphere layer (dipole field visualization)
- [TODO] Sensor swath visualizer (FOV-driven ground footprint: line/square/circle)
- [TODO] Layer panel polish (icons, tooltips, consistency)
- [TODO] 2D mode layer separation (layers panel switches to 2D-specific layers when in 2D map view)
- [TODO] 2D map overlays (country borders, lat/lon grid lines, equator/tropics, etc.)

### Extra Tools
- [TODO] Sensor swath refinement (follow pass, off-nadir offset)
- [TODO] Orbit classification badges (LEO/MEO/GEO/HEO/Molniya auto-classify)
- [TODO] Sun/Moon/eclipse readout (solar system panel)
- [TODO] Pass favorites/watchlist (star satellites, prioritize in lists)
- [TODO] Session summary on exit (satellites tracked, passes predicted, time spent)
- [TODO] Keyboard shortcut cheat-sheet (hold-key overlay)
- [TODO] Next pass strip (compact readout: next AOS, sat, max elevation)

---

## COMPLETED IN v4.0.0

### Data & Backend
- Multi-source pulling (Retlector, CelesTrak, custom URLs, pasted data)
- Local time support with UTC toggle (persisted, applied everywhere)

### Passes
- Two pass modes (single-sat + all-active)
- Richer pass entries (unique IDs, AOS/LOS times, max elevation)
- Pass progress bar (green bar for ongoing passes)
- Click to open in Polar Plot
- Pass to Doppler handoff
- Pass list height capping

### UI & Theming
- Toast notification system (top-right, auto-dismiss, theme-aware)
- Sidebar notch visibility (theme-aware, deliberate snap-hide)
- Theme review (all hardcoded colors routed through theme)
- Settings expansion (notifications, pass defaults, scope defaults)

### Locations & Persistence
- Unified locations system (markers + home in one list)
- Home designation (house button, single home flag)
- Persist critical settings (active sats, data selections, rotator, time preference)

### Layers
- Apoapsis/periapsis text labels (altitude in km)

---

## RELEASE CHECKLIST (v4.0.0 Definition of Done)

### Functional
- [ ] Data staleness enforced and surfaced per source
- [ ] Doppler shows frequency graph and exports valid CSV
- [ ] Rotator connects, configurable, persists, auto-steers
- [ ] Scope view: large window, targeting, locking, beam highlight, layers
- [ ] Van Allen and magnetosphere layers render
- [ ] Sensor swath visualizer (line/square/circle from FOV)

### Performance
- [ ] SGP4 calls reduced via Keplerian fast path
- [ ] Pass prediction over 24h smooth for all active satellites
- [ ] No per-frame allocations in render loop

### Polish
- [ ] Every panel capped to reasonable sidebar height
- [ ] No duplicate-ID UI bugs in lists

### Docs
- [ ] README feature list updated
- [ ] CREDITS updated
- [ ] Settings documented
