/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! An entry point for sending a crash ping.

use crate::std::{env, io::stdin, path::PathBuf};
use crate::{config::installation_path, glean, logging, net::ping, prefs::Prefs};

pub fn main() {
    logging::init();

    let mut args = env::args_os().skip(2);
    let data_path = args.next().expect("no data path provided");
    let reason = args.next().expect("no crash reason provided");

    let extra: serde_json::Value =
        serde_json::from_reader(stdin()).expect("failed to read extra data from stdin");

    let profile_dir = extra
        .get("ProfileDirectory")
        .and_then(|v| v.as_str())
        .map(PathBuf::from);

    let _glean_handle = load_glean(data_path.into(), profile_dir);

    ping::CrashPing {
        extra: &extra,
        reason: reason.to_str(),
    }
    .send();

    // Increase our chances of sending the ping immediately by explicitly shutting down Glean.
    ::glean::shutdown();
}

/// Just initialize Glean to allow any unsubmitted pings to be sent.
pub fn cleanup_main() {
    logging::init();

    let mut args = env::args_os().skip(2);
    let data_path = args.next().expect("no data path provided");
    let profile_dir = args.next();

    let _glean_handle = load_glean(data_path.into(), profile_dir.map(|s| s.into()));

    // Sleep for a short period for Glean to do its thing in the background (and so that
    // `glean::shutdown()` won't log a warning about waiting for init to complete).
    std::thread::sleep(std::time::Duration::from_secs(2));

    // Glean shutdown will block (for a period) on at least one ping to be sent.
    ::glean::shutdown();
}

fn load_glean(data_path: PathBuf, profile_dir: Option<PathBuf>) -> crashping::GleanHandle {
    let mut prefs = Prefs::default();
    prefs.update_from_app_dir(installation_path());
    if let Some(dir) = profile_dir {
        prefs.update_from_profile_dir(dir.as_ref());
    }

    glean::InitOptions::new(data_path.into())
        .with_prefs(&prefs)
        .init()
        .expect("failed to acquire Glean store")
}
