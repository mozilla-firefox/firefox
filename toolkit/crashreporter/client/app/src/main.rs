/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! The crash reporter application.
//!
//! # Architecture
//! The application uses a simple declarative [UI model](ui::model) to define the UI. This model
//! contains [data bindings](data) which provide the dynamic behaviors of the UI. Separate UI
//! implementations for linux (gtk), macos (cocoa), and windows (win32) exist, as well as a test UI
//! which is virtual (no actual interface is presented) but allows runtime introspection.
//!
//! # Mocking
//! This application contains mock interfaces for all the `std` functions it uses which interact
//! with the host system. You can see their implementation in [`crate::std`]. To enable mocking,
//! use the `mock` feature or build with `MOZ_CRASHREPORTER_MOCK` set (which, in `build.rs`, is
//! translated to a `cfg` option). *Note* that this cfg _must_ be enabled when running tests.
//! Unfortunately it is not possible to detect whether tests are being built in `build.rs, which
//! is why a feature needed to be made in the first place (it is enabled automatically when running
//! `mach rusttests`).
//!
//! Currently the input program configuration which is mocked when running the application is fixed
//! (see the [`main`] implementation in this file). If needed in the future, it would be nice to
//! extend this to allow runtime tweaking.
//!
//! # Development
//! Because of the mocking support previously mentioned, in generally any `std` imports should
//! actually use `crate::std`. If mocked functions/types are missing, they should be added with
//! appropriate mocking hooks.

// Use the WINDOWS windows subsystem. This prevents a console window from opening with the
// application.
#![cfg_attr(windows, windows_subsystem = "windows")]

use crate::std::sync::Arc;
#[cfg(not(test))]
use anyhow::Context;
use config::Config;

// A few macros are defined here to allow use in all submodules via textual scope lookup.

/// cc is short for Clone Capture, a shorthand way to clone a bunch of values before an expression
/// (particularly useful for closures).
macro_rules! cc {
    ( ($($c:ident),*) $e:expr ) => {
        {
            $(let $c = $c.clone();)*
            $e
        }
    }
}

/// Create a string literal to be used as an environment variable name.
///
/// This adds the application prefix `MOZ_CRASHREPORTER_`.
macro_rules! ekey {
    ( $name:literal ) => {
        concat!("MOZ_CRASHREPORTER_", $name)
    };
}

mod analyze;
mod async_task;
mod config;
mod data;
mod glean;
mod lang;
mod logging;
mod logic;
mod memory_test;
mod net;
mod prefs;
mod process;
mod send_ping;
mod settings;
mod std;
mod thread_bound;
mod ui;

#[cfg(test)]
mod test;

fn main() {
    // Determine the mode in which to run. This is very simplistic, but need not be more permissive
    // nor flexible since we control how the program is invoked. We don't use the mocked version
    // because we want the actual args.
    match ::std::env::args_os().nth(1) {
        Some(s) if s == "--analyze" => analyze::main(),
        Some(s) if s == "--memtest" => memory_test::main(),
        Some(s) if s == "--send-ping" => send_ping::main(),
        Some(s) if s == "--ping-cleanup" => send_ping::cleanup_main(),
        _ => report_main(),
    }
}

#[cfg(not(mock))]
fn report_main() {
    // Close unused fds before doing anything else, which might open some.
    #[cfg(unix)]
    let fd_cleanup_error = fd_cleanup::cleanup_unused_fds();

    let log_target = logging::init();

    let mut config = Config::new();
    config.log_target = Some(log_target);
    config.read_from_environment();

    #[cfg(unix)]
    if let Err(e) = fd_cleanup_error {
        log::warn!("fd cleanup failed: {e}");
    }

    let mut config = Arc::new(config);

    match try_run(&mut config) {
        Ok(attempted_send) => {
            if !attempted_send {
                // Exited without attempting to send the crash report; delete files.
                config.delete_files();
            }
        }
        Err(message) => {
            // TODO maybe errors should also delete files?
            log::error!("exiting with error: {message:#}");
            if !config.auto_submit {
                // Only show a dialog if auto_submit is disabled.
                ui::error_dialog(config, message);
            }
            std::process::exit(1);
        }
    }
}

