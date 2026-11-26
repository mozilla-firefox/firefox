/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

const {
  isGroupType,
  isMessageNetworkError,
  l10n,
} = require("resource://devtools/client/webconsole/utils/messages.js");

const constants = require("resource://devtools/client/webconsole/constants.js");
const { DEFAULT_FILTERS, FILTERS, MESSAGE_TYPE, MESSAGE_SOURCE } = constants;

const { getGripPreviewItems } = ChromeUtils.importESModule(
  "resource://devtools/client/shared/components/reps/index.mjs",
  { global: "current" }
);
loader.lazyRequireGetter(
  this,
  "getUnicodeUrlPath",
  "resource://devtools/client/shared/unicode-url.js",
  true
);
loader.lazyRequireGetter(
  this,
  "getSourceNames",
  "resource://devtools/client/shared/source-utils.js",
  true
);
loader.lazyRequireGetter(
  this,
  [
    "areMessagesSimilar",
    "createWarningGroupMessage",
    "isWarningGroup",
    "getWarningGroupType",
    "getDescriptorValue",
    "getParentWarningGroupMessageId",
    "getNaturalOrder",
  ],
  "resource://devtools/client/webconsole/utils/messages.js",
  true
);

const {
  UPDATE_REQUEST,
} = require("resource://devtools/client/netmonitor/src/constants.js");

const {
  processNetworkUpdates,
} = require("resource://devtools/client/netmonitor/src/utils/request-utils.js");

const MessageState = overrides =>
  Object.freeze(
    Object.assign(
      {
        // List of all the messages added to the console. Unlike other properties, this Map
        // will be mutated on state changes for performance reasons.
        mutableMessagesById: new Map(),
        // Array of message ids, in chronological order. We use a dedicated property to store
        // the order (instead of relying on the order of insertion in mutableMessagesById)
        // as we might receive messages that need to be inserted at a specific index. Doing
        // so on the Map can be costly, especially when the Map holds lots of messages.
        mutableMessagesOrder: [],
        // List of elements matching the selector of CSS Warning messages(populated
        // on-demand via the UI).
        cssMessagesMatchingElements: new Map(),
        // Array of the visible messages.
        visibleMessages: [],
        // Object for the filtered messages.
        filteredMessagesCount: getDefaultFiltersCounter(),
        // List of the message ids which are opened.
        messagesUiById: [],
        // Map of the form {groupMessageId : groupArray},
        // where groupArray is the list of of all the parent groups' ids of the groupMessageId.
        // This handles console API groups.
        groupsById: new Map(),
        // Message id of the current console API group (no corresponding console.groupEnd yet).
        currentGroup: null,
        // This group handles "warning groups" (Content Blocking, CORS, CSP, …)
        warningGroupsById: new Map(),
        // Array of fronts to release (i.e. fronts logged in removed messages).
        // This array *should not* be consumed by any UI component.
        frontsToRelease: [],
        // Map of the form {messageId : numberOfRepeat}
        repeatById: {},
        // Map of the form {messageId : networkInformation}
        // `networkInformation` holds request, response, totalTime, ...
        networkMessagesUpdateById: {},
        // Id of the last messages that was added.
        lastMessageId: null,
        // List of the message ids which are disabled
        disabledMessagesById: [],
      },
      overrides
    )
  );

function cloneState(state) {
  return {
    visibleMessages: [...state.visibleMessages],
    filteredMessagesCount: { ...state.filteredMessagesCount },
    messagesUiById: [...state.messagesUiById],
    cssMessagesMatchingElements: new Map(state.cssMessagesMatchingElements),
    groupsById: new Map(state.groupsById),
    frontsToRelease: [...state.frontsToRelease],
    repeatById: { ...state.repeatById },
    networkMessagesUpdateById: { ...state.networkMessagesUpdateById },
    warningGroupsById: new Map(state.warningGroupsById),
    // no need to mutate the properties below as they're not directly triggering re-render
    mutableMessagesById: state.mutableMessagesById,
    mutableMessagesOrder: state.mutableMessagesOrder,
    currentGroup: state.currentGroup,
    lastMessageId: state.lastMessageId,
    disabledMessagesById: [...state.disabledMessagesById],
  };
}

/**
 * Add a console message to the state.
 *
 * @param {ConsoleMessage} newMessage: The message to add to the state.
 * @param {MessageState} state: The message state ( = managed by this reducer).
 * @param {FiltersState} filtersState: The filters state.
 * @param {PrefsState} prefsState: The preferences state.
 * @param {UiState} uiState: The ui state.
 * @returns {MessageState} a new messages state.
 */
