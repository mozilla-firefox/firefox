/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! Application configuration.

use crate::std::borrow::Cow;
use crate::std::ffi::{OsStr, OsString};
use crate::std::path::{Path, PathBuf};
use crate::std::process::Command;
use crate::{lang, logging::LogTarget, prefs::Prefs, std};
use anyhow::Context;
use once_cell::sync::Lazy;

#[cfg(test)]
pub mod test {
    cfg_if::cfg_if! {
        if #[cfg(target_os = "linux")] {
            use crate::std::{mock, env, fs::MockFS, fs::MockFiles};

            fn cfg_get_data_dir_root() -> crate::std::path::PathBuf {
                let cfg = super::Config::new();
                cfg.get_data_dir_root("vendor").unwrap()
            }

            #[test]
            fn data_dir_root_xdg_default() {
                mock::builder()
                    .set(env::MockHomeDir, "home_dir".into())
                    .run(|| {
                        let path = cfg_get_data_dir_root();
                        assert_eq!(path, crate::std::path::PathBuf::from("home_dir/.config/vendor"));
                     });
            }

            #[test]
            fn data_dir_root_xdg_home() {
                mock::builder()
                    .set(env::MockHomeDir, "home_dir".into())
                    .set(env::MockEnv("XDG_CONFIG_HOME".into()), "home_dir/xdg/config".into())
                    .run(|| {
                        let path = cfg_get_data_dir_root();
                        assert_eq!(path, crate::std::path::PathBuf::from("home_dir/xdg/config/vendor"));
                    });
            }

            #[test]
            fn data_dir_root_legacy_force() {
                mock::builder()
                    .set(env::MockHomeDir, "home_dir".into())
                    .set(env::MockEnv("MOZ_LEGACY_HOME".into()), "1".into())
                    .run(|| {
                        let path = cfg_get_data_dir_root();
                        assert_eq!(path, crate::std::path::PathBuf::from("home_dir/.vendor"));
                    });
            }

            #[test]
            fn data_dir_root_legacy_existing() {
                let mock_files = MockFiles::new();
                mock_files.add_dir("home_dir").add_dir("home_dir/.vendor");

                mock::builder()
                    .set(env::MockHomeDir, "home_dir".into())
                    .set(MockFS, mock_files.clone())
                    .run(|| {
                        let path = cfg_get_data_dir_root();
                        assert_eq!(path, crate::std::path::PathBuf::from("home_dir/.vendor"));
                    });
            }
        }
    }

    #[test]
    fn restart_command_no_extra() {
        let extra = serde_json::from_str("{}").unwrap();
        assert!(!super::Config::should_suppress_restart(&extra));
    }

    #[test]
    fn restart_command_wer() {
        let extra = serde_json::from_str(r#"{"WindowsErrorReporting": "1"}"#).unwrap();
        assert!(super::Config::should_suppress_restart(&extra));
    }

    #[test]
    fn restart_command_standard_shutdown() {
        let extra = serde_json::from_str(
            r#"{
            "ShutdownProgress": "quit-application",
            "ShutdownReason": "AppClose"
        }"#,
        )
        .unwrap();
        assert!(super::Config::should_suppress_restart(&extra));
    }

    #[test]
    fn restart_command_shutdown_for_restart() {
        let extra = serde_json::from_str(
            r#"{
            "ShutdownProgress": "quit-application",
            "ShutdownReason": "AppRestart"
        }"#,
        )
        .unwrap();
        assert!(!super::Config::should_suppress_restart(&extra));
    }
}

mod buildid_section {
    include!(concat!(env!("OUT_DIR"), "/buildid_section.rs"));
}

const VENDOR_KEY: &str = "Vendor";
const PRODUCT_KEY: &str = "ProductName";
const DEFAULT_VENDOR: &str = "Mozilla";
const DEFAULT_PRODUCT: &str = "Firefox";

