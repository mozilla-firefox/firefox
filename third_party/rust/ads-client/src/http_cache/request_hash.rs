/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::collections::hash_map::DefaultHasher;
use std::hash::{Hash, Hasher};
use viaduct::Request;

#[derive(Clone, Debug, PartialEq)]
pub struct RequestHash(String);

impl From<&Request> for RequestHash {
    fn from(request: &Request) -> Self {
        let mut hasher = DefaultHasher::new();
        request.method.hash(&mut hasher);
        request.url.hash(&mut hasher);

        let mut headers: Vec<_> = request.headers.clone().into_iter().collect();
        headers.sort_by_key(|header| header.name.to_ascii_lowercase());
        for header in headers {
            header.name.hash(&mut hasher);
            header.value.hash(&mut hasher);
        }

        request.body.hash(&mut hasher);
        RequestHash(format!("{:x}", hasher.finish()))
    }
}

impl From<&str> for RequestHash {
    fn from(s: &str) -> Self {
        RequestHash(s.to_string())
    }
}

impl From<String> for RequestHash {
    fn from(s: String) -> Self {
        RequestHash(s)
    }
}

impl std::fmt::Display for RequestHash {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.0)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use viaduct::{Headers, Method, Request};

    fn create_test_request(url: &str, body: &[u8]) -> Request {
        Request {
            method: Method::Get,
            url: url.parse().unwrap(),
            headers: Headers::new(),
            body: Some(body.to_vec()),
        }
    }

    #[test]
    fn test_request_hashing() {
        let request1 = create_test_request("https://example.com/api1", b"body1");
        let request2 = create_test_request("https://example.com/api2", b"body2");
        let request3 = create_test_request("https://example.com/api", b"body");

        let hash1 = RequestHash::from(&request1);
        let hash2 = RequestHash::from(&request2);
        let hash3 = RequestHash::from(&request3);

        assert_ne!(hash1.to_string(), hash2.to_string());
        assert_ne!(hash1.to_string(), hash3.to_string());
        assert_ne!(hash2.to_string(), hash3.to_string());

        let same_request = create_test_request("https://example.com/api", b"body");
        let hash4 = RequestHash::from(&same_request);
        assert_eq!(hash3.to_string(), hash4.to_string());
    }

    #[test]
    fn test_request_hashing_header_order_and_case() {
        let base_url = "https://example.com/api";
        let body = b"body";

        let req_base = create_test_request(base_url, body);
        let mut h1 = Headers::new();
        h1.insert("Accept", "text/plain").unwrap();
        h1.insert("X-Test", "1").unwrap();
        let req1 = Request {
            headers: h1,
            ..req_base.clone()
        };

        let req_base = create_test_request(base_url, body);
        let mut h2 = Headers::new();
        h2.insert("X-Test", "1").unwrap();
        h2.insert("Accept", "text/plain").unwrap();
        let req2 = Request {
            headers: h2,
            ..req_base.clone()
        };

        let req_base = create_test_request(base_url, body);
        let mut h3 = Headers::new();
        h3.insert("accept", "text/plain").unwrap();
        h3.insert("x-test", "1").unwrap();
        let req3 = Request {
            headers: h3,
            ..req_base
        };

        let h_req1 = RequestHash::from(&req1);
        let h_req2 = RequestHash::from(&req2);
        let h_req3 = RequestHash::from(&req3);

        assert_eq!(h_req1.to_string(), h_req2.to_string());
        assert_eq!(h_req1.to_string(), h_req3.to_string());
    }

    #[test]
    fn test_request_hash_from_string() {
        let hash_str = "abc123def456";
        let hash = RequestHash::from(hash_str);
        assert_eq!(hash.to_string(), hash_str);

        let hash_string = String::from("xyz789");
        let hash2 = RequestHash::from(hash_string);
        assert_eq!(hash2.to_string(), "xyz789");
    }
}
