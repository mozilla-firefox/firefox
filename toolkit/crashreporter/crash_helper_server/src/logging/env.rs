/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

use cfg_if::cfg_if;
use mozbuild::config;
use std::{
    fs::{self, File},
    io,
    path::PathBuf,
};

/// Initialize logging
pub(crate) fn init() {
    let user_app_data_dir = guess_user_app_data_dir();
    let log_target = make_log_target(user_app_data_dir);

    env_logger::builder()
        .filter_level(log::LevelFilter::Warn)
        .parse_env(
            env_logger::Env::new()
                .filter("CRASH_HELPER_LOG")
                .write_style("CRASH_HELPER_LOG_STYLE"),
        )
        .target(env_logger::fmt::Target::Pipe(log_target))
        .init();
}

// The crash helper might be launched before Firefox has a chance to provide
// the UAppData special directory, so we generate its value autonomously here.
fn guess_user_app_data_dir() -> Option<PathBuf> {
    let home_dir = dirs::home_dir()?;

    cfg_if! {
        if #[cfg(target_os = "linux")] {
            Some(home_dir.join(".mozilla").join(config::MOZ_APP_NAME))
        } else if #[cfg(target_os = "macos")] {
            Some(home_dir.join("Library").join("Application Support").join(config::MOZ_APP_BASENAME))
        } else if #[cfg(target_os = "windows")] {
            Some(home_dir.join("AppData").join("Roaming").join(config::MOZ_APP_VENDOR).join(config::MOZ_APP_BASENAME))
        } else {
            None
        }
    }
}

fn make_log_target(log_directory: Option<PathBuf>) -> Box<dyn io::Write + Send + 'static> {
    if let Some(log_directory) = log_directory.map(|path| path.join("Crash Reports")) {
        if fs::create_dir_all(&log_directory).is_ok() {
            let log_path = log_directory.join(concat!(env!("CARGO_PKG_NAME"), ".log"));
            if let Ok(log) = File::create(log_path) {
                return Box::new(log);
            }
        }
    }

    Box::new(io::stderr())
}
