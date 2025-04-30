use super::{Value, ValueRef};
use std::borrow::Cow;
use std::error::Error;
use std::fmt;

/// Enum listing possible errors from [`FromSql`] trait.
#[derive(Debug)]
#[non_exhaustive]
pub enum FromSqlError {
    /// Error when an SQLite value is requested, but the type of the result
    /// cannot be converted to the requested Rust type.
    InvalidType,

    /// Error when the i64 value returned by SQLite cannot be stored into the
    /// requested type.
    OutOfRange(i64),

    /// Error when the blob result returned by SQLite cannot be stored into the
    /// requested type due to a size mismatch.
    InvalidBlobSize {
        /// The expected size of the blob.
        expected_size: usize,
        /// The actual size of the blob that was returned.
        blob_size: usize,
    },

    /// An error case available for implementors of the [`FromSql`] trait.
    Other(Box<dyn Error + Send + Sync + 'static>),
}

impl PartialEq for FromSqlError {
    fn eq(&self, other: &Self) -> bool {
        match (self, other) {
            (Self::InvalidType, Self::InvalidType) => true,
            (Self::OutOfRange(n1), Self::OutOfRange(n2)) => n1 == n2,
            (
                Self::InvalidBlobSize {
                    expected_size: es1,
                    blob_size: bs1,
                },
                Self::InvalidBlobSize {
                    expected_size: es2,
                    blob_size: bs2,
                },
            ) => es1 == es2 && bs1 == bs2,
            (..) => false,
        }
    }
}

impl fmt::Display for FromSqlError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match *self {
            Self::InvalidType => write!(f, "Invalid type"),
            Self::OutOfRange(i) => write!(f, "Value {i} out of range"),
            Self::InvalidBlobSize {
                expected_size,
                blob_size,
            } => {
                write!(
                    f,
                    "Cannot read {expected_size} byte value out of {blob_size} byte blob"
                )
            }
            Self::Other(ref err) => err.fmt(f),
        }
    }
}

impl Error for FromSqlError {
    fn source(&self) -> Option<&(dyn Error + 'static)> {
        if let Self::Other(ref err) = self {
            Some(&**err)
        } else {
            None
        }
    }
}

/// Result type for implementors of the [`FromSql`] trait.
pub type FromSqlResult<T> = Result<T, FromSqlError>;

