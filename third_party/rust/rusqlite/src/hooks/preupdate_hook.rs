use std::fmt::Debug;
use std::os::raw::{c_char, c_int, c_void};
use std::panic::catch_unwind;
use std::ptr;

use super::expect_utf8;
use super::free_boxed_hook;
use super::Action;
use crate::error::check;
use crate::ffi;
use crate::inner_connection::InnerConnection;
use crate::types::ValueRef;
use crate::Connection;
use crate::Result;

/// The possible cases for when a PreUpdateHook gets triggered. Allows access to the relevant
/// functions for each case through the contained values.
#[derive(Debug)]
pub enum PreUpdateCase {
    /// Pre-update hook was triggered by an insert.
    Insert(PreUpdateNewValueAccessor),
    /// Pre-update hook was triggered by a delete.
    Delete(PreUpdateOldValueAccessor),
    /// Pre-update hook was triggered by an update.
    Update {
        #[allow(missing_docs)]
        old_value_accessor: PreUpdateOldValueAccessor,
        #[allow(missing_docs)]
        new_value_accessor: PreUpdateNewValueAccessor,
    },
    /// This variant is not normally produced by SQLite. You may encounter it
    /// if you're using a different version than what's supported by this library.
    Unknown,
}

impl From<PreUpdateCase> for Action {
    fn from(puc: PreUpdateCase) -> Action {
        match puc {
            PreUpdateCase::Insert(_) => Action::SQLITE_INSERT,
            PreUpdateCase::Delete(_) => Action::SQLITE_DELETE,
            PreUpdateCase::Update { .. } => Action::SQLITE_UPDATE,
            PreUpdateCase::Unknown => Action::UNKNOWN,
        }
    }
}

/// An accessor to access the old values of the row being deleted/updated during the preupdate callback.
#[derive(Debug)]
pub struct PreUpdateOldValueAccessor {
    db: *mut ffi::sqlite3,
    old_row_id: i64,
}

impl PreUpdateOldValueAccessor {
    /// Get the amount of columns in the row being deleted/updated.
    pub fn get_column_count(&self) -> i32 {
        unsafe { ffi::sqlite3_preupdate_count(self.db) }
    }

    /// Get the depth of the query that triggered the preupdate hook.
    /// Returns 0 if the preupdate callback was invoked as a result of
    /// a direct insert, update, or delete operation;
    /// 1 for inserts, updates, or deletes invoked by top-level triggers;
    /// 2 for changes resulting from triggers called by top-level triggers; and so forth.
    pub fn get_query_depth(&self) -> i32 {
        unsafe { ffi::sqlite3_preupdate_depth(self.db) }
    }

    /// Get the row id of the row being updated/deleted.
    pub fn get_old_row_id(&self) -> i64 {
        self.old_row_id
    }

    /// Get the value of the row being updated/deleted at the specified index.
    pub fn get_old_column_value(&self, i: i32) -> Result<ValueRef> {
        let mut p_value: *mut ffi::sqlite3_value = ptr::null_mut();
        unsafe {
            check(ffi::sqlite3_preupdate_old(self.db, i, &mut p_value))?;
            Ok(ValueRef::from_value(p_value))
        }
    }
}

/// An accessor to access the new values of the row being inserted/updated
/// during the preupdate callback.
#[derive(Debug)]
pub struct PreUpdateNewValueAccessor {
    db: *mut ffi::sqlite3,
    new_row_id: i64,
}

impl PreUpdateNewValueAccessor {
    /// Get the amount of columns in the row being inserted/updated.
    pub fn get_column_count(&self) -> i32 {
        unsafe { ffi::sqlite3_preupdate_count(self.db) }
    }

    /// Get the depth of the query that triggered the preupdate hook.
    /// Returns 0 if the preupdate callback was invoked as a result of
    /// a direct insert, update, or delete operation;
    /// 1 for inserts, updates, or deletes invoked by top-level triggers;
    /// 2 for changes resulting from triggers called by top-level triggers; and so forth.
    pub fn get_query_depth(&self) -> i32 {
        unsafe { ffi::sqlite3_preupdate_depth(self.db) }
    }

    /// Get the row id of the row being inserted/updated.
    pub fn get_new_row_id(&self) -> i64 {
        self.new_row_id
    }

    /// Get the value of the row being updated/deleted at the specified index.
    pub fn get_new_column_value(&self, i: i32) -> Result<ValueRef> {
        let mut p_value: *mut ffi::sqlite3_value = ptr::null_mut();
        unsafe {
            check(ffi::sqlite3_preupdate_new(self.db, i, &mut p_value))?;
            Ok(ValueRef::from_value(p_value))
        }
    }
}

