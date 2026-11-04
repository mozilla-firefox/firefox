"use strict";

const CSP_HEADERS = {
  enforce: "Content-Security-Policy",
  report: "Content-Security-Policy-Report-Only",
};

const STRICT_CSP = "script-src 'nonce-n' 'strict-dynamic'; base-uri 'none'";
const STRICT_CSPS = {
  enforce: ["Content-Security-Policy", STRICT_CSP],
  report: ["Content-Security-Policy-Report-Only", STRICT_CSP],
  nobase: ["Content-Security-Policy", "script-src 'nonce-n' 'strict-dynamic'"],
  self: [
    "Content-Security-Policy",
    "script-src 'nonce-n' 'strict-dynamic'; base-uri 'self'",
  ],
};

// "plain" pages have no script at all, "empty" ones an empty script element,
// which does not run either.
const SCRIPTS = {
  plain: null,
  empty: "",
  innerhtml: `document.body.innerHTML = "<b>innerHTML</b>";`,
  write_default: `
    trustedTypes.createPolicy("default", { createHTML: s => s });
    document.write("<b>write</b>");`,
  policy_only: `trustedTypes.createPolicy("p", {});`,
  create_html: `
    trustedTypes.createPolicy("p", { createHTML: s => s }).createHTML("x");`,
  trusted_html: `
    const p = trustedTypes.createPolicy("p", { createHTML: s => s });
    document.body.innerHTML = p.createHTML("<b>trusted</b>");`,
  write_noargs: `document.write();`,
  sethtmlunsafe: `document.body.setHTMLUnsafe("<b>unsafe</b>");`,
  shadow_sethtmlunsafe: `
    document.body.attachShadow({ mode: "open" }).setHTMLUnsafe("<b>s</b>");`,
  parsehtmlunsafe: `Document.parseHTMLUnsafe("<b>parsed</b>");`,
  contextualfragment: `
    document.createRange().createContextualFragment("<b>fragment</b>");`,
  domparser: `new DOMParser().parseFromString("<b>dom</b>", "text/html");`,
  srcdoc_property: `document.createElement("iframe").srcdoc = "<b>x</b>";`,
  srcdoc_attribute: `
    document.createElement("iframe").setAttribute("srcdoc", "<b>x</b>");`,
  data_document: `
    document.implementation.createHTMLDocument().body.innerHTML = "<b>d</b>";`,
  mixed_write: `
    const p = trustedTypes.createPolicy("p", { createHTML: s => s });
    document.body.innerHTML = p.createHTML("<b>trusted</b>");
    document.write(p.createHTML("<b>a</b>"), "<b>b</b>");`,
};

function handleRequest(request, response) {
  const params = new URLSearchParams(request.queryString);
  response.setHeader("Content-Type", "text/html", false);
  response.setHeader("Cache-Control", "no-cache", false);
  const header = CSP_HEADERS[params.get("csp")];
  if (header) {
    response.setHeader(header, "require-trusted-types-for 'script'", true);
  }
  const strict = STRICT_CSPS[params.get("strict")];
  if (strict) {
    response.setHeader(...strict, true);
  }
  const script = SCRIPTS[params.get("script")];
  const handler = params.has("handler") ? ` onload="0"` : "";
  const type = params.has("module") ? ` type="module"` : "";
  response.write(
    `<!DOCTYPE html><html><body${handler}>${
      script === null ? "" : `<script nonce="n"${type}>${script}</script>`
    }</body></html>`
  );
}
