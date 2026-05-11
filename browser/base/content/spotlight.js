/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const browser = window.docShell.chromeEventHandler;
const { document: gDoc, XPCOMUtils } = browser.documentGlobal;

ChromeUtils.defineESModuleGetters(this, {
  AboutWelcomeParent: "resource:///actors/AboutWelcomeParent.sys.mjs",
  E10SUtils: "resource://gre/modules/E10SUtils.sys.mjs",
});

const CONFIG = window.arguments[0];

function addStylesheet(href) {
  const link = document.head.appendChild(document.createElement("link"));
  link.rel = "stylesheet";
  link.href = href;
}

function disableEscClose() {
  addEventListener("keydown", event => {
    if (event.key === "Escape") {
      event.preventDefault();
      event.stopPropagation();
    }
  });
}

/**
 * Render content based on about:welcome multistage template.
 */
function renderMultistage(ready) {
  const AWParent = new AboutWelcomeParent();
  const receive = name => data =>
    AWParent.onContentMessage(`AWPage:${name}`, data, browser);

  // Expose top level functions expected by the bundle.
  window.AWGetFeatureConfig = () => CONFIG;
  window.AWGetSelectedTheme = receive("GET_SELECTED_THEME");
  window.AWGetInstalledAddons = receive("GET_INSTALLED_ADDONS");
  window.AWSelectTheme = data => receive("SELECT_THEME")(data?.toUpperCase());
  // Do not send telemetry if message (e.g. spotlight in PBM) config sets metrics as 'block'.
  if (CONFIG?.metrics !== "block") {
    window.AWSendEventTelemetry = receive("TELEMETRY_EVENT");
  }
  window.AWSendToDeviceEmailsSupported = receive(
    "SEND_TO_DEVICE_EMAILS_SUPPORTED"
  );
  window.AWAddScreenImpression = receive("ADD_SCREEN_IMPRESSION");
  window.AWSendToParent = (name, data) => receive(name)(data);
  window.AWFinish = () => {
    window.close();
  };
  window.AWWaitForMigrationClose = receive("WAIT_FOR_MIGRATION_CLOSE");
  window.AWWaitForNimbus = receive("WAIT_FOR_NIMBUS");
  window.AWEvaluateScreenTargeting = receive("EVALUATE_SCREEN_TARGETING");
  window.AWEvaluateAttributeTargeting = receive("EVALUATE_ATTRIBUTE_TARGETING");
  window.AWPredictRemoteType = ({ browserEl, url }) => {
    const originAttributes = E10SUtils.predictOriginAttributes({
      browser: browserEl,
    });
    const loadContext = window.docShell.QueryInterface(Ci.nsILoadContext);
    const useRemoteTabs = loadContext.useRemoteTabs;
    const useRemoteSubframes = loadContext.useRemoteSubframes;

    return E10SUtils.getRemoteTypeForURI(
      url,
      useRemoteTabs,
      useRemoteSubframes,
      E10SUtils.DEFAULT_REMOTE_TYPE,
      null,
      originAttributes
    );
  };

  // Update styling to be compatible with about:welcome.
  addStylesheet("chrome://browser/content/aboutwelcome/aboutwelcome.css");

  document.body.classList.add("onboardingContainer");
  document.body.id = "multi-stage-message-root";
  // This value is reported as the "page" in telemetry
  document.body.dataset.page = "spotlight";

  // Prevent applying the default modal shadow and margins because the content
  // handles styling, including its own modal shadowing.
  const box = browser.closest(".dialogBox");
  const dialog = box.closest("dialog");
  box.classList.add("spotlightBox");
  dialog?.classList.add("spotlight");
  // Prevent SubDialog methods from manually setting dialog size.
  box.setAttribute("sizeto", "available");
  box.setAttribute("fixedsize", "false");
  addEventListener("pagehide", () => {
    box.classList.remove("spotlightBox");
    dialog?.classList.remove("spotlight");
    box.removeAttribute("sizeto");
  });

  if (CONFIG?.disableEscClose) {
    disableEscClose();

    // Prevent ESC key from closing the Spotlight when focus is on the dialog frame. SubDialog has a system group handler that closes dialogs on ESC, so we need to stop propagation at the parent window level
    const preventEscape = event => {
      if (
        event.key === "Escape" &&
        (dialog?.contains(event.target) || box.contains(event.target))
      ) {
        event.preventDefault();
        event.stopPropagation();
      }
    };
    browser.documentGlobal.addEventListener("keydown", preventEscape, {
      capture: true,
      mozSystemGroup: true,
    });

    addEventListener("pagehide", () => {
      browser.documentGlobal.removeEventListener("keydown", preventEscape, {
        capture: true,
        mozSystemGroup: true,
      });
    });
  }

  // Load the bundle to render the content as configured.
  document.head.appendChild(document.createElement("script")).src =
    "chrome://browser/content/aboutwelcome/aboutwelcome.bundle.js";
  ready();
}

// Indicate when we're ready to show and size (async localized) content.
document.mozSubdialogReady = new Promise(resolve =>
  document.addEventListener(
    "DOMContentLoaded",
    () => renderMultistage(resolve),
    {
      once: true,
    }
  )
);