// eslint-disable-next-line complexity
function addMessage(newMessage, state, filtersState, prefsState, uiState) {
  const { mutableMessagesById, groupsById, repeatById } = state;

  if (newMessage.type === constants.MESSAGE_TYPE.NAVIGATION_MARKER) {
    // We set the state's currentGroup property to null after navigating
    state.currentGroup = null;
  }
  const { currentGroup } = state;

  if (newMessage.type === constants.MESSAGE_TYPE.NULL_MESSAGE) {
    // When the message has a NULL type, we don't add it.
    return state;
  }

  if (newMessage.type === constants.MESSAGE_TYPE.END_GROUP) {
    // Compute the new current group.
    state.currentGroup = getNewCurrentGroup(currentGroup, groupsById);
    return state;
  }

  const lastMessage = mutableMessagesById.get(state.lastMessageId);
  // It can happen that the new message was actually emitted earlier than the last message,
  // which means we need to insert it at the right position.
  const isUnsorted =
    lastMessage && lastMessage.timeStamp > newMessage.timeStamp;

  if (lastMessage && mutableMessagesById.size > 0) {
    if (
      // only repeat messages if the group similar messages pref is enabled
      prefsState.groupSimilar &&
      lastMessage.groupId === currentGroup &&
      areMessagesSimilar(lastMessage, newMessage)
    ) {
      state.repeatById[lastMessage.id] = (repeatById[lastMessage.id] || 1) + 1;
      return state;
    }
  }

  // Store the id of the message as being the last one being added.
  if (!isUnsorted) {
    state.lastMessageId = newMessage.id;
  }

  // Add the new message with a reference to the parent group.
  const parentGroups = getParentGroups(currentGroup, groupsById);
  if (!isWarningGroup(newMessage)) {
    newMessage.groupId = currentGroup;
    newMessage.indent = parentGroups.length;
  }

  // Check if the current message could be placed in a Warning Group.
  // This needs to be done before setting the new message in mutableMessagesById so we have a
  // proper message.
  const warningGroupType = getWarningGroupType(newMessage);

  // If the preference for grouping is true, and the new message could be in a warning group.
  if (prefsState.groupSimilar && warningGroupType !== null) {
    const warningGroupMessageId = getParentWarningGroupMessageId(newMessage);

    // If there's no warning group for the type/innerWindowID yet
    if (!state.mutableMessagesById.has(warningGroupMessageId)) {
      // We create it and add it to the store.
      const groupMessage = createWarningGroupMessage(
        warningGroupMessageId,
        warningGroupType,
        newMessage
      );
      state = addMessage(
        groupMessage,
        state,
        filtersState,
        prefsState,
        uiState
      );
    }

    // We add the new message to the appropriate warningGroup.
    state.warningGroupsById.get(warningGroupMessageId).push(newMessage.id);

    // If the warningGroup message is not visible yet, but should be.
    if (
      !state.visibleMessages.includes(warningGroupMessageId) &&
      getMessageVisibility(
        state.mutableMessagesById.get(warningGroupMessageId),
        {
          messagesState: state,
          filtersState,
          prefsState,
          uiState,
        }
      ).visible
    ) {
      // Then we put it in the visibleMessages properties, at the position of the first
      // warning message inside the warningGroup.
      // If that first warning message is in a console.group, we place it before the
      // outermost console.group message.
      const firstWarningMessageId = state.warningGroupsById.get(
        warningGroupMessageId
      )[0];
      const firstWarningMessage = state.mutableMessagesById.get(
        firstWarningMessageId
      );
      const outermostGroupId = getOutermostGroup(
        firstWarningMessage,
        groupsById
      );
      const groupIndex = state.visibleMessages.indexOf(outermostGroupId);
      const warningMessageIndex = state.visibleMessages.indexOf(
        firstWarningMessageId
      );

      if (groupIndex > -1) {
        // We remove the warning message
        if (warningMessageIndex > -1) {
          state.visibleMessages.splice(warningMessageIndex, 1);
        }

        // And we put the warning group before the console.group
        state.visibleMessages.splice(groupIndex, 0, warningGroupMessageId);
      } else {
        // If the warning message is not in a console.group, we replace it by the
        // warning group message.
        state.visibleMessages.splice(
          warningMessageIndex,
          1,
          warningGroupMessageId
        );
      }
    }
  }

  // If we're creating a warningGroup, we init the array for its children.
  if (isWarningGroup(newMessage)) {
    state.warningGroupsById.set(newMessage.id, []);
  }

  const addedMessage = Object.freeze(newMessage);

  // If the new message isn't the "oldest" one, then we need to insert it at the right
  // position in the message map.
  if (isUnsorted) {
    let newMessageIndex = 0;
    // This is can be on a hot path, so we're not using `findIndex`, which could be slow.
    // Furthermore, there's a high chance the message beed to be inserted somewhere at the
    // end of the list, so we loop through mutableMessagesOrder in reverse order.
    for (let i = state.mutableMessagesOrder.length - 1; i >= 0; i--) {
      const message = state.mutableMessagesById.get(
        state.mutableMessagesOrder[i]
      );
      if (message.timeStamp <= addedMessage.timeStamp) {
        newMessageIndex = i + 1;
        break;
      }
    }

    state.mutableMessagesOrder.splice(newMessageIndex, 0, addedMessage.id);
  } else {
    state.mutableMessagesOrder.push(addedMessage.id);
  }
  state.mutableMessagesById.set(addedMessage.id, addedMessage);

  if (newMessage.type === "trace" || newMessage.type === "logPoint") {
    // We want the stacktrace to be open by default.
    state.messagesUiById.push(newMessage.id);
  } else if (isGroupType(newMessage.type)) {
    state.currentGroup = newMessage.id;
    state.groupsById.set(newMessage.id, parentGroups);

    if (newMessage.type === constants.MESSAGE_TYPE.START_GROUP) {
      // We want the group to be open by default.
      state.messagesUiById.push(newMessage.id);
    }
  }

  const { visible, cause } = getMessageVisibility(addedMessage, {
    messagesState: state,
    filtersState,
    prefsState,
    uiState,
  });

  if (visible) {
    // If the message is part of a visible warning group, we want to add it after the last
    // visible message of the group.
    const warningGroupId = getParentWarningGroupMessageId(newMessage);
    if (warningGroupId && state.visibleMessages.includes(warningGroupId)) {
      // Defaults to the warning group message.
      let index = state.visibleMessages.indexOf(warningGroupId);

      // We loop backward through the warning group's messages to get the latest visible
      // messages in it.
      const messagesInWarningGroup =
        state.warningGroupsById.get(warningGroupId);
      for (let i = messagesInWarningGroup.length - 1; i >= 0; i--) {
        const idx = state.visibleMessages.indexOf(messagesInWarningGroup[i]);
        if (idx > -1) {
          index = idx;
          break;
        }
      }
      // Inserts the new warning message at the wanted location "in" the warning group.
      state.visibleMessages.splice(index + 1, 0, newMessage.id);
    } else if (isUnsorted) {
      // If the new message wasn't the "oldest" one, then we need to insert its id at
      // the right position in the array.
      // This is can be on a hot path, so we're not using `findIndex`, which could be slow.
      // Furthermore, there's a high chance the message beed to be inserted somewhere at the
      // end of the list, so we loop through visibleMessages in reverse order.
      let index = 0;
      for (let i = state.visibleMessages.length - 1; i >= 0; i--) {
        const id = state.visibleMessages[i];
        if (
          state.mutableMessagesById.get(id).timeStamp <= newMessage.timeStamp
        ) {
          index = i + 1;
          break;
        }
      }
      state.visibleMessages.splice(index, 0, newMessage.id);
    } else {
      state.visibleMessages.push(newMessage.id);
    }
    maybeSortVisibleMessages(state, false);
  } else if (DEFAULT_FILTERS.includes(cause)) {
    state.filteredMessagesCount.global++;
    state.filteredMessagesCount[cause]++;
  }

  // Append received network-data also into networkMessagesUpdateById
  // that is responsible for collecting (lazy loaded) HTTP payload data.
  if (newMessage.source == "network") {
    state.networkMessagesUpdateById[newMessage.actor] = newMessage;
  }

  return state;
}

