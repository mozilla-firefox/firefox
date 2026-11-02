/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

use crate::ads::{AdImage, AdImageOrSpoc, AdSpoc, AdTile};
#[cfg(feature = "stateful")]
use crate::ads_store::AdsStore;
use crate::bytesize::ByteSize;
use crate::http_cache::{CachePolicy, HttpCache};
use crate::mars::ad_request::{AdPlacementRequest, AdRequestFlags, AdRequestRelevance};
use crate::mars::ad_response::AdResponse;
use crate::mars::error::{RecordClickError, RecordImpressionError, ReportAdError};
use crate::mars::{MARSClient, ReportReason};
use crate::telemetry::{self, ClientOperation};
use config::AdsClientConfig;
use context_id::{ContextIDComponent, DefaultContextIdCallback};
use error::RequestAdsError;
use sql_support::open_database;
use std::collections::HashMap;
use std::sync::Arc;
use std::time::Duration;
use url::Url;
use uuid::Uuid;

pub mod config;
pub mod error;

const DEFAULT_TTL_SECONDS: u64 = 300;
const DEFAULT_MAX_CACHE_SIZE_MIB: u64 = 10;
const DEFAULT_ROTATION_DAYS: u8 = 3;

pub struct AdsClient {
    inner: Arc<AdsClientInner>,
}

pub struct AdsClientInner {
    #[cfg(feature = "stateful")]
    ads_store: Arc<Option<AdsStore>>,
    client: MARSClient,
    context_id_component: ContextIDComponent,
}

impl AdsClient {
    pub fn new(client_config: AdsClientConfig) -> Self {
        let inner = Arc::new(AdsClientInner::new(client_config));
        AdsClient { inner }
    }

    pub fn clear_cache(&self) -> Result<(), open_database::Error> {
        self.inner.clear_cache()
    }

    #[cfg(test)]
    pub fn get_context_id(&self) -> context_id::ApiResult<String> {
        self.inner.get_context_id()
    }

    pub fn record_click(&self, click_url: Url, ohttp: bool) -> Result<(), RecordClickError> {
        self.inner.record_click(click_url, ohttp)
    }

    pub fn record_impression(
        &self,
        impression_url: Url,
        ohttp: bool,
    ) -> Result<(), RecordImpressionError> {
        self.inner.record_impression(impression_url, ohttp)
    }

    pub fn report_ad(
        &self,
        report_url: Url,
        reason: ReportReason,
        ohttp: bool,
    ) -> Result<(), ReportAdError> {
        self.inner.report_ad(report_url, reason, ohttp)
    }

    pub fn request_image_ads(
        &self,
        ad_placement_requests: Vec<AdPlacementRequest>,
        flags: AdRequestFlags,
        options: Option<CachePolicy>,
        ohttp: bool,
        blocks: Vec<String>,
    ) -> Result<HashMap<String, AdImage>, RequestAdsError> {
        self.inner
            .request_image_ads(ad_placement_requests, flags, options, ohttp, blocks)
    }

    pub fn request_spoc_ads(
        &self,
        ad_placement_requests: Vec<AdPlacementRequest>,
        flags: AdRequestFlags,
        options: Option<CachePolicy>,
        ohttp: bool,
        blocks: Vec<String>,
    ) -> Result<HashMap<String, Vec<AdSpoc>>, RequestAdsError> {
        self.inner
            .request_spoc_ads(ad_placement_requests, flags, options, ohttp, blocks)
    }

    pub fn request_tile_ads(
        &self,
        ad_placement_requests: Vec<AdPlacementRequest>,
        flags: AdRequestFlags,
        options: Option<CachePolicy>,
        ohttp: bool,
        blocks: Vec<String>,
        relevance: AdRequestRelevance,
    ) -> Result<HashMap<String, AdTile>, RequestAdsError> {
        self.inner.request_tile_ads(
            ad_placement_requests,
            flags,
            options,
            ohttp,
            blocks,
            relevance,
        )
    }

    pub fn request_spoc_or_image_ads(
        &self,
        ad_placement_requests: Vec<AdPlacementRequest>,
        flags: AdRequestFlags,
        options: Option<CachePolicy>,
        ohttp: bool,
        blocks: Vec<String>,
    ) -> Result<HashMap<String, Vec<AdImageOrSpoc>>, RequestAdsError> {
        self.inner
            .request_spoc_or_image_ads(ad_placement_requests, flags, options, ohttp, blocks)
    }

    pub fn shutdown(&self) {
        self.inner.shutdown();
    }

