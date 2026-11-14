/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Defines the agent commands that may be triggered when a slash is typed.
 * Commands listed here will be available in the palette, but the AgentUI will
 * still need to register handlers.
 */

/**
 * Command ids, as they are typed without the leading "/"
 *
 * "watch" is the user-facing name for this agent command and in code it's
 * called Monitor.
 */
export const AGENT_COMMANDS = Object.freeze({
  WATCH: "watch",
  RESEARCH: "research", //not in use
  SCHEDULE: "schedule", //not in use
});

/**
 * @typedef {object} AgentCommandItem
 * @property {string} l10nId - Palette label
 * @property {string} descriptionL10nId - Palette description
 * @property {string} icon - Palette icon
 */

/**
 * Palette metadata per command, keyed by its id, in the order the palette
 * lists them
 *
 * @type {Map<Values<typeof AGENT_COMMANDS>, AgentCommandItem>}
 */
export const AGENT_COMMAND_ITEMS = new Map([
  [
    AGENT_COMMANDS.WATCH,
    {
      l10nId: "smartbar-command-watch-label",
      descriptionL10nId: "smartbar-command-watch-description",
      icon: "chrome://browser/content/aiwindow/assets/agent-watch.svg",
    },
  ],
]);

// A leading "/" plus the command id, with the rest of the input as the prompt
const COMMAND_REGEX = /^\/(\w+)\b\s*(.*)$/s;

/**
 * @typedef {object} ParsedAgentCommand
 * @property {Values<typeof AGENT_COMMANDS>} command - The lowercased command id, without the leading "/"
 * @property {string} prompt - The text following the command
 * @property {string} raw - The trimmed input the command was parsed from
 */

/**
 * Parses a leading command keyword from raw smartbar input
 *
 * @param {string} value - Raw smartbar input
 * @returns {?ParsedAgentCommand} Null when there is no leading command
 */
export function parseAgentCommand(value) {
  const raw = value.trim();
  const match = COMMAND_REGEX.exec(raw);
  if (!match) {
    return null;
  }
  return {
    command: /** @type {Values<typeof AGENT_COMMANDS>} */ (
      match[1].toLowerCase()
    ),
    prompt: match[2].trim(),
    raw,
  };
}
