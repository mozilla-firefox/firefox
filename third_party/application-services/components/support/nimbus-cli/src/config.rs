// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use std::collections::HashMap;
use std::fmt;
use std::path::PathBuf;
use std::sync::LazyLock;

use anyhow::{Result, bail};
use clap::ValueEnum;
use maplit::hashmap;

use crate::cli::Cli;
use crate::version_utils::{is_before, pad_major, pad_major_minor, pad_major_minor_patch};
use crate::{LaunchableApp, NimbusApp};

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq, ValueEnum)]
#[allow(non_camel_case_types)]
pub enum App {
    Fenix,
    #[value(name = "firefox_ios")]
    FirefoxIos,
}

impl fmt::Display for App {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        f.write_str(match self {
            Self::Fenix => "fenix",
            Self::FirefoxIos => "firefox_ios",
        })
    }
}

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq, ValueEnum)]
pub enum Channel {
    Developer,
    Nightly,
    Beta,
    Release,
}

impl fmt::Display for Channel {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        f.write_str(match self {
            Self::Developer => "developer",
            Self::Nightly => "nightly",
            Self::Beta => "beta",
            Self::Release => "release",
        })
    }
}

struct ApplicationConfig {
    channels: HashMap<Channel, ChannelConfig>,
}

struct ChannelConfig {
    package: &'static str,
    scheme: &'static str,
}

static APPLICATIONS: LazyLock<HashMap<App, ApplicationConfig>> = LazyLock::new(|| {
    hashmap! {
        // Firefox for Android defines per channel deeplink schemes in the app/build.gradle.
        // e.g. https://github.com/mozilla-mobile/firefox-android/blob/5d18e7ffe2f3e4505ea815d584d20e66ad10f515/fenix/app/build.gradle#L154
        App::Fenix => ApplicationConfig {
            channels: hashmap! {
                Channel::Developer => ChannelConfig {
                    package: "org.mozilla.fenix.debug",
                    scheme: "fenix-dev",
                },
                Channel::Nightly => ChannelConfig {
                    package: "org.mozilla.fenix",
                    scheme: "fenix-nightly",
                },
                Channel::Beta => ChannelConfig {
                    package: "org.mozilla.firefox_beta",
                    scheme: "fenix-beta",
                },
                Channel::Release => ChannelConfig {
                    package: "org.mozilla.firefox",
                    scheme: "fenix",
                },
            },
        },

        // Firefox for iOS uses MOZ_PUBLIC_URL_SCHEME, which is always
        // [`firefox`](https://github.com/mozilla-mobile/firefox-ios/blob/f1acc8a2232a736e65e235b811372ddbf3e802f8/Client/Configuration/Common.xcconfig#L24)
        // and MOZ_INTERNAL_URL_SCHEME which is different per channel.
        // e.g. https://github.com/mozilla-mobile/firefox-ios/blob/f1acc8a2232a736e65e235b811372ddbf3e802f8/Client/Configuration/Firefox.xcconfig#L12
        // From inspection of the code, there are no different uses for the internal vs
        // public, so very useful for launching the specific app on a phone where you
        // have multiple versions installed.
        App::FirefoxIos => ApplicationConfig {
            channels: hashmap! {
                Channel::Developer => ChannelConfig {
                    package: "org.mozilla.ios.Fennec",
                    scheme: "fennec",
                },
                Channel::Beta => ChannelConfig {
                    package: "org.mozilla.ios.FirefoxBeta",
                    scheme: "firefox-beta",
                },
                Channel::Release => ChannelConfig {
                    package: "org.mozilla.ios.Firefox",
                    scheme: "firefox-internal",
                },
            },
        }
    }
});

impl TryFrom<&Cli> for LaunchableApp {
    type Error = anyhow::Error;
    fn try_from(value: &Cli) -> Result<Self> {
        Self::try_from_app_channel_device(value.app, value.channel, value.device_id.as_deref())
    }
}

