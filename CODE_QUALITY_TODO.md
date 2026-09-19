# Code Quality Audit

- Basis: `aweeri/TLEscope:extremelywip` @ `0edc53cd28291482ecc027a9d0dab542bf4b52da`.

## P1
- Reduce broad warning suppression in the Makefile, especially truncation, narrowing and maybe-uninitialized warnings; fix project warnings rather than masking them globally.
- Consolidate duplicated TLE/3LE payload splitting in `src/data/async_fetch.cpp` so pasted and downloaded data use the same validation path.

## P2
- Continue moving domain ownership out of `src/main.cpp`, `src/core/config.cpp` and `src/ui/ui_layout.cpp`; avoid a large architecture rewrite.
- Keep new helpers small and domain-specific; avoid adding another abstraction layer over the existing tool registry/UI split.
