/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

use crate::{
    client::{
        ad_request::AdRequest,
        ad_response::{AdResponse, AdResponseValue},
        config::Environment,
    },
    error::{
        check_http_status_for_error, CallbackRequestError, FetchAdsError, RecordClickError,
        RecordImpressionError, ReportAdError,
    },
    http_cache::{CacheOutcome, HttpCache, HttpCacheError, RequestHash},
    RequestCachePolicy,
};
use url::Url;
use viaduct::Request;

pub struct MARSClient {
    endpoint: Url,
    http_cache: Option<HttpCache>,
}

impl MARSClient {
    pub fn new(environment: Environment, http_cache: Option<HttpCache>) -> Self {
        let endpoint = environment.into_mars_url().clone();

        Self {
            endpoint,
            http_cache,
        }
    }

    fn make_callback_request(&self, callback: Url) -> Result<(), CallbackRequestError> {
        let request = Request::get(callback);
        let response = request.send()?;
        check_http_status_for_error(&response).map_err(Into::into)
    }

    pub fn get_mars_endpoint(&self) -> &Url {
        &self.endpoint
    }

    pub fn fetch_ads<T>(
        &self,
        ad_request: &AdRequest,
        cache_policy: &RequestCachePolicy,
    ) -> Result<(AdResponse<T>, RequestHash), FetchAdsError>
    where
        T: AdResponseValue,
    {
        let base = self.get_mars_endpoint();
        let url = base.join("ads")?;
        let request = Request::post(url).json(ad_request);
        let request_hash = RequestHash::from(&request);

        let response: AdResponse<T> = if let Some(cache) = self.http_cache.as_ref() {
            let outcome = cache.send_with_policy(&request, cache_policy)?;

            // TODO: observe cache outcome for metrics/logging.
            match &outcome.cache_outcome {
                CacheOutcome::Hit => {}
                CacheOutcome::LookupFailed(_err) => {}
                CacheOutcome::NoCache => {}
                CacheOutcome::MissNotCacheable => {}
                CacheOutcome::MissStored => {}
                CacheOutcome::StoreFailed(_err) => {}
                CacheOutcome::CleanupFailed(_err) => {}
            }
            check_http_status_for_error(&outcome.response)?;
            outcome.response.json()?
        } else {
            let response = request.send()?;
            check_http_status_for_error(&response)?;
            response.json()?
        };
        Ok((response, request_hash))
    }

    pub fn record_impression(&self, callback: Url) -> Result<(), RecordImpressionError> {
        Ok(self.make_callback_request(callback)?)
    }

    pub fn record_click(&self, callback: Url) -> Result<(), RecordClickError> {
        Ok(self.make_callback_request(callback)?)
    }

    pub fn invalidate_cache_by_hash(
        &self,
        request_hash: &crate::http_cache::RequestHash,
    ) -> Result<(), HttpCacheError> {
        if let Some(cache) = &self.http_cache {
            cache.invalidate_by_hash(request_hash)?;
        }
        Ok(())
    }

    pub fn report_ad(&self, callback: Url) -> Result<(), ReportAdError> {
        Ok(self.make_callback_request(callback)?)
    }

