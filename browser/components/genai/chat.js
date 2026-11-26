/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const { topChromeWindow } = window.browsingContext;

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  GenAI: "resource:///modules/GenAI.sys.mjs",
  SpecialMessageActions:
    "resource://messaging-system/lib/SpecialMessageActions.sys.mjs",
});
const { XPCOMUtils } = ChromeUtils.importESModule(
  "resource://gre/modules/XPCOMUtils.sys.mjs"
);

// Define actions for onboarding and chatbot
const ACTIONS = Object.freeze({
  CHATBOT_PERSIST: "chatbot:persist",
  CHATBOT_REVERT: "chatbot:revert",
  CHATBOT_SELECT: "chatbot:select",
  CHATBOT_SUPPORT: "chatbot:support",
  OPEN_URL: "OPEN_URL",
});

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "providerPref",
  "browser.ml.chat.provider",
  null,
  renderProviders
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "shortcutsPref",
  "browser.ml.chat.shortcuts"
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "sidebarRevampPref",
  "sidebar.revamp"
);
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "onboardingConfig",
  "browser.ml.chat.onboarding.config",
  JSON.stringify({
    id: "chatbot",
    template: "multistage",
    transitions: true,
    screens: [
      {
        id: "chat_pick",
        content: {
          fullscreen: true,
          hide_secondary_section: "responsive",
          narrow: true,
          position: "split",

          title: {
            fontWeight: 400,
            string_id: "genai-onboarding-choose-header",
          },
          cta_paragraph: {
            text: {
              string_id: "genai-onboarding-choose-description",
              string_name: "learn-more",
            },
            action: {
              type: ACTIONS.CHATBOT_SUPPORT,
            },
          },
          above_button_content: [
            // Placeholder to inject on provider change
            {
              text: " ",
              type: "text",
            },
          ],
          primary_button: {
            action: {
              navigate: true,
              type: ACTIONS.CHATBOT_PERSIST,
            },
            label: { string_id: "genai-onboarding-primary" },
          },
          additional_button: {
            action: { dismiss: true, type: ACTIONS.CHATBOT_REVERT },
            label: { string_id: "genai-onboarding-secondary" },
            style: "link",
          },
          progress_bar: true,
        },
      },
    ],
  })
);

ChromeUtils.defineLazyGetter(
  lazy,
  "supportLink",
  () =>
    Services.urlFormatter.formatURLPref("app.support.baseURL") + "ai-chatbot"
);

const node = {};

function closeSidebar() {
  topChromeWindow.SidebarController.hide();
}

function openLink(url) {
  topChromeWindow.openLinkIn(url, "tabshifted", {
    triggeringPrincipal: Services.scriptSecurityManager.createNullPrincipal({}),
  });
}

function request(url = lazy.providerPref) {
  try {
    node.chat.fixupAndLoadURIString(url, {
      triggeringPrincipal: Services.scriptSecurityManager.createNullPrincipal(
        {}
      ),
    });
  } catch (ex) {
    console.error("Failed to request chat provider", ex);
  }
}

function renderChat() {
  const browser = document.createXULElement("browser");
  const browserContainer = document.getElementById("browser-container");
  browser.setAttribute("disableglobalhistory", "true");
  browser.setAttribute("maychangeremoteness", "true");
  browser.setAttribute("messagemanagergroup", "chatbot-browser");
  browser.setAttribute("nodefaultsrc", "true");
  browser.setAttribute("remote", "true");
  browser.setAttribute("type", "content");
  return browserContainer.appendChild(browser);
}

