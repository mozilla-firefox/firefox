//! Utilities related to FFI bindings.
//!
//! This module re-exports a small set of types for backwards compatibility.
//! New code should prefer importing these items directly from
//! `core::ffi`, `alloc::ffi`, or `std::ffi` as appropriate.

#[cfg(not(windows))]
#[cfg(any(feature = "alloc", feature = "std"))]
pub use alloc::ffi::{CString, NulError};

pub use core::ffi::{
    c_char, c_int, c_long, c_longlong, c_short, c_uint, c_ulong, c_ulonglong, c_ushort, c_void,
};

#[cfg(not(windows))]
pub use core::ffi::{CStr, FromBytesWithNulError};
