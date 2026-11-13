/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import {
  GET_OPEN_TABS,
  SEARCH_BROWSING_HISTORY,
  GET_PAGE_CONTENT,
  RUN_SEARCH,
  GET_USER_MEMORIES,
  GET_NAVIGATION_INFO,
  SEARCH_THE_WEB,
  GENERATE_AITAB,
  isAITabModification,
} from "moz-src:///browser/components/aiwindow/models/Tools.sys.mjs";

export const ACTION_LOG_UI_TYPE = "action-log";

const EMPTY_ROWS = Object.freeze([]);

/**
 * A localized label shown on an action log entry
 *
 * @typedef {object} ActionLogLabel
 * @property {string} l10nId - Fluent message id for the label text
 * @property {object} [l10nArgs] - optional Fluent variables (e.g. counts)
 */

/**
 * A link embedded inside an action log label. Requires a matching
 * <a data-l10n-name> in the l10n message.
 *
 * @typedef {object} ActionLogLabelLink
 * @property {string} l10nName - matches data-l10n-name on the rendered anchor
 * @property {string} href - URL the anchor opens
 */

/**
 * A website chip shown under an action log row when the tool returns urls
 *
 * @typedef {object} ActionLogChip
 * @property {string} url - the website the chip links to
 * @property {string} label - the chip's display text (e.g. page title)
 */

/**
 * Shared adapter for tools that return url lists
 *
 * @param {Array<object>} items - raw items returned by the tool
 * @param {(item: object) => string} getLabel - extracts the chip label
 * @returns {Array<ActionLogChip>}
 */
function urlListChips(items, getLabel) {
  return (items ?? []).map(item => ({
    url: item.url,
    label: getLabel(item),
  }));
}

/**
 * UI metadata used by the action log - keyed by tool name
 *
 * @type {Map<string, { show: boolean, label: ActionLogLabel, pendingLabel: ActionLogLabel }>}
 */
const TOOL_ACTION_LOG_CONFIG = new Map([
  [
    GENERATE_AITAB,
    {
      label: (body, args) => {
        if (!body?.page) {
          return {
            l10nId: isAITabModification(args?.modify_slug)
              ? "smartwindow-aitab-updating-an-aitab-failed"
              : "smartwindow-aitab-creating-an-aitab-failed",
          };
        }
        return {
          l10nId: isAITabModification(args?.modify_slug)
            ? "smartwindow-aitab-updated-an-aitab"
            : "smartwindow-aitab-created-an-aitab",
        };
      },
      pendingLabel: args => ({
        l10nId: isAITabModification(args?.modify_slug)
          ? "smartwindow-aitab-updating"
          : "smartwindow-aitab-creating",
      }),
    },
  ],
  [
    GET_OPEN_TABS,
    {
      label: { l10nId: "action-log-searched-open-tabs" },
      pendingLabel: { l10nId: "action-log-searching-tabs" },
    },
  ],
  [
    SEARCH_BROWSING_HISTORY,
    {
      label: { l10nId: "action-log-searched-history" },
      pendingLabel: { l10nId: "action-log-searching-history" },
    },
  ],
  [
    GET_PAGE_CONTENT,
    {
      label: { l10nId: "action-log-read-page" },
      pendingLabel: { l10nId: "action-log-reading-page" },
    },
  ],
  [
    RUN_SEARCH,
    {
      label: { l10nId: "action-log-searched-web" },
      pendingLabel: { l10nId: "action-log-searching-web" },
    },
  ],
  [
    SEARCH_THE_WEB,
    {
      label: body => ({
        l10nId: body?.directAnswer
          ? "action-log-searched-and-answered-with-exa"
          : "action-log-searched-web-with-exa",
      }),
      pendingLabel: { l10nId: "action-log-searching-web-with-exa" },
      link: {
        l10nName: "exa-link",
        supportPage:
          "smart-window?as=u&utm_source=inproduct#w_the-assistant-may-search-the-web",
      },
    },
  ],
  [
    GET_USER_MEMORIES,
    {
      label: { l10nId: "action-log-checked-memories" },
      pendingLabel: { l10nId: "action-log-checking-memories" },
    },
  ],
  [
    GET_NAVIGATION_INFO,
    {
      label: { l10nId: "action-log-searched-settings" },
      pendingLabel: { l10nId: "action-log-searching-settings" },
    },
  ],
]);

