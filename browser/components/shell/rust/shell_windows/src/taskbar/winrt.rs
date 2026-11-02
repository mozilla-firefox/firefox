/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! # TaskbarManager Pin WinRT API
//!
//! This implements functionality to pin an app to the taskbar using the
//! TaskbarManager WinRT API. This was originally exposed to UWP/MSIX apps, and
//! later extended to unpackaged Win32 and packaged (Win32 MSIX) apps while
//! locking down the undocumented [IPinnedList3 COM API][super::com].
//!
//! ## Secondary Pinning
//!
//! For unpackaged Win32 applications secondary applications can be pinned to
//! the taskbar by temporarily changing the process AUMID while requesting the
//! application be pinned.
//!
//! This does not work for MSIX applications as the process AUMID is
//! virtualized, and modifying it within the application is not visible to the
//! system. Instead MSIX relies on [Secondary Tiles][crate::secondary_tiles].
//!
//! ## Limited Access Feature
//!
//! This feature initially required the "com.microsoft.windows.taskbar.pin"
//! [Limited Access Feature][crate::limited_access_features] unlocked to work
//! for unpackaged Win32 applications.
//!
//! As of Windows 11 26200 Build 7840 this graduated out of limited access and
//! became generally available.
//!
//! ## Requirements
//!
//! This API requires a shortcut present in the virtual shell:appsfolder
//! directory. For MSIX installs this requires an <Application> entry in the
//! AppxManifest.xml, for non-MSIX installs a shortcut with unique AUMID in
//! either the User or Common Start Menu folders. Note that there is a delay
//! between files being created in the Start Menu folders and becoming
//! accessible in shell:appsfolder.
//!
//! The app must be focused when pinning is requested.

use nserror::{NS_ERROR_NOT_AVAILABLE, NS_ERROR_UNEXPECTED, nsresult};
use nsstring::nsAString;
use std::sync::LazyLock;
use windows::{
    ApplicationModel::Package,
    UI::Shell::{ITaskbarManagerDesktopAppSupportStatics, TaskbarManager},
    core::{Error as WinError, factory},
};

use crate::{
    limited_access_features::LimitedAccessFeatureService,
    util::{async_timer, thread_guard::MainThreadGuard},
};

use super::PinResult;

/// Lazily attempts to unlock the taskbar pin Limited Access Feature, at most
/// once.
static LAF_LOCK: LazyLock<Result<(), nsresult>> = LazyLock::new(|| {
    let svc = LimitedAccessFeatureService::new();
    let feature_id = svc.get_taskbar_pin_feature_id()?;
    let feature = svc.generate_limited_access_feature(&feature_id)?;
    feature.unlock()?.then_some(()).ok_or(NS_ERROR_UNEXPECTED)
});

/// Context indicating whether the WinRT pinning APIs are supported on the
/// current OS version and, if so, whether pinning is currently allowed.
pub(super) enum CanPin {
    Supported { allowed: bool },
    Unsupported,
}

/// Checks whether the taskbar pinning APIs are supported on the current OS
/// version and, if so, whether pinning is currently allowed.
///
/// Note: This is a best-effort check. For Win32 desktop apps, we always report
/// that pinning is allowed when it is supported. We do this because we cannot
/// distinguish between cases where pinning is allowed due to conditions we
/// handle during pinning and cases where pinning will fail for reasons beyond
/// our control.
pub(super) fn can_pin() -> Result<CanPin, WinError> {
    if let Err(_e) = *LAF_LOCK {
        // Limited Access Feature no longer necessary for Windows 11 26200 Build
        // 7840, and possibly other channels.
        log::info!(
            "Failed to unlock Limited Access Feature, attempting to use Taskbar Pinning API assuming LAF is no longer necessary."
        );
    }

    // Verify pinning is supported for Win32 desktop applications - applies to
    // both packaged (MSIX) and unpackaged installs.
    if factory::<TaskbarManager, ITaskbarManagerDesktopAppSupportStatics>().is_err() {
        return Ok(CanPin::Unsupported);
    }

    match Package::Current() {
        Ok(_) => Ok(CanPin::Supported {
            allowed: TaskbarManager::GetDefault()?.IsPinningAllowed()?,
        }),
        Err(_) => {
            // `TaskbarManager::IsPinningAllowed` returns false if no
            // shell:appsfolder entry exists, which is only ever the case with
            // unpackaged installs. We can't rely on `IsPinningAllowed` for our
            // use case because we dynamically generate shell:appsfolder entries
            // when not present while pinning. For this reason, we assume
            // pinning is allowed when supported and not packaged.
            Ok(CanPin::Supported { allowed: true })
        }
    }
}