// eslint-disable-next-line complexity
function messages(
  state = MessageState(),
  action,
  filtersState,
  prefsState,
  uiState
) {
  const {
    mutableMessagesById,
    cssMessagesMatchingElements,
    messagesUiById,
    networkMessagesUpdateById,
    groupsById,
    visibleMessages,
    disabledMessagesById,
  } = state;

  const { logLimit } = prefsState;

  let newState;
  switch (action.type) {
    case constants.MESSAGES_ADD: {
      // If the action holds more messages than the log limit, we can preemptively remove
      // messages that will never be rendered.
      const batchHasMoreMessagesThanLogLimit =
        action.messages.length > logLimit;
      const list = batchHasMoreMessagesThanLogLimit ? [] : action.messages;
      if (batchHasMoreMessagesThanLogLimit) {
        let prunableCount = 0;
        let lastMessage = null;
        for (let i = action.messages.length - 1; i >= 0; i--) {
          const message = action.messages[i];
          if (
            !message.groupId &&
            !isGroupType(message.type) &&
            message.type !== MESSAGE_TYPE.END_GROUP
          ) {
            const messagesSimilar = areMessagesSimilar(lastMessage, message);
            if (!messagesSimilar) {
              prunableCount++;
            }
            // Once we've added the max number of messages that can be added, stop.
            // Except for repeated messages, where we keep adding over the limit.
            if (prunableCount <= logLimit || messagesSimilar) {
              list.unshift(action.messages[i]);
            } else {
              break;
            }
          } else {
            list.unshift(message);
          }
          lastMessage = message;
        }
      }

      newState = cloneState(state);
      for (const message of list) {
        newState = addMessage(
          message,
          newState,
          filtersState,
          prefsState,
          uiState
        );
      }

      return limitTopLevelMessageCount(newState, logLimit);
    }

    case constants.MESSAGES_CLEAR: {
      const frontsToRelease = [];
      for (const message of state.mutableMessagesById.values()) {
        // We want to minimize time spent in reducer as much as we can, so we're using
        // prototype.push.apply here as it seems faster than other solutions (e.g. the
        // spread operator, Array#concat, …)
        Array.prototype.push.apply(
          frontsToRelease,
          getAllFrontsInMessage(message)
        );
      }
      return MessageState({
        // Store all actors from removed messages. This array is used by
        // `releaseActorsEnhancer` to release all of those backend actors.
        frontsToRelease,
      });
    }

    case constants.PRIVATE_MESSAGES_CLEAR: {
      const removedIds = new Set();
      for (const [id, message] of mutableMessagesById) {
        if (message.private === true) {
          removedIds.add(id);
        }
      }

      // If there's no private messages, there's no need to change the state.
      if (removedIds.size === 0) {
        return state;
      }

      return removeMessagesFromState(
        {
          ...state,
        },
        removedIds
      );
    }

    case constants.TARGET_MESSAGES_REMOVE: {
      const removedIds = new Set();
      for (const [id, message] of mutableMessagesById) {
        // Remove message from the target but not evaluations and their results, so
        // 1. we're consistent with the filtering behavior, i.e. we never hide those
        // 2. when switching mode from multiprocess to parent process and back to multi,
        //    if we'd clear evaluations we wouldn't have a way to get them back, unlike
        //    log messages and errors, which are still available in the server caches).
        if (
          message.targetFront == action.targetFront &&
          message.type !== MESSAGE_TYPE.COMMAND &&
          message.type !== MESSAGE_TYPE.RESULT
        ) {
          removedIds.add(id);
        }
      }

      return removeMessagesFromState(
        {
          ...state,
        },
        removedIds
      );
    }

    case constants.MESSAGES_DISABLE:
      return {
        ...state,
        disabledMessagesById: [...disabledMessagesById, ...action.ids],
      };

    case constants.MESSAGE_OPEN: {
      const openState = { ...state };
      openState.messagesUiById = [...messagesUiById, action.id];
      const currMessage = mutableMessagesById.get(action.id);

      // If the message is a console.group/groupCollapsed or a warning group.
      if (isGroupType(currMessage.type) || isWarningGroup(currMessage)) {
        // We want to make its children visible
        const messagesToShow = [];
        for (const id of state.mutableMessagesOrder) {
          const message = mutableMessagesById.get(id);
          if (
            !visibleMessages.includes(message.id) &&
            ((isWarningGroup(currMessage) && !!getWarningGroupType(message)) ||
              (isGroupType(currMessage.type) &&
                getParentGroups(message.groupId, groupsById).includes(
                  action.id
                ))) &&
            getMessageVisibility(message, {
              messagesState: openState,
              filtersState,
              prefsState,
              uiState,
              // We want to check if the message is in an open group
              // only if it is not a direct child of the group we're opening.
              checkGroup: message.groupId !== action.id,
            }).visible
          ) {
            messagesToShow.push(id);
          }
        }

        // We can then insert the messages ids right after the one of the group.
        const insertIndex = visibleMessages.indexOf(action.id) + 1;
        openState.visibleMessages = [
          ...visibleMessages.slice(0, insertIndex),
          ...messagesToShow,
          ...visibleMessages.slice(insertIndex),
        ];
      }
      return openState;
    }

    case constants.MESSAGE_CLOSE: {
      const closeState = { ...state };
      const messageId = action.id;
      const index = closeState.messagesUiById.indexOf(messageId);
      closeState.messagesUiById.splice(index, 1);
      closeState.messagesUiById = [...closeState.messagesUiById];

      // If the message is a group
      if (isGroupType(mutableMessagesById.get(messageId).type)) {
        // Hide all its children, unless they're in a warningGroup.
        closeState.visibleMessages = visibleMessages.filter((id, i, arr) => {
          const message = mutableMessagesById.get(id);
          const warningGroupMessage = mutableMessagesById.get(
            getParentWarningGroupMessageId(message)
          );

          // If the message is in a warning group, then we return its current visibility.
          if (
            shouldGroupWarningMessages(
              warningGroupMessage,
              closeState,
              prefsState
            )
          ) {
            return arr.includes(id);
          }

          const parentGroups = getParentGroups(message.groupId, groupsById);
          return parentGroups.includes(messageId) === false;
        });
      } else if (isWarningGroup(mutableMessagesById.get(messageId))) {
        // If the message was a warningGroup, we hide all the messages in the group.
        const groupMessages = closeState.warningGroupsById.get(messageId);
        closeState.visibleMessages = visibleMessages.filter(
          id => !groupMessages.includes(id)
        );
      }
      return closeState;
    }

    case constants.CSS_MESSAGE_ADD_MATCHING_ELEMENTS:
      return {
        ...state,
        cssMessagesMatchingElements: new Map(cssMessagesMatchingElements).set(
          action.id,
          action.elements
        ),
      };

    case constants.NETWORK_MESSAGES_UPDATE: {
      const updatedState = {
        ...state,
        networkMessagesUpdateById: {
          ...networkMessagesUpdateById,
        },
      };
      let hasNetworkError = false;
      let hasUpdatesForMessageInState = false;
      for (const message of action.messages) {
        const { id } = message;
        // Ignore the update if the network message isn't in the map of messages
        if (!updatedState.mutableMessagesById.has(id)) {
          continue;
        }

        hasUpdatesForMessageInState = true;

        updatedState.mutableMessagesById.set(id, message);
        updatedState.networkMessagesUpdateById[id] = {
          ...(updatedState.networkMessagesUpdateById[id] || {}),
          ...message,
        };

        if (isMessageNetworkError(message)) {
          hasNetworkError = true;
        }
      }

      // if there was no actual update, let's return the original state so we don't
      // trigger a re-rendering.
      if (!hasUpdatesForMessageInState) {
        return state;
      }

      // If the message updates contained a network error, then we may have to display it.
      if (hasNetworkError) {
        return setVisibleMessages({
          messagesState: updatedState,
          filtersState,
          prefsState,
          uiState,
        });
      }

      return updatedState;
    }

    case UPDATE_REQUEST:
    case constants.NETWORK_UPDATES_REQUEST: {
      newState = {
        ...state,
        networkMessagesUpdateById: {
          ...networkMessagesUpdateById,
        },
      };

      // Netmonitor's UPDATE_REQUEST action comes for only one request
      const updates =
        action.type == UPDATE_REQUEST
          ? [{ id: action.id, data: action.data }]
          : action.updates;
      for (const { id, data } of updates) {
        const request = newState.networkMessagesUpdateById[id];
        if (!request) {
          continue;
        }

        // We do not want to overwrite priority with "undefined"
        if (!Number.isInteger(data.priority)) {
          delete data.priority;
        }

        newState.networkMessagesUpdateById[id] = {
          ...request,
          ...processNetworkUpdates(data),
        };
      }
      return newState;
    }

    case constants.FRONTS_TO_RELEASE_CLEAR:
      return {
        ...state,
        frontsToRelease: [],
      };

    case constants.GROUP_SIMILAR_MESSAGES_TOGGLE: {
      // There's no warningGroups, and the pref was set to false,
      // we don't need to do anything.
      if (!prefsState.groupSimilar && state.warningGroupsById.size === 0) {
        return state;
      }

      let needSort = false;
      for (const msgId of state.mutableMessagesOrder) {
        const message = state.mutableMessagesById.get(msgId);
        const warningGroupType = getWarningGroupType(message);
        if (warningGroupType) {
          const warningGroupMessageId = getParentWarningGroupMessageId(message);

          // If there's no warning group for the type/innerWindowID yet.
          if (!state.mutableMessagesById.has(warningGroupMessageId)) {
            // We create it and add it to the store.
            const groupMessage = createWarningGroupMessage(
              warningGroupMessageId,
              warningGroupType,
              message
            );
            state = addMessage(
              groupMessage,
              state,
              filtersState,
              prefsState,
              uiState
            );
          }

          // We add the new message to the appropriate warningGroup.
          const warningGroup = state.warningGroupsById.get(
            warningGroupMessageId
          );
          if (warningGroup && !warningGroup.includes(msgId)) {
            warningGroup.push(msgId);
          }

          needSort = true;
        }
      }

      // If we don't have any warning messages that could be in a group, we don't do
      // anything.
      if (!needSort) {
        return state;
      }

      return setVisibleMessages({
        messagesState: state,
        filtersState,
        prefsState,
        uiState,
        // If the user disabled warning groups, we want the messages to be sorted by their
        // timestamps.
        forceTimestampSort: !prefsState.groupSimilar,
      });
    }

    case constants.MESSAGE_REMOVE:
      return removeMessagesFromState(
        {
          ...state,
        },
        new Set([action.id])
      );

    case constants.FILTER_TOGGLE:
    case constants.FILTER_TEXT_SET:
    case constants.FILTERS_CLEAR:
    case constants.DEFAULT_FILTERS_RESET:
      return setVisibleMessages({
        messagesState: state,
        filtersState,
        prefsState,
        uiState,
      });
  }

  return state;
}