async function renderProviders() {
  // Skip potential pref change callback when unloading
  if ((await document.visibilityState) == "hidden") {
    return null;
  }

  const select = document.getElementById("provider");
  select.innerHTML = "";
  let selected = false;

  const addOption = (text = "", val = "") => {
    const option = select.appendChild(document.createElement("option"));
    option.textContent = text;
    option.value = val;
    return option;
  };

  // Add the known providers in order while looking for current selection
  lazy.GenAI.chatProviders.forEach((data, url) => {
    const option = addOption(data.name, url);
    if (lazy.providerPref == url) {
      option.selected = true;
      selected = true;
    } else if (data.hidden) {
      option.hidden = true;
    }
  });

  // Must be a custom preference if provider wasn't found
  if (!selected) {
    const option = addOption(lazy.providerPref, lazy.providerPref);
    option.selected = true;
    if (!lazy.providerPref) {
      showOnboarding();
    }
  }

  // Add extra controls after the providers
  select.appendChild(document.createElement("hr"));
  document.l10n.setAttributes(addOption(), "genai-provider-view-details");

  // Update provider telemetry
  const providerId = lazy.GenAI.getProviderId(lazy.providerPref);
  Glean.genaiChatbot.provider.set(providerId);
  if (renderProviders.lastId && document.hasFocus()) {
    Glean.genaiChatbot.providerChange.record({
      current: providerId,
      previous: renderProviders.lastId,
      surface: "panel",
    });
  }
  renderProviders.lastId = providerId;

  // Load the requested provider
  request();
  return select;
}

function renderMore() {
  const button = document.getElementById("header-more");
  button.addEventListener("click", () => {
    const topDoc = topChromeWindow.document;
    let menu = topDoc.getElementById("chatbot-menupopup");
    if (!menu) {
      menu = topDoc
        .getElementById("mainPopupSet")
        .appendChild(topDoc.createXULElement("menupopup"));
      menu.id = "chatbot-menupopup";
      node.menu = menu;
      menu.addEventListener("popuphidden", () => {
        button.setAttribute("aria-expanded", false);
      });
    }
    menu.innerHTML = "";

    const provider = lazy.GenAI.chatProviders.get(lazy.providerPref)?.name;
    const providerId = lazy.GenAI.getProviderId();
    [
      [
        "menuitem",
        [
          provider
            ? "genai-options-reload-provider"
            : "genai-options-reload-generic",
          { provider },
        ],
        function reload() {
          request();
        },
      ],
      ["menuseparator"],
      [
        "menuitem",
        ["genai-options-show-shortcut"],
        function show_shortcuts() {
          Services.prefs.setBoolPref("browser.ml.chat.shortcuts", true);
        },
        lazy.shortcutsPref,
      ],
      [
        "menuitem",
        ["genai-options-hide-shortcut"],
        function hide_shortcuts() {
          Services.prefs.setBoolPref("browser.ml.chat.shortcuts", false);
        },
        !lazy.shortcutsPref,
      ],
      ["menuseparator"],
      [
        "menuitem",
        ["genai-options-about-chatbot"],
        function about() {
          openLink(lazy.supportLink);
        },
      ],
    ].forEach(([type, l10n, command, checked]) => {
      const item = menu.appendChild(topDoc.createXULElement(type));
      if (type != "menuitem") {
        return;
      }
      document.l10n.setAttributes(item, ...l10n);
      item.addEventListener("command", () => {
        command();
        Glean.genaiChatbot.sidebarMoreMenuClick.record({
          action: command.name,
          provider: providerId,
        });
      });
      if (checked !== undefined) {
        item.setAttribute("type", "checkbox");
        if (checked) {
          item.setAttribute("checked", true);
        }
      }
    });
    menu.openPopup(button, "after_start");
    button.setAttribute("aria-expanded", true);
    Glean.genaiChatbot.sidebarMoreMenuDisplay.record({ provider: providerId });
  });
}

function handleChange({ target }) {
  const { value } = target;
  switch (target) {
    case node.provider:
      // Special behavior to show first screen of onboarding
      if (value == "") {
        target.value = lazy.providerPref;
        showOnboarding(1);
        Glean.genaiChatbot.sidebarProviderMenuClick.record({
          action: "details",
          provider: lazy.GenAI.getProviderId(),
        });
      } else {
        Services.prefs.setStringPref("browser.ml.chat.provider", value);
        // Reset Permissions UI by changing provider
        topChromeWindow.dispatchEvent(
          new CustomEvent("sidebarbrowserchanged", {
            bubble: true,
          })
        );
      }
      break;
  }
}
addEventListener("change", handleChange);

