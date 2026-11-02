/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

use std::hash::{Hash, Hasher};
use std::path::Path;
use std::time::Duration;

use ads_client::bytesize::ByteSize;
use ads_client::http_cache::{CacheOutcome, CachePolicy, HttpCache};
use mockito::mock;
use viaduct::{Client, ClientSettings, Request};

/// Test-only hashable wrapper around Request.
#[derive(Clone)]
struct TestRequest(Request);

impl Hash for TestRequest {
    fn hash<H: Hasher>(&self, state: &mut H) {
        self.0.method.as_str().hash(state);
        self.0.url.as_str().hash(state);
    }
}

impl From<TestRequest> for Request {
    fn from(t: TestRequest) -> Self {
        t.0
    }
}

// Deletes existing temporary dbs passed to `clear_test_dbs` to reset tests that require making them.
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

#[test]
#[ignore = "integration test: run manually with -- --ignored"]
fn test_cache_works_using_real_timeouts() {
    viaduct_dev::init_backend_dev();

    let test_db_location = "integration_tests.db";
    match std::fs::remove_file(std::path::Path::new(test_db_location)) {
        Ok(()) => println!("Deleted previous test's `{test_db_location}`"),
        Err(e) => println!("Did not delete previous test's `{test_db_location}`: {e}"),
    }
    let cache = HttpCache::builder("integration_tests.db")
        .default_ttl(Duration::from_secs(60))
        .max_size(ByteSize::mib(1))
        .build()
        .expect("cache build should succeed");

    let url = format!("{}/v1/ads", mockito::server_url()).parse().unwrap();
    let req = TestRequest(Request::post(url).json(&serde_json::json!({
        "context_id": "12347fff-00b0-aaaa-0978-189231239808",
        "placements": [
            {
            "placement": "mock_pocket_billboard_1",
            "count": 1,
            }
        ],
    })));

    let client = Client::new(ClientSettings::default());
    let test_ttl = 2;

    let _m1 = mock("POST", "/v1/ads")
        .with_status(200)
        .with_header("content-type", "application/json")
        .with_body(r#"{"ok":true}"#)
        .expect(1)
        .create();

    // First call: miss -> store
    let (_, outcomes) = cache
        .send_with_policy(
            &client,
            req.clone(),
            &CachePolicy::CacheFirst {
                ttl: Some(Duration::from_secs(test_ttl)),
            },
        )
        .unwrap();
    assert!(matches!(outcomes.last().unwrap(), CacheOutcome::MissStored));

    // Second call: hit (no extra HTTP due to expect(1))
    let (response, outcomes) = cache
        .send_with_policy(&client, req.clone(), &CachePolicy::default())
        .unwrap();
    assert!(matches!(outcomes.last().unwrap(), CacheOutcome::Hit));
    assert_eq!(response.status, 200);

    let _m2 = mock("POST", "/v1/ads")
        .with_status(200)
        .with_header("content-type", "application/json")
        .with_body(r#"{"ok":true}"#)
        .expect(1)
        .create();

    // Third call: Miss due to timeout for the test_ttl duration
    std::thread::sleep(Duration::from_secs(test_ttl));
    let (response, outcomes) = cache
        .send_with_policy(&client, req, &CachePolicy::default())
        .unwrap();
    assert!(matches!(outcomes.last().unwrap(), CacheOutcome::MissStored));
    assert_eq!(response.status, 200);
}

#[test]
#[ignore = "integration test: run manually with -- --ignored"]
fn test_bad_path_opens_memory_db() {
    clear_test_dbs(&["good_path.db"]);

    let good_path = HttpCache::builder("good_path_httpstore.db")
        .build()
        .expect("Should be able to build a bad path");
    assert!(
        !good_path.is_memory(),
        "A valid path should result in a file-based sqlite database"
    );

    let bad_path = HttpCache::builder("this/is/a/test/bad/path")
        .build()
        .expect("Should be able to build a bad path");
    assert!(
        bad_path.is_memory(),
        "Invalid path should result in a memory-based sqlite database"
    );

    let empty_path = HttpCache::builder("").build();
    assert!(
        empty_path.is_err(),
        "An empty string for a path should result in an error (because of validation step)"
    );

    let no_path = HttpCache::builder_memory()
        .build()
        .expect("Should be able to build a memory db");
    assert!(
        no_path.is_memory(),
        "`builder_memory` should result in a memory-based sqlite database"
    )
}