function setVisibleMessages({
  messagesState,
  filtersState,
  prefsState,
  uiState,
  forceTimestampSort = false,
}) {
  const {
    mutableMessagesById,
    mutableMessagesOrder,
    visibleMessages,
    messagesUiById,
  } = messagesState;

  const messagesToShow = new Set();
  const matchedGroups = new Set();
  const filtered = getDefaultFiltersCounter();

  mutableMessagesOrder.forEach(msgId => {
    const message = mutableMessagesById.get(msgId);
    const groupParentId = message.groupId;
    let hasMatchedAncestor = false;
    const ancestors = [];

    if (groupParentId) {
      let ancestorId = groupParentId;

      // we track the message's ancestors and their state
      while (ancestorId) {
        ancestors.push({
          ancestorId,
          matchedFilters: matchedGroups.has(ancestorId),
          isOpen: messagesUiById.includes(ancestorId),
          isCurrentlyVisible: visibleMessages.includes(ancestorId),
        });
        if (!hasMatchedAncestor && matchedGroups.has(ancestorId)) {
          hasMatchedAncestor = true;
        }
        ancestorId = mutableMessagesById.get(ancestorId).groupId;
      }
    }

    const { visible, cause } = getMessageVisibility(message, {
      messagesState,
      filtersState,
      prefsState,
      uiState,
      hasMatchedAncestor,
    });

    // if the message is not visible but passes the search filters, we show its visible ancestors
    if (!visible && passSearchFilters(message, filtersState)) {
      const tmp = [];
      ancestors.forEach(msg => {
        if (msg.isCurrentlyVisible) {
          tmp.push(msg.ancestorId);
        }
      });
      tmp.reverse().forEach(id => {
        messagesToShow.add(id);
      });
    }
    if (visible) {
      // if the visible message is a child of a group, we add its ancestors to the visible messages
      if (groupParentId) {
        // We need to reverse the visibleAncestors array to show the groups in the correct order
        ancestors.reverse().forEach(msg => {
          messagesToShow.add(msg.ancestorId);
        });
      }

      // we keep track of matched startGroup and startGroupCollapsed messages so we don't filter their children
      if (
        message.type === "startGroup" ||
        message.type === "startGroupCollapsed"
      ) {
        matchedGroups.add(msgId);
      }

      messagesToShow.add(msgId);
    } else if (DEFAULT_FILTERS.includes(cause)) {
      filtered.global = filtered.global + 1;
      filtered[cause] = filtered[cause] + 1;
    }
  });

  const newState = {
    ...messagesState,
    visibleMessages: Array.from(messagesToShow),
    filteredMessagesCount: filtered,
  };

  maybeSortVisibleMessages(
    newState,
    // Only sort for warningGroups if the feature is enabled
    prefsState.groupSimilar,
    forceTimestampSort
  );

  return newState;
}

