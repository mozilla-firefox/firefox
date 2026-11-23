/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  MonitorAgent:
    "moz-src:///browser/components/aiwindow/models/agents/MonitorAgent.sys.mjs",
  MONITOR_BLOCKED_STATE_CHANGED_TOPIC:
    "moz-src:///browser/components/aiwindow/models/agents/Monitor.sys.mjs",
  MonitorUIUtils:
    "moz-src:///browser/components/aiwindow/ui/modules/MonitorUIUtils.sys.mjs",
  TOTAL_NUM_MONITORS:
    "moz-src:///browser/components/aiwindow/models/agents/Monitor.sys.mjs",
  TOTAL_NUM_URLS_IN_MONITOR:
    "moz-src:///browser/components/aiwindow/models/agents/Monitor.sys.mjs",
  SCHEDULE_TYPES:
    "moz-src:///browser/components/aiwindow/models/agents/Schedule.sys.mjs",
});

/**
 * JSWindowActor to handle communication between SmartWindowTasks UI and MonitorAgent.
 */
export class SmartWindowTasksParent extends JSWindowActorParent {
  #messageHandlers = new Map([
    ["SmartWindowTasks:ListMonitors", this.#handleListMonitors.bind(this)],
    ["SmartWindowTasks:CreateMonitor", this.#handleCreateMonitor.bind(this)],
    ["SmartWindowTasks:DeleteMonitor", this.#handleDeleteMonitor.bind(this)],
    ["SmartWindowTasks:UpdateMonitor", this.#handleUpdateMonitor.bind(this)],
    ["SmartWindowTasks:RunMonitor", this.#handleRunMonitor.bind(this)],
    ["SmartWindowTasks:PauseMonitor", this.#handlePauseMonitor.bind(this)],
    ["SmartWindowTasks:GetConstants", this.#handleGetConstants.bind(this)],
    ["SmartWindowTasks:OpenUrl", this.#handleOpenUrl.bind(this)],
  ]);

  #observer = {
    observe: () => {
      this.sendAsyncMessage("SmartWindowTasks:BlockedStateChanged");
    },
  };

  actorCreated() {
    Services.obs.addObserver(
      this.#observer,
      lazy.MONITOR_BLOCKED_STATE_CHANGED_TOPIC
    );
  }

  didDestroy() {
    Services.obs.removeObserver(
      this.#observer,
      lazy.MONITOR_BLOCKED_STATE_CHANGED_TOPIC
    );
  }

  async receiveMessage({ data, name }) {
    const handler = this.#messageHandlers.get(name);

    if (!handler) {
      console.warn(`SmartWindowTasksParent received unknown message: ${name}`);
      return null;
    }

    return handler(data);
  }

  async #handleListMonitors() {
    try {
      const monitors = await lazy.MonitorAgent.listMonitors();
      // Resolve page titles
      await Promise.all(
        monitors.map(async monitor => {
          monitor.watchUrlTitles =
            await lazy.MonitorUIUtils.resolveWatchUrlTitles(monitor.watchUrls);
        })
      );
      return { success: true, monitors };
    } catch (error) {
      console.error("Failed to list monitors:", error);
      return { success: false, error: error.message, code: error.code };
    }
  }

  async #handleCreateMonitor(data) {
    try {
      const monitor = await lazy.MonitorAgent.createMonitor(data);
      return { success: true, monitor };
    } catch (error) {
      console.error("Failed to create monitor:", error);
      return { success: false, error: error.message, code: error.code };
    }
  }

  async #handleDeleteMonitor(data) {
    // Check if we're in test mode - tests can pass skipConfirmation flag
    const skipConfirmation = data.skipConfirmation === true;
    return lazy.MonitorUIUtils.deleteMonitorWithConfirmation(
      this.browsingContext,
      data.id,
      skipConfirmation
    );
  }

  async #handleUpdateMonitor(data) {
    try {
      const monitor = await lazy.MonitorAgent.updateMonitor(
        data.id,
        data.updates
      );
      return { success: true, monitor };
    } catch (error) {
      console.error("Failed to update monitor:", error);
      return { success: false, error: error.message, code: error.code };
    }
  }

  async #handleRunMonitor(data) {
    try {
      const result = await lazy.MonitorAgent.runNow(data.id);
      return { success: true, result };
    } catch (error) {
      console.error("Failed to run monitor:", error);
      return { success: false, error: error.message, code: error.code };
    }
  }

  async #handlePauseMonitor(data) {
    try {
      await lazy.MonitorAgent.pauseMonitor(data.id, data.pause);
      return { success: true };
    } catch (error) {
      console.error("Failed to pause monitor:", error);
      return { success: false, error: error.message, code: error.code };
    }
  }

  #handleOpenUrl(data) {
    return lazy.MonitorUIUtils.openMonitorUrl(
      this.browsingContext.topChromeWindow,
      data?.url
    );
  }

  #handleGetConstants() {
    return {
      success: true,
      constants: {
        TOTAL_NUM_MONITORS: lazy.TOTAL_NUM_MONITORS,
        TOTAL_NUM_URLS_IN_MONITOR: lazy.TOTAL_NUM_URLS_IN_MONITOR,
        SCHEDULE_TYPES: lazy.SCHEDULE_TYPES,
        isMonitorRegionSupported:
          lazy.MonitorUIUtils.isMonitorRegionSupported(),
        isBlocked: lazy.MonitorAgent.isBlocked,
        smartWindowSupportUrl:
          Services.urlFormatter.formatURLPref("app.support.baseURL") +
          "smart-window",
      },
    };
  }
}
