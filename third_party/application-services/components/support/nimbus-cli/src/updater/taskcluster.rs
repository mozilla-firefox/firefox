// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use serde::de::DeserializeOwned;
use std::time::Duration;
use update_informer::{
    http_client::{GenericHttpClient, HeaderMap, HttpClient},
    Check, Package, Registry, Result,
};

#[derive(serde::Deserialize)]
struct Response {
    version: String,
}

struct TaskClusterRegistry;

impl Registry for TaskClusterRegistry {
    const NAME: &'static str = "taskcluster";

    fn get_latest_version<T: HttpClient>(
        http_client: GenericHttpClient<T>,
        pkg: &Package,
    ) -> Result<Option<String>> {
        let name = pkg.to_string();
        let url = format!("https://firefox-ci-tc.services.mozilla.com/api/index/v1/task/project.application-services.v2.{name}.latest/artifacts/public%2Fbuild%2F{name}.json");
        let resp = http_client.get::<Response>(&url)?;
        Ok(Some(resp.version))
    }
}

#[allow(dead_code)]
pub struct GunzippingHttpClient;

impl HttpClient for GunzippingHttpClient {
    fn get<T: DeserializeOwned>(url: &str, _timeout: Duration, headers: HeaderMap) -> Result<T> {
        let mut req = viaduct::Request::get(viaduct::parse_url(url)?);

        for (key, value) in headers {
            req = req.header(key.to_string(), value.to_string())?;
        }

        let json = req.send()?.json()?;

        Ok(json)
    }
}

/// Check the specifically crafted JSON file for this package to see if there has been a change in version.
/// This is done every hour.
pub(crate) fn check_taskcluster_for_update() -> Option<(String, String)> {
    let name = env!("CARGO_PKG_NAME");
    let version = env!("CARGO_PKG_VERSION");
    let interval = Duration::from_secs(60 * 60);

    #[cfg(not(test))]
    let informer = update_informer::new(TaskClusterRegistry, name, version)
        .http_client(GunzippingHttpClient)
        .interval(interval);

    #[cfg(test)]
    let informer =
        update_informer::fake(TaskClusterRegistry, name, version, "1.0.0").interval(interval);

    if let Ok(Some(new_version)) = informer.check_version() {
        Some((format!("v{version}"), new_version.to_string()))
    } else {
        None
    }
}
