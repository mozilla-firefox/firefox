# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

devtools-settings-title = DevTools Settings

## Default Developer Tools section. Each checkbox enables or disables a tool.

devtools-settings-default-tools =
    .label = Default Developer Tools

devtools-settings-tool-netmonitor =
    .label = Network

devtools-settings-tool-styleeditor =
    .label = Style Editor

devtools-settings-tool-performance =
    .label = Performance

devtools-settings-tool-memory =
    .label = Memory

devtools-settings-tool-storage =
    .label = Storage

devtools-settings-tool-accessibility =
    .label = Accessibility

devtools-settings-tool-application =
    .label = Application

devtools-settings-tool-dom =
    .label = DOM

devtools-settings-tool-antitracking =
    .label = Anti tracking

# Description shown under a tool's checkbox when the tool can't be used with
# what the toolbox is currently debugging.
devtools-settings-tool-not-supported = Not supported for current toolbox target

## Toolbox Buttons section. Each checkbox shows or hides a button in the
## DevTools toolbox toolbar.

devtools-settings-toolbox-buttons =
    .label = Available Toolbox Buttons

# The button shows the number of errors on the page.
devtools-settings-toolbox-errorcount =
    .label = Show error count button

devtools-settings-toolbox-pick =
    .label = Show element picker button

# The button selects an iframe as the currently targeted document.
devtools-settings-toolbox-frames =
    .label = Show iframe selector button

# The button opens a list of experimental DevTools preferences. It's only
# available in unofficial builds.
devtools-settings-toolbox-experimental-prefs =
    .label = Show experimental preferences button

devtools-settings-toolbox-responsive =
    .label = Show Responsive Design Mode button

devtools-settings-toolbox-screenshot =
    .label = Show screenshot button

# The button toggles rulers along the edges of the page.
devtools-settings-toolbox-rulers =
    .label = Show rulers button

# The button toggles a tool for measuring a portion of the page.
devtools-settings-toolbox-measure =
    .label = Show measuring tool button

devtools-settings-toolbox-jstracer =
    .label = Show JavaScript Tracer button

## Experimental Features section.

devtools-settings-experimental =
    .label = Experimental Features

devtools-settings-debugger-stylesheets =
    .label = Show stylesheets in the debugger
    .title = List and view stylesheets in the debugger
    .description = Requires reopening the toolbox

## Web Console section, configuring the Web Console.

devtools-settings-webconsole =
    .label = Web Console

# Enables the split console, which displays the Web Console below other tools.
devtools-settings-webconsole-split-console =
    .label = Enable Split Console
    .title = Open Split Console with the Escape Key

## Themes section.

devtools-settings-themes =
    .label = Themes

# Accessible name for the group of buttons choosing the color theme used by
# DevTools.
devtools-settings-theme =
    .aria-label = Theme

# Follows the operating system's light or dark preference.
devtools-settings-theme-auto =
    .label = Auto

## Inspector section, configuring the page inspector.

devtools-settings-inspector =
    .label = Inspector

devtools-settings-inspector-user-agent-styles =
    .label = Show Browser Styles
    .title = Turning this on will show default styles that are loaded by the browser.

devtools-settings-inspector-user-agent-shadow-dom =
    .label = Show Browser Shadow DOM
    .title = Turning this on will show Shadow DOM elements handled by the browser.
    .description = Reloads the page

devtools-settings-inspector-show-comments =
    .label = Show comments
    .title = Display comment nodes in the inspector
    .description = Reloads the page

devtools-settings-inspector-collapse-attributes =
    .label = Truncate DOM attributes
    .title = Truncate long attributes in the inspector

devtools-settings-inspector-draggable-properties =
    .label = Click and drag to edit size values
    .title = Click and drag to edit size values in the inspector rules view.

devtools-settings-inspector-simple-highlighters =
    .label = Use simpler highlighters with prefers-reduced-motion
    .title = Enables simplified highlighters when prefers-reduced-motion is enabled. Draws lines instead of filled rectangles around highlighted elements to avoid flashing effects.

devtools-settings-inspector-focus-next-on-enter =
    .label = Focus next input on Enter
    .title = When enabled, hitting the Enter key when editing a selector, a property name or value will move the focus to the next input.

