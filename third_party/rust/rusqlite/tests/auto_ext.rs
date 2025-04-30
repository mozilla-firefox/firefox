#[cfg(all(feature = "bundled", not(feature = "loadable_extension")))]
#[test]
fn auto_ext() -> rusqlite::Result<()> {
    use rusqlite::auto_extension::*;
    use rusqlite::{ffi, Connection, Error, Result};
    use std::os::raw::{c_char, c_int};

    fn test_ok(_: Connection) -> Result<()> {
        Ok(())
    }
    unsafe extern "C" fn sqlite_test_ok(
        db: *mut ffi::sqlite3,
        pz_err_msg: *mut *mut c_char,
        _: *const ffi::sqlite3_api_routines,
    ) -> c_int {
        init_auto_extension(db, pz_err_msg, test_ok)
    }
    fn test_err(_: Connection) -> Result<()> {
        Err(Error::SqliteFailure(
            ffi::Error::new(ffi::SQLITE_CORRUPT),
            Some("AutoExtErr".to_owned()),
        ))
    }
    unsafe extern "C" fn sqlite_test_err(
        db: *mut ffi::sqlite3,
        pz_err_msg: *mut *mut c_char,
        _: *const ffi::sqlite3_api_routines,
    ) -> c_int {
        init_auto_extension(db, pz_err_msg, test_err)
    }

    //assert!(!cancel_auto_extension(sqlite_test_ok));
    unsafe { register_auto_extension(sqlite_test_ok)? };
    Connection::open_in_memory()?;
    assert!(cancel_auto_extension(sqlite_test_ok));
    assert!(!cancel_auto_extension(sqlite_test_ok));
    unsafe { register_auto_extension(sqlite_test_err)? };
    Connection::open_in_memory().unwrap_err();
    reset_auto_extension();
    Ok(())
}
