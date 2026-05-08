/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! Generic types for the column properties.
use crate::derives::*;

/// A generic type for `column-count` values.
#[derive(
    Animate,
    Clone,
    ComputeSquaredDistance,
    Copy,
    Debug,
    MallocSizeOf,
    Parse,
    PartialEq,
    SpecifiedValueInfo,
    ToAnimatedValue,
    ToAnimatedZero,
    ToComputedValue,
    ToCss,
    ToResolvedValue,
    ToShmem,
    ToTyped,
)]
#[repr(u8)]
#[typed(todo_derive_fields)]
pub enum GenericColumnCount<PositiveInteger> {
    /// A positive integer.
    Integer(PositiveInteger),
    /// The keyword `auto`.
    #[animation(error)]
    Auto,
}

pub use self::GenericColumnCount as ColumnCount;
impl<I> ColumnCount<I> {
    /// Returns whether this value is `auto`.
    #[inline]
    pub fn is_auto(&self) -> bool {
        matches!(self, ColumnCount::Auto)
    }
}