impl LaunchableApp {
    pub(crate) fn try_from_app_channel_device(
        app: Option<App>,
        channel: Option<Channel>,
        device_id: Option<&str>,
    ) -> Result<Self> {
        let (app, channel) = match (app, channel) {
            (None, None) => anyhow::bail!(
                "A value for --app and --channel must be specified. Supported apps are: fenix, firefox_ios"
            ),
            (None, _) => anyhow::bail!(
                "A value for --app must be specified. Supported apps are: fenix, firefox_ios"
            ),
            (_, None) => anyhow::bail!(
                "A value for --channel must be specified. Supported channels are: developer, nightly, beta and release"
            ),
            (Some(app), Some(channel)) => (app, channel),
        };

        let app_cfg = &(*APPLICATIONS)[&app];
        let Some(channel_cfg) = app_cfg.channels.get(&channel) else {
            let supported_channels = app_cfg
                .channels
                .keys()
                .map(ToString::to_string)
                .collect::<Vec<_>>()
                .join(", ");
            bail!("Application {app} has no channel `{channel}'. Try one of {supported_channels}");
        };

        Ok(match app {
            App::Fenix => Self::Android {
                package_name: channel_cfg.package.into(),
                activity_name: ".App".into(),
                device_id: device_id.map(Into::into),
                scheme: channel_cfg.scheme.into(),
                open_deeplink: "open".into(),
            },
            App::FirefoxIos => Self::Ios {
                app_id: channel_cfg.package.into(),
                device_id: device_id.unwrap_or("booted").into(),
                scheme: channel_cfg.scheme.into(),
            },
        })
    }
}

impl NimbusApp {
    pub(crate) fn ref_from_version(
        &self,
        version: &Option<String>,
        ref_: &String,
    ) -> Result<String> {
        let app_name = self
            .app_name()
            .ok_or_else(|| anyhow::anyhow!("Either an --app or a --manifest must be specified"))?;

        if version.is_none() {
            return Ok(ref_.to_string());
        }

        let v = version.as_ref().unwrap();
        let v = match app_name.as_str() {
            "fenix" => {
                if is_before(version, 111) {
                    pad_major_minor_patch(v)
                } else if is_before(version, 126) {
                    pad_major(v)
                } else {
                    bail!("mozilla-firefox does not have tagged versions, use --ref instead")
                }
            }
            "focus_android" => {
                if is_before(version, 110) {
                    pad_major_minor(v)
                } else if is_before(version, 126) {
                    pad_major(v)
                } else {
                    bail!("mozilla-firefox does not have tagged versions, use --ref instead")
                }
            }
            "firefox_ios" => {
                if is_before(version, 112) {
                    pad_major_minor(v)
                } else {
                    pad_major(v)
                }
            }
            "focus_ios" => pad_major(v),
            _ => v.to_string(),
        };

        Ok(match app_name.as_str() {
            "fenix" => format!("releases_v{v}"),
            "focus_android" => format!("releases_v{v}"),
            "firefox_ios" => {
                if is_before(version, 106) {
                    format!("v{v}")
                } else {
                    format!("release/v{v}")
                }
            }
            "focus_ios" => format!("releases_v{v}"),

            _ => anyhow::bail!("{} is not defined", app_name),
        })
    }

    pub(crate) fn github_repo<'a>(&self, version: &Option<String>) -> Result<&'a str> {
        let app_name = self
            .app_name()
            .ok_or_else(|| anyhow::anyhow!("Either an --app or a --manifest must be specified"))?;
        Ok(match app_name.as_str() {
            // Fenix and Focus are both in the same repo
            "fenix" => {
                if is_before(version, 111) {
                    "mozilla-mobile/fenix"
                } else if is_before(version, 126) {
                    "mozilla-mobile/firefox-android"
                } else {
                    "mozilla-firefox/firefox"
                }
            }
            "focus_android" => {
                if is_before(version, 110) {
                    "mozilla-mobile/focus-android"
                } else if is_before(version, 126) {
                    "mozilla-mobile/firefox-android"
                } else {
                    "mozilla-firefox/firefox"
                }
            }
            "firefox_ios" => "mozilla-mobile/firefox-ios",
            "focus_ios" => "mozilla-mobile/focus-ios",
            _ => unreachable!("{} is not defined", app_name),
        })
    }

