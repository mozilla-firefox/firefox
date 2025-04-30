use crate::types::FromSqlError;
use crate::types::Type;
use crate::{errmsg_to_string, ffi, Result};
use std::error;
use std::fmt;
use std::os::raw::c_int;
use std::path::PathBuf;
use std::str;

/// Enum listing possible errors from rusqlite.
#[derive(Debug)]
#[non_exhaustive]
pub enum Error {
    /// An error from an underlying SQLite call.
    SqliteFailure(ffi::Error, Option<String>),

    /// Error reported when attempting to open a connection when SQLite was
    /// configured to allow single-threaded use only.
    SqliteSingleThreadedMode,

    /// Error when the value of a particular column is requested, but it cannot
    /// be converted to the requested Rust type.
    FromSqlConversionFailure(usize, Type, Box<dyn error::Error + Send + Sync + 'static>),

    /// Error when SQLite gives us an integral value outside the range of the
    /// requested type (e.g., trying to get the value 1000 into a `u8`).
    /// The associated `usize` is the column index,
    /// and the associated `i64` is the value returned by SQLite.
    IntegralValueOutOfRange(usize, i64),

    /// Error converting a string to UTF-8.
    Utf8Error(str::Utf8Error),

    /// Error converting a string to a C-compatible string because it contained
    /// an embedded nul.
    NulError(std::ffi::NulError),

    /// Error when using SQL named parameters and passing a parameter name not
    /// present in the SQL.
    InvalidParameterName(String),

    /// Error converting a file path to a string.
    InvalidPath(PathBuf),

    /// Error returned when an [`execute`](crate::Connection::execute) call
    /// returns rows.
    ExecuteReturnedResults,

    /// Error when a query that was expected to return at least one row (e.g.,
    /// for [`query_row`](crate::Connection::query_row)) did not return any.
    QueryReturnedNoRows,

    /// Error when the value of a particular column is requested, but the index
    /// is out of range for the statement.
    InvalidColumnIndex(usize),

    /// Error when the value of a named column is requested, but no column
    /// matches the name for the statement.
    InvalidColumnName(String),

    /// Error when the value of a particular column is requested, but the type
    /// of the result in that column cannot be converted to the requested
    /// Rust type.
    InvalidColumnType(usize, String, Type),

    /// Error when a query that was expected to insert one row did not insert
    /// any or insert many.
    StatementChangedRows(usize),

    /// Error returned by
    /// [`functions::Context::get`](crate::functions::Context::get) when the
    /// function argument cannot be converted to the requested type.
    #[cfg(feature = "functions")]
    #[cfg_attr(docsrs, doc(cfg(feature = "functions")))]
    InvalidFunctionParameterType(usize, Type),
    /// Error returned by [`vtab::Values::get`](crate::vtab::Values::get) when
    /// the filter argument cannot be converted to the requested type.
    #[cfg(feature = "vtab")]
    #[cfg_attr(docsrs, doc(cfg(feature = "vtab")))]
    InvalidFilterParameterType(usize, Type),

    /// An error case available for implementors of custom user functions (e.g.,
    /// [`create_scalar_function`](crate::Connection::create_scalar_function)).
    #[cfg(feature = "functions")]
    #[cfg_attr(docsrs, doc(cfg(feature = "functions")))]
    UserFunctionError(Box<dyn error::Error + Send + Sync + 'static>),

    /// Error available for the implementors of the
    /// [`ToSql`](crate::types::ToSql) trait.
    ToSqlConversionFailure(Box<dyn error::Error + Send + Sync + 'static>),

    /// Error when the SQL is not a `SELECT`, is not read-only.
    InvalidQuery,

    /// An error case available for implementors of custom modules (e.g.,
    /// [`create_module`](crate::Connection::create_module)).
    #[cfg(feature = "vtab")]
    #[cfg_attr(docsrs, doc(cfg(feature = "vtab")))]
    ModuleError(String),

    /// An unwinding panic occurs in a UDF (user-defined function).
    UnwindingPanic,

    /// An error returned when
    /// [`Context::get_aux`](crate::functions::Context::get_aux) attempts to
    /// retrieve data of a different type than what had been stored using
    /// [`Context::set_aux`](crate::functions::Context::set_aux).
    #[cfg(feature = "functions")]
    #[cfg_attr(docsrs, doc(cfg(feature = "functions")))]
    GetAuxWrongType,

    /// Error when the SQL contains multiple statements.
    MultipleStatement,
    /// Error when the number of bound parameters does not match the number of
    /// parameters in the query. The first `usize` is how many parameters were
    /// given, the 2nd is how many were expected.
    InvalidParameterCount(usize, usize),

    /// Returned from various functions in the Blob IO positional API. For
    /// example,
    /// [`Blob::raw_read_at_exact`](crate::blob::Blob::raw_read_at_exact) will
    /// return it if the blob has insufficient data.
    #[cfg(feature = "blob")]
    #[cfg_attr(docsrs, doc(cfg(feature = "blob")))]
    BlobSizeError,
    /// Error referencing a specific token in the input SQL
    #[cfg(feature = "modern_sqlite")] // 3.38.0
    #[cfg_attr(docsrs, doc(cfg(feature = "modern_sqlite")))]
    SqlInputError {
        /// error code
        error: ffi::Error,
        /// error message
        msg: String,
        /// SQL input
        sql: String,
        /// byte offset of the start of invalid token
        offset: c_int,
    },
    /// Loadable extension initialization error
    #[cfg(feature = "loadable_extension")]
    #[cfg_attr(docsrs, doc(cfg(feature = "loadable_extension")))]
    InitError(ffi::InitError),
    /// Error when the schema of a particular database is requested, but the index
    /// is out of range.
    #[cfg(feature = "modern_sqlite")] // 3.39.0
    #[cfg_attr(docsrs, doc(cfg(feature = "modern_sqlite")))]
    InvalidDatabaseIndex(usize),
}

impl PartialEq for Error {
    fn eq(&self, other: &Self) -> bool {
        match (self, other) {
            (Self::SqliteFailure(e1, s1), Self::SqliteFailure(e2, s2)) => e1 == e2 && s1 == s2,
            (Self::SqliteSingleThreadedMode, Self::SqliteSingleThreadedMode) => true,
            (Self::IntegralValueOutOfRange(i1, n1), Self::IntegralValueOutOfRange(i2, n2)) => {
                i1 == i2 && n1 == n2
            }
            (Self::Utf8Error(e1), Self::Utf8Error(e2)) => e1 == e2,
            (Self::NulError(e1), Self::NulError(e2)) => e1 == e2,
            (Self::InvalidParameterName(n1), Self::InvalidParameterName(n2)) => n1 == n2,
            (Self::InvalidPath(p1), Self::InvalidPath(p2)) => p1 == p2,
            (Self::ExecuteReturnedResults, Self::ExecuteReturnedResults) => true,
            (Self::QueryReturnedNoRows, Self::QueryReturnedNoRows) => true,
            (Self::InvalidColumnIndex(i1), Self::InvalidColumnIndex(i2)) => i1 == i2,
            (Self::InvalidColumnName(n1), Self::InvalidColumnName(n2)) => n1 == n2,
            (Self::InvalidColumnType(i1, n1, t1), Self::InvalidColumnType(i2, n2, t2)) => {
                i1 == i2 && t1 == t2 && n1 == n2
            }
            (Self::StatementChangedRows(n1), Self::StatementChangedRows(n2)) => n1 == n2,
            #[cfg(feature = "functions")]
            (
                Self::InvalidFunctionParameterType(i1, t1),
                Self::InvalidFunctionParameterType(i2, t2),
            ) => i1 == i2 && t1 == t2,
            #[cfg(feature = "vtab")]
            (
                Self::InvalidFilterParameterType(i1, t1),
                Self::InvalidFilterParameterType(i2, t2),
            ) => i1 == i2 && t1 == t2,
            (Self::InvalidQuery, Self::InvalidQuery) => true,
            #[cfg(feature = "vtab")]
            (Self::ModuleError(s1), Self::ModuleError(s2)) => s1 == s2,
            (Self::UnwindingPanic, Self::UnwindingPanic) => true,
            #[cfg(feature = "functions")]
            (Self::GetAuxWrongType, Self::GetAuxWrongType) => true,
            (Self::InvalidParameterCount(i1, n1), Self::InvalidParameterCount(i2, n2)) => {
                i1 == i2 && n1 == n2
            }
            #[cfg(feature = "blob")]
            (Self::BlobSizeError, Self::BlobSizeError) => true,
            #[cfg(feature = "modern_sqlite")]
            (
                Self::SqlInputError {
                    error: e1,
                    msg: m1,
                    sql: s1,
                    offset: o1,
                },
                Self::SqlInputError {
                    error: e2,
                    msg: m2,
                    sql: s2,
                    offset: o2,
                },
            ) => e1 == e2 && m1 == m2 && s1 == s2 && o1 == o2,
            #[cfg(feature = "loadable_extension")]
            (Self::InitError(e1), Self::InitError(e2)) => e1 == e2,
            #[cfg(feature = "modern_sqlite")]
            (Self::InvalidDatabaseIndex(i1), Self::InvalidDatabaseIndex(i2)) => i1 == i2,
            (..) => false,
        }
    }
}

impl From<str::Utf8Error> for Error {
    #[cold]
    fn from(err: str::Utf8Error) -> Self {
        Self::Utf8Error(err)
    }
}

impl From<std::ffi::NulError> for Error {
    #[cold]
    fn from(err: std::ffi::NulError) -> Self {
        Self::NulError(err)
    }
}

const UNKNOWN_COLUMN: usize = usize::MAX;

/// The conversion isn't precise, but it's convenient to have it
/// to allow use of `get_raw(…).as_…()?` in callbacks that take `Error`.
impl From<FromSqlError> for Error {
    #[cold]
    fn from(err: FromSqlError) -> Self {
        // The error type requires index and type fields, but they aren't known in this
        // context.
        match err {
            FromSqlError::OutOfRange(val) => Self::IntegralValueOutOfRange(UNKNOWN_COLUMN, val),
            FromSqlError::InvalidBlobSize { .. } => {
                Self::FromSqlConversionFailure(UNKNOWN_COLUMN, Type::Blob, Box::new(err))
            }
            FromSqlError::Other(source) => {
                Self::FromSqlConversionFailure(UNKNOWN_COLUMN, Type::Null, source)
            }
            _ => Self::FromSqlConversionFailure(UNKNOWN_COLUMN, Type::Null, Box::new(err)),
        }
    }
}

#[cfg(feature = "loadable_extension")]
impl From<ffi::InitError> for Error {
    #[cold]
    fn from(err: ffi::InitError) -> Self {
        Self::InitError(err)
    }
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match *self {
            Self::SqliteFailure(ref err, None) => err.fmt(f),
            Self::SqliteFailure(_, Some(ref s)) => write!(f, "{s}"),
            Self::SqliteSingleThreadedMode => write!(
                f,
                "SQLite was compiled or configured for single-threaded use only"
            ),
            Self::FromSqlConversionFailure(i, ref t, ref err) => {
                if i != UNKNOWN_COLUMN {
                    write!(f, "Conversion error from type {t} at index: {i}, {err}")
                } else {
                    err.fmt(f)
                }
            }
            Self::IntegralValueOutOfRange(col, val) => {
                if col != UNKNOWN_COLUMN {
                    write!(f, "Integer {val} out of range at index {col}")
                } else {
                    write!(f, "Integer {val} out of range")
                }
            }
            Self::Utf8Error(ref err) => err.fmt(f),
            Self::NulError(ref err) => err.fmt(f),
            Self::InvalidParameterName(ref name) => write!(f, "Invalid parameter name: {name}"),
            Self::InvalidPath(ref p) => write!(f, "Invalid path: {}", p.to_string_lossy()),
            Self::ExecuteReturnedResults => {
                write!(f, "Execute returned results - did you mean to call query?")
            }
            Self::QueryReturnedNoRows => write!(f, "Query returned no rows"),
            Self::InvalidColumnIndex(i) => write!(f, "Invalid column index: {i}"),
            Self::InvalidColumnName(ref name) => write!(f, "Invalid column name: {name}"),
            Self::InvalidColumnType(i, ref name, ref t) => {
                write!(f, "Invalid column type {t} at index: {i}, name: {name}")
            }
            Self::InvalidParameterCount(i1, n1) => write!(
                f,
                "Wrong number of parameters passed to query. Got {i1}, needed {n1}"
            ),
            Self::StatementChangedRows(i) => write!(f, "Query changed {i} rows"),

            #[cfg(feature = "functions")]
            Self::InvalidFunctionParameterType(i, ref t) => {
                write!(f, "Invalid function parameter type {t} at index {i}")
            }
            #[cfg(feature = "vtab")]
            Self::InvalidFilterParameterType(i, ref t) => {
                write!(f, "Invalid filter parameter type {t} at index {i}")
            }
            #[cfg(feature = "functions")]
            Self::UserFunctionError(ref err) => err.fmt(f),
            Self::ToSqlConversionFailure(ref err) => err.fmt(f),
            Self::InvalidQuery => write!(f, "Query is not read-only"),
            #[cfg(feature = "vtab")]
            Self::ModuleError(ref desc) => write!(f, "{desc}"),
            Self::UnwindingPanic => write!(f, "unwinding panic"),
            #[cfg(feature = "functions")]
            Self::GetAuxWrongType => write!(f, "get_aux called with wrong type"),
            Self::MultipleStatement => write!(f, "Multiple statements provided"),
            #[cfg(feature = "blob")]
            Self::BlobSizeError => "Blob size is insufficient".fmt(f),
            #[cfg(feature = "modern_sqlite")]
            Self::SqlInputError {
                ref msg,
                offset,
                ref sql,
                ..
            } => write!(f, "{msg} in {sql} at offset {offset}"),
            #[cfg(feature = "loadable_extension")]
            Self::InitError(ref err) => err.fmt(f),
            #[cfg(feature = "modern_sqlite")]
            Self::InvalidDatabaseIndex(i) => write!(f, "Invalid database index: {i}"),
        }
    }
}

impl error::Error for Error {
    fn source(&self) -> Option<&(dyn error::Error + 'static)> {
        match *self {
            Self::SqliteFailure(ref err, _) => Some(err),
            Self::Utf8Error(ref err) => Some(err),
            Self::NulError(ref err) => Some(err),

            Self::IntegralValueOutOfRange(..)
            | Self::SqliteSingleThreadedMode
            | Self::InvalidParameterName(_)
            | Self::ExecuteReturnedResults
            | Self::QueryReturnedNoRows
            | Self::InvalidColumnIndex(_)
            | Self::InvalidColumnName(_)
            | Self::InvalidColumnType(..)
            | Self::InvalidPath(_)
            | Self::InvalidParameterCount(..)
            | Self::StatementChangedRows(_)
            | Self::InvalidQuery
            | Self::MultipleStatement => None,

            #[cfg(feature = "functions")]
            Self::InvalidFunctionParameterType(..) => None,
            #[cfg(feature = "vtab")]
            Self::InvalidFilterParameterType(..) => None,

            #[cfg(feature = "functions")]
            Self::UserFunctionError(ref err) => Some(&**err),

            Self::FromSqlConversionFailure(_, _, ref err)
            | Self::ToSqlConversionFailure(ref err) => Some(&**err),

            #[cfg(feature = "vtab")]
            Self::ModuleError(_) => None,

            Self::UnwindingPanic => None,

            #[cfg(feature = "functions")]
            Self::GetAuxWrongType => None,

            #[cfg(feature = "blob")]
            Self::BlobSizeError => None,
            #[cfg(feature = "modern_sqlite")]
            Self::SqlInputError { ref error, .. } => Some(error),
            #[cfg(feature = "loadable_extension")]
            Self::InitError(ref err) => Some(err),
            #[cfg(feature = "modern_sqlite")]
            Self::InvalidDatabaseIndex(_) => None,
        }
    }
}

impl Error {
    /// Returns the underlying SQLite error if this is [`Error::SqliteFailure`].
    #[inline]
    #[must_use]
    pub fn sqlite_error(&self) -> Option<&ffi::Error> {
        match self {
            Self::SqliteFailure(error, _) => Some(error),
            _ => None,
        }
    }

    /// Returns the underlying SQLite error code if this is
    /// [`Error::SqliteFailure`].
    #[inline]
    #[must_use]
    pub fn sqlite_error_code(&self) -> Option<ffi::ErrorCode> {
        self.sqlite_error().map(|error| error.code)
    }
}

// These are public but not re-exported by lib.rs, so only visible within crate.

#[cold]
pub fn error_from_sqlite_code(code: c_int, message: Option<String>) -> Error {
    Error::SqliteFailure(ffi::Error::new(code), message)
}

macro_rules! err {
    ($code:expr $(,)?) => {
        $crate::error::error_from_sqlite_code($code, None)
    };
    ($code:expr, $msg:literal $(,)?) => {
        $crate::error::error_from_sqlite_code($code, Some(format!($msg)))
    };
    ($code:expr, $err:expr $(,)?) => {
        $crate::error::error_from_sqlite_code($code, Some(format!($err)))
    };
    ($code:expr, $fmt:expr, $($arg:tt)*) => {
        $crate::error::error_from_sqlite_code($code, Some(format!($fmt, $($arg)*)))
    };
}

#[cold]
pub unsafe fn error_from_handle(db: *mut ffi::sqlite3, code: c_int) -> Error {
    error_from_sqlite_code(code, error_msg(db, code))
}

unsafe fn error_msg(db: *mut ffi::sqlite3, code: c_int) -> Option<String> {
    if db.is_null() || ffi::sqlite3_errcode(db) != code {
        let err_str = ffi::sqlite3_errstr(code);
        if err_str.is_null() {
            None
        } else {
            Some(errmsg_to_string(err_str))
        }
    } else {
        Some(errmsg_to_string(ffi::sqlite3_errmsg(db)))
    }
}

pub unsafe fn decode_result_raw(db: *mut ffi::sqlite3, code: c_int) -> Result<()> {
    if code == ffi::SQLITE_OK {
        Ok(())
    } else {
        Err(error_from_handle(db, code))
    }
}

#[cold]
#[cfg(not(feature = "modern_sqlite"))] // SQLite >= 3.38.0
pub unsafe fn error_with_offset(db: *mut ffi::sqlite3, code: c_int, _sql: &str) -> Error {
    error_from_handle(db, code)
}

#[cold]
#[cfg(feature = "modern_sqlite")] // SQLite >= 3.38.0
pub unsafe fn error_with_offset(db: *mut ffi::sqlite3, code: c_int, sql: &str) -> Error {
    if db.is_null() {
        error_from_sqlite_code(code, None)
    } else {
        let error = ffi::Error::new(code);
        let msg = error_msg(db, code);
        if ffi::ErrorCode::Unknown == error.code {
            let offset = ffi::sqlite3_error_offset(db);
            if offset >= 0 {
                return Error::SqlInputError {
                    error,
                    msg: msg.unwrap_or("error".to_owned()),
                    sql: sql.to_owned(),
                    offset,
                };
            }
        }
        Error::SqliteFailure(error, msg)
    }
}

pub fn check(code: c_int) -> Result<()> {
    if code != ffi::SQLITE_OK {
        Err(error_from_sqlite_code(code, None))
    } else {
        Ok(())
    }
}

/// Transform Rust error to SQLite error (message and code).
/// # Safety
/// This function is unsafe because it uses raw pointer
pub unsafe fn to_sqlite_error(e: &Error, err_msg: *mut *mut std::os::raw::c_char) -> c_int {
    use crate::util::alloc;
    match e {
        Error::SqliteFailure(err, s) => {
            if let Some(s) = s {
                *err_msg = alloc(s);
            }
            err.extended_code
        }
        err => {
            *err_msg = alloc(&err.to_string());
            ffi::SQLITE_ERROR
        }
    }
}
