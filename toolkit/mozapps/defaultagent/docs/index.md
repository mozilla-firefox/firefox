# Default Browser Agent

The Default Browser Agent is a Windows-only scheduled task which runs in the background to collect and submit data about the browser that the user has set as their OS default (that is, the browser that will be invoked by the operating system to open web links that the user clicks on in other programs). Its purpose is to help Mozilla understand user's default browser choices and, in the future, to engage with users at a time when they may not be actively running Firefox.

For information about the specific data that the agent sends, see the [default-agent ping](https://dictionary.telemetry.mozilla.org/apps/firefox_desktop_background_defaultagent/pings/default-agent).

## Scheduled Task

The agent itself is the `defaultagent` background task (`firefox --backgroundtask defaultagent do-task`). How that background task gets scheduled depends on how Firefox was installed:

- Installs made with the Windows (NSIS) installer use a classic Windows Task Scheduler entry, described in this section.
- Packaged (MSIX) installs use a WinRT background task, described under "MSIX Installs" below.

### NSIS/MSI Installs

The agent runs as a [Windows scheduled task](https://docs.microsoft.com/en-us/windows/win32/taskschd/about-the-task-scheduler). The scheduled task proxy executable invokes the Firefox `BackgroundTask_defaultagent` which executes all of the agent's primary functions; all of its other functions relate to managing the task. The Windows installer is responsible for creating (and the uninstaller for removing) the agent's task entry, but the code for actually doing this resides in the agent itself, and the installers simply call it using dedicated command line parameters (`register-task` and `uninstall`). The {doc}`PostUpdate </browser/installer/windows/installer/Helper>` code also calls the agent to update any properties of an existing task registration that need to be updated, or to create one during an application update if none exists.

The tasks are normal entries in the Windows Task Scheduler, managed using [its Win32 API](https://docs.microsoft.com/en-us/windows/win32/api/_taskschd/). They're created in a tasks folder called "Mozilla" (or whatever the application's vendor name is), and there's one for each installation of Firefox (or other Mozilla application). The task is set to run automatically every 24 hours starting at the time it's registered (with the first run being 24 hours after that), or the nearest time after that the computer is awake. The task is configured with one action, which is to run the agent binary with the command line parameter `do-task`, the command that invokes the actual agent functionality.

The default browser agent needs to run as some OS-level user, as opposed to, say, `LOCAL SERVICE`, in order to read the user's default browser setting. Therefore, the default browser agent runs as the user that ran the Firefox installer (although always without elevation, whether the installer had it or not).

### MSIX Installs

MSIX packages have no install-time or uninstall-time hook that could run `register-task` or `uninstall`, and Task Scheduler entries created through the Win32 API live outside the package, so Windows would not remove them when the package is uninstalled. Packaged installs instead register a [WinRT background task](https://learn.microsoft.com/en-us/windows/apps/windows-app-sdk/migrate-to-windows-app-sdk/guides/background-task-migration-strategy). The package owns that registration, so Windows removes it on uninstall, and users can see and turn off background activity in Windows Settings.

#### Registration

`DefaultAgentScheduler.sys.mjs` runs at browser idle startup. Because registration happens from Firefox itself, no task exists until Firefox has been launched at least once after install.

It registers, or deletes, the task through `TaskScheduler.sys.mjs`. On packaged installs, that module uses `TaskSchedulerWinMSIXImpl.sys.mjs`, which in turn calls the Rust `nsIWinBackgroundTaskRegistrar` component (`toolkit/components/taskscheduler/rust`). That component uses `BackgroundTaskBuilder` to register a recurring `TimeTrigger` task:

- The task name is `defaultagent:do-task`.
- The interval is 24 hours.
- The entry point is the activatable class ID `MOZ_BACKGROUNDTASK_ACTIVATABLE_CLASS_ID`.

`DefaultAgentScheduler` keeps the registration in sync with the agent's enabled state:

- If the agent is disabled by policy or by the `default-browser-agent.enabled` pref, it deletes the task. It also watches that pref, so flipping it registers or deletes the task right away.
- Otherwise, it only re-registers when the task is missing, or when the task name, interval or entry point recorded in the `default-browser-agent.msix.registered-params` pref differ from the current ones. This keeps ordinary restarts from resetting the task's schedule.
- If a previous version registered the task under a different name, the old registration is unregistered explicitly. Windows keeps a registration until it is explicitly unregistered, so otherwise both would keep firing.

#### Activation

When the trigger fires, Windows loads `msixcomserver.dll` into `backgroundtaskhost.exe` and activates the background task class:

1. `msixcomserver.dll` is a WinRT in-process server. Its logic, including the exported `DllGetActivationFactory` and `DllCanUnloadNow`, lives in the Rust static library `toolkit/mozapps/msixcomserver-rust`.
2. The DLL's `IBackgroundTask::Run` reads the task name and turns its colon-separated segments into a command line. `defaultagent:do-task` becomes `--backgroundtask defaultagent do-task`. The task name is the only data a WinRT background task registration can carry, so the name encodes the command.
3. `backgroundtaskhost.exe` runs in an AppContainer at low integrity, which does not have the access the agent needs, such as writing to the registry. So the DLL does not start Firefox directly. Instead it calls `FullTrustProcessLauncher::LaunchFullTrustProcessForCurrentAppWithArgumentsAsync`, which asks the shell to start the package's full trust process, Firefox, with those arguments. This requires the `windows.fullTrustProcess` extension in the manifest.

From there, `BackgroundTask_defaultagent.sys.mjs` runs `do-task` exactly as it does for non-MSIX installs. The same COM server and activatable class can serve other background tasks: each one is a separate registration whose name encodes its own command line.

### Remote Disablement

The default browser agent can be remotely disabled and (re-)enabled. Each time the scheduled task runs it queries [Firefox Remote Settings](https://remote-settings.readthedocs.io/en/latest/) to determine if the agent has been remotely disabled or (re-)enabled.

If the default browser agent is disabled by policy, remote disablement will not be checked. However, the notification functionality of the agent is distinct from the telemetry functionality of the agent, and remote disablement must apply to both functions. Therefore, even if the user has opted out of sending telemetry (by policy or by preference), the agent must check for remote disablement. For a user who is currently opted out of telemetry, they will not be opted in due to the default browser agent being remotely (re-)enabled.

## Data Management

The default browser agent has to be able to work with settings at several different levels: a Firefox profile, an OS user, a Firefox installation, and the entire system. This need creates an information architecture mismatch between all of those things, mostly because no Firefox profile is available to the agent while it's running; it's not really feasible to either directly use or to clone Firefox's profile selection functionality, and even if we could select a profile, whatever code we might use to actually work with it would have the same problems. So, in order to allow for controlling the agent from Firefox, certain settings are mirrored from Firefox to a location where the agent can read them. Since the agent operates in the context only of an OS-level user, that means that in this situation a single OS-level user who uses multiple Firefox profiles may be able to observe the agent's settings changing as the different profiles race to be the active mirror, without them knowingly taking any action.

### Pref Reflection

The agent needs to be able to read (but not set) values that have their canonical representation in the form of Firefox prefs. This means those pref values have to be copied out to a place where the agent can read them. The Windows registry was chosen as that place; it's easier to use than a file, and we already have keys there which are reserved by Firefox. Specifically, the subkey used for these prefs is `HKEY_CURRENT_USER\Software\[app vendor name]\[app name]\Default Browser Agent\`. During Firefox startup, the values of the prefs that control the agent are reflected to this key, and those values are updated whenever the prefs change after that.

The list of reflected prefs includes the global telemetry opt-out pref `datareporting.healthreport.uploadEnabled` and a pref called `default-browser-agent.enabled`, which can enable or disable the entire agent. The agent checks these registry-reflected pref values when its scheduled task runs, they do not actually prevent the scheduled task from running.

Enterprise policies also exist to perform the same functions as these prefs. These work the same way as all other Firefox policies and [the documentation for those](https://mozilla.github.io/policy-templates/) explains how to use them.

In addition, the following Firefox Remote Settings pref is reflected: `services.settings.server`. It is the service endpoint to consult for remote-disablement.

### Default Browser Setting

The agent is responsible for reporting both the user's current default browser and their previous default browser. Nothing in the operating system records past associations, so the agent must do this for itself. First, it gets the current default browser by calling [IApplicationAssociationRegistration::QueryCurrentDefault](https://docs.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-iapplicationassociationregistration-querycurrentdefault) for the `http` protocol. It then checks that against a value stored in its own registry key and, if those are different, it knows that the default browser has changed, and records the new and old defaults.
