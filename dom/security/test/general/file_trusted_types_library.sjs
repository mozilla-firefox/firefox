"use strict";

// Each case reproduces only the symptom a library's detection relies on.
const CASES = {
  none: "",
  lit: `trustedTypes.createPolicy("lit-html", { createHTML: s => s });`,
  react: `typeof __REACT_DEVTOOLS_GLOBAL_HOOK__;`,
  react_disabled_hook: `window.__REACT_DEVTOOLS_GLOBAL_HOOK__ = { isDisabled: true };`,
  devtools_hook_without_react: `
    Object.defineProperty(window, "__REACT_DEVTOOLS_GLOBAL_HOOK__", {
      get: () => ({ renderers: new Map(), supportsFiber: true }),
    });`,
  htmx: `var htmx = {};`,
  angular: `document.body.innerHTML = '<app-root ng-version="22.2.2"></app-root>';`,
};

function handleRequest(request, response) {
  const params = new URLSearchParams(request.queryString);
  response.setHeader("Content-Type", "text/html", false);
  response.setHeader("Cache-Control", "no-cache", false);
  response.write(
    `<!DOCTYPE html><html><body><script>${CASES[params.get("case")]}</script></body></html>`
  );
}
