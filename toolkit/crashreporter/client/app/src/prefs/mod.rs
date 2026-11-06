/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! Read-only access to Firefox prefs.
//!
//! There's a small set of prefs we're interested in, so rather than supporting random-access of
//! arbitrary prefs, we simply load those we need in a single pass.

mod parser;

use crate::std::{
    fs::read_to_string,
    io::ErrorKind,
    path::{Path, PathBuf},
};

const LOCALE_PREF_KEY: &str = "intl.locale.requested";
const TELEMETRY_ENABLED_PREF_KEY: &str = "datareporting.healthreport.uploadEnabled";
const TELEMETRY_SERVER_PREF_KEY: &str = "toolkit.telemetry.server";

/// Loaded prefs values.
#[derive(Default, Debug)]
pub struct Prefs {
    telemetry_enabled: Option<bool>,
    telemetry_server: Option<String>,
    requested_locales: Option<Vec<String>>,
    /// Paths from which prefs have been read.
    paths: Vec<PathBuf>,
}

impl Prefs {
    /// Update prefs from the given profile directory.
    pub fn update_from_profile_dir(&mut self, profile_dir: &Path) -> &mut Self {
        self.update_from_file(&profile_dir.join("prefs.js"));
        self
    }

    /// Update prefs from the given file.
    ///
    /// Any prefs set in the file will take precedence over loaded values.
    pub fn update_from_file(&mut self, prefs_file: &Path) -> &mut Self {
        let content = match read_to_string(prefs_file) {
            Ok(s) => s,
            Err(e) => {
                if e.kind() != ErrorKind::NotFound {
                    log::error!("failed to read prefs file at {}: {e}", prefs_file.display());
                }
                return self;
            }
        };

        self.paths.push(prefs_file.to_owned());

        if let Some(new_value) = parser::find_string_pref(&content, TELEMETRY_SERVER_PREF_KEY) {
            self.telemetry_server = Some(new_value.into());
        }

        if let Some(new_value) = parser::find_bool_pref(&content, TELEMETRY_ENABLED_PREF_KEY) {
            self.telemetry_enabled = Some(new_value);
        }

        if let Some(new_value) = parser::find_string_pref(&content, LOCALE_PREF_KEY) {
            self.requested_locales = Some(
                new_value
                    .split(",")
                    .map(|s| s.trim())
                    .filter(|s| !s.is_empty())
                    .map(|s| s.into())
                    .collect(),
            );
        }

        self
    }

    /// Whether the loaded prefs indicate that telemetry is enabled.
    pub fn telemetry_enabled(&self) -> Option<bool> {
        self.telemetry_enabled
    }

    /// The telemetry server stored in prefs.
    #[allow(unused)]
    pub fn telemetry_server(&self) -> Option<&str> {
        self.telemetry_server.as_deref()
    }

    /// The requested locales stored in prefs.
    pub fn requested_locales(&self) -> Option<&[String]> {
        self.requested_locales.as_deref()
    }

    /// The paths from which the prefs have been loaded.
    ///
    /// This is suitable for formatting in log messages.
    pub fn display_paths(&self) -> DisplayPrefsPaths<'_> {
        DisplayPrefsPaths { paths: &self.paths }
    }
}

pub struct DisplayPrefsPaths<'a> {
    paths: &'a [PathBuf],
}

impl std::fmt::Display for DisplayPrefsPaths<'_> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        // Forward to the Debug display
        std::fmt::Debug::fmt(self.paths, f)
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::std::{
        fs::{MockFS, MockFiles},
        mock,
        path::Path,
    };

    fn profile_prefs(content: String) -> Prefs {
        let files = MockFiles::new();
        files
            .add_dir("profile_dir")
            .add_file("profile_dir/prefs.js", content);
        mock::builder().set(MockFS, files).run(|| {
            let mut prefs = Prefs::default();
            prefs.update_from_profile_dir(Path::new("profile_dir"));
            prefs
        })
    }

    #[test]
    fn telemetry_enable_pref() {
        for pref_value in [false, true] {
            assert_eq!(
                profile_prefs(format!(
                    r#"user_pref("datareporting.healthreport.uploadEnabled", {pref_value});"#
                ))
                .telemetry_enabled()
                .unwrap_or(true),
                pref_value
            );
        }
    }

    #[test]
    fn telemetry_server_pref() {
        assert_eq!(
            profile_prefs(r#"user_pref("toolkit.telemetry.server", "example.com");"#.into())
                .telemetry_server()
                .unwrap(),
            "example.com"
        );
    }

    #[test]
    fn locales_pref_empty() {
        assert!(
            profile_prefs(r#"user_pref("intl.locale.requested","");"#.into())
                .requested_locales()
                .unwrap()
                .is_empty()
        );
    }

    #[test]
    fn locales_pref() {
        assert_eq!(
            profile_prefs(r#"user_pref("intl.locale.requested", "fr,en-US");"#.into())
                .requested_locales()
                .unwrap(),
            &["fr", "en-US"]
        );
    }
}
