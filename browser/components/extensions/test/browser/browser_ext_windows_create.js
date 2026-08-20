"use strict";

add_task(async function testWindowCreate() {
  let extension = ExtensionTestUtils.loadExtension({
    async background() {
      let _checkWindowPromise;
      browser.test.onMessage.addListener(msg => {
        if (msg == "checked-window") {
          _checkWindowPromise.resolve();
          _checkWindowPromise = null;
        }
      });

      let os;

      function checkWindow(expected) {
        return new Promise(resolve => {
          _checkWindowPromise = { resolve };
          browser.test.sendMessage("check-window", expected);
        });
      }

      async function createWindow(params, expected, keep = false) {
        let window = await browser.windows.create(...params);
        // params is null when testing create without createData
        params = params[0] || {};

        // Prevent frequent intermittent failures on macos where the newly created window
        // may have not always got into the fullscreen state before browser.window.create
        // resolves the windows details.
        if (
          os === "mac" &&
          params.state === "fullscreen" &&
          window.state !== params.state
        ) {
          browser.test.log(
            "Wait for window.state for the newly create window to be set to fullscreen"
          );
          while (window.state !== params.state) {
            window = await browser.windows.get(window.id, { populate: true });
          }
          browser.test.log(
            "Newly created browser window got into fullscreen state"
          );
        }

        for (let key of Object.keys(params)) {
          if (key == "state" && os == "mac" && params.state == "normal") {
            // OS-X doesn't have a hard distinction between "normal" and
            // "maximized" states.
            browser.test.assertTrue(
              window.state == "normal" || window.state == "maximized",
              `Expected window.state (currently ${window.state}) to be "normal" but will accept "maximized"`
            );
          } else {
            browser.test.assertEq(
              params[key],
              window[key],
              `Got expected value for window.${key}`
            );
          }
        }

        browser.test.assertEq(
          1,
          window.tabs.length,
          "tabs property got populated"
        );

        await checkWindow(expected);
        if (keep) {
          return window;
        }

        if (params.state == "fullscreen" && os == "win") {
          // FIXME: Closing a fullscreen window causes a window leak in
          // Windows tests.
          await browser.windows.update(window.id, { state: "normal" });
        }
        await browser.windows.remove(window.id);
      }

      try {
        ({ os } = await browser.runtime.getPlatformInfo());

        // Set the current window to state: "normal" because the test is failing on Windows
        // where the current window is maximized.
        let currentWindow = await browser.windows.getCurrent();
        await browser.windows.update(currentWindow.id, { state: "normal" });

        await createWindow([], { state: "STATE_NORMAL" });
        await createWindow([{ state: "maximized" }], {
          state: "STATE_MAXIMIZED",
        });
        await createWindow([{ state: "minimized" }], {
          state: "STATE_MINIMIZED",
        });
        await createWindow([{ state: "normal" }], {
          state: "STATE_NORMAL",
          hiddenChrome: [],
          navbarVisible: true,
        });
        await createWindow([{ state: "fullscreen" }], {
          state: "STATE_FULLSCREEN",
        });

        let window = await createWindow(
          [{ type: "popup" }],
          {
            hiddenChrome: ["menubar", "toolbar", "directories", "extrachrome"],
            chromeFlags: ["CHROME_OPENAS_DIALOG"],
            navbarVisible: false,
          },
          true
        );

        let tabs = await browser.tabs.query({
          windowType: "popup",
          active: true,
        });

        browser.test.assertEq(1, tabs.length, "Expected only one popup");
        browser.test.assertEq(
          window.id,
          tabs[0].windowId,
          "Expected new window to be returned in query"
        );

        await browser.windows.remove(window.id);

        browser.test.notifyPass("window-create");
      } catch (e) {
        browser.test.fail(`${e} :: ${e.stack}`);
        browser.test.notifyFail("window-create");
      }
    },
  });

  let latestWindow;
  let windowListener = (window, topic) => {
    if (topic == "domwindowopened") {
      latestWindow = window;
    }
  };
  Services.ww.registerNotification(windowListener);

  extension.onMessage("check-window", expected => {
    if (expected.state != null) {
      let { windowState } = latestWindow;
      if (latestWindow.fullScreen) {
        windowState = latestWindow.STATE_FULLSCREEN;
      }

      if (expected.state == "STATE_NORMAL") {
        ok(
          windowState == window.STATE_NORMAL ||
            windowState == window.STATE_MAXIMIZED,
          `Expected windowState (currently ${windowState}) to be STATE_NORMAL but will accept STATE_MAXIMIZED`
        );
      } else {
        is(
          windowState,
          window[expected.state],
          `Expected window state to be ${expected.state}`
        );
      }
    }
    if (expected.hiddenChrome) {
      let chromeHidden =
        latestWindow.document.documentElement.getAttribute("chromehidden");
      is(
        chromeHidden.trim().split(/\s+/).sort().join(" "),
        expected.hiddenChrome.sort().join(" "),
        "Got expected hidden chrome"
      );
    }
    if (expected.navbarVisible != null) {
      let { documentElement } = latestWindow.document;
      is(
        documentElement.hasAttribute("web-extension-popup-window"),
        !expected.navbarVisible,
        "Got expected web-extension-popup-window attribute"
      );
      is(
        BrowserTestUtils.isVisible(
          latestWindow.document.querySelector("#nav-bar")
        ),
        expected.navbarVisible,
        "Got expected #nav-bar visibility"
      );
    }
    if (expected.chromeFlags) {
      let { chromeFlags } = latestWindow.docShell.treeOwner
        .QueryInterface(Ci.nsIInterfaceRequestor)
        .getInterface(Ci.nsIAppWindow);
      for (let flag of expected.chromeFlags) {
        ok(
          chromeFlags & Ci.nsIWebBrowserChrome[flag],
          `Expected window to have the ${flag} flag`
        );
      }
    }

    extension.sendMessage("checked-window");
  });

  await extension.startup();
  await extension.awaitFinish("window-create");
  await extension.unload();

  Services.ww.unregisterNotification(windowListener);
  latestWindow = null;
});
