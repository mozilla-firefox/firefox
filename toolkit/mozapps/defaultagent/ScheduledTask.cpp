/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ScheduledTask.h"
#include "ScheduledTaskRemove.h"

#include <string>
#include <time.h>

#include <comutil.h>
#include <taskschd.h>

#include "EventLog.h"
#include "mozilla/RefPtr.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/WinHeaderOnlyUtils.h"

#include "DefaultBrowser.h"

#include "mozilla/ErrorResult.h"
#include "mozilla/intl/Localization.h"
#include "nsString.h"
#include "nsTArray.h"
using mozilla::intl::Localization;

namespace mozilla::default_agent {

// The task scheduler requires its time values to come in the form of a string
// in the format YYYY-MM-DDTHH:MM:SSZ. This format string is used to get that
// out of the C library wcsftime function.
const wchar_t* kTimeFormat = L"%Y-%m-%dT%H:%M:%SZ";
// The expanded time string should always be this length, for example:
// 2020-02-12T16:59:32Z
const size_t kTimeStrMaxLen = 20;

#define ENSURE(x)         \
  if (FAILED(hr = (x))) { \
    LOG_ERROR(hr);        \
    return hr;            \
  }

bool GetTaskDescription(mozilla::UniquePtr<wchar_t[]>& description) {
  mozilla::UniquePtr<wchar_t[]> installPath;
  bool success = GetInstallDirectory(installPath);
  if (!success) {
    LOG_ERROR_MESSAGE(L"Failed to get install directory");
    return false;
  }
  nsTArray<nsCString> resIds = {"branding/brand.ftl"_ns,
                                "browser/backgroundtasks/defaultagent.ftl"_ns};
  RefPtr<Localization> l10n = Localization::Create(resIds, true);
  nsAutoCString daTaskDesc;
  mozilla::ErrorResult rv;
  l10n->FormatValueSync("default-browser-agent-task-description"_ns, daTaskDesc,
                        rv);
  if (rv.Failed()) {
    LOG_ERROR_MESSAGE(L"Failed to read task description");
    return false;
  }
  NS_ConvertUTF8toUTF16 daTaskDescW(daTaskDesc);
  description = std::make_unique<wchar_t[]>(daTaskDescW.Length() + 1);
  wcsncpy(description.get(), daTaskDescW.get(), daTaskDescW.Length() + 1);
  return true;
}

HRESULT RegisterTask(const wchar_t* uniqueToken,
                     BSTR startTime /* = nullptr */) {
  // Do data migration during the task installation. This might seem like it
  // belongs in UpdateTask, but we want to be able to call
  //    RemoveTasks();
  //    RegisterTask();
  // and still have data migration happen. Also, UpdateTask calls this function,
  // so migration will still get run in that case.
  MaybeMigrateCurrentDefault();

  // Make sure we don't try to register a task that already exists.
  RemoveTasks(uniqueToken, WhichTasks::WdbaTaskOnly);

  // If we create a folder and then fail to create the task, we need to
  // remember to delete the folder so that whatever set of permissions it ends
  // up with doesn't interfere with trying to create the task again later, and
  // so that we don't just leave an empty folder behind.
  bool createdFolder = false;

  HRESULT hr = S_OK;
  RefPtr<ITaskService> scheduler;
  ENSURE(CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER,
                          IID_ITaskService, getter_AddRefs(scheduler)));

  ENSURE(scheduler->Connect(VARIANT{}, VARIANT{}, VARIANT{}, VARIANT{}));

  RefPtr<ITaskFolder> rootFolder;
  BStrPtr rootFolderBStr = BStrPtr(SysAllocString(L"\\"));
  ENSURE(
      scheduler->GetFolder(rootFolderBStr.get(), getter_AddRefs(rootFolder)));

