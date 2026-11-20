/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! Read-only access to Firefox prefs.
//!
//! There's a small set of prefs we're interested in, so rather than supporting random-access of
//! arbitrary prefs, we simply load those we need in a single pass.

mod parser;

use crate::std::{
    fs::{read_dir, read_to_string, ReadDir},
    io::ErrorKind,
    path::{Path, PathBuf},
};
use once_cell::sync::Lazy;
use parser::PrefPattern;

/// Create static `PrefPattern` values (accessed through Deref).
///
/// This expects zero or more statements of the form
/// ```ignore
/// static NAME_OF_STATIC = "pref.key.string";
/// ```
/// This produces a `static NAME_OF_STATIC: Lazy<PrefPattern>` which searches for prefs with the key
/// `"pref.key.string"`.
macro_rules! pref_patterns {
    () => {};
    ( static $name:ident = $key:literal; $($rest:tt)* ) => {
        static $name: Lazy<PrefPattern> =
            Lazy::new(|| PrefPattern::new($key).unwrap());
        pref_patterns!($($rest)*);
    }
}

pref_patterns! {
    static LOCALE_PREF = "intl.locale.requested";
    static TELEMETRY_ENABLED_PREF = "datareporting.healthreport.uploadEnabled";
    static TELEMETRY_SERVER_PREF = "toolkit.telemetry.server";
}

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
    /// Update prefs from the given application directory.
    ///
    /// This will load files from known defaults directories. On Linux, this includes system
    /// directories as well (not under `app_dir`).
    pub fn update_from_app_dir(&mut self, app_dir: &Path) -> &mut Self {
        let prefs_files = [
            app_dir.join("defaults/pref"),
            app_dir.join("defaults/preferences"),
            // legacy for debian official packages, see bug 2070693
            #[cfg(target_os = "linux")]
            app_dir.join("defaults/syspref"),
            // On linux, there is a sysconf directory to check.
            //
            // System-scoped directories have priority over install-scoped, so they come later in
            // the list.
            #[cfg(target_os = "linux")]
            format!(
                "{sysconf_dir}/{appname}/defaults/pref",
                sysconf_dir = if crate::config::linux::is_running_under_flatpak() {
                    "/app/etc"
                } else {
                    "/etc"
                },
                appname = mozbuild::config::MOZ_APP_BASENAME.to_lowercase()
            )
            .into(),
        ]
        .into_iter()
        .flat_map(|dir| {
            // Files in each directory are loaded in reverse-alphabetical order due to a [design][] long
            // ago, so we have to aggregate these into a Vec and sort it appropriately.
            //
            // [design]: https://searchfox.org/firefox-main/rev/d35ef8d3940ee6929400750d80d7e32d80f2c29d/modules/libpref/src/nsPrefService.cpp#651-653
            let mut files: Vec<_> = JsFilesInDir::new(dir).collect();
            files.sort_unstable_by(|a, b| b.file_name().cmp(&a.file_name()));
            files
        });

        for file in prefs_files {
            self.update_from_file(&file);
        }

        self
    }

    /// Update prefs from the given profile directory.
    pub fn update_from_profile_dir(&mut self, profile_dir: &Path) -> &mut Self {
        self.update_from_file(&profile_dir.join("prefs.js"));
        self.update_from_file(&profile_dir.join("user.js"));
        self
    }

    /// Update prefs from the given file.
    ///
    /// Any prefs set in the file will take precedence over loaded values.
    pub fn update_from_file(&mut self, prefs_file: &Path) -> &mut Self {
        let content = match read_to_string(prefs_file) {
            Ok(s) => s.into(),
            Err(e) => {
                if e.kind() != ErrorKind::NotFound {
                    log::error!("failed to read prefs file at {}: {e}", prefs_file.display());
                }
                return self;
            }
        };

        self.paths.push(prefs_file.to_owned());

        if let Some(new_value) = TELEMETRY_SERVER_PREF
            .find(&content)
            .and_then(|v| v.into_str())
        {
            self.telemetry_server = Some(new_value.into());
        }

        if let Some(new_value) = TELEMETRY_ENABLED_PREF
            .find(&content)
            .and_then(|v| v.into_bool())
        {
            self.telemetry_enabled = Some(new_value);
        }

        if let Some(new_value) = LOCALE_PREF.find(&content).and_then(|v| v.into_str()) {
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

/// An iterator over files with a .js extension in a given directory.
struct JsFilesInDir {
    dir: Option<ReadDir>,
    path: PathBuf,
}

impl JsFilesInDir {
    fn new<P: AsRef<Path>>(dir: P) -> Self {
        let path = dir.as_ref().to_owned();
        let dir = match read_dir(dir) {
            Err(e) => {
                if e.kind() != ErrorKind::NotFound {
                    log::warn!("failed to read pref directory {}: {e}", path.display());
                }
                None
            }
            Ok(dir) => Some(dir),
        };
        Self { dir, path }
    }
}

impl Iterator for JsFilesInDir {
    type Item = PathBuf;

    fn next(&mut self) -> Option<Self::Item> {
        loop {
            let entry = self.dir.as_mut()?.next()?;
            match entry {
                Err(e) => {
                    log::warn!(
                        "failed while reading pref directory {}: {e}",
                        self.path.display()
                    );
                    self.dir = None;
                    return None;
                }
                Ok(entry) => {
                    let path = entry.path();
                    if entry
                        .file_type()
                        .map(|ft| ft.is_file() || ft.is_symlink())
                        .unwrap_or(false)
                        && path.extension().map(|e| e == "js").unwrap_or(false)
                    {
                        return Some(path);
                    }
                }
            }
        }
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

    #[test]
    fn directories() {
        for default_dir in ["pref", "preferences"] {
            let files = MockFiles::new();
            files
                .add_dir("app_dir")
                .add_dir("app_dir/defaults")
                .add_dir(format!("app_dir/defaults/{default_dir}"))
                .add_file(format!("app_dir/defaults/{default_dir}/prefs.js"), "");
            let prefs = mock::builder().set(MockFS, files).run(|| {
                let mut prefs = Prefs::default();
                prefs.update_from_app_dir(Path::new("app_dir"));
                prefs
            });
            assert_eq!(
                prefs.paths,
                [format!("app_dir/defaults/{default_dir}/prefs.js").into()]
            );
        }
    }

    #[test]
    fn file_priorities() {
        let files = MockFiles::new();
        files
            .add_dir("profile_dir")
            .add_file("profile_dir/prefs.js", "")
            .add_file("profile_dir/user.js", "")
            .add_dir("app_dir")
            .add_dir("app_dir/defaults")
            .add_dir("app_dir/defaults/pref")
            .add_dir("app_dir/defaults/preferences")
            .add_file("app_dir/defaults/preferences/c_pref.js", "")
            .add_file("app_dir/defaults/pref/a_notjs.ts", "")
            .add_file("app_dir/defaults/pref/b_pref.js", "")
            .add_file("app_dir/defaults/pref/c_pref.js", "");
        let prefs = mock::builder().set(MockFS, files).run(|| {
            let mut prefs = Prefs::default();
            prefs
                .update_from_app_dir(Path::new("app_dir"))
                .update_from_profile_dir(Path::new("profile_dir"));
            prefs
        });
        assert_eq!(
            prefs.paths,
            [
                "app_dir/defaults/pref/c_pref.js".into(),
                "app_dir/defaults/pref/b_pref.js".into(),
                "app_dir/defaults/preferences/c_pref.js".into(),
                "profile_dir/prefs.js".into(),
                "profile_dir/user.js".into(),
            ]
        );
    }

    #[cfg(target_os = "linux")]
    #[test]
    fn linux_file_priorities() {
        for in_flatpak in [false, true] {
            let appname = mozbuild::config::MOZ_APP_BASENAME.to_lowercase();
            let files = MockFiles::new();
            files
                .add_dir("app_dir")
                .add_dir("app_dir/defaults")
                .add_dir("app_dir/defaults/syspref")
                .add_dir("/etc")
                .add_dir(format!("/etc/{appname}"))
                .add_dir(format!("/etc/{appname}/defaults"))
                .add_dir(format!("/etc/{appname}/defaults/pref"))
                .add_dir("/app")
                .add_dir("/app/etc")
                .add_dir(format!("/app/etc/{appname}"))
                .add_dir(format!("/app/etc/{appname}/defaults"))
                .add_dir(format!("/app/etc/{appname}/defaults/pref"))
                .add_file("app_dir/defaults/syspref/syspref.js", "")
                .add_file(format!("/etc/{appname}/defaults/pref/pref.js"), "")
                .add_file(
                    format!("/app/etc/{appname}/defaults/pref/flatpakpref.js"),
                    "",
                );
            let prefs = mock::builder()
                .set(MockFS, files)
                .set(crate::config::linux::IS_RUNNING_UNDER_FLATPAK, in_flatpak)
                .run(|| {
                    let mut prefs = Prefs::default();
                    prefs
                        .update_from_app_dir(Path::new("app_dir"))
                        .update_from_profile_dir(Path::new("profile_dir"));
                    prefs
                });
            assert_eq!(
                prefs.paths,
                [
                    "app_dir/defaults/syspref/syspref.js".into(),
                    if in_flatpak {
                        format!("/app/etc/{appname}/defaults/pref/flatpakpref.js")
                    } else {
                        format!("/etc/{appname}/defaults/pref/pref.js")
                    }
                    .into()
                ]
            );
        }
    }
}