    #[cfg(test)]
    fn request_ads(
        &self,
        placements: Vec<AdPlacementRequest>,
        flags: AdRequestFlags,
        options: Option<CachePolicy>,
        ohttp: bool,
        blocks: Vec<String>,
        relevance: AdRequestRelevance,
    ) -> Result<AdResponse, RequestAdsError> {
        self.inner
            .request_ads(placements, flags, options, ohttp, blocks, relevance)
    }
}

impl AdsClientInner {
    pub fn new(client_config: AdsClientConfig) -> Self {
        let context_id_component = ContextIDComponent::new(
            &Uuid::new_v4().to_string(),
            0,
            cfg!(test),
            Box::new(DefaultContextIdCallback),
        );

        let environment = client_config.environment;

        // Configure the cache if a path is provided.
        // Defaults for ttl and cache size are also set if unspecified.
        let http_cache = client_config
            .cache_config
            .and_then(|cache_cfg| {
                let default_cache_ttl = Duration::from_secs(
                    cache_cfg
                        .default_cache_ttl_seconds
                        .unwrap_or(DEFAULT_TTL_SECONDS),
                );
                let max_cache_size =
                    ByteSize::mib(cache_cfg.max_size_mib.unwrap_or(DEFAULT_MAX_CACHE_SIZE_MIB));

                match HttpCache::builder(cache_cfg.db_path)
                    .max_size(max_cache_size)
                    .default_ttl(default_cache_ttl)
                    .build()
                {
                    Ok(cache) => Some(cache),
                    Err(e) => {
                        telemetry::record_build_cache_error(&e);
                        None
                    }
                }
            })
            .or_else(|| {
                // If no `cache_config` is provided, create an in-memory db with default parameters.
                // TODO: the `build` function should be infallible, and no error would be needed here.
                // (true for both memory and FS, because FS would default to memory)
                HttpCache::builder_memory()
                    .build()
                    .inspect_err(telemetry::record_build_cache_error)
                    .ok()
            });

        #[cfg(feature = "stateful")]
        let ads_store =
            client_config
                .store_config
                .and_then(|x| match AdsStore::builder(x.db_path).build() {
                    Ok(store) => Some(store),
                    Err(e) => {
                        telemetry::record_ads_store_error(&e);
                        None
                    }
                });

        let client = MARSClient::new(environment, http_cache);
        telemetry::record_client_operation(ClientOperation::New);
        Self {
            #[cfg(feature = "stateful")]
            ads_store: Arc::new(ads_store),
            client,
            context_id_component,
        }
    }

    pub fn clear_cache(&self) -> Result<(), open_database::Error> {
        self.client.clear_cache()
    }

    pub fn get_context_id(&self) -> context_id::ApiResult<String> {
        self.context_id_component.request(DEFAULT_ROTATION_DAYS)
    }

    pub fn record_click(&self, click_url: Url, ohttp: bool) -> Result<(), RecordClickError> {
        // TODO: Re-enable cache invalidation behind a Nimbus experiment.
        // The mobile team has requested this be temporarily disabled.
        // let mut click_url = click_url.clone();
        // if let Some(request_hash) = RequestHash::pop_from_url(&mut click_url) {
        //     let _ = self.client.invalidate_cache_by_hash(&request_hash);
        // }
        self.client
            .record_click(click_url, ohttp)
            .inspect_err(|e| {
                telemetry::record_client_error(ClientOperation::RecordClick, e);
            })
            .inspect(|_| {
                telemetry::record_client_operation(ClientOperation::RecordClick);
            })
    }

    pub fn record_impression(
        &self,
        impression_url: Url,
        ohttp: bool,
    ) -> Result<(), RecordImpressionError> {
        // TODO: Re-enable cache invalidation behind a Nimbus experiment.
        // The mobile team has requested this be temporarily disabled.
        // let mut impression_url = impression_url.clone();
        // if let Some(request_hash) = RequestHash::pop_from_url(&mut impression_url) {
        //     let _ = self.client.invalidate_cache_by_hash(&request_hash);
        // }

        // TODO: Add count call with _cap_key for impression capping logic
        let impression_url = if let Some((_, _cap_key)) = impression_url
            .query_pairs()
            .find(|(key, _)| key == "cap_key")
        {
            let mut new_url = impression_url.clone();
            new_url
                .query_pairs_mut()
                .clear()
                .extend_pairs(
                    impression_url
                        .query_pairs()
                        .collect::<Vec<_>>()
                        .iter()
                        .filter(|(key, _)| key != "cap_key"),
                )
                .finish();
            new_url
        } else {
            impression_url
        };

        self.client
            .record_impression(impression_url, ohttp)
            .inspect_err(|e| {
                telemetry::record_client_error(ClientOperation::RecordImpression, e);
            })
            .inspect(|_| {
                telemetry::record_client_operation(ClientOperation::RecordImpression);
            })
    }

