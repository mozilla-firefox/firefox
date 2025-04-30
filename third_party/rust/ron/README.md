# Rusty Object Notation

[![MSRV](https://img.shields.io/badge/MSRV-1.64.0-orange)](https://github.com/ron-rs/ron)
[![Crates.io](https://img.shields.io/crates/v/ron.svg)](https://crates.io/crates/ron)
[![Docs](https://docs.rs/ron/badge.svg)](https://docs.rs/ron)

[![CI](https://github.com/ron-rs/ron/actions/workflows/ci.yaml/badge.svg)](https://github.com/ron-rs/ron/actions/workflows/ci.yaml)
[![Coverage](https://img.shields.io/endpoint?url=https%3A%2F%2Fron-rs.github.io%2Fron%2Fcoverage%2Fcoverage.json)](https://ron-rs.github.io/ron/coverage/)
[![Fuzzing](https://oss-fuzz-build-logs.storage.googleapis.com/badges/ron.svg)](https://bugs.chromium.org/p/oss-fuzz/issues/list?sort=-opened&can=1&q=proj:ron)

[![Matrix](https://img.shields.io/matrix/ron-rs:matrix.org.svg)](https://matrix.to/#/#ron-rs:matrix.org)

RON is a simple readable data serialization format that looks similar to Rust syntax.
It's designed to support all of [Serde's data model](https://serde.rs/data-model.html), so
structs, enums, tuples, arrays, generic maps, and primitive values.

## Example

```ron
GameConfig( // optional struct name
    window_size: (800, 600),
    window_title: "PAC-MAN",
    fullscreen: false,

    mouse_sensitivity: 1.4,
    key_bindings: {
        "up": Up,
        "down": Down,
        "left": Left,
        "right": Right,

        // Uncomment to enable WASD controls
        /*
        "W": Up,
        "S": Down,
        "A": Left,
        "D": Right,
        */
    },

    difficulty_options: (
        start_difficulty: Easy,
        adaptive: false,
    ),
)
```

## RON syntax overview

* Numbers: `42`, `3.14`, `0xFF`, `0b0110`
* Strings: `"Hello"`, `"with\\escapes\n"`, `r#"raw string, great for regex\."#`
* Byte Strings: `b"Hello"`, `b"with \x65\x73\x63\x61\x70\x65\x73\n"`, `br#"raw, too"#`
* Booleans: `true`, `false`
* Chars: `'e'`, `'\n'`
* Optionals: `Some("string")`, `Some(Some(1.34))`, `None`
* Tuples: `("abc", 1.23, true)`, `()`
* Lists: `["abc", "def"]`
* Structs: `( foo: 1.0, bar: ( baz: "I'm nested" ) )`
* Maps: `{ "arbitrary": "keys", "are": "allowed" }`

> **Note:** Serde's data model represents fixed-size Rust arrays as tuple (instead of as list)

RON also supports several extensions, which are documented [here](docs/extensions.md).

## Specification

RON's formal and complete grammar is available [here](docs/grammar.md).

There also is a very basic, work in progress specification available on
[the wiki page](https://github.com/ron-rs/ron/wiki/Specification).

## Why RON?

### Example in JSON

```json
{
   "materials": {
        "metal": {
            "reflectivity": 1.0
        },
        "plastic": {
            "reflectivity": 0.5
        }
   },
   "entities": [
        {
            "name": "hero",
            "material": "metal"
        },
        {
            "name": "monster",
            "material": "plastic"
        }
   ]
}
```

### Same example in RON

```ron
Scene( // class name is optional
    materials: { // this is a map
        "metal": (
            reflectivity: 1.0,
        ),
        "plastic": (
            reflectivity: 0.5,
        ),
    },
    entities: [ // this is an array
        (
            name: "hero",
            material: "metal",
        ),
        (
            name: "monster",
            material: "plastic",
        ),
    ],
)
```

Note the following advantages of RON over JSON:

* trailing commas allowed
* single- and multi-line comments
* field names aren't quoted, so it's less verbose
* optional struct names improve readability
* enums are supported (and less verbose than their JSON representation)

## Quickstart

### `Cargo.toml`

```toml
[dependencies]
ron = "0.8"
serde = { version = "1", features = ["derive"] }
```

### `main.rs`

```rust
use serde::{Deserialize, Serialize};

#[derive(Debug, Deserialize, Serialize)]
struct MyStruct {
    boolean: bool,
    float: f32,
}

fn main() {
    let x: MyStruct = ron::from_str("(boolean: true, float: 1.23)").unwrap();

    println!("RON: {}", ron::to_string(&x).unwrap());

    println!("Pretty RON: {}", ron::ser::to_string_pretty(
        &x, ron::ser::PrettyConfig::default()).unwrap(),
    );
}
```

## Tooling

| Editor       | Plugin                                                      |
| ------------ | ----------------------------------------------------------- |
| IntelliJ     | [intellij-ron](https://github.com/ron-rs/intellij-ron)      |
| VS Code      | [a5huynh/vscode-ron](https://github.com/a5huynh/vscode-ron) |
| Sublime Text | [RON](https://packagecontrol.io/packages/RON)               |
| Atom         | [language-ron](https://atom.io/packages/language-ron)       |
| Vim          | [ron-rs/ron.vim](https://github.com/ron-rs/ron.vim)         |
| EMACS        | [emacs-ron]                                                 |

[emacs-ron]: https://chiselapp.com/user/Hutzdog/repository/ron-mode/home

## Limitations

RON requires struct, enum, and variant names to be valid Rust identifiers and will reject invalid ones created by `#[serde(rename = "...")]` at serialization / deserialization time.

RON is not designed to be a fully self-describing format (unlike JSON) and is thus not guaranteed to work when [`deserialize_any`](https://docs.rs/serde/latest/serde/trait.Deserializer.html#tymethod.deserialize_any) is used instead of its typed alternatives. In particular, the following Serde attributes only have limited support:

- `#[serde(tag = "tag")]`, i.e. internally tagged enums [^serde-enum-hack]
- `#[serde(tag = "tag", content = "content")]`, i.e. adjacently tagged enums [^serde-enum-hack]
- `#[serde(untagged)]`, i.e. untagged enums [^serde-enum-hack]
- `#[serde(flatten)]`, i.e. flattening of structs into maps [^serde-flatten-hack]

While data structures with any of these attributes should generally roundtrip through RON, some restrictions apply [^serde-restrictions] and their textual representation may not always match your expectation:

- ron only supports string keys inside maps flattened into structs
- internally (or adjacently) tagged or untagged enum variants or `#[serde(flatten)]`ed fields must not contain:
  - struct names, e.g. by enabling the `#[enable(explicit_struct_names)]` extension or the `PrettyConfig::struct_names` setting
  - newtypes
  - zero-length arrays / tuples / tuple structs / structs / tuple variants / struct variants
    - `Option`s with `#[enable(implicit_some)]` must not contain any of these or a unit, unit struct, or an untagged unit variant
  - externally tagged tuple variants with just one field (that are not newtype variants)
  - tuples or arrays or tuple structs with just one element are not supported inside newtype variants with `#[enable(unwrap_variant_newtypes)]` (including `Some`)
  - a `ron::value::RawValue`
- untagged tuple / struct variants with no fields are not supported
- untagged tuple variants with just one field (that are not newtype variants) are not supported when the `#![enable(unwrap_variant_newtypes)]` extension is enabled
- serializing a `ron::value::RawValue` using a `PrettyConfig` may add leading and trailing whitespace and comments, which the `ron::value::RawValue` absorbs upon deserialization

Furthermore, serde imposes the following restrictions for data to roundtrip:

- structs or struct variants that contain a `#[serde(flatten)]`ed field:
  - are only serialised as maps and deserialised from maps
  - must not contain duplicate fields / keys, e.g. where an inner-struct field matches an outer-struct or inner-struct field
  - must not contain more than one (within the super-struct of all flattened structs) `#[serde(flatten)]`ed map field, which collects all unknown fields
  - if they contain a `#[serde(flatten)]`ed map, they must not contain:
    - a struct that is not flattened itself but contains some flattened fields and is flattened into the outer struct (variant)
    - an untagged struct variant that contains some flattened fields
    - a flattened externally tagged newtype, tuple, or struct variant, flattened internally tagged unit, newtype, or struct variant, or any flattened adjacently tagged variant
    - a flattened tagged struct
- internally (or adjacently) tagged or untagged enum variants or `#[serde(flatten)]`ed fields must not contain:
  - `i128` or `u128` values
- internally tagged newtype variants and `#[serde(flatten)]`ed fields must not contain:
  - a unit or a unit struct inside an untagged newtype variant
  - an untagged unit variant
- internally tagged newtype variants, which are `#[serde(flatten)]`ed together with other fields, must not contain:
  - a unit or unit struct or an untagged unit variant

While RON offers a best-effort implementation for `#[serde(flatten)]`, it may be unsupported in further cases and combinations not listed above. These limitations stem primarily from serde rather than RON. Enumerating all such cases based on serde's behavior is nontrivial, so the lists above are not exhaustive.

Please file a [new issue](https://github.com/ron-rs/ron/issues/new) if you come across a use case which is not listed among the above restrictions but still breaks.

While RON guarantees roundtrips like Rust -> RON -> Rust for Rust types using non-`deserialize_any`-based implementations, RON does not yet make any guarantees about roundtrips through `ron::Value`. For instance, even when RON -> Rust works, RON -> `ron::Value` -> Rust, or RON -> `ron::Value` -> RON -> Rust may not work. We plan on improving `ron::Value` in an upcoming version of RON, though this work is partially blocked on [serde#1183](https://github.com/serde-rs/serde/issues/1183).

[^serde-enum-hack]: Deserialising an internally, adjacently, or un-tagged enum requires detecting `serde`'s internal `serde::__private::de::content::Content` content type so that RON can describe the deserialised data structure in serde's internal JSON-like format. This detection only works for the automatically-derived [`Deserialize`](https://docs.rs/serde/latest/serde/de/trait.Deserialize.html) impls on enums. See [#451](https://github.com/ron-rs/ron/pull/451) for more details.

[^serde-flatten-hack]: Deserialising a flattened struct from a map requires that the struct's [`Visitor::expecting`](https://docs.rs/serde/latest/serde/de/trait.Visitor.html#tymethod.expecting) implementation formats a string starting with `"struct "`. This is the case for automatically-derived [`Deserialize`](https://docs.rs/serde/latest/serde/de/trait.Deserialize.html) impls on structs. See [#455](https://github.com/ron-rs/ron/pull/455) for more details.

[^serde-restrictions]: Most of these restrictions are currently blocked on [serde#1183](https://github.com/serde-rs/serde/issues/1183), which limits non-self-describing formats from roundtripping format-specific information through internally (or adjacently) tagged or untagged enums or `#[serde(flatten)]`ed fields.

## License

RON is dual-licensed under Apache-2.0 and MIT.

Any contribution intentionally submitted for inclusion in the work must be provided under the same dual-license terms.
