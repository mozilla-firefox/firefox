/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Windows11TaskbarPinning.h"

#include "nsILimitedAccessFeature.h"
#include "nsWindowsHelpers.h"
#include "MainThreadUtils.h"
#include "nsThreadUtils.h"
#include <shobjidl.h>
#include <strsafe.h>

#include "mozilla/Result.h"
#include "mozilla/ResultVariant.h"
#include "mozilla/WindowsVersion.h"
#include "mozilla/WinHeaderOnlyUtils.h"
#include "mozilla/widget/WinTaskbar.h"
#include "WinUtils.h"

#include "mozilla/Logging.h"

static mozilla::LazyLogModule sLog("Windows11TaskbarPinning");

#define TASKBAR_PINNING_LOG(level, msg, ...) \
  MOZ_LOG(sLog, level, (msg, ##__VA_ARGS__))

#ifndef __MINGW32__  // WinRT headers not yet supported by MinGW

#  include <wrl.h>

#  include <inspectable.h>
#  include <roapi.h>
#  include <shlobj_core.h>
#  include <windows.services.store.h>
#  include <windows.foundation.h>
#  include <windows.ui.shell.h>

using namespace mozilla;

/**
 * The Win32 SetEvent and WaitForSingleObject functions take HANDLE parameters
 * which are typedefs of void*. When using nsAutoHandle, that means if you
 * forget to call .get() first, everything still compiles and then doesn't work
 * at runtime. For instance, calling SetEvent(mEvent) below would compile but
 * not work at runtime and the waits would block forever.
 * To ensure this isn't an issue, we wrap the event in a custom class here
 * with the simple methods that we want on an event.
 */
class EventWrapper {
 public:
  EventWrapper() : mEvent(CreateEventW(nullptr, true, false, nullptr)) {}

  void Set() { SetEvent(mEvent.get()); }

  void Reset() { ResetEvent(mEvent.get()); }

  void Wait() { WaitForSingleObject(mEvent.get(), INFINITE); }

 private:
  nsAutoHandle mEvent;
};

using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Windows;
using namespace ABI::Windows::UI::Shell;
using namespace ABI::Windows::Foundation;
using namespace ABI::Windows::ApplicationModel;

static Win11PinToTaskBarResult UnlockTaskbarPinFeature() {
  nsCOMPtr<nsILimitedAccessFeatureService> lafService =
      do_GetService("@mozilla.org/limited-access-feature-service;1");

  nsAutoCString pinFeatureId;
  nsresult rv = lafService->GetTaskbarPinFeatureId(pinFeatureId);

  if (NS_WARN_IF(NS_FAILED(rv))) {
    return {E_FAIL, Win11PinToTaskBarResultStatus::NotSupported};
  }

  nsCOMPtr<nsILimitedAccessFeature> feature;
  rv = lafService->GenerateLimitedAccessFeature(pinFeatureId,
                                                getter_AddRefs(feature));

  if (NS_WARN_IF(NS_FAILED(rv))) {
    return {E_FAIL, Win11PinToTaskBarResultStatus::NotSupported};
  }

  bool unlocked = false;
  rv = feature->Unlock(&unlocked);

  if (NS_WARN_IF(NS_FAILED(rv))) {
    return {E_FAIL, Win11PinToTaskBarResultStatus::NotSupported};
  }

  if (!unlocked) {
    TASKBAR_PINNING_LOG(
        LogLevel::Debug,
        "Taskbar unlock: failed. Not supported on this version of Windows.");
    return {S_OK, Win11PinToTaskBarResultStatus::NotSupported};
  }

  return {S_OK, Win11PinToTaskBarResultStatus::Success};
}

static Result<ComPtr<ITaskbarManager>, HRESULT> InitializeTaskbar() {
  ComPtr<IInspectable> taskbarStaticsInspectable;

  TASKBAR_PINNING_LOG(LogLevel::Debug, "Initializing taskbar");

  HRESULT hr = RoGetActivationFactory(
      HStringReference(RuntimeClass_Windows_UI_Shell_TaskbarManager).Get(),
      IID_ITaskbarManagerStatics, &taskbarStaticsInspectable);
  if (FAILED(hr)) {
    TASKBAR_PINNING_LOG(LogLevel::Debug,
                        "Taskbar: Failed to activate. HRESULT = 0x%lx", hr);
    return Err(hr);
  }

  ComPtr<ITaskbarManagerStatics> taskbarStatics;

  hr = taskbarStaticsInspectable.As(&taskbarStatics);
  if (FAILED(hr)) {
    TASKBAR_PINNING_LOG(LogLevel::Debug, "Failed statistics. HRESULT = 0x%lx",
                        hr);
    return Err(hr);
  }

  ComPtr<ITaskbarManager> taskbarManager;

  hr = taskbarStatics->GetDefault(&taskbarManager);
  if (FAILED(hr)) {
    TASKBAR_PINNING_LOG(LogLevel::Debug,
                        "Error getting TaskbarManager. HRESULT = 0x%lx", hr);
    return Err(hr);
  }

  TASKBAR_PINNING_LOG(LogLevel::Debug,
                      "TaskbarManager retrieved successfully!");
  return taskbarManager;
}

static Win11PinToTaskBarResultStatus IsTaskbarPinningAllowed(
    bool aCheckOnly, ComPtr<ITaskbarManager>& taskbar) {
  HRESULT hr;
  auto result = InitializeTaskbar();
  if (result.isErr()) {
    hr = result.unwrapErr();
    return Win11PinToTaskBarResultStatus::NotSupported;
  }

  taskbar = result.unwrap();
  boolean supported;
  hr = taskbar->get_IsSupported(&supported);
  if (FAILED(hr) || !supported) {
    if (FAILED(hr)) {
      TASKBAR_PINNING_LOG(
          LogLevel::Debug,
          "Taskbar: error checking if supported. HRESULT = 0x%lx", hr);
    } else {
      TASKBAR_PINNING_LOG(LogLevel::Debug, "Taskbar: not supported.");
    }
    return Win11PinToTaskBarResultStatus::NotSupported;
  }

  if (aCheckOnly) {
    TASKBAR_PINNING_LOG(LogLevel::Debug, "Taskbar: check succeeded.");
    return Win11PinToTaskBarResultStatus::Success;
  }

  boolean isAllowed = false;
  hr = taskbar->get_IsPinningAllowed(&isAllowed);
  if (FAILED(hr) || !isAllowed) {
    if (FAILED(hr)) {
      TASKBAR_PINNING_LOG(
          LogLevel::Debug,
          "Taskbar: error checking if pinning is allowed. HRESULT = "
          "0x%lx",
          hr);
    } else {
      TASKBAR_PINNING_LOG(
          LogLevel::Debug,
          "Taskbar: is pinning allowed error or isn't allowed right now. "
          "It's not clear when it will be allowed. Possibly after a "
          "reboot.");
    }
    return Win11PinToTaskBarResultStatus::NotCurrentlyAllowed;
  }
  return Win11PinToTaskBarResultStatus::Success;
}

Win11PinToTaskBarResult PinCurrentAppToTaskbarWin11(
    bool aCheckOnly, const nsAString& aAppUserModelId,
    const bool aFireAndForget) {
  MOZ_DIAGNOSTIC_ASSERT(!NS_IsMainThread(),
                        "PinCurrentAppToTaskbarWin11 should be called off main "
                        "thread only. It blocks, waiting on things to execute "
                        "asynchronously on the main thread.");

  Win11PinToTaskBarResult unlockStatus = UnlockTaskbarPinFeature();
  if (unlockStatus.result != Win11PinToTaskBarResultStatus::Success) {
    // Limited Access Feature no longer necessary for Windows 11 26200 Build
    // 7840, and possibly other channels.
    if (!IsWin11OrLater()) {
      return unlockStatus;
    }

    TASKBAR_PINNING_LOG(
        LogLevel::Warning,
        "Limited Access Feature failed to unlock, attempting to use Taskbar "
        "Pinning API assuming LAF is no longer necessary.");
  }

  HRESULT hr;
  Win11PinToTaskBarResultStatus resultStatus =
      Win11PinToTaskBarResultStatus::NotSupported;

  EventWrapper event;

  // Everything related to the taskbar and pinning must be done on the main /
  // user interface thread or Windows will cause them to fail.
  NS_DispatchToMainThread(NS_NewRunnableFunction(
      "PinCurrentAppToTaskbarWin11",
      [&event, &hr, &resultStatus, aCheckOnly, aFireAndForget,
       aumid = nsString(aAppUserModelId)] {
        // We eventualy want to call SetCurrentProcessExplicitAppUserModelID()
        // on the main thread as it is not thread safe and pinning is called
        // numerous times in many different places. This is a hack used
        // explicitly for the purpose of re-enabling private browser pinning
        // as a stopgap and should not be replicated elsewhere.
        // GenerateAppUserModelId needs to be called on the main thread as
        // it checks against preferences.
        nsAutoString primaryAumid;
        mozilla::widget::WinTaskbar::GenerateAppUserModelID(primaryAumid,
                                                            false);
        auto CompletedOperations = [&event, &resultStatus,
                                    primaryAumid = nsString(primaryAumid)](
                                       Win11PinToTaskBarResultStatus status) {
          // Set AUMID back and ensure the icon is set correctly
          if (!widget::WinUtils::HasPackageIdentity()) {
            HRESULT hr =
                SetCurrentProcessExplicitAppUserModelID(primaryAumid.get());
            if (FAILED(hr)) {
              TASKBAR_PINNING_LOG(LogLevel::Debug,
                                  "Taskbar: reverting AUMID after pinning "
                                  "operation failed. HRESULT = 0x%lx",
                                  hr);
            }
          }
          resultStatus = status;
          event.Set();
        };

        // Set the process to have the AUMID of the shortcut we want to pin,
        // this is only necessary for Win32 builds
        if (!widget::WinUtils::HasPackageIdentity()) {
          hr = SetCurrentProcessExplicitAppUserModelID(aumid.get());
          if (FAILED(hr)) {
            return CompletedOperations(Win11PinToTaskBarResultStatus::Failed);
          }
        }

        ComPtr<ITaskbarManager> taskbar;
        Win11PinToTaskBarResultStatus allowed =
            IsTaskbarPinningAllowed(aCheckOnly, taskbar);
        if ((aCheckOnly && allowed == Win11PinToTaskBarResultStatus::Success) ||
            allowed != Win11PinToTaskBarResultStatus::Success) {
          return CompletedOperations(allowed);
        }

        ComPtr<IAsyncOperation<bool>> isPinnedOperation = nullptr;
        hr = taskbar->IsCurrentAppPinnedAsync(&isPinnedOperation);
        if (FAILED(hr)) {
          TASKBAR_PINNING_LOG(
              LogLevel::Debug,
              "Taskbar: is current app pinned operation failed. HRESULT = "
              "0x%lx",
              hr);
          return CompletedOperations(Win11PinToTaskBarResultStatus::Failed);
        }

        // Copy the taskbar; don't use it as a reference.
        // With the async calls, it's not guaranteed to still be valid
        // if sent as a reference.
        // resultStatus and event are not defined on the main thread and will
        // be alive until the async functions complete, so they can be used as
        // references.
        auto isPinnedCallback = Callback<IAsyncOperationCompletedHandler<
            bool>>([taskbar, &event, &resultStatus, &hr,
                    primaryAumid = nsString(primaryAumid),
                    aFireAndForget](IAsyncOperation<bool>* asyncInfo,
                                    AsyncStatus status) mutable -> HRESULT {
          auto CompletedOperations =
              [&event, &resultStatus,
               primaryAumid](Win11PinToTaskBarResultStatus status) -> HRESULT {
            // Set AUMID back and ensure the icon is set correctly
            if (!widget::WinUtils::HasPackageIdentity()) {
              HRESULT hr =
                  SetCurrentProcessExplicitAppUserModelID(primaryAumid.get());
              if (FAILED(hr)) {
                TASKBAR_PINNING_LOG(LogLevel::Debug,
                                    "Taskbar: reverting AUMID after pinning "
                                    "operation failed. HRESULT = 0x%lx",
                                    hr);
              }
            }
            resultStatus = status;
            event.Set();
            return S_OK;
          };

          bool asyncOpSucceeded = status == AsyncStatus::Completed;
          if (!asyncOpSucceeded) {
            TASKBAR_PINNING_LOG(
                LogLevel::Debug,
                "Taskbar: is pinned operation failed to complete.");
            return CompletedOperations(Win11PinToTaskBarResultStatus::Failed);
          }

          unsigned char isCurrentAppPinned = false;
          hr = asyncInfo->GetResults(&isCurrentAppPinned);
          if (FAILED(hr)) {
            TASKBAR_PINNING_LOG(
                LogLevel::Debug,
                "Taskbar: is current app pinned check failed. HRESULT = 0x%lx",
                hr);
            return CompletedOperations(Win11PinToTaskBarResultStatus::Failed);
          }

          if (isCurrentAppPinned) {
            TASKBAR_PINNING_LOG(LogLevel::Debug,
                                "Taskbar: current app is already pinned.");
            return CompletedOperations(
                Win11PinToTaskBarResultStatus::AlreadyPinned);
          }

          ComPtr<IAsyncOperation<bool>> requestPinOperation = nullptr;
          hr = taskbar->RequestPinCurrentAppAsync(&requestPinOperation);
          if (FAILED(hr)) {
            TASKBAR_PINNING_LOG(
                LogLevel::Debug,
                "Taskbar: request pin current app operation creation failed. "
                "HRESULT = 0x%lx",
                hr);
            return CompletedOperations(Win11PinToTaskBarResultStatus::Failed);
          } else if (aFireAndForget) {
            return CompletedOperations(Win11PinToTaskBarResultStatus::Success);
          }

          auto pinAppCallback = Callback<IAsyncOperationCompletedHandler<
              bool>>([CompletedOperations, &hr](
                         IAsyncOperation<bool>* asyncInfo,
                         AsyncStatus status) -> HRESULT {
            bool asyncOpSucceeded = status == AsyncStatus::Completed;
            if (!asyncOpSucceeded) {
              TASKBAR_PINNING_LOG(
                  LogLevel::Debug,
                  "Taskbar: request pin current app operation did not "
                  "complete.");
              return CompletedOperations(Win11PinToTaskBarResultStatus::Failed);
            }

            unsigned char userAffirmedPin = 0;
            hr = asyncInfo->GetResults(&userAffirmedPin);
            if (FAILED(hr)) {
              TASKBAR_PINNING_LOG(
                  LogLevel::Debug,
                  "Taskbar: request pin current app operation failed to pin "
                  "due to error. HRESULT = 0x%lx",
                  hr);
              return CompletedOperations(Win11PinToTaskBarResultStatus::Failed);
            }

            // Bug 1890634: Record pinning success rate telemetry
            TASKBAR_PINNING_LOG(
                LogLevel::Debug,
                userAffirmedPin
                    ? "Taskbar: request pin current app operation succeeded"
                    : "Taskbar: user rejected Windows pin prompt");

            return CompletedOperations(Win11PinToTaskBarResultStatus::Success);
          });

          HRESULT pinOperationHR =
              requestPinOperation->put_Completed(pinAppCallback.Get());
          if (FAILED(pinOperationHR)) {
            TASKBAR_PINNING_LOG(
                LogLevel::Debug,
                "Taskbar: request pin operation failed when setting completion "
                "callback. HRESULT = 0x%lx",
                hr);
            hr = pinOperationHR;
            return CompletedOperations(Win11PinToTaskBarResultStatus::Failed);
          }

          // DO NOT SET event HERE. It will be set in the pin operation
          // callback As in, operations are not completed, so don't call
          // CompletedOperations
          return S_OK;
        });

        HRESULT isPinnedOperationHR =
            isPinnedOperation->put_Completed(isPinnedCallback.Get());
        if (FAILED(isPinnedOperationHR)) {
          hr = isPinnedOperationHR;
          TASKBAR_PINNING_LOG(
              LogLevel::Debug,
              "Taskbar: is pinned operation failed when setting completion "
              "callback. HRESULT = 0x%lx",
              hr);
          return CompletedOperations(Win11PinToTaskBarResultStatus::Failed);
        }

        // DO NOT SET event HERE. It will be set in the is pin operation
        // callback As in, operations are not completed, so don't call
        // CompletedOperations
      }));

  // block until the pinning is completed on the main thread
  event.Wait();

  return {hr, resultStatus};
}

Win11PinToTaskBarResult IsCurrentAppPinnedToTaskbarWin11() {
  MOZ_DIAGNOSTIC_ASSERT(
      !NS_IsMainThread(),
      "IsCurrentAppPinnedToTaskbarWin11 should be called off main "
      "thread only. It blocks, waiting on things to execute "
      "asynchronously on the main thread.");

  Win11PinToTaskBarResult unlockStatus = UnlockTaskbarPinFeature();
  if (unlockStatus.result != Win11PinToTaskBarResultStatus::Success) {
    // Limited Access Feature no longer necessary for Windows 11 26200 Build
    // 7840, and possibly other channels.
    if (!IsWin11OrLater()) {
      return unlockStatus;
    }

    TASKBAR_PINNING_LOG(
        LogLevel::Warning,
        "Limited Access Feature failed to unlock, attempting to use Taskbar "
        "Pinning API assuming LAF is no longer necessary.");
  }

  HRESULT hr;
  Win11PinToTaskBarResultStatus resultStatus =
      Win11PinToTaskBarResultStatus::NotSupported;

  EventWrapper event;

  // Everything related to the taskbar and pinning must be done on the main /
  // user interface thread or Windows will cause them to fail.
  NS_DispatchToMainThread(NS_NewRunnableFunction(
      "IsCurrentAppPinnedToTaskbarWin11", [&event, &hr, &resultStatus] {
        auto CompletedOperations =
            [&event, &resultStatus](Win11PinToTaskBarResultStatus status) {
              resultStatus = status;
              event.Set();
            };

        auto result = InitializeTaskbar();
        if (result.isErr()) {
          hr = result.unwrapErr();
          return CompletedOperations(
              Win11PinToTaskBarResultStatus::NotSupported);
        }

        ComPtr<ITaskbarManager> taskbar = result.unwrap();
        ComPtr<IAsyncOperation<bool>> isPinnedOperation = nullptr;
        hr = taskbar->IsCurrentAppPinnedAsync(&isPinnedOperation);
        if (FAILED(hr)) {
          TASKBAR_PINNING_LOG(
              LogLevel::Debug,
              "Taskbar: is current app pinned operation failed. HRESULT = "
              "0x%lx",
              hr);
          return CompletedOperations(Win11PinToTaskBarResultStatus::Failed);
        }

        // Copy the taskbar; don't use it as a reference.
        // With the async calls, it's not guaranteed to still be valid
        // if sent as a reference.
        // resultStatus and event are not defined on the main thread and will
        // be alive until the async functions complete, so they can be used as
        // references.
        auto isPinnedCallback = Callback<IAsyncOperationCompletedHandler<
            bool>>([taskbar, &event, &resultStatus, &hr](
                       IAsyncOperation<bool>* asyncInfo,
                       AsyncStatus status) mutable -> HRESULT {
          auto CompletedOperations =
              [&event,
               &resultStatus](Win11PinToTaskBarResultStatus status) -> HRESULT {
            resultStatus = status;
            event.Set();
            return S_OK;
          };

          bool asyncOpSucceeded = status == AsyncStatus::Completed;
          if (!asyncOpSucceeded) {
            TASKBAR_PINNING_LOG(
                LogLevel::Debug,
                "Taskbar: is pinned operation failed to complete.");
            return CompletedOperations(Win11PinToTaskBarResultStatus::Failed);
          }

          unsigned char isCurrentAppPinned = false;
          hr = asyncInfo->GetResults(&isCurrentAppPinned);
          if (FAILED(hr)) {
            TASKBAR_PINNING_LOG(
                LogLevel::Debug,
                "Taskbar: is current app pinned check failed. HRESULT = 0x%lx",
                hr);
            return CompletedOperations(Win11PinToTaskBarResultStatus::Failed);
          }

          if (isCurrentAppPinned) {
            TASKBAR_PINNING_LOG(LogLevel::Debug,
                                "Taskbar: current app is already pinned.");
            return CompletedOperations(
                Win11PinToTaskBarResultStatus::AlreadyPinned);
          }
          return CompletedOperations(Win11PinToTaskBarResultStatus::NotPinned);
        });

        HRESULT isPinnedOperationHR =
            isPinnedOperation->put_Completed(isPinnedCallback.Get());
        if (FAILED(isPinnedOperationHR)) {
          hr = isPinnedOperationHR;
          TASKBAR_PINNING_LOG(
              LogLevel::Debug,
              "Taskbar: is pinned operation failed when setting completion "
              "callback. HRESULT = 0x%lx",
              hr);
          return CompletedOperations(Win11PinToTaskBarResultStatus::Failed);
        }

        // DO NOT SET event HERE. It will be set in the is pin operation
        // callback As in, operations are not completed, so don't call
        // CompletedOperations
      }));

  // block until the pinning check is completed on the main thread
  event.Wait();

  return {hr, resultStatus};
}

#else  // MINGW32 implementation below

Win11PinToTaskBarResult PinCurrentAppToTaskbarWin11(
    bool aCheckOnly, const nsAString& aAppUserModelId,
    const bool aFireAndForget) {
  return {S_OK, Win11PinToTaskBarResultStatus::NotSupported};
}

Win11PinToTaskBarResult IsCurrentAppPinnedToTaskbarWin11(bool aCheckOnly) {
  return {S_OK, Win11PinToTaskBarResultStatus::NotSupported};
}

#endif  // #ifndef __MINGW32__  // WinRT headers not yet supported by MinGW
