/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at https://mozilla.org/MPL/2.0/. */

use std::fs;
use std::path::Path;

use rkv::StoreOptions;

use crate::enrollment::{
    DisqualifiedReason, EnrolledReason, ExperimentEnrollment, NotEnrolledReason,
};
use crate::error::Result;
use crate::evaluator::get_calculated_attributes;
use crate::metrics::{DatabaseLoadExtraDef, DatabaseMigrationExtraDef};
use crate::stateful::enrollment::{get_experiment_participation, get_rollout_participation};
use crate::stateful::persistence::*;
use crate::tests::helpers::TestMetrics;
use crate::{EnrollmentSlugs, EnrollmentStatus, get_active_enrollments};

#[test]
fn test_db_upgrade_no_version() -> Result<()> {
    let tmp_dir = tempfile::tempdir()?;

    let rkv = Database::open_rkv(&tmp_dir)?.0;
    rkv.open_single("meta", StoreOptions::create())?;
    let experiment_store =
        SingleStore::new(rkv.open_single("experiments", StoreOptions::create())?);
    let enrollment_store =
        SingleStore::new(rkv.open_single("enrollments", StoreOptions::create())?);
    let mut writer = rkv.write()?;
    enrollment_store.put(&mut writer, "foo", &"bar".to_owned())?;
    experiment_store.put(&mut writer, "bobo", &"tron".to_owned())?;
    writer.commit()?;

    let db = Database::new(&tmp_dir, TestMetrics::new())?;
    assert_eq!(db.get(StoreId::Meta, DB_KEY_DB_VERSION)?, Some(DB_VERSION));
    assert!(db.collect_all::<String>(StoreId::Enrollments)?.is_empty());
    assert!(db.collect_all::<String>(StoreId::Experiments)?.is_empty());

    Ok(())
}

#[test]
fn test_db_upgrade_unknown_version() -> Result<()> {
    let tmp_dir = tempfile::tempdir()?;

    let rkv = Database::open_rkv(&tmp_dir)?.0;
    let meta_store = SingleStore::new(rkv.open_single("meta", StoreOptions::create())?);
    let experiment_store =
        SingleStore::new(rkv.open_single("experiments", StoreOptions::create())?);
    let enrollment_store =
        SingleStore::new(rkv.open_single("enrollments", StoreOptions::create())?);
    let mut writer = rkv.write()?;
    meta_store.put(&mut writer, DB_KEY_DB_VERSION, &u16::MAX)?;
    enrollment_store.put(&mut writer, "foo", &"bar".to_owned())?;
    experiment_store.put(&mut writer, "bobo", &"tron".to_owned())?;
    writer.commit()?;

    let metrics = TestMetrics::new();
    let db = Database::new(&tmp_dir, metrics.clone())?;

    assert_eq!(
        metrics.get_database_load_events(),
        [DatabaseLoadExtraDef {
            corrupt: Some(false),
            error: None,
            initial_version: Some(u16::MAX),
            migrated_version: Some(DB_VERSION),
            migration_error: None,
        }]
    );

    assert_eq!(
        metrics.get_database_migration_events(),
        [
            DatabaseMigrationExtraDef {
                reason: DatabaseMigrationReason::InvalidVersion.to_string(),
                from_version: u16::MAX,
                to_version: 2,
                error: None,
            },
            DatabaseMigrationExtraDef {
                reason: DatabaseMigrationReason::Upgrade.to_string(),
                from_version: 2,
                to_version: 3,
                error: None,
            },
            DatabaseMigrationExtraDef {
                reason: DatabaseMigrationReason::Upgrade.to_string(),
                from_version: 3,
                to_version: 4,
                error: None,
            },
        ]
    );

    assert_eq!(db.get(StoreId::Meta, DB_KEY_DB_VERSION)?, Some(DB_VERSION));
    assert!(db.collect_all::<String>(StoreId::Enrollments)?.is_empty());
    assert!(db.collect_all::<String>(StoreId::Experiments)?.is_empty());

    Ok(())
}