# Selects the format the inspector uses to display colors.
devtools-settings-inspector-color-unit =
    .label = Default color unit

## Options for the default color unit select. HSL(A), RGB(A) and HWB are CSS
## color functions.

# Keeps colors in the format they were written in.
devtools-settings-inspector-color-unit-authored =
    .label = As Authored

devtools-settings-inspector-color-unit-hex =
    .label = Hex

devtools-settings-inspector-color-unit-hsl =
    .label = HSL(A)

devtools-settings-inspector-color-unit-rgb =
    .label = RGB(A)

devtools-settings-inspector-color-unit-hwb =
    .label = HWB

# Uses CSS named colors, like "red", where one matches the color.
devtools-settings-inspector-color-unit-name =
    .label = Color Names

## Style Editor section, configuring the CSS Style Editor.

devtools-settings-styleeditor =
    .label = Style Editor

devtools-settings-styleeditor-autocompletion =
    .label = Autocomplete CSS
    .title = Autocomplete CSS properties, values and selectors in Style Editor as you type

## Screenshot Behavior section, configuring the screenshot button and command.

devtools-settings-screenshot =
    .label = Screenshot Behavior

devtools-settings-screenshot-clipboard =
    .label = Screenshot to clipboard only
    .title = Saves the screenshot directly to the clipboard

devtools-settings-screenshot-audio =
    .label = Play camera shutter sound
    .title = Enables the camera audio sound when taking screenshot

## Editor Preferences section, configuring the source editors used across
## DevTools.

devtools-settings-editor =
    .label = Editor Preferences

devtools-settings-editor-detect-indentation =
    .label = Detect indentation
    .title = Guess indentation based on source content

devtools-settings-editor-autoclose-brackets =
    .label = Autoclose brackets
    .title = Automatically insert closing brackets

devtools-settings-editor-expand-tab =
    .label = Indent using spaces
    .title = Use spaces instead of the tab character

# The number of spaces used to display a tab character in the source editor.
devtools-settings-editor-tab-size =
    .label = Tab size

# Selects which configuration of keyboard shortcuts to use in the source editor.
devtools-settings-editor-keymap =
    .label = Keybindings

## Options for the editor keybindings configuration. Vim, Emacs, Sublime Text
## are names of editors and should not be translated.

# Standard DevTools keyboard shortcuts.
devtools-settings-editor-keymap-default =
    .label = Default

devtools-settings-editor-keymap-vim =
    .label = Vim

devtools-settings-editor-keymap-emacs =
    .label = Emacs

devtools-settings-editor-keymap-sublime =
    .label = Sublime Text

## Advanced settings section.

devtools-settings-advanced =
    .label = Advanced settings

devtools-settings-advanced-source-maps =
    .label = Enable Source Maps
    .title = If you enable this option sources will be mapped in the tools.

devtools-settings-advanced-disable-cache =
    .label = Disable HTTP Cache (when toolbox is open)
    .title = Turning this option on will disable the HTTP cache for all tabs that have the toolbox open. Service Workers are not affected by this option.

devtools-settings-advanced-disable-javascript =
    .label = Disable JavaScript
    .title = Turning this option on will disable JavaScript for the current tab. If the tab or the toolbox is closed then this setting will be forgotten.
    .description = Current session only, reloads the page

devtools-settings-advanced-service-workers-http =
    .label = Enable Service Workers over HTTP (when toolbox is open)
    .title = Turning this option on will enable the service workers over HTTP for all tabs that have the toolbox open.

devtools-settings-advanced-chrome-debugging =
    .label = Enable browser chrome and add-on debugging toolboxes
    .title = Turning this option on will allow you to use various developer tools in browser context (via Tools > Web Developer > Browser Toolbox) and debug add-ons from the Add-ons Manager

devtools-settings-advanced-remote-debugging =
    .label = Enable remote debugging
    .title = Turning this option on will allow to debug this browser instance remotely

devtools-settings-advanced-f12 =
    .label = Use the F12 key to open or close DevTools
    .title = Turning this option on will bind the F12 key to open or close the DevTools toolbox

devtools-settings-advanced-custom-formatters =
    .label = Enable custom formatters
    .title = Turning this option on will allow sites to define custom formatters for DOM objects
