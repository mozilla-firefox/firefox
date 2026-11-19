/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Markdown parser for Smart Window chat messages.
 */

import { MarkdownIt } from "chrome://browser/content/multilineeditor/prosemirror.bundle.mjs";

export const CHAT_WRAPPER_ELEMENTS = {
  table: {
    element: "ai-chat-table",
    attributes: ["message-id", "data-line-range"],
  },
};

// Create with default preset for markdown parsing similar to GFM. Among other
// things this enables us to render tables.
const md = MarkdownIt("default", { html: false });

// Add element wrapper rules.
for (const [element, { element: wrapper }] of Object.entries(
  CHAT_WRAPPER_ELEMENTS
)) {
  md.renderer.rules[`${element}_open`] = (
    tokens,
    index,
    options,
    _env,
    renderer
  ) => {
    const dataAttributes = new Map();
    if (element === "table") {
      const { map } = tokens[index];
      if (map) {
        dataAttributes.set("data-line-range", JSON.stringify(map));
      }
    }
    const attributesString = [...dataAttributes]
      .map(([key, value]) => `${key}="${md.utils.escapeHtml(value)}"`)
      .join(" ");
    return `<${wrapper}${attributesString ? ` ${attributesString}` : ""}>${renderer.renderToken(tokens, index, options)}`;
  };
  md.renderer.rules[`${element}_close`] = (
    tokens,
    index,
    options,
    _env,
    renderer
  ) => `${renderer.renderToken(tokens, index, options)}</${wrapper}>`;
}

// A link the model invented has nowhere to point once its hallucinated URL
// token is stripped from the message, leaving `[label]()`. Markdown reads that
// as a link with an empty destination and renders an anchor that goes nowhere,
// so drop the anchor and keep the label as plain text.
function hasDestination(token) {
  return Boolean(token?.attrGet("href")?.trim());
}

md.renderer.rules.link_open = (tokens, index, options, _env, renderer) =>
  hasDestination(tokens[index])
    ? renderer.renderToken(tokens, index, options)
    : "";

md.renderer.rules.link_close = (tokens, index, options, _env, renderer) => {
  // Markdown links can't nest, so the nearest preceding `link_open` is the
  // one this token closes.
  let lastOpenLinkToken = null;
  for (let i = index - 1; i >= 0; i--) {
    if (tokens[i].type === "link_open") {
      lastOpenLinkToken = tokens[i];
      break;
    }
  }
  return lastOpenLinkToken && !hasDestination(lastOpenLinkToken)
    ? ""
    : renderer.renderToken(tokens, index, options);
};

/**
 * Parse markdown to HTML.
 *
 * @param {string} markdown - The markdown string to parse
 * @returns {string} HTML string
 */
export function parseMarkdown(markdown) {
  return md.render(markdown);
}

/**
 * Parse markdown into an ordered list of top-level blocks, each
 * rendered to its own HTML string. Joining every block's html is identical
 * to parseMarkdown(markdown); splitting this way lets a streamed reply re-render
 * only the blocks that changed.
 *
 * @param {string} markdown - The markdown string to parse
 * @returns {Array<{html: string}>} One entry per top-level block, in order
 */
export function parseMarkdownBlocks(markdown) {
  const env = {};
  const tokens = md.parse(markdown, env);
  const blocks = [];

  let start = 0;
  while (start < tokens.length) {
    let end = start + 1;
    if (tokens[start].nesting === 1) {
      // Walk to the matching close: the nesting counts sum back to zero there.
      let depth = 1;
      while (end < tokens.length && depth > 0) {
        depth += tokens[end].nesting;
        end++;
      }
    }
    blocks.push({
      html: md.renderer.render(tokens.slice(start, end), md.options, env),
    });
    start = end;
  }

  return blocks;
}
