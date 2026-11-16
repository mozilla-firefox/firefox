/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Services = object with smart getters for common XPCOM services
var { AppConstants } = ChromeUtils.importESModule(
  "resource://gre/modules/AppConstants.sys.mjs"
);
var { XPCOMUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/XPCOMUtils.sys.mjs"
);

ChromeUtils.defineESModuleGetters(this, {
  AboutNewTab: "resource:///modules/AboutNewTab.sys.mjs",
  AIWindow:
    "moz-src:///browser/components/aiwindow/ui/modules/AIWindow.sys.mjs",
  BrowserUtils: "resource://gre/modules/BrowserUtils.sys.mjs",
  BrowserWindowTracker: "resource:///modules/BrowserWindowTracker.sys.mjs",
  ContainerCreationPanel:
    "chrome://browser/content/usercontext/ContainerCreationPanel.mjs",
  ContextualIdentityService:
    "moz-src:///toolkit/components/contextualidentity/ContextualIdentityService.sys.mjs",
  ExtensionSettingsStore:
    "resource://gre/modules/ExtensionSettingsStore.sys.mjs",
  ExtensionUtils: "resource://gre/modules/ExtensionUtils.sys.mjs",
  PrivateBrowsingUtils: "resource://gre/modules/PrivateBrowsingUtils.sys.mjs",
  Referrals: "moz-src:///browser/components/referrals/Referrals.sys.mjs",
  ShellService: "moz-src:///browser/components/shell/ShellService.sys.mjs",
  URILoadingHelper: "resource:///modules/URILoadingHelper.sys.mjs",
});

ChromeUtils.defineLazyGetter(this, "ReferrerInfo", () =>
  Components.Constructor(
    "@mozilla.org/referrer-info;1",
    "nsIReferrerInfo",
    "init"
  )
);

Object.defineProperty(this, "BROWSER_NEW_TAB_URL", {
  enumerable: true,
  get() {
    if (PrivateBrowsingUtils.isWindowPrivate(window)) {
      if (
        !PrivateBrowsingUtils.permanentPrivateBrowsing &&
        !AboutNewTab.newTabURLOverridden
      ) {
        return "about:privatebrowsing";
      }
      // If an extension controls the setting and does not have private
      // browsing permission, use the default setting.
      let extensionControlled = Services.prefs.getBoolPref(
        "browser.newtab.extensionControlled",
        false
      );
      let privateAllowed = Services.prefs.getBoolPref(
        "browser.newtab.privateAllowed",
        false
      );
      // There is a potential on upgrade that the prefs are not set yet, so we double check
      // for moz-extension.
      if (
        !privateAllowed &&
        (extensionControlled ||
          ExtensionUtils.isExtensionUrl(AboutNewTab.newTabURL))
      ) {
        return "about:privatebrowsing";
      }
    }
    if (AIWindow.isAIWindowActive(window)) {
      return AIWindow.newTabURL;
    }
    return AboutNewTab.newTabURL;
  },
});

var TAB_DROP_TYPE = "application/x-moz-tabbrowser-tab";

var gBidiUI = false;

/**
 * Determines whether the given url is considered a special URL for new tabs.
 */
function isBlankPageURL(aURL) {
  return (
    aURL == "about:blank" ||
    aURL == "about:home" ||
    aURL == BROWSER_NEW_TAB_URL ||
    aURL == "chrome://browser/content/blanktab.html"
  );
}

function doGetProtocolFlags(aURI) {
  return Services.io.getDynamicProtocolFlags(aURI);
}

function openUILink(
  url,
  event,
  aIgnoreButton,
  aIgnoreAlt,
  aAllowThirdPartyFixup,
  aPostData,
  aReferrerInfo
) {
  return URILoadingHelper.openUILink(
    window,
    url,
    event,
    aIgnoreButton,
    aIgnoreAlt,
    aAllowThirdPartyFixup,
    aPostData,
    aReferrerInfo
  );
}

function openTrustedLinkIn(url, where, params) {
  URILoadingHelper.openTrustedLinkIn(window, url, where, params);
}

function openWebLinkIn(url, where, params) {
  URILoadingHelper.openWebLinkIn(window, url, where, params);
}

function openLinkIn(url, where, params) {
  return URILoadingHelper.openLinkIn(window, url, where, params);
}