#[derive(Default)]
pub struct Config {
    /// Whether reports should be automatically submitted.
    pub auto_submit: bool,
    /// Whether all threads of the process should be dumped (versus just the crashing thread).
    pub dump_all_threads: bool,
    /// Whether to delete the dump files after submission.
    pub delete_dump: bool,
    /// Whether to run memtest while ui is shown
    pub run_memtest: bool,
    /// The data directory.
    pub data_dir: Option<PathBuf>,
    /// The events directory.
    pub events_dir: Option<PathBuf>,
    /// The profile directory in use when the crash occurred.
    pub profile_dir: Option<PathBuf>,
    /// The dump file.
    ///
    /// If missing, an error dialog is displayed.
    pub dump_file: Option<PathBuf>,
    /// The XUL_APP_FILE to define if restarting the application.
    pub app_file: Option<OsString>,
    /// The path to the application to use when restarting the crashed process.
    pub restart_command: Option<OsString>,
    /// The arguments to pass if restarting the application.
    pub restart_args: Vec<OsString>,
    /// The URL to which to send reports.
    pub report_url: Option<OsString>,
    /// The localized strings to use.
    pub strings: Option<lang::LangStrings>,
    /// The log target.
    pub log_target: Option<LogTarget>,
    /// The loaded prefs data.
    pub prefs: Prefs,
}

pub struct ConfigStringBuilder<'a>(lang::LangStringBuilder<'a>);

impl<'a> ConfigStringBuilder<'a> {
    /// Set an argument for the string.
    pub fn arg<V: Into<Cow<'a, str>>>(self, key: &'a str, value: V) -> Self {
        ConfigStringBuilder(self.0.arg(key, value))
    }

    /// Get the localized string.
    pub fn get(self) -> String {
        self.0.get()
    }
}

impl Config {
    /// Return a configuration with no values set, and all bool values false.
    pub fn new() -> Self {
        Self::default()
    }

    /// Load the prefs from the installation directory.
    pub fn read_app_prefs(&mut self) {
        self.prefs.update_from_app_dir(installation_path());
    }

    /// Load a configuration from the application environment.
    #[cfg_attr(mock, allow(unused))]
    pub fn read_from_environment(&mut self) {
        self.auto_submit = env_bool(ekey!("AUTO_SUBMIT"));
        self.dump_all_threads = env_bool(ekey!("DUMP_ALL_THREADS"));
        self.delete_dump = !env_bool(ekey!("NO_DELETE_DUMP"));
        self.run_memtest = env_bool(ekey!("RUN_MEMTEST"));
        self.data_dir = env_path(ekey!("DATA_DIRECTORY"));
        self.events_dir = env_path(ekey!("EVENTS_DIRECTORY"));
        self.app_file = std::env::var_os(ekey!("RESTART_XUL_APP_FILE"));

        self.update_log_file();

        // Only support `MOZ_APP_LAUNCHER` on linux and macos.
        if cfg!(not(target_os = "windows")) {
            self.restart_command = std::env::var_os("MOZ_APP_LAUNCHER");
        }

        if self.restart_command.is_none() {
            self.restart_command =
                Some(installation_program_path(mozbuild::config::MOZ_APP_NAME).into())
        }

        // We no longer use `MOZ_CRASHREPORTER_RESTART_ARG_0`, see bug 1872920.
        self.restart_args = (1..)
            .into_iter()
            .map_while(|arg_num| std::env::var_os(format!("{}_{}", ekey!("RESTART_ARG"), arg_num)))
            // Sometimes these are empty, in which case they should be ignored.
            .filter(|s| !s.is_empty())
            .collect();

        self.report_url = std::env::var_os(ekey!("URL"));

        let mut args = std::env::args_os()
            // skip program name
            .skip(1);
        self.dump_file = args.next().map(|p| p.into());
        while let Some(arg) = args.next() {
            log::warn!("ignoring extraneous argument: {}", arg.to_string_lossy());
        }

        self.strings = Some(lang::load());

        self.read_app_prefs();
    }

    /// Get the localized string for the given index.
    pub fn string(&self, index: &str) -> String {
        self.build_string(index).get()
    }

    /// Build the localized string for the given index.
    pub fn build_string<'a>(&'a self, index: &'a str) -> ConfigStringBuilder<'a> {
        ConfigStringBuilder(
            self.strings
                .as_ref()
                .expect("strings not set")
                .builder(index),
        )
    }

    /// Whether the configured language has right-to-left text flow.
    pub fn is_rtl(&self) -> bool {
        self.strings
            .as_ref()
            .map(|s| s.is_rtl())
            .unwrap_or_default()
    }

