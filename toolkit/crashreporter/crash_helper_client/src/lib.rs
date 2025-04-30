/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

use anyhow::{bail, Result};
use crash_helper_common::{
    messages::{self},
    BreakpadString, IPCConnector,
};
#[cfg(any(target_os = "android", target_os = "linux"))]
use minidump_writer::minidump_writer::{AuxvType, DirectAuxvDumpInfo};
#[cfg(target_os = "android")]
use std::os::fd::RawFd;
use std::{
    ffi::{c_char, CString, OsString},
    ptr::null_mut,
};
#[cfg(target_os = "windows")]
use windows_sys::Win32::System::Diagnostics::Debug::{CONTEXT, EXCEPTION_RECORD};

extern crate num_traits;

pub use crash_helper_common::{BreakpadChar, BreakpadRawData, Pid};

mod platform;

pub struct CrashHelperClient {
    connector: IPCConnector,
    #[cfg(target_os = "linux")]
    pid: Pid,
}

impl CrashHelperClient {
    fn set_crash_report_path(&mut self, path: OsString) -> Result<()> {
        let message = messages::SetCrashReportPath::new(path);
        self.connector.send_message(&message)?;
        Ok(())
    }

    #[cfg(any(target_os = "android", target_os = "linux"))]
    fn register_auxv_info(&mut self, pid: Pid, auxv_info: DirectAuxvDumpInfo) -> Result<()> {
        let message = messages::RegisterAuxvInfo::new(pid, auxv_info);
        self.connector.send_message(&message)?;
        Ok(())
    }

    #[cfg(any(target_os = "android", target_os = "linux"))]
    fn unregister_auxv_info(&mut self, pid: Pid) -> Result<()> {
        let message = messages::UnregisterAuxvInfo::new(pid);
        self.connector.send_message(&message)?;
        Ok(())
    }

    fn transfer_crash_report(&mut self, pid: Pid) -> Result<CrashReport> {
        let message = messages::TransferMinidump::new(pid);
        self.connector.send_message(&message)?;

        // HACK: Workaround for a macOS-specific bug
        #[cfg(target_os = "macos")]
        self.connector.poll(nix::poll::PollFlags::POLLIN)?;

        let reply = self
            .connector
            .recv_reply::<messages::TransferMinidumpReply>()?;

        if reply.path.is_empty() {
            // TODO: We should return Result<Option<CrashReport>> instead of
            // this. Semantics would be better once we interact with Rust
            bail!("Minidump for pid {pid:} was not found");
        }

        Ok(CrashReport {
            path: reply.path.into_raw(),
            error: reply.error.map_or(null_mut(), |error| error.into_raw()),
        })
    }
}

/// Launch the crash helper process, initialize it and connect to it. Returns
/// a pointer to the client connection or `null` upon failure.
///
/// # Safety
///
/// The `helper_name` and `minidump_path` arguments must point to byte or wide
/// strings where appropriate. The `breakpad_raw_data` argument must either
/// point to a string or must contain a valid file descriptor create via
/// `CrashGenerationServer::CreateReportChannel()` depending on the platform.
#[cfg(not(target_os = "android"))]
#[no_mangle]
pub unsafe extern "C" fn crash_helper_launch(
    helper_name: *const BreakpadChar,
    breakpad_raw_data: BreakpadRawData,
    minidump_path: *const BreakpadChar,
) -> *mut CrashHelperClient {
    use crash_helper_common::BreakpadData;

    let breakpad_data = BreakpadData::new(breakpad_raw_data);

    if let Ok(crash_helper) = CrashHelperClient::new(helper_name, breakpad_data, minidump_path) {
        let crash_helper_box = Box::new(crash_helper);

        // The object will be owned by the C++ code from now on, until it is
        // passed back in `crash_helper_shutdown`.
        Box::into_raw(crash_helper_box)
    } else {
        null_mut()
    }
}

