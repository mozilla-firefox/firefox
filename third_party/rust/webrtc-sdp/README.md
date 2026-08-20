# webrtc-sdp

[![Crates.io](https://img.shields.io/crates/v/webrtc-sdp.svg)](https://crates.io/crates/webrtc-sdp)
[![Build Status](https://travis-ci.org/mozilla/webrtc-sdp.svg?branch=master)](https://travis-ci.org/mozilla/webrtc-sdp)
[![Codecov coverage status](https://codecov.io/gh/mozilla/webrtc-sdp/branch/master/graph/badge.svg)](https://codecov.io/gh/webrtc-sdp/webrtc-sdp)
[![License: MPL 2.0](https://img.shields.io/badge/License-MPL%202.0-brightgreen.svg)](#License)
[![dependency status](https://deps.rs/repo/github/mozilla/webrtc-sdp/status.svg)](https://deps.rs/repo/github/mozilla/webrtc-sdp)

A SDP parser written in Rust specifically aimed to handle WebRTC SDP offers and answers.

## Dependecies

* Rust >= 1.88.0
* log module
* serde module
* serde-derive module

Cargo installs the missing modules automatically when building webrtc-sdp for the first time.

## The webrtc-sdp API

The main function is:
```rust
fn parse_sdp(sdp: &str, fail_on_warning: bool) -> Result<SdpSession, SdpParserError>
```
The `sdp` parameter is the string which will get parsed. The `fail_on_warning` parameter determines how to treat warnings encountered during parsing. Any problems encountered during are stored until the whole string has been parsed. Any problem during parsing falls into two catgeories:

* Fatal error preventing further parsing or processing of the SDP
* Warning which don't block further processing of the SDP

Warnings will be for example unknown parameters in attributes. Setting `fail_on_warning` to `true` makes most sense during development, when you want to be aware of all potential problems. In production `fail_on_warning` is expected to be `false`.

`parse_sdp()` returns either an `SdpSession` struct ([code](https://github.com/mozilla/webrtc-sdp/blob/master/src/lib.rs#L137)) which contains all the parsed information. Or in case a fatal error was encountered (or if `fail_on_warning` was set to `true` and any warnings were encountered) an `SdpParserError` ([code](https://github.com/mozilla/webrtc-sdp/blob/master/src/error.rs#L117)) will be returned as a `Result`.

## Examples

The [file parser](https://github.com/mozilla/webrtc-sdp/blob/master/examples/file_parser.rs) in the webrtc-sdp package gives you an easy example of how to invoke the webrtc-sdp parser.

## Contributing

As the Travis CI runs are checking for code formating and clippy warnings please run the following commands locally, before submitting a Pull Request.

If you haven't clippy and Rust format installed already you add them like this:
```
rustup component add rustfmt-preview
rustup component add clippy
```

Check with clippy for warnings in the code:
```
cargo clippy
```

And format all of the code according to Rust code style convention:
```
cargo fmt --all
```

## Fuzzing

Install cargo-fuzz like this:
```
cargo install cargo-fuzz
```

With rust nightly you can start fuzzing like this:
```
cargo fuzz run fuzz_target_parse_sdp
```

## License

Licensed under [MPL-2.0](https://www.mozilla.org/MPL/2.0/)
