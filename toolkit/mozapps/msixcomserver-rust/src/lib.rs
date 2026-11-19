/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! Static library backing `msixcomserver.dll`, the packaged (MSIX) build's
//! host DLL for MSIX-specific COM servers.

use std::future::IntoFuture;
use std::sync::atomic::{AtomicI32, Ordering};

use futures_executor::block_on;
use windows::ApplicationModel::Background::{
    IBackgroundTask, IBackgroundTaskInstance, IBackgroundTask_Impl,
};
use windows::ApplicationModel::FullTrustProcessLauncher;
use windows::Win32::Foundation::{CLASS_E_CLASSNOTAVAILABLE, S_FALSE, S_OK};
use windows::Win32::System::Diagnostics::Debug::OutputDebugStringW;
use windows::Win32::System::WinRT::{IActivationFactory, IActivationFactory_Impl};
use windows_core::{
    implement, IInspectable, OutRef, Ref, Result as WindowsResult, HRESULT, HSTRING,
};

const ACTIVATABLE_CLASS_ID: &str = mozbuild::config::MOZ_BACKGROUNDTASK_ACTIVATABLE_CLASS_ID;

/// Number of live COM objects plus outstanding server locks. Windows may call
/// `DllCanUnloadNow` to decide whether to unload this DLL; it must stay loaded
/// while this count is non-zero.
static LOCK_COUNT: AtomicI32 = AtomicI32::new(0);

fn lock_module() {
    LOCK_COUNT.fetch_add(1, Ordering::Relaxed);
}

fn unlock_module() {
    LOCK_COUNT.fetch_sub(1, Ordering::Release);
}

#[implement(IBackgroundTask)]
struct FirefoxBackgroundTask;

impl FirefoxBackgroundTask {
    fn new() -> Self {
        lock_module();
        FirefoxBackgroundTask
    }
}

impl Drop for FirefoxBackgroundTask {
    fn drop(&mut self) {
        unlock_module();
    }
}

impl IBackgroundTask_Impl for FirefoxBackgroundTask_Impl {
    fn Run(&self, task_instance: Ref<IBackgroundTaskInstance>) -> WindowsResult<()> {
        let Some(instance) = task_instance.as_ref() else {
            return Ok(());
        };

        // An error returned from `Run` makes `backgroundtaskhost.exe` fail fast
        // and report a crash, so errors are logged and swallowed.
        let name = match instance.Task().and_then(|task| task.Name()) {
            Ok(name) => name.to_string(),
            Err(e) => {
                debug_log(&format!("Could not read the task name: {e}"));
                return Ok(());
            }
        };
        if name.is_empty() {
            return Ok(());
        }

        // Windows activates this DLL inside `backgroundtaskhost.exe`, which runs
        // in an AppContainer at low integrity. The tasks we run need the same
        // access a normal Firefox launch has, so ask the shell to start the package's
        // full trust process.
        if let Err(e) = launch_full_trust(&name) {
            debug_log(&format!("Could not launch task {name}: {e}"));
        }
        Ok(())
    }
}

fn debug_log(message: &str) {
    let message = HSTRING::from(format!("msixcomserver: {message}\n"));
    unsafe { OutputDebugStringW(&message) };
}

/// The manifest `desktop:ParameterGroup` ID for a task. Every task needs a
/// group in AppxManifest.xml.in whose ID is the task name with ":" replaced
/// by "-". The group's `Parameters` hold the command line, for example
/// "defaultagent:do-task" launches `firefox --backgroundtask defaultagent do-task`.
fn parameter_group_id(task_name: &str) -> String {
    task_name.replace(':', "-")
}

/// Ask the shell to start the package's full trust process with the
/// parameters of the task's manifest group. The free arguments variant of this
/// API needs FullTrustAppContract v2, which Windows 10 does not have.
/// See https://learn.microsoft.com/en-us/windows/apps/windows-app-sdk/migrate-to-windows-app-sdk/guides/background-task-migration-strategy
fn launch_full_trust(task_name: &str) -> WindowsResult<()> {
    let group_id = HSTRING::from(parameter_group_id(task_name));
    let operation =
        FullTrustProcessLauncher::LaunchFullTrustProcessForCurrentAppWithParametersAsync(
            &group_id,
        )?;
    block_on(operation.into_future())?;
    Ok(())
}

#[implement(IActivationFactory)]
struct BackgroundTaskActivationFactory;

impl IActivationFactory_Impl for BackgroundTaskActivationFactory_Impl {
    fn ActivateInstance(&self) -> WindowsResult<IInspectable> {
        Ok(FirefoxBackgroundTask::new().into())
    }
}

/// WinRT in-process activation entry point.
/// This symbol is re-exported from the DLL by `msixcomserver.def`.
///
/// # Safety
///
/// Called by the WinRT runtime with the ABI-compatible `Ref` and `OutRef`
/// wrappers, which handle the borrowed name and the out parameter. `OutRef::write`
/// rejects a null `factory` with `E_POINTER`.
#[unsafe(no_mangle)]
pub unsafe extern "system" fn DllGetActivationFactory(
    name: Ref<HSTRING>,
    factory: OutRef<IActivationFactory>,
) -> HRESULT {
    if name.to_string() != ACTIVATABLE_CLASS_ID {
        let _ = factory.write(None);
        return CLASS_E_CLASSNOTAVAILABLE;
    }

    let instance: IActivationFactory = BackgroundTaskActivationFactory.into();
    factory.write(Some(instance)).into()
}

/// In-process server unload check: `S_OK` when no objects are alive and no
/// server locks are held, otherwise `S_FALSE`.
#[unsafe(no_mangle)]
pub extern "system" fn DllCanUnloadNow() -> HRESULT {
    if LOCK_COUNT.load(Ordering::Acquire) == 0 {
        S_OK
    } else {
        S_FALSE
    }
}
