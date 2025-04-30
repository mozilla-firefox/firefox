/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! Test data that we use in many tests

use crate::{suggestion::FtsMatchInfo, testing::MockIcon, Suggestion};
use serde_json::json;
use serde_json::Value as JsonValue;

pub fn los_pollos_amp() -> JsonValue {
    json!({
        "id": 100,
        "advertiser": "Los Pollos Hermanos",
        "iab_category": "8 - Food & Drink",
        "keywords": ["lo", "los", "los p", "los pollos", "los pollos h", "los pollos hermanos"],
        "full_keywords": [("los pollos", 4), ("los pollos hermanos", 2)],
        "title": "Los Pollos Hermanos - Albuquerque",
        "url": "https://www.lph-nm.biz",
        "icon": "los-pollos-favicon",
        "impression_url": "https://example.com/impression_url",
        "click_url": "https://example.com/click_url",
        "score": 0.3
    })
}

pub fn los_pollos_icon() -> MockIcon {
    MockIcon {
        id: "los-pollos-favicon",
        data: "los-pollos-icon-data",
        mimetype: "image/png",
    }
}

pub fn los_pollos_suggestion(
    full_keyword: &str,
    fts_match_info: Option<FtsMatchInfo>,
) -> Suggestion {
    Suggestion::Amp {
        title: "Los Pollos Hermanos - Albuquerque".into(),
        url: "https://www.lph-nm.biz".into(),
        raw_url: "https://www.lph-nm.biz".into(),
        icon: Some("los-pollos-icon-data".as_bytes().to_vec()),
        icon_mimetype: Some("image/png".into()),
        block_id: 100,
        advertiser: "Los Pollos Hermanos".into(),
        iab_category: "8 - Food & Drink".into(),
        impression_url: "https://example.com/impression_url".into(),
        click_url: "https://example.com/click_url".into(),
        raw_click_url: "https://example.com/click_url".into(),
        score: 0.3,
        full_keyword: full_keyword.to_string(),
        fts_match_info,
    }
}

pub fn good_place_eats_amp() -> JsonValue {
    json!({
        "id": 101,
        "advertiser": "Good Place Eats",
        "iab_category": "8 - Food & Drink",
        "keywords": ["la", "las", "lasa", "lasagna", "lasagna come out tomorrow"],
        "full_keywords": [("lasagna", 2), ("lasagna come out tomorrow", 2)],
        "title": "Lasagna Come Out Tomorrow",
        "url": "https://www.lasagna.restaurant",
        "icon": "good-place-eats-favicon",
        "impression_url": "https://example.com/impression_url",
        "click_url": "https://example.com/click_url"
    })
}

pub fn good_place_eats_icon() -> MockIcon {
    MockIcon {
        id: "good-place-eats-favicon",
        data: "good-place-eats-icon-data",
        mimetype: "image/gif",
    }
}

pub fn good_place_eats_suggestion(
    full_keyword: &str,
    fts_match_info: Option<FtsMatchInfo>,
) -> Suggestion {
    Suggestion::Amp {
        title: "Lasagna Come Out Tomorrow".into(),
        url: "https://www.lasagna.restaurant".into(),
        raw_url: "https://www.lasagna.restaurant".into(),
        icon: Some("good-place-eats-icon-data".as_bytes().to_vec()),
        icon_mimetype: Some("image/gif".into()),
        full_keyword: full_keyword.into(),
        block_id: 101,
        advertiser: "Good Place Eats".into(),
        iab_category: "8 - Food & Drink".into(),
        impression_url: "https://example.com/impression_url".into(),
        click_url: "https://example.com/click_url".into(),
        raw_click_url: "https://example.com/click_url".into(),
        score: 0.2,
        fts_match_info,
    }
}

pub fn california_wiki() -> JsonValue {
    json!({
        "id": 200,
        "advertiser": "Wikipedia",
        "iab_category": "5 - Education",
        "keywords": ["cal", "cali", "california"],
        "title": "California",
        "url": "https://wikipedia.org/California",
        "icon": "california-favicon",
    })
}

pub fn california_icon() -> MockIcon {
    MockIcon {
        id: "california-favicon",
        data: "california-icon-data",
        mimetype: "image/png",
    }
}

pub fn california_suggestion(full_keyword: &str) -> Suggestion {
    Suggestion::Wikipedia {
        title: "California".into(),
        url: "https://wikipedia.org/California".into(),
        icon: Some("california-icon-data".as_bytes().to_vec()),
        icon_mimetype: Some("image/png".into()),
        full_keyword: full_keyword.into(),
    }
}