/// Pins the provided app to the taskbar using the WinRT TaskbarManager API,
/// optionally returning before the pin request has resolved.
pub(super) async fn pin_to_taskbar(
    aumid: &nsAString,
    fire_and_forget: bool,
    // We need to be on a UI thread for taskbar pinning prompt to show.
    _main_guard: MainThreadGuard,
) -> Result<PinResult, WinRtPinError> {
    if let Err(_e) = *LAF_LOCK {
        // Limited Access Feature no longer necessary for Windows 11 26200 Build
        // 7840, and possibly other channels.
        log::info!(
            "Failed to unlock Limited Access Feature, attempting to use Taskbar Pinning API assuming LAF is no longer necessary."
        );
    }

    log::info!("Pinning with AUMID {aumid}");

    // Ideally we would refactor this such that AUMID is only provided in
    // non-MSIX contexts and error when that is violated, and generalize our
    // pinning abstraction to automatically use secondary tile pinning when in
    // an MSIX context. For now we match the behavior adopted from prior
    // implementations.
    let aumid_holder = match Package::Current() {
        Ok(_) => None,
        Err(_) => Some(aumid::Holder::set_aumid(aumid).await?),
    };

    #[cfg(feature = "enable_tests")]
    if xpcom::is_in_automation() {
        // Return early in tests to avoid actually pinning the app. Also
        // forces the AUMID to reset immediately by dropping the AUMID
        // holder instead of moving it into an async to resolve later.
        return test::pin_result_from_pref().unwrap_or(Ok(PinResult::Unknown));
    }

    let manager = TaskbarManager::GetDefault().map_err(WinRtPinError::GetTaskbarManager)?;

    let user_confirmed = manager
        .RequestPinCurrentAppAsync()
        .map_err(WinRtPinError::ScheduleRequestPin)?;

    if let Some(aumid_holder) = aumid_holder {
        // Schedule restoring the process AUMID to remove a race between it
        // and processing the pin request using the temporarily set AUMID.
        // This is believed to work due to it hypothetically allowing the
        // main thread's STA Message loop to run first.
        //
        // Note: we don't want to await the pin request before resetting the
        // AUMID. The pin request requires user interaction and is therefore
        // not guaranteed to resolve. We want to restore the AUMID as soon
        // as possible to prevent potential adverse interactions with
        // Windows APIs reliant on the process AUMID. Additionally this
        // livelocks following attempts to pin to the taskbar because we
        // synchronize attempts to change the process AUMID, as might occur
        // when setting up several web apps in succession.
        //
        // If in the future we want to ensure AUMID is safe to reset, we
        // could try inspecting the "App" application's notification history
        // to observe when the pin prompt is shown.
        moz_task::spawn_local("WinRT Pin Defer AUMID Restore", async {
            use std::time::Duration;
            if let Err(e) = async_timer::sleep(Duration::from_millis(100)).await {
                log::error!("Error delaying before restoring the default AUMID, incorrect app might prompt to pin: {e:?}");
            }

            aumid_holder.restore_aumid();
        })
        .detach();
    }

    if fire_and_forget {
        log::info!("Pin via WinRT with fire and forget ran to end.");
        Ok(PinResult::Unknown)
    } else if user_confirmed.await.map_err(WinRtPinError::RequestPin)? {
        log::info!("Pin via WinRT affirmed by user.");
        Ok(PinResult::Pinned)
    } else {
        log::info!("Pin via WinRT rejected by user or system.");
        Ok(PinResult::Rejected)
    }
}

