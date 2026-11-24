# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

### These strings are used inside the about:debugging UI.

about-debugging-sidebar =
  .heading = Debugging

# Page Title strings

# Page title (ie tab title) for the Setup page
about-debugging-page-title-setup-page = Debugging - Setup

# Page title (ie tab title) for the Runtime page
# Variables:
#   $selectedRuntimeId - ID of the current runtime, such as "this-firefox", "localhost:6080", etc.
about-debugging-page-title-runtime-page = Debugging - Runtime / { $selectedRuntimeId }

# Sidebar strings

# Display name of the runtime for the currently running instance of Firefox. Used in the
# Sidebar and in the Setup page.
about-debugging-this-firefox-runtime-name = This { -brand-shorter-name }

# Sidebar heading and title for selecting the currently running instance of Firefox
about-debugging-sidebar-this-firefox2 = This { -brand-shorter-name }
about-debugging-sidebar-this-firefox-title =
  .title = This { -brand-shorter-name }

# Sidebar heading and title for connecting to some remote source
about-debugging-sidebar-setup2 = Setup
about-debugging-sidebar-setup-title =
    .title = Setup

# Text displayed in the about:debugging sidebar when USB devices discovery is enabled.
about-debugging-sidebar-usb-enabled = USB enabled

# Text displayed in the about:debugging sidebar when USB devices discovery is disabled
# (for instance because the mandatory ADB extension is not installed).
about-debugging-sidebar-usb-disabled = USB disabled

# Connection status (connected) for runtime items in the sidebar
aboutdebugging-sidebar-runtime-connection-status-connected = Connected
# Connection status (disconnected) for runtime items in the sidebar
aboutdebugging-sidebar-runtime-connection-status-disconnected = Disconnected

# Text displayed in the about:debugging sidebar when no device was found.
about-debugging-sidebar-no-devices = No devices discovered

# Text displayed in buttons found in sidebar items representing remote runtimes.
# Clicking on the button will attempt to connect to the runtime.
about-debugging-sidebar-item-connect-button = Connect

# Text displayed in buttons found in sidebar items when the runtime is connecting.
about-debugging-sidebar-item-connect-button-connecting = Connecting…

# Text displayed in buttons found in sidebar items when the connection failed.
about-debugging-sidebar-item-connect-button-connection-failed = Connection failed

# Text displayed in connection warning on sidebar item of the runtime when connecting to
# the runtime is taking too much time.
about-debugging-sidebar-item-connect-button-connection-not-responding = Connection still pending, check for messages on the target browser

# Text displayed as connection error in sidebar item when the connection has timed out.
about-debugging-sidebar-item-connect-button-connection-timeout = Connection timed out

# Text displayed in sidebar items for remote devices where a compatible browser (eg
# Firefox) has not been detected yet. Typically, Android phones connected via USB with
# USB debugging enabled, but where Firefox is not started.
about-debugging-sidebar-runtime-item-waiting-for-browser = Waiting for browser…

# Text displayed in sidebar items for remote devices that have been disconnected from the
# computer.
about-debugging-sidebar-runtime-item-unplugged = Unplugged

# Title for runtime sidebar items that are related to a specific device (USB, WiFi).
# Variables:
#   $displayName (string) - Displayed name
#   $deviceName (string) - Name of the device
about-debugging-sidebar-runtime-item-name =
  .title = { $displayName } ({ $deviceName })
# Title for runtime sidebar items where we cannot get device information (network
# locations).
# Variables:
#   $displayName (string) - Displayed name
about-debugging-sidebar-runtime-item-name-no-device =
  .title = { $displayName }

# Text to show in the footer of the sidebar that links to a help page
# (currently: https://firefox-source-docs.mozilla.org/devtools-user/about_colon_debugging/)
about-debugging-sidebar-support = Debugging Support

# Text to show as the ALT attribute of a help icon that accompanies the help about
# debugging link in the footer of the sidebar
about-debugging-sidebar-support-icon =
  .alt = Help icon

# Text displayed in a sidebar button to refresh the list of USB devices. Clicking on it
# will attempt to update the list of devices displayed in the sidebar.
about-debugging-refresh-usb-devices-button = Refresh devices

# Setup Page strings

# Title of the Setup page.
about-debugging-setup-title = Setup

# Introduction text in the Setup page to explain how to configure remote debugging.
about-debugging-setup-intro = Configure the connection method you wish to remotely debug your device with.

# Explanatory text in the Setup page about what the 'This Firefox' page is for
about-debugging-setup-this-firefox2 = Use <a>{ about-debugging-this-firefox-runtime-name }</a> to debug extensions and service workers on this version of { -brand-shorter-name }.

# Title of the heading Connect section of the Setup page.
about-debugging-setup-connect-heading = Connect a Device

# USB section of the Setup page
about-debugging-setup-usb-title = USB

