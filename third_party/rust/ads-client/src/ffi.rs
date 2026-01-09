/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

pub mod telemetry;

use std::sync::Arc;

use crate::client::ad_request::{AdContentCategory, AdPlacementRequest, IABContentTaxonomy};
use crate::client::ad_response::{
    AdCallbacks, AdImage, AdSpoc, AdTile, SpocFrequencyCaps, SpocRanking,
};
use crate::client::config::{AdsCacheConfig, AdsClientConfig, Environment};
use crate::error::ComponentError;
use crate::ffi::telemetry::MozAdsTelemetryWrapper;
use crate::http_cache::{CacheMode, RequestCachePolicy};
use error_support::{ErrorHandling, GetErrorHandling};
use url::Url;

pub type AdsClientApiResult<T> = std::result::Result<T, MozAdsClientApiError>;

pub use telemetry::MozAdsTelemetry;

#[derive(Debug, thiserror::Error, uniffi::Error)]
pub enum MozAdsClientApiError {
    #[error("Something unexpected occurred.")]
    Other { reason: String },
}

impl From<context_id::ApiError> for MozAdsClientApiError {
    fn from(err: context_id::ApiError) -> Self {
        MozAdsClientApiError::Other {
            reason: err.to_string(),
        }
    }
}

impl GetErrorHandling for ComponentError {
    type ExternalError = MozAdsClientApiError;

    fn get_error_handling(&self) -> ErrorHandling<Self::ExternalError> {
        ErrorHandling::convert(MozAdsClientApiError::Other {
            reason: self.to_string(),
        })
    }
}

#[derive(uniffi::Record)]
pub struct MozAdsRequestOptions {
    pub cache_policy: Option<MozAdsRequestCachePolicy>,
}

impl Default for MozAdsRequestOptions {
    fn default() -> Self {
        Self {
            cache_policy: Some(MozAdsRequestCachePolicy {
                mode: MozAdsCacheMode::default(),
                ttl_seconds: None,
            }),
        }
    }
}

#[derive(Clone, Debug, PartialEq, uniffi::Record)]
pub struct MozAdsIABContent {
    pub taxonomy: MozAdsIABContentTaxonomy,
    pub category_ids: Vec<String>,
}

#[derive(Clone, Debug, PartialEq, uniffi::Record)]
pub struct MozAdsPlacementRequest {
    pub placement_id: String,
    pub iab_content: Option<MozAdsIABContent>,
}

#[derive(Clone, Debug, PartialEq, uniffi::Record)]
pub struct MozAdsPlacementRequestWithCount {
    pub count: u32,
    pub placement_id: String,
    pub iab_content: Option<MozAdsIABContent>,
}

#[derive(Debug, PartialEq, uniffi::Record)]
pub struct MozAdsCallbacks {
    pub click: Url,
    pub impression: Url,
    pub report: Option<Url>,
}

#[derive(Default, uniffi::Record)]
pub struct MozAdsClientConfig {
    pub environment: MozAdsEnvironment,
    pub cache_config: Option<MozAdsCacheConfig>,
    pub telemetry: Option<Arc<dyn MozAdsTelemetry>>,
}

impl From<MozAdsClientConfig> for AdsClientConfig<MozAdsTelemetryWrapper> {
    fn from(config: MozAdsClientConfig) -> Self {
        let telemetry = config
            .telemetry
            .map(MozAdsTelemetryWrapper::new)
            .unwrap_or_else(MozAdsTelemetryWrapper::noop);
        Self {
            environment: config.environment.into(),
            cache_config: config.cache_config.map(Into::into),
            telemetry,
        }
    }
}

#[derive(Clone, Copy, Debug, Default, uniffi::Enum, Eq, PartialEq)]
pub enum MozAdsEnvironment {
    #[default]
    Prod,
    #[cfg(feature = "dev")]
    Staging,
    #[cfg(test)]
    Test,
}

#[derive(uniffi::Record)]
pub struct MozAdsCacheConfig {
    pub db_path: String,
    pub default_cache_ttl_seconds: Option<u64>,
    pub max_size_mib: Option<u64>,
}

#[derive(Debug, PartialEq, uniffi::Record)]
pub struct MozAdsContentCategory {
    pub taxonomy: MozAdsIABContentTaxonomy,
    pub categories: Vec<String>,
}

#[derive(Clone, Copy, Debug, uniffi::Enum, PartialEq)]
pub enum MozAdsIABContentTaxonomy {
    IAB1_0,
    IAB2_0,
    IAB2_1,
    IAB2_2,
    IAB3_0,
}

