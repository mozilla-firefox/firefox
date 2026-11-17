/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { MultilineEditor } from "chrome://browser/content/multilineeditor/multiline-editor.mjs";
import { createMentionsPlugin } from "chrome://browser/content/multilineeditor/plugins/MentionsPlugin.mjs";
import { createCommandsPlugin } from "chrome://browser/content/multilineeditor/plugins/CommandsPlugin.mjs";
import {
  AGENT_COMMAND_ITEMS,
  parseAgentCommand,
} from "chrome://browser/content/aiwindow/modules/AgentCommands.mjs";
import UrlbarPrefs from "chrome://browser/content/urlbar/UrlbarContentPrefs.mjs";
import { UrlbarShared } from "chrome://browser/content/urlbar/UrlbarShared.mjs";
import {
  CONTEXT_MENTION_TYPE,
  getTabGroupMentionId,
  parseTabGroupMentionId,
} from "chrome://browser/content/urlbar/SmartbarMentionUtils.mjs";

/**
 * @import {SmartbarInput} from "chrome://browser/content/urlbar/SmartbarInput.mjs"
 * @import {ContextMentionType} from "chrome://browser/content/urlbar/SmartbarMentionUtils.mjs"
 * @import {TabGroupColor} from "chrome://browser/content/tabbrowser/tabgroup.mjs"
 * @typedef {import("../../aiwindow/ui/components/smartwindow-panel-list/smartwindow-panel-list.mjs").SmartwindowPanelList} SmartwindowPanelList
 */

const { XPCOMUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/XPCOMUtils.sys.mjs"
);

const lazy = XPCOMUtils.declareLazy({
  AIWindowUI:
    "moz-src:///browser/components/aiwindow/ui/modules/AIWindowUI.sys.mjs",
  MENTION_TYPE:
    "moz-src:///browser/components/urlbar/SmartbarMentionsPanelSearch.sys.mjs",
  MonitorUIUtils:
    "moz-src:///browser/components/aiwindow/ui/modules/MonitorUIUtils.sys.mjs",
  SkippableTimer: "moz-src:///browser/components/urlbar/UrlbarUtils.sys.mjs",
  SmartbarMentionsPanelSearch:
    "moz-src:///browser/components/urlbar/SmartbarMentionsPanelSearch.sys.mjs",
});

const logger = () =>
  UrlbarShared.getLogger({
    prefix: "SmartbarMentionsPanel",
    maxLogLevelPref: "browser.smartwindow.smartbarMentions.logLevel",
  });

// Debounce delay for the mention suggestions query.
const MENTION_QUERY_DEBOUNCE_MS = 150;

// Marks the shared panel as showing "/" command results, so the mention and
// command selection handlers don't cross fire on the same panel
const COMMAND_TRIGGER = "inline-command";

/**
 * Whether agent command can run right now
 *
 * @returns {boolean}
 */
function isAgentCommandAvailable() {
  return (
    UrlbarPrefs.get("browser.smartwindow.agent.enabled") &&
    lazy.MonitorUIUtils.isMonitorRegionSupported()
  );
}

/**
 * Whether the input begins with a known agent command, e.g. "/watch ...".
 *
 * @param {string} value - Raw smartbar input
 * @returns {boolean}
 */
export function isAgentCommand(value) {
  return getAgentCommandId(value) !== null;
}

/**
 * The leading agent command id in the input, e.g. "watch", or null when the
 * input does not begin with a known command.
 *
 * @param {string} value - Raw smartbar input
 * @returns {?string}
 */
export function getAgentCommandId(value) {
  if (!isAgentCommandAvailable()) {
    return null;
  }
  const parsed = parseAgentCommand(value);
  return parsed && AGENT_COMMAND_ITEMS.has(parsed.command)
    ? parsed.command
    : null;
}