fn write_garbage(path: &Path) -> Result<u64> {
    const GARBAGE: &[u8] = b"Not a database!";
    const GARBAGE_LEN: usize = GARBAGE.len();

    let garbage_len: u64 = GARBAGE_LEN.try_into().unwrap();

    fs::write(path, GARBAGE)?;
    assert_eq!(fs::metadata(path)?.len(), garbage_len);

    Ok(garbage_len)
}

#[test]
fn test_corrupt_db() -> Result<()> {
    let tmp_dir = tempfile::tempdir()?;
    let db_dir = tmp_dir.path().join("db");
    fs::create_dir(&db_dir)?;

    // The database filename differs depending on the rkv mode.
    let db_file = db_dir.join("data.safe.bin");

    let garbage_len = write_garbage(&db_file)?;

    {
        let metrics = TestMetrics::new();
        // Opening the DB should delete the corrupt file and replace it.
        Database::new(&tmp_dir, metrics.clone())?;
        // Old contents should be removed and replaced with actual data.
        assert_ne!(fs::metadata(&db_file)?.len(), garbage_len);

        assert_eq!(
            metrics.get_database_load_events(),
            [DatabaseLoadExtraDef {
                corrupt: Some(true),
                error: None,
                initial_version: Some(0),
                migrated_version: Some(DB_VERSION),
                migration_error: None,
            }]
        );

        assert_eq!(
            metrics.get_database_migration_events(),
            [
                DatabaseMigrationExtraDef {
                    reason: DatabaseMigrationReason::Upgrade.to_string(),
                    from_version: 0,
                    to_version: 2,
                    error: None,
                },
                DatabaseMigrationExtraDef {
                    reason: DatabaseMigrationReason::Upgrade.to_string(),
                    from_version: 2,
                    to_version: 3,
                    error: None,
                },
                DatabaseMigrationExtraDef {
                    reason: DatabaseMigrationReason::Upgrade.to_string(),
                    from_version: 3,
                    to_version: 4,
                    error: None,
                }
            ]
        );
    }

    // The flag should be cleared.
    {
        let (rkv, open_metadata) = Database::open_rkv(&tmp_dir)?;
        assert!(!open_metadata.corrupt);

        let meta_store = rkv.open_single("meta", StoreOptions::default())?;

        let reader = rkv.read()?;
        let was_corrupt = meta_store.get(&reader, DB_KEY_DB_WAS_CORRUPT)?;

        assert!(was_corrupt.is_none());
    }

    // Subsequent loads (i.e., on the next restart) should not report corruption.
    {
        let metrics = TestMetrics::new();
        Database::new(&tmp_dir, metrics.clone())?;

        assert_eq!(
            metrics.get_database_load_events(),
            [DatabaseLoadExtraDef {
                corrupt: Some(false),
                error: None,
                initial_version: Some(DB_VERSION),
                migrated_version: None,
                migration_error: None,
            }]
        );

        assert_eq!(metrics.get_database_migration_events(), [],);
    }

    Ok(())
}

