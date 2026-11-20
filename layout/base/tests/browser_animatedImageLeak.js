"use strict";

requestLongerTimeout(4);

/*
 * This tests that when we have an animated image in a minimized window we
 * don't leak.
 * We've encountered this bug in 3 different ways:
 * -bug 1830753 - images in top level chrome processes
 *  (we avoid processing them due to their CompositorBridgeChild being paused)
 * -bug 1839109 - images in content processes
 *  (we avoid processing them due to their refresh driver being throttled, this
 *   would also fix the above case)
 * -bug 1875100 - images that are in a content iframe that is not the content
 *  of a tab, so something like an extension iframe in the sidebar
 *  (this was fixed by making the content of a tab declare that it manually
 *   manages its activeness and having all other iframes inherit their
 *   activeness from their parent)
 * In order to hit this bug we require
 * -the same image to be in a minimized window and in a non-mininmized window
 *  so that the image is animated.
 * -the animated image to go over the
 *  image.animated.decode-on-demand.threshold-kb threshold so that we do not
 *  keep all of its frames around (if we keep all its frame around then we
 *  don't try to keep allocating frames and not freeing the old ones)
 * -it has to be the same Image object in memory, not just the same uri
 * Then the visible copy of the image keeps generating new frames, those frames
 * get sent to the minimized copies of the image but they never get processed
 * or marked displayed so they can never be freed/reused.
 *
 * Note that due to bug 1889840, in order to test this we can't use an image
 * loaded at the top level (see the last point above). We must use an html page
 * that contains the image.
 */

// this test is based in part on https://searchfox.org/mozilla-central/rev/c09764753ea40725eb50decad2c51edecbd33308/browser/components/extensions/test/browser/browser_ext_sidebarAction.js

async function pushPrefs1() {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["image.animated.decode-on-demand.threshold-kb", 1],
      ["image.animated.decode-on-demand.batch-size", 2],
    ],
  });
}

async function maximizeWindow(win) {
  // the window might be maximized already, only wait for size change if the
  // size is actually changing.
  if (win.windowState == win.STATE_MAXIMIZED) {
    return;
  }
  let promiseSizeModeChange = BrowserTestUtils.waitForEvent(
    win,
    "sizemodechange"
  );
  win.maximize();
  await promiseSizeModeChange;
}

async function minimizeWindow(win) {
  if (Services.appinfo.isWayland) {
    // Wayland never reports the minimized state, so sizemodechange doesn't
    // fire, but the window still becomes inactive once it's detected as
    // occluded.
    win.minimize();
  } else {
    let promiseSizeModeChange = BrowserTestUtils.waitForEvent(
      win,
      "sizemodechange"
    );
    win.minimize();
    await promiseSizeModeChange;
  }
  await TestUtils.waitForCondition(
    () =>
      !win.browsingContext.isActive &&
      !win.gBrowser.selectedBrowser.docShellIsActive,
    "minimized window should become inactive"
  );
}

// maximize = false then minimize all windows but the last one
// this tests that minimized windows don't accumulate animated images frames
// maximize = true then maximize the last window
// this tests that occluded windows don't accumulate animated images frames
async function openWindows(maximize, taskToPerformBeforeSizeChange) {
  let wins = [null, null, null, null];
  for (let i = 0; i < wins.length; i++) {
    let win = await BrowserTestUtils.openNewBrowserWindow();
    await win.delayedStartupPromise;
    await taskToPerformBeforeSizeChange(win);

    if (maximize && i == wins.length - 1) {
      await maximizeWindow(win);
    } else if (!maximize && i < wins.length - 1) {
      await minimizeWindow(win);
    }

    wins[i] = win;
  }
  return wins;
}

async function pushPrefs2() {
  // wait so that at least one frame of the animation has been shown while the
  // below pref is not set so that the counter gets reset.
  await new Promise(resolve => setTimeout(resolve, 500));

  await SpecialPowers.pushPrefEnv({
    set: [["gfx.testing.assert-render-textures-increase", 75]],
  });
}

