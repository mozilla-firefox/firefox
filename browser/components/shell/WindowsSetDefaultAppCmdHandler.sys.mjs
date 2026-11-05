/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Command-line handler for the Windows IOpenWithLauncher round-trip.
 *
 * When the ShellService.setAsDefault{PDF,Protocol}Handler launches the windows only
 * "Open with" picker, it hands it a bundled stub file path for file-type
 * defaults, or a URL for protocol defaults, and stashes a one-shot
 * { openWithArg, overrideUri } redirect.
 *
 * If the user picks Firefox, Windows invokes Firefox with
 * `-osint -url <openWithArg>`. This handler runs before BrowserContentHandler and
 * intercepts the launch: it asks ShellService whether the -url
 * value matches the pending openWithArg and, if so, suppresses the
 * open and optionally redirects to the stashed overrideUri so the user lands
 * somewhere meaningful in Firefox.
 */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  BrowserWindowTracker: "resource:///modules/BrowserWindowTracker.sys.mjs",
  WindowsSetDefaultRedirect:
    "moz-src:///browser/components/shell/WindowsSetDefaultRedirect.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "logConsole", () => {
  return console.createInstance({
    logModule: "WindowsSetDefaultAppCmdHandler",
    maxLogLevel: "Warn",
  });
});

export class CommandLineHandler {
  static classID = Components.ID("{da7de528-7a15-452e-b5a7-521099997ca1}");
  static contractID = "@mozilla.org/browser/windows-default-clh;1";

  QueryInterface = ChromeUtils.generateQI([Ci.nsICommandLineHandler]);

  handle(aCmdLine) {
    if (aCmdLine.findFlag("osint", false) < 0) {
      return;
    }

    const urlIdx = aCmdLine.findFlag("url", false);
    if (urlIdx < 0) {
      return;
    }

    const cmdArg = aCmdLine.getArgument(urlIdx + 1);

    // null: not our openWithArg, leave -url for BrowserContentHandler
    const redirect = lazy.WindowsSetDefaultRedirect.consume(cmdArg);
    if (!redirect) {
      return;
    }

    const { overrideUri } = redirect;

    lazy.logConsole.debug(
      `Claimed IOpenWithLauncher openWithArg ${cmdArg}: state=${aCmdLine.state}, overrideUri=${overrideUri}`
    );

    // Consume the arg and suppress the default open so BrowserContentHandler
    // doesn't act on it
    aCmdLine.handleFlagWithParam("url", false);
    aCmdLine.preventDefault = true;
    if (overrideUri === null) {
      return;
    }

    lazy.logConsole.info(
      `Redirecting IOpenWithLauncher round-trip to ${overrideUri}`
    );

    try {
      const win = lazy.BrowserWindowTracker.getTopWindow();
      if (win) {
        win.openTrustedLinkIn(overrideUri, "tab");
        return;
      }

      const args = Cc["@mozilla.org/supports-string;1"].createInstance(
        Ci.nsISupportsString
      );
      args.data = overrideUri;
      lazy.BrowserWindowTracker.openWindow({ args });
    } catch (e) {
      lazy.logConsole.error(
        `Failed to open redirect target ${overrideUri}:`,
        e
      );
    }
  }
}