// Used as an onclick handler for UI elements with link-like behavior.
// e.g. onclick="checkForMiddleClick(this, event);"
// Not needed for menuitems because those fire command events even on middle clicks.
function checkForMiddleClick(node, event) {
  // We should be using the disabled property here instead of the attribute,
  // but some elements that this function is used with don't support it (e.g.
  // menuitem).
  if (node.hasAttribute("disabled")) {
    return;
  } // Do nothing

  if (event.target.tagName == "menuitem") {
    // Menu items fire command on middle-click by themselves.
    return;
  }

  if (event.button == 1) {
    /* Execute the node's oncommand or command.
     */

    let cmdEvent = document.createEvent("xulcommandevent");
    cmdEvent.initCommandEvent(
      "command",
      true,
      true,
      window,
      0,
      event.ctrlKey,
      event.altKey,
      event.shiftKey,
      event.metaKey,
      0,
      event,
      event.inputSource
    );
    node.dispatchEvent(cmdEvent);

    // Stop the propagation of the click event, to prevent the event from being
    // handled more than once.
    // E.g. see https://bugzilla.mozilla.org/show_bug.cgi?id=1657992#c4
    event.stopPropagation();
    event.preventDefault();

    // If the middle-click was on part of a menu, close the menu.
    // (Menus close automatically with left-click but not with middle-click.)
    closeMenus(event.target);
  }
}

// Populate a menu with user-context menu items. This method should be called
// by onpopupshowing passing the event as first argument.
function createUserContextMenu(
  event,
  {
    target = null,
    isContextMenu = false,
    isPanelList = false,
    excludeUserContextId = 0,
    showDefaultTab = false,
    showAddContainer = true,
    showManageContainers = true,
    containerSource = "unknown",
  } = {}
) {
  target = target || event.target;
  MozXULElement.insertFTLIfNeeded("toolkit/global/contextual-identity.ftl");
  let docfrag = document.createDocumentFragment();
  let createSeparator = isPanelList
    ? () => document.createElement("hr")
    : () => document.createXULElement("menuseparator");
  let onActivate = (item, callback) =>
    item.addEventListener(isPanelList ? "click" : "command", activateEvent => {
      if (!isPanelList) {
        activateEvent.stopPropagation();
      }
      callback();
    });

  // panel-item takes its label from the message value, so the container labels
  // have their own messages rather than the menuitem `.label` attribute ones.
  let panelItemL10nIds = {
    "user-context-new-tab2": "user-context-new-tab2-panel-item",
    "user-context-add-container2": "user-context-add-container2-panel-item",
    "user-context-manage-containers2":
      "user-context-manage-containers2-panel-item",
  };
  let panelListReplacements = l10nId =>
    (isPanelList && panelItemL10nIds[l10nId]) || l10nId;

  let createMenuItem = ({ name = null, l10nId = null }) => {
    let item = isPanelList
      ? document.createElement("panel-item")
      : document.createXULElement("menuitem");
    let setLabel = label => {
      if (isPanelList) {
        item.textContent = label;
      } else {
        item.setAttribute("label", label);
      }
    };

    if (name) {
      setLabel(name);
    } else {
      document.l10n.setAttributes(item, panelListReplacements(l10nId));
    }

    return item;
  };

  // Add an item for a tab without a container, labeled "New Tab".
  if (excludeUserContextId || showDefaultTab) {
    let menuitem = createMenuItem({ l10nId: "user-context-new-tab2" });
    menuitem.setAttribute("data-usercontextid", "0");
    if (!isContextMenu) {
      menuitem.setAttribute("command", "Browser:NewUserContextTab");
      menuitem.setAttribute("data-container-entrypoint", containerSource);
    }

    docfrag.appendChild(menuitem);
    docfrag.appendChild(createSeparator());
  }

  ContextualIdentityService.getPublicIdentities().forEach(identity => {
    if (identity.userContextId == excludeUserContextId) {
      return;
    }

    let menuitem = createMenuItem({
      name: ContextualIdentityService.getUserContextLabel(
        identity.userContextId
      ),
    });
    menuitem.setAttribute("data-usercontextid", identity.userContextId);

    if (!isContextMenu) {
      menuitem.setAttribute("command", "Browser:NewUserContextTab");
      menuitem.setAttribute("data-container-entrypoint", containerSource);
    }

    if (isPanelList) {
      let iconUrl = ContextualIdentityService.getContainerIconURL(
        identity.icon
      );
      menuitem.style.setProperty("--panel-item-icon", `url("${iconUrl}")`);
      menuitem.style.setProperty(
        "--panel-item-fill",
        ContextualIdentityService.getContainerColorCode(identity.color)
      );
    } else {
      menuitem.classList.add("menuitem-iconic");
      menuitem.classList.add("identity-color-" + identity.color);
      menuitem.classList.add("identity-icon-" + identity.icon);
    }

    docfrag.appendChild(menuitem);
  });

  if (showAddContainer || showManageContainers) {
    docfrag.appendChild(createSeparator());
  }

  if (showAddContainer) {
    let menuitem = createMenuItem({ l10nId: "user-context-add-container2" });
    onActivate(menuitem, () =>
      ContainerCreationPanel.open(window, containerSource)
    );
    docfrag.appendChild(menuitem);
  }

  if (showManageContainers) {
    let menuitem = createMenuItem({
      l10nId: "user-context-manage-containers2",
    });
    onActivate(menuitem, () =>
      openPreferences("paneContainers", {
        urlParams: { entrypoint: containerSource },
      })
    );
    docfrag.appendChild(menuitem);
  }

  target.replaceChildren(docfrag);
  return true;
}