pub fn caltech_wiki() -> JsonValue {
    json!({
        "id": 2001,
        "advertiser": "Wikipedia",
        "iab_category": "5 - Education",
        "keywords": ["cal", "cali", "california", "institute", "technology"],
        "title": "California Institute of Technology",
        "url": "https://wikipedia.org/California_Institute_of_Technology",
        "icon": "caltech-favicon"
    })
}

pub fn caltech_icon() -> MockIcon {
    MockIcon {
        id: "caltech-favicon",
        data: "caltech-icon-data",
        mimetype: "image/png",
    }
}

pub fn caltech_suggestion(full_keyword: &str) -> Suggestion {
    Suggestion::Wikipedia {
        title: "California Institute of Technology".into(),
        url: "https://wikipedia.org/California_Institute_of_Technology".into(),
        icon: Some("caltech-icon-data".as_bytes().to_vec()),
        icon_mimetype: Some("image/png".into()),
        full_keyword: full_keyword.into(),
    }
}

pub fn relay_amo() -> JsonValue {
    json!({
        "title": "Firefox Relay",
        "description": "Firefox Relay -- Email masking",
        "url": "https://example.org/amo-suggestion-1",
        "guid": "{b9db16a4-6edc-47ec-a1f4-b86292ed211d}",
        "keywords": ["relay", "spam", "masking email", "alias"],
        "icon": "https://example.org/amo-suggestion-1/icon.png",
        "rating": "4.9",
        "number_of_ratings": 800,
        "score": 0.25
    })
}

pub fn dark_mode_amo() -> JsonValue {
    json!({
        "title": "Dark Mode",
        "description": "Ease your eyes",
        "url": "https://example.org/amo-suggestion-2",
        "guid": "{6d24e3b8-1400-4d37-9440-c798f9b79b1a}",
        "keywords": ["dark mode", "dark theme", "night mode"],
        "icon": "https://example.org/amo-suggestion-2/icon.png",
        "rating": "4.6",
        "number_of_ratings": 750,
        "score": 0.25
    })
}

pub fn foxy_guestures_amo() -> JsonValue {
    json!({
        "title": "Foxy Guestures",
        "description": "Mouse gestures for Firefox",
        "url": "https://example.org/amo-suggestion-3",
        "guid": "{1e9d493b-0498-48bb-9b9a-8b45a44df146}",
        "keywords": ["grammar", "spelling", "edit"],
        "icon": "https://example.org/amo-suggestion-3/icon.png",
        "rating": "4.8",
        "number_of_ratings": 900,
        "score": 0.25
    })
}

pub fn new_tab_override_amo() -> JsonValue {
    json!({
        "title": "New Tab Override",
        "description": "New Tab Override allows you to set the page that shows whenever you open a new tab",
        "url": "https://example.org/amo-suggestion-4",
        "guid": "{1ea82ebd-a1ba-4f57-b8bb-3824ead837bd}",
        "keywords": ["image search", "visual search"],
        "icon": "https://example.org/amo-suggestion-4/icon.png",
        "rating": "5.0",
        "number_of_ratings": 100,
        "score": 0.25
    })
}

pub fn relay_suggestion() -> Suggestion {
    Suggestion::Amo {
        title: "Firefox Relay".into(),
        url: "https://example.org/amo-suggestion-1".into(),
        icon_url: "https://example.org/amo-suggestion-1/icon.png".into(),
        description: "Firefox Relay -- Email masking".into(),
        rating: Some("4.9".into()),
        number_of_ratings: 800,
        guid: "{b9db16a4-6edc-47ec-a1f4-b86292ed211d}".into(),
        score: 0.25,
    }
}

pub fn dark_mode_suggestion() -> Suggestion {
    Suggestion::Amo {
        title: "Dark Mode".into(),
        url: "https://example.org/amo-suggestion-2".into(),
        icon_url: "https://example.org/amo-suggestion-2/icon.png".into(),
        description: "Ease your eyes".into(),
        rating: Some("4.6".into()),
        number_of_ratings: 750,
        guid: "{6d24e3b8-1400-4d37-9440-c798f9b79b1a}".into(),
        score: 0.25,
    }
}

