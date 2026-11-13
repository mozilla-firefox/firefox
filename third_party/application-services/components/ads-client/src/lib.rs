/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

use std::{collections::HashMap, sync::Arc};

use client::error::ComponentError;
use error_support::handle_error;
use mars::error::CallbackRequestError;
pub use url::Url as AdsClientUrl;

use client::AdsClient;
use http_cache::CachePolicy;
use mars::ad_request::{AdPlacementRequest, AdRequestFlags};
mod ads;
#[cfg(feature = "stateful")]
pub mod ads_store;
pub mod bytesize;
mod client;
mod clock;
mod ffi;
pub mod http_cache;
mod mars;
mod non_empty_string;
#[cfg(feature = "stateful")]
pub mod request;
pub mod telemetry;
#[cfg(feature = "stateful")]
pub mod worker;

pub use ffi::*;

/// Path that makes SQLite open a private in-memory database.
pub(crate) const IN_MEMORY_DB_PATH: &str = ":memory:";

#[cfg(test)]
mod test_utils;

uniffi::setup_scaffolding!("ads_client");

uniffi::custom_type!(AdsClientUrl, String, {
    remote,
    try_lift: |val| Ok(AdsClientUrl::parse(&val)?),
    lower: |obj| obj.as_str().to_owned(),
});
/// Requests ads from MARS and records their clicks, impressions and reports.
/// Build it with [`MozAdsClientBuilder`], ideally once per app.
#[derive(uniffi::Object)]
pub struct MozAdsClient {
    inner: Arc<AdsClient>,
}

#[uniffi::export]
impl MozAdsClient {
    /// Deletes everything in the HTTP cache.
    #[handle_error(ComponentError)]
    pub fn clear_cache(&self) -> AdsClientApiResult<()> {
        self.inner.clear_cache().map_err(ComponentError::ClearCache)
    }

    /// Records a click on an ad, from its `callbacks.click` URL.
    #[handle_error(ComponentError)]
    #[uniffi::method(default(options = None))]
    pub fn record_click(
        &self,
        click_url: String,
        options: Option<MozAdsCallbackOptions>,
    ) -> AdsClientApiResult<()> {
        let url = AdsClientUrl::parse(&click_url)
            .map_err(|e| ComponentError::RecordClick(CallbackRequestError::InvalidUrl(e).into()))?;
        let ohttp = options.is_some_and(|o| o.ohttp);
        self.inner
            .record_click(url, ohttp)
            .map_err(ComponentError::RecordClick)
    }

    /// Records an impression of an ad, from its `callbacks.impression` URL.
    #[handle_error(ComponentError)]
    #[uniffi::method(default(options = None))]
    pub fn record_impression(
        &self,
        impression_url: String,
        options: Option<MozAdsCallbackOptions>,
    ) -> AdsClientApiResult<()> {
        let url = AdsClientUrl::parse(&impression_url).map_err(|e| {
            ComponentError::RecordImpression(CallbackRequestError::InvalidUrl(e).into())
        })?;
        let ohttp = options.is_some_and(|o| o.ohttp);
        self.inner
            .record_impression(url, ohttp)
            .map_err(ComponentError::RecordImpression)
    }

    /// Reports an ad, from its `callbacks.report` URL.
    #[handle_error(ComponentError)]
    #[uniffi::method(default(options = None))]
    pub fn report_ad(
        &self,
        report_url: String,
        reason: MozAdsReportReason,
        options: Option<MozAdsCallbackOptions>,
    ) -> AdsClientApiResult<()> {
        let url = AdsClientUrl::parse(&report_url)
            .map_err(|e| ComponentError::ReportAd(CallbackRequestError::InvalidUrl(e).into()))?;
        let ohttp = options.is_some_and(|o| o.ohttp);
        self.inner
            .report_ad(url, reason.into(), ohttp)
            .map_err(ComponentError::ReportAd)
    }