// Closes all popups that are ancestors of the node.
function closeMenus(node) {
  if ("tagName" in node) {
    if (
      node.namespaceURI ==
        "http://www.mozilla.org/keymaster/gatekeeper/there.is.only.xul" &&
      (node.tagName == "menupopup" || node.tagName == "popup")
    ) {
      node.hidePopup();
    }

    closeMenus(node.parentNode);
  }
}

/**
 * This function takes in a key element and compares it to the keys pressed during an event.
 *
 * @param aEvent
 *        The KeyboardEvent event you want to compare against your key.
 *
 * @param aKey
 *        The <key> element checked to see if it was called in aEvent.
 *        For example, aKey can be a variable set to document.getElementById("key_close")
 *        to check if the close command key was pressed in aEvent.
 */
function eventMatchesKey(aEvent, aKey) {
  let keyPressed = (aKey.getAttribute("key") || "").toLowerCase();
  let keyModifiers = aKey.getAttribute("modifiers");
  let modifiers = ["Alt", "Control", "Meta", "Shift"];

  if (aEvent.key != keyPressed) {
    return false;
  }
  let eventModifiers = modifiers.filter(modifier =>
    aEvent.getModifierState(modifier)
  );
  // Check if aEvent has a modifier and aKey doesn't
  if (eventModifiers.length && !keyModifiers.length) {
    return false;
  }
  // Check whether aKey's modifiers match aEvent's modifiers
  if (keyModifiers) {
    keyModifiers = keyModifiers.split(/[\s,]+/);
    // Capitalize first letter of aKey's modifers to compare to aEvent's modifier
    keyModifiers.forEach(function (modifier, index) {
      if (modifier == "accel") {
        keyModifiers[index] =
          AppConstants.platform == "macosx" ? "Meta" : "Control";
      } else {
        keyModifiers[index] = modifier[0].toUpperCase() + modifier.slice(1);
      }
    });
    return modifiers.every(
      modifier =>
        keyModifiers.includes(modifier) == aEvent.getModifierState(modifier)
    );
  }
  return true;
}

// Gather all descendent text under given document node.
// NOTE: Keep this in sync with _gatherTextUnder in
// browser/actors/ContextMenuChild.sys.mjs
function gatherTextUnder(root) {
  const encoder = Cu.createDocumentEncoder("text/plain");
  encoder.init(root.ownerDocument, "text/plain", 0);
  encoder.setContainerNode(root);
  return encoder.encodeToString().trim();
}

// This function exists for legacy reasons.
function getShellService() {
  return ShellService;
}

function isBidiEnabled() {
  // first check the pref.
  if (Services.prefs.getBoolPref("bidi.browser.ui", false)) {
    return true;
  }

  // now see if the app locale is an RTL one.
  const isRTL = Services.locale.isAppLocaleRTL;

  if (isRTL) {
    Services.prefs.setBoolPref("bidi.browser.ui", true);
  }
  return isRTL;
}

function openAboutDialog() {
  for (let win of Services.wm.getEnumerator("Browser:About")) {
    // Only open one about window (Bug 599573)
    if (win.closed) {
      continue;
    }
    win.focus();
    return;
  }

  var features = "chrome,";
  if (AppConstants.platform == "win") {
    features += "centerscreen,dependent";
  } else if (AppConstants.platform == "macosx") {
    features += "centerscreen,resizable=no,minimizable=no";
  } else {
    features += "centerscreen,dependent,dialog=no";
  }

  var win = BrowserWindowTracker.getTopWindow() || window;
  win.openDialog("chrome://browser/content/aboutDialog.xhtml", "", features);
}

function openReferralsPage() {
  Referrals.openReferralsTab(window, "help_menu");
}

