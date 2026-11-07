/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";
import { serializeClientErrorDetail } from "chrome://browser/content/aiwindow/modules/ClientErrorTelemetry.mjs";

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {});

XPCOMUtils.defineLazyServiceGetter(
  lazy,
  "ClipboardHelper",
  "@mozilla.org/widget/clipboardhelper;1",
  Ci.nsIClipboardHelper
);

/**
 * Represents a child actor for getting page data from the browser.
 */
export class AIChatContentChild extends JSWindowActorChild {
  static #EVENT_MAPPINGS_FROM_PARENT = {
    "AIChatContent:DispatchMessage": {
      event: "aiChatContentActor:message",
    },
    "AIChatContent:TruncateConversation": {
      event: "aiChatContentActor:truncate",
    },
    "AIChatContent:RemoveAppliedMemory": {
      event: "aiChatContentActor:remove-applied-memory",
    },
    "AIChatContent:SeenUrls": {
      event: "aiChatContentActor:seen-urls",
    },
    "AIChatContent:SetGenerating": {
      event: "aiChatContentActor:set-generating",
    },
    "AIChatContent:AssetsReady": {
      event: "aiChatContentActor:assets-ready",
    },
    "AIChatContent:SetMode": {
      event: "aiChatContentActor:set-mode",
    },
  };

  static #VALID_EVENTS_FROM_CONTENT = new Set([
    "AIChatContent:DispatchFollowUp",
    "AIChatContent:Ready",
    "AIChatContent:DispatchAction",
    "AIChatContent:OpenLink",
    "AIChatContent:OpenTabGroup",
    "AIChatContent:DispatchNewChat",
    "AIChatContent:AccountSignIn",
    "AIChatContent:ToolUIUpdate",
    "AIChatContent:RequestAssets",
    "AIChatContent:HistoryGridRender",
    "AIChatContent:HistoryGridItemClick",
    "AIChatContent:FirstCharacterDisplayed",
    "AIChatContent:ClientError",
  ]);

  /**
   *  Receives event from the content process and sends to the parent.
   *
   * @param {CustomEvent} event
   */
  handleEvent(event) {
    if (!AIChatContentChild.#VALID_EVENTS_FROM_CONTENT.has(event.type)) {
      console.warn(`AIChatContentChild received unknown event: ${event.type}`);
      return;
    }

    const { action, text } = event.detail ?? {};
    const copyActions = ["copy", "copy-table"];
    const isCopyAction = copyActions.includes(action) && text;

    switch (event.type) {
      case "AIChatContent:DispatchAction":
        // Copy is handled in the child actor since it depends on content-side
        // selection and clipboard context.
        if (isCopyAction) {
          lazy.ClipboardHelper.copyString(text, this.windowContext);
        }

        this.sendAsyncMessage(event.type, event.detail);
        break;

      // Relay known events to AIChatContentParent
      default:
        this.sendAsyncMessage(event.type, event.detail);
    }
  }

  async receiveMessage(message) {
    const mapping =
      AIChatContentChild.#EVENT_MAPPINGS_FROM_PARENT[message.name];

    if (!mapping) {
      console.warn(
        `AIChatContentChild received unknown message: ${message.name}`
      );
      return undefined;
    }

    const payload = message.data;
    return this.#dispatchToChatContent(mapping.event, payload);
  }

  #dispatchToChatContent(eventName, payload) {
    try {
      const chatContent = this.document.querySelector("ai-chat-content");

      if (!chatContent) {
        console.error(`No ai-chat-content element found for ${eventName}`);
        return false;
      }

      const clonedPayload = Cu.cloneInto(payload, this.contentWindow);

      const event = new this.contentWindow.CustomEvent(eventName, {
        detail: clonedPayload,
        bubbles: true,
      });

      chatContent.dispatchEvent(event);
      return true;
    } catch (error) {
      console.error(`Error dispatching ${eventName} to chat content:`, error);
      this.#reportDispatchFailure(error);
      return false;
    }
  }

  #reportDispatchFailure(error) {
    try {
      this.sendAsyncMessage(
        "AIChatContent:ClientError",
        serializeClientErrorDetail(
          error,
          "message-data",
          "message_dispatch_failed"
        )
      );
    } catch (e) {
      console.warn("Could not report dispatch failure:", e);
    }
  }
}
