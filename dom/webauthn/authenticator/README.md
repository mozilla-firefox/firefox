# A Rust library for interacting with CTAP1/CTAP2 Security Keys

This is a cross-platform library for directly interacting with Security Key-type devices via Rust.

- **Supported Platforms**: FreeBSD, Linux, macOS[^1], NetBSD, OpenBSD, and Windows[^2].
- **Supported Transports**: USB HID.
- **Supported Protocols**: [FIDO CTAP1 (U2F)][u2f], [CTAP 2.0][], CTAP 2.1-PRE, [CTAP 2.1][].

This library is used by Firefox on platforms that _do not_ provide WebAuthn
APIs (as there are on Android, iOS, [macOS][] and [Windows][hello]). It
implements a subset of the CTAP protocol that is useful for a web browser.

It was previously developed at https://github.com/mozilla/authenticator-rs and
was imported into the Firefox repository at version 0.6.1.

[^1]: [macOS has its own platform WebAuthn API][macOS] (which also supports iCloud Keychain), but still allows direct, transport level communication with authenticators.

[^2]: Windows 10 v1903 and later block direct, transport level access to authenticators to applications _not_ running as Administrator, breaking this library for most applications. [Applications will need to use the platform APIs instead][hello].

## Tests

The unit tests run as part of `./mach rusttests`.

[CTAP 2.0]: https://fidoalliance.org/specs/fido-v2.0-ps-20190130/fido-client-to-authenticator-protocol-v2.0-ps-20190130.html
[CTAP 2.1]: https://fidoalliance.org/specs/fido-v2.1-ps-20210615/fido-client-to-authenticator-protocol-v2.1-ps-errata-20220621.html
[hello]: https://github.com/microsoft/webauthn
[macOS]: https://developer.apple.com/documentation/authenticationservices/public-private-key-authentication
[u2f]: https://fidoalliance.org/specs/fido-u2f-v1.1-id-20160915/fido-u2f-raw-message-formats-v1.1-id-20160915.html
