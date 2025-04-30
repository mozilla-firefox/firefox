use std::{
    cmp::{Eq, Ordering},
    hash::{Hash, Hasher},
};

use serde::{de::Visitor, Serialize, Serializer};

/// A wrapper for any numeric primitive type in Rust.
///
/// Some varints of the `Number` enum are enabled by features:
/// - `Number::I128` and `Number::U128` by the `integer128` feature
///
/// To ensure that feature unification does not break `match`ing over `Number`,
/// the `Number` enum is non-exhaustive.
///
/// <details>
/// <summary>Exhaustively matching on <code>Number</code> in tests</summary>
///
/// If you want to ensure that you exhaustively handle every variant, you can
/// match on the hidden `Number::__NonExhaustive` variant.
///
/// <div class="warning">
/// Matching on this variant means that your code may break when RON is
/// upgraded or when feature unification enables further variants in the
/// <code>Number</code> enum than your code expects.
/// </div>
///
/// It is your responsibility to only *ever* match on `Number::__NonExhaustive`
/// inside tests, e.g. by using `#[cfg(test)]` on the particular match arm, to
/// ensure that only your tests break (e.g. in CI) when your code is not
/// exhaustively matching on every variant, e.g. after a version upgrade or
/// feature unification.
/// </details>
#[derive(Copy, Clone, Debug, PartialEq, PartialOrd, Eq, Hash, Ord)]
#[cfg_attr(doc, non_exhaustive)]
pub enum Number {
    I8(i8),
    I16(i16),
    I32(i32),
    I64(i64),
    #[cfg(feature = "integer128")]
    I128(i128),
    U8(u8),
    U16(u16),
    U32(u32),
    U64(u64),
    #[cfg(feature = "integer128")]
    U128(u128),
    F32(F32),
    F64(F64),
    #[cfg(not(doc))]
    #[allow(private_interfaces)]
    __NonExhaustive(private::Never),
}

mod private {
    #[derive(Copy, Clone, Debug, PartialEq, PartialOrd, Eq, Hash, Ord)]
    pub enum Never {}
}

impl Serialize for Number {
    fn serialize<S: Serializer>(&self, serializer: S) -> Result<S::Ok, S::Error> {
        match self {
            Self::I8(v) => serializer.serialize_i8(*v),
            Self::I16(v) => serializer.serialize_i16(*v),
            Self::I32(v) => serializer.serialize_i32(*v),
            Self::I64(v) => serializer.serialize_i64(*v),
            #[cfg(feature = "integer128")]
            Self::I128(v) => serializer.serialize_i128(*v),
            Self::U8(v) => serializer.serialize_u8(*v),
            Self::U16(v) => serializer.serialize_u16(*v),
            Self::U32(v) => serializer.serialize_u32(*v),
            Self::U64(v) => serializer.serialize_u64(*v),
            #[cfg(feature = "integer128")]
            Self::U128(v) => serializer.serialize_u128(*v),
            Self::F32(v) => serializer.serialize_f32(v.get()),
            Self::F64(v) => serializer.serialize_f64(v.get()),
            #[cfg(not(doc))]
            Self::__NonExhaustive(never) => match *never {},
        }
    }
}

impl Number {
    pub fn visit<'de, V: Visitor<'de>, E: serde::de::Error>(
        &self,
        visitor: V,
    ) -> Result<V::Value, E> {
        match self {
            Self::I8(v) => visitor.visit_i8(*v),
            Self::I16(v) => visitor.visit_i16(*v),
            Self::I32(v) => visitor.visit_i32(*v),
            Self::I64(v) => visitor.visit_i64(*v),
            #[cfg(feature = "integer128")]
            Self::I128(v) => visitor.visit_i128(*v),
            Self::U8(v) => visitor.visit_u8(*v),
            Self::U16(v) => visitor.visit_u16(*v),
            Self::U32(v) => visitor.visit_u32(*v),
            Self::U64(v) => visitor.visit_u64(*v),
            #[cfg(feature = "integer128")]
            Self::U128(v) => visitor.visit_u128(*v),
            Self::F32(v) => visitor.visit_f32(v.get()),
            Self::F64(v) => visitor.visit_f64(v.get()),
            #[cfg(not(doc))]
            Self::__NonExhaustive(never) => match *never {},
        }
    }
}

