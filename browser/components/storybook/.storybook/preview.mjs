/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { setCustomElementsManifest } from "@storybook/web-components";
import { withActions } from "storybook/actions/decorator";
import { css, html } from "lit.all.mjs";
import { MozLitElement } from "toolkit/content/widgets/lit-utils.mjs";
import customElementsManifest from "../custom-elements.json";
import { insertFTLIfNeeded, connectFluent } from "./fluent-utils.mjs";
import { chromeMap } from "./chrome-map.js";
import DocsContainer from "./DocsContainer.jsx";

// Base Fluent set up.
connectFluent();

// Any fluent imports should go through MozXULElement.insertFTLIfNeeded.
window.MozXULElement = {
  insertFTLIfNeeded,
};

// Used to set prefs in unprivileged contexts.
window.RPMSetPref = () => {
  /* NOOP */
};
window.RPMGetFormatURLPref = () => {
  /* NOOP */
};

// Stub chrome-only globals so modules that touch them at top level (e.g.
// XPCOMUtils.declareLazy for prefs) can still be evaluated in Storybook.
window.ChromeUtils = {
  importESModule: () => ({
    XPCOMUtils: {
      declareLazy: () => ({}),
    },
  }),
};

const reusableComponentModules = import.meta.glob("./moz-*/*.mjs", {
  base: "../../../../toolkit/content/widgets",
});

/**
 * Function to automatically import reusable components into all stories. This
 * helps ensure that components composed of multiple `moz-` elements will render
 * correctly, since these elements would otherwise be lazily imported.
 */
function importReusableComponents() {
  let sourceMap = chromeMap[2];
  let mozElements = new Set();
  for (let key of Object.keys(sourceMap)) {
    if (
      key.startsWith("dist/bin/chrome/toolkit/content/global/elements/moz-") &&
      key.endsWith(".mjs")
    ) {
      mozElements.add(key.split("/").pop().replace(".mjs", ""));
    }
  }
  mozElements.forEach(elementName => {
    reusableComponentModules[`./${elementName}/${elementName}.mjs`]();
  });

  // Manually import the two components that don't follow our naming conventions.
  import("toolkit/content/widgets/panel-list/panel-list.mjs");
  import("toolkit/content/widgets/named-deck.js");
}
importReusableComponents();

/**
 * Wrapper component used to decorate all of our stories by providing access to
 * `in-content/common.css` without leaking styles that conflict Storybook's CSS.
 *
 * More information on decorators can be found at:
 * https://storybook.js.org/docs/web-components/writing-stories/decorators
 *
 * @property {Function} story
 *  Storybook uses this internally to render stories. We call `story` in our
 *  render function so that the story contents have the same shadow root as
 *  `with-common-styles` and styles from `in-content/common` get applied.
 * @property {object} context
 *  Another Storybook provided property containing additional data stories use
 *  to render. If we don't make this a reactive property Lit seems to optimize
 *  away any re-rendering of components inside `with-common-styles`.
 */
class WithCommonStyles extends MozLitElement {
  static styles = css`
    :host {
      display: block;
      height: 100%;
      padding: 1rem;
      box-sizing: border-box;
    }

    :host,
    :root {
      font: message-box;
      font-size: var(--font-size-root);
      appearance: none;
      background-color: var(--background-color-canvas);
      color: var(--text-color);
      -moz-box-layout: flex;
    }

    :host {
      font-size: var(--font-size-root);
    }

    :host([theme="light"]) {
      color-scheme: light;
    }

    :host([theme="dark"]) {
      color-scheme: dark;
    }
  `;

  static properties = {
    story: { type: Function },
    context: { type: Object },
  };

  connectedCallback() {
    super.connectedCallback();
    this.classList.add("anonymous-content-host");
  }

  storyContent() {
    if (this.story) {
      return this.story();
    }
    return html` <slot></slot> `;
  }

  render() {
    return html`
      <link
        rel="stylesheet"
        href="chrome://global/skin/in-content/common.css"
      />
      ${this.storyContent()}
    `;
  }
}
customElements.define("with-common-styles", WithCommonStyles);

// ---- theme helpers used by the decorator (hoisted once) ----
const mql = window.matchMedia("(prefers-color-scheme: dark)");
const resolveTheme = (selected /* 'light'|'dark'|'system' */) => {
  if (selected === "dark") {
    return "dark";
  }
  if (selected === "light") {
    return "light";
  }
  return mql.matches ? "dark" : "light"; // system
};

// Wrap all stories in `with-common-styles`.
export default {
  decorators: [
    (story, context) => {
      const selectedTheme = context?.globals?.theme ?? "system";
      const resolvedTheme = resolveTheme(selectedTheme);

      return html`
        <with-common-styles
          .story=${story}
          .context=${context}
          theme=${resolvedTheme}
        ></with-common-styles>
      `;
    },
    withActions,
  ],
  parameters: {
    docs: {
      toc: {
        disable: false,
        headingSelector: "h2, h3",
        ignoreSelector: "h2.text-truncated-ellipsis, .toc-ignore",
        title: "On this page",
      },
      container: DocsContainer, // updates /docs iframe
    },
    options: { showPanel: true },
  },
  globalTypes: {
    theme: {
      description: "Global theme",
      defaultValue: "system",
      toolbar: {
        title: "Theme toggle",
        items: [
          {
            value: "light",
            title: "Light",
            icon: "circlehollow",
          },
          {
            value: "dark",
            title: "Dark",
            icon: "circle",
          },
          {
            value: "system",
            title: "System",
            icon: "mirror",
          },
        ],
        dynamicTitle: true,
      },
    },
  },
};

// Enable props tables documentation.
setCustomElementsManifest(customElementsManifest);
