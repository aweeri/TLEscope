# Test Coverage Audit

- Basis: `aweeri/TLEscope:extremelywip` @ `a5cf0d753868ce5b2dffca204470613d76cc6ba8`.

## P0
- Add a real `make test`; `test` is still listed in `.PHONY` but no test target exists.

## P1
- Unit tests: epoch/leap-year conversion, TLE validation, OMM JSON/CSV/KVN parsing, persistence/favorites validation and map wrap/occlusion predicates.
- Scientific reference tests: SGP4 positions, az/el, passes and Doppler.
- Integration tests: failed refresh preserves the catalogue, async/detached workers shut down cleanly, and rotator format validation.
- Network tests must use a local test server; do not depend on live CelesTrak/Retlector/TRXDB services.
- Add Linux ASan/UBSan coverage for test builds.
- Extend the existing pull-request CI to run the test target once it exists; keep the current Linux startup smoke test.

## P2
- Keep the cross-platform release matrix for packaging; do not duplicate the full test suite on every architecture.
