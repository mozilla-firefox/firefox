/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

use std::collections::HashMap;

use error::{CallbackRequestError, ComponentError};
use error_support::handle_error;
use parking_lot::Mutex;
use url::Url as AdsClientUrl;

use client::ad_request::AdPlacementRequest;
use client::AdsClient;
use http_cache::RequestCachePolicy;

mod client;
mod error;
mod ffi;
pub mod http_cache;
mod mars;
pub mod telemetry;

pub use ffi::*;

use crate::client::config::AdsClientConfig;
use crate::ffi::telemetry::MozAdsTelemetryWrapper;

#[cfg(test)]
mod test_utils;

uniffi::setup_scaffolding!("ads_client");

uniffi::custom_type!(AdsClientUrl, String, {
    remote,
    try_lift: |val| Ok(AdsClientUrl::parse(&val)?),
    lower: |obj| obj.as_str().to_string(),
});

#[derive(uniffi::Object)]
pub struct MozAdsClient {
    inner: Mutex<AdsClient<MozAdsTelemetryWrapper>>,
}
#[uniffi::export]
impl MozAdsClient {
    #[uniffi::constructor]
    pub fn new(client_config: Option<MozAdsClientConfig>) -> Self {
        let client_config = client_config.unwrap_or_default();
        let client_config: AdsClientConfig<MozAdsTelemetryWrapper> = client_config.into();
        let client = AdsClient::new(client_config);
        Self {
            inner: Mutex::new(client),
        }
    }

    #[handle_error(ComponentError)]
    pub fn request_image_ads(
        &self,
        moz_ad_requests: Vec<MozAdsPlacementRequest>,
        options: Option<MozAdsRequestOptions>,
    ) -> AdsClientApiResult<HashMap<String, MozAdsImage>> {
        let inner = self.inner.lock();
        let requests: Vec<AdPlacementRequest> = moz_ad_requests.iter().map(|r| r.into()).collect();
        let cache_policy: RequestCachePolicy = options.into();
        let response = inner
            .request_image_ads(requests, Some(cache_policy))
            .map_err(ComponentError::RequestAds)?;
        Ok(response.into_iter().map(|(k, v)| (k, v.into())).collect())
    }

    #[handle_error(ComponentError)]
    pub fn request_spoc_ads(
        &self,
        moz_ad_requests: Vec<MozAdsPlacementRequestWithCount>,
        options: Option<MozAdsRequestOptions>,
    ) -> AdsClientApiResult<HashMap<String, Vec<MozAdsSpoc>>> {
        let inner = self.inner.lock();
        let requests: Vec<AdPlacementRequest> = moz_ad_requests.iter().map(|r| r.into()).collect();
        let cache_policy: RequestCachePolicy = options.into();
        let response = inner
            .request_spoc_ads(requests, Some(cache_policy))
            .map_err(ComponentError::RequestAds)?;
        Ok(response
            .into_iter()
            .map(|(k, v)| (k, v.into_iter().map(|spoc| spoc.into()).collect()))
            .collect())
    }

    #[handle_error(ComponentError)]
    pub fn request_tile_ads(
        &self,
        moz_ad_requests: Vec<MozAdsPlacementRequest>,
        options: Option<MozAdsRequestOptions>,
    ) -> AdsClientApiResult<HashMap<String, MozAdsTile>> {
        let inner = self.inner.lock();
        let requests: Vec<AdPlacementRequest> = moz_ad_requests.iter().map(|r| r.into()).collect();
        let cache_policy: RequestCachePolicy = options.into();
        let response = inner
            .request_tile_ads(requests, Some(cache_policy))
            .map_err(ComponentError::RequestAds)?;
        Ok(response.into_iter().map(|(k, v)| (k, v.into())).collect())
    }

    #[handle_error(ComponentError)]
    pub fn record_impression(&self, impression_url: String) -> AdsClientApiResult<()> {
        let url = AdsClientUrl::parse(&impression_url).map_err(|e| {
            ComponentError::RecordImpression(CallbackRequestError::InvalidUrl(e).into())
        })?;
        let inner = self.inner.lock();
        inner
            .record_impression(url)
            .map_err(ComponentError::RecordImpression)
    }

    #[handle_error(ComponentError)]
    pub fn record_click(&self, click_url: String) -> AdsClientApiResult<()> {
        let url = AdsClientUrl::parse(&click_url)
            .map_err(|e| ComponentError::RecordClick(CallbackRequestError::InvalidUrl(e).into()))?;
        let inner = self.inner.lock();
        inner.record_click(url).map_err(ComponentError::RecordClick)
    }

    #[handle_error(ComponentError)]
    pub fn report_ad(&self, report_url: String) -> AdsClientApiResult<()> {
        let url = AdsClientUrl::parse(&report_url)
            .map_err(|e| ComponentError::ReportAd(CallbackRequestError::InvalidUrl(e).into()))?;
        let inner = self.inner.lock();
        inner.report_ad(url).map_err(ComponentError::ReportAd)
    }

    pub fn cycle_context_id(&self) -> AdsClientApiResult<String> {
        let mut inner = self.inner.lock();
        let previous_context_id = inner.cycle_context_id()?;
        Ok(previous_context_id)
    }

    pub fn clear_cache(&self) -> AdsClientApiResult<()> {
        let inner = self.inner.lock();
        inner
            .clear_cache()
            .map_err(|_| MozAdsClientApiError::Other {
                reason: "Failed to clear cache".to_string(),
            })
    }
}