    /// Load the extra file, updating configuration.
    pub fn load_extra_file(&mut self) -> anyhow::Result<serde_json::Value> {
        let extra_file = self.extra_file().unwrap();

        // Load the extra file (which minidump-analyzer just updated).
        let extra: serde_json::Value =
            serde_json::from_reader(std::fs::File::open(&extra_file).with_context(|| {
                self.build_string("crashreporter-error-opening-file")
                    .arg("path", extra_file.display().to_string())
                    .get()
            })?)
            .with_context(|| {
                self.build_string("crashreporter-error-loading-file")
                    .arg("path", extra_file.display().to_string())
                    .get()
            })?;

        // Set report url if not already set.
        if self.report_url.is_none() {
            if let Some(url) = extra["ServerURL"].as_str() {
                self.report_url = Some(url.into());
            }
        }

        // Set the data dir if not already set.
        // TODO bug 1910736: if we don't need to support VENDOR_KEY and PRODUCT_KEY in the extra
        // file, it'd simplify the data_dir logic and things like glean initialization (which
        // relies on the data dir).
        if self.data_dir.is_none() {
            let vendor = extra[VENDOR_KEY].as_str().unwrap_or(DEFAULT_VENDOR);
            let product = extra[PRODUCT_KEY].as_str().unwrap_or(DEFAULT_PRODUCT);
            self.data_dir = Some(self.get_data_dir(vendor, product)?);
            self.update_log_file();
        }

        if Self::should_suppress_restart(&extra) {
            self.restart_command = None;
        }

        self.load_profile_directory_from_extra(&extra);
        Ok(extra)
    }

    // Clear the restart command if the browser was already shutting down or if WER handled the
    // crash. This prevents restarting the program. See bugs 1872920 and 2012347.
    fn should_suppress_restart(extra: &serde_json::Value) -> bool {
        extra.get("WindowsErrorReporting").is_some()
            || (extra.get("ShutdownProgress").is_some()
                && extra.get("ShutdownReason").and_then(|v| v.as_str()) != Some("AppRestart"))
    }

    /// Load the profile directory from the extra file information.
    ///
    /// This also loads the following information that relies on the profile directory:
    /// * profile-specific prefs
    /// * localization strings from langpacks
    fn load_profile_directory_from_extra(&mut self, extra: &serde_json::Value) {
        let Some(profile_dir) = extra
            .get("ProfileDirectory")
            .and_then(|v| v.as_str())
            .map(PathBuf::from)
        else {
            return;
        };
        if !profile_dir.exists() {
            return;
        }

        self.prefs.update_from_profile_dir(&profile_dir);
        self.profile_dir = Some(profile_dir);

        // Update the localization information.
        if let Some(strings) = self.strings.as_mut() {
            if let Err(e) = strings.add_langpack(
                &self.prefs,
                self.profile_dir.as_deref().unwrap(),
                extra.get("useragent_locale").and_then(|v| v.as_str()),
            ) {
                log::warn!("failed to read localization information from profile: {e:#}")
            }
        }
    }

    /// Get the path to the extra file.
    ///
    /// Returns None if no dump_file is set.
    pub fn extra_file(&self) -> Option<PathBuf> {
        self.dump_file.clone().map(extra_file_for_dump_file)
    }

    /// Get the path to the memory file.
    ///
    /// Returns None if no dump_file is set or if the memory file does not exist.
    pub fn memory_file(&self) -> Option<PathBuf> {
        self.dump_file.clone().and_then(|p| {
            let p = memory_file_for_dump_file(p);
            p.exists().then_some(p)
        })
    }

    /// The path to the data directory.
    ///
    /// Panics if no data directory is set.
    pub fn data_dir(&self) -> &Path {
        self.data_dir.as_deref().unwrap()
    }

    /// The path to the dump file.
    ///
    /// Panics if no dump file is set.
    pub fn dump_file(&self) -> &Path {
        self.dump_file.as_deref().unwrap()
    }