#[test]
fn test_corrupt_db_get_calculated_attributes() -> Result<()> {
    let tmp_dir = tempfile::tempdir()?;
    let db_dir = tmp_dir.path().join("db");

    fs::create_dir(&db_dir)?;

    let db_filename = db_dir.join("data.safe.bin");
    write_garbage(&db_filename)?;

    // This should have opened the corrupt database and replaced it.
    get_calculated_attributes(None, tmp_dir.path().display().to_string(), "".into())?;

    {
        let (rkv, open_metadata) = Database::open_rkv(&tmp_dir)?;

        // The database should have already been replaced, so meta.corrupt
        // should be false but the db-was-corrupt flag should be set.
        assert!(!open_metadata.corrupt);

        let meta_store = rkv.open_single("meta", StoreOptions::default())?;

        let reader = rkv.read()?;
        let was_corrupt = meta_store.get(&reader, DB_KEY_DB_WAS_CORRUPT)?;

        assert!(matches!(was_corrupt, Some(rkv::value::Value::Json("true"))));
    }

    {
        let metrics = TestMetrics::new();
        Database::new(&tmp_dir, metrics.clone())?;

        assert_eq!(
            metrics.get_database_load_events(),
            [DatabaseLoadExtraDef {
                corrupt: Some(true),
                error: None,
                initial_version: Some(0),
                migrated_version: Some(DB_VERSION),
                migration_error: None,
            }]
        );

        assert_eq!(
            metrics.get_database_migration_events(),
            [
                DatabaseMigrationExtraDef {
                    reason: DatabaseMigrationReason::Upgrade.to_string(),
                    from_version: 0,
                    to_version: 2,
                    error: None,
                },
                DatabaseMigrationExtraDef {
                    reason: DatabaseMigrationReason::Upgrade.to_string(),
                    from_version: 2,
                    to_version: 3,
                    error: None,
                },
                DatabaseMigrationExtraDef {
                    reason: DatabaseMigrationReason::Upgrade.to_string(),
                    from_version: 3,
                    to_version: 4,
                    error: None,
                }
            ]
        );
    }

    // The flag should be cleared.
    {
        let (rkv, open_metadata) = Database::open_rkv(&tmp_dir)?;
        assert!(!open_metadata.corrupt);

        let meta_store = rkv.open_single("meta", StoreOptions::default())?;

        let reader = rkv.read()?;
        let was_corrupt = meta_store.get(&reader, DB_KEY_DB_WAS_CORRUPT)?;

        assert!(was_corrupt.is_none());
    }

    // Subsequent loads (i.e., on the next restart) should not report corruption.
    {
        let metrics = TestMetrics::new();
        Database::new(&tmp_dir, metrics.clone())?;

        assert_eq!(
            metrics.get_database_load_events(),
            [DatabaseLoadExtraDef {
                corrupt: Some(false),
                error: None,
                initial_version: Some(DB_VERSION),
                migrated_version: None,
                migration_error: None,
            }]
        );

        assert_eq!(metrics.get_database_migration_events(), [],);
    }

    Ok(())
}

#[test]
fn test_migrate_db_from_v2_user_opted_out() -> Result<()> {
    error_support::init_for_tests();
    let tmp_dir = tempfile::tempdir()?;

    // Create a v2 database where user opted out globally
    create_old_database_v2_with_global_participation(&tmp_dir, false)?;

    // Open with new version - should trigger migration
    let metrics = TestMetrics::new();
    let db = Database::new(&tmp_dir, metrics.clone())?;

    // Check the database was upgraded to the latest version
    assert_eq!(db.get(StoreId::Meta, DB_KEY_DB_VERSION)?, Some(DB_VERSION));

    // Check that separate flags were set correctly for opted-out user
    let reader = db.read()?;
    assert!(
        !get_experiment_participation(&db, &reader)?, // Should preserve opt-out choice for experiments
    );
    assert!(
        !get_rollout_participation(&db, &reader)?, // Should preserve opt-out choice for rollouts
    );

    // Check old key was removed
    assert_eq!(
        db.get::<bool>(StoreId::Meta, DB_KEY_GLOBAL_USER_PARTICIPATION)?,
        None
    );

    assert_eq!(
        metrics.get_database_load_events(),
        [DatabaseLoadExtraDef {
            corrupt: Some(false),
            error: None,
            initial_version: Some(2),
            migrated_version: Some(DB_VERSION),
            migration_error: None,
        }],
    );

    assert_eq!(
        metrics.get_database_migration_events(),
        [
            DatabaseMigrationExtraDef {
                reason: DatabaseMigrationReason::Upgrade.to_string(),
                from_version: 2,
                to_version: 3,
                error: None,
            },
            DatabaseMigrationExtraDef {
                reason: DatabaseMigrationReason::Upgrade.to_string(),
                from_version: 3,
                to_version: 4,
                error: None,
            }
        ],
    );

    Ok(())
}