macro_rules! float_ty {
    ($ty:ident($float:ty)) => {
        #[doc = concat!(
                    "A wrapper for [`", stringify!($float), "`], which implements [`Eq`], ",
                    "[`Hash`] and [`Ord`] using [`", stringify!($float), "::total_cmp`] ",
                    "for a total order comparison",
                )]
        #[derive(Copy, Clone, Debug)] // GRCOV_EXCL_LINE
        pub struct $ty(pub $float);

        impl $ty {
            #[doc = concat!("Construct a new [`", stringify!($ty), "`].")]
            #[must_use]
            pub fn new(v: $float) -> Self {
                Self(v)
            }

            #[doc = concat!("Returns the wrapped [`", stringify!($float), "`].")]
            #[must_use]
            pub fn get(self) -> $float {
                self.0
            }
        }

        impl From<$float> for $ty {
            fn from(v: $float) -> Self {
                Self::new(v)
            }
        }

        /// Partial equality comparison
        ///
        #[doc = concat!(
                    "In order to be able to use [`", stringify!($ty), "`] as a mapping key, ",
                    "floating values use [`", stringify!($float), "::total_cmp`] for a total ",
                    "order comparison.",
                )]
        ///
        /// See the [`Ord`] implementation.
        impl PartialEq for $ty {
            fn eq(&self, other: &Self) -> bool {
                self.cmp(other).is_eq()
            }
        }

        /// Equality comparison
        ///
        #[doc = concat!(
                    "In order to be able to use [`", stringify!($ty), "`] as a mapping key, ",
                    "floating values use [`", stringify!($float), "::total_cmp`] for a total ",
                    "order comparison.",
                )]
        ///
        /// See the [`Ord`] implementation.
        impl Eq for $ty {}

        impl Hash for $ty {
            fn hash<H: Hasher>(&self, state: &mut H) {
                self.0.to_bits().hash(state);
            }
        }

        /// Partial ordering comparison
        ///
        #[doc = concat!(
                    "In order to be able to use [`", stringify!($ty), "`] as a mapping key, ",
                    "floating values use [`", stringify!($float), "::total_cmp`] for a total ",
                    "order comparison.",
                )]
        ///
        /// See the [`Ord`] implementation.
        impl PartialOrd for $ty {
            fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
                Some(self.cmp(other))
            }
        }

        /// Ordering comparison
        ///
        #[doc = concat!(
                    "In order to be able to use [`", stringify!($ty), "`] as a mapping key, ",
                    "floating values use [`", stringify!($float), "::total_cmp`] for a total ",
                    "order comparison.",
                )]
        ///
        /// ```
        #[doc = concat!("use ron::value::", stringify!($ty), ";")]
        #[doc = concat!(
                    "assert!(", stringify!($ty), "::new(", stringify!($float), "::NAN) > ",
                    stringify!($ty), "::new(", stringify!($float), "::INFINITY));",
                )]
        #[doc = concat!(
                    "assert!(", stringify!($ty), "::new(-", stringify!($float), "::NAN) < ",
                    stringify!($ty), "::new(", stringify!($float), "::NEG_INFINITY));",
                )]
        #[doc = concat!(
                    "assert!(", stringify!($ty), "::new(", stringify!($float), "::NAN) == ",
                    stringify!($ty), "::new(", stringify!($float), "::NAN));",
                )]
        /// ```
        impl Ord for $ty {
            fn cmp(&self, other: &Self) -> Ordering {
                self.0.total_cmp(&other.0)
            }
        }
    };
}

float_ty! { F32(f32) }
float_ty! { F64(f64) }

impl Number {
    /// Construct a new number.
    pub fn new(v: impl Into<Number>) -> Self {
        v.into()
    }

    /// Returns the [`f64`] representation of the [`Number`] regardless of
    /// whether the number is stored as a float or integer.
    ///
    /// # Example
    ///
    /// ```
    /// # use ron::value::Number;
    /// let i = Number::new(5);
    /// let f = Number::new(2.0);
    /// assert_eq!(i.into_f64(), 5.0);
    /// assert_eq!(f.into_f64(), 2.0);
    /// ```
    #[must_use]
    pub fn into_f64(self) -> f64 {
        #[allow(clippy::cast_precision_loss)]
        match self {
            Number::I8(v) => f64::from(v),
            Number::I16(v) => f64::from(v),
            Number::I32(v) => f64::from(v),
            Number::I64(v) => v as f64,
            #[cfg(feature = "integer128")]
            Number::I128(v) => v as f64,
            Number::U8(v) => f64::from(v),
            Number::U16(v) => f64::from(v),
            Number::U32(v) => f64::from(v),
            Number::U64(v) => v as f64,
            #[cfg(feature = "integer128")]
            Number::U128(v) => v as f64,
            Number::F32(v) => f64::from(v.get()),
            Number::F64(v) => v.get(),
            #[cfg(not(doc))]
            Self::__NonExhaustive(never) => match never {},
        }
    }
}

macro_rules! number_from_impl {
    (Number::$variant:ident($wrap:ident($ty:ty))) => {
        impl From<$ty> for Number {
            fn from(v: $ty) -> Number {
                Number::$variant($wrap(v))
            }
        }
    };
    (Number::$variant:ident($ty:ty)) => {
        impl From<$ty> for Number {
            fn from(v: $ty) -> Number {
                Number::$variant(v)
            }
        }
    };
}

number_from_impl! { Number::I8(i8) }
number_from_impl! { Number::I16(i16) }
number_from_impl! { Number::I32(i32) }
number_from_impl! { Number::I64(i64) }
#[cfg(feature = "integer128")]
number_from_impl! { Number::I128(i128) }
number_from_impl! { Number::U8(u8) }
number_from_impl! { Number::U16(u16) }
number_from_impl! { Number::U32(u32) }
number_from_impl! { Number::U64(u64) }
#[cfg(feature = "integer128")]
number_from_impl! { Number::U128(u128) }
number_from_impl! { Number::F32(F32(f32)) }
number_from_impl! { Number::F64(F64(f64)) }

#[cfg(test)]
mod tests {
    use std::collections::hash_map::DefaultHasher;
    use std::hash::{Hash, Hasher};

    use super::*;

    fn hash<T: Hash>(v: &T) -> u64 {
        let mut state = DefaultHasher::new();
        v.hash(&mut state);
        state.finish()
    }

    #[test]
    fn test_nan() {
        assert_eq!(F32(f32::NAN), F32(f32::NAN));
        assert_eq!(F32(-f32::NAN), F32(-f32::NAN));
        assert_ne!(F32(f32::NAN), F32(-f32::NAN));

        assert_eq!(hash(&F32(f32::NAN)), hash(&F32(f32::NAN)));
        assert_eq!(hash(&F32(-f32::NAN)), hash(&F32(-f32::NAN)));
        assert_ne!(hash(&F32(f32::NAN)), hash(&F32(-f32::NAN)));
    }

    #[test]
    fn test_partial_ord() {
        assert!(F32(f32::NAN) > F32(f32::INFINITY));
        assert!(F32(-f32::NAN) < F32(f32::NEG_INFINITY));
        assert!(F32(f32::NAN) == F32(f32::NAN));
    }
}
