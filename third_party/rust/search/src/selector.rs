/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! This module defines the main `SearchEngineSelector`.

use crate::configuration_overrides_types::JSONOverridesRecord;
use crate::configuration_overrides_types::JSONSearchConfigurationOverrides;
use crate::filter::filter_engine_configuration_impl;
use crate::{
    error::Error, JSONSearchConfiguration, RefinedSearchConfig, SearchApiResult,
    SearchUserEnvironment,
};
use error_support::handle_error;
use parking_lot::Mutex;
use remote_settings::{RemoteSettingsClient, RemoteSettingsService};
use std::sync::Arc;

#[derive(Default)]
pub(crate) struct SearchEngineSelectorInner {
    configuration: Option<JSONSearchConfiguration>,
    configuration_overrides: Option<JSONSearchConfigurationOverrides>,
    search_config_client: Option<Arc<RemoteSettingsClient>>,
    search_config_overrides_client: Option<Arc<RemoteSettingsClient>>,
}

/// SearchEngineSelector parses the JSON configuration for
/// search engines and returns the applicable engines depending
/// on their region + locale.
#[derive(Default, uniffi::Object)]
pub struct SearchEngineSelector(Mutex<SearchEngineSelectorInner>);

#[uniffi::export]
impl SearchEngineSelector {
    #[uniffi::constructor]
    pub fn new() -> Self {
        Self(Mutex::default())
    }

    /// Sets the RemoteSettingsService to use. The selector will create the
    /// relevant remote settings client(s) from the service.
    ///
    /// # Params:
    ///   - `service`: The remote settings service instance for the application.
    ///   - `options`: The remote settings options to be passed to the client(s).
    ///   - `apply_engine_overrides`: Whether or not to apply overrides from
    ///                               `search-config-v2-overrides` to the selected
    ///                               engines. Should be false unless the application
    ///                               supports the click URL feature.
    pub fn use_remote_settings_server(
        self: Arc<Self>,
        service: &Arc<RemoteSettingsService>,
        apply_engine_overrides: bool,
    ) {
        let mut inner = self.0.lock();
        inner.search_config_client = Some(service.make_client("search-config-v2".to_string()));

        if apply_engine_overrides {
            inner.search_config_overrides_client =
                Some(service.make_client("search-config-overrides-v2".to_string()));
        }
    }

    /// Sets the search configuration from the given string. If the configuration
    /// string is unchanged since the last update, the cached configuration is
    /// reused to avoid unnecessary reprocessing. This helps optimize performance,
    /// particularly during test runs where the same configuration may be used
    /// repeatedly.
    #[handle_error(Error)]
    pub fn set_search_config(self: Arc<Self>, configuration: String) -> SearchApiResult<()> {
        if configuration.is_empty() {
            return Err(Error::SearchConfigNotSpecified);
        }
        self.0.lock().configuration = serde_json::from_str(&configuration)?;
        Ok(())
    }

    #[handle_error(Error)]
    pub fn set_config_overrides(self: Arc<Self>, overrides: String) -> SearchApiResult<()> {
        if overrides.is_empty() {
            return Err(Error::SearchConfigOverridesNotSpecified);
        }
        self.0.lock().configuration_overrides = serde_json::from_str(&overrides)?;
        Ok(())
    }

    /// Clears the search configuration from memory if it is known that it is
    /// not required for a time, e.g. if the configuration will only be re-filtered
    /// after an app/environment update.
    pub fn clear_search_config(self: Arc<Self>) {}

