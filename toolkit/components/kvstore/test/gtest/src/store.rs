/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

use std::{convert::Infallible, fmt, sync::Arc, time::Duration};

use kvstore::skv::{
    checker::{CheckerAction, IntoChecker},
    connection::{ConnectionIncidents, ConnectionMaintenanceTask},
    store::{Store, StoreError, StorePath},
};

use crate::latch::CountDownLatch;

pub fn under_maintenance() {
    static READY: CountDownLatch = CountDownLatch::new(1);
    static DONE: CountDownLatch = CountDownLatch::new(1);

    struct Checker;

    impl IntoChecker<Checker> for ConnectionIncidents<'_> {
        fn into_checker(self) -> CheckerAction<Checker> {
            CheckerAction::Check(Checker)
        }
    }

    impl ConnectionMaintenanceTask for Checker {
        type Error = Infallible;

        fn run(self, _conn: &mut rusqlite::Connection) -> Result<(), Self::Error> {
            // Tell the main thread that we've started the checker.
            READY.count_down();
            // Wait to return until the main thread tells us to wrap up.
            assert!(
                DONE.wait_timeout(Duration::from_secs(5)),
                "timed out waiting for checker to finish"
            );
            Ok(())
        }
    }

    let store = Arc::new(Store::new(StorePath::for_in_memory()));
    let thread = {
        let store = store.clone();
        std::thread::spawn(move || {
            // This should start the checker, wait, and return once the
            // main thread advances the phase to `Finished`.
            assert!(store.check::<Checker>().is_ok(), "check should succeed");
        })
    };
    // Wait for the spawned thread to start the checker, so that
    // its `check()` call doesn't race with ours.
    assert!(
        READY.wait_timeout(Duration::from_secs(5)),
        "timed out waiting for checker to start"
    );
    assert!(
        matches!(store.check::<Checker>(), Err(StoreError::Busy)),
        "check during maintenance should fail"
    );
    // Advance the phase to unblock the spawned thread, then
    // wait for its `check()` call to return.
    DONE.count_down();
    thread.join().unwrap();
    store.close();
}

pub fn close_during_maintenance() {
    static READY: CountDownLatch = CountDownLatch::new(1);

    struct Checker;

    impl IntoChecker<Checker> for ConnectionIncidents<'_> {
        fn into_checker(self) -> CheckerAction<Checker> {
            CheckerAction::Check(Checker)
        }
    }

    impl ConnectionMaintenanceTask for Checker {
        type Error = rusqlite::Error;

        fn run(self, conn: &mut rusqlite::Connection) -> Result<(), Self::Error> {
            // Tell the main thread that we've started the checker.
            READY.count_down();
            // Start a query that will run forever, until
            // interrupted by `close()`.
            conn.execute(
                "WITH RECURSIVE x(i) AS (
                   SELECT 1
                   UNION ALL
                   SELECT i + 1 FROM x
                 )
                 SELECT i FROM x",
                [],
            )?;
            unreachable!("query should never return")
        }
    }

    let store = Arc::new(Store::new(StorePath::for_in_memory()));
    let thread = {
        let store = store.clone();
        std::thread::spawn(move || {
            assert!(
                matches!(store.check::<Checker>(), Err(StoreError::Maintenance(_))),
                "interrupted check should fail"
            );
        })
    };
    assert!(
        READY.wait_timeout(Duration::from_secs(5)),
        "timed out waiting for checker to start"
    );
    store.close();
    assert!(
        matches!(store.check::<Checker>(), Err(StoreError::Closed)),
        "check on closed store should fail"
    );
    thread.join().unwrap();
}

pub fn maintenance_succeeds() {
    struct Checker;

    impl IntoChecker<Checker> for ConnectionIncidents<'_> {
        fn into_checker(self) -> CheckerAction<Checker> {
            CheckerAction::Check(Checker)
        }
    }

    impl ConnectionMaintenanceTask for Checker {
        type Error = Infallible;

        fn run(self, _conn: &mut rusqlite::Connection) -> Result<(), Self::Error> {
            Ok(())
        }
    }

    let store = Store::new(StorePath::for_in_memory());
    assert!(store.check::<Checker>().is_ok(), "check should succeed");
    store.close();
}

