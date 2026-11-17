//! Platform-agnostic support types.
//!
//! Copied from https://github.com/rust-lang/libc/blob/008f905256d27eb5dd416d0976678ee6c2855554/src/types.rs

use core::fmt;
use core::mem::MaybeUninit;

/// A transparent wrapper over `MaybeUninit<T>` to represent uninitialized padding
/// while providing `Default`.
// This is restricted to `Copy` types since that's a loose indicator that zeros is actually
// a valid bitpattern. There is no technical reason this is required, though, so it could be
// lifted in the future if it becomes a problem.
#[allow(dead_code)]
#[repr(transparent)]
#[derive(Clone, Copy)]
pub(crate) struct Padding<T: Copy>(MaybeUninit<T>);

impl<T: Copy> Default for Padding<T> {
    fn default() -> Self {
        Self(MaybeUninit::zeroed())
    }
}

impl<T: Copy> Padding<T> {
    /// Create a `Padding` initialized with the given value.
    #[allow(dead_code)]
    pub(crate) const fn new(val: T) -> Self {
        Self(MaybeUninit::new(val))
    }
}

impl<T: Copy> fmt::Debug for Padding<T> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        // Taken from `MaybeUninit`'s debug implementation
        // NB: there is no `.pad_fmt` so we can't use a simpler `format_args!("Padding<{..}>").
        let full_name = core::any::type_name::<Self>();
        let prefix_len = full_name.find("Padding").unwrap();
        f.pad(&full_name[prefix_len..])
    }
}
