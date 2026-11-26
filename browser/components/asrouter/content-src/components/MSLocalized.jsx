/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import React, { useEffect } from "react";
export const CONFIGURABLE_STYLES = [
  "background",
  "color",
  "display",
  "fontSize",
  "fontWeight",
  "letterSpacing",
  "lineHeight",
  "marginBlock",
  "marginBlockStart",
  "marginBlockEnd",
  "marginInline",
  "paddingBlock",
  "paddingBlockStart",
  "paddingBlockEnd",
  "paddingInline",
  "paddingInlineStart",
  "paddingInlineEnd",
  "textAlign",
  "whiteSpace",
  "width",
  "height",
  "border",
  "borderBlockStart",
  "borderBlockEnd",
  "top",
  "bottom",
  "left",
  "right",
  "inset",
  "insetBlock",
  "insetInline",
  "minHeight",
  "minWidth",
  "maxWidth",
];
const ZAP_SIZE_THRESHOLD = 160;

/**
 * Picks the CONFIGURABLE_STYLES entries present on `source` into a style
 * object.
 */
export function pickConfigurableStyles(source) {
  const style = {};
  for (const styleProp of CONFIGURABLE_STYLES) {
    if (source[styleProp] !== undefined) {
      style[styleProp] = source[styleProp];
    }
  }
  return style;
}

/**
 * Resolves which icon URL to use.
 */
export function resolveImageSrc({ imageURL, rtlImageURL }) {
  const isRTL =
    typeof document !== "undefined" &&
    document.documentElement.matches(":dir(rtl)");
  return isRTL && rtlImageURL ? rtlImageURL : imageURL;
}

/**
 * Based on the .text prop, localizes an inner element if a string_id
 * is provided, OR renders plain text, OR hides it if nothing is provided.
 * Allows configuring of some styles including zap underline and color.
 *
 * Examples:
 *
 * Localized text
 * ftl:
 *  title = Welcome
 * jsx:
 *   <Localized text={{string_id: "title"}}><h1 /></Localized>
 * output:
 *   <h1 data-l10n-id="title">Welcome</h1>
 *
 * Unlocalized text
 * jsx:
 *   <Localized text="Welcome"><h1 /></Localized>
 *   <Localized text={{raw: "Welcome"}}><h1 /></Localized>
 * output:
 *   <h1>Welcome</h1>
 *
 * Localized text with inline icons
 * ftl:
 *  subtitle = Use <img data-l10n-name="my-icon" alt="My icon"/> for every account
 * jsx:
 *   <Localized text={{
 *     string_id: "subtitle",
 *     inline_icons: {
 *       "my-icon": { imageURL: "chrome://...", rtlImageURL: "chrome://..." }
 *     }
 *   }}><p /></Localized>
 * output:
 *   <p data-l10n-id="subtitle">
 *     Use <img data-l10n-name="my-icon" class="inline-icon" src="chrome://..." alt="My icon"/> for every account
 *   </p>
 */

export const Localized = ({ text, children }) => {
  // Dynamically determine the size of the zap style.
  const zapRef = React.createRef();
  useEffect(() => {
    const { current } = zapRef;
    if (current) {
      requestAnimationFrame(() =>
        current?.classList.replace(
          "short",
          current.getBoundingClientRect().width > ZAP_SIZE_THRESHOLD
            ? "long"
            : "short"
        )
      );
    }
  });

  // Skip rendering of children with no text.
  if (!text) {
    return null;
  }

  // Allow augmenting existing child container properties.
  const props = { children: [], className: "", style: {}, ...children?.props };
  // Support nested Localized by starting with their children.
  const textNodes = Array.isArray(props.children)
    ? props.children
    : [props.children];

  // Pick desired fluent or raw/plain text to render.
  if (text.string_id) {
    // Set the key so React knows not to reuse when switching to plain text.
    props.key = text.string_id;
    props["data-l10n-id"] = text.string_id;
    if (text.args) {
      props["data-l10n-args"] = JSON.stringify(text.args);
    }
  } else if (text.raw) {
    textNodes.push(text.raw);
  } else if (typeof text === "string") {
    textNodes.push(text);
  }

  // Add zap style and content in a way that allows fluent to insert too.
  if (text.zap) {
    props.className += " welcomeZap";
    textNodes.push(
      <span className="short zap" data-l10n-name="zap" ref={zapRef}>
        {text.zap}
      </span>
    );
  }

  // Slot inline icons into the localized string. Each entry maps a Fluent
  // data-l10n-name to an icon object, and Fluent inserts the matching <img>
  // wherever <img data-l10n-name="..."/> appears in the string.
  if (text.string_id && text.inline_icons) {
    for (const [l10nName, icon] of Object.entries(text.inline_icons)) {
      textNodes.push(
        <img
          key={l10nName}
          data-l10n-name={l10nName}
          className="inline-icon"
          src={resolveImageSrc(icon)}
        />
      );
    }
  }

  if (text.aria_label) {
    props["aria-label"] = text.aria_label;
  }

  // Apply certain configurable styles.
  Object.assign(props.style, pickConfigurableStyles(text));

  return React.cloneElement(
    // Provide a default container for the text if necessary.
    children ?? <span />,
    props,
    // Conditionally pass in as void elements can't accept empty array.
    textNodes.length ? textNodes : null
  );
};
