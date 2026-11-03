# Pin to Taskbar

## Implementations

Windows Taskbar pinning is split between two implementations: an undocumented COM API for unpackaged installs on Windows 10 RS5 through early versions of Windows 11, and an official WinRT API for both packaged and unpackaged installs on later versions of Windows 11.

There was a period of overlap on Windows 11 where both the COM and WinRT APIs were available in unpackaged apps, though the exact versions have not been nailed down given the undocumented nature of the COM API in concert with Windows shipping parallel updated builds within a single version.

We are unable to detect if the COM API will prompt for user confirmation or be outright rejected by the system. Because the user may never respond to the confirmation prompt, the app remaining unpinned after the COM API is called does not necessarily indicate that the pinning request failed. For this reason, our implementation prefers the WinRT API when pinning Firefox to the Taskbar, and falls back to COM if WinRT fails.

Because the COM API can pin without asking the user, callers can request prompt-only pinning. In that mode, a WinRT failure rejects instead of falling back to COM.

:::{warning}
When testing Taskbar pinning on systems with an OS confirmation prompt, it is important to affirm the prompt. Otherwise, the system might block further attempts to pin to the Taskbar until it is restarted.
:::

### COM

Taskbar pinning first became available via the undocumented `IPinnedList3` COM API implemented by the Taskband Pin COM class. This API has minimal requirements: just a shortcut to the application anywhere on the file system. It pins without asking the user on Windows 10 and early Windows 11 builds. On Windows 11 24H2 and later it reports success without pinning.

### WinRT

The WinRT API has the constraint that the user must confirm pinning to the Taskbar via a system-issued Toast notification. Additionally, the application must have window focus for the notification to show, which is a complication for unpackaged apps due to slow population of `shell:appsfolder` necessary for the requirements outlined in the following section.

#### NSIS and MSIX

Unpackaged and packaged installs (NSIS and MSIX respectively) differ in their constraints and affordances for pinning to the Taskbar.

Notably unpackaged installs require the application is present in the virtual `shell:appsfolder` folder. `shell:appsfolder` is composed of shortcuts with a unique AUMID from the Common and User Start Menu folders. The current process AUMID - which we can set at runtime - must match one of these shortcuts when pinning. This allows us to pin Firefox to the Taskbar with different icons, names, and shortcut arguments. We leverage this in our {doc}`web apps implementation </browser/components/taskbartabs/docs/index>`.

Packaged installs don't require manual shortcut management as populating `shell:appsfolder` is handled by the packaging environment. The packaging environment also virtualizes the `AppData` directories and process AUMID. For these reasons we have to rely on other mechanisms (e.g. Secondary Tile Pinning) for alternative representations of Firefox on the Taskbar for packaged installs.

## Conflicts with set default

Ordering must be carefully maintained when asking the user to both [set Firefox as the default browser](set-default.md) and pin to the Taskbar. In updated versions of Windows 11, setting the default browser necessitates opening Windows Settings. This is in conflict with pinning to the Taskbar, which requires that the current process has window focus. To ensure these don't conflict: pin to the Taskbar should block setting the default browser, and pinning should be run in such a way that it is not blocked on the user confirming the prompt.