/**
 * Command suggestions whose id starts with the typed query
 *
 * @param {string} query - Text typed after the "/" trigger
 * @returns {Array<{headerL10nId: string, items: Array}>} Panel groups, empty when nothing matches
 */
function getCommandSuggestions(query) {
  if (!isAgentCommandAvailable()) {
    return [];
  }
  const normalized = query.trim().toLowerCase();
  // The panel keys off an id on each item, so fold the Map key into its value
  const items = [...AGENT_COMMAND_ITEMS]
    .filter(([id]) => id.startsWith(normalized))
    .map(([id, command]) => ({ id, ...command }));
  return items.length
    ? [{ headerL10nId: "smartbar-command-tasks-header", items }]
    : [];
}

const PLACEHOLDER_HINT_L10N_IDS = [
  "smartbar-placeholder-hint-1",
  "smartbar-placeholder-hint-2",
  "smartbar-placeholder-hint-3",
  "smartbar-placeholder-hint-4",
];

/**
 * @typedef {object} TabMention
 * @property {string} id - Mention ID: a tab URL or `group:<groupId>`
 * @property {ContextMentionType} [type] - Mention type
 * @property {string} [label] - Tab title or tab group label
 * @property {string} [icon] - Tab icon
 * @property {TabGroupColor} [color] - Tab group color
 * @property {string} [l10nId] - Fluent l10n ID for localized items
 * @property {object} [l10nArgs] - Arguments for l10n
 */

/**
 * @typedef {object} TabMentionGroup
 * @property {string} headerL10nId - Fluent l10n ID for the group header
 * @property {Array<TabMention>} items - Tab mentions in this group
 */

/**
 * @typedef {object} MentionSuggestionsResult
 * @property {TabMentionGroup[]} groups - The grouped mention suggestions
 * @property {number} totalCount - Total number of mention items across all groups
 */

/**
 * Get mention suggestions matching the search query.
 *
 * @param {import("../SmartbarMentionsPanelSearch.sys.mjs").SmartbarMentionsPanelSearch} mentionSearch - Search for mention suggestions
 * @param {string} searchString - Query to match against title and URL
 * @returns {MentionSuggestionsResult}
 */
function getMentionSuggestions(mentionSearch, searchString) {
  try {
    // Deduplicate by mention id, keeping the first occurrence
    // (prioritizes open tabs, then most recent)
    const seen = new Set();
    const deduplicated = mentionSearch
      .startQuery(searchString)
      // Sort by type to prioritize open tabs over closed tabs
      // Stable sort preserves timestamp ordering within each type
      .sort((r1, r2) => {
        if (r1.type == r2.type) {
          return 0;
        }
        return r1.type == lazy.MENTION_TYPE.TAB_OPEN ? -1 : 1;
      })
      .filter(item => {
        if (seen.has(item.url)) {
          return false;
        }
        seen.add(item.url);
        return true;
      })
      .slice(0, UrlbarPrefs.get("mentions.maxResults"))
      .map(({ url, title, icon }) => ({
        id: url,
        label: title,
        icon,
      }));

    let tabGroupItems = mentionSearch
      .getTabGroups()
      .map(({ id, label, color }) => ({
        id: getTabGroupMentionId(id),
        type: CONTEXT_MENTION_TYPE.TAB_GROUP,
        label,
        color,
      }))
      .filter(item => !seen.has(item.id) && seen.add(item.id))
      .slice(0, UrlbarPrefs.get("mentions.maxGroupResults"));

    /** @type {TabMentionGroup[]} */
    let groups = [];
    if (tabGroupItems.length) {
      groups.push({
        headerL10nId: "smartbar-mentions-list-tab-groups-label",
        items: tabGroupItems,
      });
    }
    groups.push({
      headerL10nId: "smartbar-mentions-list-recent-tabs-label",
      items: deduplicated,
    });

    return {
      groups,
      totalCount: deduplicated.length,
    };
  } catch (e) {
    logger().error("Error querying tabs:", e);
    return { groups: [], totalCount: 0 };
  }
}

