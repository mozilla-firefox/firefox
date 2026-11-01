/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! Command line helper shipped alongside Firefox for working with Windows push
//! notifications. This is currently plumbing only and can be invoked from a console,
//! but implements no commands yet.

// Messages written to standard output are only visible in debug builds
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

mod lifecycle;
mod preferences;
mod pushconnection;
mod widestring;

use std::env;
use std::fs::File;
use std::io::{self, BufRead, ErrorKind};
use std::path::{Path, PathBuf};
use std::process::{Command, ExitCode};
use std::str::FromStr;
use std::thread;
use std::time;

use clap::Parser;

use mozbuild::config::MOZ_APP_NAME;

use crate::preferences::*;
use crate::pushconnection::{Event, PushConnection};

const PROGRAM: &str = env!("CARGO_BIN_NAME");

const COMPATIBILITY_FILENAME: &str = "compatibility.ini";
/// File that every Firefox profile directory carries, used to tell a profile
/// apart from an arbitrary directory. Currently this is compatibility.ini
const PROFILE_MARKER: &str = COMPATIBILITY_FILENAME;

/// File that indicates a profile is currently in use.
const PROFILE_LOCK_NAME: &str = "parent.lock";

/// Default length of time between checking if we need to restart the web socket
/// Can be overwritten by setting the FX_NOTIFICATION_HELPER_CONNECTION_CHECK_TIMEOUT_SECS
/// environment variable
const DEFAULT_CONNECTION_CHECK_TIMEOUT_SECS: u64 = 60;
const LAUNCH_TIMEOUT_ENV_NAME: &str = "FX_NOTIFICATION_HELPER_CONNECTION_CHECK_TIMEOUT_SECS";
const LAST_PLATFORM_DIR_STR: &str = "LastPlatformDir=";

#[derive(Parser)]
#[command(name = PROGRAM, about, disable_version_flag = true)]
struct Args {
    /// Firefox profile directory this helper serves.
    #[arg(long, value_name = "PATH", required_unless_present = "stop")]
    profile: Option<PathBuf>,

    /// Ask this profile's helper to exit, rather than starting one.
    #[arg(long)]
    stop: bool,
}

/// Verifies if <path> is a real profile directory. Note: it is fairly easy to
/// bypass this check, so this only exists as a spot check for calls to this helper.
/// Do not treat this as a guarantee that the profile is real.
fn check_profile(path: &Path) -> Result<(), String> {
    match path.metadata() {
        Ok(meta) if meta.is_dir() => {}
        Ok(_) => return Err(format!("not a directory: {}", path.display())),
        Err(error) if error.kind() == ErrorKind::NotFound => {
            return Err(format!("no such directory: {}", path.display()));
        }
        Err(error) => return Err(format!("cannot read {}: {error}", path.display())),
    }

    // This will fail for a profile that has never been launched, but that's ok
    // because a profile that has never been launched doesn't need to check for
    // push notifications
    if !path.join(PROFILE_MARKER).is_file() {
        return Err(format!("not a Firefox profile: {}", path.display()));
    }

    Ok(())
}

fn get_firefox_path_from_executable(path: &Path) -> Option<PathBuf> {
    if let Some(parent_path) = path.parent() {
        return Some(parent_path.to_owned());
    }

    return None;
}

fn get_firefox_path_from_profile(profile_path: &Path) -> Option<PathBuf> {
    let compat_path = profile_path.join(COMPATIBILITY_FILENAME);

    if !compat_path.is_file() {
        return None;
    }

    let file = File::open(compat_path).ok()?;

    let line_reader = io::BufReader::new(file).lines();
    for line in line_reader.map_while(Result::ok) {
        if !line.starts_with(LAST_PLATFORM_DIR_STR) {
            continue;
        }

        let slice = &line[LAST_PLATFORM_DIR_STR.len()..];
        let mut exe_path = PathBuf::new();
        exe_path.push(slice.trim_end_matches(&['\r', '\n']));

        return Some(exe_path);
    }

    return None;
}

/// Tries to get a path for the Firefox executable using the profile,
/// and falling back to the current executable path.
fn get_firefox_path(profile_path: &Path, executable_path: &Path) -> Option<PathBuf> {
    let mut firefox_path = get_firefox_path_from_profile(profile_path)
        .or_else(|| get_firefox_path_from_executable(executable_path))
        .or_else(|| {
            eprintln!("Couldn't determine the Firefox installation path");
            None
        })?;

    firefox_path.push(MOZ_APP_NAME.to_owned() + ".exe");

    return if firefox_path.exists() {
        Some(firefox_path)
    } else {
        return None;
    };
}

