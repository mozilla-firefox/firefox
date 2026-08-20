/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

//! This crate is a cross-platform client library for Mozilla's Merino service.
//!
//! It provides two clients:
//!
//! - [`CuratedRecommendationsClient`](curated_recommendations::CuratedRecommendationsClient) —
//!   fetches curated content recommendations (articles, stories) from the Merino backend,
//!   powering features like Firefox's New Tab page.
//!
//! - [`SuggestClient`](suggest::SuggestClient) —
//!   fetches search suggestions from the Merino suggest endpoint,
//!   powering features like Firefox's address bar suggestions.
//!
//! This crate uses [UniFFI](https://mozilla.github.io/uniffi-rs/) to generate cross-platform
//! bindings for Android and other targets.

pub mod curated_recommendations;
pub mod suggest;
pub use curated_recommendations::{CuratedRecommendationLocale, CuratedRecommendationsApiError};
uniffi::setup_scaffolding!("merino");
