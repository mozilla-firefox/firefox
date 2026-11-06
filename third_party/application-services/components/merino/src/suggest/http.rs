/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use url::Url;
use viaduct::{Client, ClientSettings, Request, Response};

use super::error::{Error, Result};

pub struct HttpClient;

#[derive(Default)]
pub struct SuggestQueryParams<'a> {
    pub providers: Option<&'a str>,
    pub source: Option<&'a str>,
    pub country: Option<&'a str>,
    pub region: Option<&'a str>,
    pub city: Option<&'a str>,
    pub client_variants: Option<&'a str>,
    pub request_type: Option<&'a str>,
    pub accept_language: Option<&'a str>,
    pub session_id: Option<&'a str>,
    pub sequence_number: Option<u32>,
}

pub trait HttpClientTrait {
    fn make_suggest_request(
        &self,
        query: &str,
        options: SuggestQueryParams<'_>,
        endpoint_url: Url,
    ) -> Result<Option<Response>>;
}

fn build_suggest_url(query: &str, options: &SuggestQueryParams<'_>, endpoint_url: Url) -> Url {
    let mut url = endpoint_url;
    {
        let mut params = url.query_pairs_mut();
        params.append_pair("q", query);

        if let Some(v) = options.country {
            params.append_pair("country", v);
        }
        if let Some(v) = options.region {
            params.append_pair("region", v);
        }
        if let Some(v) = options.city {
            params.append_pair("city", v);
        }
        if let Some(v) = options.source {
            params.append_pair("source", v);
        }
        if let Some(v) = options.providers {
            params.append_pair("providers", v);
        }
        if let Some(v) = options.client_variants {
            params.append_pair("client_variants", v);
        }
        if let Some(v) = options.request_type {
            params.append_pair("request_type", v);
        }
        if let Some(v) = options.session_id {
            params.append_pair("sid", v);
        }
        if let Some(v) = options.sequence_number {
            params.append_pair("seq", &v.to_string());
        }
    }
    url
}

impl HttpClientTrait for HttpClient {
    fn make_suggest_request(
        &self,
        query: &str,
        options: SuggestQueryParams<'_>,
        endpoint_url: Url,
    ) -> Result<Option<Response>> {
        let url = build_suggest_url(query, &options, endpoint_url);

        let client = Client::with_ohttp_channel("merino", ClientSettings::default())?;

        let mut request = Request::get(url);
        request = request.header("accept", "application/json")?;

        if let Some(lang) = options.accept_language {
            request = request.header("accept-language", lang)?;
        }

        let response = client.send_sync(request)?;
        let status = response.status;
        match status {
            200 => Ok(Some(response)),
            204 => Ok(None),
            400 => Err(Error::BadRequest {
                code: status,
                message: response.text().to_string(),
            }),
            422 => Err(Error::Validation {
                code: status,
                message: response.text().to_string(),
            }),
            500..=599 => Err(Error::Server {
                code: status,
                message: response.text().to_string(),
            }),
            _ => Err(Error::Unexpected {
                code: status,
                message: response.text().to_string(),
            }),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const BASE_URL: &str = "https://merino.services.mozilla.com/api/v1/suggest";

    fn base_url() -> Url {
        Url::parse(BASE_URL).unwrap()
    }

    fn has_param(url: &Url, key: &str, value: &str) -> bool {
        url.query_pairs().any(|(k, v)| k == key && v == value)
    }

    #[test]
    fn test_build_suggest_url_sets_query() {
        let url = build_suggest_url("apple", &SuggestQueryParams::default(), base_url());
        assert!(has_param(&url, "q", "apple"));
    }

    #[test]
    fn test_build_suggest_url_with_geo_options() {
        let options = SuggestQueryParams {
            country: Some("US"),
            region: Some("CA"),
            city: Some("San Francisco"),
            ..SuggestQueryParams::default()
        };
        let url = build_suggest_url("coffee", &options, base_url());
        assert!(has_param(&url, "q", "coffee"));
        assert!(has_param(&url, "country", "US"));
        assert!(has_param(&url, "region", "CA"));
        assert!(has_param(&url, "city", "San Francisco"));
    }

    #[test]
    fn test_build_suggest_url_with_all_options() {
        let options = SuggestQueryParams {
            providers: Some("accuweather"),
            source: Some("urlbar"),
            country: Some("US"),
            region: Some("NY"),
            city: Some("New York"),
            client_variants: Some("control"),
            request_type: Some("weather"),
            accept_language: Some("en-US"),
            session_id: Some("6f3c2a1e-0000-4000-8000-000000000000"),
            sequence_number: Some(2),
        };
        let url = build_suggest_url("new york", &options, base_url());
        assert!(has_param(&url, "q", "new york"));
        assert!(has_param(&url, "providers", "accuweather"));
        assert!(has_param(&url, "source", "urlbar"));
        assert!(has_param(&url, "country", "US"));
        assert!(has_param(&url, "region", "NY"));
        assert!(has_param(&url, "city", "New York"));
        assert!(has_param(&url, "client_variants", "control"));
        assert!(has_param(&url, "request_type", "weather"));
        assert!(has_param(
            &url,
            "sid",
            "6f3c2a1e-0000-4000-8000-000000000000"
        ));
        assert!(has_param(&url, "seq", "2"));
    }

    #[test]
    fn test_build_suggest_url_omits_none_options() {
        let url = build_suggest_url("apple", &SuggestQueryParams::default(), base_url());
        let keys: Vec<_> = url.query_pairs().map(|(k, _)| k.into_owned()).collect();
        assert_eq!(keys, vec!["q"]);
    }

    #[test]
    fn test_build_suggest_url_full_string() {
        let options = SuggestQueryParams {
            country: Some("US"),
            region: Some("CA"),
            city: Some("San Francisco"),
            source: None,
            providers: Some("wikipedia"),
            client_variants: Some("control"),
            request_type: None,
            accept_language: None,
            session_id: Some("6f3c2a1e-0000-4000-8000-000000000000"),
            sequence_number: Some(0),
        };
        let url = build_suggest_url("apple", &options, base_url());
        assert_eq!(
            url.as_str(),
            "https://merino.services.mozilla.com/api/v1/suggest\
            ?q=apple\
            &country=US\
            &region=CA\
            &city=San+Francisco\
            &providers=wikipedia\
            &client_variants=control\
            &sid=6f3c2a1e-0000-4000-8000-000000000000\
            &seq=0"
        );
    }
}