/**
 * Returns the new current group id given the previous current group and the groupsById
 * state property.
 *
 * @param {string} currentGroup: id of the current group
 * @param {Map} groupsById
 * @param {Array} ignoredIds: An array of ids which can't be the new current group.
 * @returns {string | null} The new current group id, or null if there isn't one.
 */
function getNewCurrentGroup(currentGroup, groupsById, ignoredIds = new Set()) {
  if (!currentGroup) {
    return null;
  }

  // Retrieve the parent groups of the current group.
  const parents = groupsById.get(currentGroup);

  // If there's at least one parent, make the first one the new currentGroup.
  if (Array.isArray(parents) && parents.length) {
    // If the found group must be ignored, let's search for its parent.
    if (ignoredIds.has(parents[0])) {
      return getNewCurrentGroup(parents[0], groupsById, ignoredIds);
    }

    return parents[0];
  }

  return null;
}

function getParentGroups(currentGroup, groupsById) {
  let groups = [];
  if (currentGroup) {
    // If there is a current group, we add it as a parent
    groups = [currentGroup];

    // As well as all its parents, if it has some.
    const parentGroups = groupsById.get(currentGroup);
    if (Array.isArray(parentGroups) && parentGroups.length) {
      groups = groups.concat(parentGroups);
    }
  }

  return groups;
}

function getOutermostGroup(message, groupsById) {
  const groups = getParentGroups(message.groupId, groupsById);
  if (groups.length === 0) {
    return null;
  }
  return groups[groups.length - 1];
}

/**
 * Remove all top level messages that exceeds message limit.
 * Also populate an array of all backend actors associated with these
 * messages so they can be released.
 */
function limitTopLevelMessageCount(newState, logLimit) {
  let topLevelCount =
    newState.groupsById.size === 0
      ? newState.mutableMessagesById.size
      : getToplevelMessageCount(newState);

  if (topLevelCount <= logLimit) {
    return newState;
  }

  const removedMessagesId = new Set();

  let cleaningGroup = false;
  for (const id of newState.mutableMessagesOrder) {
    const message = newState.mutableMessagesById.get(id);
    // If we were cleaning a group and the current message does not have
    // a groupId, we're done cleaning.
    if (cleaningGroup === true && !message.groupId) {
      cleaningGroup = false;
    }

    // If we're not cleaning a group and the message count is below the logLimit,
    // we exit the loop.
    if (cleaningGroup === false && topLevelCount <= logLimit) {
      break;
    }

    // If we're not currently cleaning a group, and the current message is identified
    // as a group, set the cleaning flag to true.
    if (cleaningGroup === false && newState.groupsById.has(id)) {
      cleaningGroup = true;
    }

    if (!message.groupId) {
      topLevelCount--;
    }

    removedMessagesId.add(id);
  }

  return removeMessagesFromState(newState, removedMessagesId);
}

/**
 * Clean the properties for a given state object and an array of removed messages ids.
 * Be aware that this function MUTATE the `state` argument.
 *
 * @param {MessageState} state
 * @param {Set} removedMessagesIds
 * @returns {MessageState}
 */
function removeMessagesFromState(state, removedMessagesIds) {
  if (removedMessagesIds.size === 0) {
    return state;
  }

  const frontsToRelease = [];
  const visibleMessages = [...state.visibleMessages];
  removedMessagesIds.forEach(id => {
    const index = visibleMessages.indexOf(id);
    if (index > -1) {
      visibleMessages.splice(index, 1);
    }

    // We want to minimize time spent in reducer as much as we can, so we're using
    // prototype.push.apply here as it seems faster than other solutions (e.g. the
    // spread operator, Array#concat, …)
    Array.prototype.push.apply(
      frontsToRelease,
      getAllFrontsInMessage(state.mutableMessagesById.get(id))
    );
  });

  if (state.visibleMessages.length > visibleMessages.length) {
    state.visibleMessages = visibleMessages;
  }

  if (frontsToRelease.length) {
    state.frontsToRelease = state.frontsToRelease.concat(frontsToRelease);
  }

  const isInRemovedId = id => removedMessagesIds.has(id);
  const mapHasRemovedIdKey = map => {
    for (const id of removedMessagesIds) {
      if (map.has(id)) {
        return true;
      }
    }
    return false;
  };
  const objectHasRemovedIdKey = obj =>
    Object.keys(obj).findIndex(isInRemovedId) !== -1;

  const cleanUpMap = map => {
    const clonedMap = new Map(map);
    removedMessagesIds.forEach(id => clonedMap.delete(id));
    return clonedMap;
  };
  const cleanUpObject = object =>
    [...Object.entries(object)].reduce((res, [id, value]) => {
      if (!isInRemovedId(id)) {
        res[id] = value;
      }
      return res;
    }, {});

  removedMessagesIds.forEach(id => {
    state.mutableMessagesById.delete(id);

    state.mutableMessagesOrder.splice(
      state.mutableMessagesOrder.indexOf(id),
      1
    );
  });

  if (state.disabledMessagesById.find(isInRemovedId)) {
    state.disabledMessagesById = state.disabledMessagesById.filter(
      id => !isInRemovedId(id)
    );
  }

  if (state.messagesUiById.find(isInRemovedId)) {
    state.messagesUiById = state.messagesUiById.filter(
      id => !isInRemovedId(id)
    );
  }

  if (isInRemovedId(state.currentGroup)) {
    state.currentGroup = getNewCurrentGroup(
      state.currentGroup,
      state.groupsById,
      removedMessagesIds
    );
  }

  if (mapHasRemovedIdKey(state.cssMessagesMatchingElements)) {
    state.cssMessagesMatchingElements = cleanUpMap(
      state.cssMessagesMatchingElements
    );
  }
  if (mapHasRemovedIdKey(state.groupsById)) {
    state.groupsById = cleanUpMap(state.groupsById);
  }

  if (objectHasRemovedIdKey(state.repeatById)) {
    state.repeatById = cleanUpObject(state.repeatById);
  }

  if (objectHasRemovedIdKey(state.networkMessagesUpdateById)) {
    state.networkMessagesUpdateById = cleanUpObject(
      state.networkMessagesUpdateById
    );
  }

  return state;
}

