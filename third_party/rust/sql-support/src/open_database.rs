/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/// Use this module to open a new SQLite database connection.
///
/// Usage:
///    - Define a struct that implements ConnectionInitializer.  This handles:
///      - Initializing the schema for a new database
///      - Upgrading the schema for an existing database
///      - Extra preparation/finishing steps, for example setting up SQLite functions
///
///    - Call open_database() in your database constructor:
///      - The first method called is `prepare()`.  This is executed outside of a transaction
///        and is suitable for executing pragmas (eg, `PRAGMA journal_mode=wal`), defining
///        functions, etc.
///      - If the database file is not present and the connection is writable, open_database()
///        will create a new DB and call init(), then finish(). If the connection is not
///        writable it will panic, meaning that if you support ReadOnly connections, they must
///        be created after a writable connection is open.
///      - If the database file exists and the connection is writable, open_database() will open
///        it and call prepare(), upgrade_from() for each upgrade that needs to be applied, then
///        finish(). As above, a read-only connection will panic if upgrades are necessary, so
///        you should ensure the first connection opened is writable.
///      - If the database file is corrupt, or upgrade_from() returns [`Error::Corrupt`], the
///        database file will be removed and replaced with a new DB.
///      - If the connection is not writable, `finish()` will be called (ie, `finish()`, like
///        `prepare()`, is called for all connections)
///
///  See the autofill DB code for an example.
///
use std::{
    borrow::Cow,
    path::Path,
    sync::atomic::{AtomicUsize, Ordering},
};

use rusqlite::{
    Connection, Error as RusqliteError, ErrorCode, OpenFlags, Transaction, TransactionBehavior,
};
use thiserror::Error;

use crate::ConnExt;
use crate::{debug, info, warn};

#[derive(Error, Debug)]
pub enum Error {
    #[error("Incompatible database version: {0}")]
    IncompatibleVersion(u32),
    #[error("Database is corrupt")]
    Corrupt,
    #[error("Error executing SQL: {0}")]
    SqlError(rusqlite::Error),
    #[error("Failed to recover a corrupt database due to an error deleting the file: {0}")]
    RecoveryError(std::io::Error),
    #[error("In shutdown mode")]
    Shutdown,
}

impl From<rusqlite::Error> for Error {
    fn from(value: rusqlite::Error) -> Self {
        match value {
            RusqliteError::SqliteFailure(e, _)
                if matches!(e.code, ErrorCode::DatabaseCorrupt | ErrorCode::NotADatabase) =>
            {
                Self::Corrupt
            }
            _ => Self::SqlError(value),
        }
    }
}

pub type Result<T> = std::result::Result<T, Error>;

pub trait ConnectionInitializer {
    // Name to display in the logs
    const NAME: &'static str;

    // The version that the last upgrade function upgrades to.
    const END_VERSION: u32;