/// Checks if Firefox is running with the profile at profile_path.
/// returns: bool indicating if Firefox is running or not.
fn is_firefox_running(profile_path: &Path) -> bool {
    let lock_path = profile_path.join(PROFILE_LOCK_NAME);
    if lock_path.is_file() {
        #[cfg(not(windows))]
        compile_error!("This method for checking the lock only works on Windows");
        let result = File::open(lock_path.as_path());
        let firefox_running = match result {
            Ok(_file) => {
                println!("Firefox is not running");
                false
            }
            Err(error) => {
                println!("Firefox is running: {error}");
                true
            }
        };

        if firefox_running {
            // This profile is currently in use, so we don't need to check it
            println!("Firefox is running");
            return true;
        }
    } else {
        println!("No lock file found at {}", lock_path.display());
    }

    return false;
}

/// Launches firefox with the --receive-push-messages command line, and the
/// --profile argument set to profile_path.
/// Returns true on success, false on failure
fn launch_firefox(profile_path: &Path, firefox_path: &Path) -> bool {
    println!("Launching firefox");
    let result = Command::new(firefox_path)
        .arg("--receive-push-messages")
        .arg("--profile")
        .arg(profile_path)
        .spawn();

    if let Err(message) = result {
        eprintln!("Error launching Firefox: {message}");
        return false;
    }

    return true;
}

fn get_interval_time() -> u64 {
    if let Ok(env_str) = env::var(LAUNCH_TIMEOUT_ENV_NAME) {
        return match u64::from_str(&env_str) {
            Ok(interval) => {
                if interval > 0 {
                    interval
                } else {
                    DEFAULT_CONNECTION_CHECK_TIMEOUT_SECS
                }
            }
            Err(_) => DEFAULT_CONNECTION_CHECK_TIMEOUT_SECS,
        };
    }
    return DEFAULT_CONNECTION_CHECK_TIMEOUT_SECS;
}

fn handle_push_connection(uaid: &str, server_url: &str, profile_path: &Path) -> Result<bool, pushconnection::Error> {
    let mut pushconnection = PushConnection::new();

    println!("Connecting");
    pushconnection.connect(&server_url)?;

    println!("Connected");
    if !pushconnection.send_hello(uaid)? {
        return Ok(false);
    }

    loop {
        let message = pushconnection.wait_for_message()?;
        match message {
            Event::Closed => {
                pushconnection.close();
                return Ok(false);
            }
            Event::Notification => {
                pushconnection.close();
                return Ok(true);
            }
            Event::Uaid(received_uaid) => {
                println!("Got uaid: {received_uaid}");
                if received_uaid != uaid {
                    println!("Need to update {uaid} -> {received_uaid}");
                    if let Err(err) = rewrite_preference_file_for_new_uaid(profile_path, &received_uaid) {
                        eprintln!("Error rewriting preferences file: {:?}", err);
                    };
                }
            }
            Event::Other(message) => {
                println!("Got unknown message: {message}");
            }
        }
    }
}

/// Starts the notification work for `profile`, parks until another process asks
/// it to stop, then tears the work down.
fn run(profile: &Path, firefox_path: &Path) -> ExitCode {
    // Ensure that only one helper exists per profile. Held until the process exits.
    let _guard = match lifecycle::ProfileGuard::acquire(profile) {
        Ok(Some(guard)) => guard,
        Ok(None) => {
            // A helper is already serving this profile. Leave it alone.
            return ExitCode::SUCCESS;
        }
        Err(message) => {
            eprintln!("{PROGRAM}: {message}");
            return ExitCode::FAILURE;
        }
    };

    let stop = match lifecycle::StopEvent::open(profile) {
        Ok(stop) => stop,
        Err(message) => {
            eprintln!("{PROGRAM}: {message}");
            return ExitCode::FAILURE;
        }
    };

    println!("Starting to fetch notifications 🦀 🦊");
    println!("profile: {}", profile.display());

    let task_profile_path = profile.to_owned();
    let task_firefox_path = firefox_path.to_owned();

    let worker = thread::spawn(move || {
        // TODO: all of the websocket work goes here. Connect to the push
        // service and keep reading notifications until the connection is
        // closed out from under this thread.
        let interval_time_ms = get_interval_time() * 1000;
        let duration = time::Duration::from_millis(interval_time_ms);

        println!("Starting check loop");
        loop {
            println!("Checking for Firefox");
            if !is_firefox_running(&task_profile_path) {
                if let Ok(Some((uaid, server_url))) = load_prefs(&task_profile_path) {
                    println!("Starting push connection");

                    match handle_push_connection(&uaid, &server_url, &task_profile_path) {
                        Ok(true) => { println!("Notification received - starting Firefox");
                            launch_firefox(&task_profile_path, &task_firefox_path); },
                        Ok(false) => println!("No notification received"),
                        Err(e) => eprintln!("Push connection failed: {e}")
                    }
                } else {
                    eprintln!("Could not find uaid in prefs.js");
                };
            }

            println!("Sleeping for {interval_time_ms}ms");
            thread::sleep(duration);
        }
    });

    println!("Waiting for stop signal");
    if let Err(message) = stop.wait() {
        eprintln!("{PROGRAM}: {message}");
        return ExitCode::FAILURE;
    }

    drop(worker);

    ExitCode::SUCCESS
}

