# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

### These strings are used in the Web Console and the Browser Console.
###
### The correct localization of this file might be to keep it in English,
### or another language commonly spoken among web developers. You want to
### make that choice consistent across the developer tools. A good criteria
### is the language in which you’d find the best documentation on web
### development on the web.

## Browser Console window titles. They depend on the value of the
## `devtools.browsertoolbox.scope` pref.

# Used when the pref is set to "everything": the Browser Console logs
# messages from all processes, not just the parent process.
browserconsole-multiprocess-title = Multiprocess Browser Console
# Used when the pref is set to "parent-process".
browserconsole-parent-process-title = Parent process Browser Console

##

webconsole-console-api-disabled = The Web Console logging API (console.log, console.info, console.warn, console.error) has been disabled by a script on this page.

# Indicator displayed before the URL of a request made with XMLHttpRequest.
webconsole-xhr-indicator = XHR

# Link displayed after some messages (e.g. security related messages), pointing
# to documentation.
webconsole-learn-more = Learn More

## Stack traces, for example the result of console.trace().

# Used for JavaScript functions without a name.
webconsole-stacktrace-anonymous-function = <anonymous>
# Indicates that a stack frame has an async parent.
# Variables:
#   $cause (String) - The cause of the async call (e.g. "setTimeout handler").
webconsole-stacktrace-async-stack = (Async: { $cause })

## Results of console.timeLog() and console.timeEnd().
## Variables:
##   $name (String) - Name of the timer.
##   $duration (String) - Elapsed time in milliseconds.

webconsole-time-log = { $name }: { $duration }ms
webconsole-time-end = { $name }: { $duration }ms - timer ended

## Results and errors of other console API calls.
## Variables:
##   $name (String) - Name of the timer or counter.

# Displayed when console.clear() removes the previous messages.
webconsole-console-cleared = Console was cleared.
# Displayed when console.clear() is ignored because the “Persist Logs” setting
# is enabled. “Persist Logs” should match webconsole-settings-persistent-logs.label.
webconsole-console-clear-prevented = console.clear() was prevented due to “Persist Logs”
# Displayed for console.count() calls without a label.
webconsole-no-counter-label = <no label>
# Displayed when console.countReset() is called with a counter that doesn’t exist.
webconsole-counter-doesnt-exist = Counter “{ $name }” doesn’t exist.
# Displayed for console.group() calls without a label.
webconsole-no-group-label = <no group label>
webconsole-max-timers-exceeded = The maximum allowed number of timers in this page was exceeded.
webconsole-timer-already-exists = Timer “{ $name }” already exists.
webconsole-timer-doesnt-exist = Timer “{ $name }” doesn’t exist.
webconsole-timer-js-error = Failed to process the timer name.

##

# Tooltip of the badge showing how many times a message was repeated.
# Variables:
#   $repeats (Number) - Number of repetitions.
webconsole-message-repeats =
    { $repeats ->
        [one] { $repeats } repeat
       *[other] { $repeats } repeats
    }
# Tooltip of the badge showing how many messages a group of similar warnings
# contains (e.g. Content Blocked messages).
# Variables:
#   $count (Number) - Number of messages in the group.
webconsole-warning-group-message-count =
    { $count ->
        [one] { $count } message
       *[other] { $count } messages
    }

## Warning shown when a user pastes code in the console for the first time.

# Text that the user needs to type to allow pasting. Avoid characters that
# can’t be typed with a keyboard.
webconsole-selfxss-ok-string = allow pasting
# Variables:
#   $okString (String) - The text to type, from webconsole-selfxss-ok-string.
webconsole-selfxss-message = Scam Warning: Take care when pasting things you don’t understand. This could allow attackers to steal your identity or take control of your computer. Please type ‘{ $okString }’ below (no need to press enter) to allow pasting.

##

# Notification displayed when the debugger is paused in an original file that
# isn’t pretty printed, and original variable mapping is disabled.
# `Show original variables` should match the checkbox label in the Scopes panel
# of the debugger.
webconsole-evaluation-notification-no-original-variable-mapping = Original variables name mapping in the debugger is disabled. Evaluation results might not be accurate. Click the `Show original variables` checkbox in the debugger scopes panel to enable.

# Tooltip of the arrow used to expand or collapse the details of a message
# (e.g. the stack trace of console.error()).
webconsole-message-toggle-details = Show/hide message details.
# Tooltip of the arrow used to expand or collapse a group of messages.
webconsole-group-toggle = Show/hide group.

## Column headers of the table displayed by console.table().

webconsole-table-index = (index)
webconsole-table-iteration-index = (iteration index)
webconsole-table-key = Key
webconsole-table-value = Values

## Tooltips of the icons displayed next to messages.

