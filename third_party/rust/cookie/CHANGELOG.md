# Version 0.16

## Version 0.16.2 (Dec 16, 2022)

### General Changes

  * `base64` was updated to `0.20`.

## Version 0.16.1 (Sep 25, 2022)

### Changes and Fixes

  * The `,`, `(`, and `)` are percent-encoded/decoded when encoding is used.
  * The `aes-gcm` dependency was updated to 0.10.

## Version 0.16.0 (Dec 28, 2021)

### Breaking Changes

  * The MSRV is now `1.53`, up from `1.41` in `0.15`.
  * `time` has been updated to `0.3` and is reexported from the crate root.

### General Changes

  * `rust-crypto` dependencies were updated to their latest versions.

# Version 0.15

## Version 0.15.1 (Jul 14, 2021)

### Changes and Fixes

  * A panic that could result from non-char boundary indexing was fixed.
  * Stale doc references to version `0.14` were updated.

## Version 0.15.0 (Feb 25, 2021)

### Breaking Changes

  * `Cookie::force_remove()` takes `&Cookie` instead of `Cookie`.
  * Child jar methods split into immutable and mutable versions
    (`Cookie::{private{_mut}, signed{_mut}}`).
  * `Cookie::encoded()` returns a new `Display` struct.
  * Dates with year `<= 99` are handled like Chrome: range `0..=68` maps to
    `2000..=2068`, `69..=99` to `1969..=1999`.
  * `Cookie::{set_}expires()` operates on a new `Expiration` enum.

### New Features

  * Added `Cookie::make_removal()` to manually create expired cookies.
  * Added `Cookie::stripped()` display variant to print only the `name` and
    `value` of a cookie.
  * `Key` implements a constant-time `PartialEq`.
  * Added `Key::master()` to retrieve the full 512-bit master key.
  * Added `PrivateJar::decrypt()` to manually decrypt an encrypted `Cookie`.
  * Added `SignedJar::verify()` to manually verify a signed `Cookie`.
  * `Cookie::expires()` returns an `Option<Expiration>` to allow distinguishing
    between unset and `None` expirations.
  * Added `Cookie::expires_datetime()` to retrieve the expiration as an
    `OffsetDateTime`.
  * Added `Cookie::unset_expires()` to unset expirations.

### General Changes and Fixes

  * MSRV is 1.41.

# Version 0.14

## Version 0.14.3 (Nov 5, 2020)

### Changes and Fixes

  * `rust-crypto` dependencies were updated to their latest versions.

## Version 0.14.2 (Jul 22, 2020)

### Changes and Fixes

  * Documentation now builds on the stable channel.
  * `rust-crypto` dependencies were updated to their latest versions.
  * Fixed 'interator' -> 'iterator' documentation typo.

## Version 0.14.1 (Jun 5, 2020)

### Changes and Fixes

  * Updated `base64` dependency to 0.12.
  * Updated minimum `time` dependency to correct version: 0.2.11.
  * Added `readme` key to `Cargo.toml`, updated `license` field.

## Version 0.14.0 (May 29, 2020)

### Breaking Changes

  * The `Key::from_master()` method was deprecated in favor of the more aptly
    named `Key::derive_from()`.
  * The deprecated `CookieJar::clear()` method was removed.

### New Features

  * Added `Key::from()` to create a `Key` structure from a full-length key.
  * Signed and private cookie jars can be individually enabled via the new
    `signed` and `private` features, respectively.
  * Key derivation via key expansion can be individually enabled via the new
    `key-expansion` feature.

### General Changes and Fixes

  * `ring` is no longer a dependency: `RustCrypto`-based cryptography is used in
    lieu of `ring`. Prior to their inclusion here, the `hmac` and `hkdf` crates
    were audited.
  * Quotes, if present, are stripped from cookie values when parsing.

# Version 0.13

## Version 0.13.3 (Feb 3, 2020)

### Changes

  * The `time` dependency was unpinned from `0.2.4`, allowing any `0.2.x`
    version of `time` where `x >= 6`.

## Version 0.13.2 (Jan 28, 2020)

### Changes

  * The `time` dependency was pinned to `0.2.4` due to upstream breaking changes
    in `0.2.5`.

## Version 0.13.1 (Jan 23, 2020)

### New Features

  * Added the `CookieJar::reset_delta()` method, which reverts all _delta_
    changes to a `CookieJar`.

## Version 0.13.0 (Jan 21, 2020)

### Breaking Changes

  * `time` was updated from 0.1 to 0.2.
  * `ring` was updated from 0.14 to 0.16.
  * `SameSite::None` now writes `SameSite=None` to correspond with updated
    `SameSite` draft. `SameSite` can be unset by passing `None` to
    `Cookie::set_same_site()`.
  * `CookieBuilder` gained a lifetime: `CookieBuilder<'c>`.

### General Changes and Fixes

  * Added a CHANGELOG.
  * `expires`, `max_age`, `path`, and `domain` can be unset by passing `None` to
    the respective `Cookie::set_{field}()` method.
  * The "Expires" field is limited to a date-time of Dec 31, 9999, 23:59:59.
  * The `%` character is now properly encoded and decoded.
  * Constructor methods on `CookieBuilder` allow non-static lifetimes.
