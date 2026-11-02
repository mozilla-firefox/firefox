/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

use crate::ads::{Ad, UnexpectedAdKind};
use crate::http_cache::RequestHash;
use crate::telemetry;
use serde::{Deserialize, Deserializer, Serialize};
use std::collections::HashMap;

/// Ads by placement, in the shape MARS sends them.
#[derive(Debug, PartialEq, Serialize)]
#[serde(transparent)]
pub struct AdResponse {
    pub data: HashMap<String, Vec<Ad>>,
}

/// Ads that don't parse are recorded and skipped rather than failing the
/// whole response, and placements left without any are dropped.
impl<'de> Deserialize<'de> for AdResponse {
    fn deserialize<D: Deserializer<'de>>(deserializer: D) -> Result<Self, D::Error> {
        let raw = HashMap::<String, serde_json::Value>::deserialize(deserializer)?;
        let mut data = HashMap::new();

        for (placement, value) in raw {
            if let serde_json::Value::Array(items) = value {
                let ads: Vec<Ad> = items
                    .into_iter()
                    .filter_map(|item| {
                        serde_json::from_value(item)
                            .inspect_err(telemetry::record_invalid_ad_item)
                            .ok()
                    })
                    .collect();
                if !ads.is_empty() {
                    data.insert(placement, ads);
                }
            }
        }

        Ok(AdResponse { data })
    }
}

impl<A: Into<Ad>> From<HashMap<String, Vec<A>>> for AdResponse {
    fn from(data: HashMap<String, Vec<A>>) -> Self {
        AdResponse {
            data: data
                .into_iter()
                .map(|(placement, ads)| (placement, ads.into_iter().map(Into::into).collect()))
                .collect(),
        }
    }
}

impl AdResponse {
    pub fn enrich_callbacks(&mut self, request_hash: &RequestHash) {
        let hash_str = request_hash.to_string();
        for (placement_id, ads) in self.data.iter_mut() {
            for (position, ad) in ads.iter_mut().enumerate() {
                let cap_key = ad.cap_key();
                let callbacks = ad.callbacks_mut();
                callbacks
                    .click
                    .query_pairs_mut()
                    .append_pair("request_hash", &hash_str);
                let mut impression_callback_query = callbacks.impression.query_pairs_mut();
                impression_callback_query.append_pair("request_hash", &hash_str);
                if let Some(cap_key) = cap_key {
                    impression_callback_query.append_pair("cap_key", &cap_key);
                }
                if let Some(report_url) = callbacks.report.as_mut() {
                    report_url
                        .query_pairs_mut()
                        .append_pair("placement_id", placement_id)
                        .append_pair("position", &position.to_string());
                }
            }
        }
    }

    /// Keeps the ads `kind` accepts, dropping placements left with none. Ads of
    /// another kind are recorded like ads that don't parse.
    pub fn into_kind<A>(self) -> HashMap<String, Vec<A>>
    where
        A: TryFrom<Ad, Error = UnexpectedAdKind>,
    {
        self.data
            .into_iter()
            .filter_map(|(placement, ads)| {
                let ads: Vec<A> = ads
                    .into_iter()
                    .filter_map(|ad| {
                        A::try_from(ad)
                            .inspect_err(telemetry::record_invalid_ad_item)
                            .ok()
                    })
                    .collect();
                (!ads.is_empty()).then_some((placement, ads))
            })
            .collect()
    }

    /// The first ad `kind` accepts, for each placement that has one.
    pub fn take_first<A>(self) -> HashMap<String, A>
    where
        A: TryFrom<Ad, Error = UnexpectedAdKind>,
    {
        self.into_kind()
            .into_iter()
            .map(|(placement, mut ads)| (placement, ads.remove(0)))
            .collect()
    }
}

#[cfg(test)]
mod tests {
    use crate::ads::{AdCallbacks, AdImage, AdSpoc, AdTile, SpocFrequencyCaps, SpocRanking};

    use super::*;
    use crate::telemetry::Telemetry;
    use crate::test_utils::{get_example_happy_image_response, get_example_happy_uatile_response};
    use parking_lot::Mutex;
    use serde_json::{from_str, json};
    use std::sync::Arc;
    use url::Url;
    use url_macro::url;

