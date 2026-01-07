/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

use std::time::Duration;

use ads_client::{
    http_cache::{ByteSize, CacheOutcome, HttpCache, RequestCachePolicy},
    MozAdsClient, MozAdsPlacementRequest, MozAdsPlacementRequestWithCount,
};
use url::Url;
use viaduct::Request;

#[test]
#[ignore]
fn test_mock_pocket_billboard_1_placement() {
    viaduct_dev::init_backend_dev();

    let client = MozAdsClient::new(None);

    let placement_request = MozAdsPlacementRequest {
        placement_id: "mock_pocket_billboard_1".to_string(),
        iab_content: None,
    };

    let result = client.request_image_ads(vec![placement_request], None);

    assert!(result.is_ok(), "Failed to request ads: {:?}", result.err());

    let placements = result.unwrap();

    assert!(
        placements.contains_key("mock_pocket_billboard_1"),
        "Response should contain placement_id 'mock_pocket_billboard_1'"
    );

    placements
        .get("mock_pocket_billboard_1")
        .expect("Placement should exist");
}

#[test]
#[ignore]
fn test_newtab_spocs_placement() {
    viaduct_dev::init_backend_dev();

    let client = MozAdsClient::new(None);

    let count = 3;
    let placement_request = MozAdsPlacementRequestWithCount {
        placement_id: "newtab_spocs".to_string(),
        count,
        iab_content: None,
    };

    let result = client.request_spoc_ads(vec![placement_request], None);

    assert!(result.is_ok(), "Failed to request ads: {:?}", result.err());

    let placements = result.unwrap();

    assert!(
        placements.contains_key("newtab_spocs"),
        "Response should contain placement_id 'newtab_spocs'"
    );

    let spocs = placements
        .get("newtab_spocs")
        .expect("Placement should exist");

    assert_eq!(
        spocs.len(),
        count as usize,
        "Number of spocs should equal count parameter"
    );
}

#[test]
#[ignore]
fn test_newtab_tile_1_placement() {
    viaduct_dev::init_backend_dev();

    let client = MozAdsClient::new(None);

    let placement_request = MozAdsPlacementRequest {
        placement_id: "newtab_tile_1".to_string(),
        iab_content: None,
    };

    let result = client.request_tile_ads(vec![placement_request], None);

    assert!(result.is_ok(), "Failed to request ads: {:?}", result.err());

    let placements = result.unwrap();

    assert!(
        placements.contains_key("newtab_tile_1"),
        "Response should contain placement_id 'newtab_tile_1'"
    );

    placements
        .get("newtab_tile_1")
        .expect("Placement should exist");
}

#[test]
#[ignore]
fn test_cache_works_using_real_timeouts() {
    viaduct_dev::init_backend_dev();

    let cache = HttpCache::builder("integration_tests.db")
        .default_ttl(Duration::from_secs(60))
        .max_size(ByteSize::mib(1))
        .build()
        .expect("cache build should succeed");

    let url = Url::parse("https://ads.mozilla.org/v1/ads").unwrap();
    let req = Request::post(url).json(&serde_json::json!({
        "context_id": "12347fff-00b0-aaaa-0978-189231239808",
        "placements": [
            {
            "placement": "mock_pocket_billboard_1",
            "count": 1,
            }
        ],
    }));

    let test_ttl = 2;

    // First call: miss -> store
    let o1 = cache
        .send_with_policy(
            &req.clone(),
            &RequestCachePolicy {
                mode: ads_client::http_cache::CacheMode::CacheFirst,
                ttl_seconds: Some(test_ttl),
            },
        )
        .unwrap();
    matches!(o1.cache_outcome, CacheOutcome::MissStored);

    // Second call: hit (no extra HTTP) but no refresh
    let o2 = cache
        .send_with_policy(&req, &RequestCachePolicy::default())
        .unwrap();
    matches!(o2.cache_outcome, CacheOutcome::Hit);
    assert_eq!(o2.response.status, 200);

    // Third call: Miss due to timeout for the test_ttl duration
    std::thread::sleep(Duration::from_secs(test_ttl));
    let o3 = cache
        .send_with_policy(&req, &RequestCachePolicy::default())
        .unwrap();
    matches!(o3.cache_outcome, CacheOutcome::MissStored);
    assert_eq!(o3.response.status, 200);
}