#[cfg(mock)]
fn report_main() {
    // TODO it'd be nice to be able to set these values at runtime in some way when running the
    // mock application.

    use crate::std::{
        fs::{MockFS, MockFiles},
        mock,
        process::Command,
    };
    const MOCK_MINIDUMP_EXTRA: &str = r#"{
                            "Vendor": "FooCorp",
                            "ProductName": "Bar",
                            "ReleaseChannel": "release",
                            "BuildID": "1234",
                            "StackTraces": "{}",
                            "Version": "100.0",
                            "ServerURL": "https://reports.example",
                            "TelemetryServerURL": "https://telemetry.example",
                            "TelemetryClientId": "telemetry_client",
                            "TelemetryProfileGroupId": "telemetry_profile_group",
                            "TelemetrySessionId": "telemetry_session",
                            "URL": "https://url.example"
                        }"#;

    // Actual content doesn't matter, aside from the hash that is generated.
    const MOCK_MINIDUMP_FILE: &[u8] = &[1, 2, 3, 4];
    const MOCK_CURRENT_TIME: &str = "2004-11-09T12:34:56Z";
    const MOCK_REMOTE_CRASH_ID: &str = "8cbb847c-def2-4f68-be9e-000000000000";

    // Initialize logging but don't set it in the configuration, so that it won't be redirected to
    // a file (only shown on stderr).
    logging::init();

    // Create a default set of files which allow successful operation.
    let mock_files = MockFiles::new();
    mock_files
        .add_file("minidump.dmp", MOCK_MINIDUMP_FILE)
        .add_file("minidump.extra", MOCK_MINIDUMP_EXTRA);

    // Create a default mock environment which allows successful operation.
    let mut mock = mock::builder();
    mock.set(
        Command::mock("work_dir/pingsender"),
        Box::new(|_| Ok(crate::std::process::success_output())),
    )
    .set(
        Command::mock("curl"),
        Box::new(|_| {
            let mut output = crate::std::process::success_output();
            output.stdout = format!("CrashID={MOCK_REMOTE_CRASH_ID}").into();
            // Network latency.
            std::thread::sleep(std::time::Duration::from_secs(2));
            Ok(output)
        }),
    )
    .set(MockFS, mock_files.clone())
    .set(
        crate::std::env::MockCurrentExe,
        "work_dir/crashreporter".into(),
    )
    .set(crate::std::env::MockTempDir, "tmp".into())
    .set(
        crate::std::time::MockCurrentTime,
        time::OffsetDateTime::parse(
            MOCK_CURRENT_TIME,
            &time::format_description::well_known::Iso8601::DEFAULT,
        )
        .unwrap()
        .into(),
    )
    .set(mock::MockHook::new("enable_glean_pings"), false);

    let result = mock.run(|| {
        let mut cfg = Config::new();
        cfg.data_dir = Some("data_dir".into());
        cfg.events_dir = Some("events_dir".into());
        cfg.dump_file = Some("minidump.dmp".into());
        cfg.restart_command = Some("mockfox".into());
        cfg.strings = Some(lang::load());

        let mut cfg = Arc::new(cfg);
        try_run(&mut cfg)
    });

    if let Err(e) = result {
        log::error!("exiting with error: {e}");
        std::process::exit(1);
    }
}

