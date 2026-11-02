/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! # Taskbar Pinning
//!
//! This module abstracts over the WinRT and COM APIs relevant to Windows
//! Taskbar pinning.

use nserror::{NS_ERROR_FAILURE, NS_ERROR_UNEXPECTED, nsresult};
use nsstring::{nsAString, nsString};
use windows::ApplicationModel::Package;
use xpcom::{
    RefPtr,
    interfaces::{nsIWinTaskbar, nsIWindowsRegKey, nsIWindowsShellService},
};

use crate::util::thread_guard::{self, MainThreadGuard};

mod com;
mod ffi;
mod shortcut;
mod winrt;

/// Result from the attempt to pin to taskbar.
enum PinResult {
    /// Pin request affirmed by user.
    Pinned,
    /// Pin request rejected by user or system.
    Rejected,
    /// Either returned before pin request was acted upon, or fell back to an
    /// API where success isn't known.
    Unknown,
}

impl From<PinResult> for u8 {
    fn from(result: PinResult) -> Self {
        match result {
            PinResult::Pinned => nsIWindowsShellService::PINNED,
            PinResult::Rejected => nsIWindowsShellService::REJECTED,
            PinResult::Unknown => nsIWindowsShellService::UNKNOWN,
        }
    }
}

/// Checks whether pinning to the taskbar should be possible.
fn can_pin(main_guard: MainThreadGuard) -> bool {
    if let Some((policy, hive)) = find_policy_disabling_pin() {
        log::info!("Pinning disabled by policy {policy:?} in hive {hive:?}.");
        return false;
    }

    use winrt::CanPin::*;
    match winrt::can_pin() {
        // Respect the OS assertion that pinning is not allowed when pinning via
        // WinRT is supported.
        Ok(Supported { allowed }) => {
            log::trace!("WinRT pinning supported, allowed = {allowed:?}");
            return allowed;
        }
        Ok(Unsupported) => log::trace!("Win32 pinning via WinRT not supported by this OS."),
        Err(e) => log::error!("Error checking if we can pin via WinRT: {e:?}"),
    }

    // COM pinning is nonfunctional on MSIX as we currently implement it.
    //
    // Note: This API might work if a to-be-pinned shortcut were created to a
    // non-virtualized path, including an unvirtualized path within this
    // package’s AppData directory. This has not been validated, and we cannot
    // guarantee cleanup of those shortcuts or unpinning from the taskbar on
    // uninstall; see https://github.com/microsoft/WindowsAppSDK/issues/2779.
    if Package::Current().is_err() {
        let present = com::is_pinning_available(main_guard);
        log::trace!("COM pinning present: {present:?}");

        return present;
    }

    false
}

/// Pins the shortcut with matching AUMID to the taskbar.
///
/// When `prompt_only` is set, pinning is only attempted through the WinRT API,
/// which asks the user for consent with a system prompt.
async fn pin_app(
    aumid: &nsAString,
    shortcut_path: &nsAString,
    fire_and_forget: bool,
    prompt_only: bool,
    main_guard: MainThreadGuard,
) -> Result<PinResult, nsresult> {
    if Package::Current().is_ok() && !matches_default_aumid(&aumid)? {
        // Bug 1911343: We should make this API impossible to misuse this way by
        // consolidating SecondaryTile pinning herein.
        log::error!(
            "TaskbarManager pinning only supports apps defined in the package manifest; use SecondaryTiles instead for runtime-defined pin targets"
        );
        return Err(NS_ERROR_FAILURE);
    }

    // Attempt to use the documented WinRT pinning API.
    let winrt_pin = winrt::pin_to_taskbar(aumid, fire_and_forget, main_guard).await;

    let com_fallback = winrt_pin.is_err() && !prompt_only;
    record_winrt_pin_telemetry(&winrt_pin, com_fallback);

    winrt_pin.or_else(|e| {
        use winrt::WinRtPinError::GetTaskbarManager;
        match e {
            GetTaskbarManager(_) => log::debug!("Error pinning with WinRT API: {e:?}"),
            _ => log::error!("Error pinning with WinRT API: {e:?}"),
        }

        if !com_fallback {
            log::info!("Prompt-only pin requested, skipping COM fallback.");
            return Err(e.into());
        }

        // Fallback to undocumented COM API.
        com::modify_taskbar(com::PinOp::Pin, shortcut_path, main_guard)
    })
}

/// Checks if the current app is pinned.
async fn is_pinned(aumid: &nsAString) -> Result<bool, nsresult> {
    match Package::Current() {
        Ok(package) => {
            if !matches_default_aumid(&aumid)? {
                // Bug 1911343: We should make this API impossible to misuse this way by
                // consolidating SecondaryTile pin checks herein.
                log::error!(
                    "TaskbarManager pin checking only supports apps defined in the package manifest; use SecondaryTiles instead for runtime-defined pin targets"
                );
                return Err(NS_ERROR_FAILURE);
            }

            winrt::is_current_app_pinned(package).await.map_err(|e| {
                log::error!(
                    "Error checking whether the current app is pinned to the taskbar: {e:?}"
                );
                NS_ERROR_FAILURE
            })
        }
        Err(_) => {
            let aumid = nsString::from(aumid);

            thread_guard::spawn_background_guard(
                "shell_windows::taskbar::is_pinned",
                async move |bg_guard| shortcut::is_app_pinned(&aumid.to_string(), bg_guard),
            )
            .await
            .map_err(|e| {
                log::error!(
                    "Error checking whether the current app is pinned to the taskbar: {e:?}"
                );
                NS_ERROR_FAILURE
            })
        }
    }
}