/// Checks whether the current app is pinned to the taskbar.
pub(super) async fn is_current_app_pinned(_: Package) -> Result<bool, IsPinnedError> {
    // This implementation could be modified to check whether an app with a
    // matching AUMID has been pinned by swapping in the current process's
    // AUMID, as we do while pinning. Note that AUMID swapping does not make
    // sense in an MSIX context because we cannot swap the AUMID to create
    // additional taskbar shortcuts.
    //
    // Such an implementation would be unlikely to work for non-MSIX installs on
    // versions of Windows predating the taskbar-pinning Limited Access Feature,
    // though this has not been confirmed. Even if it did work in those
    // contexts, it would not work on versions of Windows earlier than 1809
    // because the TaskbarManager API did not yet exist.
    //
    // Because we can reliably check pin status for all non-MSIX installs by
    // inspecting shortcuts in the taskbar folder, we have opted not to support
    // such installs through WinRT. Calls to this function are therefore gated
    // on MSIX status through Package.

    let is_pinned = TaskbarManager::GetDefault()
        .map_err(IsPinnedError::GetTaskbarManager)?
        .IsCurrentAppPinnedAsync()
        .map_err(IsPinnedError::ScheduleIsCurrentAppPinned)?
        .await
        .map_err(IsPinnedError::IsCurrentAppPinned)?;

    Ok(is_pinned)
}

mod aumid {
    //! This module provides a scoped override of the current process's
    //! AppUserModelID (AUMID) that orders concurrent overrides and restores the
    //! AUMID on drop.

    use futures::lock::{Mutex, MutexGuard};
    use nsstring::{nsAString, nsString};
    use std::sync::LazyLock;
    use windows::{
        Win32::{
            System::Com::CoTaskMemFree,
            UI::Shell::{
                GetCurrentProcessExplicitAppUserModelID, SetCurrentProcessExplicitAppUserModelID,
            },
        },
        core::{HSTRING, PCWSTR},
    };

    use super::WinRtPinError;

    static DEFAULT_AUMID: LazyLock<Result<Mutex<HSTRING>, WinRtPinError>> = LazyLock::new(|| {
        // SAFETY: GetCurrentProcessExplicitAppUserModelID handles pointer
        // safety directly, and should probably be marked safe.
        //
        // PWSTR is only used when GetCurrentProcessExplicitAppUserModelID
        // succeeds therefore is a non-null and valid.
        unsafe {
            GetCurrentProcessExplicitAppUserModelID().map(|aumid| {
                let hstr = aumid.to_hstring();
                CoTaskMemFree(Some(aumid.0 as _));
                Mutex::new(hstr)
            })
        }
        .map_err(WinRtPinError::GetAumid)
    });

    /// Holder for the AUMID Mutex lock to ensure only one task sets the process
    /// AUMID at a time.
    pub(super) struct Holder<'a> {
        default_aumid_lock: MutexGuard<'a, HSTRING>,
    }

    impl Holder<'_> {
        /// Attempts to acquire a lock to set the current process AUMID, then
        /// set it to the provided AUMID.
        #[must_use]
        pub(super) async fn set_aumid(temp_aumid: &nsAString) -> Result<Self, WinRtPinError> {
            // Block while AUMID is temporarily modified.
            let default_aumid_lock = DEFAULT_AUMID.as_ref()?.lock().await;
            let original_aumid = &*default_aumid_lock;

            log::info!("Original process AUMID was {original_aumid}, setting it to {temp_aumid}");

            let temp_aumid = nsString::from(temp_aumid);
            // SAFETY: nsString is inherently non-null and null-terminated
            // therefore valid to construct a PCWSTR from to pass to
            // SetCurrentProcessExplicitAppUserModelID.
            unsafe {
                SetCurrentProcessExplicitAppUserModelID(PCWSTR::from_raw(temp_aumid.as_ptr()))
            }
            .map_err(WinRtPinError::SetAumid)?;

            Ok(Self { default_aumid_lock })
        }

        /// Drops self to trigger the AUMID to revert to the default and release
        /// the lock to set the default AUMID.
        pub(super) fn restore_aumid(self) {}
    }

    impl Drop for Holder<'_> {
        /// Restore the default AUMID.
        fn drop(&mut self) {
            log::info!("Restoring process AUMID to {:?}", self.default_aumid_lock);
            // SAFETY: HSTRING deref ensures it returns a null-terminated string
            // even when empty, which is a valid parameter for
            // SetCurrentProcessExplicitAppUserModelID.
            if let Err(e) =
                unsafe { SetCurrentProcessExplicitAppUserModelID(&*self.default_aumid_lock) }
            {
                log::error!("Error restoring AUMID: {e:?}");
            }
        }
    }
}

