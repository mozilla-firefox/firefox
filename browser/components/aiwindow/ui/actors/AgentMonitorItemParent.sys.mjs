/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  formatChatHistoryForSuggestion:
    "moz-src:///browser/components/aiwindow/ui/modules/MonitorUIUtils.sys.mjs",
  isAllowedWatchUrl:
    "moz-src:///browser/components/aiwindow/models/agents/Monitor.sys.mjs",
  TOTAL_NUM_URLS_IN_MONITOR:
    "moz-src:///browser/components/aiwindow/models/agents/Monitor.sys.mjs",
  MonitorUIUtils:
    "moz-src:///browser/components/aiwindow/ui/modules/MonitorUIUtils.sys.mjs",
});

const CHAT_PAGE_URL = "about:aichatcontent";

/**
 * Resolves privileged data for agent-monitor-item cards on behalf of
 * AgentMonitorItemChild.
 */
export class AgentMonitorItemParent extends JSWindowActorParent {
  receiveMessage({ name, data }) {
    if (name === "AgentMonitorItem:ResolveTitles") {
      // The child is content, so only look up as many watchable pages as one
      // monitor can hold
      const urls = (Array.isArray(data?.urls) ? data.urls : [])
        .filter(url => typeof url === "string" && lazy.isAllowedWatchUrl(url))
        .slice(0, lazy.TOTAL_NUM_URLS_IN_MONITOR);
      return lazy.MonitorUIUtils.resolveWatchUrlTitles(urls);
    }
    if (name === "AgentMonitorItem:Suggest") {
      // Only the card in chat offers suggestions. The Tasks page shares this
      // actor for page titles and gets no answer here.
      if (!this.manager.documentURI?.spec.startsWith(CHAT_PAGE_URL)) {
        return null;
      }
      const aiWindow = this.#getAIWindow();
      return lazy.MonitorUIUtils.suggestMonitorSetup({
        watchUrls: Array.isArray(data?.watchUrls) ? data.watchUrls : [],
        chatHistory: this.#readChat(
          aiWindow,
          chat => lazy.formatChatHistoryForSuggestion(chat?.conversation),
          ""
        ),
        previousDrafts: Array.isArray(data?.previousDrafts)
          ? data.previousDrafts
          : [],
        // Follows the memories switch as the next chat message would. When in
        // doubt, no memories.
        useMemories: this.#readChat(
          aiWindow,
          chat => chat?.memoriesEnabled === true,
          false
        ),
      });
    }
    console.warn(`AgentMonitorItemParent received unknown message: ${name}`);
    return undefined;
  }

  /**
   * The ai-window of the Smart Window this card is shown in. The lookup
   * matches AIChatContentParent's.
   *
   * @returns {?HTMLElement} null when it can't be found
   */
  #getAIWindow() {
    try {
      const browser = this.browsingContext.top.embedderElement;
      const root = browser?.getRootNode?.();
      const aiWindow =
        root?.host?.localName === "ai-window"
          ? root.host
          : browser?.ownerDocument?.querySelector("ai-window");
      return aiWindow ?? null;
    } catch (error) {
      console.warn("AgentMonitorItemParent: could not find the chat", error);
      return null;
    }
  }

  /**
   * @template T
   * @param {?HTMLElement} aiWindow
   * @param {function(?HTMLElement): T} read
   * @param {T} fallback - What to go with when the chat can't be read
   * @returns {T}
   */
  #readChat(aiWindow, read, fallback) {
    try {
      return read(aiWindow);
    } catch (error) {
      console.warn("AgentMonitorItemParent: could not read the chat", error);
      return fallback;
    }
  }
}
