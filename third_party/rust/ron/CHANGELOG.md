# Changelog
All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](http://keepachangelog.com/en/1.0.0/)
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## Unreleased

## [0.10.1] - 2025-04-08

### API Changes

- Add `ron::Options::to_io_writer` and `ron::Options::to_io_writer_pretty` to allow writing into an `io::Writer` ([#561](https://github.com/ron-rs/ron/pull/561))
- Breaking: `ron::value::Number` is now non-exhaustive, to avoid breaking `match`es when feature unification enables more of its variants than expected ([#564](https://github.com/ron-rs/ron/pull/564))

## [0.9.0] - 2025-03-18

### API Changes

- Add `ron::value::RawValue` helper type which can (de)serialize any valid RON ([#407](https://github.com/ron-rs/ron/pull/407))
- Add `escape_strings` option to `PrettyConfig` to allow serialising with or without escaping ([#426](https://github.com/ron-rs/ron/pull/426))
- Add `compact_maps` and `compact_structs` options to `PrettyConfig` to allow serialising maps and structs on a single line ([#448](https://github.com/ron-rs/ron/pull/448))
- Add minimal support for `#[serde(flatten)]` with roundtripping through RON maps ([#455](https://github.com/ron-rs/ron/pull/455))
- Add minimal roundtripping support for `#[serde(tag = "tag")]`, `#[serde(tag = "tag", content = "content")]`, and `#[serde(untagged)]` enums ([#451](https://github.com/ron-rs/ron/pull/451))
- Breaking: Expand the `value::Number` enum to explicitly encode all possible number types ([#479](https://github.com/ron-rs/ron/pull/479))
- Add `number_suffixes` option to `PrettyConfig` to allow serialising numbers with their explicit type suffix, e.g. `42i32` ([#481](https://github.com/ron-rs/ron/pull/481))
- Allow `ron::value::RawValue` to capture any whitespace to the left and right of a ron value ([#487](https://github.com/ron-rs/ron/pull/487))
- Breaking: Enforce that ron always writes valid UTF-8 ([#488](https://github.com/ron-rs/ron/pull/488))
- Add convenient `Value::from` impls ([#498](https://github.com/ron-rs/ron/pull/498))
- Add new extension `explicit_struct_names` which requires that struct names are included during deserialization ([#522](https://github.com/ron-rs/ron/pull/522))
- Add new path-based field metadata serialization support via `PrettyConfig` ([#544](https://github.com/ron-rs/ron/pull/544))
- Breaking: Change `PrettyConfig` so that `new_line`, `indentor` and `separator` are all `Cow<'static, str>` instead of `String` ([#546](https://github.com/ron-rs/ron/pull/546))

### Format Changes

- [Non-API] Breaking: Treat `Some` like a newtype variant with `unwrap_variant_newtypes` ([#465](https://github.com/ron-rs/ron/pull/465))
- Allow parsing floating point literals with underscores ([#481](https://github.com/ron-rs/ron/pull/481))
- **Format-Breaking:** Switch from base64-encoded to Rusty byte strings, still allow base64 deserialising for now ([#438](https://github.com/ron-rs/ron/pull/438))
- Fix issue [#241](https://github.com/ron-rs/ron/issues/241) and allow parsing numbers with explicit type suffixes, e.g. `1u8` or `-1f32` ([#481](https://github.com/ron-rs/ron/pull/481))
- Add support for byte literals as strongly typed unsigned 8-bit integers ([#438](https://github.com/ron-rs/ron/pull/438))
- Fix issue [#321](https://github.com/ron-rs/ron/issues/321) and allow parsing UTF-8 identifiers ([#488](https://github.com/ron-rs/ron/pull/488))

### Bug Fixes

- Fix parsing `r` as a self-describing struct or variant name (and not the start of a raw string) ([#465](https://github.com/ron-rs/ron/pull/465))
- Fix serialising raw strings containing a literal backslash ([#465](https://github.com/ron-rs/ron/pull/465))
- Fix serialising `None` inside a stack of nested `Option`s with `#![enable(implicit_some)]` enabled ([#465](https://github.com/ron-rs/ron/pull/465))
- Fix deserialising deserialising `A('/')` into a `ron::Value` ([#465](https://github.com/ron-rs/ron/pull/465))
- Fix issue [#445](https://github.com/ron-rs/ron/issues/445) and allow parsing `+unsigned` as an unsigned int ([#479](https://github.com/ron-rs/ron/pull/479))
- Fix serialising reserved identifiers `true`, `false`, `Some`, `None`, `inf`[`f32`|`f64`], and `Nan`[`f32`|`f64`] ([#487](https://github.com/ron-rs/ron/pull/487))
- Disallow unclosed line comments at the end of `ron::value::RawValue` ([#489](https://github.com/ron-rs/ron/pull/489))
- Fix parsing of struct/variant names starting in `None`, `Some`, `true`, or `false` ([#499](https://github.com/ron-rs/ron/pull/499))
- Fix deserialising owned string field names in structs, allowing deserializing into `serde_json::Value`s ([#511](https://github.com/ron-rs/ron/pull/512))

### Miscellaneous

- Add CIFuzz GitHub action ([#429](https://github.com/ron-rs/ron/pull/429))
- Update the arbitrary fuzzer to check arbitrary serde data types, values, and `ron::ser::PrettyConfig`s ([#465](https://github.com/ron-rs/ron/pull/465))
- Add a benchmark for PRs that runs over the latest fuzzer corpus ([#465](https://github.com/ron-rs/ron/pull/465))
- Fuzz serde enum representations and collect current limitations in ron and serde ([#502](https://github.com/ron-rs/ron/pull/502))
- Update `base64` dependency to version 0.22 ([#529](https://github.com/ron-rs/ron/pull/529))
- Fix issue [#556](https://github.com/ron-rs/ron/issues/556) and update minium dependency versions ([#557](https://github.com/ron-rs/ron/pull/557))

## [0.8.1] - 2023-08-17

- Fix issues [#277](https://github.com/ron-rs/ron/issues/277) and [#405](https://github.com/ron-rs/ron/issues/405) with `Value::Map` `IntoIter` and extraneous item check for `Value::Seq` ([#406](https://github.com/ron-rs/ron/pull/406))
- Fix issue [#401](https://github.com/ron-rs/ron/issues/401) with correct raw struct name identifier parsing ([#402](https://github.com/ron-rs/ron/pull/402))
- Fix issue [#410](https://github.com/ron-rs/ron/issues/410) trailing comma parsing in tuples and `Some` ([#412](https://github.com/ron-rs/ron/pull/412))
- Error instead of panic when deserializing non-identifiers as field names ([#415](https://github.com/ron-rs/ron/pull/415))
- [Non-API] Breaking: Fix issue [#307](https://github.com/ron-rs/ron/issues/307) stack overflow with explicit recursion limits in serialising and deserialising ([#420](https://github.com/ron-rs/ron/pull/420))
- Fix issue [#423](https://github.com/ron-rs/ron/issues/423) deserialising an identifier into a borrowed str ([#424](https://github.com/ron-rs/ron/pull/424))
- Bump MSRV to 1.57.0 and bump dependency: `base64` to 0.20 ([#431](https://github.com/ron-rs/ron/pull/431))
- Bump dependency `base64` to 0.21 ([#433](https://github.com/ron-rs/ron/pull/433))
- Depend on `serde_derive` directly to potentially enable more compilation parallelism ([#441](https://github.com/ron-rs/ron/pull/441))
- [Non-API] Breaking: Bump `bitflags` dependency to 2.0, changes `serde` impls of `Extensions` ([#443](https://github.com/ron-rs/ron/pull/443))
- Add `Map::retain` method ([#460](https://github.com/ron-rs/ron/pull/460))
- Bump MSRV to 1.64.0 and bump dependency: `indexmap` to 2.0 ([#459](https://github.com/ron-rs/ron/pull/459))

## [0.8.0] - 2022-08-17

- Bump dependencies: `bitflags` to 1.3, `indexmap` to 1.9 ([#399](https://github.com/ron-rs/ron/pull/399))
- Add `integer128` feature that guards `i128` and `u128` ([#304](https://github.com/ron-rs/ron/pull/304), [#351](https://github.com/ron-rs/ron/pull/351))
- Fix issue [#265](https://github.com/ron-rs/ron/issues/265) with better missing comma error ([#353](https://github.com/ron-rs/ron/pull/353))
- Fix issue [#301](https://github.com/ron-rs/ron/issues/301) with better error messages ([#354](https://github.com/ron-rs/ron/pull/354))
- Fix issue [#337](https://github.com/ron-rs/ron/issues/337) by removing `decimal_floats` PrettyConfig option and unconditional decimals in floats ([#363](https://github.com/ron-rs/ron/pull/363))
- Fix issue [#203](https://github.com/ron-rs/ron/issues/203) with full de error positioning ([#356](https://github.com/ron-rs/ron/pull/356))
- Expand the `ron::Error` enum to distinguish `serde` errors like `NoSuchEnumVariant` and `MissingStructField` with error positioning ([#394](https://github.com/ron-rs/ron/pull/394))
- Bump MSRV to 1.56.0 ([#396](https://github.com/ron-rs/ron/pull/396))

## [0.7.1] - 2022-06-15

- Add `struct_names` option to `PrettyConfig` ([#329](https://github.com/ron-rs/ron/pull/329))
- Fix newtype variant unwrapping around enum, seq and map ([#331](https://github.com/ron-rs/ron/pull/331))
- Implement `unwrap_newtypes` extension during serialization ([#333](https://github.com/ron-rs/ron/pull/333))
- Implement `unwrap_variant_newtypes` extension during serialization ([#336](https://github.com/ron-rs/ron/pull/336))
- Add `compact_arrays` ([#299](https://github.com/ron-rs/ron/pull/299)) and `separator` options to `PrettyConfig` ([#349](https://github.com/ron-rs/ron/pull/349))
- Fix issue [#338](https://github.com/ron-rs/ron/issues/338) value map roundtrip ([#341](https://github.com/ron-rs/ron/pull/341))
- Fix issue [#289](https://github.com/ron-rs/ron/issues/289) enumerate_arrays comments ([#344](https://github.com/ron-rs/ron/pull/344))
- Report struct name in expected struct error ([#342](https://github.com/ron-rs/ron/pull/342))
- Add `Options` builder to configure the RON serde roundtrip ([#343](https://github.com/ron-rs/ron/pull/343))
- Fix issue [#367](https://github.com/ron-rs/ron/issues/367) with eager implicit some ([#368](https://github.com/ron-rs/ron/pull/368))
- Fix issue [#359](https://github.com/ron-rs/ron/issues/359) with `DeserializeSeed` support ([#360](https://github.com/ron-rs/ron/pull/360))
- Fix issue [#370](https://github.com/ron-rs/ron/issues/370) with `FromStr`-equivalent float EBNF and `Error::FloatUnderscore` ([#371](https://github.com/ron-rs/ron/pull/371))
- Fix issue [#374](https://github.com/ron-rs/ron/issues/374) extraneous .0 for small floats ([#372](https://github.com/ron-rs/ron/pull/372))
- Deprecate `Serializer::new` ([#382](https://github.com/ron-rs/ron/issues/382))

## [0.7.0] - 2021-10-22

- Add `unwrap_variant_newtypes` extension ([#319](https://github.com/ron-rs/ron/pull/319))
- Change MSRV to 1.36

## [0.6.6] - 2021-10-21

- Fix serialization of raw identifiers ([#323](https://github.com/ron-rs/ron/pull/323))

## [0.6.5] - 2021-09-09
- support serde renames that start with a digit

## [0.6.3] - 2020-12-18
- bump `base64` dependency to 0.13

## [0.6.2] - 2020-09-09

- Added `decimal_floats` PrettyConfig option, which always includes decimals in floats (`1.0` vs `1`) ([#237](https://github.com/ron-rs/ron/pull/237))
- Fixed EBNF grammar for raw strings ([#236](https://github.com/ron-rs/ron/pull/236), unsigned integers ([#248](https://github.com/ron-rs/ron/pull/248)), and nested comments ([#272](https://github.com/ron-rs/ron/pull/272))
- Added `ser::to_writer_pretty` ([#269](https://github.com/ron-rs/ron/pull/269))
- Sped up parsing using table predicates ([#276](https://github.com/ron-rs/ron/pull/276))

## [0.6.1] - 2020-07-14

### Fixes

- Fix array formatting regression ([#260](https://github.com/ron-rs/ron/pull/260))

## [0.6.0] - 2020-05-21

### Additions
- Implement integer support in Numbers ([#210](https://github.com/ron-rs/ron/pull/210))
- Port `ser::Serializer` to `io::Write` ([#206](https://github.com/ron-rs/ron/pull/206))
- Support i128 and u128 ([#219](https://github.com/ron-rs/ron/pull/219))
- Allow pretty ser to work with implicit-some extension ([#182](https://github.com/ron-rs/ron/pull/182))
- Make PrettyConfig future-proof ([#173](https://github.com/ron-rs/ron/pull/173))
- Use indexmap to preserve order (optional) ([#172](https://github.com/ron-rs/ron/pull/172))
- Add tests for different enum representations ([#166](https://github.com/ron-rs/ron/pull/166))
- Implement inf, -inf and NaN handling ([#163](https://github.com/ron-rs/ron/pull/163))
- Add VS code language tooling ([#160](https://github.com/ron-rs/ron/pull/160))
- Be smarter about integer deserialization ([#157](https://github.com/ron-rs/ron/pull/157))

### Fixes
- Fix parsing of borrowed strings ([#228](https://github.com/ron-rs/ron/pull/228))
- Fix depth limit test for off-by-one fix ([#225](https://github.com/ron-rs/ron/pull/225))
- Remove deprecated uses of `Error::description` ([#208](https://github.com/ron-rs/ron/pull/208))
- Preserve ordering of map sequences ([#197](https://github.com/ron-rs/ron/pull/197))
- Remove unneeded Neg requirement for signed_integer ([#193](https://github.com/ron-rs/ron/pull/193))
- Ensure "Untagged tuple-like enum variants not deserializing correctly……" is fixed ([#170](https://github.com/ron-rs/ron/pull/170))

### Changes
- Update `serde` requirement to 1.0.60 ([#226](https://github.com/ron-rs/ron/pull/226))
- Replace Travis with GitHub actions ([#223](https://github.com/ron-rs/ron/pull/223))
- Rename `format_doc_comments` to `format_code_in_doc_comment`
- Update base64 requirement from 0.11 to 0.12 ([#204](https://github.com/ron-rs/ron/pull/204))
- Update base64 requirement from 0.10 to 0.11 ([#195](https://github.com/ron-rs/ron/pull/195))
- Update `serde_bytes` to 0.11 ([#164](https://github.com/ron-rs/ron/pull/164))

## [0.5.1] - 2019-04-05
### Fixes
- Increase source compability from Rust `1.34.0` to `1.31.0` by not relying on `as _` imports ([#156](https://github.com/ron-rs/ron/pull/156))

## [0.5.0] - 2019-03-31
### Additions
- Don't insert new lines in empty arrays or maps ([#150](https://github.com/ron-rs/ron/pull/150))
### Changes
- Transition to Rust 2018 ([#149](https://github.com/ron-rs/ron/pull/149))

## [0.4.2] - 2019-03-01
### Additions
- Add integer check for deserializer ([#148](https://github.com/ron-rs/ron/pull/148))
- Implement `Value::into_rust` ([#146](https://github.com/ron-rs/ron/pull/146))

## [0.4.1] - 2019-01-09
### Additions
- Allow underscores in integers ([#135](https://github.com/ron-rs/ron/pull/135))
- Added extension documentation ([#130](https://github.com/ron-rs/ron/pull/130))
### Changes
- Move sublime text syntax to separate repo ([#138](https://github.com/ron-rs/ron/pull/138))
- Update `base64` crate dependency to 0.10 ([#137](https://github.com/ron-rs/ron/pull/137))

## [0.4.0] - 2018-08-11
### Fixes
- Handle tuple deserialization in deserialize_any properly ([#124](https://github.com/ron-rs/ron/pull/124))
### Changes
- Add raw string syntax to grammar ([#125](https://github.com/ron-rs/ron/pull/125))
- Reexport `Value` at root ([#120](https://github.com/ron-rs/ron/pull/120))

## [0.3.0] - 2018-06-15
### Additions
- `serde_bytes` fields to be encoded using base64. ([#109](https://github.com/ron-rs/ron/pull/109))
### Fixes
- Allow raw string literals ([#114](https://github.com/ron-rs/ron/pull/114))
### Changes
- Now depends on `base64` 0.9.2.

## [0.2.2] - 2018-05-19
### Fixes
- Allow whitespace in newtype variants ([#104](https://github.com/ron-rs/ron/pull/104))

## [0.2.1] - 2018-05-04
### Additions
- Add multi-line comments ([#98](https://github.com/ron-rs/ron/pull/98))
### Fixes
- Allow more whitespace inside newtypes ([#103](https://github.com/ron-rs/ron/pull/103))

## [0.2.0] - 2018-02-14
### Additions
- Limit the pretty depth ([#93](https://github.com/ron-rs/ron/pull/93))
- Add support for `\x??` and improve unicode escapes ([#84](https://github.com/ron-rs/ron/pull/84))

## [0.1.7] - 2018-01-24
### Additions
- Deep array indexing ([#88](https://github.com/ron-rs/ron/pull/88))
- Pretty sequence indexing ([#86](https://github.com/ron-rs/ron/pull/86))
- Add unicode support for chars ([#80](https://github.com/ron-rs/ron/pull/80))
- Add support for hex, oct and bin numbers ([#78](https://github.com/ron-rs/ron/pull/78))
- Allow implicit Some ([#75](https://github.com/ron-rs/ron/pull/75))
- Add grammar specification ([#73](https://github.com/ron-rs/ron/pull/73))
- Add extension support and first extension, unwrap_newtypes ([#72](https://github.com/ron-rs/ron/pull/72))
### Fixes
- Directly serialize `f32` ([#81](https://github.com/ron-rs/ron/pull/81))

## [0.1.6] - 2018-01-24
### Additions
- Implement sequence indexing ([#87](https://github.com/ron-rs/ron/pull/87))
### Fixes
- Remove ident variable from Sublime syntax ([#71](https://github.com/ron-rs/ron/pull/71))

## [0.1.5] - 2017-12-27
### Additions
- Allow creating a new serializer ([#70](https://github.com/ron-rs/ron/pull/70))
- Sublime syntax highlighter ([#67](https://github.com/ron-rs/ron/pull/67))
- Add support for integers ([#65](https://github.com/ron-rs/ron/pull/65))
- Implement `Deserializer` for `Value` ([#64](https://github.com/ron-rs/ron/pull/64))

## [0.1.4] - 2017-10-12
### Additions
- Add `PrettyConfig` ([#61](https://github.com/ron-rs/ron/pull/61))
- impl `deserialize_ignored_any` for `id` ([#60](https://github.com/ron-rs/ron/pull/60))
### Fixes
- Fix  deserializing of ignored fields ([#62](https://github.com/ron-rs/ron/pull/62))

## [0.1.3] - 2017-10-06
### Fixes
- Removed indentation from tuple variant pretty encoder ([#57](https://github.com/ron-rs/ron/pull/57))

## [0.1.2] - 2017-10-06
### Fixes
- Fix decoding of string literals ([#56](https://github.com/ron-rs/ron/pull/56))
- Add `Value` and implement `deserialize_any` ([#53](https://github.com/ron-rs/ron/pull/53))

## [0.1.1] - 2017-08-07
### Fixes
- Be more permissive wrt whitespace decoding ([#41](https://github.com/ron-rs/ron/pull/41))
### Additions
- Add utility function to deserialize from `std::io::Read` ([#42](https://github.com/ron-rs/ron/pull/42))

## [0.1.0] - 2015-08-04
### Changes
- Reorganize deserialization modules ([#30](https://github.com/ron-rs/ron/pull/30))
- Rework deserializer not to require `pom` crate [#27](https://github.com/ron-rs/ron/pull/27), ([#38](https://github.com/ron-rs/ron/pull/38))
- Dual license under Apache 2.0 and MIT ([#26](https://github.com/ron-rs/ron/pull/26))
### Fixes
- Use CRLF for serializatio on Windows ([#32](https://github.com/ron-rs/ron/pull/32))
- Fix bors-ng to work with travis ([#31](https://github.com/ron-rs/ron/pull/31))
- Handle escapes ([#23](https://github.com/ron-rs/ron/pull/23))
### Additions
- Improve error reporting ([#29](https://github.com/ron-rs/ron/pull/29))
- Allow decoding of comments ([#28](https://github.com/ron-rs/ron/pull/28))
- Add `pretty` option to serializer ([#25](https://github.com/ron-rs/ron/pull/25))
- Add roundtrip tests ([#24](https://github.com/ron-rs/ron/pull/24))

## [0.0.1] - 2015-07-30
Initial release
