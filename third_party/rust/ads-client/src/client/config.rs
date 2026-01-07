/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

use once_cell::sync::Lazy;
use url::Url;

static MARS_API_ENDPOINT_PROD: Lazy<Url> =
    Lazy::new(|| Url::parse("https://ads.mozilla.org/v1/").expect("hardcoded URL must be valid"));

#[cfg(feature = "dev")]
static MARS_API_ENDPOINT_STAGING: Lazy<Url> =
    Lazy::new(|| Url::parse("https://ads.allizom.org/v1/").expect("hardcoded URL must be valid"));

#[derive(Default)]
pub struct AdsClientConfig {
    pub environment: Environment,
    pub cache_config: Option<AdsCacheConfig>,
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub enum Environment {
    #[default]
    Prod,
    #[cfg(feature = "dev")]
    Staging,
    #[cfg(test)]
    Test,
}

impl Environment {
    pub fn into_mars_url(self) -> Url {
        match self {
            Environment::Prod => MARS_API_ENDPOINT_PROD.clone(),
            #[cfg(feature = "dev")]
            Environment::Staging => MARS_API_ENDPOINT_STAGING.clone(),
            #[cfg(test)]
            Environment::Test => Url::parse(&mockito::server_url()).unwrap(),
        }
    }
}

#[derive(Clone, Debug)]
pub struct AdsCacheConfig {
    pub db_path: String,
    pub default_cache_ttl_seconds: Option<u64>,
    pub max_size_mib: Option<u64>,
}

#[cfg(test)]
mod tests {
    use url::Host;

    use super::*;

    #[test]
    fn prod_endpoint_parses_and_is_expected() {
        let url = Environment::Prod.into_mars_url();

        assert_eq!(url.as_str(), "https://ads.mozilla.org/v1/");

        assert_eq!(url.scheme(), "https");
        assert_eq!(url.host(), Some(Host::Domain("ads.mozilla.org")));
        assert_eq!(url.path(), "/v1/");

        let url2 = Environment::Prod.into_mars_url();
        assert!(url == url2);
    }
}
