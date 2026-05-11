# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

### These strings are used inside the Application panel which is available
### by setting the preference `devtools-application-enabled` to true.

### The correct localization of this file might be to keep it in English, or another
### language commonly spoken among web developers. You want to make that choice consistent
### across the developer tools. A good criteria is the language in which you'd find the
### best documentation on web development on the web.

# Header for the list of Service Workers displayed in the application panel for the current page.
serviceworker-list-header = Service Workers

# Text displayed next to the list of Service Workers to encourage users to check out
# about:debugging to see all registered Service Workers.
serviceworker-list-aboutdebugging = Open <a>about:debugging</a> for Service Workers from other domains

# Text for the button to unregister a Service Worker. Displayed for active Service Workers.
serviceworker-worker-unregister = Unregister

# Alt text for the image icon displayed inside a debug link for a service worker.
serviceworker-worker-inspect-icon =
  .alt = Inspect

# Text for the start link displayed for a registered but not running Service Worker.
# Clicking on the link will attempt to start the service worker.
serviceworker-worker-start3 = Start

# Text displayed for the updated time of the service worker. The <time> element will
# display the last update time of the service worker script.
# Variables:
#   $date (date) - Update date
serviceworker-worker-updated = Updated <time>{ DATETIME($date, month: "long", year: "numeric", day: "numeric", hour: "numeric", minute: "numeric", second: "numeric") }</time>

## Service Worker status strings: all serviceworker-worker-status-* strings are also
## defined in aboutdebugging.properties and should be synchronized with them.

# Service Worker status. A running service worker is registered, currently executed, can
# be debugged and stopped.
serviceworker-worker-status-running = Running

# Service Worker status. A stopped service worker is registered but not currently active.
serviceworker-worker-status-stopped = Stopped

# Text displayed when no service workers are visible for the current page.
serviceworker-empty-intro2 = No service workers found

# Link will open https://developer.mozilla.org/docs/Web/API/Service_Worker_API/Using_Service_Workers
serviceworker-empty-intro-link = Learn more

# Text displayed when there are no Service Workers to display for the current page,
# introducing hints to debug Service Worker issues.
# <a> and <span> are links that will open the webconsole and the debugger, respectively.
serviceworker-empty-suggestions2 = If the current page should have a service worker, you could look for errors in the <a>Console</a> or step through your service worker registration in the <span>Debugger</span>.

# Suggestion to go to about:debugging in order to see Service Workers for all domains.
# Link will open about:debugging in a new tab.
serviceworker-empty-suggestions-aboutdebugging2 = View service workers from other domains

# Header for the Manifest page when we have an actual manifest
manifest-view-header = App Manifest

# Header for the Manifest page when there's no manifest to inspect
manifest-empty-intro2 = No web app manifest detected

# The link will open https://developer.mozilla.org/en-US/docs/Web/Manifest
manifest-empty-intro-link = Learn how to add a manifest

# Header for the Errors and Warnings section of Manifest inspection displayed in the application panel.
manifest-item-warnings = Errors and Warnings

# Header for the Identity section of Manifest inspection displayed in the application panel.
manifest-item-identity = Identity

# Header for the Presentation section of Manifest inspection displayed in the application panel.
manifest-item-presentation = Presentation

# Header for the Icon section of Manifest inspection displayed in the application panel.
manifest-item-icons = Icons

# Text displayed while we are loading the manifest file
manifest-loading = Loading manifest…

# Text displayed when the manifest has been successfully loaded
manifest-loaded-ok = Manifest loaded.

# Text displayed as a caption when there has been an error while trying to
# load the manifest
manifest-loaded-error = There was an error while loading the manifest:

# Text displayed as an error when there has been a Firefox DevTools error while
# trying to load the manifest
manifest-loaded-devtools-error = Firefox DevTools error

# Text displayed when the page has no manifest available
manifest-non-existing = No manifest found to inspect.

# Text displayed when the page has a manifest embedded in a Data URL and
# thus we cannot link to it.
manifest-json-link-data-url = The manifest is embedded in a Data URL.

# Text displayed at manifest icons to label their purpose, as declared
# in the manifest.
# Variables:
#   $purpose (string) - Manifest purpose
manifest-icon-purpose = Purpose: <code>{ $purpose }</code>

# Text displayed as the alt attribute for <img> tags showing the icons in the
# manifest.
manifest-icon-img =
  .alt = Icon

# Text displayed as the title attribute for <img> tags showing the icons in the
# manifest.
# Variables:
#   $sizes (string) - User-dependent string that has been parsed as a
#                     space-separated list of `<width>x<height>` sizes or
#                     the keyword `any`.
manifest-icon-img-title = Icon with sizes: { $sizes }

# Text displayed as the title attribute for <img> tags showing the icons in the
# manifest, in case there's no icon size specified by the user
manifest-icon-img-title-no-sizes = Unspecified size icon

# Sidebar navigation item for Manifest sidebar item section
sidebar-item-manifest = Manifest
  .alt = Manifest Icon
  .title = Manifest

# Sidebar navigation item for Service Workers sidebar item section
sidebar-item-service-workers = Service Workers
  .alt = Service Workers Icon
  .title = Service Workers

# Sidebar navigation item for Session History sidebar item section
sidebar-item-session-history = Session History
  .alt = Session History Icon
  .title = Session History

# Entry in the Session History diagram
session-history-entry-info-button-title =
  .title = Show session history data

# Header for the Session History page when session history diagrams are unavailable
session-history-unavailable = Session history diagram unavailable

# Text displayed for when the target does not support showing session history diagrams
session-history-target-unsupported = Target does not support showing session history diagrams

# Text for the ALT and TITLE attributes of the warning icon
icon-warning =
  .alt = Warning icon
  .title = Warning

# Text for the ALT and TITLE attributes of the error icon
icon-error =
  .alt = Error icon
  .title = Error