webconsole-level-error = Error
webconsole-level-warn = Warning
webconsole-level-info = Info
webconsole-level-log = Log
webconsole-level-debug = Debug
# Used for function calls logged by the JavaScript Tracer.
webconsole-level-jstracer = Function call
webconsole-logpoint-title = Logpoints from the debugger
webconsole-blocked-request = Blocked
webconsole-disabled-icon-title = This message is no longer active, message details are not available
webconsole-command-title = Evaluated code
webconsole-result-title = Evaluation result

## Keyboard shortcuts. Modifier names (Cmd, Ctrl, CmdOrCtrl, Shift) must not
## be translated.

# Focuses the filter input.
webconsole-find-key = CmdOrCtrl+F
# Closes the Browser Console.
webconsole-close-key = CmdOrCtrl+W
# Clears the console output. On macOS, both webconsole-clear-key-macos and
# webconsole-clear-alternative-key-macos are used.
webconsole-clear-key = Ctrl+Shift+L
webconsole-clear-key-macos = Ctrl+L
webconsole-clear-alternative-key-macos = Cmd+K

## Context menu items.

# Displayed for network messages.
webconsole-menu-copy-url =
    .label = Copy Link Location
    .accesskey = a
webconsole-menu-open-url =
    .label = Open URL in New Tab
    .accesskey = T
webconsole-menu-open-in-network-panel =
    .label = Open in Network Panel
    .accesskey = N
webconsole-menu-resend-network-request =
    .label = Resend Request
    .accesskey = n
# Displayed for DOM nodes.
webconsole-menu-open-node-in-inspector =
    .label = Reveal in Inspector
    .accesskey = Q
# Creates a global variable pointing to the logged object.
webconsole-menu-store-as-global-var =
    .label = Store as Global Variable
    .accesskey = S
# Copies the content of the message, or the user selection if there’s one.
webconsole-menu-copy-message =
    .label = Copy Message
    .accesskey = C
webconsole-menu-copy-object =
    .label = Copy Object
    .accesskey = o
webconsole-menu-open-in-sidebar =
    .label = Inspect object in Sidebar
    .accesskey = V
webconsole-menu-copy-all-messages =
    .label = Copy all Messages
    .accesskey = M
webconsole-menu-save-all-messages-file =
    .label = Save all Messages to File
    .accesskey = F

## Toolbar at the top of the console.

# Variables:
#   $shortcut (String) - Keyboard shortcut to clear the output.
webconsole-clear-button-tooltip = Clear the Web Console output ({ $shortcut })
webconsole-filter-input-placeholder = Filter Output
# Displayed in the filter input when some messages are hidden by the text filter.
# Variables:
#   $count (Number) - Number of hidden messages.
webconsole-filtered-messages-by-text =
    { $count ->
        [one] { $count } hidden
       *[other] { $count } hidden
    }
# Variables:
#   $count (Number) - Number of hidden messages.
webconsole-filtered-messages-by-text-tooltip =
    { $count ->
        [one] { $count } item hidden by text filter
       *[other] { $count } items hidden by text filter
    }
webconsole-close-split-console-button-tooltip = Close Split Console (Esc)

## Filter buttons, used to show or hide each category of messages.

webconsole-filter-button-errors = Errors
webconsole-filter-button-warnings = Warnings
webconsole-filter-button-logs = Logs
webconsole-filter-button-info = Info
webconsole-filter-button-debug = Debug
webconsole-filter-button-css = CSS
# Tooltip of the CSS button when the filter is not enabled.
webconsole-filter-button-css-inactive-tooltip = Stylesheets will be reparsed to check for errors. Refresh the page to also see errors from stylesheets modified from Javascript.
# Requests made with XMLHttpRequest or fetch().
webconsole-filter-button-xhr = XHR
# All network requests, e.g. for images or scripts.
webconsole-filter-button-requests = Requests
# Only displayed in the Browser Console and the Browser Toolbox console, to
# show or hide messages emitted by the browser itself (its privileged “chrome”
# code), as opposed to web content.
webconsole-filter-button-chrome = Browser
webconsole-filter-button-chrome-tooltip = Show messages emitted by the browser itself
# Only displayed in the Browser Console and the Browser Toolbox console, to
# show or hide messages emitted by web content (websites).
webconsole-filter-button-content = Content
webconsole-filter-button-content-tooltip = Show messages emitted by web content

## Console Settings menu.

webconsole-settings-button-tooltip = Console Settings
webconsole-settings-timestamps =
    .label = Show Timestamps
    .title = If you enable this option commands and output in the Web Console will display a timestamp
webconsole-settings-warning-groups =
    .label = Group Similar Messages
    .title = When enabled, similar messages are placed into groups
webconsole-settings-autocomplete =
    .label = Enable Autocompletion
    .title = If you enable this option the input will display suggestions as you type in it
webconsole-settings-persistent-logs =
    .label = Persist Logs
    .title = If you enable this option the output will not be cleared each time you navigate to a new page
webconsole-settings-instant-evaluation =
    .label = Instant Evaluation
    .title = If you enable this option the input will be instantly evaluated as you type in it
