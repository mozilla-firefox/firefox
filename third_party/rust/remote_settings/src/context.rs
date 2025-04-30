/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use serde_json::{Map, Value};
use std::collections::HashMap;

/// Remote settings context object
///
/// This is used to filter the records returned. We always fetch all `records` from the
/// remote-settings storage. Some records could have a `filter_expression`.  If this is passed in
/// and the record has a `filter_expression`, then only returns where the expression is true will
/// be returned.
///
/// See https://remote-settings.readthedocs.io/en/latest/target-filters.html for details.
#[derive(Debug, Clone, Default, uniffi::Record)]
pub struct RemoteSettingsContext {
    /// The delivery channel of the application (e.g "nightly")
    #[uniffi(default = None)]
    pub channel: Option<String>,
    /// User visible version string (e.g. "1.0.3")
    #[uniffi(default = None)]
    pub app_version: Option<String>,
    /// String containing the XUL application app_id
    #[uniffi(default = None)]
    pub app_id: Option<String>,
    /// The locale of the application during initialization (e.g. "es-ES")
    #[uniffi(default = None)]
    pub locale: Option<String>,
    /// The name of the operating system (e.g. "Android", "iOS", "Darwin", "WINNT")
    #[uniffi(default = None)]
    pub os: Option<String>,
    /// The user-visible version of the operating system (e.g. "1.2.3")
    #[uniffi(default = None)]
    pub os_version: Option<String>,
    /// Form-factor of the device ("phone", "tablet", or "desktop")
    #[uniffi(default = None)]
    pub form_factor: Option<String>,
    /// Country of the user.
    ///
    /// This is usually populated in one of two ways:
    ///   - The second component of the locale
    ///   - By using a geolocation service, which determines country via the user's IP.
    ///     Firefox apps usually have a module that integrates with these services,
    ///     for example `Region` on Desktop and `RegionMiddleware` on Android.
    #[uniffi(default = None)]
    pub country: Option<String>,
    /// Extra attributes to add to the env for JEXL filtering.
    ///
    /// Use this for prototyping / testing new features.  In the long-term, new fields should be
    /// added to the official list and supported by both the Rust and Gecko clients.
    #[uniffi(default = None)]
    pub custom_targetting_attributes: Option<HashMap<String, String>>,
}

impl RemoteSettingsContext {
    /// Convert this into the `env` value for the remote settings JEXL filter
    ///
    /// https://remote-settings.readthedocs.io/en/latest/target-filters.html
    pub(crate) fn into_env(self) -> Value {
        let mut v = Map::new();
        v.insert("channel".to_string(), self.channel.into());
        if let Some(version) = self.app_version {
            v.insert("version".to_string(), version.into());
        }
        if let Some(locale) = self.locale {
            v.insert("locale".to_string(), locale.into());
        }
        if self.app_id.is_some() || self.os.is_some() || self.os_version.is_some() {
            let mut appinfo = Map::default();
            if let Some(app_id) = self.app_id {
                appinfo.insert("ID".to_string(), app_id.into());
            }
            // The "os" object is the new way to represent OS-related data
            if self.os.is_some() || self.os_version.is_some() {
                let mut os = Map::default();
                if let Some(os_name) = &self.os {
                    os.insert("name".to_string(), os_name.to_string().into());
                }
                if let Some(os_version) = self.os_version {
                    os.insert("version".to_string(), os_version.into());
                }
                appinfo.insert("os".to_string(), os.into());
            }
            // The "OS" string is for backwards compatibility
            if let Some(os_name) = self.os {
                appinfo.insert("OS".to_string(), os_name.into());
            }
            v.insert("appinfo".to_string(), appinfo.into());
        }
        if let Some(form_factor) = self.form_factor {
            v.insert("formFactor".to_string(), form_factor.into());
        }
        if let Some(country) = self.country {
            v.insert("country".to_string(), country.into());
        }
        if let Some(custom) = self.custom_targetting_attributes {
            v.extend(custom.into_iter().map(|(k, v)| (k, v.into())));
        }
        v.into()
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use serde_json::json;

    /// Test that the remote settings context is normalized to match
    /// https://remote-settings.readthedocs.io/en/latest/target-filters.html, regardless of what
    /// the fields are named in Rust.
    #[test]
    fn test_context_normalization() {
        let context = RemoteSettingsContext {
            channel: Some("beta".into()),
            app_id: Some("{aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee}".into()),
            app_version: Some("1.0.0".into()),
            os: Some("MS-DOS".into()),
            os_version: Some("6.1".into()),
            locale: Some("en-US".into()),
            form_factor: Some("tablet".into()),
            country: Some("US".into()),
            custom_targetting_attributes: Some(HashMap::from([("extra".into(), "test".into())])),
        };
        assert_eq!(
            context.into_env(),
            json!({
                // Official fields
                "version": "1.0.0",
                "channel": "beta",
                "locale": "en-US",
                "appinfo": {
                    "ID": "{aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee}",
                    "OS": "MS-DOS",
                    "os": {
                        "name": "MS-DOS",
                        "version": "6.1"
                    }
                },
                // Unofficial fields that we need for Suggest geo-expansion.  These should be made
                // into official fields that both the Gecko and Rust client support.
                "formFactor": "tablet",
                "country": "US",
                "extra": "test",
            })
        );
    }
}