    pub fn report_ad(
        &self,
        report_url: Url,
        reason: ReportReason,
        ohttp: bool,
    ) -> Result<(), ReportAdError> {
        self.client
            .report_ad(report_url, reason, ohttp)
            .inspect_err(|e| {
                telemetry::record_client_error(ClientOperation::ReportAd, e);
            })
            .inspect(|_| {
                telemetry::record_client_operation(ClientOperation::ReportAd);
            })
    }

    // relevance is not implemented for image ads, using an empty HashMap.
    pub fn request_image_ads(
        &self,
        ad_placement_requests: Vec<AdPlacementRequest>,
        flags: AdRequestFlags,
        options: Option<CachePolicy>,
        ohttp: bool,
        blocks: Vec<String>,
    ) -> Result<HashMap<String, AdImage>, RequestAdsError> {
        let response = self
            .request_ads(
                ad_placement_requests,
                flags,
                options,
                ohttp,
                blocks,
                AdRequestRelevance::default(),
            )
            .inspect_err(|e| {
                telemetry::record_client_error(ClientOperation::RequestAds, e);
            })?;
        telemetry::record_client_operation(ClientOperation::RequestAds);
        Ok(response.take_first::<AdImage>())
    }

    // relevance is not implemented for spocs, using an empty HashMap.
    pub fn request_spoc_ads(
        &self,
        ad_placement_requests: Vec<AdPlacementRequest>,
        flags: AdRequestFlags,
        options: Option<CachePolicy>,
        ohttp: bool,
        blocks: Vec<String>,
    ) -> Result<HashMap<String, Vec<AdSpoc>>, RequestAdsError> {
        let result = self.request_ads(
            ad_placement_requests,
            flags,
            options,
            ohttp,
            blocks,
            AdRequestRelevance::default(),
        );
        result
            .inspect_err(|e| {
                telemetry::record_client_error(ClientOperation::RequestAds, e);
            })
            .map(|response| {
                telemetry::record_client_operation(ClientOperation::RequestAds);
                response.into_kind::<AdSpoc>()
            })
    }

    // Sometimes, spoc array requests can return multiple types of ads for specific placements.
    // This allows both types to be returned without error (as instances of the enum `AdImageOrSpoc`)
    // TODO: This, as well as the other synchronous functions, are to be deprecated
    // when the stateful client rework is complete.
    pub fn request_spoc_or_image_ads(
        &self,
        ad_placement_requests: Vec<AdPlacementRequest>,
        flags: AdRequestFlags,
        options: Option<CachePolicy>,
        ohttp: bool,
        blocks: Vec<String>,
    ) -> Result<HashMap<String, Vec<AdImageOrSpoc>>, RequestAdsError> {
        let result = self.request_ads(
            ad_placement_requests,
            flags,
            options,
            ohttp,
            blocks,
            AdRequestRelevance::default(),
        );
        result
            .inspect_err(|e| {
                telemetry::record_client_error(ClientOperation::RequestAds, e);
            })
            .map(|response| {
                telemetry::record_client_operation(ClientOperation::RequestAds);
                response.into_kind::<AdImageOrSpoc>()
            })
    }

    pub fn request_tile_ads(
        &self,
        ad_placement_requests: Vec<AdPlacementRequest>,
        flags: AdRequestFlags,
        options: Option<CachePolicy>,
        ohttp: bool,
        blocks: Vec<String>,
        relevance: AdRequestRelevance,
    ) -> Result<HashMap<String, AdTile>, RequestAdsError> {
        let result = self.request_ads(
            ad_placement_requests,
            flags,
            options,
            ohttp,
            blocks,
            relevance,
        );
        result
            .inspect_err(|e| {
                telemetry::record_client_error(ClientOperation::RequestAds, e);
            })
            .map(|response| {
                telemetry::record_client_operation(ClientOperation::RequestAds);
                response.take_first::<AdTile>()
            })
    }

    pub fn shutdown(&self) {
        telemetry::uninstall();

        #[cfg(feature = "stateful")]
        if let Some(ads_store) = self.ads_store.as_ref() {
            ads_store.shutdown_db();
        }

        self.client.shutdown_db();
    }

