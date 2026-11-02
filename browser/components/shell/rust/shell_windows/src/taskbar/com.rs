/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#![allow(non_snake_case)]

//! # Taskband Pin COM API
//!
//! Taskband Pin is a COM class implementing the undocumented IPinnedList3
//! interface that affords pinning and unpinning from the Windows taskbar for
//! unpackaged Win32 (non-MSIX) applications. This API first became available in
//! Windows 10 RS5.
//!
//! ## Pinning
//!
//! On Windows 10 and early Windows 11 builds this API pins without prompting
//! the user.
//!
//! For Windows 11 24H2 and later this API reports success even though pinning
//! is not successful.
//!
//! ## Unpinning
//!
//! This API can be used to unpin an application for every version of Windows
//! released after Windows 10 RS5.
//!
//! ## Requirements
//!
//! The IPinnedList3 API can pin shortcuts from any directory.

use nserror::{
    nsresult, NS_ERROR_FILE_ACCESS_DENIED, NS_ERROR_FILE_NOT_FOUND, NS_ERROR_NOT_AVAILABLE,
};
use nsstring::{nsAString, nsString};
use windows::{
    core::{interface, IUnknown, IUnknown_Vtbl, GUID, HRESULT, PCWSTR},
    Win32::{
        System::Com::{CoCreateInstance, CLSCTX_INPROC_SERVER},
        UI::Shell::{Common::ITEMIDLIST, ILCreateFromPathW, ILFree},
    },
};

use super::PinResult;
use crate::util::thread_guard::MainThreadGuard;

pub(super) enum PinOp {
    Pin,
    UnPin,
}

pub(super) fn is_pinning_available(
    // Taskband Pin COM class is registered ThreadingModel=Apartment, so
    // instances are apartment-threaded and must live in an STA. There exists no
    // marshaler for Taskband Pin therefore we must ensure we're on an STA
    // thread before attempting to create it.
    //
    // We can ensure we're in an STA by running on the main thread; background
    // threads rely on implicit MTA thus should not be used here.
    _main_guard: MainThreadGuard,
) -> bool {
    // SAFETY: Taskband Pin is a known, undocumented Windows COM API. The
    // interface is therefore not stable but in practice has not been found to
    // change, and historically has been extended instead of modified as
    // evidenced by this being the third iteration. We redefine the interface
    // below.
    unsafe { CoCreateInstance::<_, IPinnedList3>(&CLSID_TASKBAND_PIN, None, CLSCTX_INPROC_SERVER) }
        .is_ok()
}

/// Pins or unpins the provided shortcut to the taskbar using Taskband Pin's
/// IPinnedList3 implementation, optionally only verifies the COM object with
/// relevant interface is available.
pub(super) fn modify_taskbar(
    pin_op: PinOp,
    shortcut_path: &nsAString,
    // Taskband Pin COM class is registered ThreadingModel=Apartment, so
    // instances are apartment-threaded and must live in an STA. There exists no
    // marshaler for Taskband Pin therefore we must ensure we're on an STA
    // thread before attempting to create it.
    //
    // We can ensure we're in an STA by running on the main thread; background
    // threads rely on implicit MTA thus should not be used here.
    _main_guard: MainThreadGuard,
) -> Result<PinResult, nsresult> {
    #[cfg(feature = "enable_tests")]
    if xpcom::is_in_automation() {
        // Return early in tests to avoid actually pinning the app.
        return Ok(PinResult::Unknown);
    }

    // Ensure path is a null-terminated string.
    let shortcut_path: nsString = shortcut_path.into();

    // SAFETY: Taskband Pin is a known, undocumented Windows COM API. The
    // interface is therefore not stable but in practice has not been found to
    // change, and historically has been extended instead of modified as
    // evidenced by this being the third iteration. We redefine the interface
    // below.
    let pinned_list: IPinnedList3 =
        unsafe { CoCreateInstance(&CLSID_TASKBAND_PIN, None, CLSCTX_INPROC_SERVER) }
            .map_err(|_| NS_ERROR_NOT_AVAILABLE)?;

    log::info!("COM pinning API is available.");

    // SAFETY: nsString is null-terminated utf-16 thus valid to construct a
    // PCWSTR from as an argument to ILCreateFromPathW.
    let pidl = scopeguard::guard(
        unsafe { ILCreateFromPathW(PCWSTR::from_raw(shortcut_path.as_ptr())) } as *const _,
        // SAFETY: ITEMIDLIST was either created in the above guard, or null for
        // invalid paths or null pointer as input. ILFree accepts null as valid.
        |pidl| unsafe { ILFree(Some(pidl)) },
    );
    // ILCreateFromPathW returns null when the path is non-existent or when the
    // parameter is null. This behavior is not documented, but was verified to
    // occur.
    if pidl.is_null() {
        log::error!("Failed to create identifier list from shortcut path {shortcut_path}");
        return Err(NS_ERROR_FILE_NOT_FOUND);
    }

    let (unpin_pidl, pin_pidl) = match pin_op {
        PinOp::Pin => (std::ptr::null(), *pidl),
        PinOp::UnPin => (*pidl, std::ptr::null()),
    };

    // SAFETY: ITEMIDLIST arguments are defined above and either initialized or
    // set to null (known valid for this API).
    unsafe { pinned_list.Modify(unpin_pidl, pin_pidl, PinnedListModifyCallerEnum::MAX) }
        .ok()
        .map_err(|e| {
            log::error!("Error modifying the taskbar: {e:?}");
            NS_ERROR_FILE_ACCESS_DENIED
        })?;

    log::info!("Usage of Taskband Pin COM API ran to end.");

    // IPinnedList3::Modify may have succeeded but depending on the version of
    // Windows either successfully pinned, prompt the user whether to pin the
    // app, or not pinned the app. We therefore do not know the result of our
    // attempt to pin.
    Ok(PinResult::Unknown)
}