/**
 * Get an array of all the fronts logged in a specific message.
 *
 * @param {Message} message: The message to get actors from.
 * @return {Array<ObjectFront|LongStringFront>} An array containing all the fronts logged
 *                                              in a message.
 */
function getAllFrontsInMessage(message) {
  const { parameters, messageText } = message;

  const fronts = [];
  const isFront = p => p && typeof p.release === "function";

  if (Array.isArray(parameters)) {
    message.parameters.forEach(parameter => {
      if (isFront(parameter)) {
        fronts.push(parameter);
      }
    });
  }

  if (isFront(messageText)) {
    fronts.push(messageText);
  }

  return fronts;
}

/**
 * Returns total count of top level messages (those which are not
 * within a group).
 */
function getToplevelMessageCount(state) {
  let count = 0;
  state.mutableMessagesById.forEach(message => {
    if (!message.groupId) {
      count++;
    }
  });
  return count;
}

/**
 * Check if a message should be visible in the console output, and if not, what
 * causes it to be hidden.
 *
 * @param {Message} message: The message to check
 * @param {object} option: An option object of the following shape:
 *                   - {MessageState} messagesState: The current messages state
 *                   - {FilterState} filtersState: The current filters state
 *                   - {PrefsState} prefsState: The current preferences state
 *                   - {UiState} uiState: The current ui state
 *                   - {Boolean} checkGroup: Set to false to not check if a message should
 *                                 be visible because it is in a console.group.
 *                   - {Boolean} checkParentWarningGroupVisibility: Set to false to not
 *                                 check if a message should be visible because it is in a
 *                                 warningGroup and the warningGroup is visible.
 *                   - {Boolean} hasMatchedAncestor: Set to true if message is part of a
 *                                 group that has been set to visible
 *
 * @return {object} An object of the following form:
 *         - visible {Boolean}: true if the message should be visible
 *         - cause {String}: if visible is false, what causes the message to be hidden.
 */
// eslint-disable-next-line complexity
function getMessageVisibility(
  message,
  {
    messagesState,
    filtersState,
    prefsState,
    uiState,
    checkGroup = true,
    checkParentWarningGroupVisibility = true,
    hasMatchedAncestor = false,
  }
) {
  const warningGroupMessageId = getParentWarningGroupMessageId(message);
  const parentWarningGroupMessage = messagesState.mutableMessagesById.get(
    warningGroupMessageId
  );

  // Do not display the message if it's in closed group and not in a warning group.
  if (
    checkGroup &&
    !isInOpenedGroup(
      message,
      messagesState.groupsById,
      messagesState.messagesUiById
    ) &&
    !shouldGroupWarningMessages(
      parentWarningGroupMessage,
      messagesState,
      prefsState
    )
  ) {
    return {
      visible: false,
      cause: "closedGroup",
    };
  }

  // If the message is a warningGroup, check if it should be displayed.
  if (isWarningGroup(message)) {
    if (!shouldGroupWarningMessages(message, messagesState, prefsState)) {
      return {
        visible: false,
        cause: "warningGroupHeuristicNotMet",
      };
    }

    // Hide a warningGroup if the warning filter is off.
    if (!filtersState[FILTERS.WARN]) {
      // We don't include any cause as we don't want that message to be reflected in the
      // message count.
      return {
        visible: false,
      };
    }

    // Display a warningGroup if at least one of its message will be visible.
    const childrenMessages = messagesState.warningGroupsById.get(message.id);
    const hasVisibleChild =
      childrenMessages &&
      childrenMessages.some(id => {
        const child = messagesState.mutableMessagesById.get(id);
        if (!child) {
          return false;
        }

        const { visible, cause } = getMessageVisibility(child, {
          messagesState,
          filtersState,
          prefsState,
          uiState,
          checkParentWarningGroupVisibility: false,
        });
        return visible && cause !== "visibleWarningGroup";
      });

    if (hasVisibleChild) {
      return {
        visible: true,
        cause: "visibleChild",
      };
    }
  }

  // Do not display the message if it can be in a warningGroup, and the group is
  // displayed but collapsed.
  if (
    parentWarningGroupMessage &&
    shouldGroupWarningMessages(
      parentWarningGroupMessage,
      messagesState,
      prefsState
    ) &&
    !messagesState.messagesUiById.includes(warningGroupMessageId)
  ) {
    return {
      visible: false,
      cause: "closedWarningGroup",
    };
  }

  // Display a message if it is in a warningGroup that is visible. We don't check the
  // warningGroup visibility if `checkParentWarningGroupVisibility` is false, because
  // it means we're checking the warningGroup visibility based on the visibility of its
  // children, which would cause an infinite loop.
  const parentVisibility =
    parentWarningGroupMessage && checkParentWarningGroupVisibility
      ? getMessageVisibility(parentWarningGroupMessage, {
          messagesState,
          filtersState,
          prefsState,
          uiState,
          checkGroup,
          checkParentWarningGroupVisibility,
        })
      : null;
  if (
    parentVisibility &&
    parentVisibility.visible &&
    parentVisibility.cause !== "visibleChild"
  ) {
    return {
      visible: true,
      cause: "visibleWarningGroup",
    };
  }

  // Some messages can't be filtered out (e.g. groups).
  // So, always return visible: true for those.
  if (isUnfilterable(message)) {
    return {
      visible: true,
    };
  }

  // In the browser console and the browser toolbox console, let the user
  // separately show or hide messages coming from the browser itself (privileged
  // code) and those coming from web content.
  if (!passOriginFilters(message, filtersState)) {
    return {
      visible: false,
      cause: message.chromeContext ? FILTERS.CHROME : FILTERS.CONTENT,
    };
  }

  // Let's check all level filters (error, warn, log, …) and return visible: false
  // and the message level as a cause if the function returns false.
  if (!passLevelFilters(message, filtersState)) {
    return {
      visible: false,
      cause: message.level,
    };
  }

  if (!passCssFilters(message, filtersState)) {
    return {
      visible: false,
      cause: FILTERS.CSS,
    };
  }

  if (!passNetworkFilter(message, filtersState)) {
    return {
      visible: false,
      cause: FILTERS.NET,
    };
  }

  if (!passXhrFilter(message, filtersState)) {
    return {
      visible: false,
      cause: FILTERS.NETXHR,
    };
  }

  // This should always be the last check, or we might report that a message was hidden
  // because of text search, while it may be hidden because its category is disabled.
  // Do not check for search filters if it is part of a group and one of its ancestor
  // has matched the current search filters and set to visible
  if (!hasMatchedAncestor && !passSearchFilters(message, filtersState)) {
    return {
      visible: false,
      cause: FILTERS.TEXT,
    };
  }

  return {
    visible: true,
  };
}