impl Connection {
    /// Register a callback function to be invoked before
    /// a row is updated, inserted or deleted.
    ///
    /// The callback parameters are:
    ///
    /// - the name of the database ("main", "temp", ...),
    /// - the name of the table that is updated,
    /// - a variant of the PreUpdateCase enum which allows access to extra functions depending
    ///   on whether it's an update, delete or insert.
    #[inline]
    pub fn preupdate_hook<F>(&self, hook: Option<F>)
    where
        F: FnMut(Action, &str, &str, &PreUpdateCase) + Send + 'static,
    {
        self.db.borrow_mut().preupdate_hook(hook);
    }
}

impl InnerConnection {
    #[inline]
    pub fn remove_preupdate_hook(&mut self) {
        self.preupdate_hook(None::<fn(Action, &str, &str, &PreUpdateCase)>);
    }

    /// ```compile_fail
    /// use rusqlite::{Connection, Result, hooks::PreUpdateCase};
    /// fn main() -> Result<()> {
    ///     let db = Connection::open_in_memory()?;
    ///     {
    ///         let mut called = std::sync::atomic::AtomicBool::new(false);
    ///         db.preupdate_hook(Some(|action, db: &str, tbl: &str, case: &PreUpdateCase| {
    ///             called.store(true, std::sync::atomic::Ordering::Relaxed);
    ///         }));
    ///     }
    ///     db.execute_batch("CREATE TABLE foo AS SELECT 1 AS bar;")
    /// }
    /// ```
    fn preupdate_hook<F>(&mut self, hook: Option<F>)
    where
        F: FnMut(Action, &str, &str, &PreUpdateCase) + Send + 'static,
    {
        unsafe extern "C" fn call_boxed_closure<F>(
            p_arg: *mut c_void,
            sqlite: *mut ffi::sqlite3,
            action_code: c_int,
            db_name: *const c_char,
            tbl_name: *const c_char,
            old_row_id: i64,
            new_row_id: i64,
        ) where
            F: FnMut(Action, &str, &str, &PreUpdateCase),
        {
            let action = Action::from(action_code);

            let preupdate_case = match action {
                Action::SQLITE_INSERT => PreUpdateCase::Insert(PreUpdateNewValueAccessor {
                    db: sqlite,
                    new_row_id,
                }),
                Action::SQLITE_DELETE => PreUpdateCase::Delete(PreUpdateOldValueAccessor {
                    db: sqlite,
                    old_row_id,
                }),
                Action::SQLITE_UPDATE => PreUpdateCase::Update {
                    old_value_accessor: PreUpdateOldValueAccessor {
                        db: sqlite,
                        old_row_id,
                    },
                    new_value_accessor: PreUpdateNewValueAccessor {
                        db: sqlite,
                        new_row_id,
                    },
                },
                Action::UNKNOWN => PreUpdateCase::Unknown,
            };

            drop(catch_unwind(|| {
                let boxed_hook: *mut F = p_arg.cast::<F>();
                (*boxed_hook)(
                    action,
                    expect_utf8(db_name, "database name"),
                    expect_utf8(tbl_name, "table name"),
                    &preupdate_case,
                );
            }));
        }

        let free_preupdate_hook = if hook.is_some() {
            Some(free_boxed_hook::<F> as unsafe fn(*mut c_void))
        } else {
            None
        };

        let previous_hook = match hook {
            Some(hook) => {
                let boxed_hook: *mut F = Box::into_raw(Box::new(hook));
                unsafe {
                    ffi::sqlite3_preupdate_hook(
                        self.db(),
                        Some(call_boxed_closure::<F>),
                        boxed_hook.cast(),
                    )
                }
            }
            _ => unsafe { ffi::sqlite3_preupdate_hook(self.db(), None, ptr::null_mut()) },
        };
        if !previous_hook.is_null() {
            if let Some(free_boxed_hook) = self.free_preupdate_hook {
                unsafe { free_boxed_hook(previous_hook) };
            }
        }
        self.free_preupdate_hook = free_preupdate_hook;
    }
}

#[cfg(test)]
mod test {
    use std::sync::atomic::{AtomicBool, Ordering};

    use super::super::Action;
    use super::PreUpdateCase;
    use crate::{Connection, Result};

