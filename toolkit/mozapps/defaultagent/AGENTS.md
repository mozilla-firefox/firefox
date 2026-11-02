# Windows Default Browser Agent Development

The Windows Default Browser Agent (or WDBA) is responsible for regaining default browser status when Firefox loses it and providing telemetry for browser defaults without requiring the browser to open in a privacy preserving manner. WDBA runs once every 24 hours as a background task. It reports the user's default browser and, if Firefox has lost default status, can show a toast asking the user to switch back.

## Documentation
Source documentation: https://firefox-source-docs.mozilla.org/toolkit/mozapps/defaultagent/default-browser-agent/index.html
Telemetry: https://dictionary.telemetry.mozilla.org/apps/firefox_desktop_background_defaultagent/pings/default-agent

## File Structure
- `BackgroundTask_defaultagent.sys.mjs` is the actual background task that Firefox runs.
- Scheduling, which depends on install type:
  1. NSIS: the installer calls `proxy/` (`default-browser-agent.exe`) with `register-task`/`uninstall`, which opens Firefox in a background task that uses `ScheduledTask.cpp` (Win32 Task Scheduler).
  2. MSIX: `DefaultAgentScheduler.sys.mjs` registers at idle startup via `toolkit/components/taskscheduler/TaskScheduler.sys.mjs`, then `toolkit/components/taskscheduler/TaskSchedulerWinMSIXImpl.sys.mjs` and `toolkit/components/taskscheduler/rust`. The task is run by `toolkit/mozapps/msixcomserver-rust`.
- Manifests: `browser/installer/windows/msix/AppxManifest.xml.in`, `browser/installer/package-manifest.in`.
- All of the rest of the files support and provide utils to execute the functionality of the default browser agent.

## Directory Rules
- Always use Microsoft documentation to reason about any new changes around the Windows APIs.
- Changes to scheduling or task lifecycle must handle both NSIS and MSIX.
- Disabling by policy or pref must remove the task, and uninstall must leave nothing behind.
- When suggesting changes, provide a way to test the change on Windows.
- Windows background-task failures are often silent: add logging and confirm the task actually ran.
- Double check areas such as user permissions, Windows manifest requirements, and Firefox manifest requirements.