/// A trait for types that can be created from a SQLite value.
pub trait FromSql: Sized {
    /// Converts SQLite value into Rust value.
    fn column_result(value: ValueRef<'_>) -> FromSqlResult<Self>;
}

macro_rules! from_sql_integral(
    ($t:ident) => (
        impl FromSql for $t {
            #[inline]
            fn column_result(value: ValueRef<'_>) -> FromSqlResult<Self> {
                let i = i64::column_result(value)?;
                i.try_into().map_err(|_| FromSqlError::OutOfRange(i))
            }
        }
    );
    (non_zero $nz:ty, $z:ty) => (
        impl FromSql for $nz {
            #[inline]
            fn column_result(value: ValueRef<'_>) -> FromSqlResult<Self> {
                let i = <$z>::column_result(value)?;
                <$nz>::new(i).ok_or(FromSqlError::OutOfRange(0))
            }
        }
    )
);

from_sql_integral!(i8);
from_sql_integral!(i16);
from_sql_integral!(i32);
// from_sql_integral!(i64); // Not needed because the native type is i64.
from_sql_integral!(isize);
from_sql_integral!(u8);
from_sql_integral!(u16);
from_sql_integral!(u32);
from_sql_integral!(u64);
from_sql_integral!(usize);

from_sql_integral!(non_zero std::num::NonZeroIsize, isize);
from_sql_integral!(non_zero std::num::NonZeroI8, i8);
from_sql_integral!(non_zero std::num::NonZeroI16, i16);
from_sql_integral!(non_zero std::num::NonZeroI32, i32);
from_sql_integral!(non_zero std::num::NonZeroI64, i64);
#[cfg(feature = "i128_blob")]
#[cfg_attr(docsrs, doc(cfg(feature = "i128_blob")))]
from_sql_integral!(non_zero std::num::NonZeroI128, i128);

from_sql_integral!(non_zero std::num::NonZeroUsize, usize);
from_sql_integral!(non_zero std::num::NonZeroU8, u8);
from_sql_integral!(non_zero std::num::NonZeroU16, u16);
from_sql_integral!(non_zero std::num::NonZeroU32, u32);
from_sql_integral!(non_zero std::num::NonZeroU64, u64);
// std::num::NonZeroU128 is not supported since u128 isn't either

impl FromSql for i64 {
    #[inline]
    fn column_result(value: ValueRef<'_>) -> FromSqlResult<Self> {
        value.as_i64()
    }
}

impl FromSql for f32 {
    #[inline]
    fn column_result(value: ValueRef<'_>) -> FromSqlResult<Self> {
        match value {
            ValueRef::Integer(i) => Ok(i as Self),
            ValueRef::Real(f) => Ok(f as Self),
            _ => Err(FromSqlError::InvalidType),
        }
    }
}

impl FromSql for f64 {
    #[inline]
    fn column_result(value: ValueRef<'_>) -> FromSqlResult<Self> {
        match value {
            ValueRef::Integer(i) => Ok(i as Self),
            ValueRef::Real(f) => Ok(f),
            _ => Err(FromSqlError::InvalidType),
        }
    }
}

impl FromSql for bool {
    #[inline]
    fn column_result(value: ValueRef<'_>) -> FromSqlResult<Self> {
        i64::column_result(value).map(|i| i != 0)
    }
}

impl FromSql for String {
    #[inline]
    fn column_result(value: ValueRef<'_>) -> FromSqlResult<Self> {
        value.as_str().map(ToString::to_string)
    }
}

impl FromSql for Box<str> {
    #[inline]
    fn column_result(value: ValueRef<'_>) -> FromSqlResult<Self> {
        value.as_str().map(Into::into)
    }
}

impl FromSql for std::rc::Rc<str> {
    #[inline]
    fn column_result(value: ValueRef<'_>) -> FromSqlResult<Self> {
        value.as_str().map(Into::into)
    }
}

impl FromSql for std::sync::Arc<str> {
    #[inline]
    fn column_result(value: ValueRef<'_>) -> FromSqlResult<Self> {
        value.as_str().map(Into::into)
    }
}

impl FromSql for Vec<u8> {
    #[inline]
    fn column_result(value: ValueRef<'_>) -> FromSqlResult<Self> {
        value.as_blob().map(<[u8]>::to_vec)
    }
}

impl FromSql for Box<[u8]> {
    #[inline]
    fn column_result(value: ValueRef<'_>) -> FromSqlResult<Self> {
        value.as_blob().map(Box::<[u8]>::from)
    }
}

impl FromSql for std::rc::Rc<[u8]> {
    #[inline]
    fn column_result(value: ValueRef<'_>) -> FromSqlResult<Self> {
        value.as_blob().map(std::rc::Rc::<[u8]>::from)
    }
}

impl FromSql for std::sync::Arc<[u8]> {
    #[inline]
    fn column_result(value: ValueRef<'_>) -> FromSqlResult<Self> {
        value.as_blob().map(std::sync::Arc::<[u8]>::from)
    }
}

impl<const N: usize> FromSql for [u8; N] {
    #[inline]
    fn column_result(value: ValueRef<'_>) -> FromSqlResult<Self> {
        let slice = value.as_blob()?;
        slice.try_into().map_err(|_| FromSqlError::InvalidBlobSize {
            expected_size: N,
            blob_size: slice.len(),
        })
    }
}

#[cfg(feature = "i128_blob")]
#[cfg_attr(docsrs, doc(cfg(feature = "i128_blob")))]
impl FromSql for i128 {
    #[inline]
    fn column_result(value: ValueRef<'_>) -> FromSqlResult<Self> {
        let bytes = <[u8; 16]>::column_result(value)?;
        Ok(Self::from_be_bytes(bytes) ^ (1_i128 << 127))
    }
}

#[cfg(feature = "uuid")]
#[cfg_attr(docsrs, doc(cfg(feature = "uuid")))]
impl FromSql for uuid::Uuid {
    #[inline]
    fn column_result(value: ValueRef<'_>) -> FromSqlResult<Self> {
        let bytes = <[u8; 16]>::column_result(value)?;
        Ok(Self::from_u128(u128::from_be_bytes(bytes)))
    }
}

impl<T: FromSql> FromSql for Option<T> {
    #[inline]
    fn column_result(value: ValueRef<'_>) -> FromSqlResult<Self> {
        match value {
            ValueRef::Null => Ok(None),
            _ => FromSql::column_result(value).map(Some),
        }
    }
}

impl<T: ?Sized> FromSql for Cow<'_, T>
where
    T: ToOwned,
    T::Owned: FromSql,
{
    #[inline]
    fn column_result(value: ValueRef<'_>) -> FromSqlResult<Self> {
        <T::Owned>::column_result(value).map(Cow::Owned)
    }
}

impl FromSql for Value {
    #[inline]
    fn column_result(value: ValueRef<'_>) -> FromSqlResult<Self> {
        Ok(value.into())
    }
}

#[cfg(test)]
mod test {
    use super::FromSql;
    use crate::{Connection, Error, Result};
    use std::borrow::Cow;
    use std::rc::Rc;
    use std::sync::Arc;

