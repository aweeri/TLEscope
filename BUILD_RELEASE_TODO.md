# Build and Release Audit

- Basis: `aweeri/TLEscope:extremelywip` @ `a5cf0d753868ce5b2dffca204470613d76cc6ba8`.

## P1
- Bundle the project `LICENSE`, third-party notices and required font licenses in Linux, Windows and macOS release artifacts and in the Windows installer.
- Remove stale installer/install-script references to `data.tle` and `persistence.bin`; current persistence is JSON-based.
- Add `CFBundleShortVersionString` and `CFBundleVersion` to the macOS `Info.plist` and populate them from the existing version metadata.

## P2
- Extend release validation to assert that required license/notice files and macOS version metadata are present in packaged artifacts.