/**
 * Calculate anchor position for panel positioning.
 *
 * @param {object} range - The text range
 * @param {object} view - The editor view
 * @returns {object} Anchor position
 */
const getAnchorPos = (range, view) => {
  const coordsFrom = view.coordsAtPos(range.from);
  const coordsTo = view.coordsAtPos(range.to);

  return {
    left: coordsFrom.left,
    top: coordsFrom.top,
    height: coordsTo.bottom - coordsFrom.top,
    width: coordsTo.right - coordsFrom.left,
  };
};

/**
 * Handles a suggestion panel's "panel-keydown"
 *
 * @param {MultilineEditor} editorElement - The editor element
 * @param {CustomEvent} e - The panel-keydown event
 */
function refocusEditorOnUnhandledPanelKey(editorElement, e) {
  const { originalEvent } = e.detail;
  if (["Tab", "ArrowUp", "ArrowDown", "Enter"].includes(originalEvent.key)) {
    return;
  }
  editorElement.focus();
}

/**
 *  Prevent Smartbar submission while mentions panel is open
 *
 * @param {() => boolean} isPanelOpen - Whether the plugin's panel is open
 * @param {KeyboardEvent} e - The editor keydown event
 */
function suppressEnterWhilePanelOpen(isPanelOpen, e) {
  if (isPanelOpen() && e.key === "Enter") {
    e.stopPropagation();
  }
}

/**
 * Setup context button to show mentions panel.
 *
 * @param {SmartbarInput} smartbarInput - The smartbar input element
 * @param {SmartwindowPanelList} panelList - The panel list component
 */
function setupContextMentionsButton(smartbarInput, panelList) {
  const contextButton = smartbarInput.querySelector("context-icon-button");

  panelList.addEventListener("shown", () => {
    // TODO: Bug 2064550 - use dataset instead
    if (panelList.getAttribute("data-triggered-by") === "context-mention") {
      contextButton.setAttribute("active", "");
    }
  });

  panelList.addEventListener("hidden", () => {
    contextButton.removeAttribute("active");
  });

  contextButton.addEventListener("aiwindow-context-button:on-click", () => {
    const contextMentionSearch = new lazy.SmartbarMentionsPanelSearch(
      // @ts-ignore topChromeWindow global
      window.browsingContext.topChromeWindow
    );
    panelList.anchor = contextButton;
    const { groups, totalCount } = getMentionSuggestions(
      contextMentionSearch,
      ""
    );
    panelList.groups = groups;
    // TODO: Bug 2064550 - use dataset instead
    panelList.setAttribute("data-triggered-by", "context-mention");
    panelList.toggle();

    const { chat_id, message_seq } = smartbarInput.conversationTelemetryInfo;
    Glean.smartWindow.addTabsClick.record({
      chat_id,
      location: smartbarInput.sapLocation,
      message_seq: String(message_seq),
      tabs_available: String(totalCount),
      tabs_preselected: String(smartbarInput.contextWebsitesCount),
    });
  });
}

/**
 * Mentions plugin setup for the editor.
 *
 * @param {MultilineEditor} editorElement - The editor element
 * @param {SmartwindowPanelList} panelList - The panel list component
 * @returns {object} plugin - The mentions plugin
 */