    /// Requests one image ad per placement. The result is keyed by
    /// `placement_id` and leaves out placements with no ad.
    #[handle_error(ComponentError)]
    #[uniffi::method(default(options = None))]
    pub fn request_image_ads(
        &self,
        moz_ad_requests: Vec<MozAdsPlacementRequest>,
        options: Option<MozAdsRequestOptions>,
    ) -> AdsClientApiResult<HashMap<String, MozAdsImage>> {
        let requests: Vec<AdPlacementRequest> = moz_ad_requests.iter().map(Into::into).collect();
        let options = options.unwrap_or_default();
        let flags = AdRequestFlags::from(&options);
        let ohttp = options.ohttp;
        let cache_policy = options.cache_policy.map(CachePolicy::from);
        let blocks = options.blocks;
        let response = self
            .inner
            .request_image_ads(requests, flags, cache_policy, ohttp, blocks)
            .map_err(ComponentError::RequestAds)?;
        Ok(response.into_iter().map(|(k, v)| (k, v.into())).collect())
    }

    /// Requests `count` spocs per placement. The result is keyed by
    /// `placement_id` and leaves out placements with no ad.
    #[handle_error(ComponentError)]
    #[uniffi::method(default(options = None))]
    pub fn request_spoc_ads(
        &self,
        moz_ad_requests: Vec<MozAdsPlacementRequestWithCount>,
        options: Option<MozAdsRequestOptions>,
    ) -> AdsClientApiResult<HashMap<String, Vec<MozAdsSpoc>>> {
        let requests: Vec<AdPlacementRequest> = moz_ad_requests.iter().map(Into::into).collect();
        let options = options.unwrap_or_default();
        let flags = AdRequestFlags::from(&options);
        let ohttp = options.ohttp;
        let cache_policy = options.cache_policy.map(CachePolicy::from);
        let blocks = options.blocks;
        let response = self
            .inner
            .request_spoc_ads(requests, flags, cache_policy, ohttp, blocks)
            .map_err(ComponentError::RequestAds)?;
        Ok(response
            .into_iter()
            .map(|(k, v)| (k, v.into_iter().map(Into::into).collect()))
            .collect())
    }

    /// Requests `count` ads per placement, for placements that can return
    /// either spocs or image ads. The result is keyed by `placement_id` and
    /// leaves out placements with no ad.
    #[handle_error(ComponentError)]
    #[uniffi::method(default(options = None))]
    pub fn request_spoc_or_image_ads(
        &self,
        moz_ad_requests: Vec<MozAdsPlacementRequestWithCount>,
        options: Option<MozAdsRequestOptions>,
    ) -> AdsClientApiResult<HashMap<String, Vec<MozAdsImageOrSpoc>>> {
        let requests: Vec<AdPlacementRequest> = moz_ad_requests.iter().map(Into::into).collect();
        let options = options.unwrap_or_default();
        let flags = AdRequestFlags::from(&options);
        let ohttp = options.ohttp;
        let cache_policy = options.cache_policy.map(CachePolicy::from);
        let blocks = options.blocks;
        let response = self
            .inner
            .request_spoc_or_image_ads(requests, flags, cache_policy, ohttp, blocks)
            .map_err(ComponentError::RequestAds)?;
        Ok(response
            .into_iter()
            .map(|(k, v)| (k, v.into_iter().map(Into::into).collect()))
            .collect())
    }

    /// Requests one tile ad per placement. The result is keyed by
    /// `placement_id` and leaves out placements with no ad.
    #[handle_error(ComponentError)]
    #[uniffi::method(default(options = None))]
    pub fn request_tile_ads(
        &self,
        moz_ad_requests: Vec<MozAdsPlacementRequest>,
        options: Option<MozAdsRequestOptions>,
    ) -> AdsClientApiResult<HashMap<String, MozAdsTile>> {
        let requests: Vec<AdPlacementRequest> = moz_ad_requests.iter().map(Into::into).collect();
        let options = options.unwrap_or_default();
        let flags = AdRequestFlags::from(&options);
        let ohttp = options.ohttp;
        let cache_policy = options.cache_policy.map(CachePolicy::from);
        let blocks = options.blocks;
        let relevance = options.relevance;
        let response = self
            .inner
            .request_tile_ads(requests, flags, cache_policy, ohttp, blocks, relevance)
            .map_err(ComponentError::RequestAds)?;
        Ok(response.into_iter().map(|(k, v)| (k, v.into())).collect())
    }

    /// Allows the ads-client to unload some references and prepare for a safe shutdown.
    /// Other methods should not be called after this one.
    /// Currently, we shutdown (with no expected errors).
    /// However, future shutdowns may require an error to be logged, so we keep the Result.
    #[uniffi::method()]
    pub fn shutdown(&self) -> AdsClientApiResult<()> {
        self.inner.shutdown();
        Ok(())
    }
}