// The types below, and the idea of using IPinnedList3::Modify, are thanks to
// Gee Law <https://geelaw.blog/entries/msedge-pins/>
//
// Taskband Pin is registered in the Windows Registry at:
// HKEY_CLASSES_ROOT\CLSID\{90AA3A4E-1CBA-4233-B8BB-535773D48449}
const CLSID_TASKBAND_PIN: GUID = GUID::from_u128(0x90AA3A4E_1CBA_4233_B8BB_535773D48449);

// Note: This definition mirrors how the windows crate defines COM enums.
#[repr(transparent)]
struct PinnedListModifyCallerEnum(i32);

impl PinnedListModifyCallerEnum {
    // This enum is likely only used for Windows telemetry, i32::MAX is chosen
    // to avoid confusion with existing uses.
    const MAX: Self = Self(i32::MAX);
}

/// Enum to prevent usage of IPinnedList3 methods with incomplete parameter
/// definitions.
enum IncompleteDefinition {}

#[interface("0dd79ae2-d156-45d4-9eeb-3b549769e940")]
unsafe trait IPinnedList3: IUnknown {
    // Note: We're recreating a Windows internal COM interface; method
    // declarations must not be reordered.
    unsafe fn EnumObjects(&self, i: IncompleteDefinition) -> HRESULT;
    unsafe fn GetPinnableInfo(&self, i: IncompleteDefinition) -> HRESULT;
    unsafe fn IsPinnable(&self, i: IncompleteDefinition) -> HRESULT;
    unsafe fn Resolve(&self, i: IncompleteDefinition) -> HRESULT;
    unsafe fn LegacyModify(&self, i: IncompleteDefinition) -> HRESULT;
    unsafe fn GetChangeCount(&self, i: IncompleteDefinition) -> HRESULT;
    unsafe fn IsPinned(&self, i: IncompleteDefinition) -> HRESULT;
    unsafe fn GetPinnedItem(&self, i: IncompleteDefinition) -> HRESULT;
    unsafe fn GetAppIDForPinnedItem(&self, i: IncompleteDefinition) -> HRESULT;
    unsafe fn ItemChangeNotify(&self, i: IncompleteDefinition) -> HRESULT;
    unsafe fn UpdateForRemovedItemsAsNecessary(&self, i: IncompleteDefinition) -> HRESULT;
    unsafe fn PinShellLink(&self, i: IncompleteDefinition) -> HRESULT;
    unsafe fn GetPinnedItemForAppID(&self, i: IncompleteDefinition) -> HRESULT;

    unsafe fn Modify(
        &self,
        unpin: *const ITEMIDLIST,
        pin: *const ITEMIDLIST,
        caller: PinnedListModifyCallerEnum,
    ) -> HRESULT;
}
