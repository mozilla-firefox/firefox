/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at <http://mozilla.org/MPL/2.0/>. */

export * from "./source-documents";
export * from "./source-search";
export * from "../ui";
export * from "./tokens";

import { createEditor } from "./create-editor";
import { isWasm, lineToWasmOffset, wasmOffsetToLine } from "../wasm";
import { createLocation } from "../location";
import { features } from "../prefs";

let editor;

export function getEditor() {
  if (editor) {
    return editor;
  }

  editor = createEditor({ cm6: features.codemirrorNext });
  return editor;
}

export function removeEditor() {
  editor = null;
}

/**
 *  Update line wrapping for the codemirror editor.
 */
export function updateEditorLineWrapping(value) {
  if (!editor) {
    return;
  }
  editor.setLineWrapping(value);
}

function getCodeMirror() {
  return editor && editor.hasCodeMirror ? editor.codeMirror : null;
}

export function startOperation() {
  const codeMirror = getCodeMirror();
  if (!codeMirror) {
    return;
  }

  codeMirror.startOperation();
}

export function endOperation() {
  const codeMirror = getCodeMirror();
  if (!codeMirror) {
    return;
  }

  codeMirror.endOperation();
}

export function toWasmSourceLine(sourceId, offset) {
  if (features.codemirrorNext) {
    return editor.wasmOffsetToLine(offset) || 0;
  }
  return wasmOffsetToLine(sourceId, offset) || 0;
}

/**
 * Convert source lines / WASM line offsets to Codemirror lines
 * @param {Object} source
 * @param {Number} lineOrOffset
 * @returns
 */
export function toEditorLine(source, lineOrOffset) {
  if (features.codemirrorNext) {
    if (editor.isWasm && !source.isOriginal) {
      // TODO ensure offset is always "mappable" to edit line.
      return toWasmSourceLine(source.id, lineOrOffset) + 1;
    }
    return lineOrOffset;
  }

  // CM5
  if (isWasm(source.id)) {
    return toWasmSourceLine(source.id, lineOrOffset);
  }
  return lineOrOffset ? lineOrOffset - 1 : 1;
}

export function fromEditorLine(source, line) {
  if (features.codemirrorNext) {
    // Also ignore the original source related to the .wasm file.
    if (editor.isWasm && !source.isOriginal) {
      // Content lines is 1-based in CM6 and 0-based in WASM
      return editor.lineToWasmOffset(line - 1);
    }
    return line;
  }
  // CM5
  if (isWasm(source.id)) {
    return lineToWasmOffset(source.id, line);
  }
  return line + 1;
}

export function toEditorPosition(location) {
  // Note that Spidermonkey, Debugger frontend and CodeMirror are all consistent regarding column
  // and are 0-based. But only CodeMirror consider the line to be 0-based while the two others
  // consider lines to be 1-based.
  const isSourceWasm = features.codemirrorNext
    ? editor.isWasm && !location.source.isOriginal
    : isWasm(location.source.id);
  return {
    line: toEditorLine(location.source, location.line),
    column: isSourceWasm || !location.column ? 0 : location.column,
  };
}

export function toSourceLine(source, line) {
  if (features.codemirrorNext) {
    if (editor.isWasm && !source.isOriginal) {
      return editor.lineToWasmOffset(line - 1);
    }
    return line;
  }
  // CM5
  // CodeMirror 5 cursor location is 0 based and Codemirror 6 position is 1 based.
  // Whereas in DevTools frontend and backend only column is 0-based, the line is 1 based.
  if (isWasm(source.id)) {
    return lineToWasmOffset(source.id, line);
  }
  return line + 1;
}

export function markText({ codeMirror }, className, { start, end }) {
  return codeMirror.markText(
    { ch: start.column, line: start.line },
    { ch: end.column, line: end.line },
    { className }
  );
}

export function lineAtHeight({ codeMirror }, source, event) {
  const _editorLine = codeMirror.lineAtHeight(event.clientY);
  return toSourceLine(source, _editorLine);
}

export function getSourceLocationFromMouseEvent({ codeMirror }, source, e) {
  const { line, ch } = codeMirror.coordsChar({
    left: e.clientX,
    top: e.clientY,
  });
  const isSourceWasm = features.codemirrorNext
    ? editor.isWasm
    : isWasm(source.id);
  return createLocation({
    source,
    line: fromEditorLine(source, line),
    column: isSourceWasm ? 0 : ch + 1,
  });
}

export function forEachLine(codeMirror, iter) {
  codeMirror.operation(() => {
    codeMirror.doc.iter(0, codeMirror.lineCount(), iter);
  });
}

export function removeLineClass(codeMirror, line, className) {
  codeMirror.removeLineClass(line, "wrap", className);
}

export function clearLineClass(codeMirror, className) {
  forEachLine(codeMirror, line => {
    removeLineClass(codeMirror, line, className);
  });
}

export function getTextForLine(codeMirror, line) {
  return codeMirror.getLine(line - 1).trim();
}

export function getCursorLine(codeMirror) {
  return codeMirror.getCursor().line;
}

export function getCursorColumn(codeMirror) {
  return codeMirror.getCursor().ch;
}