// Only allow summarizing pages that have a URL the provider can access
function updateSummarizeButton() {
  document.getElementById("summarize-button").disabled = !lazy.GenAI.getPageUrl(
    topChromeWindow.gBrowser.selectedBrowser
  );
}

const summarizeProgressListener = {
  onLocationChange(webProgress) {
    if (webProgress.isTopLevel) {
      updateSummarizeButton();
    }
  },
};

// Expose a promise for loading and rendering the chat browser element
var browserPromise = new Promise((resolve, reject) => {
  addEventListener("load", async () => {
    try {
      node.chat = renderChat();
      node.provider = await renderProviders();
      renderMore();
      resolve(node.chat);
      document.getElementById("header-close").addEventListener("click", () => {
        closeSidebar();
        Glean.genaiChatbot.sidebarCloseClick.record({
          provider: lazy.GenAI.getProviderId(),
        });
      });
      document
        .getElementById("summarize-button")
        .addEventListener("click", async () => {
          const badgeKey = "browser.ml.chat.page.footerBadge";
          const newBadgePref = Services.prefs.getBoolPref(badgeKey);

          if (newBadgePref) {
            Services.prefs.setBoolPref(badgeKey, false);
          }
          await lazy.GenAI.summarizeCurrentPage(topChromeWindow, "footer");
        });
      updateSummarizeButton();
      topChromeWindow.gBrowser.addProgressListener(summarizeProgressListener);
    } catch (ex) {
      console.error("Failed to render on load", ex);
      reject(ex);
    }

    Glean.genaiChatbot.sidebarToggle.record({
      opened: true,
      provider: lazy.GenAI.getProviderId(),
      reason: "load",
      version: lazy.sidebarRevampPref ? "new" : "old",
    });
  });
});

addEventListener("unload", () => {
  node.menu?.remove();
  topChromeWindow.gBrowser.removeProgressListener(summarizeProgressListener);
  Glean.genaiChatbot.sidebarToggle.record({
    opened: false,
    provider: lazy.GenAI.getProviderId(),
    reason: "unload",
    version: lazy.sidebarRevampPref ? "new" : "old",
  });
});

/**
 * Show onboarding screens
 *
 * @param {number} length optional show fewer screens
 */