/// Connect to an already launching crash helper process. This is only available
/// on Android where the crash helper is a service. Returns a pointer to the
/// client connection or `null` upon failure.
///
/// # Safety
///
/// The `minidump_path` argument must point to a valid nul-terminated C string.
/// The `breakpad_raw_data` and `client_socket` arguments must be valid file
/// descriptors used to connect with Breakpad's crash generator and the crash
/// helper respectively..
#[cfg(target_os = "android")]
#[no_mangle]
pub unsafe extern "C" fn crash_helper_connect(client_socket: RawFd) -> *mut CrashHelperClient {
    if let Ok(crash_helper) = CrashHelperClient::new(client_socket) {
        let crash_helper_box = Box::new(crash_helper);

        // The object will be owned by the C++ code from now on, until it is
        // passed back in `crash_helper_shutdown`.
        Box::into_raw(crash_helper_box)
    } else {
        null_mut()
    }
}

/// Shutdown the crash helper and dispose of the client object.
///
/// # Safety
///
/// The `client` parameter must be a valid pointer to the crash helper client
/// object returned by the [`crash_helper_launch()`] or
/// [`crash_helper_connect()`] functions.
#[no_mangle]
pub unsafe extern "C" fn crash_helper_shutdown(client: *mut CrashHelperClient) {
    // The CrashHelperClient object will be automatically destroyed when the
    // contents of this box are automatically dropped at the end of the function
    let _crash_helper_box = Box::from_raw(client);
}

/// Return the pid of the crash helper process.
///
/// # Safety
///
/// The `client` parameter must be a valid pointer to the crash helper client
/// object returned by the [`crash_helper_launch()`] or
/// [`crash_helper_connect()`] functions.
#[cfg(target_os = "linux")]
#[no_mangle]
pub unsafe extern "C" fn crash_helper_pid(client: *const CrashHelperClient) -> Pid {
    (*client).pid
}

#[repr(C)]
pub struct CrashReport {
    path: *mut BreakpadChar,
    error: *mut c_char,
}

/// Changes the path where crash reports are generated.
///
/// # Safety
///
/// The `client` parameter must be a valid pointer to the crash helper client
/// object returned by the [`crash_helper_launch()`] or
/// [`crash_helper_connect()`] functions.
#[no_mangle]
pub unsafe extern "C" fn set_crash_report_path(
    client: *mut CrashHelperClient,
    path: *const BreakpadChar,
) -> bool {
    let client = client.as_mut().unwrap();
    let path = <OsString as BreakpadString>::from_ptr(path);
    client.set_crash_report_path(path).is_ok()
}

/// Request the crash report generated for the process associated with `pid`.
/// If the crash report is found an object holding a pointer to the minidump
/// and a potential error message will be returned. Otherwise the function will
/// return `null`.
///
/// # Safety
///
/// The `client` parameter must be a valid pointer to the crash helper client
/// object returned by the [`crash_helper_launch()`] or
/// [`crash_helper_connect()`] functions.
#[no_mangle]
pub unsafe extern "C" fn transfer_crash_report(
    client: *mut CrashHelperClient,
    pid: Pid,
) -> *mut CrashReport {
    let client = client.as_mut().unwrap();
    if let Ok(crash_report) = client.transfer_crash_report(pid) {
        // The object will be owned by the C++ code from now on, until it is
        // passed back in `release_crash_report`.
        Box::into_raw(Box::new(crash_report))
    } else {
        null_mut()
    }
}

/// Release an object obtained via [`transfer_crash_report()`]
///
/// # Safety
///
/// The `crash_report` argument must be a pointer returned by the
/// [`transfer_crash_report()`] or [`crash_helper_connect()`] functions.
#[no_mangle]
pub unsafe extern "C" fn release_crash_report(crash_report: *mut CrashReport) {
    let crash_report = Box::from_raw(crash_report);

    // Release the strings, we just get back ownership of the raw objects and let the drop logic get rid of them
    let _path = <OsString as BreakpadString>::from_raw(crash_report.path);

    if !crash_report.error.is_null() {
        let _error = CString::from_raw(crash_report.error);
    }
}