#[test]
fn test_migrate_db_from_v2_user_opted_in() -> Result<()> {
    error_support::init_for_tests();
    let tmp_dir = tempfile::tempdir()?;

    // Create a v2 database where user was opted in globally
    create_old_database_v2_with_global_participation(&tmp_dir, true)?;

    let metrics = TestMetrics::new();
    let db = Database::new(&tmp_dir, metrics.clone())?;

    // Check the database was upgraded to the latest version
    assert_eq!(db.get(StoreId::Meta, DB_KEY_DB_VERSION)?, Some(DB_VERSION));

    // Check that separate flags were set correctly for opted-in user
    let reader = db.read()?;
    assert!(
        get_experiment_participation(&db, &reader)?, // Should preserve opt-in choice for experiments
    );
    assert!(
        get_rollout_participation(&db, &reader)?, // Should preserve opt-in choice for rollouts
    );

    // Check old key was removed
    assert_eq!(
        db.get::<bool>(StoreId::Meta, DB_KEY_GLOBAL_USER_PARTICIPATION)?,
        None
    );

    assert_eq!(
        metrics.get_database_load_events(),
        [DatabaseLoadExtraDef {
            corrupt: Some(false),
            error: None,
            initial_version: Some(2),
            migrated_version: Some(4),
            migration_error: None,
        }],
    );

    assert_eq!(
        metrics.get_database_migration_events(),
        [
            DatabaseMigrationExtraDef {
                reason: DatabaseMigrationReason::Upgrade.to_string(),
                from_version: 2,
                to_version: 3,
                error: None,
            },
            DatabaseMigrationExtraDef {
                reason: DatabaseMigrationReason::Upgrade.to_string(),
                from_version: 3,
                to_version: 4,
                error: None,
            }
        ],
    );

    Ok(())
}

#[test]
fn test_migrate_empty() -> Result<()> {
    error_support::init_for_tests();

    let tmp_dir = tempfile::tempdir()?;

    let metrics = TestMetrics::new();
    let db = Database::new(&tmp_dir, metrics.clone())?;
    let meta = db.get_store(StoreId::Meta);
    let reader = db.read()?;
    assert_eq!(
        meta.get::<u16, _>(&reader, DB_KEY_DB_VERSION)?,
        Some(DB_VERSION)
    );
    assert_eq!(
        meta.get::<bool, _>(&reader, DB_KEY_GLOBAL_USER_PARTICIPATION)?,
        None
    );
    assert_eq!(
        meta.get::<bool, _>(&reader, DB_KEY_EXPERIMENT_PARTICIPATION)?,
        Some(true)
    );
    assert_eq!(
        meta.get::<bool, _>(&reader, DB_KEY_ROLLOUT_PARTICIPATION)?,
        Some(true)
    );

    assert_eq!(
        metrics.get_database_load_events(),
        [DatabaseLoadExtraDef {
            corrupt: Some(false),
            error: None,
            initial_version: Some(0),
            migrated_version: Some(DB_VERSION),
            migration_error: None,
        }],
    );

    assert_eq!(
        metrics.get_database_migration_events(),
        [
            DatabaseMigrationExtraDef {
                reason: DatabaseMigrationReason::Upgrade.to_string(),
                from_version: 0,
                to_version: 2,
                error: None,
            },
            DatabaseMigrationExtraDef {
                reason: DatabaseMigrationReason::Upgrade.to_string(),
                from_version: 2,
                to_version: 3,
                error: None,
            },
            DatabaseMigrationExtraDef {
                reason: DatabaseMigrationReason::Upgrade.to_string(),
                from_version: 3,
                to_version: 4,
                error: None,
            },
        ],
    );

    Ok(())
}

