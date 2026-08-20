# Changelog
## [0.3.14] - 2026-08-17

- Change default h264 profile
- Minimum Rust version >= 1.88

## [0.3.13] - 2024-09-24

- Add support for AV1
- Minimum Rust version >= 1.70

## [0.3.12] - 2024-06-18

- Add support for RFC4566/RFC8866 framerate

## [0.3.11] - 2024-01-17

- Permit a wider set of payload type numbers

## [0.3.10] - 2023-01-05

- Permit inconsistent simulcast directions

## [0.3.9] - 2022-01-12

- Add support for RFC8858 rtcp-mux-only
- Correct seperation of tokens in FMTP parameters
- Do not emit an empty line after a media description

## [0.3.8] - 2021-01-16

- fmt numbers 35 to 63 are now usable for dynamic allocation
- parse extmap-allow-mixed as per RFC 8285

## [0.3.7] - 2020-11-23

- Minimum Rust version >= 1.45
- Added feature for parse object tree wide debug formatting, defaulted to on for now
- Moved check for multiple c lines within an m section out of the setter and into the parsing logic, credit Mnwa

## [0.3.6] - 2020-05-07

- Added support for Opus FMTP parameters ptime, maxptime, minptime, and maxaveragebitrate

## [0.3.5] - 2020-04-07

### Fixed

- RTX apt can now be zero

## [0.3.4] - 2020-03-31

### Fixed

- Fixed new clippy warnings in stable
- Accept a lack of c= lines if there are no m= lines (for JSEP compat.)

### Changed

- Added support for ssrc-group
- Added support for RTX FMTP parameters
- Example runner can no be told to expect failure

## [0.3.3] - 2019-12-10

### Changed

- Changed handling of default channel counts

## [0.3.2] - 2019-12-02

### Changed

- Fixed handling of spaces in fmtp attributes
- Minimum Rust version >= 1.36

## [0.3.1] - 2019-09-12

### Changed

- Updated `urls` dependency to `0.2.1`

### Removed

- Removed `TcpTlsRtpSavpf` protocl token
- Removed dependency on `enum-display-derive`

## [0.3.0] - 2019-08-08

### Changed

- Unsafe code is forbidden now

### Fixed

- Fixed panic from slicing unicode character in image attr braces

### Added

- Added support for FQDN addresses
- Added support for parsing ice-pacing
- Added fuzzing target

## [0.2.2] - 2019-06-21

### Changed

- Minimum Rust version >= 1.35

## [0.2.0] - 2019-06-15

### Changed

- Minimum Rust version >= 1.30.0
- Changed code coverage from kcov to tarpaulin
- Moved file parser example to examples sub directory
- Replaced cause() with source() in unit test
- Moved all unit tests into tests modules

### Fixed

- Unknown extensions in candidate attributes (#103)
- Reduced amount of internal clone() calls significantly
- Added dyn to error:Error impl required by more recent rust versions

### Added

- Support for anonymization to enable logging of SDP without personal
  information
- Quite a bit more unit testing got added

### Removed

- Replaced unsupported types with errors directly in lib.rs

## [0.1.0] - 2019-01-26

- Initial release
- Minimum Rust version >= 1.17.0
