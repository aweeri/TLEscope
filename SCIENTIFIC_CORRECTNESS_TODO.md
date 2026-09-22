# Scientific Correctness Audit

- Basis: `aweeri/TLEscope:extremelywip` @ `a5cf0d753868ce5b2dffca204470613d76cc6ba8`.

## P1
- Pass span is rounded up with `ceil(span_days)`; a 6-hour request can scan 24 hours. Stop at the exact requested end epoch.
- Validate TLE length, line numbers, matching NORAD IDs, checksum and numeric ranges before fixed-column reads.
- OMM epoch parsing is stricter now, but JSON/CSV still default several missing orbital fields to zero. Define required CCSDS fields and reject missing/non-finite/out-of-range values consistently across JSON, CSV and KVN.
- Add reference-vector tests for SGP4 position, GMST/az-el, AOS/LOS/max elevation and Doppler.

## P2
- Document accuracy limits for Sun, Moon and eclipse approximations if users may interpret them scientifically.
