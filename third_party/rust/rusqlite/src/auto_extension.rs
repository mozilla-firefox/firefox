//! Automatic extension loading
use super::ffi;
use crate::error::{check, to_sqlite_error};
use crate::{Connection, Error, Result};
use std::os::raw::{c_char, c_int};
use std::panic::catch_unwind;

/// Automatic extension initialization routine
pub type AutoExtension = fn(Connection) -> Result<()>;

/// Raw automatic extension initialization routine
pub type RawAutoExtension = unsafe extern "C" fn(
    db: *mut ffi::sqlite3,
    pz_err_msg: *mut *mut c_char,
    _: *const ffi::sqlite3_api_routines,
) -> c_int;

/// Bridge between `RawAutoExtension` and `AutoExtension`
///
/// # Safety
/// * Opening a database from an auto-extension handler will lead to
///   an endless recursion of the auto-handler triggering itself
///   indirectly for each newly-opened database.
/// * Results are undefined if the given db is closed by an auto-extension.
/// * The list of auto-extensions should not be manipulated from an auto-extension.
pub unsafe fn init_auto_extension(
    db: *mut ffi::sqlite3,
    pz_err_msg: *mut *mut c_char,
    ax: AutoExtension,
) -> c_int {
    let r = catch_unwind(|| {
        let c = Connection::from_handle(db);
        c.and_then(ax)
    })
    .unwrap_or_else(|_| Err(Error::UnwindingPanic));
    match r {
        Err(e) => to_sqlite_error(&e, pz_err_msg),
        _ => ffi::SQLITE_OK,
    }
}

/// Register au auto-extension
///
/// # Safety
/// * Opening a database from an auto-extension handler will lead to
///   an endless recursion of the auto-handler triggering itself
///   indirectly for each newly-opened database.
/// * Results are undefined if the given db is closed by an auto-extension.
/// * The list of auto-extensions should not be manipulated from an auto-extension.
pub unsafe fn register_auto_extension(ax: RawAutoExtension) -> Result<()> {
    check(ffi::sqlite3_auto_extension(Some(ax)))
}

/// Unregister the initialization routine
pub fn cancel_auto_extension(ax: RawAutoExtension) -> bool {
    unsafe { ffi::sqlite3_cancel_auto_extension(Some(ax)) == 1 }
}

/// Disable all automatic extensions previously registered
pub fn reset_auto_extension() {
    unsafe { ffi::sqlite3_reset_auto_extension() }
}
