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
 * pdfjsVersion = 7.0.241
 * pdfjsBuild = 808186117
 */
export const __webpack_esm_ids__ = ["flatten"];
export const __webpack_esm_modules__ = {

/***/ 2537
(__unused_webpack___webpack_module__, __webpack_exports__, __webpack_require__) {

/* harmony export */ __webpack_require__.d(__webpack_exports__, {
/* harmony export */   PageAnnotationFlattener: () => (/* binding */ PageAnnotationFlattener),
/* harmony export */   StructTreeFlattener: () => (/* binding */ StructTreeFlattener),
/* harmony export */   finalizeFlattening: () => (/* binding */ finalizeFlattening)
/* harmony export */ });
/* harmony import */ var _shared_util_js__WEBPACK_IMPORTED_MODULE_0__ = __webpack_require__(4292);
/* harmony import */ var _core_utils_js__WEBPACK_IMPORTED_MODULE_1__ = __webpack_require__(5399);
/* harmony import */ var _primitives_js__WEBPACK_IMPORTED_MODULE_2__ = __webpack_require__(5632);
/* harmony import */ var _annotation_js__WEBPACK_IMPORTED_MODULE_3__ = __webpack_require__(3738);
/* harmony import */ var _parser_js__WEBPACK_IMPORTED_MODULE_4__ = __webpack_require__(1440);
/* harmony import */ var _stream_js__WEBPACK_IMPORTED_MODULE_5__ = __webpack_require__(4106);
/* harmony import */ var _base_stream_js__WEBPACK_IMPORTED_MODULE_6__ = __webpack_require__(1834);
/* harmony import */ var _evaluator_js__WEBPACK_IMPORTED_MODULE_7__ = __webpack_require__(9285);
/* harmony import */ var _writer_js__WEBPACK_IMPORTED_MODULE_8__ = __webpack_require__(6721);
/* harmony import */ var _name_number_tree_js__WEBPACK_IMPORTED_MODULE_9__ = __webpack_require__(1740);










const INTERACTIVE_ANNOTATION_TYPES = new Set(["3D", "FileAttachment", "Link", "Movie", "RichMedia", "Screen", "Sound"]);
class PageAnnotationFlattener {
  #annotationStorage;
  #changes;
  #changesXref;
  #evaluator;
  #flattenForms;
  #getResources;
  #intent;
  #page;
  #structTree;
  #task;
  constructor({
    page,
    annotationStorage,
    changes,
    flattenForms,
    intent,
    structTree,
    getResources
  }) {
    this.#page = page;
    this.#annotationStorage = annotationStorage;
    this.#changes = changes;
    this.#changesXref = new _core_utils_js__WEBPACK_IMPORTED_MODULE_1__.ChangesXRefWrapper(changes, page.xref);
    this.#flattenForms = flattenForms;
    this.#intent = intent;
    this.#structTree = structTree;
    this.#getResources = getResources;
  }
  async #fetch(obj) {
    try {
      return await this.#changesXref.fetchIfRefAsync(obj);
    } catch (ex) {
      ;(0,_shared_util_js__WEBPACK_IMPORTED_MODULE_0__.warn)(`flattenAnnotations - cannot fetch "${obj}": "${ex}".`);
      return null;
    }
  }
  async flatten(handler, task) {
    const page = this.#page;
    const changes = this.#changes;
    const annotationStorage = this.#annotationStorage;
    const flattenForms = this.#flattenForms;
    const intent = this.#intent;
    const structTree = this.#structTree;
    const {
      xref
    } = page;
    if (!(page.ref instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_2__.Ref)) {
      const hasWidgets = (await page._parsedAnnotations).some(annotation => annotation instanceof _annotation_js__WEBPACK_IMPORTED_MODULE_3__.WidgetAnnotation);
      return {
        hadWidgets: hasWidgets,
        hasWidgets,
        removed: null,
        tags: null
      };
    }
    const pageDict = changes.get(page.ref)?.data || page.pageDict.clone();
    const annotations = changes.has(page.ref) ? pageDict.get("Annots") : page.annotations;
    if (!Array.isArray(annotations) || annotations.length === 0) {
      return {
        hadWidgets: false,
        hasWidgets: false,
        removed: null,
        tags: null
      };
    }
    const parsedAnnotations = new _primitives_js__WEBPACK_IMPORTED_MODULE_2__.RefMap();
    for (const parsedAnnotation of await page._parsedAnnotations) {
      if (parsedAnnotation.ref) {
        parsedAnnotations.put(parsedAnnotation.ref, parsedAnnotation);
      }
    }
    this.#evaluator = page._createPartialEvaluator(handler);
    this.#task = task;
    const flattened = [];
    const kept = [];
    const removed = new _primitives_js__WEBPACK_IMPORTED_MODULE_2__.RefSet();
    const removedPopups = new _primitives_js__WEBPACK_IMPORTED_MODULE_2__.RefSet();
    const tags = [];
    const interactive = [];
    let hasWidgets = false,
      hadWidgets = false;
    for (const annotation of annotations) {
      const dict = await this.#fetch(annotation);
      if (!(dict instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_2__.Dict)) {
        continue;
      }
      let subtype, flags;
      try {
        subtype = dict.get("Subtype");
        flags = dict.get("F");
      } catch (ex) {
        ;(0,_shared_util_js__WEBPACK_IMPORTED_MODULE_0__.warn)(`flattenAnnotations - ignoring annotation: "${ex}".`);
        kept.push({
          annotation
        });
        continue;
      }
      const isWidget = (0,_primitives_js__WEBPACK_IMPORTED_MODULE_2__.isName)(subtype, "Widget");
      hadWidgets ||= isWidget;
      if (isWidget && (!flattenForms || dict.has("PMD"))) {
        hasWidgets = true;
        kept.push({
          annotation
        });
        continue;
      }
      const parsedAnnotation = annotation instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_2__.Ref ? parsedAnnotations.get(annotation) : null;
      const isModified = annotation instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_2__.Ref && changes.has(annotation);
      let isVisible;
      if (parsedAnnotation && (!isModified || isWidget)) {
        isVisible = intent & _shared_util_js__WEBPACK_IMPORTED_MODULE_0__.RenderingIntentFlag.PRINT ? parsedAnnotation.mustBePrinted(annotationStorage) : parsedAnnotation.mustBeViewed(annotationStorage, false);
      } else {
        isVisible = (intent & _shared_util_js__WEBPACK_IMPORTED_MODULE_0__.RenderingIntentFlag.PRINT ? !!(flags & _shared_util_js__WEBPACK_IMPORTED_MODULE_0__.AnnotationFlag.PRINT) : !(flags & _shared_util_js__WEBPACK_IMPORTED_MODULE_0__.AnnotationFlag.NOVIEW)) && !(flags & _shared_util_js__WEBPACK_IMPORTED_MODULE_0__.AnnotationFlag.HIDDEN);
      }
      let appearance = null;
      if (isVisible) {
        try {
          appearance = await this.#getAppearanceToFlatten({
            dict,
            parsedAnnotation,
            isModified
          });
        } catch (ex) {
          ;(0,_shared_util_js__WEBPACK_IMPORTED_MODULE_0__.warn)(`flattenAnnotations - cannot get the appearance: "${ex}".`);
        }
      }
      if (subtype instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_2__.Name && INTERACTIVE_ANNOTATION_TYPES.has(subtype.name)) {
        if (appearance && annotation instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_2__.Ref) {
          flattened.push({
            appearance,
            oc: dict.getRaw("OC"),
            tag: null
          });
          interactive.push({
            annotation,
            dict
          });
        }
        kept.push({
          annotation
        });
        continue;
      }
      if (!appearance && (!isWidget || isVisible)) {
        hasWidgets ||= isWidget;
        const isPopup = (0,_primitives_js__WEBPACK_IMPORTED_MODULE_2__.isName)(subtype, "Popup");
        kept.push({
          annotation,
          isPopup,
          parent: isPopup ? dict.getRaw("Parent") : null
        });
        continue;
      }
      let tag = null;
      if (annotation instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_2__.Ref) {
        removed.put(annotation);
        const popup = dict.getRaw("Popup");
        if (popup instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_2__.Ref) {
          removedPopups.put(popup);
        }
        tag = structTree?.getTag(dict) ?? null;
        if (tag) {
          tag.annotation = annotation;
          tags.push(tag);
        }
      }
      if (appearance) {
        flattened.push({
          appearance,
          oc: dict.getRaw("OC"),
          tag
        });
      }
    }
    const newAnnotations = [];
    for (const {
      annotation,
      isPopup,
      parent
    } of kept) {
      if (!isPopup || !(parent instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_2__.Ref && removed.has(parent) || annotation instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_2__.Ref && removedPopups.has(annotation))) {
        newAnnotations.push(annotation);
      } else if (annotation instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_2__.Ref) {
        removed.put(annotation);
      }
    }
    if (newAnnotations.length === annotations.length && flattened.length === 0) {
      return {
        hadWidgets,
        hasWidgets,
        removed: null,
        tags: null
      };
    }
    if (flattened.length > 0 && !(await this.#drawFlattenedAppearances(pageDict, flattened))) {
      return {
        hadWidgets,
        hasWidgets: hadWidgets,
        removed: null,
        tags: null
      };
    }
    pageDict.set("Annots", newAnnotations);
    if (interactive.length > 0) {
      const emptyAppearanceDict = new _primitives_js__WEBPACK_IMPORTED_MODULE_2__.Dict(xref);
      emptyAppearanceDict.setIfName("Subtype", "Form");
      emptyAppearanceDict.set("BBox", [0, 0, 1, 1]);
      const emptyAppearanceRef = xref.getNewTemporaryRef();
      changes.put(emptyAppearanceRef, {
        data: new _stream_js__WEBPACK_IMPORTED_MODULE_5__.StringStream("", emptyAppearanceDict)
      });
      for (const {
        annotation,
        dict
      } of interactive) {
        const ap = new _primitives_js__WEBPACK_IMPORTED_MODULE_2__.Dict(xref);
        ap.set("N", emptyAppearanceRef);
        const newDict = dict.clone();
        newDict.set("AP", ap);
        newDict.delete("AS");
        changes.put(annotation, {
          data: newDict
        });
      }
    }
    changes.put(page.ref, {
      data: pageDict
    });
    return {
      hadWidgets,
      hasWidgets,
      removed,
      tags
    };
  }
  async #drawFlattenedAppearances(pageDict, flattened) {
    const {
      xref
    } = this.#page;
    const changes = this.#changes;
    const structTree = this.#structTree;
    let contentStream, unbalanced, resources;
    try {
      contentStream = await this.#page.getContentStream();
      unbalanced = getUnbalancedOperators(contentStream, xref);
      resources = await this.#getResources(contentStream.dict);
    } catch (ex) {
      ;(0,_shared_util_js__WEBPACK_IMPORTED_MODULE_0__.warn)(`flattenAnnotations - cannot parse the page content: "${ex}".`);
      return false;
    }
    const {
      saves,
      unmatchedRestores,
      markedContent,
      inText,
      lastMCID
    } = unbalanced;
    let contents;
    if (unmatchedRestores > 0) {
      const data = removeUnmatchedRestores(contentStream, unmatchedRestores, xref);
      if (!data) {
        return false;
      }
      const contentRef = xref.getNewTemporaryRef();
      changes.put(contentRef, {
        data: new _stream_js__WEBPACK_IMPORTED_MODULE_5__.Stream(data, 0, data.length, new _primitives_js__WEBPACK_IMPORTED_MODULE_2__.Dict(xref))
      });
      contents = [contentRef];
    } else {
      contents = pageDict.getRaw("Contents");
      if (contents instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_2__.Ref) {
        const obj = await this.#fetch(contents);
        if (Array.isArray(obj)) {
          contents = obj;
        }
      }
      if (!Array.isArray(contents)) {
        contents = contents ? [contents] : [];
      }
    }
    resources = resources.clone();
    const newResources = new Map();
    const resourceNames = new Map();
    const addResource = (key, prefix, value) => {
      const id = value instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_2__.Ref ? `${key}_${value}` : null;
      if (id && resourceNames.has(id)) {
        return resourceNames.get(id);
      }
      let dict = newResources.get(key);
      if (!dict) {
        let oldDict = null;
        try {
          oldDict = resources.get(key);
        } catch (ex) {
          ;(0,_shared_util_js__WEBPACK_IMPORTED_MODULE_0__.warn)(`flattenAnnotations - cannot get the resources: "${ex}".`);
        }
        dict = oldDict instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_2__.Dict ? oldDict.clone() : new _primitives_js__WEBPACK_IMPORTED_MODULE_2__.Dict(xref);
        newResources.set(key, dict);
      }
      let i = dict.size;
      while (dict.has(`${prefix}${i}`)) {
        i++;
      }
      const name = `${prefix}${i}`;
      dict.set(name, value);
      if (id) {
        resourceNames.set(id, name);
      }
      return name;
    };
    const buffer = [];
    let mcid = structTree?.getNextMCID(pageDict, lastMCID);
    for (const {
      appearance,
      oc,
      tag
    } of flattened) {
      const transform = appearance.transform.map(_writer_js__WEBPACK_IMPORTED_MODULE_8__.numberToPDFString).join(" ");
      const name = addResource("XObject", "FlatAnnot", appearance.ref);
      const ocName = oc ? addResource("Properties", "FlatOC", oc) : null;
      if (tag?.type) {
        tag.mcid = mcid++;
        buffer.push(`/${(0,_core_utils_js__WEBPACK_IMPORTED_MODULE_1__.escapePDFName)(tag.type)} <</MCID ${tag.mcid}>> BDC`);
      }
      if (ocName) {
        buffer.push(`/OC /${ocName} BDC`);
      }
      buffer.push(`q ${transform} cm /${name} Do Q`);
      if (ocName) {
        buffer.push("EMC");
      }
      if (tag?.type) {
        buffer.push("EMC");
      }
    }
    for (const [key, dict] of newResources) {
      resources.set(key, dict);
    }
    pageDict.set("Resources", resources);
    const close = (inText ? "ET\n" : "") + "EMC\n".repeat(markedContent) + "Q\n".repeat(saves + 1);
    const saveRef = xref.getNewTemporaryRef();
    const restoreRef = xref.getNewTemporaryRef();
    changes.put(saveRef, {
      data: new _stream_js__WEBPACK_IMPORTED_MODULE_5__.StringStream("q\n", new _primitives_js__WEBPACK_IMPORTED_MODULE_2__.Dict(xref))
    });
    changes.put(restoreRef, {
      data: new _stream_js__WEBPACK_IMPORTED_MODULE_5__.StringStream(`\n${close}${buffer.join("\n")}\n`, new _primitives_js__WEBPACK_IMPORTED_MODULE_2__.Dict(xref))
    });
    pageDict.set("Contents", [saveRef, ...contents, restoreRef]);
    return true;
  }
  async #getAppearanceToFlatten({
    dict,
    parsedAnnotation,
    isModified
  }) {
    let rect = (0,_core_utils_js__WEBPACK_IMPORTED_MODULE_1__.lookupNormalRect)(dict.getArray("Rect"), null);
    let ref = null,
      appearance = null;
    if (parsedAnnotation instanceof _annotation_js__WEBPACK_IMPORTED_MODULE_3__.WidgetAnnotation) {
      appearance = await parsedAnnotation.getFlattenedAppearance(this.#evaluator, this.#task, this.#annotationStorage, this.#intent);
      if (appearance === false) {
        return null;
      }
    }
    if (!appearance) {
      const ap = await this.#fetch(dict.getRaw("AP"));
      if (ap instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_2__.Dict) {
        ref = ap.getRaw("N");
        appearance = await this.#fetch(ref);
        if (appearance instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_2__.Dict) {
          const state = dict.get("AS");
          ref = state instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_2__.Name ? appearance.getRaw(state.name) : null;
          appearance = await this.#fetch(ref);
        }
      } else if (!isModified && parsedAnnotation?.appearance && isSelfContained(parsedAnnotation.appearance)) {
        ({
          appearance
        } = parsedAnnotation);
        rect = parsedAnnotation.data.rect;
      }
    }
    if (!(appearance instanceof _base_stream_js__WEBPACK_IMPORTED_MODULE_6__.BaseStream) || !rect) {
      return null;
    }
    const size = [0, 0, rect[2] - rect[0], rect[3] - rect[1]];
    const matrix = (0,_core_utils_js__WEBPACK_IMPORTED_MODULE_1__.lookupMatrix)(appearance.dict?.getArray("Matrix"), _core_utils_js__WEBPACK_IMPORTED_MODULE_1__.IDENTITY_MATRIX);
    const originalBBox = (0,_core_utils_js__WEBPACK_IMPORTED_MODULE_1__.lookupRect)(appearance.dict?.getArray("BBox"), null);
    let bbox = originalBBox ?? size;
    const transform = (0,_annotation_js__WEBPACK_IMPORTED_MODULE_3__.getTransformMatrix)(rect, bbox, matrix);
    const box = _shared_util_js__WEBPACK_IMPORTED_MODULE_0__.BBOX_INIT.slice();
    _shared_util_js__WEBPACK_IMPORTED_MODULE_0__.Util.axialAlignedBoundingBox(bbox, matrix, box);
    if (box[0] === box[2] || box[1] === box[3]) {
      bbox = _shared_util_js__WEBPACK_IMPORTED_MODULE_0__.BBOX_INIT.slice();
      _shared_util_js__WEBPACK_IMPORTED_MODULE_0__.Util.axialAlignedBoundingBox(size, _shared_util_js__WEBPACK_IMPORTED_MODULE_0__.Util.inverseTransform(matrix), bbox);
      if (!bbox.every(Number.isFinite)) {
        bbox = size;
      }
    }
    if (!transform.every(Number.isFinite) || !bbox.every(Number.isFinite)) {
      return null;
    }
    if (!(ref instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_2__.Ref) || !(0,_primitives_js__WEBPACK_IMPORTED_MODULE_2__.isName)(appearance.dict?.get("Subtype"), "Form") || bbox !== originalBBox) {
      appearance = this.#copyObject(appearance, bbox);
      appearance.dict.setIfName("Subtype", "Form");
      appearance.dict.set("BBox", bbox);
      ref = this.#page.xref.getNewTemporaryRef();
      this.#changes.put(ref, {
        data: appearance
      });
    }
    return {
      ref,
      transform
    };
  }
  #copyObject(obj, bbox) {
    if (obj instanceof _base_stream_js__WEBPACK_IMPORTED_MODULE_6__.BaseStream) {
      bbox = (0,_core_utils_js__WEBPACK_IMPORTED_MODULE_1__.lookupRect)(obj.dict?.getArray("BBox"), bbox);
      const dict = obj.dict ? this.#copyObject(obj.dict, bbox) : new _primitives_js__WEBPACK_IMPORTED_MODULE_2__.Dict(this.#page.xref);
      dict.delete("Filter");
      dict.delete("DecodeParms");
      dict.delete("Length");
      if ((0,_primitives_js__WEBPACK_IMPORTED_MODULE_2__.isName)(dict.get("Subtype"), "Form") && !(0,_core_utils_js__WEBPACK_IMPORTED_MODULE_1__.lookupRect)(dict.getArray("BBox"), null)) {
        dict.set("BBox", bbox);
      }
      obj.reset();
      const bytes = obj.getBytes();
      obj.reset();
      return new _stream_js__WEBPACK_IMPORTED_MODULE_5__.Stream(bytes, 0, bytes.length, dict);
    }
    const toIndirect = value => {
      const copy = this.#copyObject(value, bbox);
      if (!(copy instanceof _base_stream_js__WEBPACK_IMPORTED_MODULE_6__.BaseStream)) {
        return copy;
      }
      const ref = this.#page.xref.getNewTemporaryRef();
      this.#changes.put(ref, {
        data: copy
      });
      return ref;
    };
    if (obj instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_2__.Dict) {
      const dict = new _primitives_js__WEBPACK_IMPORTED_MODULE_2__.Dict(this.#page.xref);
      for (const [key, value] of obj.getRawEntries()) {
        dict.set(key, toIndirect(value));
      }
      return dict;
    }
    return Array.isArray(obj) ? obj.map(toIndirect) : obj;
  }
}
function getUnbalancedOperators(stream, xref) {
  const result = {
    saves: 0,
    unmatchedRestores: 0,
    markedContent: 0,
    inText: false,
    lastMCID: -1
  };
  const preprocessor = new _evaluator_js__WEBPACK_IMPORTED_MODULE_7__.EvaluatorPreprocessor(stream, xref);
  const operation = {
    fn: 0,
    args: []
  };
  while (true) {
    operation.args.length = 0;
    if (!preprocessor.read(operation)) {
      break;
    }
    switch (operation.fn | 0) {
      case _shared_util_js__WEBPACK_IMPORTED_MODULE_0__.OPS.save:
        result.saves++;
        break;
      case _shared_util_js__WEBPACK_IMPORTED_MODULE_0__.OPS.restore:
        if (result.saves > 0) {
          result.saves--;
        } else {
          result.unmatchedRestores++;
        }
        break;
      case _shared_util_js__WEBPACK_IMPORTED_MODULE_0__.OPS.beginText:
        result.inText = true;
        break;
      case _shared_util_js__WEBPACK_IMPORTED_MODULE_0__.OPS.endText:
        result.inText = false;
        break;
      case _shared_util_js__WEBPACK_IMPORTED_MODULE_0__.OPS.beginMarkedContentProps:
        {
          const props = operation.args[1];
          const mcid = props instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_2__.Dict ? props.get("MCID") : null;
          if (Number.isInteger(mcid) && mcid > result.lastMCID) {
            result.lastMCID = mcid;
          }
          result.markedContent++;
          break;
        }
      case _shared_util_js__WEBPACK_IMPORTED_MODULE_0__.OPS.beginMarkedContent:
        result.markedContent++;
        break;
      case _shared_util_js__WEBPACK_IMPORTED_MODULE_0__.OPS.endMarkedContent:
        if (result.markedContent > 0) {
          result.markedContent--;
        }
        break;
    }
  }
  return result;
}
function removeUnmatchedRestores(stream, count, xref) {
  try {
    stream.reset();
    const data = stream.getBytes().slice();
    const lexer = new _parser_js__WEBPACK_IMPORTED_MODULE_4__.Lexer(new _stream_js__WEBPACK_IMPORTED_MODULE_5__.Stream(data), _evaluator_js__WEBPACK_IMPORTED_MODULE_7__.EvaluatorPreprocessor.opMap);
    let saves = 0,
      nesting = 0;
    for (let obj = lexer.getObj(); obj !== _primitives_js__WEBPACK_IMPORTED_MODULE_2__.EOF; obj = lexer.getObj()) {
      while ((0,_primitives_js__WEBPACK_IMPORTED_MODULE_2__.isCmd)(obj, "BI")) {
        const parser = new _parser_js__WEBPACK_IMPORTED_MODULE_4__.Parser({
          lexer,
          xref
        });
        parser.makeInlineImage(null);
        obj = parser.buf2;
      }
      if (!(obj instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_2__.Cmd)) {
        continue;
      }
      switch (obj.cmd) {
        case "[":
        case "<<":
          nesting++;
          break;
        case "]":
        case ">>":
          nesting = Math.max(nesting - 1, 0);
          break;
        case "q":
          if (nesting === 0) {
            saves++;
          }
          break;
        case "Q":
          if (nesting > 0) {
            break;
          }
          if (saves > 0) {
            saves--;
            break;
          }
          const end = lexer.currentChar < 0 ? data.length : lexer.stream.pos - 1;
          data[end - 1] = 0x20;
          count--;
          break;
      }
    }
    return count === 0 ? data : null;
  } catch (ex) {
    ;(0,_shared_util_js__WEBPACK_IMPORTED_MODULE_0__.warn)(`flattenAnnotations - cannot remove the unmatched restores: "${ex}".`);
    return null;
  }
}
function isSelfContained(obj) {
  if (obj instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_2__.Ref) {
    return false;
  }
  if (obj instanceof _base_stream_js__WEBPACK_IMPORTED_MODULE_6__.BaseStream) {
    return !obj.dict || isSelfContained(obj.dict);
  }
  return obj instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_2__.Dict ? obj.getRawValues().every(isSelfContained) : !Array.isArray(obj) || obj.every(isSelfContained);
}
async function finalizeFlattening({
  results,
  structTree,
  manager,
  acroForm,
  acroFormRef,
  catalogRef,
  changes,
  xref,
  flattenForms
}) {
  const changesXref = new _core_utils_js__WEBPACK_IMPORTED_MODULE_1__.ChangesXRefWrapper(changes, xref);
  const getDict = obj => {
    try {
      const data = changesXref.fetchIfRef(obj);
      return data instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_2__.Dict ? data : null;
    } catch {
      return null;
    }
  };
  if (structTree) {
    for (const {
      pageRef,
      tags
    } of results) {
      if (tags?.length > 0 && pageRef instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_2__.Ref) {
        structTree.update({
          pageRef,
          tags
        });
      }
    }
    structTree.save();
  }
  const removed = new _primitives_js__WEBPACK_IMPORTED_MODULE_2__.RefSet();
  for (const result of results) {
    for (const ref of result.removed ?? []) {
      removed.put(ref);
    }
  }
  const keepAcroForm = !flattenForms || !(catalogRef instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_2__.Ref) || !results.some(({
    hadWidgets
  }) => hadWidgets) || results.some(({
    hasWidgets
  }) => hasWidgets) || (await manager.ensureDoc("formInfo")).hasSignatures;
  if (!keepAcroForm) {
    const catalog = getDict(catalogRef);
    if (catalog instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_2__.Dict && catalog.has("AcroForm")) {
      const newCatalog = catalog.clone();
      newCatalog.delete("AcroForm");
      changes.put(catalogRef, {
        data: newCatalog
      });
    }
    acroForm = null;
  } else if (flattenForms && acroForm instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_2__.Dict) {
    acroForm = pruneFields({
      acroForm,
      acroFormRef,
      catalogRef,
      changes,
      removed,
      getDict
    });
  }
  pruneChanges({
    changes,
    removed,
    removeFields: !keepAcroForm,
    xref,
    getDict
  });
  return acroForm;
}
function pruneFields({
  acroForm,
  acroFormRef,
  catalogRef,
  changes,
  removed,
  getDict
}) {
  const visited = new _primitives_js__WEBPACK_IMPORTED_MODULE_2__.RefSet();
  const pruneKids = kids => {
    const newKids = [];
    let modified = false;
    for (const kid of kids) {
      const dict = getDict(kid);
      if (kid instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_2__.Ref) {
        if (removed.has(kid) && !(dict && (0,_primitives_js__WEBPACK_IMPORTED_MODULE_2__.isName)((0,_core_utils_js__WEBPACK_IMPORTED_MODULE_1__.getInheritableProperty)({
          dict,
          key: "FT"
        }), "Sig"))) {
          modified = true;
          continue;
        }
        if (visited.has(kid)) {
          newKids.push(kid);
          continue;
        }
        visited.put(kid);
      }
      const grandKids = dict?.get("Kids");
      const newGrandKids = Array.isArray(grandKids) && grandKids.length > 0 ? pruneKids(grandKids) : grandKids;
      if (newGrandKids === grandKids) {
        newKids.push(kid);
        continue;
      }
      modified = true;
      if (newGrandKids.length === 0) {
        if (kid instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_2__.Ref) {
          removed.put(kid);
        }
        continue;
      }
      const newDict = dict.clone();
      newDict.set("Kids", newGrandKids);
      if (kid instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_2__.Ref) {
        changes.put(kid, {
          ...changes.get(kid),
          data: newDict
        });
        newKids.push(kid);
      } else {
        newKids.push(newDict);
      }
    }
    return modified ? newKids : kids;
  };
  const fields = acroForm.get("Fields");
  if (!Array.isArray(fields)) {
    return acroForm;
  }
  const newFields = pruneKids(fields);
  const co = acroForm.get("CO");
  const newCO = Array.isArray(co) ? co.filter(ref => !(ref instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_2__.Ref && removed.has(ref))) : co;
  if (newFields === fields && newCO?.length === co?.length) {
    return acroForm;
  }
  acroForm = acroForm.clone();
  acroForm.set("Fields", newFields);
  if (Array.isArray(newCO)) {
    acroForm.set("CO", newCO);
  }
  if (acroFormRef) {
    changes.put(acroFormRef, {
      data: acroForm
    });
  } else if (catalogRef instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_2__.Ref) {
    const catalog = getDict(catalogRef);
    if (catalog instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_2__.Dict) {
      const newCatalog = catalog.clone();
      newCatalog.set("AcroForm", acroForm);
      changes.put(catalogRef, {
        data: newCatalog
      });
    }
  }
  return acroForm;
}
function pruneChanges({
  changes,
  removed,
  removeFields,
  xref,
  getDict
}) {
  const pruned = new _primitives_js__WEBPACK_IMPORTED_MODULE_2__.RefSet(removed);
  if (removeFields) {
    for (const refStr of removed) {
      for (let parent = getDict(_primitives_js__WEBPACK_IMPORTED_MODULE_2__.Ref.fromString(refStr))?.getRaw("Parent"); parent instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_2__.Ref && !pruned.has(parent); parent = getDict(parent)?.getRaw("Parent")) {
        pruned.put(parent);
      }
    }
  }
  for (const ref of pruned) {
    changes.remove(ref);
  }
  const isNew = ref => !xref.getEntry(ref.num);
  const used = new _primitives_js__WEBPACK_IMPORTED_MODULE_2__.RefSet();
  const visit = obj => {
    if (obj instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_2__.Ref) {
      if (isNew(obj) && changes.has(obj) && !used.has(obj)) {
        used.put(obj);
        visit(changes.get(obj).data);
      }
    } else if (obj instanceof _base_stream_js__WEBPACK_IMPORTED_MODULE_6__.BaseStream) {
      visit(obj.dict);
    } else if (obj instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_2__.Dict) {
      for (const value of obj.getRawValues()) {
        visit(value);
      }
    } else if (Array.isArray(obj)) {
      for (const value of obj) {
        visit(value);
      }
    }
  };
  for (const [ref, {
    data
  }] of changes.items()) {
    if (!isNew(ref)) {
      visit(data);
    }
  }
  for (const ref of changes.keys()) {
    if (isNew(ref) && !used.has(ref)) {
      changes.remove(ref);
    }
  }
}
class StructTreeFlattener {
  #changes;
  #changesXref;
  #entries = null;
  #isModified = false;
  #numberTree = null;
  #parentTree = null;
  #parentTreeRef = null;
  #root;
  #rootRef;
  #xref;
  constructor({
    changes,
    root,
    rootRef,
    xref
  }) {
    this.#changes = changes;
    this.#changesXref = new _core_utils_js__WEBPACK_IMPORTED_MODULE_1__.ChangesXRefWrapper(changes, xref);
    this.#root = root;
    this.#rootRef = rootRef;
    this.#xref = xref;
    const parentTreeRef = root.getRaw("ParentTree");
    const parentTree = this.#fetch(parentTreeRef);
    if (!(parentTree instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_2__.Dict)) {
      return;
    }
    const numberTree = new _name_number_tree_js__WEBPACK_IMPORTED_MODULE_9__.NumberTree(parentTreeRef, this.#changesXref);
    try {
      this.#entries = numberTree.getAll(true);
    } catch (ex) {
      ;(0,_shared_util_js__WEBPACK_IMPORTED_MODULE_0__.warn)(`StructTreeFlattener: cannot read the parent tree: "${ex}".`);
      this.#numberTree = numberTree;
      return;
    }
    for (const key of this.#entries.keys()) {
      if (!Number.isInteger(key)) {
        this.#entries.delete(key);
      }
    }
    this.#parentTree = parentTree;
    this.#parentTreeRef = parentTreeRef;
  }
  static create({
    catalogRef,
    changes,
    xref
  }) {
    try {
      const changesXref = new _core_utils_js__WEBPACK_IMPORTED_MODULE_1__.ChangesXRefWrapper(changes, xref);
      const rootRef = changesXref.fetchIfRef(catalogRef)?.getRaw("StructTreeRoot");
      const root = changesXref.fetchIfRef(rootRef);
      return rootRef instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_2__.Ref && root instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_2__.Dict ? new StructTreeFlattener({
        changes,
        root,
        rootRef,
        xref
      }) : null;
    } catch (ex) {
      ;(0,_shared_util_js__WEBPACK_IMPORTED_MODULE_0__.warn)(`StructTreeFlattener: "${ex}".`);
      return null;
    }
  }
  #fetch(obj) {
    return this.#changesXref.fetchIfRef(obj);
  }
  getTag(annotationDict) {
    try {
      const structParent = annotationDict.get("StructParent");
      if (!Number.isInteger(structParent)) {
        return null;
      }
      const ref = this.#entries ? this.#entries.get(structParent) : this.#numberTree?.getRaw(structParent);
      const element = this.#fetch(ref);
      if (!(ref instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_2__.Ref) || !(element instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_2__.Dict)) {
        return null;
      }
      const type = element.get("S");
      return {
        ref,
        structParent,
        type: this.#entries && type instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_2__.Name ? type.name : null
      };
    } catch (ex) {
      ;(0,_shared_util_js__WEBPACK_IMPORTED_MODULE_0__.warn)(`StructTreeFlattener.getTag: "${ex}".`);
      return null;
    }
  }
  getNextMCID(pageDict, lastMCID) {
    const parents = this.#fetch(this.#entries?.get(pageDict.get("StructParents")));
    return Math.max(lastMCID + 1, Array.isArray(parents) ? parents.length : 0);
  }
  update({
    pageRef,
    tags
  }) {
    const xref = this.#xref;
    let parents = null;
    for (const {
      annotation,
      mcid,
      ref,
      structParent
    } of tags) {
      try {
        let mcr = null;
        if (this.#entries && Number.isInteger(mcid)) {
          mcr = new _primitives_js__WEBPACK_IMPORTED_MODULE_2__.Dict(xref);
          mcr.set("Type", _primitives_js__WEBPACK_IMPORTED_MODULE_2__.Name.get("MCR"));
          mcr.set("Pg", pageRef);
          mcr.set("MCID", mcid);
          parents ??= this.#getParents(pageRef);
          while (parents.length < mcid) {
            parents.push(null);
          }
          parents[mcid] = ref;
        }
        this.#replaceObjectReference(ref, annotation, mcr);
        if (this.#entries?.delete(structParent)) {
          this.#isModified = true;
        }
      } catch (ex) {
        ;(0,_shared_util_js__WEBPACK_IMPORTED_MODULE_0__.warn)(`StructTreeFlattener.update: "${ex}".`);
      }
    }
  }
  #getParents(pageRef) {
    const pageDict = this.#changes.get(pageRef).data;
    let key = pageDict.get("StructParents");
    let parents = this.#fetch(this.#entries.get(key));
    if (Number.isInteger(key) && Array.isArray(parents)) {
      parents = parents.slice();
    } else {
      key = this.#root.get("ParentTreeNextKey");
      if (!Number.isInteger(key) || key < 0) {
        key = 0;
      }
      for (const entryKey of this.#entries.keys()) {
        key = Math.max(key, entryKey + 1);
      }
      this.#root = this.#root.clone();
      this.#root.set("ParentTreeNextKey", key + 1);
      pageDict.set("StructParents", key);
      parents = [];
    }
    this.#entries.set(key, parents);
    this.#isModified = true;
    return parents;
  }
  #replaceObjectReference(ref, annotation, replacement) {
    const element = this.#fetch(ref);
    if (!(element instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_2__.Dict)) {
      return;
    }
    const rawKids = element.getRaw("K");
    const kids = this.#fetch(rawKids);
    const newKids = [];
    let found = false;
    for (const kid of Array.isArray(kids) ? kids : [rawKids]) {
      let obj;
      try {
        obj = this.#fetch(kid);
      } catch (ex) {
        ;(0,_shared_util_js__WEBPACK_IMPORTED_MODULE_0__.warn)(`StructTreeFlattener: cannot read a structure kid: "${ex}".`);
      }
      const objRef = obj instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_2__.Dict && (0,_primitives_js__WEBPACK_IMPORTED_MODULE_2__.isName)(obj.get("Type"), "OBJR") ? obj.getRaw("Obj") : null;
      if (objRef instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_2__.Ref && annotation instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_2__.Ref && (0,_primitives_js__WEBPACK_IMPORTED_MODULE_2__.isRefsEqual)(objRef, annotation)) {
        found = true;
        if (replacement) {
          newKids.push(replacement);
        }
        continue;
      }
      newKids.push(kid);
    }
    if (found) {
      const newElement = element.clone();
      newElement.set("K", newKids);
      this.#changes.put(ref, {
        data: newElement
      });
    }
  }
  save() {
    if (!this.#isModified) {
      return;
    }
    const xref = this.#xref;
    const nums = [];
    for (const key of [...this.#entries.keys()].sort((a, b) => a - b)) {
      nums.push(key, this.#entries.get(key));
    }
    const parentTree = this.#parentTree.clone();
    parentTree.delete("Kids");
    parentTree.delete("Limits");
    parentTree.set("Nums", nums);
    let parentTreeRef = this.#parentTreeRef;
    if (!(parentTreeRef instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_2__.Ref)) {
      parentTreeRef = xref.getNewTemporaryRef();
      this.#root = this.#root.clone();
      this.#root.set("ParentTree", parentTreeRef);
    }
    this.#changes.put(parentTreeRef, {
      data: parentTree
    });
    this.#changes.put(this.#rootRef, {
      data: this.#root
    });
  }
}


/***/ }

};
