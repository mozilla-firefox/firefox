/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* globals browser */

"use strict";

const SMARTBLOCK_EMBED_OBSERVER_TIMEOUT_MS = 10000;

/**
 * Helper library to create shims for Smartblock Embeds
 *
 */
const embedHelperLib = (() => {
  let prevRanShims = new Set();
  let originalEmbedContainers = [];
  let embedPlaceholders = [];
  let observerTimeout;
  let newEmbedObserver;

  function sendMessageToAddon(message, shimId) {
    return browser.runtime.sendMessage({ message, shimId });
  }

  function addonMessageHandler(message, SHIM_INFO) {
    const { topic, shimId: sendingShimId } = message;
    const { shimId: handlingShimId, scriptURL } = SHIM_INFO;

    // Only react to messages which are targeting this shim.
    if (sendingShimId != handlingShimId) {
      return;
    }

    if (topic === "smartblock:unblock-embed") {
      if (newEmbedObserver) {
        newEmbedObserver.disconnect();
        newEmbedObserver = null;
      }

      if (observerTimeout) {
        clearTimeout(observerTimeout);
        observerTimeout = null;
      }

      // remove embed placeholders
      embedPlaceholders.forEach((p, idx) => {
        p.replaceWith(originalEmbedContainers[idx]);
      });

      // recreate scripts
      let scriptElement = document.createElement("script");

      // Set the script element's src with the website's principal instead of
      // the content script principal to ensure the tracker script is not loaded
      // via the content script's expanded principal.
      scriptElement.wrappedJSObject.src = scriptURL;
      document.body.appendChild(scriptElement);
    }
  }

  /**
   * Replaces embeds with a SmartBlock Embed placeholder. Optionally takes a list
   * of embeds to replace, otherwise will search for all embeds on the page.
   *
   * @param {HTMLElement[]} embedContainers - Array of elements to replace with placeholders.
   *                                  If the array is empty, this function will search
   *                                  for and replace all embeds on the page.
   *
   * @param {object} SHIM_INFO - Information about the shim wrapped in an object.
   */
  async function createShimPlaceholders(embedContainers, SHIM_INFO) {
    const { shimId, embedSelector, embedLogoURL, isTestShim } = SHIM_INFO;

    const [titleString, descriptionString, buttonString] =
      await sendMessageToAddon("smartblockGetFluentString", shimId);

    if (!embedContainers.length) {
      // No containers were passed in, do own search for containers
      embedContainers = document.querySelectorAll(embedSelector);
    }

    embedContainers.forEach(originalContainer => {
      // this string has to be defined within this function to avoid linting errors
      // see: https://github.com/mozilla/eslint-plugin-no-unsanitized/issues/259
      const SMARTBLOCK_PLACEHOLDER_HTML_STRING = `
        <style>
          #smartblock-placeholder-wrapper {
            min-height: 225px;
            width: 400px;
            padding: 32px 24px;
  
            display: block;
            align-content: center;
            text-align: center;
  
            background-color: light-dark(rgb(255, 255, 255), rgb(28, 27, 34));
            color: light-dark(rgb(43, 42, 51), rgb(251, 251, 254));
  
            border-radius: 8px;
            border: 2px dashed #0250bb;
  
            font-size: 14px;
            line-height: 1.2;
            font-family: system-ui;
          }
  
          #smartblock-placeholder-button {
            min-height: 32px;
            padding: 8px 14px;
  
            border-radius: 4px;
            font-weight: 600;
            border: 0;
  
            /* Colours match light/dark theme from
              https://searchfox.org/mozilla-central/source/browser/themes/addons/light/manifest.json
              https://searchfox.org/mozilla-central/source/browser/themes/addons/dark/manifest.json */
            background-color: light-dark(rgb(0, 97, 224), rgb(0, 221, 255));
            color: light-dark(rgb(251, 251, 254), rgb(43, 42, 51));
          }
  
          #smartblock-placeholder-button:hover {
            /* Colours match light/dark theme from
              https://searchfox.org/mozilla-central/source/browser/themes/addons/light/manifest.json
              https://searchfox.org/mozilla-central/source/browser/themes/addons/dark/manifest.json */
            background-color: light-dark(rgb(2, 80, 187), rgb(128, 235, 255));
          }
  
          #smartblock-placeholder-button:hover:active {
            /* Colours match light/dark theme from
              https://searchfox.org/mozilla-central/source/browser/themes/addons/light/manifest.json
              https://searchfox.org/mozilla-central/source/browser/themes/addons/dark/manifest.json */
            background-color: light-dark(rgb(5, 62, 148), rgb(170, 242, 255));
          }
  
          #smartblock-placeholder-title {
            margin-block: 14px;
            font-size: 16px;
            font-weight: bold;
          }
  
          #smartblock-placeholder-desc {
            margin-block: 14px;
          }
        </style>
        <div id="smartblock-placeholder-wrapper">
          <img id="smartblock-placeholder-image" width="24" height="24" />
          <p id="smartblock-placeholder-title"></p>
          <p id="smartblock-placeholder-desc"></p>
          <button id="smartblock-placeholder-button"></button>
        </div>`;

      // Create the placeholder inside a shadow dom
      const placeholderDiv = document.createElement("div");

      if (isTestShim) {
        // Tag the div with a class to make it easily detectable FOR THE TEST SHIM ONLY
        placeholderDiv.classList.add("shimmed-embedded-content");
      }

      const shadowRoot = placeholderDiv.attachShadow({ mode: "closed" });

      shadowRoot.innerHTML = SMARTBLOCK_PLACEHOLDER_HTML_STRING;
      shadowRoot.getElementById("smartblock-placeholder-image").src =
        embedLogoURL;
      shadowRoot.getElementById("smartblock-placeholder-title").textContent =
        titleString;
      shadowRoot.getElementById("smartblock-placeholder-desc").textContent =
        descriptionString;
      shadowRoot.getElementById("smartblock-placeholder-button").textContent =
        buttonString;

      // Wait for user to opt-in.
      shadowRoot
        .getElementById("smartblock-placeholder-button")
        .addEventListener("click", ({ isTrusted }) => {
          if (!isTrusted) {
            return;
          }
          // Send a message to the addon to allow loading tracking resources
          // needed by the embed.
          sendMessageToAddon("embedClicked", shimId);
        });

      // Save the original embed element and the newly created placeholder
      embedPlaceholders.push(placeholderDiv);
      originalEmbedContainers.push(originalContainer);

      // Replace the embed with the placeholder
      originalContainer.replaceWith(placeholderDiv);

      sendMessageToAddon("smartblockEmbedReplaced", shimId);
    });

    if (isTestShim) {
      // Dispatch event to signal that the script is done replacing FOR TEST SHIM ONLY
      const finishedEvent = new CustomEvent("smartblockEmbedScriptFinished", {
        bubbles: true,
        composed: true,
      });
      window.dispatchEvent(finishedEvent);
    }
  }

  /**
   * Creates a mutation observer to observe new changes after page load to monitor for
   * new embeds.
   *
   * @param {object} SHIM_INFO - Information about the shim wrapped in an object.
   */
  function createEmbedMutationObserver(SHIM_INFO) {
    const { embedSelector } = SHIM_INFO;

    // Monitor for new embeds being added after page load so we can replace them
    // with placeholders.
    newEmbedObserver = new MutationObserver(mutations => {
      for (let { addedNodes, target, type } of mutations) {
        const nodes = type === "attributes" ? [target] : addedNodes;
        for (const node of nodes) {
          if (node.matches(embedSelector)) {
            // If node is an embed, replace with placeholder
            createShimPlaceholders([node], SHIM_INFO);
          } else {
            // If node is not an embed, check if any children are
            // and replace if needed
            let maybeEmbedNodeList = node.querySelectorAll?.(embedSelector);
            if (maybeEmbedNodeList) {
              createShimPlaceholders(maybeEmbedNodeList, SHIM_INFO);
            }
          }
        }
      }
    });

    newEmbedObserver.observe(document.documentElement, {
      childList: true,
      subtree: true,
      attributes: true,
      attributeFilter: ["id", "class"],
    });

    // Disconnect the mutation observer after a fixed (long) timeout to conserve resources.
    observerTimeout = setTimeout(() => {
      if (newEmbedObserver) {
        newEmbedObserver.disconnect();
      }
    }, SMARTBLOCK_EMBED_OBSERVER_TIMEOUT_MS);
  }

  /**
   * Initializes a smartblock embeds shim on the page.
   *
   * @param {object} SHIM_INFO - Information about the shim wrapped in an object.
   */
  function initEmbedShim(SHIM_INFO) {
    let { shimId } = SHIM_INFO;

    if (prevRanShims.has(shimId)) {
      // we should not init shims twice
      return;
    }

    prevRanShims.add(shimId);

    // Listen for messages from the background script.
    browser.runtime.onMessage.addListener(request => {
      addonMessageHandler(request, SHIM_INFO);
    });

    // Listen for page changes in case of new embeds
    createEmbedMutationObserver(SHIM_INFO);

    // Run placeholder creation
    createShimPlaceholders([], SHIM_INFO);
  }

  return {
    initEmbedShim,
  };
})();