/**
 * Per tool row adapters. Each takes a tool result body and returns
 * an array of rows for <ai-action-result>
 *
 * @type {Map<string, (body: object) => Array<ActionLogChip>>}
 */
const TOOL_RESULT_TO_CHIPS = new Map([
  [GET_OPEN_TABS, body => urlListChips(body, tab => tab.title)],
  [
    SEARCH_BROWSING_HISTORY,
    body => urlListChips(body?.results, result => result.title),
  ],
]);

function resolveSupportUrl(supportPage) {
  return (
    Services.urlFormatter.formatURLPref("app.support.baseURL") + supportPage
  );
}

/**
 * Look up the action log UI config for a given tool. Tools without an entry
 * default to suppressed (show: false)
 *
 * @param {string} toolName
 * @param {object} [body] - tool result body. A `pending` body means the tool
 *   is still running, so the in-progress label is used for the row.
 * @param {object} [args] - parsed tool call arguments
 * @returns {{ show: boolean, label: ActionLogLabel | null, pendingLabel: ActionLogLabel | null, link: ActionLogLabelLink | null }}
 */
export function getActionLogConfigForTool(toolName, body, args) {
  const cfg = TOOL_ACTION_LOG_CONFIG.get(toolName);
  if (!cfg) {
    return { show: false, label: null, pendingLabel: null, link: null };
  }
  const completedLabel =
    typeof cfg.label === "function" ? cfg.label(body, args) : cfg.label;
  const link = cfg.link
    ? {
        l10nName: cfg.link.l10nName,
        href: resolveSupportUrl(cfg.link.supportPage),
      }
    : null;
  let pendingLabel =
    typeof cfg.pendingLabel === "function"
      ? cfg.pendingLabel(args)
      : (cfg.pendingLabel ?? null);
  if (pendingLabel && link) {
    pendingLabel = { ...pendingLabel, link };
  }
  // A still-running tool carries a `pending` placeholder body; show its
  // in-progress label on the row rather than the completed one.
  const label = body?.pending
    ? (pendingLabel ?? completedLabel)
    : completedLabel;
  return {
    show: true,
    label,
    pendingLabel,
    link,
  };
}

/**
 * Look up the action log website chips for a given tool
 *
 * @param {string} toolName
 * @param {object} body - tool result body
 * @param {object} [args] - parsed tool call args (URL tokens already expanded)
 * @returns {Array<ActionLogChip>}
 */
export function getActionLogChipsForTool(toolName, body, args) {
  const adapter = TOOL_RESULT_TO_CHIPS.get(toolName);
  return adapter ? (adapter(body, args) ?? EMPTY_ROWS) : EMPTY_ROWS;
}

/**
 * Build a single action log row in the shape <ai-action-result>
 * The renderer collects the rows from a turn's tool messages
 * into a single grouped card
 *
 * @param {string} toolName
 * @param {ActionLogLabel | string} label
 * @param {object} body - tool result body
 * @param {object} [args] - parsed tool call args
 * @param {ActionLogLabelLink} [link] - optional link embedded in the label
 * @returns {{ labelL10nId?: string, labelL10nArgs?: object, label?: string, link?: ActionLogLabelLink, items: Array<ActionLogChip> }}
 */
export function buildActionLogRow(toolName, label, body, args, link) {
  const row = { items: getActionLogChipsForTool(toolName, body, args) };

  if (link?.href) {
    row.link = link;
  }

  if (label && typeof label === "object" && label.l10nId) {
    row.labelL10nId = label.l10nId;
    row.labelL10nArgs = label.l10nArgs;
  } else {
    row.label = typeof label === "string" ? label : "";
  }

  return row;
}