# Explanatory text displayed in the Setup page when USB debugging is disabled
about-debugging-setup-usb-disabled = Enabling this will download and add the required Android USB debugging components to { -brand-shorter-name }.

# Text of the button displayed in the USB section of the setup page when USB debugging is disabled.
# Clicking on it will download components needed to debug USB Devices remotely.
about-debugging-setup-usb-enable-button = Enable USB Devices

# Text of the button displayed in the USB section of the setup page when USB debugging is enabled.
about-debugging-setup-usb-disable-button = Disable USB Devices

# Text of the button displayed in the USB section of the setup page while USB debugging
# components are downloaded and installed.
about-debugging-setup-usb-updating-button = Updating…

# USB section of the Setup page (USB status)
about-debugging-setup-usb-status-enabled = Enabled
about-debugging-setup-usb-status-disabled = Disabled
about-debugging-setup-usb-status-updating = Updating…

# USB section step by step guide
about-debugging-setup-usb-step-enable-dev-menu2 = Enable Developer menu on your Android device.

# USB section step by step guide
about-debugging-setup-usb-step-enable-debug2 = Enable USB Debugging in the Android Developer Menu.

# USB section step by step guide
about-debugging-setup-usb-step-enable-file-transfer = Enable file transfer and ensure that your device is not in charging-only mode.

# USB section step by step guide
about-debugging-setup-usb-step-enable-debug-firefox2 = Enable USB Debugging in Firefox on the Android device.

# USB section step by step guide
about-debugging-setup-usb-step-plug-device = Connect the Android device to your computer.

# Text shown in the USB section of the setup page with a link to troubleshoot connection errors.
# The link goes to https://firefox-source-docs.mozilla.org/devtools-user/about_colon_debugging/index.html#connecting-to-a-remote-device
about-debugging-setup-usb-troubleshoot = Problems connecting to the USB device? <a>Troubleshoot</a>

# Network section of the Setup page
about-debugging-setup-network =
  .title = Network Location

# Text shown in the Network section of the setup page with a link to troubleshoot connection errors.
# The link goes to https://firefox-source-docs.mozilla.org/devtools-user/about_colon_debugging/index.html#connecting-over-the-network
about-debugging-setup-network-troubleshoot = Problems connecting via network location? <a>Troubleshoot</a>

# Text of a button displayed after the network locations "Host" input.
# Clicking on it will add the new network location to the list.
about-debugging-network-locations-add-button = Add

# Text of the label for the text input that allows users to add new network locations in
# the Connect page. A host is a hostname and a port separated by a colon, as suggested by
# the input's placeholder "localhost:6080".
about-debugging-network-locations-host-input-label = Host

# Text of a button displayed next to existing network locations in the Connect page.
# Clicking on it removes the network location from the list.
about-debugging-network-locations-remove-button = Remove

# Text used as error message if the format of the input value was invalid in the network locations form of the Setup page.
# Variables:
#   $host-value (string) - The input value submitted by the user in the network locations form
about-debugging-network-location-form-invalid = Invalid host “{ $host-value }”. The expected format is “hostname:portnumber”.

# Text used as error message if the input value was already registered in the network locations form of the Setup page.
# Variables:
#   $host-value (string) - The input value submitted by the user in the network locations form
about-debugging-network-location-form-duplicate = The host “{ $host-value }” is already registered

# Runtime Page strings

# Below are the titles for the various categories of debug targets that can be found
# on "runtime" pages of about:debugging.
# Title of the temporary extensions category (only available for "This Firefox" runtime).
# .name is processed by fluent-react / DebugTargetPane
about-debugging-runtime-temporary-extensions =
  .name = Temporary Extensions
# Title of the extensions category.
# .name is processed by fluent-react / DebugTargetPane
about-debugging-runtime-extensions =
  .name = Extensions
# Title of the tabs category.
# .name is processed by fluent-react / DebugTargetPane
about-debugging-runtime-tabs =
  .name = Tabs
# Title of the service workers category.
# .name is processed by fluent-react / DebugTargetPane
about-debugging-runtime-service-workers =
  .name = Service Workers
# Title of the shared workers category.
# .name is processed by fluent-react / DebugTargetPane
about-debugging-runtime-shared-workers =
  .name = Shared Workers
# Title of the other workers category.
# .name is processed by fluent-react / DebugTargetPane
about-debugging-runtime-other-workers =
  .name = Other Workers
# Title of the processes category.
# .name is processed by fluent-react / DebugTargetPane
about-debugging-runtime-processes =
  .name = Processes

# Label of the button opening the performance profiler panel in runtime pages for remote
# runtimes.
about-debugging-runtime-profile-button2 = Profile performance