  RefPtr<ITaskFolder> taskFolder;
  BStrPtr vendorBStr = BStrPtr(SysAllocString(kTaskVendor));
  if (FAILED(rootFolder->GetFolder(vendorBStr.get(),
                                   getter_AddRefs(taskFolder)))) {
    hr = rootFolder->CreateFolder(vendorBStr.get(), VARIANT{},
                                  getter_AddRefs(taskFolder));

    if (SUCCEEDED(hr)) {
      createdFolder = true;
    } else if (hr == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS)) {
      // `CreateFolder` doesn't assign to the out pointer on
      // `ERROR_ALREADY_EXISTS`, so try to get the folder again. This behavior
      // is undocumented but was verified in a debugger.
      HRESULT priorHr = hr;
      hr = rootFolder->GetFolder(vendorBStr.get(), getter_AddRefs(taskFolder));

      if (FAILED(hr)) {
        LOG_ERROR(priorHr);
        LOG_ERROR(hr);
        return hr;
      }
    } else {
      LOG_ERROR(hr);
      return hr;
    }
  }

  auto cleanupFolder =
      mozilla::MakeScopeExit([&hr, createdFolder, &rootFolder, &vendorBStr] {
        if (createdFolder && FAILED(hr)) {
          // If this fails, we can't really handle that intelligently, so
          // don't even bother to check the return code.
          rootFolder->DeleteFolder(vendorBStr.get(), 0);
        }
      });

  RefPtr<ITaskDefinition> newTask;
  ENSURE(scheduler->NewTask(0, getter_AddRefs(newTask)));

  mozilla::UniquePtr<wchar_t[]> description;
  if (!GetTaskDescription(description)) {
    return E_FAIL;
  }
  BStrPtr descriptionBstr = BStrPtr(SysAllocString(description.get()));

  RefPtr<IRegistrationInfo> taskRegistration;
  ENSURE(newTask->get_RegistrationInfo(getter_AddRefs(taskRegistration)));
  ENSURE(taskRegistration->put_Description(descriptionBstr.get()));

  RefPtr<ITaskSettings> taskSettings;
  ENSURE(newTask->get_Settings(getter_AddRefs(taskSettings)));
  ENSURE(taskSettings->put_DisallowStartIfOnBatteries(VARIANT_FALSE));
  ENSURE(taskSettings->put_MultipleInstances(TASK_INSTANCES_IGNORE_NEW));
  ENSURE(taskSettings->put_StartWhenAvailable(VARIANT_TRUE));
  ENSURE(taskSettings->put_StopIfGoingOnBatteries(VARIANT_FALSE));
  // This cryptic string means "12 hours 5 minutes". So, if the task runs for
  // longer than that, the process will be killed, because that should never
  // happen. See
  // https://docs.microsoft.com/en-us/windows/win32/taskschd/tasksettings-executiontimelimit
  // for a detailed explanation of these strings.
  BStrPtr execTimeLimitBStr = BStrPtr(SysAllocString(L"PT12H5M"));
  ENSURE(taskSettings->put_ExecutionTimeLimit(execTimeLimitBStr.get()));

  RefPtr<IRegistrationInfo> regInfo;
  ENSURE(newTask->get_RegistrationInfo(getter_AddRefs(regInfo)));

  ENSURE(regInfo->put_Author(vendorBStr.get()));

  RefPtr<ITriggerCollection> triggers;
  ENSURE(newTask->get_Triggers(getter_AddRefs(triggers)));

  RefPtr<ITrigger> newTrigger;
  ENSURE(triggers->Create(TASK_TRIGGER_DAILY, getter_AddRefs(newTrigger)));

  RefPtr<IDailyTrigger> dailyTrigger;
  ENSURE(newTrigger->QueryInterface(IID_IDailyTrigger,
                                    getter_AddRefs(dailyTrigger)));

  if (startTime) {
    ENSURE(dailyTrigger->put_StartBoundary(startTime));
  } else {
    // The time that the task is scheduled to run at every day is taken from the
    // time in the trigger's StartBoundary property. We'll set this to the
    // current time, on the theory that the time at which we're being installed
    // is a time that the computer is likely to be on other days. If our
    // theory is wrong and the computer is offline at the scheduled time, then
    // because we've set StartWhenAvailable above, the task will run whenever
    // it wakes up. Since our task is entirely in the background and doesn't use
    // a lot of resources, we're not concerned about it bothering the user if it
    // runs while they're actively using this computer.
    time_t now_t = time(nullptr);
    // Subtract a minute from the current time, to avoid "winning" a potential
    // race with the scheduler that might have it start the task immediately
    // after we register it, if we finish doing that and then it evaluates the
    // trigger during the same second. We haven't seen this happen in practice,
    // but there's no documented guarantee that it won't, so let's be sure.
    now_t -= 60;

    tm now_tm;
    errno_t errno_rv = gmtime_s(&now_tm, &now_t);
    if (errno_rv != 0) {
      // The C runtime has a (private) function to convert Win32 error codes to
      // errno values, but there's nothing that goes the other way, and it
      // isn't worth including one here for something that's this unlikely to
      // fail anyway. So just return a generic error.
      hr = HRESULT_FROM_WIN32(ERROR_INVALID_TIME);
      LOG_ERROR(hr);
      return hr;
    }

    auto timeStr = std::make_unique<wchar_t[]>(kTimeStrMaxLen + 1);

    if (wcsftime(timeStr.get(), kTimeStrMaxLen + 1, kTimeFormat, &now_tm) ==
        0) {
      hr = E_NOT_SUFFICIENT_BUFFER;
      LOG_ERROR(hr);
      return hr;
    }

    BStrPtr startTimeBStr = BStrPtr(SysAllocString(timeStr.get()));
    ENSURE(dailyTrigger->put_StartBoundary(startTimeBStr.get()));
  }

  ENSURE(dailyTrigger->put_DaysInterval(1));

  RefPtr<IActionCollection> actions;
  ENSURE(newTask->get_Actions(getter_AddRefs(actions)));

  RefPtr<IAction> action;
  ENSURE(actions->Create(TASK_ACTION_EXEC, getter_AddRefs(action)));

  RefPtr<IExecAction> execAction;
  ENSURE(action->QueryInterface(IID_IExecAction, getter_AddRefs(execAction)));

  // Register proxy instead of Firefox background task.
  mozilla::UniquePtr<wchar_t[]> installPath = mozilla::GetFullBinaryPath();
  if (!PathRemoveFileSpecW(installPath.get())) {
    return E_FAIL;
  }
  std::wstring proxyPath(installPath.get());
  proxyPath += L"\\default-browser-agent.exe";

  BStrPtr binaryPathBStr = BStrPtr(SysAllocString(proxyPath.c_str()));
  ENSURE(execAction->put_Path(binaryPathBStr.get()));

  std::wstring taskArgs = L"do-task \"";
  taskArgs += uniqueToken;
  taskArgs += L"\"";
  BStrPtr argsBStr = BStrPtr(SysAllocString(taskArgs.c_str()));
  ENSURE(execAction->put_Arguments(argsBStr.get()));

  std::wstring taskName(kTaskName);
  taskName += uniqueToken;
  BStrPtr taskNameBStr = BStrPtr(SysAllocString(taskName.c_str()));

  RefPtr<IRegisteredTask> registeredTask;
  ENSURE(taskFolder->RegisterTaskDefinition(
      taskNameBStr.get(), newTask, TASK_CREATE_OR_UPDATE, VARIANT{}, VARIANT{},
      TASK_LOGON_INTERACTIVE_TOKEN, VARIANT{}, getter_AddRefs(registeredTask)));

  return hr;
}

