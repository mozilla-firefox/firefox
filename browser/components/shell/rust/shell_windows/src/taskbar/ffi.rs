/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! FFI accessible entry points into the taskbar module.

use nserror::{
    NS_ERROR_NOT_AVAILABLE, NS_ERROR_NOT_SAME_THREAD, NS_ERROR_UNEXPECTED, NS_OK, nsresult,
};
use nsstring::{nsAString, nsString};
use xpcom::{
    Promise, RefPtr,
    interfaces::{nsIVariant, nsIWritableVariant},
};

use super::{PinResult, can_pin, pin_app, unpin_shortcut};
use crate::util::thread_guard::{self, ThreadGuard};

/// FFI accessible interface to check if taskbar pinning APIs are available.
///
/// # Safety
///
/// No safety considerations, marked unsafe to satisfy FFI requirements.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn shell_windows_taskbar_can_pin_to_taskbar() -> nsresult {
    let main_guard = match thread_guard::get_thread_guard() {
        ThreadGuard::Main(guard) => guard,
        _ => {
            log::error!("Must be called on main thread to check for pinning APIs.");
            return NS_ERROR_NOT_SAME_THREAD;
        }
    };

    match can_pin(main_guard) {
        true => NS_OK,
        false => NS_ERROR_NOT_AVAILABLE,
    }
}

/// FFI accessible interface to asynchronously check whether the current app is
/// pinned to the taskbar. On MSIX this uses the WinRT TaskbarManager API; on
/// non-MSIX installs it scans the taskbar shortcuts for a matching AUMID.
///
/// # Safety
///
/// The caller is responsible for ensuring all pointers point to initialized
/// memory if non-null.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn shell_windows_taskbar_is_current_app_pinned(
    aumid: &nsAString,
    promise: &Promise,
) -> nsresult {
    if !moz_task::is_main_thread() {
        log::error!("Pin check must be called from the main thread to resolve the DOM promise.");
        return NS_ERROR_NOT_SAME_THREAD;
    }

    let aumid = nsString::from(aumid);
    let promise = RefPtr::new(promise);

    moz_task::spawn_local("Is Current App Pinned to Taskbar", async move {
        let is_pinned = super::is_pinned(&aumid).await.and_then(bool_to_variant);

        match is_pinned {
            Ok(variant) => promise.resolve_with_variant(&variant),
            Err(e) => promise.reject_with_nsresult(e),
        }
    })
    .detach();

    NS_OK
}

/// FFI accessible interface to asynchronously pin a given shortcut and AUMID to
/// the taskbar. When `prompt_only` is set, only the WinRT API (which prompts
/// the user) is used and the COM fallback is skipped.
///
/// # Safety
///
/// The caller is responsible for ensuring all pointers point to initialized
/// memory if non-null.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn shell_windows_taskbar_pin_app_to_taskbar(
    aumid: &nsAString,
    shortcut_path: &nsAString,
    fire_and_forget: bool,
    prompt_only: bool,
    promise: &Promise,
) -> nsresult {
    let main_guard = match thread_guard::get_thread_guard() {
        ThreadGuard::Main(guard) => guard,
        _ => {
            log::error!("Pinning must be called from main thread to resolve DOM promise.");
            return NS_ERROR_NOT_SAME_THREAD;
        }
    };

    let aumid = nsString::from(aumid);
    let shortcut_path = nsString::from(shortcut_path);
    let promise = RefPtr::new(promise);

    moz_task::spawn_local("Pin to Taskbar", async move {
        let result = pin_app(
            &aumid,
            &shortcut_path,
            fire_and_forget,
            prompt_only,
            main_guard,
        )
        .await
        .and_then(pin_result_to_variant);

        match result {
            Ok(variant) => promise.resolve_with_variant(&variant),
            Err(e) => promise.reject_with_nsresult(e),
        }
    })
    .detach();

    NS_OK
}

/// FFI accessible interface to unpin a given shortcut from the taskbar.
///
/// # Safety
///
/// The caller is responsible for ensuring all pointers point to initialized
/// memory if non-null.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn shell_windows_taskbar_unpin_shortcut_from_taskbar(
    shortcut_path: &nsAString,
) -> nsresult {
    let main_guard = match thread_guard::get_thread_guard() {
        ThreadGuard::Main(guard) => guard,
        _ => {
            log::error!(
                "Unpinning must be called from the main thread to ensure the underlying COM API is run from an STA thread."
            );
            return NS_ERROR_NOT_SAME_THREAD;
        }
    };

    match unpin_shortcut(shortcut_path, main_guard) {
        Ok(_) => NS_OK,
        Err(e) => e,
    }
}

fn create_writable_variant() -> Result<RefPtr<nsIWritableVariant>, nsresult> {
    xpcom::create_instance::<nsIWritableVariant>(c"@mozilla.org/variant;1").ok_or_else(|| {
        log::error!("Failed to create writable variant.");
        NS_ERROR_UNEXPECTED
    })
}

fn pin_result_to_variant(result: PinResult) -> Result<RefPtr<nsIVariant>, nsresult> {
    let variant = create_writable_variant()?;

    // SAFETY: No invariants to uphold as parameter is POD.
    unsafe { variant.SetAsUint8(result.into()) }
        .to_result()
        .inspect_err(|e| log::error!("Failed to set Uint8 on nsIWritableVariant: {e:?}"))?;

    Ok(RefPtr::new(variant.coerce()))
}

fn bool_to_variant(value: bool) -> Result<RefPtr<nsIVariant>, nsresult> {
    let variant = create_writable_variant()?;

    // SAFETY: No invariants to uphold as parameter is POD.
    unsafe { variant.SetAsBool(value) }
        .to_result()
        .inspect_err(|e| log::error!("Failed to set Bool on nsIWritableVariant: {e:?}"))?;

    Ok(RefPtr::new(variant.coerce()))
}