fn try_run(config: &mut Arc<Config>) -> anyhow::Result<bool> {
    if config.dump_file.is_none() {
        if !config.auto_submit {
            Err(anyhow::anyhow!(config.string("crashreporter-information")))
        } else {
            Ok(false)
        }
    } else if !config.dump_file().exists() {
        // Bug 1959875: If the minidump file doesn't exist, it indicates that an error occurred
        // when generating the minidump, and we should return a specific error message to make
        // things clear to the user.
        Err(anyhow::anyhow!(
            config.string("crashreporter-error-failed-to-generate-minidump")
        ))
    } else {
        // Use minidump-analyzer to gather stack traces.
        #[cfg(not(mock))]
        {
            if let Err(e) = minidump_analyzer::MinidumpAnalyzer::new(config.dump_file())
                .all_threads(config.dump_all_threads)
                .analyze()
            {
                // Minidump analysis gives optional additional information; if it fails, we should
                // still proceed.
                log::warn!("minidump analyzer failed: {e}");
            }
        }

        let extra = {
            // Perform a few things which may change the config, then treat it as immutable.
            let config = Arc::get_mut(config).expect("unexpected config references");
            let extra = config.load_extra_file()?;
            config.move_crash_data_to_pending()?;
            extra
        };

        // Initialize glean here since it relies on the data directory (which will not change after
        // this point). We could potentially initialize it even later (only just before we need
        // it), however we may use it for more than just the crash ping in the future, in which
        // case it makes more sense to do it as early as possible.
        //
        // When we are testing, glean will already be initialized (if needed).
        #[cfg(not(test))]
        let glean_handle = glean::InitOptions::from_config(&config)
            .init()
            .context("failed to acquire Glean store")?;

        let report_crash = logic::ReportCrash::new(config.clone(), extra)?;
        #[cfg(not(test))]
        let report_crash = report_crash.with_glean_handle(glean_handle);
        report_crash.run()
    }
}

/// Close inherited fds which we don't need.
///
/// This is important since we may re-launch Firefox (which may crash again, accumulating open fds
/// all the while). See bug 1986095.
///
/// The `close_fds` crate does this in a more comprehensive way, so we may consider vendoring that in
/// the future.
#[cfg(all(unix, not(mock)))]
mod fd_cleanup {
    unsafe extern "C" {
        fn close(fd: std::ffi::c_int) -> std::ffi::c_int;
    }

    pub fn cleanup_unused_fds() -> anyhow::Result<()> {
        use anyhow::Context;

        let fd_dir: &str = match std::env::consts::OS {
            "linux" => "/proc/self/fd",
            "macos" => "/dev/fd",
            os => anyhow::bail!("unimplemented for target os {os}"),
        };

        let dir =
            std::fs::read_dir(fd_dir).with_context(|| format!("failed to enumerate {fd_dir}"))?;

        // Aggregate the fds to close so that we don't close the ReadDir fd (we could get this fd by
        // using libc/nix, but at the time of this writing those crates aren't dependencies, and the
        // workaround is simple enough).
        let mut fds_to_close = Vec::new();
        for entry_result in dir {
            let entry = match entry_result {
                Ok(entry) => entry,
                Err(e) => {
                    log::warn!("failed to enumerate {fd_dir}: {e}");
                    continue;
                }
            };
            let filename = entry.file_name();
            // These should all be valid utf-8
            let Some(filename) = filename.to_str() else {
                continue;
            };
            let fd: std::os::fd::RawFd = match filename.parse() {
                Ok(n) => n,
                Err(e) => {
                    log::warn!("failed to parse {filename} as an fd: {e}");
                    continue;
                }
            };
            // Ignore negative, stdin (0), stdout (1), or stderr(2) fds.
            if fd >= 3 {
                fds_to_close.push(fd);
            }
        }

        for fd in fds_to_close {
            // We can ignore errors (e.g. inevitably there will be at least one error from closing the
            // ReadDir fd which is now closed). We use libc `close` directly since OwnedFd has the
            // invariant that the fd is open (and has a debug assert checking this).
            unsafe { close(fd) };
        }

        Ok(())
    }
}

// `std` uses `raw-dylib` to link this dll, but that doesn't work properly on x86 MinGW, so we explicitly
// have to link it.
#[cfg(all(target_os = "windows", target_env = "gnu"))]
#[link(name = "bcryptprimitives")]
extern "C" {}

#[cfg(windows)]
#[link(name = "rpcrt4")]
extern "C" {}
