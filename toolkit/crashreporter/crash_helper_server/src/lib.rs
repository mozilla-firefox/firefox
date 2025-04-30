/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#[cfg(any(target_os = "linux", target_os = "android"))]
extern crate rust_minidump_writer_linux;

mod breakpad_crash_generator;
mod crash_generation;
mod ipc_server;
mod logging;
mod phc;

use crash_helper_common::{BreakpadData, BreakpadRawData, IPCConnector, IPCListener, Pid};
use std::ffi::{c_char, CStr, OsString};

use crash_generation::CrashGenerator;
use ipc_server::{IPCServer, IPCServerState};
#[cfg(target_os = "android")]
use std::os::fd::{FromRawFd, OwnedFd, RawFd};

/// Runs the crash generator process logic, this includes the IPC used by
/// processes to signal that they crashed, the IPC used to retrieve crash
/// reports from the crash helper process and the logic used to generate the
/// actual minidumps. This function will return when the main process has
/// disconnected from the crash helper.
///
/// # Safety
///
/// `minidump_data`, `listener` and `pipe` must point to valid,
/// nul-terminated C strings. `breakpad_data` must be a valid file descriptor
/// (Linux) or point to a nul-terminated C string using either byte (macOS)
/// or wide characters (Windows).
#[cfg(not(target_os = "android"))]
#[no_mangle]
pub unsafe extern "C" fn crash_generator_logic_desktop(
    client_pid: Pid,
    breakpad_data: BreakpadRawData,
    minidump_path: *const c_char,
    listener: *const c_char,
    pipe: *const c_char,
) -> i32 {
    logging::init();

    let breakpad_data = BreakpadData::new(breakpad_data);
    let minidump_path = unsafe { CStr::from_ptr(minidump_path) }
        .to_owned()
        .into_string()
        .unwrap();
    let minidump_path = OsString::from(minidump_path);
    let listener = unsafe { CStr::from_ptr(listener) };
    let listener = IPCListener::deserialize(listener, client_pid)
        .map_err(|error| {
            log::error!("Could not parse the crash generator's listener (error: {error})");
        })
        .unwrap();
    let pipe = unsafe { CStr::from_ptr(pipe) };
    let connector = IPCConnector::deserialize(pipe)
        .map_err(|error| {
            log::error!("Could not parse the crash generator's connector (error: {error})");
        })
        .unwrap();

    let crash_generator = CrashGenerator::new(client_pid, breakpad_data, minidump_path)
        .map_err(|error| {
            log::error!("Could not create the crash generator (error: {error})");
            error
        })
        .unwrap();

    let ipc_server = IPCServer::new(client_pid, listener, connector);

    main_loop(ipc_server, crash_generator)
}

/// Runs the crash generator process logic, this includes the IPC used by
/// processes to signal that they crashed, the IPC used to retrieve crash
/// reports from the crash helper process and the logic used to generate the
/// actual minidumps. The logic will run in a separate thread and this
/// function will return immediately after launching it.
///
/// # Safety
///
/// `minidump_data` must point to valid, nul-terminated C strings. `listener`
/// and `server_pipe` must be valid file descriptors and `breakpad_data` must
/// also be a valid file descriptor compatible with Breakpad's crash generation
/// server.
#[cfg(target_os = "android")]
#[no_mangle]
pub unsafe extern "C" fn crash_generator_logic_android(
    client_pid: Pid,
    breakpad_data: BreakpadRawData,
    minidump_path: *const c_char,
    listener: RawFd,
    pipe: RawFd,
) {
    logging::init();

    let breakpad_data = BreakpadData::new(breakpad_data);
    let minidump_path = unsafe { CStr::from_ptr(minidump_path) }
        .to_owned()
        .into_string()
        .unwrap();
    let minidump_path = OsString::from(minidump_path);
    let crash_generator = CrashGenerator::new(client_pid, breakpad_data, minidump_path)
        .map_err(|error| {
            log::error!("Could not create the crash generator (error: {error})");
            error
        })
        .unwrap();

    let listener = unsafe { OwnedFd::from_raw_fd(listener) };
    let listener = IPCListener::from_fd(client_pid, listener)
        .map_err(|error| {
            log::error!("Could not use the listener (error: {error})");
        })
        .unwrap();
    let pipe = unsafe { OwnedFd::from_raw_fd(pipe) };
    let connector = IPCConnector::from_fd(pipe)
        .map_err(|error| {
            log::error!("Could not use the pipe (error: {error})");
        })
        .unwrap();
    let ipc_server = IPCServer::new(client_pid, listener, connector);

    // On Android the main thread is used to respond to the intents so we
    // can't block it. Run the crash generation loop in a separate thread.
    let _ = std::thread::spawn(move || main_loop(ipc_server, crash_generator));
}

fn main_loop(mut ipc_server: IPCServer, mut crash_generator: CrashGenerator) -> i32 {
    loop {
        match ipc_server.run(&mut crash_generator) {
            Ok(_result @ IPCServerState::ClientDisconnected) => {
                return 0;
            }
            Err(error) => {
                log::error!("The crashhelper encountered an error, exiting (error: {error})");
                return -1;
            }
            _ => {} // Go on
        }
    }
}