fn main() -> ExitCode {
    let args = Args::parse();

    let Ok(executable) = std::env::current_exe() else {
        eprintln!("Missing executable name");
        return ExitCode::FAILURE;
    };

    if args.stop {
        return match lifecycle::send_stop_signal(args.profile.as_deref()) {
            Ok(()) => ExitCode::SUCCESS,
            Err(message) => {
                eprintln!("{PROGRAM}: {message}");
                ExitCode::FAILURE
            }
        };
    }

    let profile = args
        .profile
        .as_deref()
        .expect("clap requires --profile unless --stop is given");

    if let Err(message) = check_profile(profile) {
        eprintln!("{PROGRAM}: {message}");
        return ExitCode::FAILURE;
    }

    let Some(firefox_path) = get_firefox_path(profile, &executable) else {
        eprintln!("Could not find Firefox");
        return ExitCode::FAILURE;
    };

    if firefox_path.is_file() {
        println!("Found Firefox at: {:?}", firefox_path);
    } else {
        eprintln!("Found Firefox path {:?} but it is not a file", firefox_path);
        return ExitCode::FAILURE;
    }

    run(profile, &firefox_path)
}

#[cfg(test)]
mod tests {
    use super::*;

    use std::ffi::OsStr;

    use clap::CommandFactory;

    /// clap's own check for a malformed derive: duplicate names, unreachable
    /// arguments, and the like. Cheap, and it fails at build time rather than
    /// on the first user who passes the wrong flag.
    #[test]
    fn the_argument_definition_is_well_formed() {
        Args::command().debug_assert();
    }

    /// clap has no opinion on whether a path exists, so a bogus `--profile`
    /// parses cleanly and is only rejected once `check_profile` looks at it.
    #[test]
    fn a_profile_that_does_not_exist_is_rejected() {
        let path = std::env::temp_dir().join(format!("{PROGRAM}-missing-{}", std::process::id()));
        let args = Args::try_parse_from([
            OsStr::new(PROGRAM),
            OsStr::new("--profile"),
            path.as_os_str(),
        ])
        .unwrap();

        let error = check_profile(args.profile.as_deref().unwrap()).unwrap_err();

        assert!(error.starts_with("no such directory:"), "{error}");
    }

    /// Starting has no default profile, so it must refuse rather than guess.
    /// A bare `--stop` is the exception, covered by
    /// `stop_without_a_profile_is_allowed`.
    #[test]
    fn starting_requires_a_profile() {
        assert!(Args::try_parse_from([PROGRAM]).is_err());
    }

    /// Starting is the default, so an absent --stop must never read as a stop.
    #[test]
    fn stop_is_off_unless_asked_for() {
        let args = Args::try_parse_from([PROGRAM, "--profile", r"c:\profiles\a"]).unwrap();

        assert!(!args.stop);
    }

    #[test]
    fn stop_is_parsed() {
        let args =
            Args::try_parse_from([PROGRAM, "--stop", "--profile", r"c:\profiles\a"]).unwrap();

        assert!(args.stop);
        assert_eq!(args.profile, Some(PathBuf::from(r"c:\profiles\a")));
    }

    /// A bare --stop is the stop-everything spelling, so it must parse without
    /// a profile rather than being rejected the way a bare start is.
    #[test]
    fn stop_without_a_profile_is_allowed() {
        let args = Args::try_parse_from([PROGRAM, "--stop"]).unwrap();

        assert!(args.stop);
        assert_eq!(args.profile, None);
    }

    #[test]
    fn stop_takes_no_value() {
        assert!(Args::try_parse_from([PROGRAM, "--stop=yes", "--profile", r"c:\p"]).is_err());
    }

    /// Firefox starts a helper on every launch, so finding one already running
    /// is the ordinary case rather than a failure.
    #[test]
    fn run_declines_quietly_when_a_helper_already_has_the_profile() {
        let profile = Path::new(r"c:\profiles\run-declines");
        let firefox_path = Path::new(r"c:\fakepath\firefox.exe");

        let _held = lifecycle::ProfileGuard::acquire(profile).unwrap().unwrap();

        assert_eq!(run(profile, firefox_path), ExitCode::SUCCESS);
    }
}