#[derive(Clone, Copy, Debug, Default, uniffi::Record)]
pub struct MozAdsRequestCachePolicy {
    pub mode: MozAdsCacheMode,
    pub ttl_seconds: Option<u64>,
}

#[derive(Clone, Copy, Debug, Default, uniffi::Enum)]
pub enum MozAdsCacheMode {
    #[default]
    CacheFirst,
    NetworkFirst,
}

#[derive(Debug, PartialEq, uniffi::Record)]
pub struct MozAdsImage {
    pub alt_text: Option<String>,
    pub block_key: String,
    pub callbacks: MozAdsCallbacks,
    pub format: String,
    pub image_url: String,
    pub url: String,
}

#[derive(Debug, PartialEq, uniffi::Record)]
pub struct MozAdsSpoc {
    pub block_key: String,
    pub callbacks: MozAdsCallbacks,
    pub caps: MozAdsSpocFrequencyCaps,
    pub domain: String,
    pub excerpt: String,
    pub format: String,
    pub image_url: String,
    pub ranking: MozAdsSpocRanking,
    pub sponsor: String,
    pub sponsored_by_override: Option<String>,
    pub title: String,
    pub url: String,
}

#[derive(Debug, PartialEq, uniffi::Record)]
pub struct MozAdsSpocFrequencyCaps {
    pub cap_key: String,
    pub day: u32,
}

#[derive(Debug, PartialEq, uniffi::Record)]
pub struct MozAdsSpocRanking {
    pub priority: u32,
    pub personalization_models: std::collections::HashMap<String, u32>,
    pub item_score: f64,
}

#[derive(Debug, PartialEq, uniffi::Record)]
pub struct MozAdsTile {
    pub block_key: String,
    pub callbacks: MozAdsCallbacks,
    pub format: String,
    pub image_url: String,
    pub name: String,
    pub url: String,
}

impl From<AdCallbacks> for MozAdsCallbacks {
    fn from(callbacks: AdCallbacks) -> Self {
        Self {
            click: callbacks.click,
            impression: callbacks.impression,
            report: callbacks.report,
        }
    }
}

impl From<MozAdsCallbacks> for AdCallbacks {
    fn from(callbacks: MozAdsCallbacks) -> Self {
        Self {
            click: callbacks.click,
            impression: callbacks.impression,
            report: callbacks.report,
        }
    }
}

impl From<SpocFrequencyCaps> for MozAdsSpocFrequencyCaps {
    fn from(caps: SpocFrequencyCaps) -> Self {
        Self {
            cap_key: caps.cap_key,
            day: caps.day,
        }
    }
}

impl From<SpocRanking> for MozAdsSpocRanking {
    fn from(ranking: SpocRanking) -> Self {
        Self {
            priority: ranking.priority,
            personalization_models: ranking.personalization_models.unwrap_or_default(),
            item_score: ranking.item_score,
        }
    }
}

impl From<AdImage> for MozAdsImage {
    fn from(img: AdImage) -> Self {
        Self {
            alt_text: img.alt_text,
            block_key: img.block_key,
            callbacks: img.callbacks.into(),
            format: img.format,
            image_url: img.image_url,
            url: img.url,
        }
    }
}

impl From<AdSpoc> for MozAdsSpoc {
    fn from(spoc: AdSpoc) -> Self {
        Self {
            block_key: spoc.block_key,
            callbacks: spoc.callbacks.into(),
            caps: spoc.caps.into(),
            domain: spoc.domain,
            excerpt: spoc.excerpt,
            format: spoc.format,
            image_url: spoc.image_url,
            ranking: spoc.ranking.into(),
            sponsor: spoc.sponsor,
            sponsored_by_override: spoc.sponsored_by_override,
            title: spoc.title,
            url: spoc.url,
        }
    }
}

impl From<AdTile> for MozAdsTile {
    fn from(tile: AdTile) -> Self {
        Self {
            block_key: tile.block_key,
            callbacks: tile.callbacks.into(),
            format: tile.format,
            image_url: tile.image_url,
            name: tile.name,
            url: tile.url,
        }
    }
}

impl From<Environment> for MozAdsEnvironment {
    fn from(env: Environment) -> Self {
        match env {
            Environment::Prod => MozAdsEnvironment::Prod,
            #[cfg(feature = "dev")]
            Environment::Staging => MozAdsEnvironment::Staging,
            #[cfg(test)]
            Environment::Test => MozAdsEnvironment::Test,
        }
    }
}

