/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/** @import { SettingSection, SettingChangeHandler, SettingCondition } from "./types/inventory.d.ts" */

// eslint-disable-next-line no-shadow
const { require } = ChromeUtils.importESModule(
  "resource://devtools/shared/loader/Loader.sys.mjs"
);
/** @type {typeof import("../framework/devtools.js")} */
const {
  gDevTools,
} = require("resource://devtools/client/framework/devtools.js");

/** @type {ReadonlyArray<SettingSection>} */
export const inventory = [
  {
    id: "settings-default-tools",
    l10nId: "devtools-settings-default-tools",
    settings: [
      {
        id: "settings-tool-netmonitor",
        l10nId: "devtools-settings-tool-netmonitor",
        type: "boolean",
        pref: "devtools.netmonitor.enabled",
        disabled: "unsupportedTool",
        disabledL10nId: "devtools-settings-tool-not-supported",
        changeAction: "updateToolRegistration",
      },
      {
        id: "settings-tool-styleeditor",
        l10nId: "devtools-settings-tool-styleeditor",
        type: "boolean",
        pref: "devtools.styleeditor.enabled",
        disabled: "unsupportedTool",
        disabledL10nId: "devtools-settings-tool-not-supported",
        changeAction: "updateToolRegistration",
      },
      {
        id: "settings-tool-performance",
        l10nId: "devtools-settings-tool-performance",
        type: "boolean",
        pref: "devtools.performance.enabled",
        disabled: "unsupportedTool",
        disabledL10nId: "devtools-settings-tool-not-supported",
        changeAction: "updateToolRegistration",
      },
      {
        id: "settings-tool-memory",
        l10nId: "devtools-settings-tool-memory",
        type: "boolean",
        pref: "devtools.memory.enabled",
        disabled: "unsupportedTool",
        disabledL10nId: "devtools-settings-tool-not-supported",
        changeAction: "updateToolRegistration",
      },
      {
        id: "settings-tool-storage",
        l10nId: "devtools-settings-tool-storage",
        type: "boolean",
        pref: "devtools.storage.enabled",
        disabled: "unsupportedTool",
        disabledL10nId: "devtools-settings-tool-not-supported",
        changeAction: "updateToolRegistration",
      },
      {
        id: "settings-tool-accessibility",
        l10nId: "devtools-settings-tool-accessibility",
        type: "boolean",
        pref: "devtools.accessibility.enabled",
        disabled: "unsupportedTool",
        disabledL10nId: "devtools-settings-tool-not-supported",
        changeAction: "updateToolRegistration",
      },
      {
        id: "settings-tool-application",
        l10nId: "devtools-settings-tool-application",
        type: "boolean",
        pref: "devtools.application.enabled",
        disabled: "unsupportedTool",
        disabledL10nId: "devtools-settings-tool-not-supported",
        changeAction: "updateToolRegistration",
      },
      {
        id: "settings-tool-dom",
        l10nId: "devtools-settings-tool-dom",
        type: "boolean",
        pref: "devtools.dom.enabled",
        disabled: "unsupportedTool",
        disabledL10nId: "devtools-settings-tool-not-supported",
        changeAction: "updateToolRegistration",
      },
      {
        id: "settings-tool-antitracking",
        l10nId: "devtools-settings-tool-antitracking",
        type: "boolean",
        pref: "devtools.anti-tracking.enabled",
        // TODO: Move Anti Tracking to an experimental tools section.
        // https://bugzilla.mozilla.org/show_bug.cgi?id=2084099
        hidden: "antiTrackingUnavailable",
        disabled: "unsupportedTool",
        disabledL10nId: "devtools-settings-tool-not-supported",
        changeAction: "updateToolRegistration",
      },
    ],
  },
  {
    id: "settings-toolbox-buttons",
    l10nId: "devtools-settings-toolbox-buttons",
    settings: [
      {
        id: "settings-toolbox-errorcount",
        l10nId: "devtools-settings-toolbox-errorcount",
        type: "boolean",
        pref: "devtools.command-button-errorcount.enabled",
        hidden: "unsupportedButton",
        changeAction: "updateToolboxButtons",
      },
      {
        id: "settings-toolbox-pick",
        l10nId: "devtools-settings-toolbox-pick",
        type: "boolean",
        pref: "devtools.command-button-pick.enabled",
        hidden: "unsupportedButton",
        changeAction: "updateToolboxButtons",
      },
      {
        id: "settings-toolbox-frames",
        l10nId: "devtools-settings-toolbox-frames",
        type: "boolean",
        pref: "devtools.command-button-frames.enabled",
        hidden: "unsupportedButton",
        changeAction: "updateToolboxButtons",
      },
      {
        id: "settings-toolbox-experimental-prefs",
        l10nId: "devtools-settings-toolbox-experimental-prefs",
        type: "boolean",
        pref: "devtools.command-button-experimental-prefs.enabled",
        hidden: "unsupportedButton",
        changeAction: "updateToolboxButtons",
      },
      {
        id: "settings-toolbox-responsive",
        l10nId: "devtools-settings-toolbox-responsive",
        type: "boolean",
        pref: "devtools.command-button-responsive.enabled",
        hidden: "unsupportedButton",
        changeAction: "updateToolboxButtons",
      },
      {
        id: "settings-toolbox-screenshot",
        l10nId: "devtools-settings-toolbox-screenshot",
        type: "boolean",
        pref: "devtools.command-button-screenshot.enabled",
        hidden: "unsupportedButton",
        changeAction: "updateToolboxButtons",
      },
      {
        id: "settings-toolbox-rulers",
        l10nId: "devtools-settings-toolbox-rulers",
        type: "boolean",
        pref: "devtools.command-button-rulers.enabled",
        hidden: "unsupportedButton",
        changeAction: "updateToolboxButtons",
      },
      {
        id: "settings-toolbox-measure",
        l10nId: "devtools-settings-toolbox-measure",
        type: "boolean",
        pref: "devtools.command-button-measure.enabled",
        hidden: "unsupportedButton",
        changeAction: "updateToolboxButtons",
      },
      {
        id: "settings-toolbox-jstracer",
        l10nId: "devtools-settings-toolbox-jstracer",
        type: "boolean",
        pref: "devtools.command-button-jstracer.enabled",
        hidden: "unsupportedButton",
        changeAction: "updateToolboxButtons",
      },
    ],
  },
  {
    id: "settings-experimental",
    l10nId: "devtools-settings-experimental",
    settings: [
      {
        id: "settings-debugger-stylesheets",
        l10nId: "devtools-settings-debugger-stylesheets",
        type: "boolean",
        pref: "devtools.debugger.features.stylesheets-in-debugger",
        changeAction: "recordDebuggerStylesheets",
      },
    ],
  },
  {
    id: "settings-webconsole",
    l10nId: "devtools-settings-webconsole",
    settings: [
      {
        id: "settings-webconsole-split-console",
        l10nId: "devtools-settings-webconsole-split-console",
        type: "boolean",
        pref: "devtools.toolbox.splitconsole.enabled",
        changeAction: "updateSplitConsole",
      },
    ],
  },
  {
    id: "settings-themes",
    l10nId: "devtools-settings-themes",
    settings: [
      {
        id: "settings-theme",
        l10nId: "devtools-settings-theme",
        type: "custom-element",
        element: "devtools-settings-theme",
      },
    ],
  },
  {
    id: "settings-inspector",
    l10nId: "devtools-settings-inspector",
    settings: [
      {
        id: "settings-inspector-user-agent-styles",
        l10nId: "devtools-settings-inspector-user-agent-styles",
        type: "boolean",
        pref: "devtools.inspector.showUserAgentStyles",
      },
      {
        id: "settings-inspector-user-agent-shadow-dom",
        l10nId: "devtools-settings-inspector-user-agent-shadow-dom",
        type: "boolean",
        pref: "devtools.inspector.showAllAnonymousContent",
        changeAction: "reloadPage",
      },
      {
        id: "settings-inspector-show-comments",
        l10nId: "devtools-settings-inspector-show-comments",
        type: "boolean",
        pref: "devtools.markup.showComments",
        changeAction: "reloadPage",
      },
      {
        id: "settings-inspector-collapse-attributes",
        l10nId: "devtools-settings-inspector-collapse-attributes",
        type: "boolean",
        pref: "devtools.markup.collapseAttributes",
      },
      {
        id: "settings-inspector-draggable-properties",
        l10nId: "devtools-settings-inspector-draggable-properties",
        type: "boolean",
        pref: "devtools.inspector.draggable_properties",
      },
      {
        id: "settings-inspector-simple-highlighters",
        l10nId: "devtools-settings-inspector-simple-highlighters",
        type: "boolean",
        pref: "devtools.inspector.simple-highlighters-reduced-motion",
      },
      {
        id: "settings-inspector-focus-next-on-enter",
        l10nId: "devtools-settings-inspector-focus-next-on-enter",
        type: "boolean",
        pref: "devtools.inspector.rule-view.focusNextOnEnter",
      },
      {
        id: "settings-inspector-color-unit",
        l10nId: "devtools-settings-inspector-color-unit",
        type: "select",
        pref: "devtools.defaultColorUnit",
        options: [
          {
            value: "authored",
            l10nId: "devtools-settings-inspector-color-unit-authored",
          },
          {
            value: "hex",
            l10nId: "devtools-settings-inspector-color-unit-hex",
          },
          {
            value: "hsl",
            l10nId: "devtools-settings-inspector-color-unit-hsl",
          },
          {
            value: "rgb",
            l10nId: "devtools-settings-inspector-color-unit-rgb",
          },
          {
            value: "hwb",
            l10nId: "devtools-settings-inspector-color-unit-hwb",
          },
          {
            value: "name",
            l10nId: "devtools-settings-inspector-color-unit-name",
          },
        ],
      },
    ],
  },
  {
    id: "settings-styleeditor",
    l10nId: "devtools-settings-styleeditor",
    settings: [
      {
        id: "settings-styleeditor-autocompletion",
        l10nId: "devtools-settings-styleeditor-autocompletion",
        type: "boolean",
        pref: "devtools.styleeditor.autocompletion-enabled",
      },
    ],
  },
  {
    id: "settings-screenshot",
    l10nId: "devtools-settings-screenshot",
    settings: [
      {
        id: "settings-screenshot-clipboard",
        l10nId: "devtools-settings-screenshot-clipboard",
        type: "boolean",
        pref: "devtools.screenshot.clipboard.enabled",
      },
      {
        id: "settings-screenshot-audio",
        l10nId: "devtools-settings-screenshot-audio",
        type: "boolean",
        pref: "devtools.screenshot.audio.enabled",
      },
    ],
  },
  {
    id: "settings-editor",
    l10nId: "devtools-settings-editor",
    settings: [
      {
        id: "settings-editor-detect-indentation",
        l10nId: "devtools-settings-editor-detect-indentation",
        type: "boolean",
        pref: "devtools.editor.detectindentation",
      },
      {
        id: "settings-editor-autoclose-brackets",
        l10nId: "devtools-settings-editor-autoclose-brackets",
        type: "boolean",
        pref: "devtools.editor.autoclosebrackets",
      },
      {
        id: "settings-editor-expand-tab",
        l10nId: "devtools-settings-editor-expand-tab",
        type: "boolean",
        pref: "devtools.editor.expandtab",
      },
      {
        id: "settings-editor-tab-size",
        l10nId: "devtools-settings-editor-tab-size",
        type: "select",
        pref: "devtools.editor.tabsize",
        options: [{ value: "2" }, { value: "4" }, { value: "8" }],
      },
      {
        id: "settings-editor-keymap",
        l10nId: "devtools-settings-editor-keymap",
        type: "select",
        pref: "devtools.editor.keymap",
        options: [
          {
            value: "default",
            l10nId: "devtools-settings-editor-keymap-default",
          },
          { value: "vim", l10nId: "devtools-settings-editor-keymap-vim" },
          { value: "emacs", l10nId: "devtools-settings-editor-keymap-emacs" },
          {
            value: "sublime",
            l10nId: "devtools-settings-editor-keymap-sublime",
          },
        ],
      },
    ],
  },
  {
    id: "settings-advanced",
    l10nId: "devtools-settings-advanced",
    settings: [
      {
        id: "settings-advanced-source-maps",
        l10nId: "devtools-settings-advanced-source-maps",
        type: "boolean",
        pref: "devtools.source-map.client-service.enabled",
      },
      {
        id: "settings-advanced-disable-cache",
        l10nId: "devtools-settings-advanced-disable-cache",
        type: "boolean",
        pref: "devtools.cache.disabled",
      },
      {
        id: "settings-advanced-disable-javascript",
        l10nId: "devtools-settings-advanced-disable-javascript",
        type: "custom-element",
        element: "devtools-settings-disable-javascript",
        hidden: "notTabToolbox",
      },
      {
        id: "settings-advanced-service-workers-http",
        l10nId: "devtools-settings-advanced-service-workers-http",
        type: "boolean",
        pref: "devtools.serviceWorkers.testing.enabled",
      },
      {
        id: "settings-advanced-chrome-debugging",
        l10nId: "devtools-settings-advanced-chrome-debugging",
        type: "boolean",
        pref: "devtools.chrome.enabled",
      },
      {
        id: "settings-advanced-remote-debugging",
        l10nId: "devtools-settings-advanced-remote-debugging",
        type: "boolean",
        pref: "devtools.debugger.remote-enabled",
      },
      {
        id: "settings-advanced-f12",
        l10nId: "devtools-settings-advanced-f12",
        type: "boolean",
        pref: "devtools.f12_enabled",
      },
      {
        id: "settings-advanced-custom-formatters",
        l10nId: "devtools-settings-advanced-custom-formatters",
        type: "boolean",
        pref: "devtools.custom-formatters.enabled",
      },
    ],
  },
];

