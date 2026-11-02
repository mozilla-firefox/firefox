/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

use std::collections::HashMap;

use url::Url;
use url_macro::url;

#[cfg(feature = "stateful")]
use crate::ads::{Ad, PlacementId};
use crate::ads::{AdCallbacks, AdImage, AdSpoc, AdTile, SpocFrequencyCaps, SpocRanking};
use crate::mars::{
    ad_request::{AdContentCategory, AdPlacementRequest, IABContentTaxonomy},
    ad_response::AdResponse,
};
use crate::MozAdsTelemetry;

pub const TEST_CONTEXT_ID: &str = "00000000-0000-4000-8000-000000000001";

pub struct NoopMozAdsTelemetry;

impl MozAdsTelemetry for NoopMozAdsTelemetry {
    fn record_build_cache_error(&self, _label: String, _value: String) {}
    fn record_client_error(&self, _label: String, _value: String) {}
    fn record_client_operation_total(&self, _label: String) {}
    fn record_deserialization_error(&self, _label: String, _value: String) {}
    fn record_http_cache_outcome(&self, _label: String, _value: String) {}
}

#[cfg(feature = "stateful")]
pub fn get_example_happy_image_ads(placement_id: &str) -> (PlacementId, Vec<Ad>) {
    let images = get_example_happy_image_response()
        .data
        .remove("example_placement_1")
        .expect("example image response has example_placement_1");
    (PlacementId::new(placement_id), images)
}

pub fn make_happy_placement_requests() -> Vec<AdPlacementRequest> {
    vec![
        AdPlacementRequest {
            content: Some(AdContentCategory {
                categories: vec!["entertainment".to_string()],
                taxonomy: IABContentTaxonomy::IAB2_1,
            }),
            count: 1,
            placement: "example_placement_1".to_string(),
        },
        AdPlacementRequest {
            content: Some(AdContentCategory {
                categories: vec!["entertainment".to_string()],
                taxonomy: IABContentTaxonomy::IAB2_1,
            }),
            count: 1,
            placement: "example_placement_2".to_string(),
        },
    ]
}

pub fn get_example_happy_image_response() -> AdResponse {
    let base_url = mockito::server_url();
    AdResponse::from(HashMap::from([
        (
            "example_placement_1".to_string(),
            vec![AdImage {
                url: url!("https://ads.fakeexample.org/example_ad_1"),
                image_url: url!("https://ads.fakeexample.org/example_image_1"),
                format: "billboard".to_string(),
                block_key: "abc123".into(),
                alt_text: Some("An ad for a puppy".to_string()),
                callbacks: AdCallbacks {
                    click: Url::parse(&format!("{}/click/example_ad_1", base_url)).unwrap(),
                    impression: Url::parse(&format!("{}/impression/example_ad_1", base_url))
                        .unwrap(),
                    report: Some(Url::parse(&format!("{}/report/example_ad_1", base_url)).unwrap()),
                },
            }],
        ),
        (
            "example_placement_2".to_string(),
            vec![AdImage {
                url: url!("https://ads.fakeexample.org/example_ad_2"),
                image_url: url!("https://ads.fakeexample.org/example_image_2"),
                format: "skyscraper".to_string(),
                block_key: "abc123".into(),
                alt_text: Some("An ad for a pet duck".to_string()),
                callbacks: AdCallbacks {
                    click: Url::parse(&format!("{}/click/example_ad_2", base_url)).unwrap(),
                    impression: Url::parse(&format!("{}/impression/example_ad_2", base_url))
                        .unwrap(),
                    report: Some(Url::parse(&format!("{}/report/example_ad_2", base_url)).unwrap()),
                },
            }],
        ),
    ]))
}