    fn request_ads(
        &self,
        placements: Vec<AdPlacementRequest>,
        flags: AdRequestFlags,
        options: Option<CachePolicy>,
        ohttp: bool,
        blocks: Vec<String>,
        relevance: AdRequestRelevance,
    ) -> Result<AdResponse, RequestAdsError> {
        let context_id = if relevance.is_empty() {
            Some(self.get_context_id()?)
        } else {
            None
        };

        let cache_policy = options.unwrap_or_default();
        let (mut response, request_hash) = self.client.fetch_ads(
            context_id,
            flags,
            placements,
            cache_policy,
            ohttp,
            blocks,
            relevance,
        )?;
        response.enrich_callbacks(&request_hash);
        Ok(response)
    }
}

#[cfg(test)]
mod tests {

    #[cfg(feature = "stateful")]
    use crate::ads_store::builder::AdsStoreBuilder;
    use crate::{
        mars::Environment,
        test_utils::{
            get_example_happy_image_response, get_example_happy_spoc_response,
            get_example_happy_uatile_response, make_happy_placement_requests,
        },
    };

    use super::*;

    fn new_with_mars_client(client: MARSClient) -> AdsClient {
        let inner = Arc::new(AdsClientInner {
            #[cfg(feature = "stateful")]
            ads_store: Arc::new(Some(
                AdsStoreBuilder::new("test_store.db")
                    .build()
                    .expect("Simplest AdsStoreBuilder should be constructable"),
            )),
            client,
            context_id_component: ContextIDComponent::new(
                &Uuid::new_v4().to_string(),
                0,
                false,
                Box::new(DefaultContextIdCallback),
            ),
        });
        AdsClient {
            inner: inner.clone(),
        }
    }

    #[test]
    fn test_get_context_id() {
        let config = AdsClientConfig {
            cache_config: None,
            environment: Environment::Test,
            #[cfg(feature = "stateful")]
            store_config: None,
        };
        let client = AdsClient::new(config);
        let context_id = client.get_context_id().unwrap();
        assert!(!context_id.is_empty());
    }

    #[test]
    fn test_request_image_ads_happy() {
        viaduct_dev::init_backend_dev();

        let expected_response = get_example_happy_image_response();
        let m = mockito::mock("POST", "/ads")
            .with_status(200)
            .with_header("content-type", "application/json")
            .with_body(serde_json::to_string(&expected_response.data).unwrap())
            .create();

        let mars_client = MARSClient::new(Environment::Test, None);
        let ads_client = new_with_mars_client(mars_client);

        let result = ads_client.request_image_ads(
            make_happy_placement_requests(),
            AdRequestFlags::default(),
            None,
            false,
            Default::default(),
        );
        assert!(result.is_ok());
        m.assert();
    }

    #[test]
    fn test_request_spocs_happy() {
        viaduct_dev::init_backend_dev();

        let expected_response = get_example_happy_spoc_response();
        let m = mockito::mock("POST", "/ads")
            .with_status(200)
            .with_header("content-type", "application/json")
            .with_body(serde_json::to_string(&expected_response.data).unwrap())
            .create();

        let mars_client = MARSClient::new(Environment::Test, None);
        let ads_client = new_with_mars_client(mars_client);

        let result = ads_client.request_spoc_ads(
            make_happy_placement_requests(),
            AdRequestFlags::default(),
            None,
            false,
            Default::default(),
        );
        assert!(result.is_ok());
        m.assert();
    }

    #[test]
    fn test_request_tiles_happy() {
        viaduct_dev::init_backend_dev();

        let expected_response = get_example_happy_uatile_response();
        let m = mockito::mock("POST", "/ads")
            .with_status(200)
            .with_header("content-type", "application/json")
            .with_body(serde_json::to_string(&expected_response.data).unwrap())
            .create();

        let mars_client = MARSClient::new(Environment::Test, None);
        let ads_client = new_with_mars_client(mars_client);

        let result = ads_client.request_tile_ads(
            make_happy_placement_requests(),
            AdRequestFlags::default(),
            None,
            false,
            Default::default(),
            AdRequestRelevance::default(),
        );
        assert!(result.is_ok());
        m.assert();
    }

