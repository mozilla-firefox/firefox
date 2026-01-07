/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

use crate::http_cache::RequestHash;
use serde::de::DeserializeOwned;
use serde::Deserializer;
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use url::Url;

#[derive(Debug, Deserialize, PartialEq, Serialize)]
pub struct AdResponse<T: AdResponseValue> {
    #[serde(deserialize_with = "deserialize_ad_response", flatten)]
    pub data: HashMap<String, Vec<T>>,
}

impl<T: AdResponseValue> AdResponse<T> {
    pub fn add_request_hash_to_callbacks(&mut self, request_hash: &RequestHash) {
        for ads in self.data.values_mut() {
            for ad in ads.iter_mut() {
                let callbacks = ad.callbacks_mut();
                let hash_str = request_hash.to_string();
                callbacks
                    .click
                    .query_pairs_mut()
                    .append_pair("request_hash", &hash_str);
                callbacks
                    .impression
                    .query_pairs_mut()
                    .append_pair("request_hash", &hash_str);
            }
        }
    }

    pub fn take_first(self) -> HashMap<String, T> {
        self.data
            .into_iter()
            .filter_map(|(k, mut v)| {
                if v.is_empty() {
                    None
                } else {
                    Some((k, v.remove(0)))
                }
            })
            .collect()
    }
}

pub fn pop_request_hash_from_url(url: &mut Url) -> Option<RequestHash> {
    let mut request_hash = None;
    let mut query = url::form_urlencoded::Serializer::new(String::new());

    for (key, value) in url.query_pairs() {
        if key == "request_hash" {
            request_hash = Some(RequestHash::from(value.as_ref()));
        } else {
            query.append_pair(&key, &value);
        }
    }

    let query_string = query.finish();
    if query_string.is_empty() {
        url.set_query(None);
    } else {
        url.set_query(Some(&query_string));
    }
    request_hash
}

fn deserialize_ad_response<'de, D, T>(deserializer: D) -> Result<HashMap<String, Vec<T>>, D::Error>
where
    D: Deserializer<'de>,
    T: AdResponseValue,
{
    let raw = HashMap::<String, serde_json::Value>::deserialize(deserializer)?;
    let mut result = HashMap::new();

    for (key, value) in raw {
        if let serde_json::Value::Array(arr) = value {
            let mut ads: Vec<T> = vec![];
            for item in arr {
                if let Ok(ad) = serde_json::from_value::<T>(item.clone()) {
                    ads.push(ad);
                } else {
                    #[cfg(not(test))]
                    {
                        use crate::instrument::{emit_telemetry_event, TelemetryEvent};
                        let _ = emit_telemetry_event(Some(TelemetryEvent::InvalidUrlError));
                    }
                }
            }
            if !ads.is_empty() {
                result.insert(key, ads);
            }
        }
    }

    Ok(result)
}

#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub struct AdImage {
    pub alt_text: Option<String>,
    pub block_key: String,
    pub callbacks: AdCallbacks,
    pub format: String,
    pub image_url: String,
    pub url: String,
}

#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub struct AdSpoc {
    pub block_key: String,
    pub callbacks: AdCallbacks,
    pub caps: SpocFrequencyCaps,
    pub domain: String,
    pub excerpt: String,
    pub format: String,
    pub image_url: String,
    pub ranking: SpocRanking,
    pub sponsor: String,
    pub sponsored_by_override: Option<String>,
    pub title: String,
    pub url: String,
}

#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub struct AdTile {
    pub block_key: String,
    pub callbacks: AdCallbacks,
    pub format: String,
    pub image_url: String,
    pub name: String,
    pub url: String,
}

#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub struct SpocFrequencyCaps {
    pub cap_key: String,
    pub day: u32,
}

#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub struct SpocRanking {
    pub priority: u32,
    pub personalization_models: Option<HashMap<String, u32>>,
    pub item_score: f64,
}

#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
pub struct AdCallbacks {
    pub click: Url,
    pub impression: Url,
    pub report: Option<Url>,
}

pub trait AdResponseValue: DeserializeOwned {
    fn callbacks_mut(&mut self) -> &mut AdCallbacks;
}

impl AdResponseValue for AdImage {
    fn callbacks_mut(&mut self) -> &mut AdCallbacks {
        &mut self.callbacks
    }
}

impl AdResponseValue for AdSpoc {
    fn callbacks_mut(&mut self) -> &mut AdCallbacks {
        &mut self.callbacks
    }
}

