/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#[cfg(feature = "stateful")]
use ads_client::ads_store::builder::AdsStoreBuilder;
#[cfg(feature = "stateful")]
use std::path::Path;

// Deletes existing temporary dbs passed to `clear_test_dbs` to reset tests that require making them.
#[cfg(feature = "stateful")]
fn clear_test_dbs(test_db_locations: &[&'static str]) {
    for test_db_location in test_db_locations {
        let test_db_location_path = Path::new(test_db_location);

        // A small sanity check to ensure the passed db test is a subfile of the current directory.
        if test_db_location_path.components().count() != 1 || !test_db_location_path.is_relative() {
            panic!("Tests provided to `clear_test_dbs` must single-component and in the current directory: {test_db_location}");
        }
        match std::fs::remove_file(test_db_location_path) {
            Ok(()) => println!("Deleted previous test's `{test_db_location}`"),
            Err(e) => println!("Did not delete previous test's `{test_db_location}`: {e}"),
        }
    }
}

#[cfg(feature = "stateful")]
#[test]
#[ignore = "integration test: run manually with -- --ignored"]
fn test_bad_path_opens_memory_db() {
    clear_test_dbs(&["good_path.db"]);

    let good_path = AdsStoreBuilder::new("good_path.db")
        .build()
        .expect("Should be able to build a bad path");
    assert!(
        !good_path.is_memory().unwrap(),
        "A valid path should result in a file-based sqlite database"
    );

    let bad_path = AdsStoreBuilder::new("this/is/a/test/bad/path")
        .build()
        .expect("Should be able to build a bad path");
    assert!(
        bad_path.is_memory().unwrap(),
        "Invalid path should result in a memory-based sqlite database"
    );

    let empty_path = AdsStoreBuilder::new("").build();
    assert!(
        empty_path.is_err(),
        "An empty string for a path should result in an error"
    );
}