    #[test]
    fn test_integral_ranges() -> Result<()> {
        let db = Connection::open_in_memory()?;

        fn check_ranges<T>(db: &Connection, out_of_range: &[i64], in_range: &[i64])
        where
            T: Into<i64> + FromSql + std::fmt::Debug,
        {
            for n in out_of_range {
                let err = db
                    .query_row("SELECT ?1", [n], |r| r.get::<_, T>(0))
                    .unwrap_err();
                match err {
                    Error::IntegralValueOutOfRange(_, value) => assert_eq!(*n, value),
                    _ => panic!("unexpected error: {err}"),
                }
            }
            for n in in_range {
                assert_eq!(
                    *n,
                    db.query_row("SELECT ?1", [n], |r| r.get::<_, T>(0))
                        .unwrap()
                        .into()
                );
            }
        }

        check_ranges::<i8>(&db, &[-129, 128], &[-128, 0, 1, 127]);
        check_ranges::<i16>(&db, &[-32769, 32768], &[-32768, -1, 0, 1, 32767]);
        check_ranges::<i32>(
            &db,
            &[-2_147_483_649, 2_147_483_648],
            &[-2_147_483_648, -1, 0, 1, 2_147_483_647],
        );
        check_ranges::<u8>(&db, &[-2, -1, 256], &[0, 1, 255]);
        check_ranges::<u16>(&db, &[-2, -1, 65536], &[0, 1, 65535]);
        check_ranges::<u32>(&db, &[-2, -1, 4_294_967_296], &[0, 1, 4_294_967_295]);
        Ok(())
    }