async function waitForEnoughFrames() {
  // we want to wait for over 75 frames of the image, it has a delay of 200ms
  // Windows debug test machines seem to animate at about 10 fps though
  await new Promise(resolve => setTimeout(resolve, 20000));
}

async function closeWindows(wins) {
  for (let i = 0; i < wins.length; i++) {
    await BrowserTestUtils.closeWindow(wins[i]);
  }
}

async function popPrefs() {
  await SpecialPowers.popPrefEnv();
  await SpecialPowers.popPrefEnv();
}

add_task(async () => {
  async function runTest(theTestPath, maximize) {
    await pushPrefs1();

    let wins = await openWindows(maximize, async function (win) {
      let tab = await BrowserTestUtils.openNewForegroundTab(
        win.gBrowser,
        theTestPath
      );
    });

    await pushPrefs2();

    await waitForEnoughFrames();

    await closeWindows(wins);

    await popPrefs();

    ok(true, "got here without assserting");
  }

  function fileURL(filename) {
    let ifile = getChromeDir(getResolvedURI(gTestPath));
    ifile.append(filename);
    return Services.io.newFileURI(ifile).spec;
  }

  // This tests the image in content process case
  let contentURL = fileURL("helper_animatedImageLeak.html");
  await runTest(contentURL, /* maximize = */ true);
  await runTest(contentURL, /* maximize = */ false);

  // This tests the image in chrome process case
  let chromeURL = getRootDirectory(gTestPath) + "helper_animatedImageLeak.html";
  await runTest(chromeURL, /* maximize = */ true);
  await runTest(chromeURL, /* maximize = */ false);
});

// Now we test the image in a sidebar loaded via an extension case.

/*
 * The data uri below is a 2kb apng that is 3000x200 with 22 frames with delay
 * of 200ms, it just toggles the color of one pixel from black to red so it's
 * tiny. We use the same data uri (although that is not important to this test)
 * in helper_animatedImageLeak.html.
 */

/*
 * This is just data to create a simple extension that creates a sidebar with
 * an image in it.
 */
let extData = {
  manifest: {
    sidebar_action: {
      default_panel: "sidebar.html",
    },
  },
  useAddonManager: "temporary",

  files: {
    "sidebar.html": `
<!DOCTYPE html>
<html>
  <head>
    <meta charset="UTF-8">
    <script src="sidebar.js"></script>
  </head>
  <body><p>Sidebar</p>
  <img src="data:image/png;base64,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"/>
  </body>
</html>
    `,

    "sidebar.js": function () {
      window.onload = () => {
        browser.test.sendMessage("sidebar");
      };
    },
  },
};

function getExtData(manifestUpdates = {}) {
  return {
    ...extData,
    manifest: {
      ...extData.manifest,
      ...manifestUpdates,
    },
  };
}

async function sendMessage(ext, msg, data = undefined) {
  ext.sendMessage({ msg, data });
  await ext.awaitMessage("done");
}

add_task(async function sidebar_initial_install() {
  async function runTest(maximize) {
    await pushPrefs1();

    ok(
      document.getElementById("sidebar-box").hidden,
      "sidebar box is not visible"
    );

    let extension = ExtensionTestUtils.loadExtension(getExtData());
    await extension.startup();
    await extension.awaitMessage("sidebar");

    // Test sidebar is opened on install
    ok(
      !document.getElementById("sidebar-box").hidden,
      "sidebar box is visible"
    );

    // the sidebar appears on all new windows automatically.
    let wins = await openWindows(maximize, async function (win) {
      await extension.awaitMessage("sidebar");
    });

    await pushPrefs2();

    await waitForEnoughFrames();

    await extension.unload();
    // Test that the sidebar was closed on unload.
    ok(
      document.getElementById("sidebar-box").hidden,
      "sidebar box is not visible"
    );

    await closeWindows(wins);

    await popPrefs();

    ok(true, "got here without assserting");
  }

  await runTest(/* maximize = */ true);
  await runTest(/* maximize = */ false);
});
