//! Busy handler (when the database is locked)
use std::mem;
use std::os::raw::{c_int, c_void};
use std::panic::catch_unwind;
use std::ptr;
use std::time::Duration;

use crate::ffi;
use crate::{Connection, InnerConnection, Result};

impl Connection {
    /// Set a busy handler that sleeps for a specified amount of time when a
    /// table is locked. The handler will sleep multiple times until at
    /// least "ms" milliseconds of sleeping have accumulated.
    ///
    /// Calling this routine with an argument equal to zero turns off all busy
    /// handlers.
    ///
    /// There can only be a single busy handler for a particular database
    /// connection at any given moment. If another busy handler was defined
    /// (using [`busy_handler`](Connection::busy_handler)) prior to calling this
    /// routine, that other busy handler is cleared.
    ///
    /// Newly created connections currently have a default busy timeout of
    /// 5000ms, but this may be subject to change.
    pub fn busy_timeout(&self, timeout: Duration) -> Result<()> {
        let ms: i32 = timeout
            .as_secs()
            .checked_mul(1000)
            .and_then(|t| t.checked_add(timeout.subsec_millis().into()))
            .and_then(|t| t.try_into().ok())
            .expect("too big");
        self.db.borrow_mut().busy_timeout(ms)
    }

    /// Register a callback to handle `SQLITE_BUSY` errors.
    ///
    /// If the busy callback is `None`, then `SQLITE_BUSY` is returned
    /// immediately upon encountering the lock. The argument to the busy
    /// handler callback is the number of times that the
    /// busy handler has been invoked previously for the
    /// same locking event. If the busy callback returns `false`, then no
    /// additional attempts are made to access the
    /// database and `SQLITE_BUSY` is returned to the
    /// application. If the callback returns `true`, then another attempt
    /// is made to access the database and the cycle repeats.
    ///
    /// There can only be a single busy handler defined for each database
    /// connection. Setting a new busy handler clears any previously set
    /// handler. Note that calling [`busy_timeout()`](Connection::busy_timeout)
    /// or evaluating `PRAGMA busy_timeout=N` will change the busy handler
    /// and thus clear any previously set busy handler.
    ///
    /// Newly created connections default to a
    /// [`busy_timeout()`](Connection::busy_timeout) handler with a timeout
    /// of 5000ms, although this is subject to change.
    pub fn busy_handler(&self, callback: Option<fn(i32) -> bool>) -> Result<()> {
        unsafe extern "C" fn busy_handler_callback(p_arg: *mut c_void, count: c_int) -> c_int {
            let handler_fn: fn(i32) -> bool = mem::transmute(p_arg);
            c_int::from(catch_unwind(|| handler_fn(count)).unwrap_or_default())
        }
        let c = self.db.borrow_mut();
        let r = match callback {
            Some(f) => unsafe {
                ffi::sqlite3_busy_handler(c.db(), Some(busy_handler_callback), f as *mut c_void)
            },
            None => unsafe { ffi::sqlite3_busy_handler(c.db(), None, ptr::null_mut()) },
        };
        c.decode_result(r)
    }
}

impl InnerConnection {
    #[inline]
    fn busy_timeout(&mut self, timeout: c_int) -> Result<()> {
        let r = unsafe { ffi::sqlite3_busy_timeout(self.db, timeout) };
        self.decode_result(r)
    }
}

#[cfg(test)]
mod test {
    use crate::{Connection, ErrorCode, Result, TransactionBehavior};
    use std::sync::atomic::{AtomicBool, Ordering};

    #[test]
    fn test_default_busy() -> Result<()> {
        let temp_dir = tempfile::tempdir().unwrap();
        let path = temp_dir.path().join("test.db3");

        let mut db1 = Connection::open(&path)?;
        let tx1 = db1.transaction_with_behavior(TransactionBehavior::Exclusive)?;
        let db2 = Connection::open(&path)?;
        let r: Result<()> = db2.query_row("PRAGMA schema_version", [], |_| unreachable!());
        assert_eq!(
            r.unwrap_err().sqlite_error_code(),
            Some(ErrorCode::DatabaseBusy)
        );
        tx1.rollback()
    }

    #[test]
    fn test_busy_handler() -> Result<()> {
        static CALLED: AtomicBool = AtomicBool::new(false);
        fn busy_handler(n: i32) -> bool {
            if n > 2 {
                false
            } else {
                CALLED.swap(true, Ordering::Relaxed)
            }
        }

        let temp_dir = tempfile::tempdir().unwrap();
        let path = temp_dir.path().join("busy-handler.db3");

        let db1 = Connection::open(&path)?;
        db1.execute_batch("CREATE TABLE IF NOT EXISTS t(a)")?;
        let db2 = Connection::open(&path)?;
        db2.busy_handler(Some(busy_handler))?;
        db1.execute_batch("BEGIN EXCLUSIVE")?;
        let err = db2.prepare("SELECT * FROM t").unwrap_err();
        assert_eq!(err.sqlite_error_code(), Some(ErrorCode::DatabaseBusy));
        assert!(CALLED.load(Ordering::Relaxed));
        db1.busy_handler(None)?;
        Ok(())
    }
}