    #[test]
    fn a_malformed_url_is_a_deserialization_error() {
        let response = json!({
            "block_key": "abc123",
            "callbacks": {
                "click": "https://buyanvilseveryday.test/click",
                "impression": "https://buyanvilseveryday.test/impression"
            },
            "format": "Leaderboard",
            "image_url": "not a url",
            "url": "https://buyanvilseveryday.test"
        })
        .to_string();

        // Before these fields were typed as `Url`, this parsed and handed
        // "not a url" straight to the caller.
        assert!(from_str::<AdImage>(&response).is_err());
    }

    #[test]
    fn test_moz_ad_full() {
        let response_full = json!({
            "alt_text": "An ad for an anvil",
            "block_key": "abc123",
            "callbacks": {
                "click": "https://buyanvilseveryday.test/click",
                "impression": "https://buyanvilseveryday.test/impression",
                "report": "https://buyanvilseveryday.test/report"
            },
            "format": "Leaderboard",
            "image_url": "https://buyanvilseveryday.test/img.png",
            "url": "https://buyanvilseveryday.test"
        })
        .to_string();

        let full: AdImage = from_str(&response_full).unwrap();
        assert_eq!(
            full,
            AdImage {
                alt_text: Some("An ad for an anvil".into()),
                block_key: "abc123".into(),
                callbacks: AdCallbacks {
                    click: Url::parse("https://buyanvilseveryday.test/click").unwrap(),
                    impression: Url::parse("https://buyanvilseveryday.test/impression").unwrap(),
                    report: Some(Url::parse("https://buyanvilseveryday.test/report").unwrap()),
                },
                format: "Leaderboard".into(),
                image_url: url!("https://buyanvilseveryday.test/img.png"),
                url: url!("https://buyanvilseveryday.test"),
            }
        );
    }

    #[test]
    fn test_moz_ad_response_partial() {
        let response_partial = json!({
            "alt_text": null,
            "block_key": "abc123",
            "callbacks": {
                "click": "https://example.test/click",
                "impression": "https://example.test/impression",
                "report": null
            },
            "format": "Leaderboard",
            "image_url": "https://example.test/image.png",
            "url": "https://example.test/item"
        })
        .to_string();

        let partial: AdImage = from_str(&response_partial).unwrap();
        assert_eq!(
            partial,
            AdImage {
                alt_text: None,
                block_key: "abc123".into(),
                callbacks: AdCallbacks {
                    click: Url::parse("https://example.test/click").unwrap(),
                    impression: Url::parse("https://example.test/impression").unwrap(),
                    report: None,
                },
                format: "Leaderboard".into(),
                image_url: url!("https://example.test/image.png"),
                url: url!("https://example.test/item"),
            }
        );
    }

