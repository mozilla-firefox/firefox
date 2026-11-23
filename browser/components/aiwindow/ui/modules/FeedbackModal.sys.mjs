/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  ASRouter: "resource:///modules/asrouter/ASRouter.sys.mjs",
  Spotlight: "resource:///modules/asrouter/Spotlight.sys.mjs",
  normalizeChatLog:
    "moz-src:///browser/components/aiwindow/ui/modules/ChatUtils.sys.mjs",
});

export const FeedbackModal = {
  /**
   * @param {MozBrowser} browser
   * @param {string} type - "thumbs-up" or "thumbs-down".
   * @param {object} metadata - Chat session data to include in telemetry.
   */
  async open(browser, type, metadata) {
    await lazy.ASRouter.waitForInitialized;
    const message = await lazy.ASRouter.handleMessageRequest({
      triggerId: "feedbackThumbClick",
      triggerParam: { type },
      template: "spotlight",
    });
    if (!message) {
      return;
    }
    const clonedMessage = structuredClone(message);
    if (metadata) {
      clonedMessage.content.feedbackData = {
        metadata: metadata.metadata,
        chat: metadata.chatLog,
        chatWithoutPageContent: metadata.chatLogWithoutPageContent,
      };

      for (const screen of clonedMessage.content.screens ?? []) {
        const { tiles } = screen.content ?? {};
        const textboxTile = tiles?.find(t => t.type === "textbox");
        const contentToggleTile = tiles?.find(t => t.type === "content-toggle");

        if (textboxTile && metadata.chatLog) {
          textboxTile.data.content = JSON.stringify(
            {
              metadata: metadata.metadata,
              ...lazy.normalizeChatLog(metadata.chatLog),
            },
            null,
            2
          );
          if (textboxTile.header?.title) {
            textboxTile.header.title.string_id =
              metadata.chatLogWithoutPageContent
                ? "aiwindow-feedback-preview-report-with-page"
                : "aiwindow-feedback-preview-report";
          }
          if (contentToggleTile) {
            contentToggleTile.data.visible =
              !!metadata.chatLogWithoutPageContent;
          }
          if (metadata.chatLogWithoutPageContent) {
            textboxTile.data.alternateContent = JSON.stringify(
              {
                metadata: metadata.metadata,
                ...lazy.normalizeChatLog(metadata.chatLogWithoutPageContent),
              },
              null,
              2
            );
          }
        }
      }
    }
    try {
      await lazy.Spotlight.showSpotlightDialog(browser, clonedMessage);
    } catch (e) {
      console.error("Failed to open feedback modal", e);
    }
  },
};