#[test]
fn test_migrate_db_from_v1_cumulative_participation_enabled() -> Result<()> {
    error_support::init_for_tests();

    let tmp_dir = tempfile::tempdir()?;

    {
        let rkv = Database::open_rkv(&tmp_dir)?.0;
        let meta = SingleStore::new(rkv.open_single("meta", StoreOptions::create())?);

        let mut writer = rkv.write()?;
        meta.put(&mut writer, DB_KEY_DB_VERSION, &1)?;
        meta.put(&mut writer, DB_KEY_GLOBAL_USER_PARTICIPATION, &true)?;
        writer.commit()?;
    }

    // Open the database and migrate it.
    let metrics = TestMetrics::new();
    let db = Database::new(&tmp_dir, metrics.clone())?;
    let meta = db.get_store(StoreId::Meta);
    let reader = db.read()?;
    assert_eq!(
        meta.get::<u16, _>(&reader, DB_KEY_DB_VERSION)?,
        Some(DB_VERSION)
    );
    assert_eq!(
        meta.get::<bool, _>(&reader, DB_KEY_GLOBAL_USER_PARTICIPATION)?,
        None
    );
    assert_eq!(
        meta.get::<bool, _>(&reader, DB_KEY_EXPERIMENT_PARTICIPATION)?,
        Some(true)
    );
    assert_eq!(
        meta.get::<bool, _>(&reader, DB_KEY_ROLLOUT_PARTICIPATION)?,
        Some(true)
    );

    assert_eq!(
        metrics.get_database_load_events(),
        [DatabaseLoadExtraDef {
            corrupt: Some(false),
            error: None,
            initial_version: Some(1),
            migrated_version: Some(DB_VERSION),
            migration_error: None,
        }],
    );

    assert_eq!(
        metrics.get_database_migration_events(),
        [
            DatabaseMigrationExtraDef {
                reason: DatabaseMigrationReason::Upgrade.to_string(),
                from_version: 1,
                to_version: 2,
                error: None,
            },
            DatabaseMigrationExtraDef {
                reason: DatabaseMigrationReason::Upgrade.to_string(),
                from_version: 2,
                to_version: 3,
                error: None,
            },
            DatabaseMigrationExtraDef {
                reason: DatabaseMigrationReason::Upgrade.to_string(),
                from_version: 3,
                to_version: 4,
                error: None,
            }
        ],
    );

    Ok(())
}

#[test]
fn test_migrate_db_from_v1_cumulative_participation_disabled() -> Result<()> {
    error_support::init_for_tests();

    let tmp_dir = tempfile::tempdir()?;

    {
        let rkv = Database::open_rkv(&tmp_dir)?.0;
        let meta = SingleStore::new(rkv.open_single("meta", StoreOptions::create())?);

        let mut writer = rkv.write()?;
        meta.put(&mut writer, DB_KEY_DB_VERSION, &1)?;
        meta.put(&mut writer, DB_KEY_GLOBAL_USER_PARTICIPATION, &false)?;
        writer.commit()?;
    }

    // Open the database and migrate it.
    let metrics = TestMetrics::new();
    let db = Database::new(&tmp_dir, metrics.clone())?;
    let meta = db.get_store(StoreId::Meta);
    let reader = db.read()?;
    assert_eq!(
        meta.get::<u16, _>(&reader, DB_KEY_DB_VERSION)?,
        Some(DB_VERSION)
    );
    assert_eq!(
        meta.get::<bool, _>(&reader, DB_KEY_GLOBAL_USER_PARTICIPATION)?,
        None
    );
    assert_eq!(
        meta.get::<bool, _>(&reader, DB_KEY_EXPERIMENT_PARTICIPATION)?,
        Some(false)
    );
    assert_eq!(
        meta.get::<bool, _>(&reader, DB_KEY_ROLLOUT_PARTICIPATION)?,
        Some(false)
    );

    assert_eq!(
        metrics.get_database_load_events(),
        [DatabaseLoadExtraDef {
            corrupt: Some(false),
            error: None,
            initial_version: Some(1),
            migrated_version: Some(DB_VERSION),
            migration_error: None,
        }],
    );

    assert_eq!(
        metrics.get_database_migration_events(),
        [
            DatabaseMigrationExtraDef {
                reason: DatabaseMigrationReason::Upgrade.to_string(),
                from_version: 1,
                to_version: 2,
                error: None,
            },
            DatabaseMigrationExtraDef {
                reason: DatabaseMigrationReason::Upgrade.to_string(),
                from_version: 2,
                to_version: 3,
                error: None,
            },
            DatabaseMigrationExtraDef {
                reason: DatabaseMigrationReason::Upgrade.to_string(),
                from_version: 3,
                to_version: 4,
                error: None,
            }
        ],
    );

    Ok(())
}

