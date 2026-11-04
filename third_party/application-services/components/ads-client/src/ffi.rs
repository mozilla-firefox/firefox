/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

pub mod error;
pub(crate) mod telemetry;
use crate::ads::{
    AdCallbacks, AdImage, AdImageOrSpoc, AdSpoc, AdTile, SpocFrequencyCaps, SpocRanking,
};
#[cfg(feature = "stateful")]
use crate::client::config::AdsStoreConfig;
use crate::client::config::{AdsCacheConfig, AdsClientConfig};
use crate::client::AdsClient;
use crate::http_cache::CachePolicy;
use crate::mars::ad_request::{
    AdContentCategory, AdPlacementRequest, AdRequestFlags, IABContentTaxonomy,
};
use crate::mars::Environment;
use crate::mars::ReportReason;
use crate::AdsClientUrl;
use crate::MozAdsClient;
use parking_lot::Mutex;
use std::collections::HashMap;
use std::sync::Arc;
#[cfg(test)]
use std::sync::Weak;

pub use error::{AdsClientApiResult, MozAdsClientApiError};
pub use telemetry::MozAdsTelemetry;

/// Options for one ad request. Every field has a default.
#[derive(Default, uniffi::Record)]
pub struct MozAdsRequestOptions {
    #[uniffi(default)]
    pub blocks: Vec<String>,
    #[uniffi(default)]
    pub cache_policy: Option<MozAdsCachePolicy>,
    #[uniffi(default)]
    pub flags: HashMap<String, bool>,
    #[uniffi(default = false)]
    pub ohttp: bool,
    #[uniffi(default)]
    pub relevance: HashMap<String, f64>, // tiles only
}

/// Options for a callback request (click, impression or report).
#[derive(Default, uniffi::Record)]
pub struct MozAdsCallbackOptions {
    #[uniffi(default = false)]
    pub ohttp: bool,
}

/// The IAB content classification of a placement, for targeting.
#[derive(Clone, Debug, PartialEq, uniffi::Record)]
pub struct MozAdsIABContent {
    pub category_ids: Vec<String>,
    pub taxonomy: MozAdsIABContentTaxonomy,
}

/// One placement to request a single ad for. `placement_id` must be unique
/// within one request.
#[derive(Clone, Debug, PartialEq, uniffi::Record)]
pub struct MozAdsPlacementRequest {
    #[uniffi(default = None)]
    pub iab_content: Option<MozAdsIABContent>,
    pub placement_id: String,
}

/// One placement to request `count` ads for. `placement_id` must be unique
/// within one request.
#[derive(Clone, Debug, PartialEq, uniffi::Record)]
pub struct MozAdsPlacementRequestWithCount {
    pub count: u32,
    #[uniffi(default = None)]
    pub iab_content: Option<MozAdsIABContent>,
    pub placement_id: String,
}

/// The callback URLs of an ad: pass them to `record_click`,
/// `record_impression` and `report_ad`.
#[derive(Debug, PartialEq, uniffi::Record)]
pub struct MozAdsCallbacks {
    pub click: AdsClientUrl,
    pub impression: AdsClientUrl,
    pub report: Option<AdsClientUrl>,
}

/// Configures and builds a [`MozAdsClient`].
#[derive(uniffi::Object)]
pub struct MozAdsClientBuilder(Mutex<MozAdsClientBuilderInner>);

#[derive(Default)]
struct MozAdsClientBuilderInner {
    cache_config: Option<MozAdsCacheConfig>,
    environment: Option<MozAdsEnvironment>,
    #[cfg(feature = "stateful")]
    store_config: Option<MozAdsStoreConfig>,
    telemetry: Option<Arc<dyn MozAdsTelemetry>>,
}

impl Default for MozAdsClientBuilder {
    fn default() -> Self {
        Self(Mutex::new(MozAdsClientBuilderInner::default()))
    }
}

#[uniffi::export]
impl MozAdsClientBuilder {
    /// A builder with the defaults: the production environment, no cache and
    /// no telemetry.
    #[uniffi::constructor]
    pub fn new() -> Self {
        Self::default()
    }

    /// Builds the client. A telemetry instance set on the builder becomes the
    /// destination of every client's metrics in the process.
    pub fn build(&self) -> MozAdsClient {
        let mut inner = self.0.lock();
        // Taken so the builder does not keep the callback alive.
        if let Some(callback) = inner.telemetry.take() {
            telemetry::install(callback);
        }
        let client_config = AdsClientConfig {
            cache_config: inner.cache_config.clone().map(Into::into),
            environment: inner.environment.clone().unwrap_or_default().into(),
            #[cfg(feature = "stateful")]
            store_config: inner.store_config.clone().map(Into::into),
        };

        let client = AdsClient::new(client_config);
        let inner = Arc::new(client);
        MozAdsClient { inner }
    }