    /// The id of the local dump file (the base filename without extension).
    ///
    /// Panics if no dump file is set.
    pub fn local_dump_id(&self) -> Cow<'_, str> {
        self.dump_file().file_stem().unwrap().to_string_lossy()
    }

    /// Move crash data to the pending folder.
    pub fn move_crash_data_to_pending(&mut self) -> anyhow::Result<()> {
        let pending_crashes_dir = self.data_dir().join("pending");
        std::fs::create_dir_all(&pending_crashes_dir).with_context(|| {
            self.build_string("crashreporter-error-creating-dir")
                .arg("path", pending_crashes_dir.display().to_string())
                .get()
        })?;

        let move_file = |from: &Path| -> anyhow::Result<PathBuf> {
            let to = pending_crashes_dir.join(from.file_name().unwrap());
            // Try to rename, but copy and remove if it fails. `rename` won't work across
            // mount points. (bug 506009)
            if let Err(e) = std::fs::rename(from, &to) {
                log::warn!("failed to move {} to {}: {e}", from.display(), to.display());
                log::info!("trying to copy and remove instead");

                std::fs::copy(from, &to).with_context(|| {
                    self.build_string("crashreporter-error-moving-path")
                        .arg("from", from.display().to_string())
                        .arg("to", to.display().to_string())
                        .get()
                })?;
                if let Err(e) = std::fs::remove_file(from) {
                    log::warn!("failed to remove {}: {e}", from.display());
                }
            }
            Ok(to)
        };

        let new_dump_file = move_file(self.dump_file())?;
        move_file(self.extra_file().unwrap().as_ref())?;
        // Failing to move the memory file is recoverable.
        if let Some(memory_file) = self.memory_file() {
            if let Err(e) = move_file(memory_file.as_ref()) {
                log::warn!("failed to move memory file: {e}");
                if let Err(e) = std::fs::remove_file(&memory_file) {
                    log::warn!("failed to remove {}: {e}", memory_file.display());
                }
            }
        }

        self.dump_file = Some(new_dump_file);

        Ok(())
    }

    /// Form the path which signals EOL for a particular version.
    pub fn version_eol_file(&self, version: &str) -> PathBuf {
        self.data_dir().join(format!("EndOfLife{version}"))
    }

    /// Return the path used to store submitted crash ids.
    pub fn submitted_crash_dir(&self) -> PathBuf {
        self.data_dir().join("submitted")
    }

    /// Delete files related to the crash report.
    pub fn delete_files(&self) {
        if !self.delete_dump {
            return;
        }

        for file in [&self.dump_file, &self.extra_file(), &self.memory_file()]
            .into_iter()
            .flatten()
        {
            if let Err(e) = std::fs::remove_file(file) {
                log::warn!("failed to remove {}: {e}", file.display());
            }
        }
    }

    /// Restart the program based on the configured restart command.
    pub fn restart_process(&self) {
        if self.restart_command.is_none() {
            // The restart button should be hidden in this case, so this error should not occur.
            log::error!("no process configured for restart");
            return;
        }

        let mut cmd = Command::new(self.restart_command.as_ref().unwrap());
        cmd.args(&self.restart_args)
            .stdin(std::process::Stdio::null())
            .stdout(std::process::Stdio::null())
            .stderr(std::process::Stdio::null());
        if let Some(xul_app_file) = &self.app_file {
            cmd.env("XUL_APP_FILE", xul_app_file);
        }
        log::debug!("restarting process: {:?}", cmd);
        if let Err(e) = cmd.spawn() {
            log::error!("failed to restart process: {e}");
        }
    }

    /// Update the log file based on the current configured data_dir.
    fn update_log_file(&self) {
        if let (Some(log_target), Some(data_dir)) = (&self.log_target, &self.data_dir) {
            log_target.set_file(&data_dir.join("submit.log"));
        }
    }

    #[cfg(all(target_os = "linux", any(not(mock), test)))]
    fn get_data_dir_root(&self, vendor: &str) -> anyhow::Result<PathBuf> {
        // home_dir is deprecated due to incorrect behavior on windows, but we only use it on linux
        #[allow(deprecated)]
        let home_dir = std::env::home_dir();

        let legacy_data = home_dir
            .clone()
            .map(|h| h.join(format!(".{}", vendor.to_lowercase())));
        let data_path = if std::env::var_os("MOZ_LEGACY_HOME").is_some()
            || legacy_data.as_ref().expect("No HOME env?").exists()
        {
            legacy_data
        } else {
            std::env::var_os("XDG_CONFIG_HOME")
                .map(PathBuf::from)
                .or_else(|| home_dir.map(|home| home.join(".config")))
                .map(|h| h.join(format!("{}", vendor.to_lowercase())))
        }
        .with_context(|| self.string("crashreporter-error-no-home-dir"))?;
        Ok(data_path)
    }

    cfg_if::cfg_if! {
        if #[cfg(mock)] {
            fn get_data_dir(&self, vendor: &str, product: &str) -> anyhow::Result<PathBuf> {
                let mut path = PathBuf::from("data_dir");
                path.push(vendor);
                path.push(product);
                path.push("Crash Reports");
                Ok(path)
            }
        } else if #[cfg(target_os = "linux")] {
            fn get_data_dir(&self, vendor: &str, product: &str) -> anyhow::Result<PathBuf> {
            let mut data_path = self.get_data_dir_root(vendor)?;
                data_path.push(product.to_lowercase());
                data_path.push("Crash Reports");
                Ok(data_path)
            }
        } else if #[cfg(target_os = "macos")] {
            fn get_data_dir(&self, _vendor: &str, product: &str) -> anyhow::Result<PathBuf> {
                use objc::{
                    rc::autoreleasepool,
                    runtime::{Object, BOOL, YES},
                    *,
                };
                #[link(name = "Foundation", kind = "framework")]
                extern "system" {
                    fn NSSearchPathForDirectoriesInDomains(
                        directory: usize,
                        domain_mask: usize,
                        expand_tilde: BOOL,
                    ) -> *mut Object /* NSArray<NSString*>* */;
                }
                #[allow(non_upper_case_globals)]
                const NSApplicationSupportDirectory: usize = 14;
                #[allow(non_upper_case_globals)]
                const NSUserDomainMask: usize = 1;

                let mut data_path = autoreleasepool(|| {
                    let paths /* NSArray<NSString*>* */ = unsafe {
                                NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES)
                            };
                    if paths.is_null() {
                        anyhow::bail!("NSSearchPathForDirectoriesInDomains returned nil");
                    }
                    let path: *mut Object /* NSString* */ = unsafe { msg_send![paths, firstObject] };
                    if path.is_null() {
                        anyhow::bail!("NSSearchPathForDirectoriesInDomains returned no paths");
                    }

                    let str_pointer: *const i8 = unsafe { msg_send![path, UTF8String] };
                    // # Safety
                    // The pointer is a readable C string with a null terminator.
                    let Ok(s) = unsafe { std::ffi::CStr::from_ptr(str_pointer) }.to_str() else {
                        anyhow::bail!("NSString wasn't valid UTF8");
                    };
                    Ok(PathBuf::from(s))
                })?;
                data_path.push(product);
                std::fs::create_dir_all(&data_path).with_context(|| {
                    self.build_string("crashreporter-error-creating-dir")
                        .arg("path", data_path.display().to_string())
                        .get()
                })?;
                data_path.push("Crash Reports");
                Ok(data_path)
            }
        } else if #[cfg(target_os = "windows")] {
            fn get_data_dir(&self, vendor: &str, product: &str) -> anyhow::Result<PathBuf> {
                use crate::std::os::windows::ffi::OsStringExt;
                use windows_sys::{
                    core::PWSTR,
                    Win32::{
                        Globalization::lstrlenW,
                        System::Com::CoTaskMemFree,
                        UI::Shell::{FOLDERID_RoamingAppData, SHGetKnownFolderPath},
                    },
                };

                let mut path: PWSTR = std::ptr::null_mut();
                let result = unsafe { SHGetKnownFolderPath(&FOLDERID_RoamingAppData, 0, std::ptr::null_mut(), &mut path) };
                if result != 0 {
                    unsafe { CoTaskMemFree(path as _) };
                    anyhow::bail!("failed to get known path for roaming appdata");
                }

                let length = unsafe { lstrlenW(path) };
                let slice = unsafe { std::slice::from_raw_parts(path, length as usize) };
                let osstr = OsString::from_wide(slice);
                unsafe { CoTaskMemFree(path as _) };
                let mut path = PathBuf::from(osstr);
                path.push(vendor);
                path.push(product);
                path.push("Crash Reports");
                Ok(path)
            }
        }
    }
}