#[test]
fn test_migrate_db_idempotent() -> Result<()> {
    error_support::init_for_tests();

    let tmp_dir = tempfile::tempdir()?;

    {
        let rkv = Database::open_rkv(&tmp_dir)?.0;
        let meta = SingleStore::new(rkv.open_single("meta", StoreOptions::create())?);

        let mut writer = rkv.write()?;
        meta.put(&mut writer, DB_KEY_DB_VERSION, &DB_VERSION)?;
        meta.put(&mut writer, DB_KEY_EXPERIMENT_PARTICIPATION, &false)?;
        meta.put(&mut writer, DB_KEY_ROLLOUT_PARTICIPATION, &true)?;
        writer.commit()?;
    }

    // Open the database and migrate it. The fields should be unchanged.
    let metrics = TestMetrics::new();
    let db = Database::new(&tmp_dir, metrics.clone())?;
    let meta = db.get_store(StoreId::Meta);
    let reader = db.read()?;
    assert_eq!(
        meta.get::<u16, _>(&reader, DB_KEY_DB_VERSION)?,
        Some(DB_VERSION)
    );
    assert_eq!(
        meta.get::<bool, _>(&reader, DB_KEY_GLOBAL_USER_PARTICIPATION)?,
        None
    );
    assert_eq!(
        meta.get::<bool, _>(&reader, DB_KEY_EXPERIMENT_PARTICIPATION)?,
        Some(false)
    );
    assert_eq!(
        meta.get::<bool, _>(&reader, DB_KEY_ROLLOUT_PARTICIPATION)?,
        Some(true)
    );

    assert_eq!(
        metrics.get_database_load_events(),
        [DatabaseLoadExtraDef {
            corrupt: Some(false),
            error: None,
            initial_version: Some(DB_VERSION),
            migrated_version: None,
            migration_error: None,
        }],
    );

    assert_eq!(metrics.get_database_migration_events(), []);

    Ok(())
}

#[test]
fn test_get_active_enrollments() -> Result<()> {
    error_support::init_for_tests();

    let tmp_dir = tempfile::tempdir()?;

    {
        let db = Database::open_single(&tmp_dir, StoreId::Enrollments)?;
        let mut writer = db.write()?;

        db.store.put(
            &mut writer,
            "experiment-enrolled",
            &ExperimentEnrollment {
                slug: "experiment-enrolled".into(),
                status: EnrollmentStatus::Enrolled {
                    reason: EnrolledReason::Qualified,
                    branch: "treatment-a".into(),
                    prev_gecko_pref_states: None,
                },
            },
        )?;
        db.store.put(
            &mut writer,
            "experiment-wasenrolled",
            &ExperimentEnrollment {
                slug: "experiment-wasenrolled".into(),
                status: EnrollmentStatus::WasEnrolled {
                    branch: "control".into(),
                    experiment_ended_at: 0,
                },
            },
        )?;
        db.store.put(
            &mut writer,
            "experiment-disqualified",
            &ExperimentEnrollment {
                slug: "experiment-disqualified".into(),
                status: EnrollmentStatus::Disqualified {
                    branch: "control".into(),
                    reason: DisqualifiedReason::OptOut,
                },
            },
        )?;
        db.store.put(
            &mut writer,
            "experiment-notenrolled",
            &ExperimentEnrollment {
                slug: "experiment-notenrolled".into(),
                status: EnrollmentStatus::NotEnrolled {
                    reason: NotEnrolledReason::NotTargeted,
                },
            },
        )?;
        db.store.put(
            &mut writer,
            "experiment-error",
            &ExperimentEnrollment {
                slug: "experiment-error".into(),
                status: EnrollmentStatus::Error {
                    reason: "uh oh".into(),
                },
            },
        )?;

        writer.commit()?;
    }

    let active_enrollments = get_active_enrollments(&tmp_dir)?;
    assert_eq!(
        &active_enrollments,
        &[EnrollmentSlugs {
            slug: "experiment-enrolled".into(),
            branch_slug: "treatment-a".into(),
        }]
    );

    Ok(())
}

// Helper function to create a v2 database with global participation flag
fn create_old_database_v2_with_global_participation(
    tmp_dir: &tempfile::TempDir,
    global_participation: bool,
) -> Result<()> {
    let rkv = Database::open_rkv(tmp_dir)?.0;
    let meta_store = SingleStore::new(rkv.open_single("meta", StoreOptions::create())?);
    let mut writer = rkv.write()?;

    // Set version to 2
    meta_store.put(&mut writer, DB_KEY_DB_VERSION, &2u16)?;

    // Set global participation flag (the old way)
    meta_store.put(
        &mut writer,
        DB_KEY_GLOBAL_USER_PARTICIPATION,
        &global_participation,
    )?;

    writer.commit()?;
    Ok(())
}