    #[test]
    fn test_ad_response_serialization() {
        let raw_ad_response = json!({
            "missing_click_url": [
                {
                    "block_key": "abc123",
                    "url": "https://ads.fakeexample.org/example_ad_1",
                    "image_url": "https://ads.fakeexample.org/example_image_1",
                    "format": "billboard",
                    "alt_text": "An ad for a puppy",
                    "callbacks": {
                        "impression": "https://ads.fakeexample.org/impression/example_ad_1",
                    }
                }
            ],
            "incorrect_click_url": [
                {
                    "block_key": "abc123",
                    "url": "https://ads.fakeexample.org/example_ad_1",
                    "image_url": "https://ads.fakeexample.org/example_image_1",
                    "format": "billboard",
                    "alt_text": "An ad for a puppy",
                    "callbacks": {
                        "click": "incorrect-click-url",
                        "impression": "https://ads.fakeexample.org/impression/example_ad_1",
                    }
                }
            ],
            "missing_impression_url": [
                {
                    "block_key": "abc123",
                    "url": "https://ads.fakeexample.org/example_ad_1",
                    "image_url": "https://ads.fakeexample.org/example_image_1",
                    "format": "billboard",
                    "alt_text": "An ad for a puppy",
                    "callbacks": {
                        "click": "https://ads.fakeexample.org/click/example_ad_1",
                    }
                }
            ],
            "incorrect_impression_url": [
                {
                    "block_key": "abc123",
                    "url": "https://ads.fakeexample.org/example_ad_2",
                    "image_url": "https://ads.fakeexample.org/example_image_2",
                    "format": "skyscraper",
                    "alt_text": "An ad for a pet duck",
                    "callbacks": {
                        "click": "https://ads.fakeexample.org/click/example_ad_2",
                        "impression": "incorrect-impression-url",
                    }
                }
            ],
            "valid_ad": [
                {
                    "block_key": "abc123",
                    "url": "https://ads.fakeexample.org/example_ad_3",
                    "image_url": "https://ads.fakeexample.org/example_image_3",
                    "format": "skyscraper",
                    "alt_text": "An ad for a pet duck",
                    "callbacks": {
                        "click": "https://ads.fakeexample.org/click/example_ad_3",
                        "impression": "https://ads.fakeexample.org/impression/example_ad_3",
                        "report": "https://ads.fakeexample.org/report/example_ad_3"
                    }
                }
            ]
        });

        let parsed = serde_json::from_value::<AdResponse>(raw_ad_response).unwrap();

        let expected = AdResponse::from(HashMap::from([(
            "valid_ad".to_string(),
            vec![AdImage {
                url: url!("https://ads.fakeexample.org/example_ad_3"),
                image_url: url!("https://ads.fakeexample.org/example_image_3"),
                format: "skyscraper".to_string(),
                block_key: "abc123".into(),
                alt_text: Some("An ad for a pet duck".to_string()),
                callbacks: AdCallbacks {
                    click: Url::parse("https://ads.fakeexample.org/click/example_ad_3").unwrap(),
                    impression: Url::parse("https://ads.fakeexample.org/impression/example_ad_3")
                        .unwrap(),
                    report: Some(
                        Url::parse("https://ads.fakeexample.org/report/example_ad_3").unwrap(),
                    ),
                },
            }],
        )]));

        assert_eq!(parsed, expected);
    }

    #[test]
    fn test_empty_ad_response_serialization() {
        let raw_ad_response = json!({
            "example_placement_1": [],
            "example_placement_2": []
        });

        let parsed = serde_json::from_value::<AdResponse>(raw_ad_response).unwrap();

        let expected = AdResponse {
            data: HashMap::from([]),
        };

        assert_eq!(parsed, expected);
    }

    #[test]
    fn test_take_first() {
        let mut data: HashMap<String, Vec<AdImage>> = HashMap::new();
        data.insert(
            "placement_1".to_string(),
            vec![
                AdImage {
                    alt_text: Some("First ad".to_string()),
                    block_key: "key1".to_string(),
                    callbacks: AdCallbacks {
                        click: Url::parse("https://example.com/click1").unwrap(),
                        impression: Url::parse("https://example.com/impression1").unwrap(),
                        report: None,
                    },
                    format: "billboard".to_string(),
                    image_url: url!("https://example.com/image1.png"),
                    url: url!("https://example.com/ad1"),
                },
                AdImage {
                    alt_text: Some("Second ad".to_string()),
                    block_key: "key2".to_string(),
                    callbacks: AdCallbacks {
                        click: Url::parse("https://example.com/click2").unwrap(),
                        impression: Url::parse("https://example.com/impression2").unwrap(),
                        report: None,
                    },
                    format: "billboard".to_string(),
                    image_url: url!("https://example.com/image2.png"),
                    url: url!("https://example.com/ad2"),
                },
            ],
        );
        data.insert(
            "placement_2".to_string(),
            vec![AdImage {
                alt_text: Some("Third ad".to_string()),
                block_key: "key3".to_string(),
                callbacks: AdCallbacks {
                    click: Url::parse("https://example.com/click3").unwrap(),
                    impression: Url::parse("https://example.com/impression3").unwrap(),
                    report: None,
                },
                format: "skyscraper".to_string(),
                image_url: url!("https://example.com/image3.png"),
                url: url!("https://example.com/ad3"),
            }],
        );
        data.insert("placement_3".to_string(), vec![]);

        let result = AdResponse::from(data).take_first::<AdImage>();

        assert_eq!(result.len(), 2);
        assert!(result.contains_key("placement_1"));
        assert!(result.contains_key("placement_2"));
        assert!(!result.contains_key("placement_3"));

        let first_ad = result.get("placement_1").unwrap();
        assert_eq!(first_ad.alt_text, Some("First ad".to_string()));
        assert_eq!(first_ad.block_key, "key1");

        let second_ad = result.get("placement_2").unwrap();
        assert_eq!(second_ad.alt_text, Some("Third ad".to_string()));
        assert_eq!(second_ad.block_key, "key3");
    }

