# Security and Network Audit

- Basis: `aweeri/TLEscope:extremelywip` @ `a5cf0d753868ce5b2dffca204470613d76cc6ba8`.

## P0
- Windows fetches still disable TLS peer verification with `CURLOPT_SSL_VERIFYPEER = 0`; restore certificate verification.

## P1
- Add low-speed timeout and response-size limits so slow or unexpectedly large responses cannot consume unbounded time/memory.
- Restrict built-in transfers and redirects to HTTPS; validate allowed protocols for custom URLs.
- Extend the current curl timing/proxy-CONNECT diagnostics with primary IP, TLS backend, OS error and whether proxy environment variables are present.
- The macOS app launcher mirrors static system HTTP/HTTPS proxy settings, but PAC evaluation is unsupported. Document that limitation and add explicit/PAC proxy support only if it is a supported requirement.
- Add configurable CA trust/bundle support for TLS-inspecting networks.
- Retlector and TRXDB still launch detached worker threads; give them owned lifetime/cancellation and ensure shutdown cannot race application teardown.
- Validate rotator command templates instead of passing `rot.set_fmt` directly to `snprintf` as a format string.

## P2
- Move rotator DNS/connect/poll I/O off the UI thread.
- Redact credentials, tokens and sensitive query values from logged custom URLs.