/// Get the path of resources for the Firefox installation containing the crashreporter.
pub fn installation_resource_path() -> &'static Path {
    static PATH: Lazy<PathBuf> = Lazy::new(|| {
        if cfg!(all(not(mock), target_os = "macos")) {
            installation_path().parent().unwrap().join("Resources")
        } else {
            installation_path().to_owned()
        }
    });
    &*PATH
}

/// Get the path of a program in the installation.
///
/// The returned path isn't guaranteed to exist.
pub fn installation_program_path<N: AsRef<OsStr>>(program: N) -> PathBuf {
    let self_path = self_path();
    let exe_extension = self_path.extension().unwrap_or_default();
    let mut p = program.as_ref().to_os_string();
    if !exe_extension.is_empty() {
        p.push(".");
        p.push(exe_extension);
    }
    installation_path().join(p)
}

/// Get the path of the Firefox installation containing the crashreporter.
///
/// On MacOS, this assumes that the crashreporter is its own application bundle within the main
/// program bundle. It returns the MacOS directory of the Firefox application bundle.
pub fn installation_path() -> &'static Path {
    static PATH: Lazy<&'static Path> = Lazy::new(|| {
        // Expect shouldn't panic as we don't invoke the program without a parent directory.
        let dir_path = self_path().parent().expect("program invoked based on PATH");

        if cfg!(all(not(mock), target_os = "macos")) {
            // On macOS the crash reporter client is shipped as an application bundle contained
            // within Firefox's main application bundle. So when it's invoked its current working
            // directory looks like:
            // Firefox.app/Contents/MacOS/crashreporter.app/Contents/MacOS/

            // 3rd ancestor (the 0th element of ancestors has no paths removed) to remove
            // `crashreporter.app/Contents/MacOS`.
            if let Some(ancestor) = dir_path.ancestors().nth(3) {
                return ancestor;
            }
        }

        dir_path
    });
    &*PATH
}