# This string is displayed in the runtime page if the current configuration of the
# target runtime is incompatible with service workers. "Learn more" points to:
# https://firefox-source-docs.mozilla.org/devtools-user/about_colon_debugging/index.html#service-workers-not-compatible
about-debugging-runtime-service-workers-not-compatible = Your browser configuration is not compatible with Service Workers. <a>Learn more</a>

# This string is displayed in the runtime page if the remote browser version is too old.
# "Troubleshooting" link points to https://firefox-source-docs.mozilla.org/devtools-user/about_colon_debugging/
# { $runtimeVersion } is the version of the remote browser (for instance "67.0a1")
# { $minVersion } is the minimum version that is compatible with the current Firefox instance (same format)
about-debugging-browser-version-too-old = The connected browser has an old version ({ $runtimeVersion }). The minimum supported version is ({ $minVersion }). This is an unsupported setup and may cause DevTools to fail. Please update the connected browser. <a>Troubleshooting</a>

# Dedicated message for a backward compatibility issue that occurs when connecting:
# from Fx 70+ to the old Firefox for Android (aka Fennec) which uses Fx 68.
about-debugging-browser-version-too-old-fennec = This version of Firefox cannot debug Firefox for Android (68). We recommend installing Firefox for Android Nightly on your phone for testing. <a>More details</a>

# This string is displayed in the runtime page if the remote browser version is too recent.
# "Troubleshooting" link points to https://firefox-source-docs.mozilla.org/devtools-user/about_colon_debugging/
# { $runtimeID } is the build ID of the remote browser (for instance "20181231", format is yyyyMMdd)
# { $localID } is the build ID of the current Firefox instance (same format)
# { $runtimeVersion } is the version of the remote browser (for instance "67.0a1")
# { $localVersion } is the version of your current browser (same format)
about-debugging-browser-version-too-recent = The connected browser is more recent ({ $runtimeVersion }, buildID { $runtimeID }) than your { -brand-shorter-name } ({ $localVersion }, buildID { $localID }). This is an unsupported setup and may cause DevTools to fail. Please update Firefox. <a>Troubleshooting</a>

# Displayed for runtime info in runtime pages.
# { $name } is brand name such as "Firefox Nightly"
# { $version } is version such as "64.0a1"
about-debugging-runtime-name = { $name } ({ $version })

# Text of a button displayed in Runtime pages for remote runtimes.
# Clicking on the button will close the connection to the runtime.
about-debugging-runtime-disconnect-button = Disconnect

# Text of the connection prompt button displayed in Runtime pages, when the preference
# "devtools.debugger.prompt-connection" is false on the target runtime.
about-debugging-connection-prompt-enable-button = Enable connection prompt

# Text of the connection prompt button displayed in Runtime pages, when the preference
# "devtools.debugger.prompt-connection" is true on the target runtime.
about-debugging-connection-prompt-disable-button = Disable connection prompt

# Title of a modal dialog displayed on remote runtime pages after clicking on the Profile Runtime button.
about-debugging-profiler-dialog-title2 = Profiler

# Clicking on the header of a debug target category will expand or collapse the debug
# target items in the category. This text is used as ’title’ attribute of the header,
# to describe this feature.
about-debugging-collapse-expand-debug-targets = Collapse / expand

# Debug Targets strings

# Displayed in the categories of "runtime" pages that don't have any debug target to
# show. Debug targets depend on the category (extensions, tabs, workers...).
about-debugging-debug-target-list-empty = Nothing yet.

# Text of a button displayed next to debug targets of "runtime" pages. Clicking on this
# button will open a DevTools toolbox that will allow inspecting the target.
# A target can be an addon, a tab, a worker...
about-debugging-debug-target-inspect-button = Inspect

# Text of a button displayed in the "This Firefox" page, in the Temporary Extension
# section. Clicking on the button will open a file picker to load a temporary extension
about-debugging-tmp-extension-install-button = Load Temporary Add-on…

# Text displayed when trying to install a temporary extension in the "This Firefox" page.
about-debugging-tmp-extension-install-error = There was an error during the temporary add-on installation.

# Text of a button displayed for a temporary extension loaded in the "This Firefox" page.
# Clicking on the button will reload the extension.
about-debugging-tmp-extension-reload-button = Reload

# Text of a button displayed for a temporary extension loaded in the "This Firefox" page.
# Clicking on the button will uninstall the extension and remove it from the page.
about-debugging-tmp-extension-remove-button = Remove

# Text of a button displayed for a temporary extension loaded in the "This Firefox" page.
# Clicking on the button will forcefully terminate the extension background script (button
# only visible in extensions that includes a non-persistent background script, either an
# event page or a background service worker).
about-debugging-tmp-extension-terminate-bgscript-button = Terminate background script

# Message displayed in the file picker that opens to select a temporary extension to load
# (triggered by the button using "about-debugging-tmp-extension-install-button")
# manifest.json .xpi and .zip should not be localized.
# Note: this message is only displayed in Windows and Linux platforms.
about-debugging-tmp-extension-install-message = Select manifest.json file or .xpi/.zip archive

