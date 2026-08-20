/* - This Source Code Form is subject to the terms of the Mozilla Public
   - License, v. 2.0. If a copy of the MPL was not distributed with this file,
   - You can obtain one at http://mozilla.org/MPL/2.0/. */

/* import-globals-from preferences.js */

"use strict";

var { XPCOMUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/XPCOMUtils.sys.mjs"
);

// Note: we get loaded in dialogs so we need to define our
// own getters, separate from preferences.js .
ChromeUtils.defineESModuleGetters(this, {
  AddonManager: "resource://gre/modules/AddonManager.sys.mjs",
  BrowserUtils: "resource://gre/modules/BrowserUtils.sys.mjs",
  DeferredTask: "resource://gre/modules/DeferredTask.sys.mjs",

  ExtensionPreferencesManager:
    "resource://gre/modules/ExtensionPreferencesManager.sys.mjs",

  ExtensionSettingsStore:
    "resource://gre/modules/ExtensionSettingsStore.sys.mjs",

  Management: "resource://gre/modules/Extension.sys.mjs",
});

const PREF_SETTING_TYPE = "prefs";
const PROXY_KEY = "proxy.settings";
const API_PROXY_PREFS = [
  "network.proxy.type",
  "network.proxy.http",
  "network.proxy.http_port",
  "network.proxy.share_proxy_settings",
  "network.proxy.ssl",
  "network.proxy.ssl_port",
  "network.proxy.socks",
  "network.proxy.socks_port",
  "network.proxy.socks_version",
  "network.proxy.socks_remote_dns",
  "network.proxy.socks5_remote_dns",
  "network.proxy.no_proxies_on",
  "network.proxy.autoconfig_url",
  "signon.autologin.proxy",
];

let extensionControlledContentIds = {
  webNotificationsDisabled: "browserNotificationsPermissionExtensionContent",
  "services.passwordSavingEnabled": "passwordManagerExtensionContent",
  "proxy.settings": "proxyExtensionContent",
  get "websites.trackingProtectionMode"() {
    return {
      button: "contentBlockingDisableTrackingProtectionExtension",
      section: "contentBlockingTrackingProtectionExtensionContentLabel",
    };
  },
};

const extensionControlledL10nKeys = {
  webNotificationsDisabled: "extension-controlling-web-notifications",
  "services.passwordSavingEnabled": "extension-controlling-password-saving",
  "privacy.containers": "extension-controlling-privacy-containers",
  "websites.trackingProtectionMode":
    "extension-controlling-websites-content-blocking-all-trackers",
  "proxy.settings": "extension-controlling-proxy-config",
};

let extensionControlledIds = {};

/**
 * Check if a pref is being managed by an extension.
 */
async function getControllingExtensionInfo(type, settingName) {
  await ExtensionSettingsStore.initialize();
  return ExtensionSettingsStore.getSetting(type, settingName);
}

function getControllingExtensionEls(settingName) {
  let idInfo = extensionControlledContentIds[settingName];
  let section = document.getElementById(idInfo.section || idInfo);
  let button = idInfo.button
    ? document.getElementById(idInfo.button)
    : section.querySelector("button");
  return {
    section,
    button,
    description: section.querySelector("description"),
  };
}

async function getControllingExtension(type, settingName) {
  let info = await getControllingExtensionInfo(type, settingName);
  let addon = info && info.id && (await AddonManager.getAddonByID(info.id));
  return addon;
}

async function handleControllingExtension(type, settingName) {
  let addon = await getControllingExtension(type, settingName);

  // Sometimes the ExtensionSettingsStore gets in a bad state where it thinks
  // an extension is controlling a setting but the extension has been uninstalled
  // outside of the regular lifecycle. If the extension isn't currently installed
  // then we should treat the setting as not being controlled.
  // See https://bugzilla.mozilla.org/show_bug.cgi?id=1411046 for an example.
  if (addon) {
    extensionControlledIds[settingName] = addon.id;
    showControllingExtension(settingName, addon);
  } else {
    let elements = getControllingExtensionEls(settingName);
    if (
      extensionControlledIds[settingName] &&
      !document.hidden &&
      elements.button
    ) {
      showEnableExtensionMessage(settingName);
    } else {
      hideControllingExtension(settingName);
    }
    delete extensionControlledIds[settingName];
  }

  return !!addon;
}

function settingNameToL10nID(settingName) {
  if (!extensionControlledL10nKeys.hasOwnProperty(settingName)) {
    throw new Error(
      `Unknown extension controlled setting name: ${settingName}`
    );
  }
  return extensionControlledL10nKeys[settingName];
}

/**
 * Set the localization data for the description of the controlling extension.
 *
 * The function alters the inner DOM structure of the fragment to, depending
 * on the `addon` argument, remove the `<img/>` element or ensure it's
 * set to the correct src.
 * This allows Fluent DOM Overlays to localize the fragment.
 *
 * @param elem {Element}
 *        <description> element to be annotated
 * @param addon {Object?}
 *        Addon object with meta information about the addon (or null)
 * @param settingName {String}
 *        If `addon` is set this handled the name of the setting that will be used
 *        to fetch the l10n id for the given message.
 *        If `addon` is set to null, this will be the full l10n-id assigned to the
 *        element.
 */
