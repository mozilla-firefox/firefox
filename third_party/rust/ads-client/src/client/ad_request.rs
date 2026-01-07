/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

use std::collections::HashSet;

use serde::{Deserialize, Serialize};

use crate::error::BuildRequestError;

#[derive(Debug, PartialEq, Serialize)]
pub struct AdRequest {
    pub context_id: String,
    pub placements: Vec<AdPlacementRequest>,
}

impl AdRequest {
    pub fn build(
        context_id: String,
        placements: Vec<AdPlacementRequest>,
    ) -> Result<Self, BuildRequestError> {
        if placements.is_empty() {
            return Err(BuildRequestError::EmptyConfig);
        };

        let mut request = AdRequest {
            placements: vec![],
            context_id,
        };

        let mut used_placement_ids: HashSet<String> = HashSet::new();

        for ad_placement_request in placements {
            if used_placement_ids.contains(&ad_placement_request.placement) {
                return Err(BuildRequestError::DuplicatePlacementId {
                    placement_id: ad_placement_request.placement.clone(),
                });
            }

            request.placements.push(AdPlacementRequest {
                placement: ad_placement_request.placement.clone(),
                count: ad_placement_request.count,
                content: ad_placement_request
                    .content
                    .map(|iab_content| AdContentCategory {
                        categories: iab_content.categories,
                        taxonomy: iab_content.taxonomy,
                    }),
            });

            used_placement_ids.insert(ad_placement_request.placement.clone());
        }

        Ok(request)
    }
}

#[derive(Debug, PartialEq, Serialize)]
pub struct AdPlacementRequest {
    pub placement: String,
    pub count: u32,
    pub content: Option<AdContentCategory>,
}

#[derive(Debug, Deserialize, PartialEq, Serialize)]
pub struct AdContentCategory {
    pub taxonomy: IABContentTaxonomy,
    pub categories: Vec<String>,
}

#[derive(Clone, Copy, Debug, Deserialize, PartialEq, Serialize)]
pub enum IABContentTaxonomy {
    #[serde(rename = "IAB-1.0")]
    IAB1_0,

    #[serde(rename = "IAB-2.0")]
    IAB2_0,

    #[serde(rename = "IAB-2.1")]
    IAB2_1,

    #[serde(rename = "IAB-2.2")]
    IAB2_2,

    #[serde(rename = "IAB-3.0")]
    IAB3_0,
}

#[cfg(test)]
mod tests {
    use crate::test_utils::TEST_CONTEXT_ID;

    use super::*;
    use serde_json::{json, to_value};

    #[test]
    fn test_ad_placement_request_with_content_serialize() {
        let request = AdPlacementRequest {
            placement: "example_placement".into(),
            count: 1,
            content: Some(AdContentCategory {
                taxonomy: IABContentTaxonomy::IAB2_1,
                categories: vec!["Technology".into(), "Programming".into()],
            }),
        };

        let serialized = to_value(&request).unwrap();

        let expected_json = json!({
            "placement": "example_placement",
            "count": 1,
            "content": {
                "taxonomy": "IAB-2.1",
                "categories": ["Technology", "Programming"]
            }
        });

        assert_eq!(serialized, expected_json);
    }

    #[test]
    fn test_iab_content_taxonomy_serialize() {
        use serde_json::to_string;

        // We expect that enums map to strings like "IAB-2.2"
        let s = to_string(&IABContentTaxonomy::IAB1_0).unwrap();
        assert_eq!(s, "\"IAB-1.0\"");

        let s = to_string(&IABContentTaxonomy::IAB2_0).unwrap();
        assert_eq!(s, "\"IAB-2.0\"");

        let s = to_string(&IABContentTaxonomy::IAB2_1).unwrap();
        assert_eq!(s, "\"IAB-2.1\"");

        let s = to_string(&IABContentTaxonomy::IAB2_2).unwrap();
        assert_eq!(s, "\"IAB-2.2\"");

        let s = to_string(&IABContentTaxonomy::IAB3_0).unwrap();
        assert_eq!(s, "\"IAB-3.0\"");
    }

    #[test]
    fn test_build_ad_request_happy() {
        let request = AdRequest::build(
            TEST_CONTEXT_ID.to_string(),
            vec![
                AdPlacementRequest {
                    placement: "example_placement_1".to_string(),
                    count: 1,
                    content: Some(AdContentCategory {
                        taxonomy: IABContentTaxonomy::IAB2_1,
                        categories: vec!["entertainment".to_string()],
                    }),
                },
                AdPlacementRequest {
                    placement: "example_placement_2".to_string(),
                    count: 2,
                    content: Some(AdContentCategory {
                        taxonomy: IABContentTaxonomy::IAB2_1,
                        categories: vec![],
                    }),
                },
            ],
        )
        .unwrap();

        let expected_request = AdRequest {
            context_id: TEST_CONTEXT_ID.to_string(),
            placements: vec![
                AdPlacementRequest {
                    placement: "example_placement_1".to_string(),
                    count: 1,
                    content: Some(AdContentCategory {
                        taxonomy: IABContentTaxonomy::IAB2_1,
                        categories: vec!["entertainment".to_string()],
                    }),
                },
                AdPlacementRequest {
                    placement: "example_placement_2".to_string(),
                    count: 2,
                    content: Some(AdContentCategory {
                        taxonomy: IABContentTaxonomy::IAB2_1,
                        categories: vec![],
                    }),
                },
            ],
        };

        assert_eq!(request, expected_request);
    }

    #[test]
    fn test_build_ad_request_fails_on_duplicate_placement_id() {
        let request = AdRequest::build(
            TEST_CONTEXT_ID.to_string(),
            vec![
                AdPlacementRequest {
                    placement: "example_placement_1".to_string(),
                    count: 1,
                    content: Some(AdContentCategory {
                        taxonomy: IABContentTaxonomy::IAB2_1,
                        categories: vec!["entertainment".to_string()],
                    }),
                },
                AdPlacementRequest {
                    placement: "example_placement_1".to_string(),
                    count: 1,
                    content: Some(AdContentCategory {
                        taxonomy: IABContentTaxonomy::IAB3_0,
                        categories: vec![],
                    }),
                },
            ],
        );
        assert!(request.is_err());
    }

    #[test]
    fn test_build_ad_request_fails_on_empty_request() {
        let request = AdRequest::build(TEST_CONTEXT_ID.to_string(), vec![]);
        assert!(request.is_err());
    }
}