function isUnfilterable(message) {
  return [
    MESSAGE_TYPE.COMMAND,
    MESSAGE_TYPE.RESULT,
    MESSAGE_TYPE.NAVIGATION_MARKER,
  ].includes(message.type);
}

function isInOpenedGroup(message, groupsById, messagesUI) {
  return (
    !message.groupId ||
    (!isGroupClosed(message.groupId, messagesUI) &&
      !hasClosedParentGroup(groupsById.get(message.groupId), messagesUI))
  );
}

function hasClosedParentGroup(group, messagesUI) {
  return group.some(groupId => isGroupClosed(groupId, messagesUI));
}

function isGroupClosed(groupId, messagesUI) {
  return messagesUI.includes(groupId) === false;
}

/**
 * Returns true if the message shouldn't be hidden because of the network filter state.
 *
 * @param {object} message - The message to check the filter against.
 * @param {FilterState} filters - redux "filters" state.
 * @returns {boolean}
 */
function passNetworkFilter(message, filters) {
  // The message passes the filter if it is not a network message,
  // or if it is an xhr one,
  // or if the network filter is on.
  return (
    message.source !== MESSAGE_SOURCE.NETWORK ||
    message.isXHR === true ||
    filters[FILTERS.NET] === true ||
    (filters[FILTERS.ERROR] && isMessageNetworkError(message))
  );
}

/**
 * Returns true if the message shouldn't be hidden because of the xhr filter state.
 *
 * @param {object} message - The message to check the filter against.
 * @param {FilterState} filters - redux "filters" state.
 * @returns {boolean}
 */
function passXhrFilter(message, filters) {
  // The message passes the filter if it is not a network message,
  // or if it is a non-xhr one,
  // or if the xhr filter is on.
  return (
    message.source !== MESSAGE_SOURCE.NETWORK ||
    message.isXHR === false ||
    filters[FILTERS.NETXHR] === true ||
    (filters[FILTERS.ERROR] && isMessageNetworkError(message))
  );
}

/**
 * Returns true if the message shouldn't be hidden because of levels filter state.
 *
 * @param {object} message - The message to check the filter against.
 * @param {FilterState} filters - redux "filters" state.
 * @returns {boolean}
 */
function passLevelFilters(message, filters) {
  // The message passes the filter if it is not a console call,
  // or if its level matches the state of the corresponding filter.
  return (
    (message.source !== MESSAGE_SOURCE.CONSOLE_API &&
      message.source !== MESSAGE_SOURCE.JAVASCRIPT) ||
    filters[message.level] === true ||
    (filters[FILTERS.ERROR] && isMessageNetworkError(message))
  );
}

/**
 * Returns true if the message shouldn't be hidden because of the origin filters
 * (chrome/content). These filters are only displayed in the browser console and
 * the browser toolbox console; in the web console both default to true so this
 * never hides anything.
 *
 * @param {object} message - The message to check the filter against.
 * @param {FilterState} filters - redux "filters" state.
 * @returns {boolean}
 */
function passOriginFilters(message, filters) {
  if (message.chromeContext) {
    return filters[FILTERS.CHROME] === true;
  }
  return filters[FILTERS.CONTENT] === true;
}

/**
 * Returns true if the message shouldn't be hidden because of the CSS filter state.
 *
 * @param {object} message - The message to check the filter against.
 * @param {FilterState} filters - redux "filters" state.
 * @returns {boolean}
 */
function passCssFilters(message, filters) {
  // The message passes the filter if it is not a CSS message,
  // or if the CSS filter is on.
  return message.source !== MESSAGE_SOURCE.CSS || filters.css === true;
}

/**
 * Returns true if the message shouldn't be hidden because of search filter state.
 *
 * @param {object} message - The message to check the filter against.
 * @param {FilterState} filters - redux "filters" state.
 * @returns {boolean}
 */
function passSearchFilters(message, filters) {
  const trimmed = (filters.text || "").trim();

  // "-"-prefix switched to exclude mode
  const exclude = trimmed.startsWith("-");
  const term = exclude ? trimmed.slice(1) : trimmed;

  // This regex matches a very basic regex with an optional i flag
  const regexMatch = /^\/(?<search>.+)\/(?<flags>i)?$/.exec(term);
  let regex;
  if (regexMatch !== null) {
    const flags = "m" + (regexMatch.groups.flags || "");
    try {
      regex = new RegExp(regexMatch.groups.search, flags);
    } catch (e) {}
  }
  const matchStr = regex
    ? str => regex.test(str)
    : str => str.toLocaleLowerCase().includes(term.toLocaleLowerCase());

  // If there is no search, the message passes the filter.
  if (!term) {
    return true;
  }

  const matched =
    // Look for a match in parameters.
    isTextInParameters(matchStr, message.parameters) ||
    // Look for a match in location.
    isTextInFrame(matchStr, message.frame) ||
    // Look for a match in net events.
    isTextInNetEvent(matchStr, message) ||
    // Look for a match in stack-trace.
    isTextInStackTrace(matchStr, message.stacktrace) ||
    // Look for a match in messageText.
    isTextInMessageText(matchStr, message.messageText) ||
    // Look for a match in notes.
    isTextInNotes(matchStr, message.notes) ||
    // Look for a match in prefix.
    isTextInPrefix(matchStr, message.prefix) ||
    // Look for a match in displayName.
    isTextInDisplayName(matchStr, message.displayName);

  return matched ? !exclude : exclude;
}

/**
 * Returns true if given text is included in provided stack frame.
 */
function isTextInFrame(matchStr, frame) {
  if (!frame) {
    return false;
  }

  const { functionName, line, column, source } = frame;
  const { short } = getSourceNames(source);
  const unicodeShort = getUnicodeUrlPath(short);

  const str = `${
    functionName ? functionName + " " : ""
  }${unicodeShort}:${line}:${column}`;
  return matchStr(str);
}

/**
 * Returns true if given text is included in provided parameters.
 */
function isTextInParameters(matchStr, parameters) {
  if (!parameters) {
    return false;
  }

  return parameters.some(parameter => isTextInParameter(matchStr, parameter));
}

