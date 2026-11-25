/**
 * @licstart The following is the entire license notice for the
 * JavaScript code in this page
 *
 * Copyright 2024 Mozilla Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 * @licend The above is the entire license notice for the
 * JavaScript code in this page
 */

/**
 * pdfjsVersion = 7.0.234
 * pdfjsBuild = 3471ae169
 */
export const __webpack_esm_ids__ = ["print-appearances"];
export const __webpack_esm_modules__ = {

/***/ 7631
(__unused_webpack___webpack_module__, __webpack_exports__, __webpack_require__) {

/* harmony export */ __webpack_require__.d(__webpack_exports__, {
/* harmony export */   importPrintedAppearances: () => (/* binding */ importPrintedAppearances)
/* harmony export */ });
/* harmony import */ var _primitives_js__WEBPACK_IMPORTED_MODULE_0__ = __webpack_require__(5632);
/* harmony import */ var _shared_util_js__WEBPACK_IMPORTED_MODULE_1__ = __webpack_require__(4292);
/* harmony import */ var _base_stream_js__WEBPACK_IMPORTED_MODULE_2__ = __webpack_require__(1834);
/* harmony import */ var _core_utils_js__WEBPACK_IMPORTED_MODULE_3__ = __webpack_require__(5399);
/* harmony import */ var _evaluator_js__WEBPACK_IMPORTED_MODULE_4__ = __webpack_require__(9285);
/* harmony import */ var _parser_js__WEBPACK_IMPORTED_MODULE_5__ = __webpack_require__(1440);
/* harmony import */ var _pdf_manager_js__WEBPACK_IMPORTED_MODULE_6__ = __webpack_require__(7877);
/* harmony import */ var _stream_js__WEBPACK_IMPORTED_MODULE_7__ = __webpack_require__(4106);








const MARKED_CONTENT_OPS = new Set(["BDC", "BMC", "DP", "EMC", "MP"]);
async function copyObject(obj, sourceXref, target) {
  const {
    changes,
    refs,
    xref,
    xrefWrapper
  } = target;
  if (obj instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_0__.Ref) {
    let newRef = refs.get(obj);
    if (!newRef) {
      refs.put(obj, newRef = xref.getNewTemporaryRef());
      const value = await sourceXref.fetchAsync(obj);
      changes.put(newRef, {
        data: await copyObject(value, sourceXref, target)
      });
    }
    return newRef;
  }
  if (Array.isArray(obj)) {
    return Promise.all(obj.map(value => copyObject(value, sourceXref, target)));
  }
  let dict, stream;
  if (obj instanceof _base_stream_js__WEBPACK_IMPORTED_MODULE_2__.BaseStream) {
    ({
      dict
    } = stream = obj.getOriginalStream().clone());
  } else if (obj instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_0__.Dict) {
    dict = obj.clone();
  } else {
    return obj;
  }
  dict.xref = xrefWrapper;
  const promises = [];
  for (const [key, value] of dict.getRawEntries()) {
    promises.push(copyObject(value, sourceXref, target).then(newValue => dict.set(key, newValue)));
  }
  await Promise.all(promises);
  return stream ?? dict;
}
function filterContentStream(bytes, isWidget) {
  const lexer = new _parser_js__WEBPACK_IMPORTED_MODULE_5__.Lexer(new _stream_js__WEBPACK_IMPORTED_MODULE_7__.Stream(bytes), _evaluator_js__WEBPACK_IMPORTED_MODULE_4__.EvaluatorPreprocessor.opMap);
  const parts = isWidget ? [(0,_shared_util_js__WEBPACK_IMPORTED_MODULE_1__.stringToBytes)("/Tx BMC\n")] : [];
  let start = 0;
  let end = 0;
  try {
    while (true) {
      const obj = lexer.getObj();
      if (obj === _primitives_js__WEBPACK_IMPORTED_MODULE_0__.EOF) {
        break;
      }
      if (!(obj instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_0__.Cmd) || !_evaluator_js__WEBPACK_IMPORTED_MODULE_4__.EvaluatorPreprocessor.opMap[obj.cmd]) {
        continue;
      }
      if (obj.cmd === "BI") {
        (0,_shared_util_js__WEBPACK_IMPORTED_MODULE_1__.warn)("filterContentStream: inline images aren't supported.");
        return null;
      }
      const cmdEnd = lexer.currentChar < 0 ? bytes.length : lexer.stream.pos - 1;
      if (MARKED_CONTENT_OPS.has(obj.cmd)) {
        parts.push(bytes.subarray(start, end));
        start = cmdEnd;
      }
      end = cmdEnd;
    }
  } catch (reason) {
    ;(0,_shared_util_js__WEBPACK_IMPORTED_MODULE_1__.warn)(`filterContentStream: "${reason}".`);
    return null;
  }
  parts.push(bytes.subarray(start));
  if (isWidget) {
    parts.push((0,_shared_util_js__WEBPACK_IMPORTED_MODULE_1__.stringToBytes)("\nEMC"));
  }
  const data = new Uint8Array(parts.reduce((length, part) => length + part.length, 0));
  let offset = 0;
  for (const part of parts) {
    data.set(part, offset);
    offset += part.length;
  }
  return data;
}
async function importPrintedAppearances({
  buffer,
  changes,
  docId,
  entries,
  evaluatorOptions,
  handler,
  xref
}) {
  const pdfManager = new _pdf_manager_js__WEBPACK_IMPORTED_MODULE_6__.LocalPdfManager({
    source: buffer,
    docId: `${docId}_printToPDF`,
    handler,
    evaluatorOptions
  });
  await pdfManager.initDocument(false);
  const {
    pdfDocument
  } = pdfManager;
  if (pdfDocument.numPages !== entries.length) {
    throw new Error("The generated PDF must have one page per appearance.");
  }
  const appearances = new Map();
  const target = {
    changes,
    refs: new _primitives_js__WEBPACK_IMPORTED_MODULE_0__.RefMap(),
    xref,
    xrefWrapper: new _core_utils_js__WEBPACK_IMPORTED_MODULE_3__.ChangesXRefWrapper(changes, xref)
  };
  for (let i = 0, ii = entries.length; i < ii; i++) {
    const {
      key,
      isWidget,
      matrix,
      data
    } = entries[i];
    const page = await pdfDocument.getPage(i);
    const contentStream = await page.getContentStream();
    contentStream.reset();
    const bytes = filterContentStream(contentStream.getBytes(), isWidget);
    if (!bytes) {
      continue;
    }
    const dict = new _primitives_js__WEBPACK_IMPORTED_MODULE_0__.Dict(xref);
    dict.setIfName("Type", "XObject");
    dict.setIfName("Subtype", "Form");
    dict.set("FormType", 1);
    dict.set("BBox", [0, 0, data.width, data.height]);
    dict.setIfArray("Matrix", matrix);
    dict.set("Resources", await copyObject(page.resources, pdfDocument.xref, target));
    const ref = xref.getNewTemporaryRef();
    changes.put(ref, {
      data: new _stream_js__WEBPACK_IMPORTED_MODULE_7__.Stream(bytes, 0, bytes.length, dict)
    });
    appearances.set(key, ref);
  }
  return appearances;
}


/***/ }

};
