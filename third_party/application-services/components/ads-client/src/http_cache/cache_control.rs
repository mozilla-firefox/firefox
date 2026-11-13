/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use serde::{Deserialize, Serialize};
use std::time::Duration;
use viaduct::{header_names, Response};

#[derive(Clone, Debug, Deserialize, Serialize)]
pub(super) struct CacheControl {
    pub max_age: Option<u64>,
    pub must_revalidate: bool,
    pub no_cache: bool,
    pub no_store: bool,
    pub private: bool,
}

impl From<&Response> for CacheControl {
    fn from(response: &Response) -> Self {
        let mut cache_control = Self {
            max_age: None,
            must_revalidate: false,
            no_cache: false,
            no_store: false,
            private: false,
        };

        if let Some(header) = response.headers.get(header_names::CACHE_CONTROL) {
            for directive in header.split(',').map(str::trim) {
                match directive {
                    "must-revalidate" => cache_control.must_revalidate = true,
                    "no-cache" => cache_control.no_cache = true,
                    "no-store" => cache_control.no_store = true,
                    "private" => cache_control.private = true,
                    s if s.starts_with("max-age=") => {
                        if let Some(age_str) = s.strip_prefix("max-age=") {
                            cache_control.max_age = age_str.parse().ok();
                        }
                    }
                    _ => {}
                }
            }
        }

        cache_control
    }
}

impl CacheControl {
    /// How long `max-age` keeps a response of this `age` fresh: zero once it
    /// is stale, `None` without `max-age`.
    pub(super) fn time_to_live(&self, age: Duration) -> Option<Duration> {
        self.max_age
            .map(|max_age| Duration::from_secs(max_age).saturating_sub(age))
    }

    pub(super) fn should_cache(&self) -> bool {
        !self.no_store
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use viaduct::{Headers, Method};

    fn from_header(header: Option<&str>) -> CacheControl {
        let mut headers = Headers::new();
        if let Some(header) = header {
            headers.insert(header_names::CACHE_CONTROL, header).unwrap();
        }
        CacheControl::from(&Response {
            body: b"".to_vec(),
            headers,
            request_method: Method::Get,
            status: 200,
            url: "https://example.com".parse().unwrap(),
        })
    }

    #[test]
    fn time_to_live_is_max_age_minus_age() {
        let cache_control = from_header(Some("max-age=86400"));
        assert_eq!(
            cache_control.time_to_live(Duration::ZERO),
            Some(Duration::from_secs(86400))
        );
        assert_eq!(
            cache_control.time_to_live(Duration::from_secs(72000)),
            Some(Duration::from_secs(14400))
        );
    }

    #[test]
    fn time_to_live_is_zero_once_stale() {
        let cache_control = from_header(Some("max-age=3600"));
        for age in [3600, 7200] {
            assert_eq!(
                cache_control.time_to_live(Duration::from_secs(age)),
                Some(Duration::ZERO)
            );
        }
    }

    #[test]
    fn time_to_live_needs_max_age() {
        assert_eq!(
            from_header(Some("no-cache")).time_to_live(Duration::ZERO),
            None
        );
    }

    #[test]
    fn test_cache_control_parsing() {
        // Test max-age
        let directives = from_header(Some("max-age=3600"));
        assert_eq!(directives.max_age, Some(3600));
        assert!(!directives.no_cache);
        assert!(!directives.no_store);

        // Test no-cache and no-store
        let directives = from_header(Some("no-cache, no-store"));
        assert!(directives.no_cache);
        assert!(directives.no_store);

        // Test multiple directives
        let directives = from_header(Some("max-age=1800, must-revalidate, private"));
        assert_eq!(directives.max_age, Some(1800));
        assert!(directives.must_revalidate);
        assert!(directives.private);

        // Test empty header
        let directives = from_header(None);
        assert_eq!(directives.max_age, None);
        assert!(!directives.no_cache);
        assert!(!directives.no_store);
        assert!(directives.should_cache());
    }
}