/** @satisfies {Record<string, SettingChangeHandler>} */
export const callbacks = {
  updateToolboxButtons: ({ toolbox }) =>
    toolbox.updateToolboxButtonsVisibility(),

  updateSplitConsole: ({ toolbox }) => toolbox.updateIsSplitConsoleEnabled(),

  reloadPage: ({ toolbox }) => toolbox.reload(false),

  updateToolRegistration: ({ event }) => {
    const tool = getToolForPref(event.pref);
    const isEnabled = Services.prefs.getBoolPref(event.pref);
    gDevTools.emit(
      isEnabled ? "tool-registered" : "tool-unregistered",
      tool.id
    );
    Glean.devtoolsTool.registered[tool.id].set(isEnabled);
  },

  recordDebuggerStylesheets: ({ event }) => {
    if (Services.prefs.getBoolPref(event.pref)) {
      Glean.devtoolsDebuggerStylesheets.stylesheetPrefEnabledCount.add(1);
    } else {
      Glean.devtoolsDebuggerStylesheets.stylesheetPrefDisabledCount.add(1);
    }
  },
};

/** @satisfies {Record<string, SettingCondition>} */
export const conditions = {
  // Match the old settings panel, which lists Anti Tracking only when it was
  // enabled at startup and keeps it listed after it's unticked.
  // https://bugzilla.mozilla.org/show_bug.cgi?id=2084099
  antiTrackingUnavailable: () =>
    !gDevTools.getDefaultTools().some(tool => tool.id === "antitracking"),

  unsupportedTool: ({ toolbox, setting }) => {
    if (!("pref" in setting)) {
      throw new Error(`The ${setting.id} setting has no pref.`);
    }
    const tool = getToolForPref(setting.pref);
    return !tool.isToolSupported(toolbox);
  },

  unsupportedButton: ({ toolbox, setting }) => {
    if (!toolbox) {
      return false;
    }
    if (!("pref" in setting)) {
      throw new Error(`The ${setting.id} setting has no pref.`);
    }
    const button = toolbox.toolbarButtons?.find(
      definition => definition.visibilityswitch === setting.pref
    );
    if (!button) {
      throw new Error(`No toolbox button uses the ${setting.pref} pref.`);
    }
    return !button.isToolSupported(toolbox);
  },

  notTabToolbox: ({ toolbox }) =>
    !toolbox.commands.descriptorFront.isTabDescriptor,
};

/** @param {string} pref */
function getToolForPref(pref) {
  const tool = gDevTools
    .getDefaultTools()
    .find(
      definition =>
        "visibilityswitch" in definition && definition.visibilityswitch === pref
    );
  if (!tool) {
    throw new Error(`No tool uses the ${pref} pref.`);
  }
  return tool;
}