    // Functions called only for writable connections all take a Transaction
    // Initialize a newly created database to END_VERSION
    fn init(&self, tx: &Transaction<'_>) -> Result<()>;

    // Upgrade schema from version -> version + 1
    fn upgrade_from(&self, conn: &Transaction<'_>, version: u32) -> Result<()>;

    // Runs immediately after creation for all types of connections. If writable,
    // will *not* be in the transaction created for the "only writable" functions above.
    fn prepare(&self, _conn: &Connection, _db_empty: bool) -> Result<()> {
        Ok(())
    }

    // Runs for all types of connections. If a writable connection is being
    // initialized, this will be called after all initialization functions,
    // but inside their transaction.
    fn finish(&self, _conn: &Connection) -> Result<()> {
        Ok(())
    }
}

pub fn open_database<CI: ConnectionInitializer, P: AsRef<Path>>(
    path: P,
    connection_initializer: &CI,
) -> Result<Connection> {
    open_database_with_flags(path, OpenFlags::default(), connection_initializer)
}

pub fn open_memory_database<CI: ConnectionInitializer>(
    conn_initializer: &CI,
) -> Result<Connection> {
    open_memory_database_with_flags(OpenFlags::default(), conn_initializer)
}

pub fn open_database_with_flags<CI: ConnectionInitializer, P: AsRef<Path>>(
    path: P,
    open_flags: OpenFlags,
    connection_initializer: &CI,
) -> Result<Connection> {
    do_open_database_with_flags(&path, open_flags, connection_initializer).or_else(|e| {
        // See if we can recover from the error and try a second time
        try_handle_db_failure(&path, open_flags, connection_initializer, e)?;
        do_open_database_with_flags(&path, open_flags, connection_initializer)
    })
}

/// OpenFlags for a read-write database
pub fn read_write_flags() -> OpenFlags {
    OpenFlags::SQLITE_OPEN_URI
        | OpenFlags::SQLITE_OPEN_NO_MUTEX
        | OpenFlags::SQLITE_OPEN_CREATE
        | OpenFlags::SQLITE_OPEN_READ_WRITE
}

/// OpenFlags for a read-only database
pub fn read_only_flags() -> OpenFlags {
    OpenFlags::SQLITE_OPEN_URI | OpenFlags::SQLITE_OPEN_NO_MUTEX | OpenFlags::SQLITE_OPEN_READ_ONLY
}

fn do_open_database_with_flags<CI: ConnectionInitializer, P: AsRef<Path>>(
    path: P,
    open_flags: OpenFlags,
    connection_initializer: &CI,
) -> Result<Connection> {
    // Try running the migration logic with an existing file
    debug!("{}: opening database", CI::NAME);
    let mut conn = Connection::open_with_flags(path, open_flags)?;
    debug!("{}: checking if initialization is necessary", CI::NAME);
    let db_empty = is_db_empty(&conn)?;

    debug!("{}: preparing", CI::NAME);
    connection_initializer.prepare(&conn, db_empty)?;

    if open_flags.contains(OpenFlags::SQLITE_OPEN_READ_WRITE) {
        let mut write_schema_version = true;
        let tx = conn.transaction_with_behavior(TransactionBehavior::Immediate)?;
        if db_empty {
            debug!("{}: initializing new database", CI::NAME);
            connection_initializer.init(&tx)?;
        } else {
            let mut current_version = get_schema_version(&tx)?;
            if current_version > CI::END_VERSION {
                return Err(Error::IncompatibleVersion(current_version));
            } else if current_version == CI::END_VERSION {
                write_schema_version = false;
            } else {
                while current_version < CI::END_VERSION {
                    debug!(
                        "{}: upgrading database to {}",
                        CI::NAME,
                        current_version + 1
                    );
                    connection_initializer.upgrade_from(&tx, current_version)?;
                    current_version += 1;
                }
            }
        }
        debug!("{}: finishing writable database open", CI::NAME);
        connection_initializer.finish(&tx)?;
        if write_schema_version {
            set_schema_version(&tx, CI::END_VERSION)?;
        }
        tx.commit()?;
    } else {
        // There's an implied requirement that the first connection to a DB is
        // writable, so read-only connections do much less, but panic if stuff is wrong
        assert!(!db_empty, "existing writer must have initialized");
        assert!(
            get_schema_version(&conn)? == CI::END_VERSION,
            "existing writer must have migrated"
        );
        debug!("{}: finishing readonly database open", CI::NAME);
        connection_initializer.finish(&conn)?;
    }
    debug!("{}: database open successful", CI::NAME);
    Ok(conn)
}

pub fn open_memory_database_with_flags<CI: ConnectionInitializer>(
    flags: OpenFlags,
    conn_initializer: &CI,
) -> Result<Connection> {
    open_database_with_flags(":memory:", flags, conn_initializer)
}

// Attempt to handle failure when opening the database.
//
// Returns:
//   - Ok(()) the failure is potentially handled and we should make a second open attempt
//   - Err(e) the failure couldn't be handled and we should return this error
fn try_handle_db_failure<CI: ConnectionInitializer, P: AsRef<Path>>(
    path: P,
    open_flags: OpenFlags,
    _connection_initializer: &CI,
    err: Error,
) -> Result<()> {
    if !open_flags.contains(OpenFlags::SQLITE_OPEN_CREATE)
        && matches!(err, Error::SqlError(rusqlite::Error::SqliteFailure(code, _)) if code.code == rusqlite::ErrorCode::CannotOpen)
    {
        info!(
            "{}: database doesn't exist, but we weren't requested to create it",
            CI::NAME
        );
        return Err(err);
    }
    warn!("{}: database operation failed: {}", CI::NAME, err);
    if !open_flags.contains(OpenFlags::SQLITE_OPEN_READ_WRITE) {
        warn!(
            "{}: not attempting recovery as this is a read-only connection request",
            CI::NAME
        );
        return Err(err);
    }

    let delete = matches!(err, Error::Corrupt);
    if delete {
        info!(
            "{}: the database is fatally damaged; deleting and starting fresh",
            CI::NAME
        );
        // Note we explicitly decline to move the path to, say ".corrupt", as it's difficult to
        // identify any value there - actually getting our hands on the file from a mobile device
        // is tricky and it would just take up disk space forever.
        if let Err(io_err) = std::fs::remove_file(path) {
            return Err(Error::RecoveryError(io_err));
        }
        Ok(())
    } else {
        Err(err)
    }
}

fn is_db_empty(conn: &Connection) -> Result<bool> {
    Ok(conn.conn_ext_query_one::<u32>("SELECT COUNT(*) FROM sqlite_master")? == 0)
}

fn get_schema_version(conn: &Connection) -> Result<u32> {
    let version = conn.query_row_and_then("PRAGMA user_version", [], |row| row.get(0))?;
    Ok(version)
}

fn set_schema_version(conn: &Connection, version: u32) -> Result<()> {
    conn.set_pragma("user_version", version)?;
    Ok(())
}

// Get a unique in-memory database path
//
// This can be very useful for testing.
pub fn unique_in_memory_db_path() -> String {
    static COUNTER: AtomicUsize = AtomicUsize::new(0);
    format!(
        "file:in-memory-db-{}?mode=memory&cache=shared",
        COUNTER.fetch_add(1, Ordering::Relaxed)
    )
}

// It would be nice for this to be #[cfg(test)], but that doesn't allow it to be used in tests for
// our other crates.
pub mod test_utils {
    use super::*;
    use std::{
        cell::RefCell,
        collections::{HashMap, HashSet},
        path::PathBuf,
    };
    use tempfile::TempDir;

    pub struct TestConnectionInitializer {
        pub calls: RefCell<Vec<&'static str>>,
        pub buggy_v3_upgrade: bool,
    }

    impl Default for TestConnectionInitializer {
        fn default() -> Self {
            Self::new()
        }
    }

    impl TestConnectionInitializer {
        pub fn new() -> Self {
            Self {
                calls: RefCell::new(Vec::new()),
                buggy_v3_upgrade: false,
            }
        }
        pub fn new_with_buggy_logic() -> Self {
            Self {
                calls: RefCell::new(Vec::new()),
                buggy_v3_upgrade: true,
            }
        }

        pub fn clear_calls(&self) {
            self.calls.borrow_mut().clear();
        }

        pub fn push_call(&self, call: &'static str) {
            self.calls.borrow_mut().push(call);
        }

        pub fn check_calls(&self, expected: Vec<&'static str>) {
            assert_eq!(*self.calls.borrow(), expected);
        }
    }

    impl ConnectionInitializer for TestConnectionInitializer {
        const NAME: &'static str = "test db";
        const END_VERSION: u32 = 4;

        fn prepare(&self, conn: &Connection, _: bool) -> Result<()> {
            self.push_call("prep");
            conn.execute_batch(
                "
                PRAGMA journal_mode = wal;
                ",
            )?;
            Ok(())
        }

        fn init(&self, conn: &Transaction<'_>) -> Result<()> {
            self.push_call("init");
            conn.execute_batch(
                "
                CREATE TABLE prep_table(col);
                INSERT INTO prep_table(col) VALUES ('correct-value');
                CREATE TABLE my_table(col);
                ",
            )
            .map_err(|e| e.into())
        }

        fn upgrade_from(&self, conn: &Transaction<'_>, version: u32) -> Result<()> {
            match version {
                // This upgrade forces the database to be replaced by returning
                // `Error::Corrupt`.
                1 => {
                    self.push_call("upgrade_from_v1");
                    Err(Error::Corrupt)
                }
                2 => {
                    self.push_call("upgrade_from_v2");
                    conn.execute_batch(
                        "
                        ALTER TABLE my_old_table_name RENAME TO my_table;
                        ",
                    )?;
                    Ok(())
                }
                3 => {
                    self.push_call("upgrade_from_v3");

                    if self.buggy_v3_upgrade {
                        conn.execute_batch("ILLEGAL_SQL_CODE")?;
                    }

                    conn.execute_batch(
                        "
                        ALTER TABLE my_table RENAME COLUMN old_col to col;
                        ",
                    )?;
                    Ok(())
                }
                _ => {
                    panic!("Unexpected version: {}", version);
                }
            }
        }

        fn finish(&self, conn: &Connection) -> Result<()> {
            self.push_call("finish");
            conn.execute_batch(
                "
                INSERT INTO my_table(col) SELECT col FROM prep_table;
                ",
            )?;
            Ok(())
        }
    }

    // Database file that we can programmatically run upgrades on
    //
    // We purposefully don't keep a connection to the database around to force upgrades to always
    // run against a newly opened DB, like they would in the real world.  See #4106 for
    // details.
    pub struct MigratedDatabaseFile<CI: ConnectionInitializer> {
        // Keep around a TempDir to ensure the database file stays around until this struct is
        // dropped
        _tempdir: TempDir,
        pub connection_initializer: CI,
        pub path: PathBuf,
    }

    impl<CI: ConnectionInitializer> MigratedDatabaseFile<CI> {
        pub fn new(connection_initializer: CI, init_sql: &str) -> Self {
            Self::new_with_flags(connection_initializer, init_sql, OpenFlags::default())
        }

        pub fn new_with_flags(
            connection_initializer: CI,
            init_sql: &str,
            open_flags: OpenFlags,
        ) -> Self {
            let tempdir = tempfile::tempdir().unwrap();
            let path = tempdir.path().join(Path::new("db.sql"));
            let conn = Connection::open_with_flags(&path, open_flags).unwrap();
            conn.execute_batch(init_sql).unwrap();
            Self {
                _tempdir: tempdir,
                connection_initializer,
                path,
            }
        }

        /// Attempt to run all upgrades up to a specific version.
        ///
        /// This will result in a panic if an upgrade fails to run.
        pub fn upgrade_to(&self, version: u32) {
            let mut conn = self.open();
            let tx = conn.transaction().unwrap();
            let mut current_version = get_schema_version(&tx).unwrap();
            while current_version < version {
                self.connection_initializer
                    .upgrade_from(&tx, current_version)
                    .unwrap();
                current_version += 1;
            }
            set_schema_version(&tx, current_version).unwrap();
            self.connection_initializer.finish(&tx).unwrap();
            tx.commit().unwrap();
        }

        /// Attempt to run all upgrades
        ///
        /// This will result in a panic if an upgrade fails to run.
        pub fn run_all_upgrades(&self) {
            let current_version = get_schema_version(&self.open()).unwrap();
            for version in current_version..CI::END_VERSION {
                self.upgrade_to(version + 1);
            }
        }

        pub fn assert_schema_matches_new_database(&self) {
            let db = self.open();
            let new_db = match open_memory_database(&self.connection_initializer) {
                Ok(db) => db,
                Err(e) => panic!("Creating new database failed:\n{e}"),
            };

            compare_sql_maps("table", get_sql(&db, "table"), get_sql(&new_db, "table"));
            compare_sql_maps("index", get_sql(&db, "index"), get_sql(&new_db, "index"));
            compare_sql_maps(
                "trigger",
                get_sql(&db, "trigger"),
                get_sql(&new_db, "trigger"),
            );
        }

        pub fn open(&self) -> Connection {
            Connection::open(&self.path).unwrap()
        }
    }

    fn get_sql(conn: &Connection, type_: &str) -> HashMap<String, Option<String>> {
        conn.query_rows_and_then(
            "SELECT name, sql FROM sqlite_master WHERE type=?",
            (type_,),
            |row| -> rusqlite::Result<(String, Option<String>)> { Ok((row.get(0)?, row.get(1)?)) },
        )
        .unwrap()
        .into_iter()
        .collect()
    }

    fn compare_sql_maps(
        type_: &str,
        old_items: HashMap<String, Option<String>>,
        new_items: HashMap<String, Option<String>>,
    ) {
        let old_db_keys: HashSet<&String> = old_items.keys().collect();
        let new_db_keys: HashSet<&String> = new_items.keys().collect();

        let old_db_extra_keys = Vec::from_iter(old_db_keys.difference(&new_db_keys));
        if !old_db_extra_keys.is_empty() {
            panic!("Extra keys not present in new database for {type_}: {old_db_extra_keys:?}");
        }
        let new_db_extra_keys = Vec::from_iter(new_db_keys.difference(&old_db_keys));
        if !new_db_extra_keys.is_empty() {
            panic!("Extra keys only present in new database for {type_}: {new_db_extra_keys:?}");
        }
        for key in old_db_keys {
            assert_eq!(
                old_items.get(key).unwrap().as_deref().map(normalize),
                new_items.get(key).unwrap().as_deref().map(normalize),
                "sql differs for {type_} {key}"
            );
        }
    }

    /// Normalize SQL code by changing all whitespace to a single space.
    fn normalize(sql: &str) -> String {
        sql.split('\'')
            .enumerate()
            .map(|(i, part)| {
                // Only normalize the even parts.  Odd parts are either inside a string literal.
                // Note: SQLite uses a double quote (`''`) as the escape, which works with this
                // system.  We'll just end up normalizing the empty string, which doesn't hurt
                // anything.
                if (i % 2) == 0 {
                    Cow::Owned(part.split_whitespace().collect::<Vec<_>>().join(" "))
                } else {
                    Cow::Borrowed(part)
                }
            })
            .collect()
    }
}

#[cfg(test)]
mod test {
    use super::test_utils::{MigratedDatabaseFile, TestConnectionInitializer};
    use super::*;
    use std::io::Write;

    // A special schema used to test the upgrade that forces the database to be
    // replaced.
    static INIT_V1: &str = "
        CREATE TABLE prep_table(col);
        PRAGMA user_version=1;
    ";

    // Initialize the database to v2 to test upgrading from there
    static INIT_V2: &str = "
        CREATE TABLE prep_table(col);
        INSERT INTO prep_table(col) VALUES ('correct-value');
        CREATE TABLE my_old_table_name(old_col);
        PRAGMA user_version=2;
    ";

    fn check_final_data(conn: &Connection) {
        let value: String = conn
            .query_row("SELECT col FROM my_table", [], |r| r.get(0))
            .unwrap();
        assert_eq!(value, "correct-value");
        assert_eq!(get_schema_version(conn).unwrap(), 4);
    }

    #[test]
    fn test_init() {
        let connection_initializer = TestConnectionInitializer::new();
        let conn = open_memory_database(&connection_initializer).unwrap();
        check_final_data(&conn);
        connection_initializer.check_calls(vec!["prep", "init", "finish"]);
    }

    #[test]
    fn test_upgrades() {
        let db_file = MigratedDatabaseFile::new(TestConnectionInitializer::new(), INIT_V2);
        let conn = open_database(db_file.path.clone(), &db_file.connection_initializer).unwrap();
        check_final_data(&conn);
        db_file.connection_initializer.check_calls(vec![
            "prep",
            "upgrade_from_v2",
            "upgrade_from_v3",
            "finish",
        ]);
    }

    #[test]
    fn test_open_current_version() {
        let db_file = MigratedDatabaseFile::new(TestConnectionInitializer::new(), INIT_V2);
        db_file.upgrade_to(4);
        db_file.connection_initializer.clear_calls();
        let conn = open_database(db_file.path.clone(), &db_file.connection_initializer).unwrap();
        check_final_data(&conn);
        db_file
            .connection_initializer
            .check_calls(vec!["prep", "finish"]);
    }

    #[test]
    fn test_pragmas() {
        let db_file = MigratedDatabaseFile::new(TestConnectionInitializer::new(), INIT_V2);
        let conn = open_database(db_file.path.clone(), &db_file.connection_initializer).unwrap();
        assert_eq!(
            conn.conn_ext_query_one::<String>("PRAGMA journal_mode")
                .unwrap(),
            "wal"
        );
    }

    #[test]
    fn test_migration_error() {
        let db_file =
            MigratedDatabaseFile::new(TestConnectionInitializer::new_with_buggy_logic(), INIT_V2);
        db_file
            .open()
            .execute(
                "INSERT INTO my_old_table_name(old_col) VALUES ('I should not be deleted')",
                [],
            )
            .unwrap();

        open_database(db_file.path.clone(), &db_file.connection_initializer).unwrap_err();
        // Even though the upgrades failed, the data should still be there.  The changes that
        // upgrade_to_v3 made should have been rolled back.
        assert_eq!(
            db_file
                .open()
                .conn_ext_query_one::<i32>("SELECT COUNT(*) FROM my_old_table_name")
                .unwrap(),
            1
        );
    }

    #[test]
    fn test_version_too_new() {
        let db_file = MigratedDatabaseFile::new(TestConnectionInitializer::new(), INIT_V2);
        set_schema_version(&db_file.open(), 5).unwrap();

        db_file
            .open()
            .execute(
                "INSERT INTO my_old_table_name(old_col) VALUES ('I should not be deleted')",
                [],
            )
            .unwrap();

        assert!(matches!(
            open_database(db_file.path.clone(), &db_file.connection_initializer,),
            Err(Error::IncompatibleVersion(5))
        ));
        // Make sure that even when DeleteAndRecreate is specified, we don't delete the database
        // file when the schema is newer
        assert_eq!(
            db_file
                .open()
                .conn_ext_query_one::<i32>("SELECT COUNT(*) FROM my_old_table_name")
                .unwrap(),
            1
        );
    }

    #[test]
    fn test_corrupt_db() {
        let tempdir = tempfile::tempdir().unwrap();
        let path = tempdir.path().join(Path::new("corrupt-db.sql"));
        let mut file = std::fs::File::create(path.clone()).unwrap();
        // interestingly, sqlite seems to treat a 0-byte file as a missing one.
        // Note that this will exercise the `ErrorCode::NotADatabase` error code. It's not clear
        // how we could hit `ErrorCode::DatabaseCorrupt`, but even if we could, there's not much
        // value as this test can't really observe which one it was.
        file.write_all(b"not sql").unwrap();
        let metadata = std::fs::metadata(path.clone()).unwrap();
        assert_eq!(metadata.len(), 7);
        drop(file);
        open_database(path.clone(), &TestConnectionInitializer::new()).unwrap();
        let metadata = std::fs::metadata(path).unwrap();
        // just check the file is no longer what it was before.
        assert_ne!(metadata.len(), 7);
    }

    #[test]
    fn test_force_replace() {
        let db_file = MigratedDatabaseFile::new(TestConnectionInitializer::new(), INIT_V1);
        let conn = open_database(db_file.path.clone(), &db_file.connection_initializer).unwrap();
        check_final_data(&conn);
        db_file.connection_initializer.check_calls(vec![
            "prep",
            "upgrade_from_v1",
            "prep",
            "init",
            "finish",
        ]);
    }
}
