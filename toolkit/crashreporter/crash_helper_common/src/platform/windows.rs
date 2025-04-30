/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

use crate::{errors::IPCError, Pid};
use std::{
    os::windows::io::{AsRawHandle, BorrowedHandle, FromRawHandle, OwnedHandle, RawHandle},
    ptr::null,
};
use windows_sys::Win32::{
    Foundation::{GetLastError, FALSE, HANDLE},
    System::Threading::{CreateEventA, ResetEvent, SetEvent},
};

pub(crate) fn server_name(pid: Pid) -> String {
    // We'll be passing this to CreateNamedPipeA() so we nul-terminate it.
    format!("\\\\.\\pipe\\gecko-crash-helper-pipe.{pid:}\0")
}

pub(crate) fn create_manual_reset_event() -> Result<OwnedHandle, IPCError> {
    // SAFETY: We pass null pointers for all the pointer arguments.
    let raw_handle = unsafe {
        CreateEventA(
            /* lpEventAttributes */ null(),
            /* bManualReset */ FALSE,
            /* bInitialState */ FALSE,
            /* lpName */ null(),
        )
    } as RawHandle;

    if raw_handle.is_null() {
        return Err(IPCError::System(unsafe { GetLastError() }));
    }

    // SAFETY: We just verified that `raw_handle` is valid.
    Ok(unsafe { OwnedHandle::from_raw_handle(raw_handle) })
}

pub(crate) fn reset_event(handle: BorrowedHandle) -> Result<(), IPCError> {
    // SAFETY: The handle we pass is guaranteed to be valid.
    let res = unsafe { ResetEvent(handle.as_raw_handle() as HANDLE) };

    match res {
        FALSE => Err(IPCError::System(unsafe { GetLastError() })),
        _ => Ok(()),
    }
}

pub(crate) fn set_event(handle: BorrowedHandle) -> Result<(), IPCError> {
    // SAFETY: The handle we pass is guaranteed to be valid.
    let res = unsafe { SetEvent(handle.as_raw_handle() as HANDLE) };

    match res {
        FALSE => Err(IPCError::System(unsafe { GetLastError() })),
        _ => Ok(()),
    }
}