impl AdResponseValue for AdTile {
    fn callbacks_mut(&mut self) -> &mut AdCallbacks {
        &mut self.callbacks
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::{from_str, json};

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
                image_url: "https://buyanvilseveryday.test/img.png".into(),
                url: "https://buyanvilseveryday.test".into(),
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
                image_url: "https://example.test/image.png".into(),
                url: "https://example.test/item".into(),
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
        })
        .to_string();

        let parsed: AdResponse<AdImage> = from_str(&raw_ad_response).unwrap();

        let expected = AdResponse {
            data: HashMap::from([(
                "valid_ad".to_string(),
                vec![AdImage {
                    url: "https://ads.fakeexample.org/example_ad_3".to_string(),
                    image_url: "https://ads.fakeexample.org/example_image_3".to_string(),
                    format: "skyscraper".to_string(),
                    block_key: "abc123".into(),
                    alt_text: Some("An ad for a pet duck".to_string()),
                    callbacks: AdCallbacks {
                        click: Url::parse("https://ads.fakeexample.org/click/example_ad_3")
                            .unwrap(),
                        impression: Url::parse(
                            "https://ads.fakeexample.org/impression/example_ad_3",
                        )
                        .unwrap(),
                        report: Some(
                            Url::parse("https://ads.fakeexample.org/report/example_ad_3").unwrap(),
                        ),
                    },
                }],
            )]),
        };

        assert_eq!(parsed, expected);
    }

    #[test]
    fn test_empty_ad_response_serialization() {
        let raw_ad_response = json!({
            "example_placement_1": [],
            "example_placement_2": []
        })
        .to_string();

        let parsed: AdResponse<AdImage> = from_str(&raw_ad_response).unwrap();

        let expected = AdResponse {
            data: HashMap::from([]),
        };

        assert_eq!(parsed, expected);
    }

    #[test]
    fn test_take_first() {
        let mut response = AdResponse {
            data: HashMap::new(),
        };
        response.data.insert(
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
                    image_url: "https://example.com/image1.png".to_string(),
                    url: "https://example.com/ad1".to_string(),
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
                    image_url: "https://example.com/image2.png".to_string(),
                    url: "https://example.com/ad2".to_string(),
                },
            ],
        );
        response.data.insert(
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
                image_url: "https://example.com/image3.png".to_string(),
                url: "https://example.com/ad3".to_string(),
            }],
        );
        response.data.insert("placement_3".to_string(), vec![]);

        let result = response.take_first();

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
    fn test_add_request_hash_to_callbacks() {
        let mut response = AdResponse {
            data: HashMap::from([(
                "placement_1".to_string(),
                vec![AdImage {
                    alt_text: Some("An ad for a puppy".to_string()),
                    block_key: "abc123".into(),
                    callbacks: AdCallbacks {
                        click: Url::parse("https://example.com/click").unwrap(),
                        impression: Url::parse("https://example.com/impression").unwrap(),
                        report: Some(Url::parse("https://example.com/report").unwrap()),
                    },
                    format: "billboard".to_string(),
                    image_url: "https://example.com/image.png".to_string(),
                    url: "https://example.com/ad".to_string(),
                }],
            )]),
        };

        let request_hash = RequestHash::from("abc123def456");
        response.add_request_hash_to_callbacks(&request_hash);
        let callbacks = &response.data.values().next().unwrap()[0].callbacks;

        assert!(callbacks
            .click
            .query()
            .unwrap_or("")
            .contains("request_hash=abc123def456"));
        assert!(callbacks
            .impression
            .query()
            .unwrap_or("")
            .contains("request_hash=abc123def456"));
    }

    #[test]
    fn test_pop_request_hash_from_url() {
        let mut url_with_hash =
            Url::parse("https://example.com/callback?request_hash=abc123def456&other=param")
                .unwrap();
        let extracted = pop_request_hash_from_url(&mut url_with_hash);
        assert_eq!(extracted, Some(RequestHash::from("abc123def456")));
        assert_eq!(url_with_hash.query(), Some("other=param"));

        let mut url_without_hash = Url::parse("https://example.com/callback?other=param").unwrap();
        let extracted_none = pop_request_hash_from_url(&mut url_without_hash);
        assert_eq!(extracted_none, None);
        assert_eq!(url_without_hash.query(), Some("other=param"));

        let mut url_no_query = Url::parse("https://example.com/callback").unwrap();
        let extracted_empty = pop_request_hash_from_url(&mut url_no_query);
        assert_eq!(extracted_empty, None);
        assert_eq!(url_no_query.query(), None);
    }
}