    /// Filters the search configuration with the user's given environment,
    /// and returns the set of engines and parameters that should be presented
    /// to the user.
    #[handle_error(Error)]
    pub fn filter_engine_configuration(
        self: Arc<Self>,
        user_environment: SearchUserEnvironment,
    ) -> SearchApiResult<RefinedSearchConfig> {
        let inner = self.0.lock();
        if let Some(client) = &inner.search_config_client {
            // Remote settings ships dumps of the collections, so it is highly
            // unlikely that we'll ever hit the case where we have no records.
            // However, just in case of an issue that does causes us to receive
            // no records, we will raise an error so that the application can
            // handle or record it appropriately.
            let records = client.get_records(false);

            if let Some(records) = records {
                if records.is_empty() {
                    return Err(Error::SearchConfigNoRecords);
                }

                if let Some(overrides_client) = &inner.search_config_overrides_client {
                    let overrides_records = overrides_client.get_records(false);

                    if let Some(overrides_records) = overrides_records {
                        if overrides_records.is_empty() {
                            return filter_engine_configuration_impl(
                                user_environment,
                                &records,
                                None,
                            );
                        }
                        // TODO: Bug 1947241 - Find a way to avoid having to serialise the records
                        // back to strings and then deserialise them into the records that we want.
                        let stringified = serde_json::to_string(&overrides_records)?;
                        let json_overrides: Vec<JSONOverridesRecord> =
                            serde_json::from_str(&stringified)?;

                        return filter_engine_configuration_impl(
                            user_environment,
                            &records,
                            Some(json_overrides),
                        );
                    } else {
                        return Err(Error::SearchConfigOverridesNoRecords);
                    }
                }

                return filter_engine_configuration_impl(user_environment, &records, None);
            } else {
                return Err(Error::SearchConfigNoRecords);
            }
        }
        let config = match &inner.configuration {
            None => return Err(Error::SearchConfigNotSpecified),
            Some(configuration) => configuration.data.clone(),
        };

        let config_overrides = match &inner.configuration_overrides {
            None => return Err(Error::SearchConfigOverridesNotSpecified),
            Some(overrides) => overrides.data.clone(),
        };
        return filter_engine_configuration_impl(user_environment, &config, Some(config_overrides));
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{types::*, SearchApiError};
    use env_logger;
    use mockito::mock;
    use pretty_assertions::assert_eq;
    use remote_settings::{RemoteSettingsConfig2, RemoteSettingsContext, RemoteSettingsServer};
    use serde_json::json;

    #[test]
    fn test_set_config_should_allow_basic_config() {
        let selector = Arc::new(SearchEngineSelector::new());

        let config_result = Arc::clone(&selector).set_search_config(
            json!({
              "data": [
                {
                  "recordType": "engine",
                  "identifier": "test",
                  "base": {
                    "name": "Test",
                    "classification": "general",
                    "urls": {
                      "search": {
                        "base": "https://example.com",
                        "method": "GET"
                      }
                    }
                  },
                  "variants": [{
                    "environment": {
                      "allRegionsAndLocales": true,
                      "excludedRegions": []
                    }
                  }],
                },
                {
                  "recordType": "defaultEngines",
                  "globalDefault": "test"
                }
              ]
            })
            .to_string(),
        );
        assert!(
            config_result.is_ok(),
            "Should have set the configuration successfully. {:?}",
            config_result
        );
    }

    #[test]
    fn test_set_config_should_allow_extra_fields() {
        let selector = Arc::new(SearchEngineSelector::new());

        let config_result = Arc::clone(&selector).set_search_config(
            json!({
              "data": [
                {
                  "recordType": "engine",
                  "identifier": "test",
                  "base": {
                    "name": "Test",
                    "classification": "general",
                    "urls": {
                      "search": {
                        "base": "https://example.com",
                        "method": "GET",
                        "extraField1": true
                      }
                    },
                    "extraField2": "123"
                  },
                  "variants": [{
                    "environment": {
                      "allRegionsAndLocales": true
                    }
                  }],
                  "extraField3": ["foo"]
                },
                {
                  "recordType": "defaultEngines",
                  "globalDefault": "test",
                  "extraField4": {
                    "subField1": true
                  }
                }
              ]
            })
            .to_string(),
        );
        assert!(
            config_result.is_ok(),
            "Should have set the configuration successfully with extra fields. {:?}",
            config_result
        );
    }

    #[test]
    fn test_set_config_should_ignore_unknown_record_types() {
        let selector = Arc::new(SearchEngineSelector::new());

        let config_result = Arc::clone(&selector).set_search_config(
            json!({
              "data": [
                {
                  "recordType": "engine",
                  "identifier": "test",
                  "base": {
                    "name": "Test",
                    "classification": "general",
                    "urls": {
                      "search": {
                        "base": "https://example.com",
                        "method": "GET"
                      }
                    }
                  },
                  "variants": [{
                    "environment": {
                      "allRegionsAndLocales": true
                    }
                  }],
                },
                {
                  "recordType": "defaultEngines",
                  "globalDefault": "test"
                },
                {
                  "recordType": "unknown"
                }
              ]
            })
            .to_string(),
        );
        assert!(
            config_result.is_ok(),
            "Should have set the configuration successfully with unknown record types. {:?}",
            config_result
        );
    }

    #[test]
    fn test_filter_engine_configuration_throws_without_config() {
        let selector = Arc::new(SearchEngineSelector::new());

        let result = selector.filter_engine_configuration(SearchUserEnvironment {
            ..Default::default()
        });

        assert!(
            result.is_err(),
            "Should throw an error when a configuration has not been specified before filtering"
        );
        assert!(result
            .unwrap_err()
            .to_string()
            .contains("Search configuration not specified"))
    }

    #[test]
    fn test_filter_engine_configuration_throws_without_config_overrides() {
        let selector = Arc::new(SearchEngineSelector::new());
        let _ = Arc::clone(&selector).set_search_config(
            json!({
              "data": [
                {
                  "recordType": "engine",
                  "identifier": "test",
                  "base": {
                    "name": "Test",
                    "classification": "general",
                    "urls": {
                      "search": {
                        "base": "https://example.com",
                        "method": "GET",
                      }
                    }
                  },
                  "variants": [{
                    "environment": {
                      "allRegionsAndLocales": true
                    }
                  }],
                },
              ]
            })
            .to_string(),
        );

        let result = selector.filter_engine_configuration(SearchUserEnvironment {
            ..Default::default()
        });

        assert!(
            result.is_err(),
            "Should throw an error when a configuration overrides has not been specified before filtering"
        );

        assert!(result
            .unwrap_err()
            .to_string()
            .contains("Search configuration overrides not specified"))
    }

    #[test]
    fn test_filter_engine_configuration_returns_basic_engines() {
        let selector = Arc::new(SearchEngineSelector::new());

        let config_overrides_result = Arc::clone(&selector).set_config_overrides(
            json!({
              "data": [
                {
                  "identifier": "overrides-engine",
                  "partnerCode": "overrides-partner-code",
                  "clickUrl": "https://example.com/click-url",
                  "telemetrySuffix": "overrides-telemetry-suffix",
                  "urls": {
                    "search": {
                      "base": "https://example.com/search-overrides",
                      "method": "GET",
                      "params": []
                    }
                  }
                }
              ]
            })
            .to_string(),
        );
        let config_result = Arc::clone(&selector).set_search_config(
            json!({
              "data": [
                {
                  "recordType": "engine",
                  "identifier": "test1",
                  "base": {
                    "name": "Test 1",
                    "classification": "general",
                    "urls": {
                      "search": {
                        "base": "https://example.com/1",
                        "method": "GET",
                        "params": [{
                          "name": "search-name",
                          "enterpriseValue": "enterprise-value",
                        }],
                        "searchTermParamName": "q"
                      },
                      "suggestions": {
                        "base": "https://example.com/suggestions",
                        "method": "POST",
                        "params": [{
                          "name": "suggestion-name",
                          "value": "suggestion-value",
                        }],
                        "searchTermParamName": "suggest"
                      },
                      "trending": {
                        "base": "https://example.com/trending",
                        "method": "GET",
                        "params": [{
                          "name": "trending-name",
                          "experimentConfig": "trending-experiment-value",
                        }]
                      },
                      "searchForm": {
                        "base": "https://example.com/search-form",
                        "method": "GET",
                        "params": [{
                          "name": "search-form-name",
                          "value": "search-form-value",
                        }]
                      }
                    }
                  },
                  "variants": [{
                    "environment": {
                      "allRegionsAndLocales": true
                    }
                  }],
                },
                {
                  "recordType": "engine",
                  "identifier": "test2",
                  "base": {
                    "name": "Test 2",
                    // No classification specified to test fallback.
                    "urls": {
                      "search": {
                        "base": "https://example.com/2",
                        "method": "GET",
                        "searchTermParamName": "search"
                      }
                    }
                  },
                  "variants": [{
                    "environment": {
                      "allRegionsAndLocales": true
                    }
                  }],
                },
                {
                  "recordType": "defaultEngines",
                  "globalDefault": "test1",
                  "globalDefaultPrivate": "test2"
                }
              ]
            })
            .to_string(),
        );
        assert!(
            config_result.is_ok(),
            "Should have set the configuration successfully. {:?}",
            config_result
        );
        assert!(
            config_overrides_result.is_ok(),
            "Should have set the configuration overrides successfully. {:?}",
            config_overrides_result
        );

        let result = selector.filter_engine_configuration(SearchUserEnvironment {
            ..Default::default()
        });

        assert!(
            result.is_ok(),
            "Should have filtered the configuration without error. {:?}",
            result
        );
        assert_eq!(
            result.unwrap(),
            RefinedSearchConfig {
                engines: vec!(
                    SearchEngineDefinition {
                        charset: "UTF-8".to_string(),
                        classification: SearchEngineClassification::General,
                        identifier: "test1".to_string(),
                        name: "Test 1".to_string(),
                        urls: SearchEngineUrls {
                            search: SearchEngineUrl {
                                base: "https://example.com/1".to_string(),
                                method: "GET".to_string(),
                                params: vec![SearchUrlParam {
                                    name: "search-name".to_string(),
                                    value: None,
                                    enterprise_value: Some("enterprise-value".to_string()),
                                    experiment_config: None
                                }],
                                search_term_param_name: Some("q".to_string())
                            },
                            suggestions: Some(SearchEngineUrl {
                                base: "https://example.com/suggestions".to_string(),
                                method: "POST".to_string(),
                                params: vec![SearchUrlParam {
                                    name: "suggestion-name".to_string(),
                                    value: Some("suggestion-value".to_string()),
                                    enterprise_value: None,
                                    experiment_config: None
                                }],
                                search_term_param_name: Some("suggest".to_string())
                            }),
                            trending: Some(SearchEngineUrl {
                                base: "https://example.com/trending".to_string(),
                                method: "GET".to_string(),
                                params: vec![SearchUrlParam {
                                    name: "trending-name".to_string(),
                                    value: None,
                                    enterprise_value: None,
                                    experiment_config: Some(
                                        "trending-experiment-value".to_string()
                                    )
                                }],
                                search_term_param_name: None
                            }),
                            search_form: Some(SearchEngineUrl {
                                base: "https://example.com/search-form".to_string(),
                                method: "GET".to_string(),
                                params: vec![SearchUrlParam {
                                    name: "search-form-name".to_string(),
                                    value: Some("search-form-value".to_string()),
                                    experiment_config: None,
                                    enterprise_value: None,
                                }],
                                search_term_param_name: None,
                            }),
                        },
                        ..Default::default()
                    },
                    SearchEngineDefinition {
                        aliases: Vec::new(),
                        charset: "UTF-8".to_string(),
                        classification: SearchEngineClassification::Unknown,
                        identifier: "test2".to_string(),
                        name: "Test 2".to_string(),
                        optional: false,
                        order_hint: None,
                        partner_code: String::new(),
                        telemetry_suffix: String::new(),
                        urls: SearchEngineUrls {
                            search: SearchEngineUrl {
                                base: "https://example.com/2".to_string(),
                                method: "GET".to_string(),
                                params: Vec::new(),
                                search_term_param_name: Some("search".to_string())
                            },
                            suggestions: None,
                            trending: None,
                            search_form: None
                        },
                        click_url: None,
                    }
                ),
                app_default_engine_id: Some("test1".to_string()),
                app_private_default_engine_id: Some("test2".to_string())
            }
        )
    }

    #[test]
    fn test_filter_engine_configuration_handles_basic_variants() {
        let selector = Arc::new(SearchEngineSelector::new());

        let config_overrides_result = Arc::clone(&selector).set_config_overrides(
            json!({
              "data": [
                {
                  "identifier": "overrides-engine",
                  "partnerCode": "overrides-partner-code",
                  "clickUrl": "https://example.com/click-url",
                  "telemetrySuffix": "overrides-telemetry-suffix",
                  "urls": {
                    "search": {
                      "base": "https://example.com/search-overrides",
                      "method": "GET",
                      "params": []
                    }
                  }
                }
              ]
            })
            .to_string(),
        );
        let config_result = Arc::clone(&selector).set_search_config(
            json!({
              "data": [
                {
                  "recordType": "engine",
                  "identifier": "test1",
                  "base": {
                    "name": "Test 1",
                    "classification": "general",
                    "partnerCode": "star",
                    "urls": {
                      "search": {
                        "base": "https://example.com/1",
                        "method": "GET",
                        "searchTermParamName": "q"
                      },
                      "suggestions": {
                        "base": "https://example.com/suggestions",
                        "method": "POST",
                        "params": [{
                          "name": "type",
                          "value": "space",
                        }],
                        "searchTermParamName": "suggest"
                      },
                      "trending": {
                        "base": "https://example.com/trending",
                        "method": "GET",
                        "params": [{
                          "name": "area",
                          "experimentConfig": "area-param",
                        }]
                      },
                      "searchForm": {
                        "base": "https://example.com/search-form",
                        "method": "GET",
                        "params": [{
                          "name": "search-form-name",
                          "value": "search-form-value",
                        }]
                      }
                    }
                  },
                  "variants": [{
                    "environment": {
                      "allRegionsAndLocales": true
                    },
                  },
                  {
                    "environment": {
                      "regions": ["FR"]
                    },
                    "urls": {
                      "search": {
                        "method": "POST",
                        "params": [{
                          "name": "mission",
                          "value": "ongoing"
                        }]
                      }
                    }
                  }],
                },
                {
                  "recordType": "engine",
                  "identifier": "test2",
                  "base": {
                    "name": "Test 2",
                    "classification": "general",
                    "urls": {
                      "search": {
                        "base": "https://example.com/2",
                        "method": "GET",
                        "searchTermParamName": "search"
                      }
                    }
                  },
                  "variants": [{
                    "environment": {
                      "allRegionsAndLocales": true
                    },
                    "partnerCode": "ship",
                    "telemetrySuffix": "E",
                    "optional": true
                  }],
                },
                {
                  "recordType": "defaultEngines",
                  "globalDefault": "test1",
                  "globalDefaultPrivate": "test2"
                }
              ]
            })
            .to_string(),
        );
        assert!(
            config_result.is_ok(),
            "Should have set the configuration successfully. {:?}",
            config_result
        );
        assert!(
            config_overrides_result.is_ok(),
            "Should have set the configuration overrides successfully. {:?}",
            config_overrides_result
        );

        let result = selector.filter_engine_configuration(SearchUserEnvironment {
            region: "FR".into(),
            ..Default::default()
        });

        assert!(
            result.is_ok(),
            "Should have filtered the configuration without error. {:?}",
            result
        );
        assert_eq!(
            result.unwrap(),
            RefinedSearchConfig {
                engines: vec!(
                    SearchEngineDefinition {
                        charset: "UTF-8".to_string(),
                        classification: SearchEngineClassification::General,
                        identifier: "test1".to_string(),
                        name: "Test 1".to_string(),
                        partner_code: "star".to_string(),
                        urls: SearchEngineUrls {
                            search: SearchEngineUrl {
                                base: "https://example.com/1".to_string(),
                                method: "POST".to_string(),
                                params: vec![SearchUrlParam {
                                    name: "mission".to_string(),
                                    value: Some("ongoing".to_string()),
                                    enterprise_value: None,
                                    experiment_config: None
                                }],
                                search_term_param_name: Some("q".to_string())
                            },
                            suggestions: Some(SearchEngineUrl {
                                base: "https://example.com/suggestions".to_string(),
                                method: "POST".to_string(),
                                params: vec![SearchUrlParam {
                                    name: "type".to_string(),
                                    value: Some("space".to_string()),
                                    enterprise_value: None,
                                    experiment_config: None
                                }],
                                search_term_param_name: Some("suggest".to_string())
                            }),
                            trending: Some(SearchEngineUrl {
                                base: "https://example.com/trending".to_string(),
                                method: "GET".to_string(),
                                params: vec![SearchUrlParam {
                                    name: "area".to_string(),
                                    value: None,
                                    enterprise_value: None,
                                    experiment_config: Some("area-param".to_string())
                                }],
                                search_term_param_name: None
                            }),
                            search_form: Some(SearchEngineUrl {
                                base: "https://example.com/search-form".to_string(),
                                method: "GET".to_string(),
                                params: vec![SearchUrlParam {
                                    name: "search-form-name".to_string(),
                                    value: Some("search-form-value".to_string()),
                                    enterprise_value: None,
                                    experiment_config: None,
                                }],
                                search_term_param_name: None,
                            }),
                        },
                        ..Default::default()
                    },
                    SearchEngineDefinition {
                        charset: "UTF-8".to_string(),
                        classification: SearchEngineClassification::General,
                        identifier: "test2".to_string(),
                        name: "Test 2".to_string(),
                        optional: true,
                        partner_code: "ship".to_string(),
                        telemetry_suffix: "E".to_string(),
                        urls: SearchEngineUrls {
                            search: SearchEngineUrl {
                                base: "https://example.com/2".to_string(),
                                method: "GET".to_string(),
                                params: Vec::new(),
                                search_term_param_name: Some("search".to_string())
                            },
                            ..Default::default()
                        },
                        ..Default::default()
                    }
                ),
                app_default_engine_id: Some("test1".to_string()),
                app_private_default_engine_id: Some("test2".to_string())
            }
        )
    }

    #[test]
    fn test_filter_engine_configuration_handles_basic_subvariants() {
        let selector = Arc::new(SearchEngineSelector::new());

        let config_overrides_result = Arc::clone(&selector).set_config_overrides(
            json!({
              "data": [
                {
                  "identifier": "overrides-engine",
                  "partnerCode": "overrides-partner-code",
                  "clickUrl": "https://example.com/click-url",
                  "telemetrySuffix": "overrides-telemetry-suffix",
                  "urls": {
                    "search": {
                      "base": "https://example.com/search-overrides",
                      "method": "GET",
                      "params": []
                    }
                  }
                }
              ]
            })
            .to_string(),
        );
        let config_result = Arc::clone(&selector).set_search_config(
            json!({
              "data": [
                {
                  "recordType": "engine",
                  "identifier": "test1",
                  "base": {
                    "name": "Test 1",
                    "partnerCode": "star",
                    "urls": {
                      "search": {
                        "base": "https://example.com/1",
                        "method": "GET",
                        "searchTermParamName": "q"
                      },
                      "suggestions": {
                        "base": "https://example.com/suggestions",
                        "method": "POST",
                        "params": [{
                          "name": "type",
                          "value": "space",
                        }],
                        "searchTermParamName": "suggest"
                      },
                      "trending": {
                        "base": "https://example.com/trending",
                        "method": "GET",
                        "params": [{
                          "name": "area",
                          "experimentConfig": "area-param",
                        }]
                      },
                      "searchForm": {
                        "base": "https://example.com/search-form",
                        "method": "GET",
                        "params": [{
                          "name": "search-form-name",
                          "value": "search-form-value",
                        }]
                      }
                    }
                  },
                  "variants": [{
                    "environment": {
                      "allRegionsAndLocales": true
                    },
                  },
                  {
                    "environment": {
                      "regions": ["FR"]
                    },
                    "urls": {
                      "search": {
                        "method": "POST",
                        "params": [{
                          "name": "variant-param-name",
                          "value": "variant-param-value"
                        }]
                      }
                    },
                    "subVariants": [
                      {
                        "environment": {
                          "locales": ["fr"]
                        },
                        "partnerCode": "fr-partner-code",
                        "telemetrySuffix": "fr-telemetry-suffix"
                      },
                      {
                        "environment": {
                          "locales": ["en-CA"]
                        },
                        "urls": {
                          "search": {
                            "method": "GET",
                            "params": [{
                              "name": "en-ca-param-name",
                              "value": "en-ca-param-value"
                            }]
                          }
                        },
                      }
                    ]
                  }],
                },
                {
                  "recordType": "defaultEngines",
                  "globalDefault": "test1"
                }
              ]
            })
            .to_string(),
        );
        assert!(
            config_result.is_ok(),
            "Should have set the configuration successfully. {:?}",
            config_result
        );
        assert!(
            config_overrides_result.is_ok(),
            "Should have set the configuration overrides successfully. {:?}",
            config_overrides_result
        );

        let mut result = Arc::clone(&selector).filter_engine_configuration(SearchUserEnvironment {
            region: "FR".into(),
            locale: "fr".into(),
            ..Default::default()
        });

        assert!(
            result.is_ok(),
            "Should have filtered the configuration without error. {:?}",
            result
        );
        assert_eq!(
            result.unwrap(),
            RefinedSearchConfig {
                engines: vec!(SearchEngineDefinition {
                    charset: "UTF-8".to_string(),
                    identifier: "test1".to_string(),
                    name: "Test 1".to_string(),
                    partner_code: "fr-partner-code".to_string(),
                    telemetry_suffix: "fr-telemetry-suffix".to_string(),
                    urls: SearchEngineUrls {
                        search: SearchEngineUrl {
                            base: "https://example.com/1".to_string(),
                            method: "POST".to_string(),
                            params: vec![SearchUrlParam {
                                name: "variant-param-name".to_string(),
                                value: Some("variant-param-value".to_string()),
                                enterprise_value: None,
                                experiment_config: None
                            }],
                            search_term_param_name: Some("q".to_string())
                        },
                        suggestions: Some(SearchEngineUrl {
                            base: "https://example.com/suggestions".to_string(),
                            method: "POST".to_string(),
                            params: vec![SearchUrlParam {
                                name: "type".to_string(),
                                value: Some("space".to_string()),
                                enterprise_value: None,
                                experiment_config: None
                            }],
                            search_term_param_name: Some("suggest".to_string())
                        }),
                        trending: Some(SearchEngineUrl {
                            base: "https://example.com/trending".to_string(),
                            method: "GET".to_string(),
                            params: vec![SearchUrlParam {
                                name: "area".to_string(),
                                value: None,
                                enterprise_value: None,
                                experiment_config: Some("area-param".to_string())
                            }],
                            search_term_param_name: None
                        }),
                        search_form: Some(SearchEngineUrl {
                            base: "https://example.com/search-form".to_string(),
                            method: "GET".to_string(),
                            params: vec![SearchUrlParam {
                                name: "search-form-name".to_string(),
                                value: Some("search-form-value".to_string()),
                                enterprise_value: None,
                                experiment_config: None,
                            }],
                            search_term_param_name: None,
                        }),
                    },
                    ..Default::default()
                }),
                app_default_engine_id: Some("test1".to_string()),
                app_private_default_engine_id: None
            },
            "Should have correctly matched and merged the fr locale sub-variant."
        );

        result = selector.filter_engine_configuration(SearchUserEnvironment {
            region: "FR".into(),
            locale: "en-CA".into(),
            ..Default::default()
        });

        assert!(
            result.is_ok(),
            "Should have filtered the configuration without error. {:?}",
            result
        );
        assert_eq!(
            result.unwrap(),
            RefinedSearchConfig {
                engines: vec!(SearchEngineDefinition {
                    charset: "UTF-8".to_string(),
                    identifier: "test1".to_string(),
                    name: "Test 1".to_string(),
                    partner_code: "star".to_string(),
                    urls: SearchEngineUrls {
                        search: SearchEngineUrl {
                            base: "https://example.com/1".to_string(),
                            method: "GET".to_string(),
                            params: vec![SearchUrlParam {
                                name: "en-ca-param-name".to_string(),
                                value: Some("en-ca-param-value".to_string()),
                                enterprise_value: None,
                                experiment_config: None
                            }],
                            search_term_param_name: Some("q".to_string())
                        },
                        suggestions: Some(SearchEngineUrl {
                            base: "https://example.com/suggestions".to_string(),
                            method: "POST".to_string(),
                            params: vec![SearchUrlParam {
                                name: "type".to_string(),
                                value: Some("space".to_string()),
                                enterprise_value: None,
                                experiment_config: None
                            }],
                            search_term_param_name: Some("suggest".to_string())
                        }),
                        trending: Some(SearchEngineUrl {
                            base: "https://example.com/trending".to_string(),
                            method: "GET".to_string(),
                            params: vec![SearchUrlParam {
                                name: "area".to_string(),
                                value: None,
                                enterprise_value: None,
                                experiment_config: Some("area-param".to_string())
                            }],
                            search_term_param_name: None
                        }),
                        search_form: Some(SearchEngineUrl {
                            base: "https://example.com/search-form".to_string(),
                            method: "GET".to_string(),
                            params: vec![SearchUrlParam {
                                name: "search-form-name".to_string(),
                                value: Some("search-form-value".to_string()),
                                enterprise_value: None,
                                experiment_config: None,
                            }],
                            search_term_param_name: None,
                        }),
                    },
                    ..Default::default()
                }),
                app_default_engine_id: Some("test1".to_string()),
                app_private_default_engine_id: None
            },
            "Should have correctly matched and merged the en-CA locale sub-variant."
        );
    }

    #[test]
    fn test_filter_engine_configuration_handles_environments() {
        let selector = Arc::new(SearchEngineSelector::new());

        let config_overrides_result = Arc::clone(&selector).set_config_overrides(
            json!({
              "data": [
                {
                  "identifier": "overrides-engine",
                  "partnerCode": "overrides-partner-code",
                  "clickUrl": "https://example.com/click-url",
                  "telemetrySuffix": "overrides-telemetry-suffix",
                  "urls": {
                    "search": {
                      "base": "https://example.com/search-overrides",
                      "method": "GET",
                      "params": []
                    }
                  }
                }
              ]
            })
            .to_string(),
        );
        let config_result = Arc::clone(&selector).set_search_config(
            json!({
              "data": [
                {
                  "recordType": "engine",
                  "identifier": "test1",
                  "base": {
                    "name": "Test 1",
                    "classification": "general",
                    "urls": {
                      "search": {
                        "base": "https://example.com/1",
                        "method": "GET",
                        "searchTermParamName": "q"
                      }
                    }
                  },
                  "variants": [{
                    "environment": {
                      "allRegionsAndLocales": true
                    }
                  }],
                },
                {
                  "recordType": "engine",
                  "identifier": "test2",
                  "base": {
                    "name": "Test 2",
                    "classification": "general",
                    "urls": {
                      "search": {
                        "base": "https://example.com/2",
                        "method": "GET",
                        "searchTermParamName": "search"
                      }
                    }
                  },
                  "variants": [{
                    "environment": {
                      "applications": ["firefox-android", "focus-ios"]
                    }
                  }],
                },
                {
                  "recordType": "engine",
                  "identifier": "test3",
                  "base": {
                    "name": "Test 3",
                    "classification": "general",
                    "urls": {
                      "search": {
                        "base": "https://example.com/3",
                        "method": "GET",
                        "searchTermParamName": "trek"
                      }
                    }
                  },
                  "variants": [{
                    "environment": {
                      "distributions": ["starship"]
                    }
                  }],
                },
                {
                  "recordType": "defaultEngines",
                  "globalDefault": "test1",
                }
              ]
            })
            .to_string(),
        );
        assert!(
            config_result.is_ok(),
            "Should have set the configuration successfully. {:?}",
            config_result
        );
        assert!(
            config_overrides_result.is_ok(),
            "Should have set the configuration overrides successfully. {:?}",
            config_overrides_result
        );

        let mut result = Arc::clone(&selector).filter_engine_configuration(SearchUserEnvironment {
            distribution_id: String::new(),
            app_name: SearchApplicationName::Firefox,
            ..Default::default()
        });

        assert!(
            result.is_ok(),
            "Should have filtered the configuration without error. {:?}",
            result
        );
        assert_eq!(
            result.unwrap(),
            RefinedSearchConfig {
                engines: vec!(
                    SearchEngineDefinition {
                        charset: "UTF-8".to_string(),
                        classification: SearchEngineClassification::General,
                        identifier: "test1".to_string(),
                        name: "Test 1".to_string(),
                        urls: SearchEngineUrls {
                            search: SearchEngineUrl {
                                base: "https://example.com/1".to_string(),
                                method: "GET".to_string(),
                                params: Vec::new(),
                                search_term_param_name: Some("q".to_string())
                            },
                            ..Default::default()
                        },
                        ..Default::default()
                      },
            ),
                app_default_engine_id: Some("test1".to_string()),
                app_private_default_engine_id: None
            }, "Should have selected test1 for all matching locales, as the environments do not match for the other two"
        );

        result = Arc::clone(&selector).filter_engine_configuration(SearchUserEnvironment {
            distribution_id: String::new(),
            app_name: SearchApplicationName::FocusIos,
            ..Default::default()
        });

        assert!(
            result.is_ok(),
            "Should have filtered the configuration without error. {:?}",
            result
        );
        assert_eq!(
            result.unwrap(),
            RefinedSearchConfig {
                engines: vec!(
                    SearchEngineDefinition {
                        charset: "UTF-8".to_string(),
                        classification: SearchEngineClassification::General,
                        identifier: "test1".to_string(),
                        name: "Test 1".to_string(),
                        urls: SearchEngineUrls {
                            search: SearchEngineUrl {
                                base: "https://example.com/1".to_string(),
                                method: "GET".to_string(),
                                params: Vec::new(),
                                search_term_param_name: Some("q".to_string())
                            },
                            ..Default::default()
                        },
                        ..Default::default()
                    },
                    SearchEngineDefinition {
                        charset: "UTF-8".to_string(),
                        classification: SearchEngineClassification::General,
                        identifier: "test2".to_string(),
                        name: "Test 2".to_string(),
                        urls: SearchEngineUrls {
                            search: SearchEngineUrl {
                                base: "https://example.com/2".to_string(),
                                method: "GET".to_string(),
                                params: Vec::new(),
                                search_term_param_name: Some("search".to_string())
                            },
                            ..Default::default()
                        },
                        ..Default::default()
                    },
                ),
                app_default_engine_id: Some("test1".to_string()),
                app_private_default_engine_id: None
            },
            "Should have selected test1 for all matching locales and test2 for matching Focus IOS"
        );

        result = Arc::clone(&selector).filter_engine_configuration(SearchUserEnvironment {
            distribution_id: "starship".to_string(),
            app_name: SearchApplicationName::Firefox,
            ..Default::default()
        });

        assert!(
            result.is_ok(),
            "Should have filtered the configuration without error. {:?}",
            result
        );
        assert_eq!(
            result.unwrap(),
            RefinedSearchConfig {
                engines: vec!(
                    SearchEngineDefinition {
                        charset: "UTF-8".to_string(),
                        classification: SearchEngineClassification::General,
                        identifier: "test1".to_string(),
                        name: "Test 1".to_string(),
                        urls: SearchEngineUrls {
                            search: SearchEngineUrl {
                                base: "https://example.com/1".to_string(),
                                method: "GET".to_string(),
                                params: Vec::new(),
                                search_term_param_name: Some("q".to_string())
                            },
                            ..Default::default()
                        },
                        ..Default::default()
                    },
                    SearchEngineDefinition {
                        charset: "UTF-8".to_string(),
                        classification: SearchEngineClassification::General,
                        identifier: "test3".to_string(),
                        name: "Test 3".to_string(),
                        urls: SearchEngineUrls {
                            search: SearchEngineUrl {
                                base: "https://example.com/3".to_string(),
                                method: "GET".to_string(),
                                params: Vec::new(),
                                search_term_param_name: Some("trek".to_string())
                            },
                            ..Default::default()
                        },
                        ..Default::default()
                      },
                ),
                app_default_engine_id: Some("test1".to_string()),
                app_private_default_engine_id: None
            }, "Should have selected test1 for all matching locales and test3 for matching the distribution id"
        );
    }

    #[test]
    fn test_set_config_should_handle_default_engines() {
        let selector = Arc::new(SearchEngineSelector::new());

        let config_overrides_result = Arc::clone(&selector).set_config_overrides(
            json!({
              "data": [
                {
                  "identifier": "overrides-engine",
                  "partnerCode": "overrides-partner-code",
                  "clickUrl": "https://example.com/click-url",
                  "telemetrySuffix": "overrides-telemetry-suffix",
                  "urls": {
                    "search": {
                      "base": "https://example.com/search-overrides",
                      "method": "GET",
                      "params": []
                    }
                  }
                }
              ]
            })
            .to_string(),
        );
        let config_result = Arc::clone(&selector).set_search_config(
            json!({
              "data": [
                {
                  "recordType": "engine",
                  "identifier": "test",
                  "base": {
                    "name": "Test",
                    "classification": "general",
                    "urls": {
                      "search": {
                        "base": "https://example.com",
                        "method": "GET",
                      }
                    }
                  },
                  "variants": [{
                    "environment": {
                      "allRegionsAndLocales": true
                    }
                  }],
                },
                {
                  "recordType": "engine",
                  "identifier": "distro-default",
                  "base": {
                    "name": "Distribution Default",
                    "classification": "general",
                    "urls": {
                      "search": {
                        "base": "https://example.com",
                        "method": "GET"
                      }
                    }
                  },
                  "variants": [{
                    "environment": {
                      "allRegionsAndLocales": true
                    }
                  }],
                },
                {
                  "recordType": "engine",
                  "identifier": "private-default-FR",
                  "base": {
                    "name": "Private default FR",
                    "classification": "general",
                    "urls": {
                      "search": {
                        "base": "https://example.com",
                        "method": "GET"
                      }
                    }
                  },
                  "variants": [{
                    "environment": {
                      "allRegionsAndLocales": true,
                    }
                  }],
                },
                {
                  "recordType": "defaultEngines",
                  "globalDefault": "test",
                  "specificDefaults": [{
                    "environment": {
                      "distributions": ["test-distro"],
                    },
                    "default": "distro-default"
                  }, {
                    "environment": {
                      "regions": ["fr"]
                    },
                    "defaultPrivate": "private-default-FR"
                  }]
                }
              ]
            })
            .to_string(),
        );
        assert!(
            config_result.is_ok(),
            "Should have set the configuration successfully. {:?}",
            config_result
        );
        assert!(
            config_overrides_result.is_ok(),
            "Should have set the configuration overrides successfully. {:?}",
            config_overrides_result
        );

        let test_engine = SearchEngineDefinition {
            charset: "UTF-8".to_string(),
            classification: SearchEngineClassification::General,
            identifier: "test".to_string(),
            name: "Test".to_string(),
            urls: SearchEngineUrls {
                search: SearchEngineUrl {
                    base: "https://example.com".to_string(),
                    method: "GET".to_string(),
                    params: Vec::new(),
                    search_term_param_name: None,
                },
                ..Default::default()
            },
            ..Default::default()
        };
        let distro_default_engine = SearchEngineDefinition {
            charset: "UTF-8".to_string(),
            classification: SearchEngineClassification::General,
            identifier: "distro-default".to_string(),
            name: "Distribution Default".to_string(),
            urls: SearchEngineUrls {
                search: SearchEngineUrl {
                    base: "https://example.com".to_string(),
                    method: "GET".to_string(),
                    params: Vec::new(),
                    search_term_param_name: None,
                },
                ..Default::default()
            },
            ..Default::default()
        };
        let private_default_fr_engine = SearchEngineDefinition {
            charset: "UTF-8".to_string(),
            classification: SearchEngineClassification::General,
            identifier: "private-default-FR".to_string(),
            name: "Private default FR".to_string(),
            urls: SearchEngineUrls {
                search: SearchEngineUrl {
                    base: "https://example.com".to_string(),
                    method: "GET".to_string(),
                    params: Vec::new(),
                    search_term_param_name: None,
                },
                ..Default::default()
            },
            ..Default::default()
        };

        let result = Arc::clone(&selector).filter_engine_configuration(SearchUserEnvironment {
            distribution_id: "test-distro".to_string(),
            ..Default::default()
        });
        assert!(
            result.is_ok(),
            "Should have filtered the configuration without error. {:?}",
            result
        );
        assert_eq!(
            result.unwrap(),
            RefinedSearchConfig {
                engines: vec![
                    distro_default_engine.clone(),
                    private_default_fr_engine.clone(),
                    test_engine.clone(),
                ],
                app_default_engine_id: Some("distro-default".to_string()),
                app_private_default_engine_id: None
            },
            "Should have selected the default engine for the matching specific default"
        );

        let result = Arc::clone(&selector).filter_engine_configuration(SearchUserEnvironment {
            region: "fr".into(),
            distribution_id: String::new(),
            ..Default::default()
        });
        assert!(
            result.is_ok(),
            "Should have filtered the configuration without error. {:?}",
            result
        );
        assert_eq!(
            result.unwrap(),
            RefinedSearchConfig {
                engines: vec![
                    test_engine,
                    private_default_fr_engine,
                    distro_default_engine,
                ],
                app_default_engine_id: Some("test".to_string()),
                app_private_default_engine_id: Some("private-default-FR".to_string())
            },
            "Should have selected the private default engine for the matching specific default"
        );
    }

    #[test]
    fn test_filter_engine_orders() {
        let selector = Arc::new(SearchEngineSelector::new());

        let config_overrides_result = Arc::clone(&selector).set_config_overrides(
            json!({
              "data": [
                {
                  "identifier": "overrides-engine",
                  "partnerCode": "overrides-partner-code",
                  "clickUrl": "https://example.com/click-url",
                  "telemetrySuffix": "overrides-telemetry-suffix",
                  "urls": {
                    "search": {
                      "base": "https://example.com/search-overrides",
                      "method": "GET",
                      "params": []
                    }
                  }
                }
              ]
            })
            .to_string(),
        );
        let engine_order_config = Arc::clone(&selector).set_search_config(
            json!({
              "data": [
                {
                  "recordType": "engine",
                  "identifier": "after-defaults",
                  "base": {
                    "name": "after-defaults",
                    "classification": "general",
                    "urls": {
                      "search": {
                        "base": "https://example.com",
                        "method": "GET"
                      }
                    }
                  },
                  "variants": [{
                    "environment": {
                      "allRegionsAndLocales": true,
                    }
                  }],
                },
                {
                  "recordType": "engine",
                  "identifier": "b-engine",
                  "base": {
                    "name": "b-engine",
                    "classification": "general",
                    "urls": {
                      "search": {
                        "base": "https://example.com",
                        "method": "GET"
                      }
                    }
                  },
                  "variants": [{
                    "environment": {
                      "allRegionsAndLocales": true
                    }
                  }],
                },
                {
                  "recordType": "engine",
                  "identifier": "a-engine",
                  "base": {
                    "name": "a-engine",
                    "classification": "general",
                    "urls": {
                      "search": {
                        "base": "https://example.com",
                        "method": "GET"
                      }
                    }
                  },
                  "variants": [{
                    "environment": {
                      "allRegionsAndLocales": true,
                    }
                  }],
                },
                {
                  "recordType": "engine",
                  "identifier": "default-engine",
                  "base": {
                    "name": "default-engine",
                    "classification": "general",
                    "urls": {
                      "search": {
                        "base": "https://example.com",
                        "method": "GET"
                      }
                    }
                  },
                  "variants": [{
                    "environment": {
                      "allRegionsAndLocales": true,
                    }
                  }],
                },
                {
                  "recordType": "engine",
                  "identifier": "default-private-engine",
                  "base": {
                    "name": "default-private-engine",
                    "classification": "general",
                    "urls": {
                      "search": {
                        "base": "https://example.com",
                        "method": "GET"
                      }
                    }
                  },
                  "variants": [{
                    "environment": {
                      "allRegionsAndLocales": true,
                    }
                  }],
                },
                {
                  "recordType": "defaultEngines",
                  "globalDefault": "default-engine",
                  "globalDefaultPrivate": "default-private-engine",
                },
                {
                  "recordType": "engineOrders",
                  "orders": [
                    {
                      "environment": {
                        "locales": ["en-CA"],
                        "regions": ["CA"],
                      },
                      "order": ["after-defaults"],
                    },
                  ],
                },
              ]
            })
            .to_string(),
        );
        assert!(
            engine_order_config.is_ok(),
            "Should have set the configuration successfully. {:?}",
            engine_order_config
        );
        assert!(
            config_overrides_result.is_ok(),
            "Should have set the configuration overrides successfully. {:?}",
            config_overrides_result
        );

        fn assert_actual_engines_equals_expected(
            result: Result<RefinedSearchConfig, SearchApiError>,
            expected_engine_orders: Vec<String>,
            message: &str,
        ) {
            assert!(
                result.is_ok(),
                "Should have filtered the configuration without error. {:?}",
                result
            );

            let refined_config = result.unwrap();
            let actual_engine_orders: Vec<String> = refined_config
                .engines
                .into_iter()
                .map(|e| e.identifier)
                .collect();

            assert_eq!(actual_engine_orders, expected_engine_orders, "{}", message);
        }

        assert_actual_engines_equals_expected(
            Arc::clone(&selector).filter_engine_configuration(SearchUserEnvironment {
                locale: "en-CA".into(),
                region: "CA".into(),
                ..Default::default()
            }),
            vec![
                "default-engine".to_string(),
                "default-private-engine".to_string(),
                "after-defaults".to_string(),
                "a-engine".to_string(),
                "b-engine".to_string(),
            ],
            "Should order the default engine first, default private engine second, and the rest of the engines based on order hint then alphabetically."
        );

        let starts_with_wiki_config = Arc::clone(&selector).set_search_config(
            json!({
              "data": [
                {
                  "recordType": "engine",
                  "identifier": "wiki-ca",
                  "base": {
                    "name": "wiki-ca",
                    "classification": "general",
                    "urls": {
                      "search": {
                        "base": "https://example.com",
                        "method": "GET"
                      }
                    }
                  },
                  "variants": [{
                    "environment": {
                      "locales": ["en-CA"],
                      "regions": ["CA"],
                    }
                  }],
                },
                {
                  "recordType": "engine",
                  "identifier": "wiki-uk",
                  "base": {
                    "name": "wiki-uk",
                    "classification": "general",
                    "urls": {
                      "search": {
                        "base": "https://example.com",
                        "method": "GET"
                      }
                    }
                  },
                  "variants": [{
                    "environment": {
                      "locales": ["en-GB"],
                      "regions": ["GB"],
                    }
                  }],
                },
                {
                  "recordType": "engine",
                  "identifier": "engine-1",
                  "base": {
                    "name": "engine-1",
                    "classification": "general",
                    "urls": {
                      "search": {
                        "base": "https://example.com",
                        "method": "GET"
                      }
                    }
                  },
                  "variants": [{
                    "environment": {
                      "allRegionsAndLocales": true,
                    }
                  }],
                },
                {
                  "recordType": "engine",
                  "identifier": "engine-2",
                  "base": {
                    "name": "engine-2",
                    "classification": "general",
                    "urls": {
                      "search": {
                        "base": "https://example.com",
                        "method": "GET"
                      }
                    }
                  },
                  "variants": [{
                    "environment": {
                      "allRegionsAndLocales": true,
                    }
                  }],
                },
                {
                  "recordType": "engineOrders",
                  "orders": [
                    {
                      "environment": {
                        "locales": ["en-CA"],
                        "regions": ["CA"],
                      },
                      "order": ["wiki*", "engine-1", "engine-2"],
                    },
                    {
                      "environment": {
                        "locales": ["en-GB"],
                        "regions": ["GB"],
                      },
                      "order": ["wiki*", "engine-1", "engine-2"],
                    },
                  ],
                },
              ]
            })
            .to_string(),
        );
        assert!(
            starts_with_wiki_config.is_ok(),
            "Should have set the configuration successfully. {:?}",
            starts_with_wiki_config
        );

        assert_actual_engines_equals_expected(
            Arc::clone(&selector).filter_engine_configuration(SearchUserEnvironment {
                locale: "en-CA".into(),
                region: "CA".into(),
                ..Default::default()
            }),
            vec![
                "wiki-ca".to_string(),
                "engine-1".to_string(),
                "engine-2".to_string(),
            ],
            "Should list the wiki-ca engine and other engines in correct orders with the en-CA and CA locale region environment."
        );

        assert_actual_engines_equals_expected(
            Arc::clone(&selector).filter_engine_configuration(SearchUserEnvironment {
                locale: "en-GB".into(),
                region: "GB".into(),
                ..Default::default()
            }),
            vec![
                "wiki-uk".to_string(),
                "engine-1".to_string(),
                "engine-2".to_string(),
            ],
            "Should list the wiki-uk engine and other engines in correct orders with the en-GB and GB locale region environment."
        );
    }

    const APPLY_OVERRIDES: bool = true;
    const DO_NOT_APPLY_OVERRIDES: bool = false;
    const RECORDS_MISSING: bool = false;
    const RECORDS_PRESENT: bool = true;

    fn setup_remote_settings_test(
        should_apply_overrides: bool,
        expect_sync_successful: bool,
    ) -> Arc<SearchEngineSelector> {
        let _ = env_logger::builder().try_init();
        viaduct_reqwest::use_reqwest_backend();

        let config = RemoteSettingsConfig2 {
            server: Some(RemoteSettingsServer::Custom {
                url: mockito::server_url(),
            }),
            bucket_name: Some(String::from("main")),
            app_context: Some(RemoteSettingsContext::default()),
        };
        let service = Arc::new(RemoteSettingsService::new(String::from(":memory:"), config));

        let selector = Arc::new(SearchEngineSelector::new());

        Arc::clone(&selector).use_remote_settings_server(&service, should_apply_overrides);
        let sync_result = Arc::clone(&service).sync();
        assert!(
            if expect_sync_successful {
                sync_result.is_ok()
            } else {
                sync_result.is_err()
            },
            "Should have completed the sync successfully. {:?}",
            sync_result
        );

        selector
    }

    fn response_body() -> String {
        json!({
          "metadata": {
            "id": "search-config-v2",
            "last_modified": 1000,
            "bucket": "main",
            "signature": {
              "x5u": "fake",
              "signature": "fake",
            },
          },
          "timestamp": 1000,
          "changes": [
            {
              "recordType": "engine",
              "identifier": "test",
              "base": {
                "name": "Test",
                "classification": "general",
                "urls": {
                  "search": {
                    "base": "https://example.com",
                    "method": "GET",
                  }
                }
              },
              "variants": [{
                "environment": {
                  "allRegionsAndLocales": true
                }
              }],
              "id": "c5dcd1da-7126-4abb-846b-ec85b0d4d0d7",
              "schema": 1001,
              "last_modified": 1000
            },
            {
              "recordType": "engine",
              "identifier": "distro-default",
              "base": {
                "name": "Distribution Default",
                "classification": "general",
                "urls": {
                  "search": {
                    "base": "https://example.com",
                    "method": "GET"
                  }
                }
              },
              "variants": [{
                "environment": {
                  "allRegionsAndLocales": true
                }
              }],
              "id": "c5dcd1da-7126-4abb-846b-ec85b0d4d0d8",
              "schema": 1002,
              "last_modified": 1000
            },
            {
              "recordType": "engine",
              "identifier": "private-default-FR",
              "base": {
                "name": "Private default FR",
                "classification": "general",
                "urls": {
                  "search": {
                    "base": "https://example.com",
                    "method": "GET"
                  }
                }
              },
              "variants": [{
                "environment": {
                  "allRegionsAndLocales": true,
                }
              }],
              "id": "c5dcd1da-7126-4abb-846b-ec85b0d4d0d9",
              "schema": 1003,
              "last_modified": 1000
            },
            {
              "recordType": "defaultEngines",
              "globalDefault": "test",
              "specificDefaults": [{
                "environment": {
                  "distributions": ["test-distro"],
                },
                "default": "distro-default"
              }, {
                "environment": {
                  "regions": ["fr"]
                },
                "defaultPrivate": "private-default-FR"
              }],
              "id": "c5dcd1da-7126-4abb-846b-ec85b0d4d0e0",
              "schema": 1004,
              "last_modified": 1000,
            }
          ]
        })
        .to_string()
    }

    fn response_body_overrides() -> String {
        json!({
          "metadata": {
            "id": "search-config-overrides-v2",
            "last_modified": 1000,
            "bucket": "main",
            "signature": {
              "x5u": "fake",
              "signature": "fake",
            },
          },
          "timestamp": 1000,
          "changes": [
            {
              "urls": {
                "search": {
                  "base": "https://example.com/search-overrides",
                  "method": "GET",
                    "params": [{
                      "name": "overrides-name",
                      "value": "overrides-value",
                    }],
                }
              },
              "identifier": "test",
              "clickUrl": "https://example.com/click-url",
              "telemetrySuffix": "overrides-telemetry-suffix",
              "partnerCode": "overrides-partner-code",
              "id": "c5dcd1da-7126-4abb-846b-ec85b0d4d0d7",
              "schema": 1001,
              "last_modified": 1000
            },
          ]
        })
        .to_string()
    }

    #[test]
    fn test_remote_settings_empty_search_config_records_throws_error() {
        let m = mock(
            "GET",
            "/v1/buckets/main/collections/search-config-v2/changeset?_expected=0",
        )
        .with_body(
            json!({
              "metadata": {
                "id": "search-config-v2",
                "last_modified": 1000,
                "bucket": "main",
                "signature": {
                  "x5u": "fake",
                  "signature": "fake",
                },
              },
              "timestamp": 1000,
              "changes": [
            ]})
            .to_string(),
        )
        .with_status(200)
        .with_header("content-type", "application/json")
        .with_header("etag", "\"1000\"")
        .create();

        let selector = setup_remote_settings_test(DO_NOT_APPLY_OVERRIDES, RECORDS_PRESENT);

        let result = Arc::clone(&selector).filter_engine_configuration(SearchUserEnvironment {
            distribution_id: "test-distro".to_string(),
            ..Default::default()
        });
        assert!(
            result.is_err(),
            "Should throw an error when a configuration has not been specified before filtering"
        );
        assert!(result
            .unwrap_err()
            .to_string()
            .contains("No search config v2 records received from remote settings"));
        m.expect(1).assert();
    }

    #[test]
    fn test_remote_settings_search_config_records_is_none_throws_error() {
        let m1 = mock(
            "GET",
            "/v1/buckets/main/collections/search-config-v2/changeset?_expected=0",
        )
        .with_body(response_body())
        .with_status(501)
        .with_header("content-type", "application/json")
        .with_header("etag", "\"1000\"")
        .create();

        let selector = setup_remote_settings_test(DO_NOT_APPLY_OVERRIDES, RECORDS_MISSING);

        let result = Arc::clone(&selector).filter_engine_configuration(SearchUserEnvironment {
            distribution_id: "test-distro".to_string(),
            ..Default::default()
        });
        assert!(
            result.is_err(),
            "Should throw an error when a configuration has not been specified before filtering"
        );
        assert!(result
            .unwrap_err()
            .to_string()
            .contains("No search config v2 records received from remote settings"));
        m1.expect(1).assert();
    }

    #[test]
    fn test_remote_settings_empty_search_config_overrides_filtered_without_error() {
        let m1 = mock(
            "GET",
            "/v1/buckets/main/collections/search-config-v2/changeset?_expected=0",
        )
        .with_body(response_body())
        .with_status(200)
        .with_header("content-type", "application/json")
        .with_header("etag", "\"1000\"")
        .create();

        let m2 = mock(
            "GET",
            "/v1/buckets/main/collections/search-config-overrides-v2/changeset?_expected=0",
        )
        .with_body(
            json!({
               "metadata": {
                 "id": "search-config-overrides-v2",
                 "last_modified": 1000,
                 "bucket": "main",
                 "signature": {
                   "x5u": "fake",
                   "signature": "fake",
                 },
               },
               "timestamp": 1000,
               "changes": [
            ]})
            .to_string(),
        )
        .with_status(200)
        .with_header("content-type", "application/json")
        .with_header("etag", "\"1000\"")
        .create();

        let selector = setup_remote_settings_test(APPLY_OVERRIDES, RECORDS_PRESENT);

        let result = Arc::clone(&selector).filter_engine_configuration(SearchUserEnvironment {
            distribution_id: "test-distro".to_string(),
            ..Default::default()
        });
        assert!(
            result.is_ok(),
            "Should have filtered the configuration using an empty search config overrides without causing an error. {:?}",
            result
        );
        m1.expect(1).assert();
        m2.expect(1).assert();
    }

    #[test]
    fn test_remote_settings_search_config_overrides_records_is_none_throws_error() {
        let m1 = mock(
            "GET",
            "/v1/buckets/main/collections/search-config-v2/changeset?_expected=0",
        )
        .with_body(response_body())
        .with_status(200)
        .with_header("content-type", "application/json")
        .with_header("etag", "\"1000\"")
        .create();

        let m2 = mock(
            "GET",
            "/v1/buckets/main/collections/search-config-overrides-v2/changeset?_expected=0",
        )
        .with_body(response_body_overrides())
        .with_status(501)
        .with_header("content-type", "application/json")
        .with_header("etag", "\"1000\"")
        .create();

        let selector = setup_remote_settings_test(APPLY_OVERRIDES, RECORDS_MISSING);

        let result = Arc::clone(&selector).filter_engine_configuration(SearchUserEnvironment {
            distribution_id: "test-distro".to_string(),
            ..Default::default()
        });
        assert!(
            result.is_err(),
            "Should throw an error when a configuration overrides has not been specified before filtering"
        );
        assert!(result
            .unwrap_err()
            .to_string()
            .contains("No search config overrides v2 records received from remote settings"));
        m1.expect(1).assert();
        m2.expect(1).assert();
    }

    #[test]
    fn test_filter_with_remote_settings_overrides() {
        let m1 = mock(
            "GET",
            "/v1/buckets/main/collections/search-config-v2/changeset?_expected=0",
        )
        .with_body(response_body())
        .with_status(200)
        .with_header("content-type", "application/json")
        .with_header("etag", "\"1000\"")
        .create();

        let m2 = mock(
            "GET",
            "/v1/buckets/main/collections/search-config-overrides-v2/changeset?_expected=0",
        )
        .with_body(response_body_overrides())
        .with_status(200)
        .with_header("content-type", "application/json")
        .with_header("etag", "\"1000\"")
        .create();

        let selector = setup_remote_settings_test(APPLY_OVERRIDES, RECORDS_PRESENT);

        let test_engine = SearchEngineDefinition {
            charset: "UTF-8".to_string(),
            classification: SearchEngineClassification::General,
            identifier: "test".to_string(),
            name: "Test".to_string(),
            partner_code: "overrides-partner-code".to_string(),
            telemetry_suffix: "overrides-telemetry-suffix".to_string(),
            click_url: Some("https://example.com/click-url".to_string()),
            urls: SearchEngineUrls {
                search: SearchEngineUrl {
                    base: "https://example.com/search-overrides".to_string(),
                    method: "GET".to_string(),
                    params: vec![SearchUrlParam {
                        name: "overrides-name".to_string(),
                        value: Some("overrides-value".to_string()),
                        enterprise_value: None,
                        experiment_config: None,
                    }],
                    search_term_param_name: None,
                },
                ..Default::default()
            },
            ..Default::default()
        };

        let result = Arc::clone(&selector).filter_engine_configuration(SearchUserEnvironment {
            ..Default::default()
        });

        assert!(
            result.is_ok(),
            "Should have filtered the configuration without error. {:?}",
            result
        );
        assert_eq!(
            result.unwrap().engines[0],
            test_engine.clone(),
            "Should have applied the overrides to the matching engine"
        );
        m1.expect(1).assert();
        m2.expect(1).assert();
    }

    #[test]
    fn test_filter_with_remote_settings() {
        let m = mock(
            "GET",
            "/v1/buckets/main/collections/search-config-v2/changeset?_expected=0",
        )
        .with_body(response_body())
        .with_status(200)
        .with_header("content-type", "application/json")
        .with_header("etag", "\"1000\"")
        .create();

        let selector = setup_remote_settings_test(DO_NOT_APPLY_OVERRIDES, RECORDS_PRESENT);

        let test_engine = SearchEngineDefinition {
            charset: "UTF-8".to_string(),
            classification: SearchEngineClassification::General,
            identifier: "test".to_string(),
            name: "Test".to_string(),
            urls: SearchEngineUrls {
                search: SearchEngineUrl {
                    base: "https://example.com".to_string(),
                    method: "GET".to_string(),
                    params: Vec::new(),
                    search_term_param_name: None,
                },
                ..Default::default()
            },
            ..Default::default()
        };
        let private_default_fr_engine = SearchEngineDefinition {
            charset: "UTF-8".to_string(),
            classification: SearchEngineClassification::General,
            identifier: "private-default-FR".to_string(),
            name: "Private default FR".to_string(),
            urls: SearchEngineUrls {
                search: SearchEngineUrl {
                    base: "https://example.com".to_string(),
                    method: "GET".to_string(),
                    params: Vec::new(),
                    search_term_param_name: None,
                },
                ..Default::default()
            },
            ..Default::default()
        };
        let distro_default_engine = SearchEngineDefinition {
            charset: "UTF-8".to_string(),
            classification: SearchEngineClassification::General,
            identifier: "distro-default".to_string(),
            name: "Distribution Default".to_string(),
            urls: SearchEngineUrls {
                search: SearchEngineUrl {
                    base: "https://example.com".to_string(),
                    method: "GET".to_string(),
                    params: Vec::new(),
                    search_term_param_name: None,
                },
                ..Default::default()
            },
            ..Default::default()
        };

        let result = Arc::clone(&selector).filter_engine_configuration(SearchUserEnvironment {
            distribution_id: "test-distro".to_string(),
            ..Default::default()
        });
        assert!(
            result.is_ok(),
            "Should have filtered the configuration without error. {:?}",
            result
        );
        assert_eq!(
            result.unwrap(),
            RefinedSearchConfig {
                engines: vec![
                    distro_default_engine.clone(),
                    private_default_fr_engine.clone(),
                    test_engine.clone(),
                ],
                app_default_engine_id: Some("distro-default".to_string()),
                app_private_default_engine_id: None
            },
            "Should have selected the default engine for the matching specific default"
        );

        let result = Arc::clone(&selector).filter_engine_configuration(SearchUserEnvironment {
            region: "fr".into(),
            distribution_id: String::new(),
            ..Default::default()
        });
        assert!(
            result.is_ok(),
            "Should have filtered the configuration without error. {:?}",
            result
        );
        assert_eq!(
            result.unwrap(),
            RefinedSearchConfig {
                engines: vec![
                    test_engine.clone(),
                    private_default_fr_engine.clone(),
                    distro_default_engine.clone(),
                ],
                app_default_engine_id: Some("test".to_string()),
                app_private_default_engine_id: Some("private-default-FR".to_string())
            },
            "Should have selected the private default engine for the matching specific default"
        );
        m.expect(1).assert();
    }

    #[test]
    fn test_configuration_overrides_applied() {
        let selector = Arc::new(SearchEngineSelector::new());

        let config_overrides_result = Arc::clone(&selector).set_config_overrides(
            json!({
              "data": [
                {
                  "identifier": "test",
                  "partnerCode": "overrides-partner-code",
                  "clickUrl": "https://example.com/click-url",
                  "telemetrySuffix": "overrides-telemetry-suffix",
                  "urls": {
                    "search": {
                      "base": "https://example.com/search-overrides",
                      "method": "GET",
                        "params": [{
                          "name": "overrides-name",
                          "value": "overrides-value",
                        }],
                    }
                  },
                },
                { // Test partial override with some missing fields
                  "identifier": "distro-default",
                  "partnerCode": "distro-overrides-partner-code",
                  "clickUrl": "https://example.com/click-url-distro",
                  "urls": {
                    "search": {
                      "base": "https://example.com/search-distro",
                    },
                  },
                }
              ]
            })
            .to_string(),
        );
        let config_result = Arc::clone(&selector).set_search_config(
            json!({
              "data": [
                {
                  "recordType": "engine",
                  "identifier": "test",
                  "base": {
                    "name": "Test",
                    "classification": "general",
                    "urls": {
                      "search": {
                        "base": "https://example.com",
                        "method": "GET",
                      }
                    }
                  },
                  "variants": [{
                    "environment": {
                      "allRegionsAndLocales": true
                    }
                  }],
                },
                {
                  "recordType": "engine",
                  "identifier": "distro-default",
                  "base": {
                    "name": "Distribution Default",
                    "classification": "general",
                    "urls": {
                      "search": {
                        "base": "https://example.com",
                        "method": "GET"
                      }
                    }
                  },
                  "variants": [{
                    "environment": {
                      "allRegionsAndLocales": true
                    },
                    "telemetrySuffix": "distro-telemetry-suffix",
                  }],
                },
              ]
            })
            .to_string(),
        );
        assert!(
            config_result.is_ok(),
            "Should have set the configuration successfully. {:?}",
            config_result
        );
        assert!(
            config_overrides_result.is_ok(),
            "Should have set the configuration overrides successfully. {:?}",
            config_overrides_result
        );

        let test_engine = SearchEngineDefinition {
            charset: "UTF-8".to_string(),
            classification: SearchEngineClassification::General,
            identifier: "test".to_string(),
            name: "Test".to_string(),
            partner_code: "overrides-partner-code".to_string(),
            telemetry_suffix: "overrides-telemetry-suffix".to_string(),
            click_url: Some("https://example.com/click-url".to_string()),
            urls: SearchEngineUrls {
                search: SearchEngineUrl {
                    base: "https://example.com/search-overrides".to_string(),
                    method: "GET".to_string(),
                    params: vec![SearchUrlParam {
                        name: "overrides-name".to_string(),
                        value: Some("overrides-value".to_string()),
                        enterprise_value: None,
                        experiment_config: None,
                    }],
                    search_term_param_name: None,
                },
                ..Default::default()
            },
            ..Default::default()
        };
        let distro_default_engine = SearchEngineDefinition {
            charset: "UTF-8".to_string(),
            classification: SearchEngineClassification::General,
            identifier: "distro-default".to_string(),
            name: "Distribution Default".to_string(),
            partner_code: "distro-overrides-partner-code".to_string(),
            telemetry_suffix: "distro-telemetry-suffix".to_string(),
            click_url: Some("https://example.com/click-url-distro".to_string()),
            urls: SearchEngineUrls {
                search: SearchEngineUrl {
                    base: "https://example.com/search-distro".to_string(),
                    method: "GET".to_string(),
                    params: Vec::new(),
                    search_term_param_name: None,
                },
                ..Default::default()
            },
            ..Default::default()
        };

        let result = Arc::clone(&selector).filter_engine_configuration(SearchUserEnvironment {
            ..Default::default()
        });
        assert!(
            result.is_ok(),
            "Should have filtered the configuration without error. {:?}",
            result
        );

        assert_eq!(
            result.unwrap(),
            RefinedSearchConfig {
                engines: vec![distro_default_engine.clone(), test_engine.clone(),],
                app_default_engine_id: None,
                app_private_default_engine_id: None
            },
            "Should have applied the overrides to the matching engine."
        );
    }
}