/// Unpins the provided shortcut from the taskbar.
fn unpin_shortcut(shortcut_path: &nsAString, main_guard: MainThreadGuard) -> Result<(), nsresult> {
    com::modify_taskbar(com::PinOp::UnPin, shortcut_path, main_guard).map(|_| ())
}

/// Checks if the provided AUMID matches the app default AUMID.
fn matches_default_aumid(aumid: &nsAString) -> Result<bool, nsresult> {
    let taskbar = xpcom::create_instance::<nsIWinTaskbar>(c"@mozilla.org/windows-taskbar;1")
        .ok_or(NS_ERROR_UNEXPECTED)?;

    let mut default_aumid = nsString::new();
    // SAFETY: `group_id` is a valid writable XPCOM string.
    unsafe { taskbar.GetDefaultGroupId(&mut *default_aumid) }.to_result()?;

    Ok(*aumid == default_aumid)
}

/// Records Glean telemetry for the attempted pin to taskbar using WinRT.
fn record_winrt_pin_telemetry(
    pin_result: &Result<PinResult, winrt::WinRtPinError>,
    com_fallback: bool,
) {
    use firefox_on_glean::metrics::taskbar::{self, PinWinrtExtra};

    use PinResult::*;
    let metric_extra = match pin_result {
        Ok(Pinned) => "success_pinned",
        Ok(Rejected) => "success_rejected",
        Ok(Unknown) => "success_fire_and_forget",
        Err(e) => e.to_metric_taskbar_pin_winrt(),
    };

    let hresult = pin_result
        .as_ref()
        .err()
        .map(|e| format!("{:#010x}", e.hresult().0));

    taskbar::pin_winrt.record(PinWinrtExtra {
        result: Some(metric_extra.into()),
        hresult,
        com_fallback: Some(com_fallback),
    });
}

#[derive(Clone, Copy, Debug)]
enum PinPolicy {
    NoPinningToTaskbar,
    TaskbarNoPinnedList,
}

impl PinPolicy {
    fn to_reg_value_name(self) -> &'static str {
        match self {
            PinPolicy::NoPinningToTaskbar => "NoPinningToTaskbar",
            PinPolicy::TaskbarNoPinnedList => "TaskbarNoPinnedList",
        }
    }
}

#[derive(Clone, Copy, Debug)]
enum Hive {
    HKCU,
    HKLM,
}

impl Hive {
    fn to_root_key(self) -> u32 {
        match self {
            Hive::HKCU => nsIWindowsRegKey::ROOT_KEY_CURRENT_USER,
            Hive::HKLM => nsIWindowsRegKey::ROOT_KEY_LOCAL_MACHINE,
        }
    }
}

const PIN_POLICIES: [(PinPolicy, Hive); 3] = [
    // NoPinningToTaskbar, class="User"
    //
    // "... users can't unpin these programs already pinned to the Taskbar, and
    // they can't pin new programs to the Taskbar"
    // https://learn.microsoft.com/en-us/windows/configuration/taskbar/policy-settings?tabs=actions&pivots=windows-10#do-not-allow-pinning-programs-to-the-taskbar
    (PinPolicy::NoPinningToTaskbar, Hive::HKCU),
    // TaskbarNoPinnedList, class="Both"
    //
    // "... Users can't pin programs to the taskbar"
    // https://learn.microsoft.com/en-us/windows/configuration/taskbar/policy-settings?tabs=taskbar&pivots=windows-10#remove-pinned-programs-from-the-taskbar
    (PinPolicy::TaskbarNoPinnedList, Hive::HKCU),
    (PinPolicy::TaskbarNoPinnedList, Hive::HKLM),
];

/// Returns the first group policy found disabling taskbar pinning, if present.
fn find_policy_disabling_pin() -> Option<&'static (PinPolicy, Hive)> {
    PIN_POLICIES
        .iter()
        .find(|policy| read_pin_policy(policy.1, policy.0).is_some_and(|enabled| enabled != 0))
}

/// Reads a taskbar pinning policy DWORD from the given hive, returning `None`
/// when the policy is unset or unreadable.
fn read_pin_policy(hive: Hive, policy: PinPolicy) -> Option<u32> {
    let explorer_policy_subkey = nsString::from(r"Software\Policies\Microsoft\Windows\Explorer");

    let reg: RefPtr<nsIWindowsRegKey> =
        xpcom::create_instance(c"@mozilla.org/windows-registry-key;1")?;

    // SAFETY: `explorer_policy_subkey` was initialized above.
    unsafe {
        reg.Open(
            hive.to_root_key(),
            &*explorer_policy_subkey,
            nsIWindowsRegKey::ACCESS_QUERY_VALUE,
        )
    }
    .to_result()
    .ok()?;

    let value_name = nsString::from(policy.to_reg_value_name());
    let mut value = 0u32;
    // SAFETY: `value_name` and `value` were initialized above.
    unsafe { reg.ReadIntValue(&*value_name, &mut value) }
        .to_result()
        .ok()?;

    Some(value)
}