    #[test]
    fn test_nonzero_ranges() -> Result<()> {
        let db = Connection::open_in_memory()?;

        macro_rules! check_ranges {
            ($nz:ty, $out_of_range:expr, $in_range:expr) => {
                for &n in $out_of_range {
                    assert_eq!(
                        db.query_row("SELECT ?1", [n], |r| r.get::<_, $nz>(0)),
                        Err(Error::IntegralValueOutOfRange(0, n)),
                        "{}",
                        std::any::type_name::<$nz>()
                    );
                }
                for &n in $in_range {
                    let non_zero = <$nz>::new(n).unwrap();
                    assert_eq!(
                        Ok(non_zero),
                        db.query_row("SELECT ?1", [non_zero], |r| r.get::<_, $nz>(0))
                    );
                }
            };
        }

        check_ranges!(std::num::NonZeroI8, &[0, -129, 128], &[-128, 1, 127]);
        check_ranges!(
            std::num::NonZeroI16,
            &[0, -32769, 32768],
            &[-32768, -1, 1, 32767]
        );
        check_ranges!(
            std::num::NonZeroI32,
            &[0, -2_147_483_649, 2_147_483_648],
            &[-2_147_483_648, -1, 1, 2_147_483_647]
        );
        check_ranges!(
            std::num::NonZeroI64,
            &[0],
            &[-2_147_483_648, -1, 1, 2_147_483_647, i64::MAX, i64::MIN]
        );
        check_ranges!(
            std::num::NonZeroIsize,
            &[0],
            &[-2_147_483_648, -1, 1, 2_147_483_647]
        );
        check_ranges!(std::num::NonZeroU8, &[0, -2, -1, 256], &[1, 255]);
        check_ranges!(std::num::NonZeroU16, &[0, -2, -1, 65536], &[1, 65535]);
        check_ranges!(
            std::num::NonZeroU32,
            &[0, -2, -1, 4_294_967_296],
            &[1, 4_294_967_295]
        );
        check_ranges!(
            std::num::NonZeroU64,
            &[0, -2, -1, -4_294_967_296],
            &[1, 4_294_967_295, i64::MAX as u64]
        );
        check_ranges!(
            std::num::NonZeroUsize,
            &[0, -2, -1, -4_294_967_296],
            &[1, 4_294_967_295]
        );

        Ok(())
    }

    #[test]
    fn test_cow() -> Result<()> {
        let db = Connection::open_in_memory()?;

        assert_eq!(
            db.query_row("SELECT 'this is a string'", [], |r| r
                .get::<_, Cow<'_, str>>(0)),
            Ok(Cow::Borrowed("this is a string")),
        );
        assert_eq!(
            db.query_row("SELECT x'09ab20fdee87'", [], |r| r
                .get::<_, Cow<'_, [u8]>>(0)),
            Ok(Cow::Owned(vec![0x09, 0xab, 0x20, 0xfd, 0xee, 0x87])),
        );
        assert_eq!(
            db.query_row("SELECT 24.5", [], |r| r.get::<_, Cow<'_, f32>>(0),),
            Ok(Cow::Borrowed(&24.5)),
        );

        Ok(())
    }

    #[test]
    fn test_heap_slice() -> Result<()> {
        let db = Connection::open_in_memory()?;

        assert_eq!(
            db.query_row("SELECT 'Some string slice!'", [], |r| r
                .get::<_, Rc<str>>(0)),
            Ok(Rc::from("Some string slice!")),
        );
        assert_eq!(
            db.query_row("SELECT x'012366779988fedc'", [], |r| r
                .get::<_, Rc<[u8]>>(0)),
            Ok(Rc::from(b"\x01\x23\x66\x77\x99\x88\xfe\xdc".as_slice())),
        );

        assert_eq!(
            db.query_row(
                "SELECT x'6120737472696e672043414e206265206120626c6f62'",
                [],
                |r| r.get::<_, Box<[u8]>>(0)
            ),
            Ok(b"a string CAN be a blob".to_vec().into_boxed_slice()),
        );
        assert_eq!(
            db.query_row("SELECT 'This is inside an Arc.'", [], |r| r
                .get::<_, Arc<str>>(0)),
            Ok(Arc::from("This is inside an Arc.")),
        );
        assert_eq!(
            db.query_row("SELECT x'afd374'", [], |r| r.get::<_, Arc<[u8]>>(0),),
            Ok(Arc::from(b"\xaf\xd3\x74".as_slice())),
        );

        Ok(())
    }
}
