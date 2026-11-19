/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * @import { A2UIComponent } from "moz-src:///browser/components/aiwindow/models/aitab/AITab.sys.mjs"
 */

/**
 * Returns the href parsed as an http(s) URL, or null for anything else. Hrefs
 * in a generated page can also be in-app route ids, which must not become
 * links.
 *
 * @param {string} href
 * @returns {?URL}
 */
export function httpUrl(href) {
  const parsed = URL.parse(String(href ?? "").trim());
  return parsed?.protocol == "http:" || parsed?.protocol == "https:"
    ? parsed
    : null;
}

/**
 * The component name recorded for creation telemetry. A component that carries
 * a layout is `component_layout`; every other component is its catalog name.
 * List is the component that has a layout, added with bug 2079888, so the
 * typedef does not declare it yet.
 *
 * @param {A2UIComponent} [component]
 * @returns {string|undefined}
 */
export function componentTypeName(component = {}) {
  const { component: typeName, layout } = component;
  return typeof layout == "string" ? `${typeName}_${layout}` : typeName;
}