function setupMentionsPlugin(editorElement, panelList) {
  let isHandlingMentions = false;
  let mentionChangeTimer = null;
  let mentionSearch = null;
  let latestMentionData = null;

  document.l10n
    .formatValue("smartbar-mention-typing-placeholder")
    .then(text => {
      editorElement.style.setProperty(
        "--multiline-editor-mention-placeholder",
        `" ${text}"`
      );
    });

  const handleMentionsChange = () => {
    if (!latestMentionData || !mentionSearch) {
      return;
    }
    const { text } = latestMentionData;
    // Don't trim() the query - we need to preserve spaces to match tab titles
    // that contain spaces (e.g., "@my tab" should match "my tab title")
    const query = text.substring(1);
    const { groups } = getMentionSuggestions(mentionSearch, query);
    panelList.groups = groups;
    mentionChangeTimer = null;
  };

  const smartbarInput = /** @type {SmartbarInput} */ (
    editorElement.closest("moz-smartbar")
  );
  const plugin = createMentionsPlugin({
    triggerChar: "@",
    allowSpaces: true,
    toDOM: node => [
      "span",
      {
        "data-mention-type": node.attrs.type,
        "data-mention-id": node.attrs.id,
        "data-mention-label": node.attrs.label,
      },
      node.attrs.label,
    ],
    nodeView: node => [
      "smartwindow-chip",
      {
        ...(node.attrs.type == CONTEXT_MENTION_TYPE.TAB_GROUP
          ? { isTabGroup: true, tabGroupColor: node.attrs.color ?? "" }
          : {
              href: node.attrs.id,
              iconSrc: `page-icon:${node.attrs.id}`,
            }),
        label: node.attrs.label,
        type: "in-line",
      },
    ],
    onEnter: mentionData => {
      isHandlingMentions = true;
      latestMentionData = mentionData;
      mentionSearch = new lazy.SmartbarMentionsPanelSearch(
        // @ts-ignore topChromeWindow global
        window.browsingContext.topChromeWindow
      );
      panelList.anchor = getAnchorPos(mentionData.range, mentionData.view);
      const { groups, totalCount } = getMentionSuggestions(mentionSearch, "");
      panelList.groups = groups;
      // TODO: Bug 2064550 - use dataset instead
      panelList.setAttribute("data-triggered-by", "inline-mention");
      panelList.show();
      editorElement.setAttribute("data-mention-placeholder", "");

      const { chat_id, message_seq } = smartbarInput.conversationTelemetryInfo;
      Glean.smartWindow.mentionStart.record({
        chat_id,
        location: smartbarInput.sapLocation,
        mentions_available: String(totalCount),
        message_seq: String(message_seq),
      });
    },
    onChange: mentionData => {
      latestMentionData = mentionData;

      editorElement.toggleAttribute(
        "data-mention-placeholder",
        mentionData.text.length <= 1
      );

      if (!mentionChangeTimer) {
        mentionChangeTimer = new lazy.SkippableTimer({
          name: "SmartbarMentionsChange",
          callback: handleMentionsChange,
          time: MENTION_QUERY_DEBOUNCE_MS,
        });
      }
    },
    onExit: () => {
      isHandlingMentions = false;
      panelList.hide();
      editorElement.removeAttribute("data-mention-placeholder");

      // Cancel pending queries
      if (mentionChangeTimer) {
        mentionChangeTimer.cancel();
        mentionChangeTimer = null;
      }
      latestMentionData = null;
      mentionSearch = null;
    },
  });

  const handleChipDisconnected = e => {
    if (e.detail.type === "in-line") {
      const { chat_id, message_seq } = smartbarInput.conversationTelemetryInfo;
      Glean.smartWindow.mentionRemove.record({
        chat_id,
        location: smartbarInput.sapLocation,
        mentions: String(plugin.mentions.getAll().length),
        message_seq: String(message_seq),
      });
    }
  };

  const handleItemSelected = e => {
    // TODO: Bug 2064550 - use dataset instead
    // "/" command selections are handled by the commands plugin
    if (panelList.getAttribute("data-triggered-by") === COMMAND_TRIGGER) {
      return;
    }
    let { id, type, label, icon, color } = e.detail;
    let isTabGroup = type == CONTEXT_MENTION_TYPE.TAB_GROUP;
    // The labels match the mention_type metric.
    let mentionType = isTabGroup ? "tab_group" : "tab";

    // TODO: Bug 2064550 - use dataset instead
    const isContextButtonTrigger =
      panelList.getAttribute("data-triggered-by") === "context-mention";

    const { chat_id, message_seq } = smartbarInput.conversationTelemetryInfo;

    // If the mention suggestions are triggered by the context “+”-button,
    // add the mention to the context header.
    if (isContextButtonTrigger) {
      const tabsPreselected = smartbarInput.contextWebsitesCount;
      smartbarInput.addContextMention(
        isTabGroup
          ? {
              type: CONTEXT_MENTION_TYPE.TAB_GROUP,
              groupId: parseTabGroupMentionId(id),
              label,
              color,
            }
          : {
              type: CONTEXT_MENTION_TYPE.TAB,
              url: id,
              label,
              iconSrc: icon,
            }
      );
      Glean.smartWindow.addTabsSelection.record({
        chat_id,
        location: smartbarInput.sapLocation,
        mention_type: mentionType,
        message_seq: String(message_seq),
        tabs_available: String(
          panelList.groups.reduce((sum, group) => sum + group.items.length, 0)
        ),
        tabs_preselected: String(tabsPreselected),
        tabs_selected: String(smartbarInput.contextWebsitesCount),
      });
    } else {
      // Add inline mention when triggered by typing "@".
      // Inline mentions are not added as context chips.
      Glean.smartWindow.mentionSelect.record({
        chat_id,
        length: label.length,
        location: smartbarInput.sapLocation,
        mention_type: mentionType,
        mentions_available: panelList.groups.reduce(
          (sum, group) => sum + group.items.length,
          0
        ),
        message_seq: String(message_seq),
      });
      plugin.mentions.insert(
        {
          type: isTabGroup
            ? CONTEXT_MENTION_TYPE.TAB_GROUP
            : CONTEXT_MENTION_TYPE.TAB,
          id,
          label,
          color,
        },
        latestMentionData?.range.from ?? 0,
        latestMentionData?.range.to ?? 1
      );
    }
    // TODO: Bug 2064550 - use dataset instead
    panelList.removeAttribute("data-triggered-by");
  };

  const handlePanelKeyDown = e =>
    refocusEditorOnUnhandledPanelKey(editorElement, e);
  const handleEditorKeyDown = e =>
    suppressEnterWhilePanelOpen(() => isHandlingMentions, e);

  panelList.addEventListener("item-selected", handleItemSelected);
  panelList.addEventListener("panel-keydown", handlePanelKeyDown);
  editorElement.addEventListener("keydown", handleEditorKeyDown, {
    capture: true,
  });
  editorElement.addEventListener(
    "smartwindow-chip:disconnected",
    handleChipDisconnected
  );

  /**
   * Adds the following properties to `editorElement`:
   *
   * @property {boolean} isHandlingMentions - Whether the mentions panel is open
   * @property {boolean} hasMention - Whether the editor has inline mentions
   */
  Object.defineProperties(editorElement, {
    isHandlingMentions: {
      get: () => isHandlingMentions,
    },
    hasMention: {
      get: () => plugin.mentions.hasMention(),
    },
    getAllMentions: {
      value: () => plugin.mentions.getAll(),
    },
    /**
     * Inserts an inline mention at the specified text offset
     *
     * @param {Node} mention
     * @param {number} textOffset
     */
    insertMention: {
      value: (mention, textOffset) => {
        const pos = editorElement.textOffsetToPos(textOffset);
        plugin.mentions.insertNode(mention, pos);
      },
    },
  });

  return plugin;
}