    #[test]
    fn test_preupdate_hook_insert() -> Result<()> {
        let db = Connection::open_in_memory()?;

        static CALLED: AtomicBool = AtomicBool::new(false);

        db.preupdate_hook(Some(|action, db: &str, tbl: &str, case: &PreUpdateCase| {
            assert_eq!(Action::SQLITE_INSERT, action);
            assert_eq!("main", db);
            assert_eq!("foo", tbl);
            match case {
                PreUpdateCase::Insert(accessor) => {
                    assert_eq!(1, accessor.get_column_count());
                    assert_eq!(1, accessor.get_new_row_id());
                    assert_eq!(0, accessor.get_query_depth());
                    // out of bounds access should return an error
                    assert!(accessor.get_new_column_value(1).is_err());
                    assert_eq!(
                        "lisa",
                        accessor.get_new_column_value(0).unwrap().as_str().unwrap()
                    );
                    assert_eq!(0, accessor.get_query_depth());
                }
                _ => panic!("wrong preupdate case"),
            }
            CALLED.store(true, Ordering::Relaxed);
        }));
        db.execute_batch("CREATE TABLE foo (t TEXT)")?;
        db.execute_batch("INSERT INTO foo VALUES ('lisa')")?;
        assert!(CALLED.load(Ordering::Relaxed));
        Ok(())
    }

    #[test]
    fn test_preupdate_hook_delete() -> Result<()> {
        let db = Connection::open_in_memory()?;

        static CALLED: AtomicBool = AtomicBool::new(false);

        db.execute_batch("CREATE TABLE foo (t TEXT)")?;
        db.execute_batch("INSERT INTO foo VALUES ('lisa')")?;

        db.preupdate_hook(Some(|action, db: &str, tbl: &str, case: &PreUpdateCase| {
            assert_eq!(Action::SQLITE_DELETE, action);
            assert_eq!("main", db);
            assert_eq!("foo", tbl);
            match case {
                PreUpdateCase::Delete(accessor) => {
                    assert_eq!(1, accessor.get_column_count());
                    assert_eq!(1, accessor.get_old_row_id());
                    assert_eq!(0, accessor.get_query_depth());
                    // out of bounds access should return an error
                    assert!(accessor.get_old_column_value(1).is_err());
                    assert_eq!(
                        "lisa",
                        accessor.get_old_column_value(0).unwrap().as_str().unwrap()
                    );
                    assert_eq!(0, accessor.get_query_depth());
                }
                _ => panic!("wrong preupdate case"),
            }
            CALLED.store(true, Ordering::Relaxed);
        }));

        db.execute_batch("DELETE from foo")?;
        assert!(CALLED.load(Ordering::Relaxed));
        Ok(())
    }

    #[test]
    fn test_preupdate_hook_update() -> Result<()> {
        let db = Connection::open_in_memory()?;

        static CALLED: AtomicBool = AtomicBool::new(false);

        db.execute_batch("CREATE TABLE foo (t TEXT)")?;
        db.execute_batch("INSERT INTO foo VALUES ('lisa')")?;

        db.preupdate_hook(Some(|action, db: &str, tbl: &str, case: &PreUpdateCase| {
            assert_eq!(Action::SQLITE_UPDATE, action);
            assert_eq!("main", db);
            assert_eq!("foo", tbl);
            match case {
                PreUpdateCase::Update {
                    old_value_accessor,
                    new_value_accessor,
                } => {
                    assert_eq!(1, old_value_accessor.get_column_count());
                    assert_eq!(1, old_value_accessor.get_old_row_id());
                    assert_eq!(0, old_value_accessor.get_query_depth());
                    // out of bounds access should return an error
                    assert!(old_value_accessor.get_old_column_value(1).is_err());
                    assert_eq!(
                        "lisa",
                        old_value_accessor
                            .get_old_column_value(0)
                            .unwrap()
                            .as_str()
                            .unwrap()
                    );
                    assert_eq!(0, old_value_accessor.get_query_depth());

                    assert_eq!(1, new_value_accessor.get_column_count());
                    assert_eq!(1, new_value_accessor.get_new_row_id());
                    assert_eq!(0, new_value_accessor.get_query_depth());
                    // out of bounds access should return an error
                    assert!(new_value_accessor.get_new_column_value(1).is_err());
                    assert_eq!(
                        "janice",
                        new_value_accessor
                            .get_new_column_value(0)
                            .unwrap()
                            .as_str()
                            .unwrap()
                    );
                    assert_eq!(0, new_value_accessor.get_query_depth());
                }
                _ => panic!("wrong preupdate case"),
            }
            CALLED.store(true, Ordering::Relaxed);
        }));

        db.execute_batch("UPDATE foo SET t = 'janice'")?;
        assert!(CALLED.load(Ordering::Relaxed));
        Ok(())
    }
}