HRESULT UpdateTask(const wchar_t* uniqueToken) {
  RefPtr<ITaskService> scheduler;
  HRESULT hr = S_OK;
  ENSURE(CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER,
                          IID_ITaskService, getter_AddRefs(scheduler)));

  ENSURE(scheduler->Connect(VARIANT{}, VARIANT{}, VARIANT{}, VARIANT{}));

  RefPtr<ITaskFolder> taskFolder;
  BStrPtr folderBStr = BStrPtr(SysAllocString(kTaskVendor));

  if (FAILED(
          scheduler->GetFolder(folderBStr.get(), getter_AddRefs(taskFolder)))) {
    // If our folder doesn't exist, create it and the task.
    return RegisterTask(uniqueToken);
  }

  std::wstring taskName(kTaskName);
  taskName += uniqueToken;
  BStrPtr taskNameBStr = BStrPtr(SysAllocString(taskName.c_str()));

  RefPtr<IRegisteredTask> task;
  if (FAILED(taskFolder->GetTask(taskNameBStr.get(), getter_AddRefs(task)))) {
    // If our task doesn't exist at all, just create one.
    return RegisterTask(uniqueToken);
  }

  // If we have a task registered already, we need to recreate it because
  // something might have changed that we need to update. But we don't
  // want to restart the schedule from now, because that might mean the
  // task never runs at all for e.g. Nightly. So create a new task, but
  // first get and preserve the existing trigger.
  RefPtr<ITaskDefinition> definition;
  if (FAILED(task->get_Definition(getter_AddRefs(definition)))) {
    // This task is broken, make a new one.
    return RegisterTask(uniqueToken);
  }

  RefPtr<ITriggerCollection> triggerList;
  if (FAILED(definition->get_Triggers(getter_AddRefs(triggerList)))) {
    // This task is broken, make a new one.
    return RegisterTask(uniqueToken);
  }

  RefPtr<ITrigger> trigger;
  if (FAILED(triggerList->get_Item(1, getter_AddRefs(trigger)))) {
    // This task is broken, make a new one.
    return RegisterTask(uniqueToken);
  }

  BSTR startTimeBstr;
  if (FAILED(trigger->get_StartBoundary(&startTimeBstr))) {
    // This task is broken, make a new one.
    return RegisterTask(uniqueToken);
  }
  BStrPtr startTime(startTimeBstr);

  return RegisterTask(uniqueToken, startTime.get());
}

}  // namespace mozilla::default_agent