    /// Enables the HTTP cache with this configuration. Without it, nothing is
    /// cached.
    pub fn cache_config(self: Arc<Self>, cache_config: MozAdsCacheConfig) -> Arc<Self> {
        self.0.lock().cache_config = Some(cache_config);
        self
    }

    /// The MARS environment to send requests to. Defaults to production.
    pub fn environment(self: Arc<Self>, environment: MozAdsEnvironment) -> Arc<Self> {
        self.0.lock().environment = Some(environment);
        self
    }

    /// Where the client records its metrics. Telemetry is process-wide: see
    /// [`MozAdsTelemetry`].
    pub fn telemetry(self: Arc<Self>, telemetry: Box<dyn MozAdsTelemetry>) -> Arc<Self> {
        self.0.lock().telemetry = Some(Arc::from(telemetry));
        self
    }
}

#[cfg(feature = "stateful")]
#[uniffi::export]
impl MozAdsClientBuilder {
    /// Where the stateful client keeps its ads database.
    #[cfg(feature = "stateful")]
    pub fn store_config(self: Arc<Self>, store_config: MozAdsStoreConfig) -> Arc<Self> {
        self.0.lock().store_config = Some(store_config);
        self
    }
}

impl MozAdsClientBuilder {
    #[cfg(test)]
    pub fn fetch_telemetry(&self) -> Option<Weak<dyn MozAdsTelemetry>> {
        self.0.lock().telemetry.as_ref().map(Arc::downgrade)
    }
}

/// The MARS environment the client sends requests to. Defaults to `Prod`.
///
/// Variants are deliberately unsorted: UniFFI encodes them by index, and
/// this mirrors `Environment`.
#[derive(Clone, Debug, Default, uniffi::Enum, PartialEq, Eq)]
pub enum MozAdsEnvironment {
    #[default]
    Prod,
    Staging,
    #[cfg(test)]
    Test,
    Custom(AdsClientUrl),
}

/// The on-disk HTTP cache. `default_cache_ttl_seconds` defaults to 300 and
/// `max_size_mib` to 10.
#[derive(Clone, uniffi::Record)]
pub struct MozAdsCacheConfig {
    pub db_path: String,
    #[uniffi(default = None)]
    pub default_cache_ttl_seconds: Option<u64>,
    #[uniffi(default = None)]
    pub max_size_mib: Option<u64>,
}

/// The SQLite database the stateful client keeps its ads in.
#[derive(Clone, uniffi::Record)]
pub struct MozAdsStoreConfig {
    pub db_path: String,
}

/// IAB content categories, in one taxonomy version.
#[derive(Debug, PartialEq, uniffi::Record)]
pub struct MozAdsContentCategory {
    pub categories: Vec<String>,
    pub taxonomy: MozAdsIABContentTaxonomy,
}

/// The version of the IAB Content Taxonomy that category ids refer to.
#[derive(Clone, Copy, Debug, uniffi::Enum, PartialEq)]
pub enum MozAdsIABContentTaxonomy {
    IAB1_0,
    IAB2_0,
    IAB2_1,
    IAB2_2,
    IAB3_0,
}

/// How one request uses the cache. `ttl_seconds` overrides the client's
/// default TTL; `0` does not cache the response.
#[derive(Clone, Copy, Debug, Default, uniffi::Record)]
pub struct MozAdsCachePolicy {
    pub mode: MozAdsCacheMode,
    #[uniffi(default = None)]
    pub ttl_seconds: Option<u64>,
}

/// Why the user reported an ad.
#[derive(Clone, Copy, Debug, uniffi::Enum)]
pub enum MozAdsReportReason {
    Inappropriate,
    NotInterested,
    SeenTooManyTimes,
    Scam,
    Misleading,
    Offensive,
}

impl From<MozAdsReportReason> for ReportReason {
    fn from(reason: MozAdsReportReason) -> Self {
        match reason {
            MozAdsReportReason::Inappropriate => ReportReason::Inappropriate,
            MozAdsReportReason::Misleading => ReportReason::Misleading,
            MozAdsReportReason::NotInterested => ReportReason::NotInterested,
            MozAdsReportReason::Offensive => ReportReason::Offensive,
            MozAdsReportReason::Scam => ReportReason::Scam,
            MozAdsReportReason::SeenTooManyTimes => ReportReason::SeenTooManyTimes,
        }
    }
}