    #[test]
    fn test_empty_ad_response() {
        let empty = || AdResponse {
            data: HashMap::new(),
        };

        let mut response = empty();
        response.enrich_callbacks(&RequestHash::from("abc123def456"));
        assert_eq!(response, empty());

        assert!(empty().into_kind::<AdImage>().is_empty());
        assert!(empty().take_first::<AdImage>().is_empty());
    }

    #[test]
    fn test_into_kind_drops_placements_left_empty() {
        let image = get_example_happy_image_response()
            .data
            .remove("example_placement_1")
            .unwrap()
            .remove(0);
        let tile = get_example_happy_uatile_response()
            .data
            .remove("example_placement_1")
            .unwrap()
            .remove(0);
        let response = || AdResponse {
            data: HashMap::from([
                ("empty".to_string(), vec![]),
                ("tile".to_string(), vec![tile.clone()]),
                (
                    "tile_then_image".to_string(),
                    vec![tile.clone(), image.clone()],
                ),
            ]),
        };

        let images = response().into_kind::<AdImage>();
        assert_eq!(images.len(), 1);
        assert_eq!(images["tile_then_image"].len(), 1);

        // `take_first` never sees a placement without an image, and skips the
        // tile ahead of one.
        let mut first = response().take_first::<AdImage>();
        assert_eq!(first.len(), 1);
        assert_eq!(Ad::from(first.remove("tile_then_image").unwrap()), image);
    }

    #[test]
    fn test_enrich_callbacks() {
        let mut response = AdResponse::from(HashMap::from([(
            "mock_tile_1".to_string(),
            vec![
                AdImage {
                    alt_text: None,
                    block_key: "key1".into(),
                    callbacks: AdCallbacks {
                        click: Url::parse("https://example.com/click1").unwrap(),
                        impression: Url::parse("https://example.com/impression1").unwrap(),
                        report: Some(Url::parse("https://example.com/report").unwrap()),
                    },
                    format: "billboard".to_string(),
                    image_url: url!("https://example.com/image1.png"),
                    url: url!("https://example.com/ad1"),
                },
                AdImage {
                    alt_text: None,
                    block_key: "key2".into(),
                    callbacks: AdCallbacks {
                        click: Url::parse("https://example.com/click2").unwrap(),
                        impression: Url::parse("https://example.com/impression2").unwrap(),
                        report: Some(Url::parse("https://example.com/report").unwrap()),
                    },
                    format: "billboard".to_string(),
                    image_url: url!("https://example.com/image2.png"),
                    url: url!("https://example.com/ad2"),
                },
            ],
        )]));

        let request_hash = RequestHash::from("abc123def456");
        response.enrich_callbacks(&request_hash);

        let ads = response.into_kind::<AdImage>();
        let ads = &ads["mock_tile_1"];

        // request_hash added to click and impression
        assert!(ads[0]
            .callbacks
            .click
            .query()
            .unwrap_or("")
            .contains("request_hash=abc123def456"));
        assert!(ads[0]
            .callbacks
            .impression
            .query()
            .unwrap_or("")
            .contains("request_hash=abc123def456"));

        // placement info added to report
        let report_0 = ads[0]
            .callbacks
            .report
            .as_ref()
            .unwrap()
            .query()
            .unwrap_or("");
        assert!(report_0.contains("placement_id=mock_tile_1"));
        assert!(report_0.contains("position=0"));

        let report_1 = ads[1]
            .callbacks
            .report
            .as_ref()
            .unwrap()
            .query()
            .unwrap_or("");
        assert!(report_1.contains("placement_id=mock_tile_1"));
        assert!(report_1.contains("position=1"));
    }

