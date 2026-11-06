/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import React from "react";
import { createRoot } from "react-dom/client";
import { MultistageWithDismiss } from "./components/MultistageWithDismiss";

window.mountMultistageMessage = function mountMultistageMessage(
  container,
  props
) {
  const { messageData, handleDismiss, handleBlock, handleClose } = props;
  const config = messageData.content;
  const impressionListeners = new AbortController();
  // Outcome of each screen that has a primary button, keyed by screen id:
  // "completed" (primary button clicked)
  // "skipped" (secondary button clicked)
  // "expired" (dismissed without either)
  const completionStatus = {};
  let currentScreenId = null;
  let dismissSent = false;

  const sendTelemetry = data =>
    window.ASRouterMessage({
      type: "AS_ROUTER_TELEMETRY_USER_EVENT",
      data: { ...data, action: "newtab_message_user_event" },
    });

  const trackCards = (screens, shownScreens) => {
    const shownIds = new Set(shownScreens.map(({ id }) => id));
    for (const { id, content } of screens) {
      if (!shownIds.has(id)) {
        delete completionStatus[id];
      } else if (content?.primary_button) {
        completionStatus[id] ??= "expired";
      }
    }
  };

  const trackTelemetry = data => {
    if (data.event === "IMPRESSION") {
      currentScreenId = data.event_context?.screen_id ?? null;
    } else if (
      data.event === "CLICK_BUTTON" &&
      currentScreenId in completionStatus
    ) {
      if (data.event_context?.source === "primary_button") {
        completionStatus[currentScreenId] = "completed";
      } else if (data.event_context?.source === "secondary_button") {
        completionStatus[currentScreenId] = "skipped";
      }
    }
  };

  const sendDismissTelemetry = () => {
    if (dismissSent) {
      return;
    }
    dismissSent = true;
    const completionCount = Object.values(completionStatus).filter(
      cardStatus => cardStatus === "completed"
    ).length;
    sendTelemetry({
      event: "DISMISS",
      event_context: {
        source: "newtab",
        page: "about:newtab",
        reason: completionCount ? "engaged" : "not_engaged",
        completion_status: completionStatus,
        completion_count: completionCount,
      },
      message_id: messageData.id,
    });
  };

  const awHandlers = {
    AWEvaluateScreenTargeting: async screens => {
      const filtered = await window.ASRouterMessage({
        type: "AW_EVALUATE_SCREEN_TARGETING",
        data: screens,
      });
      trackCards(screens, filtered ?? screens);
      return filtered;
    },
    AWGetFeatureConfig: () => config,
    AWFinish: () => {
      sendDismissTelemetry();
      handleClose();
    },
    AWSendToParent: (handlerName, data) =>
      window.ASRouterMessage({
        type: "USER_ACTION",
        data,
      }),
    AWAddScreenImpression: screenObj => {
      window.ASRouterMessage({
        type: "AW_ADD_SCREEN_IMPRESSION",
        data: screenObj,
      });
    },
    AWSendEventTelemetry: data => {
      trackTelemetry(data);
      if (
        data.event !== "IMPRESSION" ||
        document.visibilityState === "visible"
      ) {
        sendTelemetry(data);
      } else {
        document.addEventListener(
          "visibilitychange",
          () => sendTelemetry(data),
          {
            once: true,
            signal: impressionListeners.signal,
          }
        );
      }
    },
    AWGetSelectedTheme: () => Promise.resolve(),
    AWGetActiveThemeId: async () => {
      try {
        return await window.ASRouterMessage({
          type: "AW_GET_ACTIVE_THEME_ID",
        });
      } catch {
        return "";
      }
    },
    AWGetInstalledAddons: () => Promise.resolve(),
    AWWaitForMigrationClose: () =>
      window.ASRouterMessage({ type: "AW_WAIT_FOR_MIGRATION_CLOSE" }),
  };

  for (const [handlerName, fn] of Object.entries(awHandlers)) {
    window[handlerName] = fn;
  }

  const root = createRoot(container);
  root.render(
    <MultistageWithDismiss
      config={config}
      handleDismiss={handleDismiss}
      handleBlock={handleBlock}
    />
  );

  return function cleanup() {
    impressionListeners.abort();
    root.unmount();
    for (const handlerName of Object.keys(awHandlers)) {
      delete window[handlerName];
    }
  };
};
