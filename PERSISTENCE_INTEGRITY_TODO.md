# Persistence Integrity Audit

- Basis: `aweeri/TLEscope:extremelywip` @ `0edc53cd28291482ecc027a9d0dab542bf4b52da`.

## P0
- A catalogue refresh still sets `sat_count = 0` before any source succeeds. Stage the refreshed catalogue separately and swap it in only after an acceptable refresh completes.
- Preserve the previous catalogue as last-known-good when every selected source fails.

## P1
- Make `data.json`, `settings.json` and `data_selections.json` writes atomic: write a temporary file, check write/flush errors, sync where appropriate, then rename.
- Validate persistence versions, required fields and numeric ranges when loading JSON rather than accepting missing/invalid values as defaults.
- On corrupt persisted files, warn and preserve the bad file for recovery instead of silently replacing it with empty/default state.

## Tests
- Failed network refresh preserves the previous catalogue.
- Disk-full/interrupted writes preserve the previous file.
- Persistence round-trips arbitrary strings and rejects malformed/out-of-range records cleanly.
