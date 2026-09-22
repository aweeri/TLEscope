# Persistence Integrity Audit

- Basis: `aweeri/TLEscope:extremelywip` @ `a5cf0d753868ce5b2dffca204470613d76cc6ba8`.

## P0
- A catalogue refresh still sets `sat_count = 0` before any source succeeds. Stage the refreshed catalogue separately and swap it in only after an acceptable refresh completes.
- Preserve the previous catalogue as last-known-good when every selected source fails.

## P1
- Make `data.json`, `settings.json`, `data_selections.json` and `favorites.json` writes atomic: write a temporary file, check write/flush errors, sync where appropriate, then rename.
- Validate persistence versions, required fields and numeric ranges when loading JSON; reject invalid favorite/NORAD IDs instead of casting them into range.
- On corrupt persisted files, warn and preserve the bad file for recovery instead of silently replacing it with empty/default state.

## Tests
- Failed network refresh preserves the previous catalogue.
- Disk-full/interrupted writes preserve the previous file.
- Persistence round-trips arbitrary strings and favorites and rejects malformed/out-of-range records cleanly.