    pub(crate) fn manifest_location<'a>(&self, version: &Option<String>) -> Result<&'a str> {
        let app_name = self
            .app_name()
            .ok_or_else(|| anyhow::anyhow!("Either an --app or a --manifest must be specified"))?;
        Ok(match app_name.as_str() {
            "fenix" => {
                if is_before(version, 98) {
                    bail!("Fenix wasn't Nimbus enabled before v98")
                } else if is_before(version, 111) {
                    "nimbus.fml.yaml"
                } else if is_before(version, 112) {
                    "fenix/nimbus.fml.yaml"
                } else if is_before(version, 126) {
                    "fenix/app/nimbus.fml.yaml"
                } else {
                    "mobile/android/fenix/app/nimbus.fml.yaml"
                }
            }
            "focus_android" => {
                if is_before(version, 102) {
                    bail!("Focus for Android wasn't Nimbus enabled before v102")
                } else if is_before(version, 110) {
                    "nimbus.fml.yaml"
                } else if is_before(version, 112) {
                    "focus-android/nimbus.fml.yaml"
                } else if is_before(version, 126) {
                    "focus-android/app/nimbus.fml.yaml"
                } else {
                    "mobile/android/focus-android/app/nimbus.fml.yaml"
                }
            }
            "firefox_ios" => {
                if is_before(version, 98) {
                    bail!("Firefox for iOS wasn't Nimbus enabled before v98")
                } else if is_before(version, 122) {
                    "nimbus.fml.yaml"
                } else {
                    "firefox-ios/nimbus.fml.yaml"
                }
            }
            "focus_ios" => {
                if is_before(version, 108) {
                    bail!("Focus wasn't Nimbus enabled before v108")
                } else if is_before(version, 122) {
                    "nimbus.fml.yaml"
                } else {
                    "focus-ios/nimbus.fml.yaml"
                }
            }
            _ => anyhow::bail!("{} is not defined", app_name),
        })
    }
}

pub(crate) fn rs_production_server() -> String {
    std::env::var("NIMBUS_URL")
        .unwrap_or_else(|_| "https://firefox.settings.services.mozilla.com".to_string())
}

pub(crate) fn rs_stage_server() -> String {
    std::env::var("NIMBUS_URL_STAGE")
        .unwrap_or_else(|_| "https://firefox.settings.services.allizom.org".to_string())
}

pub(crate) fn api_v6_production_server() -> String {
    std::env::var("NIMBUS_API_URL")
        .unwrap_or_else(|_| "https://experimenter.services.mozilla.com".to_string())
}

pub(crate) fn api_v6_stage_server() -> String {
    std::env::var("NIMBUS_API_URL_STAGE")
        .unwrap_or_else(|_| "https://stage.experimenter.nonprod.webservices.mozgcp.net".to_string())
}

pub(crate) fn manifest_cache_dir() -> Option<PathBuf> {
    match std::env::var("NIMBUS_MANIFEST_CACHE") {
        Ok(s) => {
            let cwd = std::env::current_dir().expect("Current Working Directory is not set");
            Some(cwd.join(s))
        }
        // We let the Nimbus FML define its own cache.
        _ => None,
    }
}

#[cfg(feature = "server")]
pub(crate) fn server_port() -> String {
    match std::env::var("NIMBUS_CLI_SERVER_PORT") {
        Ok(s) => s,
        _ => "8080".to_string(),
    }
}

#[cfg(feature = "server")]
pub(crate) fn server_host() -> String {
    match std::env::var("NIMBUS_CLI_SERVER_HOST") {
        Ok(s) => s,
        _ => {
            use local_ip_address::local_ip;
            let ip = local_ip().unwrap();
            ip.to_string()
        }
    }
}
