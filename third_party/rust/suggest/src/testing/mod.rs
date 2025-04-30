/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

mod client;
mod data;

pub use client::{MockAttachment, MockIcon, MockRecord, MockRemoteSettingsClient};
pub use data::*;

use crate::Suggestion;
use parking_lot::Once;
use serde_json::Value as JsonValue;

pub use serde_json::json;
pub use sql_support::ConnExt;

use std::{borrow::Borrow, hash::Hash};

/// Trait with utility functions for JSON handling in the tests
pub trait JsonExt {
    fn merge(self, other: JsonValue) -> JsonValue;

    fn remove<Q>(self, key: &Q) -> JsonValue
    where
        String: Borrow<Q>,
        Q: ?Sized + Ord + Eq + Hash;
}

impl JsonExt for JsonValue {
    fn merge(mut self, mut other: JsonValue) -> JsonValue {
        let JsonValue::Object(self_map) = &mut self else {
            panic!("merge called on non-object `self`: {self:?}");
        };
        let JsonValue::Object(other_map) = &mut other else {
            panic!("merge called on non-object `other`: {other:?}");
        };
        self_map.append(other_map);
        self
    }

    fn remove<Q>(mut self, key: &Q) -> JsonValue
    where
        String: Borrow<Q>,
        Q: ?Sized + Ord + Eq + Hash,
    {
        let JsonValue::Object(obj) = &mut self else {
            panic!("remove called on non-object: {self:?}");
        };
        obj.remove(key);
        self
    }
}

/// Extra methods for testing
impl Suggestion {
    pub fn with_score(mut self, score: f64) -> Self {
        let current_score = match &mut self {
            Self::Amp { score, .. } => score,
            Self::Pocket { score, .. } => score,
            Self::Amo { score, .. } => score,
            Self::Yelp { score, .. } => score,
            Self::Mdn { score, .. } => score,
            Self::Weather { score, .. } => score,
            Self::Wikipedia { .. } => panic!("with_score not valid for wikipedia suggestions"),
            Self::Fakespot { score, .. } => score,
            Self::Dynamic { score, .. } => score,
        };
        *current_score = score;
        self
    }

    pub fn has_location_sign(self, has_location_sign: bool) -> Self {
        match self {
            Self::Yelp {
                title,
                url,
                icon,
                icon_mimetype,
                score,
                subject_exact_match,
                location_param,
                ..
            } => Self::Yelp {
                title,
                url,
                icon,
                icon_mimetype,
                score,
                subject_exact_match,
                location_param,
                has_location_sign,
            },
            _ => panic!("has_location_sign only valid for yelp suggestions"),
        }
    }

    pub fn subject_exact_match(self, subject_exact_match: bool) -> Self {
        match self {
            Self::Yelp {
                title,
                url,
                icon,
                icon_mimetype,
                score,
                has_location_sign,
                location_param,
                ..
            } => Self::Yelp {
                title,
                url,
                icon,
                icon_mimetype,
                score,
                subject_exact_match,
                location_param,
                has_location_sign,
            },
            _ => panic!("has_location_sign only valid for yelp suggestions"),
        }
    }
}

pub fn before_each() {
    static ONCE: Once = Once::new();
    ONCE.call_once(|| {
        env_logger::Builder::from_env(env_logger::Env::default().default_filter_or("trace"))
            .is_test(true)
            .init();
    });
}
