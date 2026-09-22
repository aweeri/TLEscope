# Dependency and License Audit

- Basis: `aweeri/TLEscope:extremelywip` @ `a5cf0d753868ce5b2dffca204470613d76cc6ba8`.

## P1
- Add a concise `THIRD_PARTY_NOTICES.md` covering csgp4, Dear ImGui, raylib, rlImGui, nlohmann/json, Font Awesome, Natural Earth and bundled theme/font/texture assets.
- Ship the project `LICENSE`, third-party notices and required font licenses in every release artifact.
- Record the exact upstream version/commit for vendored csgp4, Dear ImGui and rlImGui; raylib and nlohmann/json are already pinned as git submodules.
- Verify redistribution/attribution requirements for bundled textures and fonts and keep required attribution with shipped assets.

## P2
- Slim the vendored Dear ImGui/rlImGui trees to build-required files, or convert them to pinned submodules.
- Do not remove dependency license files while slimming.
- Add a packaging check that required license/notice files are present in every artifact.