pub fn get_example_happy_spoc_response() -> AdResponse {
    AdResponse::from(HashMap::from([
        (
            "example_placement_1".to_string(),
            vec![AdSpoc {
                url: url!("https://ads.fakeexample.org/example_spoc_1"),
                image_url: url!("https://ads.fakeexample.org/example_spoc_image_1"),
                format: "spoc".to_string(),
                block_key: "spoc123".into(),
                title: "Example Spoc Title".to_string(),
                excerpt: "This is an example spoc excerpt".to_string(),
                domain: "example.com".to_string(),
                sponsor: "Example Sponsor".to_string(),
                sponsored_by_override: None,
                caps: SpocFrequencyCaps {
                    cap_key: "spoc_cap_1".to_string(),
                    day: 7,
                },
                ranking: SpocRanking {
                    priority: 1,
                    personalization_models: Some(HashMap::from([("model1".to_string(), 10)])),
                    item_score: 0.85,
                },
                callbacks: AdCallbacks {
                    click: Url::parse("https://ads.fakeexample.org/click/example_spoc_1").unwrap(),
                    impression: Url::parse("https://ads.fakeexample.org/impression/example_spoc_1")
                        .unwrap(),
                    report: Some(
                        Url::parse("https://ads.fakeexample.org/report/example_spoc_1").unwrap(),
                    ),
                },
            }],
        ),
        (
            "example_placement_2".to_string(),
            vec![AdSpoc {
                url: url!("https://ads.fakeexample.org/example_spoc_2"),
                image_url: url!("https://ads.fakeexample.org/example_spoc_image_2"),
                format: "spoc".to_string(),
                block_key: "spoc456".into(),
                title: "Another Spoc Title".to_string(),
                excerpt: "This is another example spoc excerpt".to_string(),
                domain: "another-example.com".to_string(),
                sponsor: "Another Sponsor".to_string(),
                sponsored_by_override: Some("Override Sponsor".to_string()),
                caps: SpocFrequencyCaps {
                    cap_key: "spoc_cap_2".to_string(),
                    day: 14,
                },
                ranking: SpocRanking {
                    priority: 2,
                    personalization_models: None,
                    item_score: 0.75,
                },
                callbacks: AdCallbacks {
                    click: Url::parse("https://ads.fakeexample.org/click/example_spoc_2").unwrap(),
                    impression: Url::parse("https://ads.fakeexample.org/impression/example_spoc_2")
                        .unwrap(),
                    report: Some(
                        Url::parse("https://ads.fakeexample.org/report/example_spoc_2").unwrap(),
                    ),
                },
            }],
        ),
    ]))
}

pub fn get_example_happy_uatile_response() -> AdResponse {
    AdResponse::from(HashMap::from([
        (
            "example_placement_1".to_string(),
            vec![AdTile {
                url: url!("https://ads.fakeexample.org/example_uatile_1"),
                image_url: url!("https://ads.fakeexample.org/example_uatile_image_1"),
                format: "tile".to_string(),
                block_key: "uatile123".into(),
                name: "Example UA Tile".to_string(),
                callbacks: AdCallbacks {
                    click: Url::parse("https://ads.fakeexample.org/click/example_uatile_1")
                        .unwrap(),
                    impression: Url::parse(
                        "https://ads.fakeexample.org/impression/example_uatile_1",
                    )
                    .unwrap(),
                    report: Some(
                        Url::parse("https://ads.fakeexample.org/report/example_uatile_1").unwrap(),
                    ),
                },
            }],
        ),
        (
            "example_placement_2".to_string(),
            vec![AdTile {
                url: url!("https://ads.fakeexample.org/example_uatile_2"),
                image_url: url!("https://ads.fakeexample.org/example_uatile_image_2"),
                format: "tile".to_string(),
                block_key: "uatile456".into(),
                name: "Another UA Tile".to_string(),
                callbacks: AdCallbacks {
                    click: Url::parse("https://ads.fakeexample.org/click/example_uatile_2")
                        .unwrap(),
                    impression: Url::parse(
                        "https://ads.fakeexample.org/impression/example_uatile_2",
                    )
                    .unwrap(),
                    report: Some(
                        Url::parse("https://ads.fakeexample.org/report/example_uatile_2").unwrap(),
                    ),
                },
            }],
        ),
    ]))
}
