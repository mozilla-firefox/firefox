/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { AIWindow } from "moz-src:///browser/components/aiwindow/ui/modules/AIWindow.sys.mjs";
import { AIWindowUI } from "moz-src:///browser/components/aiwindow/ui/modules/AIWindowUI.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  BrowserUtils: "resource://gre/modules/BrowserUtils.sys.mjs",
});

const MAX_RECENT_CHATS = 4;

/**
 * Adds the AI Window menuitems to the app menu
 */
export class AIWindowMenu {
  static canShowAITabMenu(win, uris) {
    return (
      Services.prefs.getBoolPref("browser.smartwindow.aitab.menus", false) &&
      Services.prefs.getBoolPref("browser.smartwindow.aitab.enabled", false) &&
      AIWindow.isAIWindowActiveAndEnabled(win) &&
      uris.some(uri => uri.schemeIs("http") || uri.schemeIs("https"))
    );
  }

  constructor() {}

  /**
   * Adds the AI menu bar menuitems
   *
   * @param {Event} event - Event from clicking the History app menu item
   * @param {Window} win - Window reference
   */
  async addMenuitems(event, win) {
    this.#addChatsMenuitem(win, event.target);
    await this.#addRecentChats(win, event.target);
  }

  #addChatsMenuitem(win, menu) {
    this.#removeChatsMenuitem(menu);

    if (!AIWindow.isAIWindowActiveAndEnabled(win)) {
      return;
    }

    this.#addChatsMenuitemToHistory(menu);
  }

  #removeChatsMenuitem(menu) {
    const chatsMenuitem = menu.querySelector("#chatsHistoryMenu");
    chatsMenuitem.hidden = true;
  }

  #addChatsMenuitemToHistory(menu) {
    const chatsMenuitem = menu.querySelector("#chatsHistoryMenu");
    chatsMenuitem.hidden = false;
  }

  async #addRecentChats(win, menu) {
    this.#removeChatsMenuitems(menu);

    if (!AIWindow.isAIWindowActiveAndEnabled(win)) {
      return;
    }

    const items =
      await AIWindow.chatStore.findRecentConversations(MAX_RECENT_CHATS);

    if (!items.length) {
      return;
    }

    this.#addRecentChatsMenuitemHeader(menu);
    this.#addRecentChatMenuitems(items, win);
  }

  #removeChatsMenuitems(menu) {
    const separator = menu.querySelector("#startChatHistorySeparator");
    separator.hidden = true;

    const startingElement = menu.querySelector("#recentChatsHeader");
    startingElement.hidden = true;

    let next = startingElement?.nextElementSibling;

    while (next && next.hasAttribute && next.hasAttribute("data-conv-id")) {
      const toRemove = next;
      next = next.nextSibling;
      toRemove.remove();
    }
  }

  #addRecentChatsMenuitemHeader(menu) {
    const separator = menu.querySelector("#startChatHistorySeparator");
    separator.hidden = false;

    const chatsHeader = menu.querySelector("#recentChatsHeader");
    chatsHeader.hidden = false;
  }

  #addRecentChatMenuitems(items, win) {
    const document = win.document;
    const chatsHeader = document.getElementById("recentChatsHeader");

    while (items.length) {
      const item = items.pop();
      const menuItem = document.createXULElement("menuitem");
      menuItem.classList.add("recent-chat-item");
      menuItem.setAttribute("label", item.title);
      menuItem.setAttribute("data-conv-id", item.id);
      menuItem.addEventListener("command", this.#onRecentChatMenuitemClick);

      chatsHeader.insertAdjacentElement("afterend", menuItem);
    }
  }

  async #onRecentChatMenuitemClick(event) {
    const convId = event.target.getAttribute("data-conv-id");
    const conversation = await AIWindow.chatStore.findConversationById(convId);

    if (!conversation) {
      return;
    }

    let where = lazy.BrowserUtils.whereToOpenLink(event);
    if (where === "current") {
      where = "tab";
    }

    const win = event.target.documentGlobal;
    AIWindowUI.reopenConversationInTab(win, conversation, where);
  }
}
