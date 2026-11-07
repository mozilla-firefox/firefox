/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {
  createSelector,
} = require("resource://devtools/client/shared/vendor/reselect.js");

/**
 * Returns list of messages that are visible to the user.
 * Filtered messages by types and text are factored in.
 */
const getDisplayedMessages = createSelector(
  state => state.messages,
  ({
    messages,
    messageFilterType,
    showControlFrames,
    messageFilterText,
    currentChannelId,
  }) => {
    if (!currentChannelId || !messages.get(currentChannelId)) {
      return [];
    }

    const messagesArray = messages.get(currentChannelId);
    if (messageFilterType === "all" && messageFilterText.length === 0) {
      return messagesArray.filter(message =>
        typeFilter(message, messageFilterType, showControlFrames)
      );
    }

    const filter = searchFilter(messageFilterText);

    // If message payload is > 10,000 characters long, we check the LongStringActor payload string
    return messagesArray.filter(
      message =>
        (message.payload.initial
          ? filter(message.payload.initial)
          : filter(message.payload)) &&
        typeFilter(message, messageFilterType, showControlFrames)
    );
  }
);

function typeFilter(message, messageFilterType, showControlFrames) {
  const controlFrames = [0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf];
  const isControlFrame = controlFrames.includes(message.opCode);
  if (messageFilterType === "all" || messageFilterType === message.type) {
    return showControlFrames || !isControlFrame;
  }
  return false;
}

function searchFilter(messageFilterText) {
  let regex;
  if (looksLikeRegex(messageFilterText)) {
    try {
      regex = regexFromText(messageFilterText);
    } catch (e) {}
  }

  return regex
    ? payload => regex.test(payload)
    : payload => payload.includes(messageFilterText);
}

function looksLikeRegex(text) {
  return text.startsWith("/") && text.endsWith("/") && text.length > 2;
}

function regexFromText(text) {
  return new RegExp(text.slice(1, -1), "im");
}

/**
 * Checks if the selected message is visible.
 * If the selected message is not visible, the SplitBox component
 * should not show the MessagePayload component.
 */
const isSelectedMessageVisible = createSelector(
  state => state.messages,
  getDisplayedMessages,
  ({ selectedMessage }, displayedMessages) =>
    displayedMessages.some(message => message === selectedMessage)
);

/**
 * Returns the current selected message.
 */
const getSelectedMessage = createSelector(
  state => state.messages,
  ({ selectedMessage }) => (selectedMessage ? selectedMessage : undefined)
);

/**
 * Returns summary data of the list of messages that are visible to the user.
 * Filtered messages by types and text are factored in.
 */
const getDisplayedMessagesSummary = createSelector(
  getDisplayedMessages,
  displayedMessages => {
    let firstStartedMs = +Infinity;
    let lastEndedMs = -Infinity;
    let sentSize = 0;
    let receivedSize = 0;
    let totalSize = 0;

    displayedMessages.forEach(message => {
      const size = message.payloadSize ?? message.payload.length;
      if (message.type == "received") {
        receivedSize += size;
      } else if (message.type == "sent") {
        sentSize += size;
      }
      totalSize += size;
      if (message.timeStamp < firstStartedMs) {
        firstStartedMs = message.timeStamp;
      }
      if (message.timeStamp > lastEndedMs) {
        lastEndedMs = message.timeStamp;
      }
    });

    return {
      count: displayedMessages.length,
      totalMs: (lastEndedMs - firstStartedMs) / 1000,
      sentSize,
      receivedSize,
      totalSize,
    };
  }
);

/**
 * Returns if the currentChannelId is closed
 */
const isCurrentChannelClosed = createSelector(
  state => state.messages,
  ({ closedConnections, currentChannelId }) =>
    closedConnections.has(currentChannelId)
);

/**
 * Returns the closed connection details of the currentChannelId
 * Null, if the connection is still open
 */
const getClosedConnectionDetails = createSelector(
  state => state.messages,
  ({ closedConnections, currentChannelId }) =>
    closedConnections.get(currentChannelId)
);

module.exports = {
  getSelectedMessage,
  isSelectedMessageVisible,
  getDisplayedMessages,
  getDisplayedMessagesSummary,
  isCurrentChannelClosed,
  getClosedConnectionDetails,
};
