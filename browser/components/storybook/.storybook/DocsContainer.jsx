/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

// eslint-disable-next-line no-unused-vars
import React, { useEffect, useLayoutEffect, useState } from "react";
// eslint-disable-next-line no-unused-vars
import { DocsContainer as BaseContainer } from "@storybook/addon-docs/blocks";
import { addons } from "storybook/preview-api";
import {
  GLOBALS_UPDATED,
  UPDATE_GLOBALS,
  SET_GLOBALS,
} from "storybook/internal/core-events";
import darkTheme from "./theme-dark.mjs";
import lightTheme from "./theme-light.mjs";

/* -------- helpers -------- */
function getSelectedFromUrl() {
  const g = new URLSearchParams(location.search).get("globals");
  return g?.match(/(?:^|;)theme:([^;]+)/)?.[1] ?? "system"; // 'light' | 'dark' | 'system'
}
function getOsDark() {
  return window.matchMedia("(prefers-color-scheme: dark)").matches;
}
function mergeGlobalsParam(existing, key, value) {
  const parts = (existing || "").split(";").filter(Boolean);
  const map = new Map(parts.map(s => s.split(":")));
  map.set(key, value);
  return [...map.entries()].map(([k, v]) => `${k}:${v}`).join(";");
}
function syncGlobalsUrl(key, value) {
  // update this iframe URL
  try {
    const u = new URL(window.location.href);
    const prev = u.searchParams.get("globals") || "";
    const merged = mergeGlobalsParam(prev, key, value);
    if (merged !== prev) {
      u.searchParams.set("globals", merged);
      window.history.replaceState({}, "", u.toString());
    }
  } catch {}
  // update top/manager URL (same-origin)
  try {
    const topWin = window.parent || window.top;
    const u2 = new URL(topWin.location.href);
    const prev2 = u2.searchParams.get("globals") || "";
    const merged2 = mergeGlobalsParam(prev2, key, value);
    if (merged2 !== prev2) {
      u2.searchParams.set("globals", merged2);
      topWin.history.replaceState({}, "", u2.toString());
    }
  } catch {}
}
function applyDocsDomTheme(dark) {
  const mode = dark ? "dark" : "light";
  const html = document.documentElement;
  html.style.colorScheme = mode; // UA widgets
  html.classList.toggle("dark", dark); // your CSS hook
  const root = document.getElementById("storybook-docs");
  if (root) {
    root.classList.toggle("dark", dark);
  }
}

/* -------- container -------- */
export default function DocsContainer(props) {
  // initial selection from URL; "system" follows OS via mql below
  const [selected, setSelected] = useState(getSelectedFromUrl()); // 'light' | 'dark' | 'system'
  const [osDark, setOsDark] = useState(getOsDark());

  // concrete resolution
  const isDark = selected === "dark" || (selected !== "light" && osDark);

  // apply before paint on initial load + any theme change
  useLayoutEffect(() => {
    applyDocsDomTheme(isDark);
  }, [isDark]);

  // follow OS flips when toolbar = "system"
  useEffect(() => {
    const mql = window.matchMedia("(prefers-color-scheme: dark)");
    const onChange = e => setOsDark(e.matches);
    mql.addEventListener?.("change", onChange) || mql.addListener?.(onChange);
    return () => {
      mql.removeEventListener?.("change", onChange) ||
        mql.removeListener?.(onChange);
    };
  }, []);

  // toolbar changes → sync URL, flip DOM immediately, update state
  useEffect(() => {
    const channel = addons.getChannel();
    const onUpdate = payload => {
      const next = payload?.globals?.theme; // 'light' | 'dark' | 'system'
      if (!next) {
        return;
      }
      syncGlobalsUrl("theme", next); // make refresh consistent
      applyDocsDomTheme(next === "dark" || (next === "system" && getOsDark()));
      setSelected(prev => (prev === next ? prev : next));
    };
    channel.on(SET_GLOBALS, onUpdate);
    channel.on(GLOBALS_UPDATED, onUpdate);
    channel.on(UPDATE_GLOBALS, onUpdate);
    return () => {
      channel.off(SET_GLOBALS, onUpdate);
      channel.off(GLOBALS_UPDATED, onUpdate);
      channel.off(UPDATE_GLOBALS, onUpdate);
    };
  }, []);

  // Storybook theme object (NOT strings)
  const themeObj = isDark ? darkTheme : lightTheme;
  const themeKey = isDark ? "dark" : "light"; // remounts docs subtree when theme flips

  return <BaseContainer key={themeKey} {...props} theme={themeObj} />;
}
