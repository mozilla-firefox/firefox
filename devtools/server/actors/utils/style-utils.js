/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const XHTML_NS = "http://www.w3.org/1999/xhtml";
const FONT_PREVIEW_TEXT = "Abc";
const FONT_PREVIEW_FONT_SIZE = 40;
const FONT_PREVIEW_FILLSTYLE = "black";
const FONT_PREVIEW_FONT_FALLBACK = "serif";
// Offset (in px) to avoid cutting off text edges of italic fonts.
const FONT_PREVIEW_OFFSET = 4;
// Factor used to resize the canvas in order to get better text quality.
const FONT_PREVIEW_OVERSAMPLING_FACTOR = 2;
const FONT_NEED_WRAPPING_QUOTES_REGEX = /^[^'"].* /;

/**
 * Helper function for getting an image preview of the given font.
 *
 * @param font {string}
 *        Name of font to preview
 * @param doc {Document}
 *        Document to use to render font
 * @param options {object}
 *        Object with options 'previewText' and 'previewFontSize'
 *
 * @return {Object} An object with the following properties:
 *         - dataUrl {string}: The data URI of the font preview image
 *         - size {Number}: The optimal width of preview image
 *         - ctx {CanvasRenderingContext2D}: The canvas context (returned for tests)
 */
function getFontPreviewData(font, doc, options) {
  options = options || {};
  const previewText = options.previewText || FONT_PREVIEW_TEXT;
  const previewTextLines = previewText.split("\n");
  const previewFontSize = options.previewFontSize || FONT_PREVIEW_FONT_SIZE;
  const fillStyle = options.fillStyle || FONT_PREVIEW_FILLSTYLE;
  const fontStyle = options.fontStyle || "";
  const fontWeight = options.fontWeight || "";

  const canvas = doc.createElementNS(XHTML_NS, "canvas");
  const ctx = canvas.getContext("2d");

  // We want to wrap some font in quotes so font family like `Font Awesome 5 Brands` are
  // properly applied, but we don't want to wrap all fonts, otherwise generic family names
  // (e.g. `monospace`) wouldn't work.
  // It should be safe to only add the quotes when the font has some spaces (generic family
  // names don't have spaces, https://developer.mozilla.org/en-US/docs/Web/CSS/font-family#generic-name)
  // We also don't want to add quotes if there are already some
  // `font` is the declaration value, so it can have multiple parts,
  // e.g: `"Menlo", MonoLisa, monospace`
  const fontParts = [];
  // We could use the parser to properly handle complex values, for example css variable,
  // but ideally this function would only receive computed values (see Bug 1952821).
  // If we'd get `var(--x)` here, we'd have to resolve it somehow, so it'd be simpler to
  // get the computed value directly.
  for (let f of font.split(",")) {
    if (FONT_NEED_WRAPPING_QUOTES_REGEX.test(f.trim())) {
      f = `"${f}"`;
    }
    fontParts.push(f);
  }
  // Add a fallback value
  fontParts.push(FONT_PREVIEW_FONT_FALLBACK);

  // Apply individual font properties to the canvas element so we can easily compute the
  // canvas context font
  // First, we need to start with a default shorthand to make it work
  canvas.style.font = `${FONT_PREVIEW_FONT_SIZE}px ${FONT_PREVIEW_FONT_FALLBACK}`;
  // Then we can set the different properties
  canvas.style.fontFamily = fontParts.join(", ");
  canvas.style.fontSize = `${previewFontSize}px`;
  if (fontWeight) {
    canvas.style.fontWeight = fontWeight;
  }
  if (fontStyle) {
    canvas.style.fontStyle = fontStyle;
  }

  const fontValue = canvas.style.font;

  // Get the correct preview text measurements and set the canvas dimensions
  ctx.font = fontValue;
  ctx.fillStyle = fillStyle;
  const previewTextLinesWidths = previewTextLines.map(
    previewTextLine => ctx.measureText(previewTextLine).width
  );
  const textWidth = Math.round(Math.max(...previewTextLinesWidths));

  // The canvas width is calculated as the width of the longest line plus
  // an offset at the left and right of it.
  // The canvas height is calculated as the font size multiplied by the
  // number of lines plus an offset at the top and bottom.
  //
  // In order to get better text quality, we oversample the canvas.
  // That means, after the width and height are calculated, we increase
  // both sizes by some factor.
  const simpleCanvasWidth = textWidth + FONT_PREVIEW_OFFSET * 2;
  canvas.width = simpleCanvasWidth * FONT_PREVIEW_OVERSAMPLING_FACTOR;
  canvas.height =
    (previewFontSize * previewTextLines.length + FONT_PREVIEW_OFFSET * 2) *
    FONT_PREVIEW_OVERSAMPLING_FACTOR;

  // we have to reset these after changing the canvas size
  ctx.font = fontValue;
  ctx.fillStyle = fillStyle;

  // Oversample the canvas for better text quality
  ctx.scale(FONT_PREVIEW_OVERSAMPLING_FACTOR, FONT_PREVIEW_OVERSAMPLING_FACTOR);

  ctx.textBaseline = "top";
  ctx.textAlign = "center";
  const horizontalTextPosition = simpleCanvasWidth / 2;
  let verticalTextPosition = FONT_PREVIEW_OFFSET;
  for (let i = 0; i < previewTextLines.length; i++) {
    ctx.fillText(
      previewTextLines[i],
      horizontalTextPosition,
      verticalTextPosition
    );

    // Move vertical text position one line down
    verticalTextPosition += previewFontSize;
  }

  const dataURL = canvas.toDataURL("image/png");

  return {
    dataURL,
    size: textWidth + FONT_PREVIEW_OFFSET * 2,
    ctx,
  };
}

exports.getFontPreviewData = getFontPreviewData;

/**
 * Get the text content of a rule given some CSS text, a line and a column
 * Consider the following example:
 * body {
 *  color: red;
 * }
 * p {
 *  line-height: 2em;
 *  color: blue;
 * }
 * Calling the function with the whole text above and line=4 and column=1 would
 * return "line-height: 2em; color: blue;"
 * @param {String} initialText
 * @param {Number} line (1-indexed)
 * @param {Number} column (1-indexed)
 * @return {object} An object of the form {offset: number, text: string}
 *                  The offset is the index into the input string where
 *                  the rule text started.  The text is the content of
 *                  the rule.
 */
function getRuleText(initialText, line, column) {
  if (typeof line === "undefined" || typeof column === "undefined") {
    throw new Error("Location information is missing");
  }

  const { text } = getTextAtLineColumn(initialText, line, column);
  const res = InspectorUtils.getRuleBodyText(text);
  if (res === null || typeof res === "undefined") {
    throw new Error("Couldn't find rule");
  }
  return res;
}

exports.getRuleText = getRuleText;

/**
 * Return the offset and substring of |text| that starts at the given
 * line and column.
 * @param {String} text
 * @param {Number} line (1-indexed)
 * @param {Number} column (1-indexed)
 * @return {object} An object of the form {offset: number, text: string},
 *                  where the offset is the offset into the input string
 *                  where the text starts, and where text is the text.
 */
function getTextAtLineColumn(text, line, column) {
  let offset;
  if (line > 1) {
    const rx = new RegExp(
      "(?:[^\\r\\n\\f]*(?:\\r\\n|\\n|\\r|\\f)){" + (line - 1) + "}"
    );
    offset = rx.exec(text)[0].length;
  } else {
    offset = 0;
  }
  offset += column - 1;
  return { offset, text: text.substr(offset) };
}

exports.getTextAtLineColumn = getTextAtLineColumn;