    #[test]
    fn test_enrich_callbacks_spoc_impressions_have_cap_key() {
        let mut response = AdResponse::from(HashMap::from([(
            "tile1".into(),
            vec![
                AdSpoc {
                    block_key: "block_key1".into(),
                    callbacks: AdCallbacks {
                        click: Url::parse("https://example.com/click1").unwrap(),
                        impression: Url::parse("https://example.com/impression1").unwrap(),
                        report: Some(Url::parse("https://example.com/report1").unwrap()),
                    },
                    caps: SpocFrequencyCaps {
                        cap_key: "cap_key1".into(),
                        day: 100,
                    },
                    domain: "1.example.com".into(),
                    excerpt: "excerpt1".into(),
                    format: "format1".into(),
                    image_url: url!("https://example.com/image1.png"),
                    url: url!("https://example.com/ad1"),
                    ranking: SpocRanking {
                        priority: 1,
                        personalization_models: None,
                        item_score: 1.0,
                    },
                    sponsor: "sponsor1".into(),
                    sponsored_by_override: None,
                    title: "title1".into(),
                },
                AdSpoc {
                    block_key: "block_key2".into(),
                    callbacks: AdCallbacks {
                        click: Url::parse("https://example.com/click2").unwrap(),
                        impression: Url::parse("https://example.com/impression2").unwrap(),
                        report: Some(Url::parse("https://example.com/report2").unwrap()),
                    },
                    caps: SpocFrequencyCaps {
                        cap_key: "cap_key2".into(),
                        day: 200,
                    },
                    domain: "2.example.com".into(),
                    excerpt: "excerpt2".into(),
                    format: "format2".into(),
                    image_url: url!("https://example.com/image2.png"),
                    url: url!("https://example.com/ad2"),
                    ranking: SpocRanking {
                        priority: 2,
                        personalization_models: None,
                        item_score: 2.0,
                    },
                    sponsor: "sponsor2".into(),
                    sponsored_by_override: None,
                    title: "title2".into(),
                },
            ],
        )]));

        let request_hash = RequestHash::from("abc123def456");
        response.enrich_callbacks(&request_hash);

        let ads = response.into_kind::<AdSpoc>();
        let ads = &ads["tile1"];

        assert!(ads[0]
            .callbacks
            .impression
            .query()
            .unwrap_or("")
            .contains("cap_key=cap_key1"));

        assert!(ads[1]
            .callbacks
            .impression
            .query()
            .unwrap_or("")
            .contains("cap_key=cap_key2"));
    }

    #[test]
    fn test_enrich_callbacks_skips_ads_without_report_url() {
        let mut response = AdResponse::from(HashMap::from([(
            "mock_tile_1".to_string(),
            vec![AdImage {
                alt_text: None,
                block_key: "key1".into(),
                callbacks: AdCallbacks {
                    click: Url::parse("https://example.com/click").unwrap(),
                    impression: Url::parse("https://example.com/impression").unwrap(),
                    report: None,
                },
                format: "billboard".to_string(),
                image_url: url!("https://example.com/image.png"),
                url: url!("https://example.com/ad"),
            }],
        )]));

        // Should not panic
        let request_hash = RequestHash::from("abc123def456");
        response.enrich_callbacks(&request_hash);

        let ads = response.into_kind::<AdImage>();
        let ad = &ads["mock_tile_1"][0];
        assert!(ad.callbacks.report.is_none());
        // click/impression still get request_hash
        assert!(ad
            .callbacks
            .click
            .query()
            .unwrap_or("")
            .contains("request_hash=abc123def456"));
        assert!(ad
            .callbacks
            .impression
            .query()
            .unwrap_or("")
            .contains("request_hash=abc123def456"));
    }

    #[derive(Clone, Default)]
    struct RecordingTelemetry(Arc<Mutex<Vec<(String, String)>>>);

    impl Telemetry for RecordingTelemetry {
        fn record_build_cache_error(&self, _label: &str, _value: String) {}
        fn record_client_error(&self, _label: &str, _value: String) {}
        fn record_client_operation_total(&self, _label: &str) {}
        fn record_deserialization_error(&self, label: &str, value: String) {
            self.0.lock().push((label.to_string(), value));
        }
        fn record_http_cache_outcome(&self, _label: &str, _value: String) {}
    }

    #[test]
    fn test_into_kind_records_ads_of_another_kind() {
        let _lock = telemetry::test_lock();
        let recording = RecordingTelemetry::default();
        telemetry::install(recording.clone());

        let tiles = get_example_happy_image_response().into_kind::<AdTile>();
        telemetry::uninstall();

        assert!(tiles.is_empty());
        // Other tests may record concurrently, so only look for ours.
        assert!(recording.0.lock().iter().any(|(label, value)| {
            label == "invalid_ad_item"
                && value == "Expected a tile, got an ad with format `billboard`"
        }));
    }
}