/**
 * Returns true if given text is included in provided parameter.
 */
function isTextInParameter(matchStr, parameter) {
  const paramGrip =
    parameter && parameter.getGrip ? parameter.getGrip() : parameter;

  if (paramGrip && paramGrip.class && matchStr(paramGrip.class)) {
    return true;
  }

  const parameterType = typeof parameter;
  if (parameterType !== "object" && parameterType !== "undefined") {
    const str = paramGrip + "";
    if (matchStr(str)) {
      return true;
    }
  }

  const previewItems = getGripPreviewItems(paramGrip);
  for (const item of previewItems) {
    if (isTextInParameter(matchStr, item)) {
      return true;
    }
  }

  if (paramGrip && paramGrip.ownProperties) {
    for (const [key, desc] of Object.entries(paramGrip.ownProperties)) {
      if (matchStr(key)) {
        return true;
      }

      if (isTextInParameter(matchStr, getDescriptorValue(desc))) {
        return true;
      }
    }
  }

  return false;
}

/**
 * Returns true if given text is included in provided net event grip.
 */
function isTextInNetEvent(matchStr, { method, url } = {}) {
  if (!method && !url) {
    return false;
  }
  return matchStr(method) || matchStr(url);
}

/**
 * Returns true if given text is included in provided stack trace.
 */
function isTextInStackTrace(matchStr, stacktrace) {
  if (!Array.isArray(stacktrace)) {
    return false;
  }

  // isTextInFrame expect the properties of the frame object to be in the same
  // order they are rendered in the Frame component.
  return stacktrace.some(frame =>
    isTextInFrame(matchStr, {
      functionName:
        frame.functionName ||
        l10n.getStr("webconsole-stacktrace-anonymous-function"),
      source: frame.filename,
      lineNumber: frame.lineNumber,
      columnNumber: frame.columnNumber,
    })
  );
}

/**
 * Returns true if given text is included in `messageText` field.
 */
function isTextInMessageText(matchStr, messageText) {
  if (!messageText) {
    return false;
  }

  if (typeof messageText === "string") {
    return matchStr(messageText);
  }

  const grip =
    messageText && messageText.getGrip ? messageText.getGrip() : messageText;
  if (grip && grip.type === "longString") {
    return matchStr(grip.initial);
  }

  return true;
}

/**
 * Returns true if given text is included in JS Trace display name.
 */
function isTextInDisplayName(matchStr, displayName) {
  return displayName && matchStr(displayName);
}

/**
 * Returns true if given text is included in notes.
 */
function isTextInNotes(matchStr, notes) {
  if (!Array.isArray(notes)) {
    return false;
  }

  return notes.some(
    note =>
      // Look for a match in location.
      isTextInFrame(matchStr, note.frame) ||
      // Look for a match in messageBody.
      (note.messageBody && matchStr(note.messageBody))
  );
}

/**
 * Returns true if given text is included in prefix.
 */
function isTextInPrefix(matchStr, prefix) {
  if (!prefix) {
    return false;
  }

  return matchStr(`${prefix}: `);
}

function getDefaultFiltersCounter() {
  const count = DEFAULT_FILTERS.reduce((res, filter) => {
    res[filter] = 0;
    return res;
  }, {});
  count.global = 0;
  return count;
}

/**
 * Sort state.visibleMessages if needed.
 *
 * @param {MessageState} state
 * @param {boolean} sortWarningGroupMessage: set to true to sort warningGroup
 *                                           messages. Default to false, as in some
 *                                           situations we already take care of putting
 *                                           the ids at the right position.
 * @param {boolean} timeStampSort: set to true to sort messages by their timestamps.
 */
function maybeSortVisibleMessages(
  state,
  sortWarningGroupMessage = false,
  timeStampSort = false
) {
  if (state.warningGroupsById.size > 0 && sortWarningGroupMessage) {
    state.visibleMessages.sort((a, b) => {
      const messageA = state.mutableMessagesById.get(a);
      const messageB = state.mutableMessagesById.get(b);

      const warningGroupIdA = getParentWarningGroupMessageId(messageA);
      const warningGroupIdB = getParentWarningGroupMessageId(messageB);

      const warningGroupA = state.mutableMessagesById.get(warningGroupIdA);
      const warningGroupB = state.mutableMessagesById.get(warningGroupIdB);

      const aFirst = -1;
      const bFirst = 1;

      // If both messages are in a warningGroup, or if both are not in warningGroups.
      if (
        (warningGroupA && warningGroupB) ||
        (!warningGroupA && !warningGroupB)
      ) {
        return getNaturalOrder(messageA, messageB);
      }

      // If `a` is in a warningGroup (and `b` isn't).
      if (warningGroupA) {
        // If `b` is the warningGroup of `a`, `a` should be after `b`.
        if (warningGroupIdA === messageB.id) {
          return bFirst;
        }
        // `b` is a regular message, we place `a` before `b` if `b` came after `a`'s
        // warningGroup.
        return getNaturalOrder(warningGroupA, messageB);
      }

      // If `b` is in a warningGroup (and `a` isn't).
      if (warningGroupB) {
        // If `a` is the warningGroup of `b`, `a` should be before `b`.
        if (warningGroupIdB === messageA.id) {
          return aFirst;
        }
        // `a` is a regular message, we place `a` after `b` if `a` came after `b`'s
        // warningGroup.
        return getNaturalOrder(messageA, warningGroupB);
      }

      return 0;
    });
  }

  if (timeStampSort) {
    state.visibleMessages.sort((a, b) => {
      const messageA = state.mutableMessagesById.get(a);
      const messageB = state.mutableMessagesById.get(b);
      return getNaturalOrder(messageA, messageB);
    });
  }
}

/**
 * Returns if a given type of warning message should be grouped.
 *
 * @param {ConsoleMessage} warningGroupMessage
 * @param {MessageState} messagesState
 * @param {PrefsState} prefsState
 */
function shouldGroupWarningMessages(
  warningGroupMessage,
  messagesState,
  prefsState
) {
  if (!warningGroupMessage) {
    return false;
  }

  // Only group if the preference is ON.
  if (!prefsState.groupSimilar) {
    return false;
  }

  // We group warning messages if there are at least 2 messages that could go in it.
  const warningGroup = messagesState.warningGroupsById.get(
    warningGroupMessage.id
  );
  if (!warningGroup || !Array.isArray(warningGroup)) {
    return false;
  }

  return warningGroup.length > 1;
}

exports.messages = messages;
