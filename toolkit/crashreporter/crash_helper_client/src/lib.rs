/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

use anyhow::{bail, Result};
use crash_helper_common::{
    messages::{self},
    ApplicationInfo, BreakpadString, FromRawThreadHandle, GeckoChildId, IPCClientChannel,
    IPCConnector, RawIPCConnector, RawThreadHandle, ThreadHandle,
};
#[cfg(any(target_os = "android", target_os = "linux"))]
use minidump_writer::minidump_writer::{AuxvType, DirectAuxvDumpInfo};
#[cfg(target_os = "android")]
use std::os::fd::RawFd;
#[cfg(target_os = "windows")]
use std::os::windows::io::{AsRawHandle, BorrowedHandle, OwnedHandle, RawHandle};
use std::{
    ffi::{c_char, CString, OsString},
    hint::spin_loop,
    path::PathBuf,
    ptr::null_mut,
    sync::{
        atomic::{AtomicBool, Ordering},
        OnceLock,
    },
};
#[cfg(target_os = "windows")]
use windows_sys::Win32::{
    Foundation::HANDLE,
    System::{
        Diagnostics::Debug::{CONTEXT, EXCEPTION_RECORD},
        Threading::{GetProcessId, GetThreadId},
    },
};

extern crate num_traits;

pub use crash_helper_common::{BreakpadChar, BreakpadRawData, Pid};

mod platform;

pub struct CrashHelperClient {
    connector: IPCConnector,
    #[allow(unused)]
    pid: Pid,
}

impl CrashHelperClient {
    fn set_path(&self, path_type: messages::PathType, path: OsString) -> Result<()> {
        let message = messages::SetPath::new(path_type, path);
        self.connector.send_message(message)?;
        Ok(())
    }

    fn get_path(&self, path_type: messages::PathType) -> Result<OsString> {
        let message = messages::GetPath::new(path_type);
        self.connector.send_message(message)?;
        let reply = self.connector.recv_reply::<messages::PathReply>()?;
        Ok(reply.path)
    }

    fn register_child_process(&mut self, id: GeckoChildId) -> Result<IPCConnector> {
        let ipc_channel = IPCClientChannel::new()?;
        let (server_endpoint, client_endpoint) = ipc_channel.deconstruct();
        let message = messages::RegisterChildProcess::new(id, server_endpoint.into_ancillary());
        self.connector.send_message(message)?;

        Ok(client_endpoint)
    }

    #[cfg(target_os = "windows")]
    fn child_process_proxy_rendezvous(
        &mut self,
        id: GeckoChildId,
        pid: Pid,
        handle: OwnedHandle,
    ) -> bool {
        let message = messages::ProcessRendezVous::new(/*dumpable */ true, pid, id, [handle]);
        self.connector.send_message(message).is_ok()
    }

    #[cfg(any(target_os = "android", target_os = "linux"))]
    fn register_auxv_info(
        &mut self,
        id: GeckoChildId,
        auxv_info: DirectAuxvDumpInfo,
    ) -> Result<()> {
        let message = messages::RegisterAuxvInfo::new(id, auxv_info);
        self.connector.send_message(message)?;
        Ok(())
    }

    #[cfg(any(target_os = "android", target_os = "linux"))]
    fn unregister_auxv_info(&mut self, id: GeckoChildId) -> Result<()> {
        let message = messages::UnregisterAuxvInfo::new(id);
        self.connector.send_message(message)?;
        Ok(())
    }

    fn transfer_crash_report(&mut self, id: GeckoChildId) -> Result<CrashReport> {
        let message = messages::TransferMinidump::new(id);
        self.connector.send_message(message)?;

        let reply = self.connector.recv_reply::<messages::MinidumpReply>()?;

        if reply.path.is_empty() {
            // TODO: We should return Result<Option<CrashReport>> instead of
            // this. Semantics would be better once we interact with Rust
            bail!("Minidump for id {id:} was not found");
        }

        Ok(CrashReport {
            path: reply.path.into_raw(),
            error: reply.error.map_or(null_mut(), |error| error.into_raw()),
        })
    }