    #[test]
    fn test_context_id_is_sent_to_mars() {
        viaduct_dev::init_backend_dev();

        let config = AdsClientConfig {
            cache_config: None,
            environment: Environment::Test,
            #[cfg(feature = "stateful")]
            store_config: None,
        };
        let client = AdsClient::new(config);

        // The client generates its own context id, so read it back first and
        // assert that exactly that value reaches the wire.
        let context_id = client.get_context_id().unwrap();
        assert!(Uuid::parse_str(&context_id).is_ok());

        let expected_response = get_example_happy_image_response();
        let m = mockito::mock("POST", "/ads")
            .match_body(mockito::Matcher::PartialJsonString(format!(
                r#"{{"context_id":"{context_id}"}}"#
            )))
            .with_status(200)
            .with_header("content-type", "application/json")
            .with_body(serde_json::to_string(&expected_response.data).unwrap())
            .create();

        let result = client.request_image_ads(
            make_happy_placement_requests(),
            AdRequestFlags::default(),
            None,
            false,
            Default::default(),
        );
        assert!(result.is_ok());
        m.assert();
    }

    #[test]
    fn test_context_id_is_not_sent_to_mars_when_relevance_is_set() {
        viaduct_dev::init_backend_dev();

        let config = AdsClientConfig {
            cache_config: None,
            environment: Environment::Test,
            #[cfg(feature = "stateful")]
            store_config: None,
        };
        let client = AdsClient::new(config);

        // Matcher::Json compares the whole body, ignoring key order, so any
        // extra key such as context_id makes the request go unmatched.
        // This test will fail if any new fields that are not empty are added to the request.  This is intentional.
        // Any new fields must be considered along side relevence scores.
        let expected_response = get_example_happy_uatile_response();
        let m: mockito::Mock = mockito::mock("POST", "/ads")
            .match_body(mockito::Matcher::Json(serde_json::json!({
                "placements": serde_json::to_value(make_happy_placement_requests()).unwrap(),
                "relevance": {"sports": 0.8},
            })))
            .with_status(200)
            .with_header("content-type", "application/json")
            .with_body(serde_json::to_string(&expected_response.data).unwrap())
            .create();

        let result = client.request_tile_ads(
            make_happy_placement_requests(),
            AdRequestFlags::default(),
            None,
            false,
            Default::default(),
            HashMap::from([("sports".to_string(), 0.8)]),
        );
        assert!(result.is_ok());
        m.assert();
    }

    #[test]
    fn test_record_impression_removes_cap_key() {
        viaduct_dev::init_backend_dev();
        let mars_client = MARSClient::new(Environment::Test, None);
        let ads_client = new_with_mars_client(mars_client);

        let base_url = mockito::server_url();
        let path_and_query = "/impression?kept=example";
        let callback_url = Url::parse(&format!("{}{}", base_url, path_and_query)).unwrap();

        let mock = mockito::mock("GET", path_and_query)
            .with_status(200)
            .create();

        ads_client.record_impression(callback_url, false).unwrap();

        mock.assert();

        let callback_url_with_cap_key =
            Url::parse(&format!("{}{}&cap_key=test", base_url, path_and_query)).unwrap();
        ads_client
            .record_impression(callback_url_with_cap_key, false)
            .unwrap();

        mock.expect(2).assert();
    }

    #[test]
    #[ignore = "Cache invalidation temporarily disabled - will be re-enabled behind Nimbus experiment"]
    fn test_record_click_invalidates_cache() {
        viaduct_dev::init_backend_dev();
        let cache = HttpCache::builder("test_record_click_invalidates_cache")
            .build()
            .unwrap();
        let mars_client = MARSClient::new(Environment::Test, Some(cache));
        let ads_client = new_with_mars_client(mars_client);

        let response = get_example_happy_image_response();

        let m1 = mockito::mock("POST", "/ads")
            .with_status(200)
            .with_header("content-type", "application/json")
            .with_body(serde_json::to_string(&response.data).unwrap())
            .expect(2) // we expect 2 requests to the server, one for the initial ad request and one after for the cache invalidation request
            .create();

        let response = ads_client
            .request_image_ads(
                make_happy_placement_requests(),
                AdRequestFlags::default(),
                None,
                false,
                Default::default(),
            )
            .unwrap();
        let callback_url = response.values().next().unwrap().callbacks.click.clone();

        let m2 = mockito::mock("GET", callback_url.path())
            .with_status(200)
            .create();

        ads_client
            .request_image_ads(
                make_happy_placement_requests(),
                AdRequestFlags::default(),
                None,
                false,
                Default::default(),
            )
            .unwrap();

        ads_client.record_click(callback_url, false).unwrap();

        ads_client
            .request_ads(
                make_happy_placement_requests(),
                AdRequestFlags::default(),
                Some(CachePolicy::default()),
                false,
                Default::default(),
                AdRequestRelevance::default(),
            )
            .unwrap();

        m1.assert();
        m2.assert();
    }
}
