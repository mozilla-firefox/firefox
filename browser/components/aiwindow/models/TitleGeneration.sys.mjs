/**
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

import {
  renderPrompt,
  MODEL_FEATURES,
} from "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs";
import { AuthProvider } from "moz-src:///browser/components/aiwindow/models/AuthProvider.sys.mjs";
import { sanitizeUntrustedContent } from "moz-src:///browser/components/aiwindow/models/ChatUtils.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  buildConversation:
    "moz-src:///browser/components/aiwindow/models/PromptLoader.sys.mjs",
  loadPrompt:
    "moz-src:///browser/components/aiwindow/models/PromptLoader.sys.mjs",
});

/**
 * Generate a default title from the first four words of a message.
 *
 * @param {string} message - The user's message
 * @returns {string} The default title
 */
function generateDefaultTitle(message) {
  if (!message || typeof message !== "string") {
    return "New Chat";
  }

  const words = message
    .trim()
    .split(/\s+/)
    .filter(word => !!word.length);

  if (words.length === 0) {
    return "New Chat";
  }

  const titleWords = words.slice(0, 4);
  return titleWords.join(" ") + "...";
}

/**
 * Generate a chat title based on the user's message, current tab information,
 * and optionally the first assistant response.
 *
 * @param {string} message - The user's message
 * @param {object} current_tab - Object containing current tab information
 * @param {string} [assistantResponse] - The first assistant response
 * @param {string | null} [flowId] - Flow ID for correlating with firefox_ai_runtime telemetry
 * @returns {Promise<string>} The generated chat title
 */
export async function generateChatTitle(
  message,
  current_tab,
  assistantResponse,
  flowId = null
) {
  try {
    const [conversation, { prompt: rawPrompt }] = await Promise.all([
      lazy.buildConversation(MODEL_FEATURES.TITLE_GENERATION, { flowId }),
      lazy.loadPrompt(MODEL_FEATURES.TITLE_GENERATION),
    ]);

    const tabInfo = current_tab || { url: "", title: "", description: "" };
    tabInfo.title = sanitizeUntrustedContent(tabInfo.title);

    // The first assistant reply is context for the prompt, not a turn to
    // continue: some providers (e.g. Mistral) reject a conversation that ends
    // on an assistant message, which made title generation fall back to the
    // default title. Its framing lives in the Remote Settings prompt.
    conversation.setSystemMessage(
      renderPrompt(rawPrompt, {
        current_tab: JSON.stringify(tabInfo),
        assistant_response: assistantResponse,
      })
    );
    conversation.addUserMessage(message);

    const response = await conversation.run({
      token: await AuthProvider.getToken(),
    });

    return response?.finalOutput?.trim() || generateDefaultTitle(message);
  } catch (error) {
    console.error("Failed to generate chat title:", error);
    return generateDefaultTitle(message);
  }
}
