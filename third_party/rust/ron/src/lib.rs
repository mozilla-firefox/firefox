#![deny(clippy::correctness)]
#![deny(clippy::suspicious)]
#![deny(clippy::complexity)]
#![deny(clippy::perf)]
#![deny(clippy::style)]
#![warn(clippy::pedantic)]
#![cfg_attr(not(test), deny(clippy::unwrap_used))]
#![cfg_attr(not(test), deny(clippy::expect_used))]
#![deny(clippy::panic)]
#![warn(clippy::todo)]
#![deny(clippy::unimplemented)]
#![deny(clippy::unreachable)]
#![deny(unsafe_code)]
#![allow(clippy::missing_errors_doc)] // FIXME
#![doc = include_str!("../README.md")]
#![doc(html_root_url = "https://docs.rs/ron/0.10.1")]

pub mod de;
pub mod ser;

pub mod error;
pub mod value;

pub mod extensions;

pub mod options;

pub use de::{from_str, Deserializer};
pub use error::{Error, Result};
pub use options::Options;
pub use ser::{to_string, Serializer};
pub use value::{Map, Number, Value};

mod parse;
