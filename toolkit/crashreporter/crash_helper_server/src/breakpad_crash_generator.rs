/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/******************************************************************************
 * Wrappers used to call into Breakpad code                                   *
 ******************************************************************************/

use std::{
    ffi::{c_char, c_void, OsString},
    ptr::NonNull,
};

use anyhow::{bail, Result};
use cfg_if::cfg_if;
use crash_helper_common::{BreakpadChar, BreakpadData, BreakpadString};
#[cfg(any(target_os = "android", target_os = "linux"))]
use minidump_writer::minidump_writer::DirectAuxvDumpInfo;

use crate::crash_generation::BreakpadProcessId;

#[cfg(target_os = "windows")]
type BreakpadInitType = *const u16;
#[cfg(target_os = "macos")]
type BreakpadInitType = *const c_char;
#[cfg(any(target_os = "linux", target_os = "android"))]
type BreakpadInitType = std::os::fd::RawFd;
#[cfg(any(target_os = "linux", target_os = "android"))]
use std::os::fd::{FromRawFd, OwnedFd};

extern "C" {
    fn CrashGenerationServer_init(
        breakpad_data: BreakpadInitType,
        minidump_path: *const BreakpadChar,
        cb: extern "C" fn(BreakpadProcessId, *const c_char, *const BreakpadChar),
        #[cfg(any(target_os = "android", target_os = "linux"))] auxv_cb: extern "C" fn(
            crash_helper_common::Pid,
            *mut DirectAuxvDumpInfo,
        )
            -> bool,
    ) -> *mut c_void;
    fn CrashGenerationServer_shutdown(server: *mut c_void);
    fn CrashGenerationServer_set_path(server: *mut c_void, path: *const BreakpadChar);
}

pub(crate) struct BreakpadCrashGenerator {
    ptr: NonNull<c_void>,
    path: NonNull<BreakpadChar>,
    #[allow(
        dead_code,
        reason = "This socket is used by Breakpad so it must be closed on Drop() as we own it"
    )]
    #[cfg(any(target_os = "linux", target_os = "android"))]
    breakpad_socket: OwnedFd,
}

// Safety: We own the pointer to the Breakpad C++ CrashGeneration server object
// so we can safely transfer this object to another thread.
unsafe impl Send for BreakpadCrashGenerator {}

// Safety: All mutations to the pointer to the Breakpad C++ CrashGeneration
// server happen within this object meaning it's safe to read it from different
// threads.
unsafe impl Sync for BreakpadCrashGenerator {}

impl BreakpadCrashGenerator {
    pub(crate) fn new(
        breakpad_data: BreakpadData,
        path: OsString,
        finalize_callback: extern "C" fn(BreakpadProcessId, *const c_char, *const BreakpadChar),
        #[cfg(any(target_os = "android", target_os = "linux"))]
        auxv_callback: extern "C" fn(
            crash_helper_common::Pid,
            *mut DirectAuxvDumpInfo,
        ) -> bool,
    ) -> Result<BreakpadCrashGenerator> {
        let breakpad_raw_data = breakpad_data.into_raw();
        let path_ptr = path.into_raw();

        // SAFETY: Calling into breakpad code with parameters that have been previously validated.
        let breakpad_server = unsafe {
            CrashGenerationServer_init(
                breakpad_raw_data,
                path_ptr,
                finalize_callback,
                #[cfg(any(target_os = "android", target_os = "linux"))]
                auxv_callback,
            )
        };

        // Retake ownership of the raw data & strings so we don't leak them.
        cfg_if! {
            if #[cfg(any(target_os = "macos", target_os = "windows"))] {
                // SAFETY: We've allocated this object within this same block.
                let _breakpad_data = unsafe { BreakpadData::new(breakpad_raw_data) };
            }
        }

        if breakpad_server.is_null() {
            bail!("Could not initialize Breakpad crash generator");
        }

        // SAFETY: We already verified that the pointers are non-null. On Linux
        // and Android the breakpad socket is also a valid file descriptor. We
        // store it in an owned file descriptor because we're taking ownership
        // of it and we want it closed when we shut down the crash generation
        // server.
        Ok(unsafe {
            BreakpadCrashGenerator {
                ptr: NonNull::new(breakpad_server).unwrap_unchecked(),
                path: NonNull::new(path_ptr).unwrap_unchecked(),
                #[cfg(any(target_os = "linux", target_os = "android"))]
                breakpad_socket: OwnedFd::from_raw_fd(breakpad_raw_data),
            }
        })
    }

    pub(crate) fn set_path(&self, path: OsString) {
        unsafe {
            let path = path.into_raw();
            CrashGenerationServer_set_path(self.ptr.as_ptr(), path);
        };
    }
}

impl Drop for BreakpadCrashGenerator {
    fn drop(&mut self) {
        // SAFETY: The pointers we're passing are guaranteed to be non-null and
        // valid since we created them ourselves during construction.
        unsafe {
            CrashGenerationServer_shutdown(self.ptr.as_ptr());
            let _path = <OsString as BreakpadString>::from_raw(self.path.as_ptr());
        }
    }
}