/// Read the buildid from the installation.
///
/// This may fail if installation files are not found.
pub fn buildid() -> Option<&'static str> {
    static BUILDID: Lazy<Option<String>> = Lazy::new(|| {
        let section_name = buildid_section::MOZ_BUILDID_SECTION_NAME.to_str().ok()?;
        let xul_path = installation_path().join(if cfg!(target_os = "macos") {
            "XUL"
        } else if cfg!(target_os = "windows") {
            "xul.dll"
        } else {
            "libxul.so"
        });
        #[cfg(mock)]
        let xul_path = xul_path.as_ref();
        match buildid_reader::BuildIdReader::new(&xul_path)
            .and_then(|mut reader| reader.read_string_build_id(section_name))
        {
            Ok(s) => {
                log::info!("read build id from XUL: {s}");
                Some(s)
            }
            Err(e) => {
                log::warn!("failed to read build id from XUL: {e}");
                None
            }
        }
    });
    BUILDID.as_deref()
}

fn self_path() -> &'static Path {
    static PATH: Lazy<PathBuf> = Lazy::new(|| {
        // Expect shouldn't ever panic here because we need more than one argument to run
        // the program in the first place (we've already previously iterated args).
        //
        // We use argv[0] rather than `std::env::current_exe` because `current_exe` doesn't define
        // how symlinks are treated, and we want to support running directly from the local build
        // directory (which uses symlinks on linux and macos).
        PathBuf::from(std::env::args_os().next().expect("failed to get argv[0]"))
    });
    &*PATH
}

#[cfg(target_os = "linux")]
pub mod linux {
    #![cfg_attr(mock, allow(unused))]

    #[cfg(mock)]
    use crate::std::mock::MockKey;
    use crate::std::{mock::mocked_static, path::Path};
    use once_cell::sync::Lazy;

    struct IsRunningUnderFlatpak {
        inner: Lazy<bool>,
    }

    impl IsRunningUnderFlatpak {
        const fn new() -> Self {
            Self {
                inner: Lazy::new(|| {
                    // https://gitlab.gnome.org/GNOME/gtk/-/blob/4300a5c609306ce77cbc8a3580c19201dccd8d13/gdk/gdk.c#L472
                    Path::new("/.flatpak-info").exists()
                }),
            }
        }
    }

    impl IsRunningUnderFlatpak {
        fn get(&self) -> bool {
            *self.inner
        }
    }

    mocked_static! {
        static IS_RUNNING_UNDER_FLATPAK: IsRunningUnderFlatpak = IsRunningUnderFlatpak::new();
        mocked type = bool;

        impl IS_RUNNING_UNDER_FLATPAK {
            fn get(&self) -> bool {
                Self.try_get(|v| *v).unwrap_or(false)
            }
        }
    }

    pub fn is_running_under_flatpak() -> bool {
        IS_RUNNING_UNDER_FLATPAK.get()
    }
}

fn env_bool<K: AsRef<OsStr>>(name: K) -> bool {
    std::env::var(name).map(|s| !s.is_empty()).unwrap_or(false)
}

fn env_path<K: AsRef<OsStr>>(name: K) -> Option<PathBuf> {
    std::env::var_os(name).map(PathBuf::from)
}

fn extra_file_for_dump_file(mut dump_file: PathBuf) -> PathBuf {
    dump_file.set_extension("extra");
    dump_file
}

fn memory_file_for_dump_file(mut dump_file: PathBuf) -> PathBuf {
    dump_file.set_extension("memory.json.gz");
    dump_file
}