/// Report an exception to the crash manager that has been captured outside of
/// Firefox processes, typically within the Windows Error Reporting runtime
/// exception module.
///
/// # Safety
///
/// The `exception_record_ptr` and `context_ptr` parameters must point to valid
/// objects of the corresponding types.
#[cfg(target_os = "windows")]
pub unsafe fn report_external_exception(
    main_process_pid: Pid,
    pid: Pid,
    thread: Pid, // TODO: This should be a different type, but it's the same on Windows
    exception_record_ptr: *mut EXCEPTION_RECORD,
    context_ptr: *mut CONTEXT,
) {
    let exception_records = collect_exception_records(exception_record_ptr);
    let context = unsafe { context_ptr.read() };
    let message =
        messages::WindowsErrorReportingMinidump::new(pid, thread, exception_records, context);

    // In the code below we connect to the crash helper, send our message and wait for a reply before returning, but we ignore errors because we can't do anything about them in the calling code
    if let Ok(connector) = IPCConnector::connect(main_process_pid) {
        let _ = connector
            .send_message(&message)
            .and_then(|_| connector.recv_reply::<messages::WindowsErrorReportingMinidumpReply>());
    }
}

// Collect a linked-list of exception records and turn it into a vector
#[cfg(target_os = "windows")]
fn collect_exception_records(
    mut exception_record_ptr: *mut EXCEPTION_RECORD,
) -> Vec<EXCEPTION_RECORD> {
    let mut exception_records = Vec::<EXCEPTION_RECORD>::with_capacity(1);
    loop {
        if exception_record_ptr.is_null() {
            return exception_records;
        }

        let mut exception_record = unsafe { exception_record_ptr.read() };
        exception_record_ptr = exception_record.ExceptionRecord;
        exception_record.ExceptionRecord = null_mut();
        exception_records.push(exception_record);
    }
}

/// Send the auxiliary vector information for the process identified by `pid`
/// to the crash helper.
///
/// # Safety
///
/// The `client` parameter must be a valid pointer to the crash helper client
/// object returned by the [`crash_helper_launch()`] or
/// [`crash_helper_connect()`] functions. The `auxv_info` pointer must be
/// non-null and point to a properly populated `DirectAuxvDumpInfo` structure.
#[cfg(any(target_os = "android", target_os = "linux"))]
#[no_mangle]
pub unsafe extern "C" fn register_child_auxv_info(
    client: *mut CrashHelperClient,
    pid: Pid,
    auxv_info_ptr: *const rust_minidump_writer_linux::DirectAuxvDumpInfo,
) -> bool {
    let client = client.as_mut().unwrap();
    let auxv_info = DirectAuxvDumpInfo {
        program_header_count: (*auxv_info_ptr).program_header_count as AuxvType,
        program_header_address: (*auxv_info_ptr).program_header_address as AuxvType,
        linux_gate_address: (*auxv_info_ptr).linux_gate_address as AuxvType,
        entry_address: (*auxv_info_ptr).entry_address as AuxvType,
    };

    client.register_auxv_info(pid, auxv_info).is_ok()
}

/// Deregister previously sent auxiliary vector information for the process
/// identified by `pid`.
///
/// # Safety
///
/// The `client` parameter must be a valid pointer to the crash helper client
/// object returned by the [`crash_helper_launch()`] or
/// [`crash_helper_connect()`] functions.
#[cfg(any(target_os = "android", target_os = "linux"))]
#[no_mangle]
pub unsafe extern "C" fn unregister_child_auxv_info(
    client: *mut CrashHelperClient,
    pid: Pid,
) -> bool {
    let client = client.as_mut().unwrap();
    client.unregister_auxv_info(pid).is_ok()
}
