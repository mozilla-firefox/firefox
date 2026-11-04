/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::collections::hash_map::DefaultHasher;
use std::hash::{Hash, Hasher};

use url::Url;

#[derive(Clone, Debug, PartialEq)]
pub struct RequestHash(String);

impl RequestHash {
    pub fn new(value: &impl Hash) -> Self {
        let mut hasher = DefaultHasher::new();
        value.hash(&mut hasher);
        RequestHash(format!("{:x}", hasher.finish()))
    }

    /// Takes the `request_hash` query parameter out of `url`, leaving the other
    /// parameters in place.
    pub fn pop_from_url(url: &mut Url) -> Option<Self> {
        let mut request_hash = None;
        let mut query = url::form_urlencoded::Serializer::new(String::new());

        for (key, value) in url.query_pairs() {
            if key == "request_hash" {
                request_hash = Some(RequestHash::from(value.as_ref()));
            } else {
                query.append_pair(&key, &value);
            }
        }

        let query_string = query.finish();
        if query_string.is_empty() {
            url.set_query(None);
        } else {
            url.set_query(Some(&query_string));
        }
        request_hash
    }
}

impl From<&str> for RequestHash {
    fn from(s: &str) -> Self {
        RequestHash(s.to_owned())
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

    #[test]
    fn test_same_value_produces_same_hash() {
        let hash1 = RequestHash::new(&("GET", "https://example.com/api"));
        let hash2 = RequestHash::new(&("GET", "https://example.com/api"));
        assert_eq!(hash1, hash2);
    }

    #[test]
    fn test_different_values_produce_different_hashes() {
        let hash1 = RequestHash::new(&("GET", "https://example.com/api1"));
        let hash2 = RequestHash::new(&("GET", "https://example.com/api2"));
        assert_ne!(hash1, hash2);
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

    #[test]
    fn pop_from_url_takes_the_hash_and_keeps_other_params() {
        let mut url_with_hash =
            Url::parse("https://example.com/callback?request_hash=abc123def456&other=param")
                .unwrap();
        let extracted = RequestHash::pop_from_url(&mut url_with_hash);
        assert_eq!(extracted, Some(RequestHash::from("abc123def456")));
        assert_eq!(url_with_hash.query(), Some("other=param"));

        let mut url_without_hash = Url::parse("https://example.com/callback?other=param").unwrap();
        let extracted_none = RequestHash::pop_from_url(&mut url_without_hash);
        assert_eq!(extracted_none, None);
        assert_eq!(url_without_hash.query(), Some("other=param"));

        let mut url_no_query = Url::parse("https://example.com/callback").unwrap();
        let extracted_empty = RequestHash::pop_from_url(&mut url_no_query);
        assert_eq!(extracted_empty, None);
        assert_eq!(url_no_query.query(), None);
    }
}