function showOnboarding(length) {
  // Insert onboarding container and render with script
  const root = document.createElement("div");
  root.id = "multi-stage-message-root";
  document.getElementById(root.id)?.remove();
  document.body.prepend(root);
  history.replaceState("", "");
  const script = document.head.appendChild(document.createElement("script"));
  script.src = "chrome://browser/content/aboutwelcome/aboutwelcome.bundle.js";

  // Convert provider data for lookup by id
  const providerConfigs = new Map();
  lazy.GenAI.chatProviders.forEach((data, url) => {
    if (!data.hidden) {
      providerConfigs.set(data.id, { ...data, url });
    }
  });

  // Define various AW* functions to control aboutwelcome bundle behavior
  Object.assign(window, {
    AWEvaluateScreenTargeting(screens) {
      return screens;
    },
    AWFinish() {
      if (lazy.providerPref == "") {
        closeSidebar();
      }
      root.remove();

      // Indicate onboarding finished and allow another
      showOnboarding.resolve();
      onboardingPromise = new Promise(resolve => {
        showOnboarding.resolve = resolve;
      });
    },
    AWGetFeatureConfig() {
      const onboarding = JSON.parse(lazy.onboardingConfig);
      const providerTiles = {
        action: { picker: "<event>" },
        data: [...providerConfigs.values()].map(config => ({
          action: { type: ACTIONS.CHATBOT_SELECT, config },
          id: config.id,
          label: config.name,
          tooltip: { string_id: config.tooltipId },
        })),
        // Default to nothing selected
        selected: " ",
        subtitle: { string_id: "genai-onboarding-choose-header" },
        type: "single-select",
      };
      // Insert provider tiles on the first screen
      onboarding.screens[0].content.tiles = providerTiles;
      // Remove extra screens if any
      onboarding.screens = onboarding.screens.slice(0, length);
      return onboarding;
    },
    AWGetInstalledAddons() {},
    AWGetSelectedTheme() {
      const primary = document.querySelector(".primary");
      if (primary) {
        primary.disabled = true;
      }
    },
    AWSendEventTelemetry({ event, event_context: { source } }) {
      const { provider } = window.AWSendEventTelemetry;
      const step = 1;
      switch (true) {
        case event == "IMPRESSION":
          Glean.genaiChatbot.onboardingProviderChoiceDisplayed.record({
            provider: lazy.GenAI.getProviderId(lazy.providerPref),
            step,
          });
          break;
        case source == "cta_paragraph":
          Glean.genaiChatbot.onboardingLearnMore.record({ provider, step });
          break;
        case source == "primary_button":
          Glean.genaiChatbot.onboardingFinish.record({ provider, step });
          break;
        case source == "additional_button":
          Glean.genaiChatbot.onboardingClose.record({ provider, step });
          break;
        case source.startsWith("link"):
          Glean.genaiChatbot.onboardingProviderTerms.record({
            provider,
            step,
            text: source,
          });
          break;
        // Assume generic click not yet handled above single select of provider
        case event == "CLICK_BUTTON":
          window.AWSendEventTelemetry.provider = source;
          Glean.genaiChatbot.onboardingProviderSelection.record({
            provider: source,
            step,
          });
          break;
      }
    },
    AWSendToParent(_message, action) {
      switch (action.type) {
        case ACTIONS.OPEN_URL:
          lazy.SpecialMessageActions.handleAction(action, topChromeWindow);
          return;
        case ACTIONS.CHATBOT_PERSIST: {
          const { value } = document.querySelector(
            "label:has(.selected) input"
          );
          Services.prefs.setStringPref(
            "browser.ml.chat.provider",
            providerConfigs.get(value).url
          );
          break;
        }
        case ACTIONS.CHATBOT_REVERT: {
          request();
          break;
        }
        // Handle single select provider choice
        case ACTIONS.CHATBOT_SELECT: {
          const { config } = action;
          if (!config) {
            break;
          }

          request(config.url);
          document.querySelector(".primary").disabled = false;

          // Set max-height to trigger transition
          document.querySelectorAll("label .text div").forEach(div => {
            const selected =
              div.closest("label").querySelector("input").value == config.id;
            div.style.maxHeight = selected ? div.scrollHeight + "px" : 0;
            const a = div.querySelector("a");
            if (a) {
              a.tabIndex = selected ? 0 : -1;
            }
          });

          // Update potentially multiple links for the provider
          const links = document.querySelector(".link-paragraph");
          if (links && links.dataset.l10nId != config.linksId) {
            links.innerHTML = "";
            for (let i = 1; i <= 3; i++) {
              const link = links.appendChild(document.createElement("a"));
              const name = (link.dataset.l10nName = `link${i}`);
              link.href = config[name];
              link.setAttribute("value", name);
            }
            document.l10n.setAttributes(links, config.linksId);

            const handleLink = ev => {
              const { href } = ev.target;
              if (href) {
                ev.preventDefault();
                openLink(href);
              }
            };

            if (!links._listenerAdded) {
              links?.addEventListener("click", handleLink);
              links._listenerAdded = true;
            }
          }

          break;
        }
        case ACTIONS.CHATBOT_SUPPORT:
          openLink(lazy.supportLink);
          break;
      }
    },
  });
}

// Expose a promise for onboarding finishing
var onboardingPromise = new Promise(resolve => {
  showOnboarding.resolve = resolve;
});

window.addEventListener("SidebarFocused", () =>
  document.querySelector("#browser-container browser").focus()
);