async function openPreferences(paneID, extraArgs) {
  // This function is duplicated from preferences.js.
  function internalPrefCategoryNameToFriendlyName(aName) {
    return (aName || "").replace(/^pane./, function (toReplace) {
      return toReplace[4].toLowerCase();
    });
  }

  let win = Services.wm.getMostRecentWindow("navigator:browser");
  let friendlyCategoryName = internalPrefCategoryNameToFriendlyName(paneID);
  let params;
  if (extraArgs && extraArgs.urlParams) {
    params = new URLSearchParams();
    let urlParams = extraArgs.urlParams;
    for (let name in urlParams) {
      if (urlParams[name] !== undefined) {
        params.set(name, urlParams[name]);
      }
    }
  }
  let preferencesURLSuffix =
    (params ? "?" + params : "") +
    (friendlyCategoryName ? "#" + friendlyCategoryName : "");
  let newLoad = true;
  let browser = null;
  if (!win) {
    let windowArguments = Cc["@mozilla.org/array;1"].createInstance(
      Ci.nsIMutableArray
    );
    let supportsStringPrefURL = Cc[
      "@mozilla.org/supports-string;1"
    ].createInstance(Ci.nsISupportsString);
    supportsStringPrefURL.data = "about:preferences" + preferencesURLSuffix;
    windowArguments.appendElement(supportsStringPrefURL);

    win = Services.ww.openWindow(
      null,
      AppConstants.BROWSER_CHROME_URL,
      "_blank",
      "chrome,dialog=no,all",
      windowArguments
    );
  } else {
    let shouldReplaceFragment = friendlyCategoryName
      ? "whenComparingAndReplace"
      : "whenComparing";
    newLoad = !win.switchToTabHavingURI(
      "about:settings" + preferencesURLSuffix,
      false,
      {
        ignoreFragment: shouldReplaceFragment,
        replaceQueryString: true,
        triggeringPrincipal:
          Services.scriptSecurityManager.getSystemPrincipal(),
      }
    );
    if (newLoad) {
      newLoad = !win.switchToTabHavingURI(
        "about:preferences" + preferencesURLSuffix,
        true,
        {
          ignoreFragment: shouldReplaceFragment,
          replaceQueryString: true,
          triggeringPrincipal:
            Services.scriptSecurityManager.getSystemPrincipal(),
        }
      );
    }
    browser = win.gBrowser.selectedBrowser;
  }

  if (!newLoad && paneID) {
    if (browser.contentDocument?.readyState != "complete") {
      await new Promise(resolve => {
        browser.addEventListener("load", resolve, {
          capture: true,
          once: true,
        });
      });
    }
    browser.contentWindow.gotoPref(paneID);
  }
}

/**
 * Opens the troubleshooting information (about:support) page for this version
 * of the application.
 */
function openTroubleshootingPage() {
  openTrustedLinkIn("about:support", "tab");
}

/**
 * Opens the feedback page for this version of the application.
 */
function openFeedbackPage() {
  var url = Services.urlFormatter.formatURLPref("app.feedback.baseURL");
  openTrustedLinkIn(url, "tab");
}

/**
 * Appends UTM parameters to then opens the SUMO URL for device migration.
 */
function openSwitchingDevicesPage() {
  let url = getHelpLinkURL("switching-devices");
  let parsedUrl = new URL(url);
  parsedUrl.searchParams.set("utm_content", "switch-to-new-device");
  parsedUrl.searchParams.set("utm_source", "help-menu");
  parsedUrl.searchParams.set("utm_medium", "firefox-desktop");
  parsedUrl.searchParams.set("utm_campaign", "migration");
  openTrustedLinkIn(parsedUrl.href, "tab");
}

function buildHelpMenu() {
  document.getElementById("feedbackPage").disabled =
    !Services.policies.isAllowed("feedbackCommands");

  document.getElementById("helpSafeMode").disabled =
    !Services.policies.isAllowed("safeMode");

  document.getElementById("troubleShooting").disabled =
    !Services.policies.isAllowed("aboutSupport");

  document.getElementById("menu_referralsPage").hidden =
    !Services.prefs.getBoolPref("browser.referrals.enabled");

  let supportMenu = Services.policies.getSupportMenu();
  if (supportMenu) {
    let menuitem = document.getElementById("helpPolicySupport");
    menuitem.hidden = false;
    menuitem.setAttribute("label", supportMenu.Title);
    if ("AccessKey" in supportMenu) {
      menuitem.setAttribute("accesskey", supportMenu.AccessKey);
    }
    document.getElementById("helpPolicySeparator").hidden = false;
  }

  // Enable/disable the "Report Web Forgery" menu item.
  if (typeof gSafeBrowsing != "undefined") {
    gSafeBrowsing.setReportPhishingMenu();
  }
}

function isElementVisible(aElement) {
  if (!aElement) {
    return false;
  }

  // If aElement or a direct or indirect parent is hidden or collapsed,
  // height, width or both will be 0.
  var rect = aElement.getBoundingClientRect();
  return rect.height > 0 && rect.width > 0;
}

function makeURLAbsolute(aBase, aUrl) {
  // Note:  makeURI() will throw if aUri is not a valid URI
  return makeURI(aUrl, null, makeURI(aBase)).spec;
}

function getHelpLinkURL(aHelpTopic) {
  var url = Services.urlFormatter.formatURLPref("app.support.baseURL");
  return url + aHelpTopic;
}

function openHelpLink(aHelpTopic) {
  openTrustedLinkIn(getHelpLinkURL(aHelpTopic), "tab");
}
