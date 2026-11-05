/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

let lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  TaskbarTabs: "moz-src:///browser/components/taskbartabs/TaskbarTabs.sys.mjs",
  TaskbarTabsUtils:
    "moz-src:///browser/components/taskbartabs/TaskbarTabsUtils.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "logConsole", () => {
  return console.createInstance({
    logModule: "TaskbarTabs",
    maxLogLevel: "Warn",
  });
});

/**
 * A command line handler for Firefox shortcuts with the flag "-taskbar-tab",
 * which will trigger a Taskbar Tab window to be opened.
 */
export class CommandLineHandler {
  static classID = Components.ID("{974fe39b-4584-4cb5-bf62-5c141aedc557}");
  static contractID = "@mozilla.org/browser/taskbar-tabs-clh;1";

  QueryInterface = ChromeUtils.generateQI([Ci.nsICommandLineHandler]);

  handle(aCmdLine) {
    if (!lazy.TaskbarTabsUtils.isEnabled()) {
      lazy.logConsole.info("Taskbar Tabs disabled, skipping.");
      return;
    }

    let id = aCmdLine.handleFlagWithParam("taskbar-tab", false);
    if (!id) {
      return;
    }

    let context = { id };

    // Handle the commands before entering an async context so that they're not
    // handled by other nsICommandLine handlers.
    let urlParam = aCmdLine.handleFlagWithParam("new-window", false);
    context.url = Services.io.newURI(urlParam);

    let containerParam = aCmdLine.handleFlagWithParam("container", false);
    if (containerParam !== null) {
      context.userContextId = Number(containerParam);
    }

    lazy.logConsole.info(
      `Handling command line invoation for Taskbar Tab ${id}`
    );

    // Prevent the default commandline handler from running.
    aCmdLine.preventDefault = true;

    // Retrieving Taskbar Tabs requires async operations. Prevent shutdown while
    // it loads context to open the window.
    Services.startup.enterLastWindowClosingSurvivalArea();
    launchTaskbarTab(context).finally(() => {
      Services.startup.exitLastWindowClosingSurvivalArea();
    });
  }
}

/**
 * Launches a new Taskbar Tab, recreating it if it didn't exist.
 *
 * @param {object} aContext - Command line retrieved flags and context.
 */
async function launchTaskbarTab(aContext) {
  let taskbarTab;
  try {
    taskbarTab = await lazy.TaskbarTabs.getTaskbarTab(aContext.id);

    lazy.logConsole.debug(
      `Found Taskbar Tab matching the flag "-taskbar-tab ${aContext.id}"`
    );
  } catch (e) {
    lazy.logConsole.debug(
      `Taskbar Tab for ID ${aContext.id} doesn't exist, reconstructing it.`
    );

    if (!Object.hasOwn(aContext, "userContextId")) {
      lazy.logConsole.error(
        "Expected -container flag, but found none. Using the default container."
      );

      aContext.userContextId =
        Services.scriptSecurityManager.DEFAULT_USER_CONTEXT_ID;
    }

    ({ taskbarTab } = await lazy.TaskbarTabs.findOrCreateTaskbarTab(
      aContext.url,
      aContext.userContextId
    ));
  }

  await lazy.TaskbarTabs.openWindow(taskbarTab);
}