/**
 * Typing "/" at the start of the input opens a dropdown of agent commands.
 * Picking one completes the input to "/<command> " and
 * the user types the prompt after it.
 * Shares the same panel as mentions, distinguished by the
 * COMMAND_TRIGGER marker.
 *
 * @param {MultilineEditor} editorElement - The editor element
 * @param {SmartwindowPanelList} panelList - The panel list component
 * @returns {object} plugin - The command plugin bundle
 */
function setupCommandsPlugin(editorElement, panelList) {
  let isHandlingCommands = false;
  let latestCommandData = null;
  const smartbarInput = /** @type {SmartbarInput} */ (
    editorElement.closest("moz-smartbar")
  );

  const isLeadingCommand = () =>
    editorElement.value.trimStart().startsWith("/");

  const updatePanel = query => {
    const groups = getCommandSuggestions(query);
    panelList.groups = groups;
    if (!groups.length) {
      panelList.hide();
      return false;
    }
    panelList.anchor = smartbarInput;
    // TODO: Bug 2064550 - use dataset instead
    panelList.setAttribute("data-triggered-by", COMMAND_TRIGGER);
    panelList.show();
    return true;
  };

  const onExitPalette = () => {
    isHandlingCommands = false;
    latestCommandData = null;
    // TODO: Bug 2064550 - use dataset instead
    if (panelList.getAttribute("data-triggered-by") === COMMAND_TRIGGER) {
      panelList.hide();
      panelList.removeAttribute("data-triggered-by");
    }
  };

  // Selecting a command runs it immediately
  const executeCommand = (id, submitType) => {
    if (!latestCommandData) {
      return;
    }

    const { chat_id, message_seq } = smartbarInput.conversationTelemetryInfo;
    Glean.smartWindow.agentCommandSelect.record({
      agent: id,
      chat_id,
      commands_available: String(
        panelList.groups.reduce((sum, group) => sum + group.items.length, 0)
      ),
      location: smartbarInput.sapLocation,
      message_seq: String(message_seq),
      source: "manual",
    });

    onExitPalette();
    smartbarInput.submitChat(null, `/${id}`, submitType);
  };

  const handleItemSelected = e => {
    // TODO: Bug 2064550 - use dataset instead
    if (
      panelList.getAttribute("data-triggered-by") !== COMMAND_TRIGGER ||
      !latestCommandData
    ) {
      return;
    }
    executeCommand(e.detail.id, "button");
  };

  const handlePanelKeyDown = e => {
    if (e.detail?.originalEvent?.key === "Escape") {
      onExitPalette();
      return;
    }
    refocusEditorOnUnhandledPanelKey(editorElement, e);
  };

  const handleEditorKeyDown = e => {
    if (
      !isHandlingCommands ||
      e.shiftKey ||
      e.altKey ||
      e.ctrlKey ||
      e.metaKey
    ) {
      return;
    }

    const keyHandlers = {
      ArrowDown: () => panelList.moveSelection(1),
      ArrowUp: () => panelList.moveSelection(-1),
      Enter: () => {
        const selected = panelList.getSelectedItem();
        if (selected) {
          executeCommand(selected.id, "enter");
        }
      },
      Escape: () => onExitPalette(),
    };

    const handler = keyHandlers[e.key];
    if (!handler) {
      return;
    }

    handler();
    e.preventDefault();
    e.stopPropagation();
  };

  panelList.addEventListener("item-selected", handleItemSelected);
  panelList.addEventListener("panel-keydown", handlePanelKeyDown);
  editorElement.addEventListener("keydown", handleEditorKeyDown, {
    capture: true,
  });

  /**
   * Exposes command state on the editor element so consumers can read it
   *
   * @property {boolean} isHandlingCommands - Whether the command palette is
   *   currently showing suggestions
   */
  Object.defineProperties(editorElement, {
    isHandlingCommands: {
      get: () => isHandlingCommands,
    },
  });

  return createCommandsPlugin({
    triggerChar: "/",
    allowSpaces: false,
    onEnter: data => {
      // Open the palette for any leading "/" so it can show and filter while
      // the user is still typing the command name
      if (!isLeadingCommand()) {
        return;
      }
      latestCommandData = data;
      isHandlingCommands = updatePanel(data.text.substring(1));

      if (isHandlingCommands) {
        const { chat_id, message_seq } =
          smartbarInput.conversationTelemetryInfo;
        Glean.smartWindow.agentCommandStart.record({
          chat_id,
          commands_available: String(
            panelList.groups.reduce((sum, group) => sum + group.items.length, 0)
          ),
          location: smartbarInput.sapLocation,
          message_seq: String(message_seq),
          source: "manual",
        });
      }
    },
    onChange: data => {
      if (!isLeadingCommand()) {
        return;
      }
      latestCommandData = data;
      isHandlingCommands = updatePanel(data.text.substring(1));
    },
    onExit: () => {
      onExitPalette();
    },
  });
}

