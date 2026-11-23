/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const wasmparser = require("resource://devtools/client/shared/vendor/WasmParser.js");
const wasmdis = require("resource://devtools/client/shared/vendor/WasmDis.js");

const wasmStates = new WeakMap();

function getWasmText(subject, data) {
  if (wasmStates.has(subject)) {
    const wasmState = wasmStates.get(subject);
    return { lines: wasmState.result.lines, done: wasmState.result.done };
  }
  const parser = new wasmparser.BinaryReader();
  parser.setData(data.buffer, 0, data.length);
  const dis = new wasmdis.WasmDisassembler();
  dis.addOffsets = true;
  const done = dis.disassembleChunk(parser);
  let result = dis.getResult();
  if (result.lines.length === 0) {
    result = { lines: ["No luck with wast conversion"], offsets: [0], done };
  }
  // Cache mappings of WASM offsets to lines
  const offsets = result.offsets,
    lines = [];
  for (let line = 0; line < offsets.length; line++) {
    const offset = offsets[line];
    lines[offset] = line;
  }
  wasmStates.set(subject, { offsets, lines, result });

  return { lines: result.lines, done: result.done };
}

/**
 * Creates wasm formatter function used to generate the hexadecimal number
 * displayed in the line gutter
 *
 * @param {object} subject - An object which decribes the source, it is comprised of the sourceId
 * @returns {Function}
 */
function getWasmLineNumberFormatter(subject) {
  const codeOf0 = 48,
    codeOfA = 65;
  const buffer = [
    codeOf0,
    codeOf0,
    codeOf0,
    codeOf0,
    codeOf0,
    codeOf0,
    codeOf0,
    codeOf0,
  ];
  let last0 = 7;
  return function (line) {
    const offset = lineToWasmOffset(subject, line - 1);
    if (offset === undefined) {
      return "";
    }
    let i = 7;
    for (let n = offset | 0; n !== 0 && i >= 0; n >>= 4, i--) {
      const nibble = n & 15;
      buffer[i] = nibble < 10 ? codeOf0 + nibble : codeOfA - 10 + nibble;
    }
    for (let j = i; j > last0; j--) {
      buffer[j] = codeOf0;
    }
    last0 = i;
    return String.fromCharCode.apply(null, buffer);
  };
}

/**
 * Checks if the specified source exists in the cache.
 * This is used to determine if the source is a WASM source
 *
 * @param {object} subject
 * @returns {boolean}
 */
function isWasm(subject) {
  return wasmStates.has(subject);
}

/**
 * Converts the source (decimal) line to its WASM offset
 *
 * @param {object} subject
 * @param {number} line
 * @param {boolean} findNextOffset
 *        There are scenarios (e.g are empty lines) where we might want to find the next best offset match.
 *        Every line will usually have offsets assigned except empty lines (which could be between functions
 *        or some declarations).
 * @returns {number}
 */
function lineToWasmOffset(subject, line, findNextOffset = false) {
  const wasmState = wasmStates.get(subject);
  if (!wasmState) {
    return undefined;
  }

  let offset = wasmState.offsets[line];
  if (findNextOffset) {
    while (offset === undefined && line > 0) {
      offset = wasmState.offsets[--line];
    }
  }
  return offset;
}

/**
 * Converts the WASM offset to the source line
 *
 * @param {object} subject
 * @param {number} offset
 * @returns {number}
 */
function wasmOffsetToLine(subject, offset) {
  const wasmState = wasmStates.get(subject);
  if (!wasmState) {
    return undefined;
  }
  return wasmState.lines[offset];
}

// A cache of the wasm source text as an array of lines.
// The lines are cached with the value object of the source content
// as the key.
const wasmLines = new WeakMap();

/**
 * Returns the wasm text lines for the given source content. The content may
 * belong to a source which is not currently displayed in the editor, so the
 * content object itself is used as the cache key.
 *
 * @param {object} content
 * @returns {Array<string>}
 */
function renderWasmText(content) {
  if (wasmLines.has(content)) {
    return wasmLines.get(content) || [];
  }

  // binary does not survive as Uint8Array, converting from string
  const { binary } = content.value;
  const data = new Uint8Array(binary.length);
  for (let i = 0; i < data.length; i++) {
    data[i] = binary.charCodeAt(i);
  }
  const { lines } = getWasmText(content, data);
  const MAX_LINES = 1000000;
  if (lines.length > MAX_LINES) {
    lines.splice(MAX_LINES, lines.length - MAX_LINES);
    lines.push(";; .... text is truncated due to the size");
  }

  wasmLines.set(content, lines);
  return lines;
}

module.exports = {
  getWasmText,
  getWasmLineNumberFormatter,
  isWasm,
  lineToWasmOffset,
  wasmOffsetToLine,
  renderWasmText,
};