impl From<MozAdsEnvironment> for Environment {
    fn from(env: MozAdsEnvironment) -> Self {
        match env {
            MozAdsEnvironment::Prod => Environment::Prod,
            #[cfg(feature = "dev")]
            MozAdsEnvironment::Staging => Environment::Staging,
            #[cfg(test)]
            MozAdsEnvironment::Test => Environment::Test,
        }
    }
}

impl From<IABContentTaxonomy> for MozAdsIABContentTaxonomy {
    fn from(taxonomy: IABContentTaxonomy) -> Self {
        match taxonomy {
            IABContentTaxonomy::IAB1_0 => MozAdsIABContentTaxonomy::IAB1_0,
            IABContentTaxonomy::IAB2_0 => MozAdsIABContentTaxonomy::IAB2_0,
            IABContentTaxonomy::IAB2_1 => MozAdsIABContentTaxonomy::IAB2_1,
            IABContentTaxonomy::IAB2_2 => MozAdsIABContentTaxonomy::IAB2_2,
            IABContentTaxonomy::IAB3_0 => MozAdsIABContentTaxonomy::IAB3_0,
        }
    }
}

impl From<MozAdsIABContentTaxonomy> for IABContentTaxonomy {
    fn from(taxonomy: MozAdsIABContentTaxonomy) -> Self {
        match taxonomy {
            MozAdsIABContentTaxonomy::IAB1_0 => IABContentTaxonomy::IAB1_0,
            MozAdsIABContentTaxonomy::IAB2_0 => IABContentTaxonomy::IAB2_0,
            MozAdsIABContentTaxonomy::IAB2_1 => IABContentTaxonomy::IAB2_1,
            MozAdsIABContentTaxonomy::IAB2_2 => IABContentTaxonomy::IAB2_2,
            MozAdsIABContentTaxonomy::IAB3_0 => IABContentTaxonomy::IAB3_0,
        }
    }
}

impl From<MozAdsRequestCachePolicy> for RequestCachePolicy {
    fn from(policy: MozAdsRequestCachePolicy) -> Self {
        Self {
            mode: policy.mode.into(),
            ttl_seconds: policy.ttl_seconds,
        }
    }
}

impl From<CacheMode> for MozAdsCacheMode {
    fn from(mode: CacheMode) -> Self {
        match mode {
            CacheMode::CacheFirst => MozAdsCacheMode::CacheFirst,
            CacheMode::NetworkFirst => MozAdsCacheMode::NetworkFirst,
        }
    }
}

impl From<MozAdsCacheMode> for CacheMode {
    fn from(mode: MozAdsCacheMode) -> Self {
        match mode {
            MozAdsCacheMode::CacheFirst => CacheMode::CacheFirst,
            MozAdsCacheMode::NetworkFirst => CacheMode::NetworkFirst,
        }
    }
}

impl From<&MozAdsIABContent> for AdContentCategory {
    fn from(content: &MozAdsIABContent) -> Self {
        Self {
            taxonomy: content.taxonomy.into(),
            categories: content.category_ids.clone(),
        }
    }
}

impl From<MozAdsRequestOptions> for RequestCachePolicy {
    fn from(options: MozAdsRequestOptions) -> Self {
        options.cache_policy.map(Into::into).unwrap_or_default()
    }
}

impl From<Option<MozAdsRequestOptions>> for RequestCachePolicy {
    fn from(options: Option<MozAdsRequestOptions>) -> Self {
        options.map(Into::into).unwrap_or_default()
    }
}

impl From<MozAdsCacheConfig> for AdsCacheConfig {
    fn from(config: MozAdsCacheConfig) -> Self {
        Self {
            db_path: config.db_path,
            default_cache_ttl_seconds: config.default_cache_ttl_seconds,
            max_size_mib: config.max_size_mib,
        }
    }
}

impl From<&MozAdsPlacementRequest> for AdPlacementRequest {
    fn from(request: &MozAdsPlacementRequest) -> Self {
        Self {
            placement: request.placement_id.clone(),
            count: 1,
            content: request.iab_content.as_ref().map(Into::into),
        }
    }
}

impl From<&MozAdsPlacementRequestWithCount> for AdPlacementRequest {
    fn from(request: &MozAdsPlacementRequestWithCount) -> Self {
        Self {
            placement: request.placement_id.clone(),
            count: request.count,
            content: request.iab_content.as_ref().map(Into::into),
        }
    }
}
