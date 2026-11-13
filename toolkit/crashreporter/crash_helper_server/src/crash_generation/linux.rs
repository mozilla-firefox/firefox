/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

use super::CrashGenerator;

use anyhow::Result;
use crash_helper_common::{
    crash_annotations::CrashAnnotation,
    messages::{self},
    ApplicationInfo, GeckoChildId, RawProcessHandle,
};
use minidump_writer::{
    minidump_writer::DirectAuxvDumpInfo, remote_process_inspection::OUTPUT_BUFFER_LEN,
};
use mozannotation_server::CAnnotation;
use once_cell::sync::Lazy;
use std::{
    collections::HashMap,
    fs,
    os::fd::{BorrowedFd, RawFd},
    sync::Mutex,
};

// Table holding the information about the auxiliary vector of potentially
// every process registered with the crash helper.
static AUXV_INFO_MAP: Lazy<Mutex<HashMap<GeckoChildId, DirectAuxvDumpInfo>>> =
    Lazy::new(Default::default);

impl CrashGenerator {
    pub(crate) fn register_auxv_info(&self, message: messages::RegisterAuxvInfo) -> Result<()> {
        let map = &mut AUXV_INFO_MAP.lock().unwrap();
        map.insert(message.id, message.auxv_info);
        Ok(())
    }

    pub(crate) fn unregister_auxv_info(&self, message: messages::UnregisterAuxvInfo) -> Result<()> {
        let map = &mut AUXV_INFO_MAP.lock().unwrap();
        map.remove(&message.id);
        Ok(())
    }
}

pub(crate) extern "C" fn get_auxv_info(
    id: GeckoChildId,
    auxv_info_ptr: *mut DirectAuxvDumpInfo,
) -> bool {
    let map = &mut AUXV_INFO_MAP.lock().unwrap();

    if let Some(auxv_info) = map.get(&id) {
        // SAFETY: The auxv_info_ptr is guaranteed to be valid by the caller.
        unsafe { auxv_info_ptr.write(auxv_info.to_owned()) };
        true
    } else {
        false
    }
}

/// Create the annotations that are specific to this platform, there are none
/// at the moment.
pub(crate) fn create_platform_specific_annotations(
    _app_info: &ApplicationInfo,
) -> Result<Vec<CAnnotation>> {
    Ok(Vec::new())
}

fn process_uptime_internal(pid: RawProcessHandle) -> Result<f64> {
    let metadata = fs::metadata(format!("/proc/{pid}"))?;
    let creation_time = metadata.modified()?;
    let uptime = creation_time.elapsed()?;
    Ok(uptime.as_secs_f64())
}

pub(crate) fn process_uptime(pid: RawProcessHandle) -> f64 {
    match process_uptime_internal(pid) {
        Ok(uptime) => uptime,
        Err(err) => {
            log::error!("Could not calculate uptime for process {pid}: {err}");
            0.0f64
        }
    }
}

/// Read the crash annotations of a process through its remote executor.
pub(crate) fn retrieve_annotations_remote(remote_executor_fd: RawFd) -> Result<Vec<CAnnotation>> {
    // SAFETY: Breakpad keeps the endpoint open for the duration of the dump callback.
    let socket = unsafe { BorrowedFd::borrow_raw(remote_executor_fd) }.try_clone_to_owned()?;
    Ok(mozannotation_server::retrieve_annotations_remote(
        socket,
        OUTPUT_BUFFER_LEN,
        CrashAnnotation::Count as usize,
    )?)
}
