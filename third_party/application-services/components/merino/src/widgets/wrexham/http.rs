/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use url::Url;
use viaduct::{Request, Response};

use super::error::{Error, Result};

pub struct HttpClient;

pub trait HttpClientTrait {
    fn make_request(&self, url: Url) -> Result<Response>;
}

/// Adds the optional `limit` query parameter to the matches endpoint URL.
pub fn build_matches_url(endpoint_url: Url, limit: Option<u32>) -> Url {
    let mut url = endpoint_url;
    if let Some(limit) = limit {
        url.query_pairs_mut()
            .append_pair("limit", &limit.to_string());
    }
    url
}

/// Returns the response if it succeeded, or the error for its status code.
pub fn check_status(response: Response) -> Result<Response> {
    let status = response.status;
    match status {
        200 => Ok(response),
        400 => Err(Error::BadRequest {
            code: status,
            message: response.text().to_string(),
        }),
        503 => Err(Error::Unavailable {
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

impl HttpClientTrait for HttpClient {
    fn make_request(&self, url: Url) -> Result<Response> {
        let response = Request::get(url)
            .header("accept", "application/json")?
            .send()?;
        check_status(response)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use viaduct::{Headers, Method};

    const MATCHES_URL: &str = "https://merino.services.mozilla.com/api/v1/widgets/wrexham/matches";

    fn response(status: u16, body: &str) -> Response {
        Response {
            request_method: Method::Get,
            url: Url::parse(MATCHES_URL).unwrap(),
            status,
            headers: Headers::new(),
            body: body.as_bytes().to_vec(),
        }
    }

    #[test]
    fn test_build_matches_url_with_limit() {
        let url = build_matches_url(Url::parse(MATCHES_URL).unwrap(), Some(3));
        assert_eq!(url.as_str(), format!("{MATCHES_URL}?limit=3"));
    }

    #[test]
    fn test_build_matches_url_without_limit() {
        let url = build_matches_url(Url::parse(MATCHES_URL).unwrap(), None);
        assert_eq!(url.as_str(), MATCHES_URL);
    }

    #[test]
    fn test_check_status_success() {
        let response = check_status(response(200, "{}")).unwrap();
        assert_eq!(response.text(), "{}");
    }

    #[test]
    fn test_check_status_errors() {
        assert!(matches!(
            check_status(response(400, "bad limit")),
            Err(Error::BadRequest { code: 400, .. })
        ));
        assert!(matches!(
            check_status(response(503, "unavailable")),
            Err(Error::Unavailable { code: 503, .. })
        ));
        assert!(matches!(
            check_status(response(500, "boom")),
            Err(Error::Server { code: 500, .. })
        ));
        assert!(matches!(
            check_status(response(404, "not found")),
            Err(Error::Unexpected { code: 404, .. })
        ));
    }
}
