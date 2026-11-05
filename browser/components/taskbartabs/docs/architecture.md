# Firefox Web Apps Architecture
```{mermaid}
graph TD;
  TaskbarTabs --> TaskbarTabsPin
  TaskbarTabs --> TaskbarTabsWindowManager
  TaskbarTabs --> TaskbarTabsRegistry

  TaskbarTabsWindowManager -.-> browser

  browser([browser init code])
  browser -- for webapp windows --> TaskbarTabsChrome
  browser -- for all windows --> TaskbarTabsPageAction
  TaskbarTabsPageAction --> TaskbarTabs

  cmdline([command line handling])
  cmdline --> TaskbarTabsCmd --> TaskbarTabs
```

The main entrypoint for everything around web apps is {searchfox}`TaskbarTabs.sys.mjs <browser/components/taskbartabs/TaskbarTabs.sys.mjs>`. Behind the scenes, there's a few more modules that do most of the work.

Some rules of thumb for where things might be implemented:

* User interface code should be in TaskbarTabsChrome (for the web app window itself) or TaskbarTabsPageAction (for the page action).
* Code for integrating with the system UI should be in TaskbarTabsPin.
* Code to do with the web app window 'under the hood' should be in TaskbarTabsWindowManager (or browser-init.js, if it's important it runs very early in the window's lifecycle).
* Code for storing data about web apps should be in TaskbarTabsRegistry.
* Code for integrating these modules, or others, should be in TaskbarTabs.sys.mjs.
* Utilities needed in a few different places should be in TaskbarTabsUtils.

## TaskbarTabs.sys.mjs

This file ties the rest of the Web App infrastructure together, providing functions that generally wrap an internal TaskbarTabsRegistry instance with additional features. In particular, it loads the primary `taskbartabs.json` file and is responsible for selecting the icon to use for each web app, as well as interpreting any Web App Manifest.

When a new Web App is created, the icon to use for it is determined by iterating through sources (manifests, favicons) until a valid icon is found. An existing Web App saves its icon on the filesystem and loads it when needed.

## TaskbarTabsChrome

TaskbarTabsChrome is responsible for creating the UI visible within a web app. This includes the icon in the corner, the audio controls, and other styling for the window.

## TaskbarTabsCmd

TaskbarTabsCmd allows Web Apps to start from the command line. This is used so clicking on a shortcut on the taskbar opens the Web App.

Usually, a command to start a Web App looks like this:

```
firefox -taskbar-tab <id> -new-window <start url> -profile <profile path> -container <usercontextid>
```

The `-taskbar-tab` argument is interpreted in this module, and causes a webapp window to start instead of a standard browser window.

If the web app doesn't exist, a new one will be created, although not with that ID ([bug 1985658](https://bugzilla.mozilla.org/show_bug.cgi?id=1985658)). If the web app exists, its start URL will be used; otherwise, the start URL from the shortcut will open instead, as a fallback.

## TaskbarTabsPageAction

The 'page action' is the button on the URL bar that serves as the entrypoint to Web Apps. This module is responsible for showing and hiding that button depending on whether the page supports Web Apps (i.e. whether it has an HTTP or HTTPS scheme), as well as calling `TaskbarTabs.moveTabIntoTaskbar` or `TaskbarTabs.ejectWindow` when it's clicked on.

## TaskbarTabsPin

Generally, TaskbarTabsPin is responsible for integrating the newly-created web app with the surrounding desktop environment, like the taskbar and Applications menu. This involves saving and deleting the icon from disk (although the icon itself is selected in TaskbarTabs.sys.mjs), and some platform-dependent behaviour:

* On Windows non-MSIX, this entails creating a shortcut and requesting that the user pin it using the ShellService.
* On Windows MSIX, this entails creating a {searchfox}`secondary tile <browser/components/shell/rust/shell_windows/src/secondary_tiles.rs`, since the folder we'd put the shortcuts in isn't accessible otherwise.
* On Linux non-Flatpak, this entails creating a [desktop entry](https://specifications.freedesktop.org/desktop-entry/latest/) with `ShellService.createLinuxDesktopEntry`. These entries will be in `~/.local/share/applications/org.mozilla.firefox.webapp-<id>.desktop` by default, assuming `$XDG_DATA_HOME` is unset.
* On Linux Flatpak, this still uses `createLinuxDesktopEntry`, but it uses the [DynamicLauncher portal](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.DynamicLauncher) to create the entry instead of writing it to disk itself. This avoids poking a hole in the Flatpak sandbox, since the location that we'd need to put them in otherwise isn't accessible.

Linux under Snap is currently not supported.

The platform-dependent parts live in subclasses of {searchfox}`TaskbarTabsPinBase <browser/components/taskbartabs/TaskbarTabsPinBase.sys.mjs>`: `TaskbarTabsPinWin`, `TaskbarTabsPinMsix` and `TaskbarTabsPinLinux`. Each one creates its shortcut however its platform needs, and only overrides the pinning and unpinning steps if they're separate from creating and deleting the shortcut (currently only on Windows non-MSIX). On platforms without support, TaskbarTabsPinBase is used as-is and pinning fails. TaskbarTabsPin saves the icon, builds the command line arguments, picks the right subclass at runtime and records telemetry, so callers don't need to care about the platform.

### Keep in mind!
* On Windows prior to Windows 11, we usually can pin to the taskbar without prompting the user. However, we want to make sure we have the user's permission first (e.g. by clicking on the page action).
* On Linux under Flatpak, you need the user's permission to update the desktop entry.

## TaskbarTabsRegistry

TaskbarTabsRegistry stores the metadata associated with each web app.

Part of the design of Web Apps is that the registry shouldn't make policy decisions or kick off other logic. Instead of doing something when `TaskbarTabsRegistry.findOrCreateTaskbarTab` is called, do it when `TaskbarTabs.#findOrCreateTaskbarTab` (or its callers) is called.

The registry should also be private to the TaskbarTabs instance and shouldn't be passed around elsewhere. If some other logic needs to change the registry, make it go through TaskbarTabs.sys.mjs.

## TaskbarTabsUtils

This file has utilities for general tasks around Web Apps, like getting paths or loading images. Put things here if they aren't very specific to a specific module, and especially if they're used from multiple (or also used from tests).

When loading an image, there are several functions available depending on how much you trust the image that you're loading. Refer to the JSDocs for information on choosing which ones to use.

## TaskbarTabsWindowManager

This file is responsible for creating the new window for each Web App, 'ejecting' web apps back into the tabs they came from, and capturing related telemetry.

When a Taskbar Tabs window is created, there's a few places you can hook in:

* Before the window is created, in `openWindow` and `replaceTabWithWindow`. However, you don't have a window yet, so you might not be able to do very much.
* In `#openWindow`, after the window is created. This time, the window may have finished loading before you're called; for something like telemetry that's great, but some attributes (especially on Linux) need to be set before load.
* In `browser-init.js`, before the window loads. Don't do anything complicated here, since that could hurt performance, but this is probably the best place to set those attributes.

  Note that this file is synchronous, and you don't have access to the `TaskbarTab` object. If you need to do something asynchronous, or need to access other `TaskbarTab` fields, do that logic in `openWindow` etc., then put the output into the `extraOptions` property bag.