/**
 * Creates a Smartbar editor element.
 *
 * @param {HTMLInputElement | MultilineEditor} inputElement
 *   The input element to replace.
 * @returns {{
 *   input: MultilineEditor,
 *   editor: object
 * } | null}
 *   An object with the new editor element and the adapter.
 */
export function createEditor(inputElement) {
  if (!inputElement) {
    return null;
  }

  if (inputElement instanceof MultilineEditor) {
    return {
      input: inputElement,
      editor: createEditorAdapter(inputElement),
    };
  }

  const doc = inputElement.ownerDocument;
  const editorElement = /** @type {MultilineEditor} */ (
    doc.createElement("moz-multiline-editor")
  );

  // Copy attributes except those that don’t apply.
  for (const attr of inputElement.attributes) {
    if (attr.name == "type" || attr.name == "value") {
      continue;
    }
    editorElement.setAttribute(attr.name, attr.value);
  }

  editorElement.className = inputElement.className;
  editorElement.id = inputElement.id;
  editorElement.value = inputElement.value ?? "";

  const isSidebarMode =
    window.browsingContext?.embedderElement?.id === lazy.AIWindowUI.BROWSER_ID;
  document.l10n
    .formatValues(PLACEHOLDER_HINT_L10N_IDS.map(id => ({ id })))
    .then(hints => {
      editorElement.placeholderHints = hints;
      editorElement.showPlaceholderAnimation = !isSidebarMode;
    })
    .catch(console.error);

  inputElement.replaceWith(editorElement);

  const container = editorElement.closest(".urlbar-input-container");
  const panelList = /** @type {SmartwindowPanelList} */ (
    container.querySelector("smartwindow-panel-list")
  );
  panelList.placeholderL10nId = "smartbar-mentions-list-no-results-label";
  panelList.sidebarMode = isSidebarMode;

  const smartbarInput = /** @type {SmartbarInput} */ (
    editorElement.closest("moz-smartbar")
  );

  const mentionsPlugin = setupMentionsPlugin(editorElement, panelList);
  const plugins = [mentionsPlugin];
  // Enable the "/" command palette in every Smart Window smartbar
  if (smartbarInput.sapName === "smartbar") {
    plugins.push(setupCommandsPlugin(editorElement, panelList));
  }
  editorElement.plugins = plugins;

  setupContextMentionsButton(smartbarInput, panelList);

  return {
    input: editorElement,
    editor: createEditorAdapter(editorElement),
  };
}

/**
 * Creates an adapter for the Smartbar editor element.
 *
 * @param {MultilineEditor} editorElement
 *   The editor element.
 */
export function createEditorAdapter(editorElement) {
  const getSelectionBounds = () => {
    let start = editorElement.selectionStart ?? 0;
    let end = editorElement.selectionEnd ?? start;
    if (start > end) {
      [start, end] = [end, start];
    }
    return { start, end };
  };

  return {
    get composing() {
      return !!editorElement.composing;
    },
    selection: {
      get rangeCount() {
        const { start, end } = getSelectionBounds();
        return start === end && editorElement.value === "" ? 0 : 1;
      },
      toStringWithFormat() {
        const { start, end } = getSelectionBounds();
        if (start == null || end == null) {
          return "";
        }
        return editorElement.value?.substring(start, end);
      },
    },
  };
}