# This string is displayed as a message about the add-on having a temporaryID.
about-debugging-tmp-extension-temporary-id = This WebExtension has a temporary ID. <a>Learn more</a>

# Text displayed for extensions in "runtime" pages, before displaying a link the extension's
# manifest URL.
about-debugging-extension-manifest-url =
  .label = Manifest URL

# Text displayed for extensions in "runtime" pages, before displaying the extension's uuid.
# UUIDs look like b293e463-481e-5148-a487-5aaf7a130429
about-debugging-extension-uuid =
  .label = Internal UUID

# Text displayed for extensions (temporary extensions only) in "runtime" pages, before
# displaying the location of the temporary extension.
about-debugging-extension-location =
  .label = Location

# Text displayed for extensions in "runtime" pages, before displaying the extension's ID.
# For instance "geckoprofiler@mozilla.com" or "{ed26ddcb-5611-4512-a89a-51b8db81cfb2}".
about-debugging-extension-id =
  .label = Extension ID

# Text displayed for extensions in "runtime" pages, before displaying the status of the
# extension background script.
about-debugging-extension-backgroundscript =
  .label = Background script

# Displayed for extension using a non-persistent background page (either an event page or
# background service worker) when the background script is currently running.
about-debugging-extension-backgroundscript-status-running = Running

# Displayed for extension using a non-persistent background page when is currently stopped.
about-debugging-extension-backgroundscript-status-stopped = Stopped

# This string is displayed as a label of the button that pushes a test payload
# to a service worker.
# Note this relates to the "Push" API, which is normally not localized so it is
# probably better to not localize it.
# .disabledTitle is processed by the fluent-react / ActionButton code.
about-debugging-worker-action-push2 = Push
  .disabledTitle = Service Worker push is currently disabled for multiprocess { -brand-shorter-name }

# This string is displayed as a label of the button that starts a service worker.
# .disabledTitle is processed by the fluent-react / ActionButton code.
about-debugging-worker-action-start2 = Start
  .disabledTitle = Service Worker start is currently disabled for multiprocess { -brand-shorter-name }

# This string is displayed as a label of the button that unregisters a service worker.
about-debugging-worker-action-unregister = Unregister

# Displayed for service workers in runtime pages that listen to Fetch events.
about-debugging-worker-fetch-listening =
  .label = Fetch
  .value = Listening for fetch events

# Displayed for service workers in runtime pages that do not listen to Fetch events.
about-debugging-worker-fetch-not-listening =
  .label = Fetch
  .value = Not listening for fetch events

# Displayed for service workers in runtime pages that are currently running (service
# worker instance is active).
about-debugging-worker-status-running = Running

# Displayed for service workers in runtime pages that are registered but stopped.
about-debugging-worker-status-stopped = Stopped

# Displayed for service workers in runtime pages that are registering.
about-debugging-worker-status-registering = Registering

# Displayed for service workers in runtime pages, to label the scope of a worker
about-debugging-worker-scope =
  .label = Scope

# Displayed for service workers in runtime pages, to label the push service endpoint (url)
# of a worker
about-debugging-worker-push-service =
  .label = Push Service

# Displayed for service workers in runtime pages, to label the origin of a worker.
about-debugging-worker-origin =
  .label = Origin

# Displayed as title of the inspect button when service worker debugging is disabled.
about-debugging-worker-inspect-action-disabled =
  .title = Service Worker inspection is currently disabled for multiprocess { -brand-shorter-name }

# Displayed as title of the inspect button for zombie tabs (e.g. tabs loaded via a session restore).
about-debugging-zombie-tab-inspect-action-disabled =
  .title = Tab is not fully loaded and cannot be inspected

# Displayed as name for the Main Process debug target in the Processes category. Only for
# remote runtimes, if `devtools.aboutdebugging.process-debugging` is true.
about-debugging-multiprocess-toolbox-name = Multiprocess Toolbox

# Displayed as description for the Main Process debug target in the Processes category.
# Only for remote browsers, if `devtools.aboutdebugging.process-debugging` is true.
about-debugging-multiprocess-toolbox-description = Main Process and Content Processes for the target browser

about-debugging-message-error-icon =
  .alt = Error icon

about-debugging-message-info-icon =
  .alt = Information icon

about-debugging-message-warning-icon =
  .alt = Warning icon

# Alt text used for the close icon of message component (warnings, errors and notifications).
about-debugging-message-close-icon2 =
  .tooltiptext = Close message

# Label text used for the error details of message component.
about-debugging-message-details-label-error = Error details

# Label text used for the warning details of message component.
about-debugging-message-details-label-warning = Warning details

# Label text used for default state of details of message component.
about-debugging-message-details-label = Details