# Only displayed in the Browser Console and the Browser Toolbox console.
browserconsole-settings-network-monitoring =
    .label = Enable Network Monitoring
    .title = Enable this to start listening to network requests

##

# Displayed when the inspected page navigates to a new location.
# Variables:
#   $url (String) - The new URL.
webconsole-navigated = Navigated to { $url }
# Displayed when the inspected page is reloaded from DevTools.
webconsole-reloaded = Reloaded

webconsole-close-sidebar-button-tooltip = Close Sidebar

## Reverse search UI, used to search the command history.
## Variables:
##   $shortcut (String) - Keyboard shortcut for the action.

webconsole-reverse-search-input-placeholder = Search history
webconsole-reverse-search-close-button-tooltip = Close ({ $shortcut })
# Variables:
#   $index (Number) - Index of the displayed result.
#   $total (Number) - Total number of results.
webconsole-reverse-search-results =
    { $total ->
        [one] { $total } result
       *[other] { $index } of { $total } results
    }
webconsole-reverse-search-no-result = No results
webconsole-reverse-search-previous-button-tooltip = Previous result ({ $shortcut })
webconsole-reverse-search-next-button-tooltip = Next result ({ $shortcut })

## Dialog displayed when the user tries to autocomplete a property with a
## getter. For example, given `x = {get y() {}}`, typing `x.y.` displays
## the dialog.

# Variables:
#   $getterName (String) - Name of the getter.
webconsole-confirm-dialog-getter-label = Invoke getter <span data-l10n-name="getter-name">{ $getterName }</span> to retrieve the property list?
# Variables:
#   $shortcut (String) - Keyboard shortcut for the action.
webconsole-confirm-dialog-getter-invoke-button = Invoke ({ $shortcut })
# Variables:
#   $shortcut (String) - Keyboard shortcut for the action.
webconsole-confirm-dialog-getter-close-button-tooltip = Close ({ $shortcut })

##

# Label for the list of HTML elements matching the selector of a CSS warning.
# Variables:
#   $selector (String) - The CSS selector.
webconsole-css-warning-elements = Elements matching selector: { $selector }

## Displayed when the console fails to render a message.

# Variables:
#   $url (String) - URL to file a bug about the error.
webconsole-message-render-error = [DEVTOOLS ERROR] We’re sorry, we couldn’t render the message. This shouldn’t have happened - please file a bug at { $url } with the message metadata in the description.
webconsole-message-render-error-copy-button = Copy message metadata to clipboard

## Toolbar of the multi-line editor mode (devtools.webconsole.input.editor).
## Variables:
##   $shortcut (String) - Keyboard shortcut for the action.

webconsole-editor-toolbar-execute-button-label = Run
webconsole-editor-toolbar-execute-button-tooltip = Run expression ({ $shortcut }). This won’t clear the input.
webconsole-editor-toolbar-pretty-print-button-tooltip = Pretty print expression
webconsole-editor-toolbar-prev-expression-button-tooltip = Previous Expression
webconsole-editor-toolbar-next-expression-button-tooltip = Next Expression
# The reverse search mimics the bash-like reverse search of command history,
# searching commands from the last item backwards.
webconsole-editor-toolbar-open-reverse-search-tooltip = Open History Reverse Search ({ $shortcut })
webconsole-editor-toolbar-close-reverse-search-tooltip = Close History Reverse Search ({ $shortcut })
webconsole-editor-toolbar-close-button-tooltip = Switch back to inline mode ({ $shortcut })

## Console input.

# Tooltip of the button that switches to the multi-line editor mode.
# Variables:
#   $shortcut (String) - Keyboard shortcut for the action.
webconsole-input-open-editor-button-tooltip = Switch to multi-line editor mode ({ $shortcut })

# Displayed the first time the multi-line editor mode is used, until dismissed.
# Variables:
#   $runShortcut (String) - Shortcut to run the expression (e.g. Ctrl+Enter).
webconsole-input-editor-onboarding = Iterate on your code faster with the new multi-line editor mode. Use <span data-l10n-name="enter-key">{ webconsole-enter-key }</span> to add new lines and <span data-l10n-name="run-shortcut">{ $runShortcut }</span> to run.
webconsole-input-editor-onboarding-dismiss = Got it!
# Name of the Enter key, used in the editor onboarding and in the tooltip of
# the “Run” button.
webconsole-enter-key = Enter

# Title of the file picker used to open a file in the console input (Ctrl+O or
# Cmd+O on macOS).
webconsole-input-open-javascript-file = Open JavaScript File
webconsole-input-open-javascript-file-filter = JavaScript Files

# Name used for the main thread of execution of the page in the evaluation
# context selector.
webconsole-input-selector-top = Top
webconsole-input-selector-tooltip = Select evaluation context

## Labels of groups of similar warnings.

webconsole-group-cookie = Cookie warnings
# “Content-Security-Policy” is the name of an HTTP header and should not be
# translated.
webconsole-group-csp = Content-Security-Policy warnings