    fn generate_crash_report(
        &mut self,
        id: GeckoChildId,
        target_thread: ThreadHandle,
    ) -> Result<CrashReport> {
        let message = messages::GenerateMinidump::new(id, target_thread);
        self.connector.send_message(message)?;

        let reply = self.connector.recv_reply::<messages::MinidumpReply>()?;

        if reply.path.is_empty() {
            // TODO: We should return Result<Option<CrashReport>> instead of
            // this. Semantics would be better once we interact with Rust
            bail!("Minidump for id {id:} could not be generated");
        }

        Ok(CrashReport {
            path: reply.path.into_raw(),
            error: reply.error.map_or(null_mut(), CString::into_raw),
        })
    }
}

/******************************************************************************
 * Main process interface                                                     *
 ******************************************************************************/

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
    build_id: *const c_char,
) -> *mut CrashHelperClient {
    use crash_helper_common::BreakpadData;

    let breakpad_data = BreakpadData::new(breakpad_raw_data);

    if let Ok(crash_helper) =
        CrashHelperClient::new(helper_name, breakpad_data, minidump_path, build_id)
    {
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
    if let Ok(crash_helper) = CrashHelperClient::new(RawIPCConnector {
        socket: client_socket,
    }) {
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

/// Return the PID of the crash helper.
///
/// # Safety
///
/// The `client` parameter must be a valid pointer to the crash helper client
/// object returned by the [`crash_helper_launch()`] or
/// [`crash_helper_connect()`] functions.
#[no_mangle]
pub unsafe extern "C" fn crash_helper_pid(client: *mut CrashHelperClient) -> Pid {
    let client = client.as_mut().unwrap();

    client.pid
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
/// [`crash_helper_connect()`] functions. The `path` parameter must be a valid
/// pointer to a platform-dependent nul-terminated string.
#[no_mangle]
pub unsafe extern "C" fn set_crash_report_path(
    client: *const CrashHelperClient,
    path: *const BreakpadChar,
) -> bool {
    let client = client.as_ref().unwrap();
    let path = <OsString as BreakpadString>::from_ptr(path);
    client
        .set_path(messages::PathType::CrashReports, path)
        .is_ok()
}

/// Retrieves the path where crash reports are generated.
///
/// The returned path is a pointer to a nul-terminated UTF8 string on various
/// Unixes (including macOS) and an UTF16 string on Windows.
///
/// # Safety
///
/// The `client` parameter must be a valid pointer to the crash helper client
/// object returned by the [`crash_helper_launch()`] or
/// [`crash_helper_connect()`] functions.
#[no_mangle]
pub unsafe extern "C" fn get_crash_report_path(
    client: *const CrashHelperClient,
) -> *mut BreakpadChar {
    let client = client.as_ref().unwrap();
    let path = client
        .get_path(messages::PathType::CrashReports)
        .unwrap_or(OsString::new());
    BreakpadString::into_raw(path)
}

/// Releases a path obtained via [`get_crash_report_path()`]
///
/// # Safety
///
/// The `path` argument must be a pointer returned by the
/// [`get_crash_report_path()`] function.
#[no_mangle]
pub unsafe extern "C" fn release_path(path: *mut BreakpadChar) {
    let _string = <OsString as BreakpadString>::from_raw(path);
}

/// Creates a new IPC channel to connect a soon-to-be-created child process
/// with the crash helper client. The server-side endpoint of this channel
/// will be sent to the crash helper, and the client-side endpoint will be
/// stored in the structure pointed by the `connector` argument.
///
/// This function will return false if we failed to create the IPC channel.
///
/// # Safety
///
/// The `client` parameter must be a valid pointer to the crash helper client
/// object returned by the [`crash_helper_launch()`] or
/// [`crash_helper_connect()`] functions. The `connector` pointer must be a
/// valid pointer to a `RawIPCConnector` structure.
#[no_mangle]
pub unsafe extern "C" fn register_child_ipc_channel(
    client: *mut CrashHelperClient,
    id: GeckoChildId,
    connector: *mut RawIPCConnector,
) -> bool {
    let client = client.as_mut().unwrap();
    if let Ok(client_endpoint) = client.register_child_process(id) {
        let raw_connector = client_endpoint.into_raw_connector();
        unsafe {
            connector.write(raw_connector);
        }

        true
    } else {
        false
    }
}

/// Request the crash report generated for the process identified by `id`.
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
    id: GeckoChildId,
) -> *mut CrashReport {
    let client = client.as_mut().unwrap();
    if let Ok(crash_report) = client.transfer_crash_report(id) {
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

/// Generate a minidump for the process specified by `id`.
///
/// # Safety
///
/// The `client` parameter must be a valid pointer to the crash helper client
/// object returned by the [`crash_helper_launch()`] or
/// [`crash_helper_connect()`] functions. The `target_thread` parameter must be
/// a valid platform-specific thread handle.
#[no_mangle]
pub unsafe extern "C" fn generate_crash_report(
    client: *mut CrashHelperClient,
    id: GeckoChildId,
    target_thread: RawThreadHandle,
) -> *mut CrashReport {
    let client = client.as_mut().unwrap();
    let target_thread = ThreadHandle::from_raw_handle(target_thread);
    if let Ok(crash_report) = client.generate_crash_report(id, target_thread) {
        // The object will be owned by the C++ code from now on, until it is
        // passed back in `release_crash_report`.
        Box::into_raw(Box::new(crash_report))
    } else {
        null_mut()
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
    process: BorrowedHandle,
    thread: BorrowedHandle,
    exception_record_ptr: *mut EXCEPTION_RECORD,
    context_ptr: *mut CONTEXT,
) {
    let exception_records = collect_exception_records(exception_record_ptr);
    let context = unsafe { context_ptr.read() };
    // SAFETY: The `process` handle is guaranteed to be valid.
    let pid = match unsafe { GetProcessId(process.as_raw_handle() as HANDLE) } {
        0 => return,
        pid => pid,
    };
    // SAFETY: The `thread` handle is guaranteed to be valid.
    let tid = match unsafe { GetThreadId(thread.as_raw_handle() as HANDLE) } {
        0 => return,
        tid => tid,
    };
    let message =
        messages::WindowsErrorReportingMinidump::new(pid, tid, exception_records, context);

    // In the code below we connect to the crash helper, send our message and
    // wait for a reply before returning, but we ignore errors because we
    // can't do anything about them in the calling code.
    let server_addr = crash_helper_common::server_addr(main_process_pid);
    if let Ok(connector) = IPCConnector::connect(&server_addr) {
        let _ = connector
            .send_message(message)
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

/// Sends the rendez-vous message of a child process from the main process
/// instead of from the child itself. This is used on Windows where the main
/// process has access to the child handle and can duplicate handles into the
/// crash helper, but the child cannot.
///
/// # Safety
///
/// The `client` parameter must be a valid pointer to the crash helper client
/// object returned by the [`crash_helper_launch()`] or
/// [`crash_helper_connect()`] functions. The `handle` parameter must contain
/// a valid process handle.
#[cfg(target_os = "windows")]
#[no_mangle]
pub unsafe extern "C" fn child_process_proxy_rendezvous(
    client: *mut CrashHelperClient,
    id: GeckoChildId,
    pid: Pid,
    handle: HANDLE,
) -> bool {
    let client = client.as_mut().unwrap();

    let Ok(handle) = BorrowedHandle::borrow_raw(handle as RawHandle).try_clone_to_owned() else {
        return false;
    };
    client.child_process_proxy_rendezvous(id, pid, handle)
}

/// Send the auxiliary vector information for the process identified by `id`
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
    id: GeckoChildId,
    auxv_info_ptr: *const rust_minidump_writer_linux::DirectAuxvDumpInfo,
) -> bool {
    let client = client.as_mut().unwrap();
    let auxv_info = DirectAuxvDumpInfo {
        program_header_count: (*auxv_info_ptr).program_header_count as AuxvType,
        program_header_address: (*auxv_info_ptr).program_header_address as AuxvType,
        linux_gate_address: (*auxv_info_ptr).linux_gate_address as AuxvType,
        entry_address: (*auxv_info_ptr).entry_address as AuxvType,
    };

    client.register_auxv_info(id, auxv_info).is_ok()
}

/// Deregister previously sent auxiliary vector information for the process
/// identified by `id`.
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
    id: GeckoChildId,
) -> bool {
    let client = client.as_mut().unwrap();
    client.unregister_auxv_info(id).is_ok()
}

/// Return the installation time of the folder/file specified in `path` or the
/// current running executable if `path` is null. Note that this will not be an
/// exact value, but rather a somewhat unique value for each installation and
/// for each different user of said installation. We use it to approximate user
/// counts without resorting to identifiable information, as such it is not
/// meant to provide an accurate result. A return value of 0 indicates an error.
///
/// # Safety
///
/// The `path` argument must point to a nul-terminated C string or be null.
#[no_mangle]
pub unsafe extern "C" fn get_install_time(path: *const BreakpadChar) -> u64 {
    let path = if path.is_null() {
        None
    } else {
        let path = <OsString as BreakpadString>::from_ptr(path);
        Some(PathBuf::from(path))
    };
    ApplicationInfo::compute_install_time(path).unwrap_or(0)
}

/******************************************************************************
 * Child process interface                                                    *
 ******************************************************************************/

// This contains the raw IPC endpoint that a child will use to reach out to
// the crash helper process. We explicitly store it in its raw form rather
// than as an IPCConnector because the latter is neither thread-safe nor
// signal/exception-safe. We will access this endpoint only from within the
// exception handler with bare syscalls so we can leave the `IPCConnector`
// object behind.
static CHILD_IPC_ENDPOINT: OnceLock<Box<RawIPCConnector>> = OnceLock::new();
static RENDEZVOUS_FAILED: AtomicBool = AtomicBool::new(false);

/// Let a client rendez-vous with the crash helper process. This step ensures
/// the crash helper will be able to dump the calling child. This will also
/// serve additional functionality in the future.
///
/// # Safety
///
/// This function is safe to use if the `client_endpoint` parameter contains
/// a valid pipe handle (on Windows) or a valid file descriptor (on all other
/// platforms).
#[no_mangle]
pub unsafe extern "C" fn crash_helper_rendezvous(
    raw_connector: RawIPCConnector,
    id: GeckoChildId,
    crash_helper_pid: Option<&Pid>,
) {
    let Ok(connector) = IPCConnector::from_raw_connector(raw_connector) else {
        RENDEZVOUS_FAILED.store(true, Ordering::Relaxed);
        return;
    };

    let rendezvous = CrashHelperClient::prepare_for_minidump(crash_helper_pid.copied(), id);
    if let Some(rendezvous) = rendezvous {
        if connector.send_message(rendezvous).is_err() {
            RENDEZVOUS_FAILED.store(true, Ordering::Relaxed);
            return;
        }
    }

    assert!(
        CHILD_IPC_ENDPOINT
            .set(Box::new(connector.into_raw_connector()))
            .is_ok(),
        "The crash_helper_rendezvous() function must only be called once"
    );
}

/// Ensure that the rendez-vous with the crash helper has happened. This method
/// can be called safely from within an exception handler.
///
/// # Safety
///
/// It is always safe to call this function. It's safe even from within an
/// exception handler.
#[no_mangle]
pub unsafe extern "C" fn crash_helper_wait_for_rendezvous() {
    while CHILD_IPC_ENDPOINT.get().is_none() {
        if RENDEZVOUS_FAILED.load(Ordering::Relaxed) {
            break;
        }

        spin_loop();
    }
}
