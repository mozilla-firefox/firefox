/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import { addons } from "storybook/manager-api";
import {
  GLOBALS_UPDATED,
  UPDATE_GLOBALS,
  SET_GLOBALS,
} from "storybook/internal/core-events";
import darkTheme from "./theme-dark.mjs";
import lightTheme from "./theme-light.mjs";

// Change favicon
const link = document.createElement("link");
link.setAttribute("rel", "shortcut icon");
link.setAttribute("href", "chrome://branding/content/document.ico");
document.head.appendChild(link);

const mql = window.matchMedia("(prefers-color-scheme: dark)");
const getToolbarTheme = () => {
  // read ‘theme’ from globals in URL if present (initial load)
  const g = new URLSearchParams(location.search).get("globals");
  return g?.match(/(?:^|;)theme:([^;]+)/)?.[1] ?? null; // 'light' | 'dark' | 'system' | null
};

let selected = getToolbarTheme() || "system";
const pickTheme = () => {
  if (selected === "dark") {
    return darkTheme;
  }
  if (selected === "light") {
    return lightTheme;
  }
  return mql.matches ? darkTheme : lightTheme;
};

const apply = () => addons.setConfig({ theme: pickTheme() });
apply();

// update when OS changes (only affects 'system')
const onOsChange = () => {
  if (selected === "system") {
    apply();
  }
};
mql.addEventListener?.("change", onOsChange) || mql.addListener?.(onOsChange);

// update when toolbar changes (cover all events some builds emit)
const onGlobals = ({ globals }) => {
  if (!globals?.theme) {
    return;
  }
  selected = globals.theme; // 'light' | 'dark' | 'system'
  apply();
};
addons.ready().then(channel => {
  channel.on(SET_GLOBALS, onGlobals);
  channel.on(UPDATE_GLOBALS, onGlobals);
  channel.on(GLOBALS_UPDATED, onGlobals);
});