// `#[warn(dead_code)]` ignores usage of the Debug trait; suppress it to allow
// `WinError` to be included in logs.
#[allow(dead_code)]
#[derive(Clone, Debug)]
pub(super) enum WinRtPinError {
    GetAumid(WinError),
    SetAumid(WinError),
    GetTaskbarManager(WinError),
    ScheduleRequestPin(WinError),
    RequestPin(WinError),
}

impl WinRtPinError {
    /// Returns the HRESULT of the underlying Windows error.
    pub fn hresult(&self) -> windows::core::HRESULT {
        use WinRtPinError::*;
        match self {
            GetAumid(e)
            | SetAumid(e)
            | GetTaskbarManager(e)
            | ScheduleRequestPin(e)
            | RequestPin(e) => e.code(),
        }
    }

    /// Converts Error into Glean metric strings.
    pub fn to_metric_taskbar_pin_winrt(&self) -> &'static str {
        use WinRtPinError::*;
        match self {
            GetAumid(_) => "error_get_aumid",
            SetAumid(_) => "error_set_aumid",
            GetTaskbarManager(_) => "error_get_taskbar_manager",
            ScheduleRequestPin(_) => "error_schedule_request_pin",
            RequestPin(_) => "error_request_pin",
        }
    }
}

impl From<&WinRtPinError> for WinRtPinError {
    fn from(e: &WinRtPinError) -> Self {
        e.clone()
    }
}

impl From<WinRtPinError> for nsresult {
    fn from(e: WinRtPinError) -> Self {
        use WinRtPinError::*;
        match e {
            GetAumid(_) | SetAumid(_) | ScheduleRequestPin(_) | RequestPin(_) => {
                NS_ERROR_UNEXPECTED
            }
            GetTaskbarManager(_) => NS_ERROR_NOT_AVAILABLE,
        }
    }
}

// `#[warn(dead_code)]` ignores usage of the Debug trait; suppress it to allow
// `WinError` to be included in logs.
#[allow(dead_code)]
#[derive(Debug)]
pub(super) enum IsPinnedError {
    GetTaskbarManager(WinError),
    ScheduleIsCurrentAppPinned(WinError),
    IsCurrentAppPinned(WinError),
}

#[cfg(feature = "enable_tests")]
mod test {
    //! Test-only module to stub WinRT pin results via prefs.

    use nsstring::nsCString;
    use windows::core::Error as WinError;
    use xpcom::interfaces::nsIPrefBranch;

    use super::{
        PinResult::{self, *},
        WinRtPinError::{self, *},
    };

    /// Maps `browser.shell.taskbar.test.pinWinRtStubResult` to the pin Result.
    pub(super) fn pin_result_from_pref() -> Option<Result<PinResult, WinRtPinError>> {
        let value = get_char_pref(c"browser.shell.taskbar.test.pinWinRtStubResult")?;
        Some(match value.to_utf8().as_ref() {
            "success_pinned" => Ok(Pinned),
            "success_rejected" => Ok(Rejected),
            "success_fire_and_forget" => Ok(Unknown),
            "error_get_aumid" => Err(GetAumid(WinError::empty())),
            "error_set_aumid" => Err(SetAumid(WinError::empty())),
            "error_get_taskbar_manager" => Err(GetTaskbarManager(WinError::empty())),
            "error_schedule_request_pin" => Err(ScheduleRequestPin(WinError::empty())),
            "error_request_pin" => Err(RequestPin(WinError::empty())),
            other => {
                log::error!("Unknown pinWinrtStubResult pref value: {other}");
                return None;
            }
        })
    }

    /// Attempts to retrieve the provided preference.
    fn get_char_pref(name: &std::ffi::CStr) -> Option<nsCString> {
        let mut value = nsCString::new();
        let prefs = xpcom::get_service::<nsIPrefBranch>(c"@mozilla.org/preferences-service;1")?;
        // SAFETY: nsCString points to valid, initialized memory.
        unsafe { prefs.GetCharPref(name.as_ptr(), &mut *value) }
            .to_result()
            .ok()?;
        Some(value)
    }
}