pub fn maintenance_fails() {
    struct Checker;

    impl IntoChecker<Checker> for ConnectionIncidents<'_> {
        fn into_checker(self) -> CheckerAction<Checker> {
            CheckerAction::Check(Checker)
        }
    }

    impl ConnectionMaintenanceTask for Checker {
        type Error = CheckerError;

        fn run(self, _conn: &mut rusqlite::Connection) -> Result<(), Self::Error> {
            Err(CheckerError)
        }
    }

    #[derive(Debug)]
    struct CheckerError;
    impl std::error::Error for CheckerError {}

    impl fmt::Display for CheckerError {
        fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
            f.write_str("mercury in retrograde")
        }
    }

    let store = Store::new(StorePath::for_in_memory());
    assert!(
        matches!(store.check::<Checker>(), Err(StoreError::Maintenance(_))),
        "check should fail"
    );
    store.close();
}

pub fn renames_corrupt_database_file() {
    struct SucceedingChecker;

    impl IntoChecker<SucceedingChecker> for ConnectionIncidents<'_> {
        fn into_checker(self) -> CheckerAction<SucceedingChecker> {
            CheckerAction::Check(SucceedingChecker)
        }
    }

    impl ConnectionMaintenanceTask for SucceedingChecker {
        type Error = Infallible;

        fn run(self, _conn: &mut rusqlite::Connection) -> Result<(), Self::Error> {
            Ok(())
        }
    }

    struct FailingChecker;

    impl IntoChecker<FailingChecker> for ConnectionIncidents<'_> {
        fn into_checker(self) -> CheckerAction<FailingChecker> {
            CheckerAction::Check(FailingChecker)
        }
    }

    impl ConnectionMaintenanceTask for FailingChecker {
        type Error = CheckerError;

        fn run(self, _conn: &mut rusqlite::Connection) -> Result<(), Self::Error> {
            Err(CheckerError)
        }
    }

    #[derive(Debug)]
    struct CheckerError;
    impl std::error::Error for CheckerError {}

    impl fmt::Display for CheckerError {
        fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
            f.write_str("mercury in retrograde")
        }
    }

    // Make a temporary copy of our (non-empty, valid) fixture database file.
    let fixture_file_path = std::env::current_dir()
        .expect("current dir")
        .join("kvstore-ok.sqlite");
    // On Android, the system temporary directory is in the app's cache
    // directory, which may be purged at any time.
    let tmp_dir = std::env::temp_dir();
    std::fs::create_dir_all(&tmp_dir).expect("should create temp dir");
    let storage_dir = tempfile::tempdir_in(&tmp_dir).expect("should create storage dir");
    let file_path = storage_dir.path().join("kvstore-ok.sqlite");
    std::fs::copy(fixture_file_path, file_path.as_path())
        .expect("should copy database fixture file to storage dir");

    // Simulate a failing check.
    let store = Arc::new(Store::new(StorePath::OnDisk(file_path.clone())));
    assert!(
        matches!(
            store.check::<FailingChecker>(),
            Err(StoreError::Maintenance(_))
        ),
        "failing checker should fail"
    );
    store.close();

    // Make sure that we backed up the "corrupt" database, and all its
    // temporary files.
    let files = std::fs::read_dir(storage_dir.path())
        .unwrap()
        .map(|result| result.map(|entry| entry.file_name()))
        .collect::<Result<Vec<_>, _>>()
        .expect("should collect files in storage dir");
    assert!(
        files.iter().all(|f| f
            .to_string_lossy()
            .starts_with("kvstore-ok.sqlite.corrupt-")),
        "should rename corrupt database file"
    );

    // Reopening the store should create a fresh database.
    let store = Arc::new(Store::new(StorePath::OnDisk(file_path)));
    assert!(
        store.check::<SucceedingChecker>().is_ok(),
        "succeeding checker should succeed"
    );
    assert!(
        matches!(
            store
                .writer()
                .and_then(|writer| writer.read(|conn| Ok(conn.query_row(
                    "SELECT count(*) FROM dbs",
                    [],
                    |row| row.get::<_, usize>(0)
                )?))),
            Ok(0)
        ),
        "should recreate empty database file"
    );

    store.close();
}
