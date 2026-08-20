/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use crate::config::{Application, Application::*};

/// Enumeration containing all Rust components.
/// When adding new variants, make sure to also update the impl block below
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Component {
    Autofill,
    Fxa,
    Logins,
    Places,
    RemoteSettings,
    Suggest,
    Tabs,
}

impl Component {
    /// Unique name for the component in slug format (lower-case letters + dashes).
    pub fn slug(&self) -> &'static str {
        match self {
            Self::Autofill => "autofill",
            Self::Fxa => "fxa",
            Self::Logins => "logins",
            Self::Places => "places",
            Self::RemoteSettings => "remote-settings",
            Self::Suggest => "suggest",
            Self::Tabs => "tabs",
        }
    }

    /// Applications your component ships on
    pub fn applications(&self) -> &[Application] {
        match self {
            Self::Autofill => &[Android, Ios],
            Self::Fxa => &[Android, Ios],
            Self::Logins => &[Desktop, Android, Ios],
            Self::Places => &[Android, Ios],
            Self::RemoteSettings => &[Desktop, Android, Ios],
            Self::Suggest => &[Desktop, Android, Ios],
            Self::Tabs => &[Desktop, Android, Ios],
        }
    }

    /// Prefix for error strings.
    ///
    /// This is the common prefix for strings sent to the `error_support`.  You can usually find it
    /// by going to `error.rs` for your component and looking at the `report_error` calls.
    pub fn error_prefix(&self) -> &'static str {
        match self {
            Self::Autofill => "autofill-",
            Self::Fxa => "fxa-client-",
            Self::Logins => "logins-",
            Self::Places => "places-",
            Self::RemoteSettings => "remote-settings-",
            Self::Suggest => "suggest-",
            Self::Tabs => "tabs-",
        }
    }

    /// Sync engine names
    ///
    /// These represent 2 things:
    ///   - The Glean pings for the component without the `-sync` suffix.
    ///   - The `engine.name` value for the legacy `telemetry.sync` table.
    pub fn sync_engines(&self) -> &[&'static str] {
        match self {
            Self::Autofill => &["addresses", "creditcards"],
            Self::Fxa => &[],
            Self::Logins => &["logins", "rust-logins"],
            Self::Places => &["bookmarks", "history"],
            Self::RemoteSettings => &[],
            Self::Suggest => &[],
            Self::Tabs => &["tabs"],
        }
    }
}