function setControllingExtensionDescription(elem, addon, settingName) {
  const existingImg = elem.querySelector("img");
  if (addon === null) {
    // If the element has an image child element,
    // remove it.
    if (existingImg) {
      existingImg.remove();
    }
    document.l10n.setAttributes(elem, settingName);
    return;
  }

  const defaultIcon = "chrome://mozapps/skin/extensions/extensionGeneric.svg";
  const src = addon.iconURL || defaultIcon;

  if (!existingImg) {
    // If an element doesn't have an image child
    // node, add it.
    let image = document.createElementNS("http://www.w3.org/1999/xhtml", "img");
    image.setAttribute("src", src);
    image.setAttribute("data-l10n-name", "icon");
    image.setAttribute("role", "presentation");
    image.classList.add("extension-controlled-icon");
    elem.appendChild(image);
  } else if (existingImg.getAttribute("src") !== src) {
    existingImg.setAttribute("src", src);
  }

  const l10nId = settingNameToL10nID(settingName);
  document.l10n.setAttributes(elem, l10nId, {
    name: addon.name,
  });
}

async function showControllingExtension(settingName, addon) {
  // Tell the user what extension is controlling the setting.
  let elements = getControllingExtensionEls(settingName);

  elements.section.classList.remove("extension-controlled-disabled");
  let description = elements.description;

  setControllingExtensionDescription(description, addon, settingName);

  if (elements.button) {
    elements.button.hidden = !(
      addon.permissions & AddonManager.PERM_CAN_DISABLE
    );
  }

  // Show the controlling extension row and hide the old label.
  elements.section.hidden = false;
}

function hideControllingExtension(settingName) {
  let elements = getControllingExtensionEls(settingName);
  elements.section.hidden = true;
  if (elements.button) {
    elements.button.hidden = true;
  }
}

function showEnableExtensionMessage(settingName) {
  let elements = getControllingExtensionEls(settingName);

  elements.button.hidden = true;
  elements.section.classList.add("extension-controlled-disabled");

  elements.description.textContent = "";

  // We replace localization of the <description> with a DOM Fragment containing
  // the enable-extension-enable message. That means a change from:
  //
  // <description data-l10n-id="..."/>
  //
  // to:
  //
  // <description>
  //   <img/>
  //   <label data-l10n-id="..."/>
  // </description>
  //
  // We need to remove the l10n-id annotation from the <description> to prevent
  // Fluent from overwriting the element in case of any retranslation.
  elements.description.removeAttribute("data-l10n-id");

  let icon = (url, name) => {
    let img = document.createElementNS("http://www.w3.org/1999/xhtml", "img");
    img.src = url;
    img.setAttribute("data-l10n-name", name);
    img.setAttribute("role", "presentation");
    img.className = "extension-controlled-icon";
    return img;
  };
  let label = document.createXULElement("label");
  let addonIcon = icon(
    "chrome://mozapps/skin/extensions/extensionGeneric.svg",
    "addons-icon"
  );
  let toolbarIcon = icon("chrome://browser/skin/menu.svg", "menu-icon");
  label.appendChild(addonIcon);
  label.appendChild(toolbarIcon);
  document.l10n.setAttributes(label, "extension-controlled-enable");
  elements.description.appendChild(label);
  let dismissButton = document.createXULElement("image");
  dismissButton.setAttribute("class", "extension-controlled-icon close-icon");
  dismissButton.addEventListener("click", function dismissHandler() {
    hideControllingExtension(settingName);
    dismissButton.removeEventListener("click", dismissHandler);
  });
  elements.description.appendChild(dismissButton);
}

function makeDisableControllingExtension(type, settingName) {
  return async function disableExtension() {
    let { id } = await getControllingExtensionInfo(type, settingName);
    let addon = await AddonManager.getAddonByID(id);
    await addon.disable();
  };
}

/**
 *  Initialize listeners though the Management API to update the UI
 *  when an extension is controlling a pref.
 *
 * @param {string} type
 * @param {string} prefId The unique id of the setting
 * @param {HTMLElement} controlledElement
 */
async function initListenersForPrefChange(type, prefId, controlledElement) {
  await Management.asyncLoadSettingsModules();

  let managementObserver = async () => {
    let managementControlled = await handleControllingExtension(type, prefId);
    // Enterprise policy may have locked the pref, so we need to preserve that
    controlledElement.disabled =
      managementControlled || Services.prefs.prefIsLocked(prefId);
  };
  managementObserver();
  Management.on(`extension-setting-changed:${prefId}`, managementObserver);

  window.addEventListener("unload", () => {
    Management.off(`extension-setting-changed:${prefId}`, managementObserver);
  });
}

function initializeProxyUI(container) {
  let deferredUpdate = new DeferredTask(() => {
    container.updateProxySettingsUI();
  }, 10);
  let proxyObserver = {
    observe: (subject, topic, data) => {
      if (API_PROXY_PREFS.includes(data)) {
        deferredUpdate.arm();
      }
    },
  };
  Services.prefs.addObserver("", proxyObserver);
  window.addEventListener("unload", () => {
    Services.prefs.removeObserver("", proxyObserver);
  });
}