/// Whether a request reads the cache before the network.
#[derive(Clone, Copy, Debug, Default, uniffi::Enum)]
pub enum MozAdsCacheMode {
    #[default]
    CacheFirst,
    NetworkFirst,
}

/// An image ad returned by MARS.
#[derive(Debug, PartialEq, uniffi::Record)]
pub struct MozAdsImage {
    pub alt_text: Option<String>,
    pub block_key: String,
    pub callbacks: MozAdsCallbacks,
    pub format: String,
    pub image_url: AdsClientUrl,
    pub url: AdsClientUrl,
}

/// A sponsored content (spoc) ad returned by MARS.
#[derive(Debug, PartialEq, uniffi::Record)]
pub struct MozAdsSpoc {
    pub block_key: String,
    pub callbacks: MozAdsCallbacks,
    pub caps: MozAdsSpocFrequencyCaps,
    pub domain: String,
    pub excerpt: String,
    pub format: String,
    pub image_url: AdsClientUrl,
    pub ranking: MozAdsSpocRanking,
    pub sponsor: String,
    pub sponsored_by_override: Option<String>,
    pub title: String,
    pub url: AdsClientUrl,
}

/// An ad from a placement that can return either an image or a spoc.
#[derive(Debug, PartialEq, uniffi::Enum)]
pub enum MozAdsImageOrSpoc {
    Image(MozAdsImage),
    Spoc(MozAdsSpoc),
}

/// Frequency capping information for a spoc.
#[derive(Debug, PartialEq, uniffi::Record)]
pub struct MozAdsSpocFrequencyCaps {
    pub cap_key: String,
    pub day: u32,
}

/// Ranking and personalization information for a spoc.
#[derive(Debug, PartialEq, uniffi::Record)]
pub struct MozAdsSpocRanking {
    pub item_score: f64,
    pub personalization_models: HashMap<String, u32>,
    pub priority: u32,
}

/// A tile ad returned by MARS.
#[derive(Debug, PartialEq, uniffi::Record)]
pub struct MozAdsTile {
    pub block_key: String,
    pub callbacks: MozAdsCallbacks,
    pub format: String,
    pub image_url: AdsClientUrl,
    pub name: String,
    pub url: AdsClientUrl,
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
            item_score: ranking.item_score,
            personalization_models: ranking.personalization_models.unwrap_or_default(),
            priority: ranking.priority,
        }
    }
}

impl From<AdImageOrSpoc> for MozAdsImageOrSpoc {
    fn from(ad: AdImageOrSpoc) -> Self {
        match ad {
            AdImageOrSpoc::Image(ad) => MozAdsImageOrSpoc::Image(ad.into()),
            AdImageOrSpoc::Spoc(ad) => MozAdsImageOrSpoc::Spoc(ad.into()),
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
            Environment::Staging => MozAdsEnvironment::Staging,
            #[cfg(test)]
            Environment::Test => MozAdsEnvironment::Test,
            Environment::Custom(url) => MozAdsEnvironment::Custom(url),
        }
    }
}