pub fn foxy_guestures_suggestion() -> Suggestion {
    Suggestion::Amo {
        title: "Foxy Guestures".into(),
        url: "https://example.org/amo-suggestion-3".into(),
        icon_url: "https://example.org/amo-suggestion-3/icon.png".into(),
        description: "Mouse gestures for Firefox".into(),
        rating: Some("4.8".into()),
        number_of_ratings: 900,
        guid: "{1e9d493b-0498-48bb-9b9a-8b45a44df146}".into(),
        score: 0.25,
    }
}

pub fn new_tab_override_suggestion() -> Suggestion {
    Suggestion::Amo {
        title: "New Tab Override".into(),
        url: "https://example.org/amo-suggestion-4".into(),
        icon_url: "https://example.org/amo-suggestion-4/icon.png".into(),
        description:
            "New Tab Override allows you to set the page that shows whenever you open a new tab"
                .into(),
        rating: Some("5.0".into()),
        number_of_ratings: 100,
        guid: "{1ea82ebd-a1ba-4f57-b8bb-3824ead837bd}".into(),
        score: 0.25,
    }
}

pub fn burnout_pocket() -> JsonValue {
    json!({
        "description": "pocket suggestion",
        "url": "https://getpocket.com/collections/its-not-just-burnout-how-grind-culture-failed-women",
        "lowConfidenceKeywords": ["soft life", "workaholism", "toxic work culture", "work-life balance"],
        "highConfidenceKeywords": ["burnout women", "grind culture", "women burnout"],
        "title": "‘It’s Not Just Burnout:’ How Grind Culture Fails Women",
        "score": 0.25
    })
}

pub fn burnout_suggestion(is_top_pick: bool) -> Suggestion {
    Suggestion::Pocket {
        title: "‘It’s Not Just Burnout:’ How Grind Culture Fails Women".into(),
        url:
            "https://getpocket.com/collections/its-not-just-burnout-how-grind-culture-failed-women"
                .into(),
        score: 0.25,
        is_top_pick,
    }
}

pub fn ramen_yelp() -> JsonValue {
    json!({
        "subjects": ["ramen", "spicy ramen", "spicy random ramen", "rats", "raven", "raccoon", "012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789", "012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789Z"],
        "preModifiers": ["best", "super best", "same_modifier"],
        "postModifiers": ["delivery", "super delivery", "same_modifier"],
        "locationSigns": [
            // V1 format also can be used as location sign.
            { "keyword": "in", "needLocation": true },
            { "keyword": "near by", "needLocation": false },
            // V2 format.
            "near",
            "near me",
        ],
        "yelpModifiers": ["yelp", "yelp keyword"],
        "icon": "yelp-favicon",
        "score": 0.5
    })
}

pub fn ramen_suggestion(title: &str, url: &str) -> Suggestion {
    Suggestion::Yelp {
        title: title.into(),
        url: url.into(),
        icon: Some("yelp-favicon-data".into()),
        icon_mimetype: Some("image/svg+xml".into()),
        score: 0.5,
        has_location_sign: true,
        subject_exact_match: true,
        location_param: "find_loc".into(),
    }
}

pub fn yelp_favicon() -> MockIcon {
    MockIcon {
        id: "yelp-favicon",
        data: "yelp-favicon-data",
        mimetype: "image/svg+xml",
    }
}

pub fn array_mdn() -> JsonValue {
    json!({
        "description": "Javascript Array",
        "url": "https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Array",
        "keywords": ["array javascript", "javascript array", "wildcard"],
        "title": "Array",
        "score": 0.24
    })
}

pub fn array_suggestion() -> Suggestion {
    Suggestion::Mdn {
        title: "Array".into(),
        url:
            "https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Array"
                .into(),
        description: "Javascript Array".into(),
        score: 0.24,
    }
}

pub fn multimatch_wiki() -> JsonValue {
    json!({
        "id": 0,
        "advertiser": "Wikipedia",
        "iab_category": "5 - Education",
        "keywords": ["multimatch"],
        "title": "Multimatch",
        "url": "https://wikipedia.org/Multimatch",
        "icon": "multimatch-wiki-favicon"
    })
}

pub fn multimatch_amo() -> JsonValue {
    json!({
        "description": "amo suggestion multi-match",
        "url": "https://addons.mozilla.org/en-US/firefox/addon/multimatch",
        "guid": "{b9db16a4-6edc-47ec-a1f4-b86292ed211d}",
        "keywords": ["multimatch"],
        "title": "Firefox Multimatch",
        "icon": "https://addons.mozilla.org/user-media/addon_icons/2633/2633704-64.png?modified=2c11a80b",
        "rating": "4.9",
        "number_of_ratings": 888,
        "score": 0.25
    })
}

