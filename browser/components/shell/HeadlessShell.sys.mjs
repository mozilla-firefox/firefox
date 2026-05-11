/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { E10SUtils } from "resource://gre/modules/E10SUtils.sys.mjs";
import { HiddenFrame } from "resource://gre/modules/HiddenFrame.sys.mjs";

// Refrences to the progress listeners to keep them from being gc'ed
// before they are called.
const progressListeners = new Set();

export class ScreenshotParent extends JSWindowActorParent {
  getDimensions(params) {
    return this.sendQuery("GetDimensions", params);
  }
}

ChromeUtils.registerWindowActor("Screenshot", {
  parent: {
    esModuleURI: "moz-src:///browser/components/shell/HeadlessShell.sys.mjs",
  },
  child: {
    esModuleURI: "moz-src:///browser/components/shell/ScreenshotChild.sys.mjs",
  },
});

function loadContentWindow(browser, url) {
  let uri = URL.parse(url)?.URI;
  if (!uri) {
    let err = new Error(`Invalid URL passed to loadContentWindow(): ${url}`);
    console.error(err);
    return Promise.reject(err);
  }

  const principal = Services.scriptSecurityManager.getSystemPrincipal();
  return new Promise(resolve => {
    let oa = E10SUtils.predictOriginAttributes({
      browser,
    });
    let loadURIOptions = {
      triggeringPrincipal: principal,
      remoteType: E10SUtils.getRemoteTypeForURI(
        url,
        true,
        false,
        E10SUtils.DEFAULT_REMOTE_TYPE,
        null,
        oa
      ),
    };
    browser.loadURI(uri, loadURIOptions);
    let { webProgress } = browser;

    let progressListener = {
      onLocationChange(progress, request, location, flags) {
        // Ignore inner-frame events
        if (!progress.isTopLevel) {
          return;
        }
        // Ignore events that don't change the document
        if (flags & Ci.nsIWebProgressListener.LOCATION_CHANGE_SAME_DOCUMENT) {
          return;
        }
        // Ignore the initial about:blank, unless about:blank is requested
        if (location.spec == "about:blank" && uri.spec != "about:blank") {
          return;
        }

        progressListeners.delete(progressListener);
        webProgress.removeProgressListener(progressListener);
        resolve();
      },
      QueryInterface: ChromeUtils.generateQI([
        "nsIWebProgressListener",
        "nsISupportsWeakReference",
      ]),
    };
    progressListeners.add(progressListener);
    webProgress.addProgressListener(
      progressListener,
      Ci.nsIWebProgress.NOTIFY_LOCATION
    );
  });
}

async function takeScreenshot(
  fullWidth,
  fullHeight,
  contentWidth,
  contentHeight,
  path,
  url
) {
  let frame;
  try {
    frame = new HiddenFrame();
    let windowlessBrowser = await frame.get();

    let doc = windowlessBrowser.document;
    let browser = doc.createXULElement("browser");
    browser.setAttribute("remote", "true");
    browser.setAttribute("type", "content");
    browser.style.width = `${contentWidth}px`;
    browser.style.minWidth = `${contentWidth}px`;
    browser.style.height = `${contentHeight}px`;
    browser.style.minHeight = `${contentHeight}px`;
    browser.setAttribute("maychangeremoteness", "true");
    doc.documentElement.appendChild(browser);

    await loadContentWindow(browser, url);

    let actor =
      browser.browsingContext.currentWindowGlobal.getActor("Screenshot");
    let dimensions = await actor.getDimensions();

    let canvas = doc.createElementNS(
      "http://www.w3.org/1999/xhtml",
      "html:canvas"
    );
    let context = canvas.getContext("2d");
    let width = dimensions.innerWidth;
    let height = dimensions.innerHeight;
    if (fullWidth) {
      width += dimensions.scrollMaxX - dimensions.scrollMinX;
    }
    if (fullHeight) {
      height += dimensions.scrollMaxY - dimensions.scrollMinY;
    }
    canvas.width = width;
    canvas.height = height;
    let rect = new DOMRect(0, 0, width, height);

    let snapshot =
      await browser.browsingContext.currentWindowGlobal.drawSnapshot(
        rect,
        1,
        "rgb(255, 255, 255)"
      );
    context.drawImage(snapshot, 0, 0);

    snapshot.close();

    let blob = await new Promise(resolve => canvas.toBlob(resolve));

    let reader = await new Promise(resolve => {
      let fr = new FileReader();
      fr.onloadend = () => resolve(fr);
      fr.readAsArrayBuffer(blob);
    });

    await IOUtils.write(path, new Uint8Array(reader.result));
    dump("Screenshot saved to: " + path + "\n");
  } catch (e) {
    dump("Failure taking screenshot: " + e + "\n");
  } finally {
    if (frame) {
      frame.destroy();
    }
  }
}

export let HeadlessShell = {
  async handleCmdLineArgs(cmdLine, URLlist) {
    try {
      // Don't quit even though we don't create a window
      Services.startup.enterLastWindowClosingSurvivalArea();

      // Default options
      let fullWidth = true;
      let fullHeight = true;
      // Most common screen resolution of Firefox users
      let contentWidth = 1366;
      let contentHeight = 768;

      // Parse `window-size`
      try {
        var dimensionsStr = cmdLine.handleFlagWithParam("window-size", true);
      } catch (e) {
        dump("expected format: --window-size width[,height]\n");
        return;
      }
      if (dimensionsStr) {
        let success;
        let dimensions = dimensionsStr.split(",", 2);
        if (dimensions.length == 1) {
          success = dimensions[0] > 0;
          if (success) {
            fullWidth = false;
            fullHeight = true;
            contentWidth = dimensions[0];
          }
        } else {
          success = dimensions[0] > 0 && dimensions[1] > 0;
          if (success) {
            fullWidth = false;
            fullHeight = false;
            contentWidth = dimensions[0];
            contentHeight = dimensions[1];
          }
        }

        if (!success) {
          dump("expected format: --window-size width[,height]\n");
          return;
        }
      }

      let urlOrFileToSave = null;
      try {
        urlOrFileToSave = cmdLine.handleFlagWithParam("screenshot", true);
      } catch (e) {
        // We know that the flag exists so we only get here if there was no parameter.
        cmdLine.handleFlag("screenshot", true); // Remove `screenshot`
      }

      // Assume that the remaining arguments that do not start
      // with a hyphen are URLs
      for (let i = 0; i < cmdLine.length; ++i) {
        const argument = cmdLine.getArgument(i);
        if (argument.startsWith("-")) {
          dump(`Warning: unrecognized command line flag ${argument}\n`);
          // To emulate the pre-nsICommandLine behavior, we ignore
          // the argument after an unrecognized flag.
          ++i;
        } else {
          URLlist.push(argument);
        }
      }

      let path = null;
      if (urlOrFileToSave && !URLlist.length) {
        // URL was specified next to "-screenshot"
        // Example: -screenshot https://www.example.com -attach-console
        URLlist.push(urlOrFileToSave);
      } else {
        path = urlOrFileToSave;
      }

      if (!path) {
        path = PathUtils.join(cmdLine.workingDirectory.path, "screenshot.png");
      }

      if (URLlist.length == 1) {
        await takeScreenshot(
          fullWidth,
          fullHeight,
          contentWidth,
          contentHeight,
          path,
          URLlist[0]
        );
      } else {
        dump("expected exactly one URL when using `screenshot`\n");
      }
    } finally {
      Services.startup.exitLastWindowClosingSurvivalArea();
      Services.startup.quit(Ci.nsIAppStartup.eForceQuit);
    }
  },
};