impl From<MozAdsEnvironment> for Environment {
    fn from(env: MozAdsEnvironment) -> Self {
        match env {
            MozAdsEnvironment::Prod => Environment::Prod,
            MozAdsEnvironment::Staging => Environment::Staging,
            #[cfg(test)]
            MozAdsEnvironment::Test => Environment::Test,
            MozAdsEnvironment::Custom(url) => Environment::Custom(url),
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

impl From<MozAdsCachePolicy> for CachePolicy {
    fn from(policy: MozAdsCachePolicy) -> Self {
        let ttl = policy.ttl_seconds.map(std::time::Duration::from_secs);
        match policy.mode {
            MozAdsCacheMode::CacheFirst => CachePolicy::CacheFirst { ttl },
            MozAdsCacheMode::NetworkFirst => CachePolicy::NetworkFirst { ttl },
        }
    }
}

impl From<&MozAdsIABContent> for AdContentCategory {
    fn from(content: &MozAdsIABContent) -> Self {
        Self {
            categories: content.category_ids.clone(),
            taxonomy: content.taxonomy.into(),
        }
    }
}

impl From<&MozAdsRequestOptions> for AdRequestFlags {
    fn from(options: &MozAdsRequestOptions) -> Self {
        options.flags.clone()
    }
}

impl From<MozAdsRequestOptions> for CachePolicy {
    fn from(options: MozAdsRequestOptions) -> Self {
        options.cache_policy.map(Into::into).unwrap_or_default()
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

#[cfg(feature = "stateful")]
impl From<MozAdsStoreConfig> for AdsStoreConfig {
    fn from(config: MozAdsStoreConfig) -> Self {
        Self {
            db_path: config.db_path,
        }
    }
}

impl From<&MozAdsPlacementRequest> for AdPlacementRequest {
    fn from(request: &MozAdsPlacementRequest) -> Self {
        Self {
            content: request.iab_content.as_ref().map(Into::into),
            count: 1,
            placement: request.placement_id.clone(),
        }
    }
}

impl From<&MozAdsPlacementRequestWithCount> for AdPlacementRequest {
    fn from(request: &MozAdsPlacementRequestWithCount) -> Self {
        Self {
            content: request.iab_content.as_ref().map(Into::into),
            count: request.count,
            placement: request.placement_id.clone(),
        }
    }
}

#[cfg(test)]
mod tests {
    use crate::{
        telemetry, test_utils::NoopMozAdsTelemetry, MozAdsCacheConfig, MozAdsClientBuilder,
    };
    use std::sync::Arc;

    #[test]
    fn test_telemetry_not_held_by_builder() {
        let _lock = telemetry::test_lock();
        // Related to Bug 2064543
        // The builder can hold a reference to the passed telemetry, meaning that if the builder still exists, `shutdown` doesn't drop all references.

        // Make a builder and pass in telemetry.
        let builder = Arc::new(MozAdsClientBuilder::new());
        assert!(builder.fetch_telemetry().is_none());
        let builder = MozAdsClientBuilder::telemetry(builder, Box::new(NoopMozAdsTelemetry));
        let weak_telemetry = builder
            .fetch_telemetry()
            .expect("Telemetry should be set in builder after being passed");
        assert_eq!(weak_telemetry.strong_count(), 1);

        // Building the MozAdsClient should pass the telemetry, not clone it.
        let built_client = builder.build();
        assert_eq!(weak_telemetry.strong_count(), 1);

        // Shutting down, even though the builder still exists, should successfully shutdown all telemetry references.
        built_client.shutdown().unwrap();
        assert_eq!(weak_telemetry.strong_count(), 0);
        builder.build();
        assert_eq!(weak_telemetry.strong_count(), 0);
    }

    #[test]
    fn test_shutdown_telemetry_basic() {
        let _lock = telemetry::test_lock();
        viaduct_dev::init_backend_dev();

        // test with client created from config with no cache
        let builder = Arc::new(MozAdsClientBuilder::new()).telemetry(Box::new(NoopMozAdsTelemetry));
        let weak_reference = builder
            .fetch_telemetry()
            .expect("Inner telemetry should be Some in builder");
        let client = builder.build();

        // weak ref will show 0 strong references when the Arc<dyn MozAdsTelemetry> is gone.
        assert_ne!(weak_reference.strong_count(), 0);
        client.shutdown().unwrap();
        assert_eq!(weak_reference.strong_count(), 0);

        // test also with http cache
        let builder = Arc::new(MozAdsClientBuilder::new())
            .telemetry(Box::new(NoopMozAdsTelemetry))
            .cache_config(MozAdsCacheConfig {
                db_path: crate::IN_MEMORY_DB_PATH.to_string(),
                default_cache_ttl_seconds: None,
                max_size_mib: None,
            });
        let weak_reference = builder
            .fetch_telemetry()
            .expect("Inner telemetry should be Some in builder");
        let client = builder.build();

        // weak ref will show 0 strong references when the Arc<dyn MozAdsTelemetry> is gone.
        assert_ne!(weak_reference.strong_count(), 0);
        client.shutdown().unwrap();
        assert_eq!(weak_reference.strong_count(), 0);
    }

    #[test]
    fn test_shutdown_is_idempotent() {
        let _lock = telemetry::test_lock();
        viaduct_dev::init_backend_dev();

        let builder = Arc::new(MozAdsClientBuilder::new())
            .telemetry(Box::new(NoopMozAdsTelemetry))
            .cache_config(MozAdsCacheConfig {
                db_path: crate::IN_MEMORY_DB_PATH.to_string(),
                default_cache_ttl_seconds: None,
                max_size_mib: None,
            });
        let weak_reference = builder
            .fetch_telemetry()
            .expect("Inner telemetry should be Some in builder");
        let client = builder.build();

        client.shutdown().unwrap();
        assert_eq!(weak_reference.strong_count(), 0);

        // Repeated shutdowns must not error or re-close an already closed connection.
        client.shutdown().unwrap();
        client.shutdown().unwrap();
        assert_eq!(weak_reference.strong_count(), 0);
    }
}