    pub fn clear_cache(&self) -> Result<(), HttpCacheError> {
        if let Some(cache) = &self.http_cache {
            cache.clear()?;
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {

    use super::*;
    use crate::client::ad_response::AdImage;
    use crate::test_utils::{get_example_happy_image_response, make_happy_ad_request};
    use mockito::mock;

    #[test]
    fn test_record_impression_with_valid_url_should_succeed() {
        viaduct_dev::init_backend_dev();
        let _m = mock("GET", "/impression_callback_url")
            .with_status(200)
            .create();
        let client = MARSClient::new(Environment::Test, None);
        let url = Url::parse(&format!(
            "{}/impression_callback_url",
            &mockito::server_url()
        ))
        .unwrap();
        let result = client.record_impression(url);
        assert!(result.is_ok());
    }

    #[test]
    fn test_record_click_with_valid_url_should_succeed() {
        viaduct_dev::init_backend_dev();
        let _m = mock("GET", "/click_callback_url").with_status(200).create();

        let client = MARSClient::new(Environment::Test, None);
        let url = Url::parse(&format!("{}/click_callback_url", &mockito::server_url())).unwrap();
        let result = client.record_click(url);
        assert!(result.is_ok());
    }

    #[test]
    fn test_report_ad_with_valid_url_should_succeed() {
        viaduct_dev::init_backend_dev();
        let _m = mock("GET", "/report_ad_callback_url")
            .with_status(200)
            .create();

        let client = MARSClient::new(Environment::Test, None);
        let url = Url::parse(&format!(
            "{}/report_ad_callback_url",
            &mockito::server_url()
        ))
        .unwrap();
        let result = client.report_ad(url);
        assert!(result.is_ok());
    }

    #[test]
    fn test_fetch_ads_success() {
        viaduct_dev::init_backend_dev();
        let expected_response = get_example_happy_image_response();

        let _m = mock("POST", "/ads")
            .match_header("content-type", "application/json")
            .with_status(200)
            .with_header("content-type", "application/json")
            .with_body(serde_json::to_string(&expected_response).unwrap())
            .create();

        let client = MARSClient::new(Environment::Test, None);

        let ad_request = make_happy_ad_request();

        let result = client.fetch_ads::<AdImage>(&ad_request, &RequestCachePolicy::default());
        assert!(result.is_ok());
        let (response, _request_hash) = result.unwrap();
        assert_eq!(expected_response, response);
    }

    #[test]
    fn test_fetch_ads_cache_hit_skips_network() {
        viaduct_dev::init_backend_dev();
        let expected = get_example_happy_image_response();
        let _m = mock("POST", "/ads")
            .with_status(200)
            .with_header("content-type", "application/json")
            .with_body(serde_json::to_string(&expected).unwrap())
            .expect(1) // only first request goes to network
            .create();

        let client = MARSClient::new(Environment::Test, None);
        let ad_request = make_happy_ad_request();

        // First call should be a miss then warm the cache
        let (response1, _request_hash1) = client
            .fetch_ads::<AdImage>(&ad_request, &RequestCachePolicy::default())
            .unwrap();
        assert_eq!(response1, expected);

        // Second call should be a hit
        let (response2, _request_hash2) = client
            .fetch_ads::<AdImage>(&ad_request, &RequestCachePolicy::default())
            .unwrap();
        assert_eq!(response2, expected);
    }

    #[test]
    fn default_client_uses_prod_url() {
        let client = MARSClient::new(Environment::Prod, None);
        assert_eq!(
            client.get_mars_endpoint().as_str(),
            "https://ads.mozilla.org/v1/"
        );
    }

    #[test]
    fn test_record_click_makes_callback_request() {
        viaduct_dev::init_backend_dev();
        let cache = HttpCache::builder("test_record_click.db")
            .default_ttl(std::time::Duration::from_secs(300))
            .max_size(crate::http_cache::ByteSize::mib(1))
            .build()
            .unwrap();

        let client = MARSClient::new(Environment::Test, Some(cache));

        let callback_url = Url::parse(&format!("{}/click", mockito::server_url())).unwrap();

        let _m = mock("GET", "/click").with_status(200).create();

        let result = client.record_click(callback_url);
        assert!(result.is_ok());
    }

    #[test]
    fn test_record_impression_makes_callback_request() {
        viaduct_dev::init_backend_dev();
        let cache = HttpCache::builder("test_record_impression.db")
            .default_ttl(std::time::Duration::from_secs(300))
            .max_size(crate::http_cache::ByteSize::mib(1))
            .build()
            .unwrap();

        let client = MARSClient::new(Environment::Test, Some(cache));

        let callback_url = Url::parse(&format!("{}/impression", mockito::server_url())).unwrap();

        let _m = mock("GET", "/impression").with_status(200).create();

        let result = client.record_impression(callback_url);
        assert!(result.is_ok());
    }
}