pub fn multimatch_pocket() -> JsonValue {
    json!({
        "description": "pocket suggestion multi-match",
        "url": "https://getpocket.com/collections/multimatch",
        "lowConfidenceKeywords": [],
        "highConfidenceKeywords": ["multimatch"],
        "title": "Multimatching",
        "score": 0.88
    })
}

pub fn multimatch_wiki_icon() -> MockIcon {
    MockIcon {
        id: "multimatch-wiki-favicon",
        data: "multimatch-wiki-icon-data",
        mimetype: "image/png",
    }
}

pub fn multimatch_pocket_suggestion(is_top_pick: bool) -> Suggestion {
    Suggestion::Pocket {
        title: "Multimatching".into(),
        url: "https://getpocket.com/collections/multimatch".into(),
        score: 0.88,
        is_top_pick,
    }
}

pub fn multimatch_amo_suggestion() -> Suggestion {
    Suggestion::Amo {
        title: "Firefox Multimatch".into(),
        url: "https://addons.mozilla.org/en-US/firefox/addon/multimatch".into(),
        icon_url: "https://addons.mozilla.org/user-media/addon_icons/2633/2633704-64.png?modified=2c11a80b".into(),
        description: "amo suggestion multi-match".into(),
        rating: Some("4.9".into()),
        number_of_ratings: 888,
        guid: "{b9db16a4-6edc-47ec-a1f4-b86292ed211d}".into(),
        score: 0.25,
    }
}

pub fn multimatch_wiki_suggestion() -> Suggestion {
    Suggestion::Wikipedia {
        title: "Multimatch".into(),
        url: "https://wikipedia.org/Multimatch".into(),
        icon: Some("multimatch-wiki-icon-data".as_bytes().to_vec()),
        icon_mimetype: Some("image/png".into()),
        full_keyword: "multimatch".into(),
    }
}

// Fakespot test data

pub fn snowglobe_fakespot() -> JsonValue {
    json!({
        "fakespot_grade": "B",
        "product_id": "amazon-ABC",
        "keywords": "",
        "product_type": "snow globe",
        "rating": 4.7,
        "score": 0.8,
        "title": "Make Your Own Glitter Snow Globes",
        "total_reviews": 152,
        "url": "http://amazon.com/dp/ABC"
    })
}

pub fn snowglobe_suggestion(match_info: Option<FtsMatchInfo>) -> Suggestion {
    Suggestion::Fakespot {
        fakespot_grade: "B".into(),
        product_id: "amazon-ABC".into(),
        rating: 4.7,
        title: "Make Your Own Glitter Snow Globes".into(),
        total_reviews: 152,
        url: "http://amazon.com/dp/ABC".into(),
        score: 0.3 + 0.00008,
        icon: Some("fakespot-icon-amazon-data".as_bytes().to_vec()),
        icon_mimetype: Some("image/png".into()),
        match_info,
    }
}

pub fn simpsons_fakespot() -> JsonValue {
    json!({
        "fakespot_grade": "A",
        // Use a product ID that doesn't match the ingested icons to test what happens.  In this
        // case, icon and icon_mimetype for the returned Suggestion should both be None.
        "product_id": "vendorwithouticon-XYZ",
        "keywords": "",
        "product_type": "",
        "rating": 4.9,
        "score": 0.9,
        "title": "The Simpsons: Skinner's Sense of Snow (DVD)",
        "total_reviews": 14000,
        "url": "http://vendorwithouticon.com/dp/XYZ"
    })
}

pub fn simpsons_suggestion(match_info: Option<FtsMatchInfo>) -> Suggestion {
    Suggestion::Fakespot {
        fakespot_grade: "A".into(),
        product_id: "vendorwithouticon-XYZ".into(),
        rating: 4.9,
        title: "The Simpsons: Skinner's Sense of Snow (DVD)".into(),
        total_reviews: 14000,
        url: "http://vendorwithouticon.com/dp/XYZ".into(),
        score: 0.3 + 0.00009,
        icon: None,
        icon_mimetype: None,
        match_info,
    }
}

pub fn fakespot_amazon_icon() -> MockIcon {
    MockIcon {
        id: "fakespot-amazon",
        data: "fakespot-icon-amazon-data",
        mimetype: "image/png",
    }
}
