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
 * pdfjsVersion = 7.0.229
 * pdfjsBuild = 32fb05b3b
 */
export const __webpack_esm_ids__ = ["editor"];
export const __webpack_esm_modules__ = {

/***/ 3008
(__unused_webpack___webpack_module__, __webpack_exports__, __webpack_require__) {

/* harmony export */ __webpack_require__.d(__webpack_exports__, {
/* harmony export */   PDFEditor: () => (/* binding */ PDFEditor)
/* harmony export */ });
/* harmony import */ var _core_utils_js__WEBPACK_IMPORTED_MODULE_0__ = __webpack_require__(5399);
/* harmony import */ var _primitives_js__WEBPACK_IMPORTED_MODULE_1__ = __webpack_require__(5632);
/* harmony import */ var _writer_js__WEBPACK_IMPORTED_MODULE_2__ = __webpack_require__(6721);
/* harmony import */ var _shared_util_js__WEBPACK_IMPORTED_MODULE_3__ = __webpack_require__(4292);
/* harmony import */ var _name_number_tree_js__WEBPACK_IMPORTED_MODULE_4__ = __webpack_require__(1740);
/* harmony import */ var _string_utils_js__WEBPACK_IMPORTED_MODULE_5__ = __webpack_require__(5389);
/* harmony import */ var _annotation_js__WEBPACK_IMPORTED_MODULE_6__ = __webpack_require__(3738);
/* harmony import */ var _base_stream_js__WEBPACK_IMPORTED_MODULE_7__ = __webpack_require__(1834);
/* harmony import */ var _pdf_images_js__WEBPACK_IMPORTED_MODULE_8__ = __webpack_require__(3571);
/* harmony import */ var _document_js__WEBPACK_IMPORTED_MODULE_9__ = __webpack_require__(9397);
/* harmony import */ var _shared_murmurhash3_js__WEBPACK_IMPORTED_MODULE_10__ = /*#__PURE__*/__webpack_require__(7651);
/* harmony import */ var _stream_js__WEBPACK_IMPORTED_MODULE_11__ = __webpack_require__(4106);












const MAX_LEAVES_PER_PAGES_NODE = 16;
const MAX_IN_NAME_TREE_NODE = 64;
const INFO_CONFORMANCE_KEYS = ["GTS_PDFXVersion", "GTS_PDFXConformance", "ISO_PDFEVersion"];
class PageData {
  constructor(page, documentData) {
    this.page = page;
    this.documentData = documentData;
    this.annotations = null;
    this.pointingNamedDestinations = null;
    this.copyLevel = 0;
    documentData.pagesMap.put(page.ref, this);
  }
}
class DocumentData {
  constructor(document) {
    this.document = document;
    this.destinations = null;
    this.pageLabels = null;
    this.pagesMap = new _primitives_js__WEBPACK_IMPORTED_MODULE_1__.RefMap();
    this.oldRefMapping = new _primitives_js__WEBPACK_IMPORTED_MODULE_1__.RefMap();
    this.dedupNamedDestinations = new Map();
    this.usedNamedDestinations = new Set();
    this.postponedRefCopies = new _primitives_js__WEBPACK_IMPORTED_MODULE_1__.RefMap();
    this.resourceStreamPromises = new Map();
    this.usedStructParents = new Set();
    this.oldStructParentMapping = new Map();
    this.structTreeRoot = null;
    this.parentTree = null;
    this.idTree = null;
    this.roleMap = null;
    this.classMap = null;
    this.namespaces = null;
    this.structTreeAF = null;
    this.structTreePronunciationLexicon = [];
    this.acroForm = null;
    this.acroFormDefaultAppearance = "";
    this.acroFormDefaultResources = null;
    this.acroFormQ = 0;
    this.hasSignatureAnnotations = false;
    this.fieldToParent = new _primitives_js__WEBPACK_IMPORTED_MODULE_1__.RefMap();
    this.outline = null;
    this.embeddedFiles = null;
  }
}
class XRefWrapper {
  constructor(entries, getNewRef) {
    this.entries = entries;
    this._getNewRef = getNewRef;
  }
  getNewTemporaryRef() {
    return this._getNewRef();
  }
  countUpdatesAfter(offset) {
    return null;
  }
  fetchIfRef(obj) {
    return obj instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Ref ? this.fetch(obj) : obj;
  }
  fetch(ref) {
    if (!(ref instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Ref)) {
      throw new Error("ref object is not a reference");
    }
    return this.entries[ref.num];
  }
  async fetchIfRefAsync(obj) {
    return obj instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Ref ? this.fetchAsync(obj) : obj;
  }
  async fetchAsync(ref) {
    return this.fetch(ref);
  }
}
class PDFEditor {
  isSingleFile = false;
  #newAnnotationsParams = null;
  #primaryDocument = null;
  #primaryRefMappings = [];
  #resourceStreamCache = new Map();
  currentDocument = null;
  oldPages = [];
  newPages = [];
  xref = [null];
  xrefWrapper = new XRefWrapper(this.xref, () => this.newRef);
  newRefCount = 1;
  namesDict = null;
  version = "1.7";
  pageLabels = null;
  namedDestinations = new Map();
  parentTree = new Map();
  structTreeKids = [];
  idTree = new Map();
  classMap = new _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Dict();
  roleMap = new _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Dict();
  namespaces = new Map();
  structTreeAF = [];
  structTreePronunciationLexicon = [];
  fields = [];
  acroFormDefaultAppearance = "";
  acroFormDefaultResources = null;
  acroFormNeedAppearances = false;
  acroFormSigFlags = 0;
  acroFormCalculationOrder = null;
  acroFormQ = 0;
  outlineItems = null;
  embeddedFiles = new Map();
  constructor({
    useObjectStreams = true,
    title = "",
    author = ""
  } = {}) {
    [this.rootRef, this.rootDict] = this.newDict;
    [this.infoRef, this.infoDict] = this.newDict;
    [this.pagesRef, this.pagesDict] = this.newDict;
    this.useObjectStreams = useObjectStreams;
    this.objStreamRefs = useObjectStreams ? new Set() : null;
    this.title = title;
    this.author = author;
  }
  get newRef() {
    return _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Ref.get(this.newRefCount++, 0);
  }
  get newDict() {
    const ref = this.newRef;
    const dict = this.xref[ref.num] = new _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Dict();
    return [ref, dict];
  }
  async #cloneObject(obj, xref) {
    const ref = this.newRef;
    this.xref[ref.num] = await this.#collectDependencies(obj, true, xref);
    return ref;
  }
  cloneDict(dict) {
    const newDict = dict.clone();
    newDict.xref = this.xrefWrapper;
    return newDict;
  }
  async #collectDependencies(obj, mustClone, xref, resourceStreamPath = new _primitives_js__WEBPACK_IMPORTED_MODULE_1__.RefSet()) {
    if (obj instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Ref) {
      const {
        currentDocument: {
          fieldToParent,
          oldRefMapping
        }
      } = this;
      const existingRef = oldRefMapping.get(obj);
      if (existingRef) {
        return existingRef;
      }
      const oldRef = obj;
      obj = await xref.fetchAsync(oldRef);
      const mappedRef = oldRefMapping.get(oldRef);
      if (mappedRef) {
        return mappedRef;
      }
      if (typeof obj === "number") {
        return obj;
      }
      if (obj instanceof _base_stream_js__WEBPACK_IMPORTED_MODULE_7__.BaseStream && this.#isResourceStream(obj.dict)) {
        return this.#collectResourceStream(oldRef, obj, xref, resourceStreamPath);
      }
      const newRef = this.newRef;
      oldRefMapping.put(oldRef, newRef);
      let cloneSource = true;
      if (fieldToParent.has(oldRef) && obj instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Dict) {
        obj = this.cloneDict(obj);
        obj.delete("Parent");
        cloneSource = false;
      }
      this.xref[newRef.num] = await this.#collectDependencies(obj, cloneSource, xref, resourceStreamPath);
      return newRef;
    }
    const promises = [];
    const {
      currentDocument: {
        postponedRefCopies
      }
    } = this;
    if (Array.isArray(obj)) {
      if (mustClone) {
        obj = obj.slice();
      }
      for (let i = 0, ii = obj.length; i < ii; i++) {
        const postponedActions = obj[i] instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Ref && postponedRefCopies.get(obj[i]);
        if (postponedActions) {
          postponedActions.push(ref => obj[i] = ref);
          continue;
        }
        promises.push(this.#collectDependencies(obj[i], true, xref, resourceStreamPath).then(newObj => obj[i] = newObj));
      }
      await Promise.all(promises);
      return obj;
    }
    let dict;
    if (obj instanceof _base_stream_js__WEBPACK_IMPORTED_MODULE_7__.BaseStream) {
      ({
        dict
      } = obj = obj.getOriginalStream().clone());
      dict.xref = this.xrefWrapper;
    } else if (obj instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Dict) {
      if (mustClone) {
        obj = obj.clone();
        obj.xref = this.xrefWrapper;
      }
      dict = obj;
    }
    if (dict) {
      for (const [key, rawObj] of dict.getRawEntries()) {
        const postponedActions = rawObj instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Ref && postponedRefCopies.get(rawObj);
        if (postponedActions) {
          postponedActions.push(ref => dict.set(key, ref));
          continue;
        }
        promises.push(this.#collectDependencies(rawObj, true, xref, resourceStreamPath).then(newObj => dict.set(key, newObj)));
      }
      await Promise.all(promises);
    }
    return obj;
  }
  #isResourceStream(dict) {
    const subtype = dict.get("Subtype");
    return (0,_primitives_js__WEBPACK_IMPORTED_MODULE_1__.isName)(subtype, "Image") || dict.has("Length1") || (0,_primitives_js__WEBPACK_IMPORTED_MODULE_1__.isName)(subtype, "Type1C") || (0,_primitives_js__WEBPACK_IMPORTED_MODULE_1__.isName)(subtype, "CIDFontType0C") || (0,_primitives_js__WEBPACK_IMPORTED_MODULE_1__.isName)(subtype, "OpenType");
  }
  #rawStreamBytes(stream) {
    const original = stream.getOriginalStream();
    original.reset();
    return original.getBytes();
  }
  async #serializeDict(dict) {
    const buffer = [];
    await (0,_writer_js__WEBPACK_IMPORTED_MODULE_2__.writeValue)(dict, buffer, null);
    return buffer.join("");
  }
  #resourceStreamKey(dictStr, bytes) {
    const SAMPLE_SIZE = 256;
    const SAMPLE_COUNT = 4;
    const {
      length
    } = bytes;
    const hash = new _shared_murmurhash3_js__WEBPACK_IMPORTED_MODULE_10__.MurmurHash3_64();
    hash.update(dictStr);
    hash.update(`#${length}`);
    if (length <= SAMPLE_SIZE * SAMPLE_COUNT) {
      hash.update(bytes);
    } else {
      const step = Math.floor((length - SAMPLE_SIZE) / (SAMPLE_COUNT - 1));
      for (let i = 0; i < SAMPLE_COUNT; i++) {
        const start = Math.min(i * step, length - SAMPLE_SIZE);
        hash.update(bytes.subarray(start, start + SAMPLE_SIZE));
      }
    }
    return hash.hexdigest();
  }
  async #collectResourceStream(oldRef, stream, xref, resourceStreamPath) {
    const {
      currentDocument: {
        oldRefMapping,
        resourceStreamPromises
      }
    } = this;
    if (resourceStreamPath.has(oldRef)) {
      return oldRefMapping.getOrPutComputed(oldRef, () => this.newRef);
    }
    const key = oldRef.toString();
    const pending = resourceStreamPromises.get(key);
    if (pending) {
      return pending;
    }
    const childPath = new _primitives_js__WEBPACK_IMPORTED_MODULE_1__.RefSet(resourceStreamPath);
    childPath.put(oldRef);
    const promise = Promise.resolve().then(async () => {
      const collected = await this.#collectDependencies(stream, true, xref, childPath);
      const cycleRef = oldRefMapping.get(oldRef);
      if (cycleRef) {
        this.xref[cycleRef.num] = collected;
        return cycleRef;
      }
      const ref = await this.#dedupResourceStream(collected);
      oldRefMapping.put(oldRef, ref);
      return ref;
    });
    resourceStreamPromises.set(key, promise);
    try {
      return await promise;
    } finally {
      if (resourceStreamPromises.get(key) === promise) {
        resourceStreamPromises.delete(key);
      }
    }
  }
  async #dedupResourceStream(stream) {
    const dictStr = await this.#serializeDict(stream.dict);
    const bytes = this.#rawStreamBytes(stream);
    const key = this.#resourceStreamKey(dictStr, bytes);
    const bucket = this.#resourceStreamCache.getOrInsertComputed(key, _shared_util_js__WEBPACK_IMPORTED_MODULE_3__.makeArr);
    for (const entry of bucket) {
      if (entry.dictStr === dictStr && (0,_shared_util_js__WEBPACK_IMPORTED_MODULE_3__.isArrayEqual)(this.#rawStreamBytes(entry.stream), bytes)) {
        return entry.ref;
      }
    }
    const ref = this.newRef;
    this.xref[ref.num] = stream;
    bucket.push({
      ref,
      dictStr,
      stream
    });
    return ref;
  }
  async #resolveStructKids(rawKids, xref) {
    if (rawKids instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Ref) {
      const fetched = await xref.fetchAsync(rawKids);
      return Array.isArray(fetched) ? fetched : [rawKids];
    }
    return Array.isArray(rawKids) ? rawKids : [rawKids];
  }
  async #cloneStructTreeNode(parentStructRef, node, xref, removedStructElements, dedupIDs, dedupClasses, dedupRoles, visited = new _primitives_js__WEBPACK_IMPORTED_MODULE_1__.RefSet()) {
    const {
      currentDocument: {
        pagesMap,
        oldRefMapping
      }
    } = this;
    const pg = node.getRaw("Pg");
    if (pg instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Ref && !pagesMap.has(pg)) {
      return null;
    }
    const k = node.getRaw("K");
    if (k instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Ref && visited.has(k)) {
      return null;
    }
    const kids = await this.#resolveStructKids(k, xref);
    const newKids = [];
    const structElemIndices = [];
    for (let kid of kids) {
      const kidRef = kid instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Ref ? kid : null;
      if (kidRef) {
        if (visited.has(kidRef)) {
          continue;
        }
        visited.put(kidRef);
        kid = await xref.fetchAsync(kidRef);
      }
      if (typeof kid === "number") {
        newKids.push(kid);
        continue;
      }
      if (!(kid instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Dict)) {
        continue;
      }
      const pgRef = kid.getRaw("Pg");
      if (pgRef instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Ref && !pagesMap.has(pgRef)) {
        continue;
      }
      const type = kid.get("Type");
      if (!type || (0,_primitives_js__WEBPACK_IMPORTED_MODULE_1__.isName)(type, "StructElem")) {
        let setAsSpan = false;
        if (kidRef && removedStructElements.has(kidRef)) {
          if (!(0,_primitives_js__WEBPACK_IMPORTED_MODULE_1__.isName)(kid.get("S"), "Link")) {
            continue;
          }
          setAsSpan = true;
        }
        const newKidRef = await this.#cloneStructTreeNode(kidRef, kid, xref, removedStructElements, dedupIDs, dedupClasses, dedupRoles, visited);
        if (newKidRef) {
          structElemIndices.push(newKids.length);
          newKids.push(newKidRef);
          if (kidRef) {
            oldRefMapping.put(kidRef, newKidRef);
          }
          if (setAsSpan) {
            this.xref[newKidRef.num].setIfName("S", "Span");
          }
        }
        continue;
      }
      if ((0,_primitives_js__WEBPACK_IMPORTED_MODULE_1__.isName)(type, "OBJR")) {
        if (!kidRef) {
          continue;
        }
        const oldObjRef = kid.getRaw("Obj");
        if (oldObjRef instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Ref && !oldRefMapping.get(oldObjRef)) {
          continue;
        }
        const newKidRef = oldRefMapping.get(kidRef) || (await this.#collectDependencies(kidRef, true, xref));
        const newKid = this.xref[newKidRef.num];
        const objRef = newKid.getRaw("Obj");
        if (objRef instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Ref) {
          const obj = this.xref[objRef.num];
          if (obj instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Dict && !obj.has("StructParent") && parentStructRef) {
            const structParent = this.parentTree.size;
            this.parentTree.set(structParent, [oldRefMapping, parentStructRef]);
            obj.set("StructParent", structParent);
          }
        }
        newKids.push(newKidRef);
        continue;
      }
      if ((0,_primitives_js__WEBPACK_IMPORTED_MODULE_1__.isName)(type, "MCR")) {
        const newKid = await this.#collectDependencies(kidRef || kid, true, xref);
        newKids.push(newKid);
        continue;
      }
      if (kidRef) {
        const newKidRef = await this.#collectDependencies(kidRef, true, xref);
        newKids.push(newKidRef);
      }
    }
    if (kids.length !== 0 && newKids.length === 0) {
      return null;
    }
    const newNodeRef = this.newRef;
    const newNode = this.xref[newNodeRef.num] = this.cloneDict(node);
    newNode.delete("ID");
    newNode.delete("C");
    newNode.delete("K");
    newNode.delete("P");
    newNode.delete("S");
    await this.#collectDependencies(newNode, false, xref);
    const classNames = node.get("C");
    if (classNames instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Name) {
      const newClassName = dedupClasses.get(classNames.name);
      newNode.set("C", newClassName ? _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Name.get(newClassName) : classNames);
    } else if (Array.isArray(classNames)) {
      const newClassNames = [];
      for (const className of classNames) {
        if (className instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Name) {
          const newClassName = dedupClasses.get(className.name);
          newClassNames.push(newClassName ? _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Name.get(newClassName) : className);
        }
      }
      newNode.set("C", newClassNames);
    }
    const roleName = node.get("S");
    if (roleName instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Name) {
      const newRoleName = dedupRoles.get(roleName.name);
      newNode.set("S", newRoleName ? _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Name.get(newRoleName) : roleName);
    }
    const id = node.get("ID");
    if (typeof id === "string") {
      const stringId = (0,_string_utils_js__WEBPACK_IMPORTED_MODULE_5__.stringToPDFString)(id, false);
      const newId = dedupIDs.get(stringId);
      newNode.set("ID", newId ? (0,_string_utils_js__WEBPACK_IMPORTED_MODULE_5__.stringToAsciiOrUTF16BE)(newId) : id);
    }
    let attributes = newNode.get("A");
    if (attributes) {
      if (!Array.isArray(attributes)) {
        attributes = [attributes];
      }
      for (let attr of attributes) {
        attr = this.xrefWrapper.fetchIfRef(attr);
        if (!(attr instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Dict)) {
          continue;
        }
        if ((0,_primitives_js__WEBPACK_IMPORTED_MODULE_1__.isName)(attr.get("O"), "Table") && attr.has("Headers")) {
          const headers = this.xrefWrapper.fetchIfRef(attr.getRaw("Headers"));
          if (Array.isArray(headers)) {
            for (let i = 0, ii = headers.length; i < ii; i++) {
              const header = this.xrefWrapper.fetchIfRef(headers[i]);
              if (typeof header !== "string") {
                continue;
              }
              const newId = dedupIDs.get((0,_string_utils_js__WEBPACK_IMPORTED_MODULE_5__.stringToPDFString)(header, false));
              if (newId) {
                headers[i] = newId;
              }
            }
          }
        }
      }
    }
    for (const index of structElemIndices) {
      const structElemRef = newKids[index];
      const structElem = this.xref[structElemRef.num];
      structElem.set("P", newNodeRef);
    }
    if (newKids.length === 1) {
      newNode.set("K", newKids[0]);
    } else if (newKids.length > 1) {
      newNode.set("K", newKids);
    }
    return newNodeRef;
  }
  #getFilteredPageIndices({
    document,
    includePages,
    excludePages
  }) {
    if (!document) {
      return [];
    }
    const compile = list => {
      if (!list?.length) {
        return null;
      }
      const indices = new Set();
      const ranges = [];
      for (const item of list) {
        if (Array.isArray(item)) {
          ranges.push(item);
        } else {
          indices.add(item);
        }
      }
      return {
        indices,
        ranges
      };
    };
    const matches = (index, {
      indices,
      ranges
    }) => indices.has(index) || ranges.some(([start, end]) => index >= start && index <= end);
    const inc = compile(includePages);
    const exc = compile(excludePages);
    const result = [];
    for (let i = 0, ii = document.numPages; i < ii; i++) {
      if (exc && matches(i, exc)) {
        continue;
      }
      if (!inc || matches(i, inc)) {
        result.push(i);
      }
    }
    return result;
  }
  #resolveInsertAfterIndices(pageInfos) {
    const counts = new Array(pageInfos.length);
    const sequence = [];
    const insertAfterList = [];
    for (let i = 0; i < pageInfos.length; i++) {
      const info = pageInfos[i];
      let count;
      if (info.image) {
        count = counts[i] = 1;
      } else if (!info.document) {
        counts[i] = 0;
        continue;
      } else {
        count = counts[i] = this.#getFilteredPageIndices(info).length;
      }
      if (info.pageIndices) {
        continue;
      }
      if (info.insertAfter === undefined) {
        for (let j = 0; j < count; j++) {
          sequence.push(i);
        }
      } else {
        insertAfterList.push({
          i,
          insertAfter: info.insertAfter,
          count
        });
      }
    }
    if (insertAfterList.length === 0) {
      return pageInfos;
    }
    const hasContent = info => !!(info.document || info.image);
    for (let i = 0; i < pageInfos.length; i++) {
      const info = pageInfos[i];
      if (hasContent(info) && info.pageIndices && info.pageIndices.length < counts[i]) {
        throw new Error("extractPages: partial pageIndices cannot be combined with insertAfter entries.");
      }
    }
    insertAfterList.sort((a, b) => a.insertAfter - b.insertAfter || a.i - b.i);
    if (sequence.length === 0 && pageInfos.some(info => hasContent(info) && info.pageIndices)) {
      const updatedPageInfos = pageInfos.slice();
      let maxExistingPos = -1;
      for (const info of pageInfos) {
        if (!hasContent(info) || !info.pageIndices) {
          continue;
        }
        for (const idx of info.pageIndices) {
          if (idx > maxExistingPos) {
            maxExistingPos = idx;
          }
        }
      }
      let offset = 0;
      for (const {
        i,
        insertAfter,
        count
      } of insertAfterList) {
        const threshold = Math.min(Math.max(insertAfter, -1) + offset, maxExistingPos);
        for (let j = 0; j < updatedPageInfos.length; j++) {
          const existingInfo = updatedPageInfos[j];
          if (!hasContent(existingInfo) || !existingInfo.pageIndices || existingInfo.pageIndices.every(idx => idx <= threshold)) {
            continue;
          }
          updatedPageInfos[j] = {
            ...existingInfo,
            pageIndices: existingInfo.pageIndices.map(idx => idx > threshold ? idx + count : idx)
          };
        }
        const pageIndices = [];
        for (let k = 0; k < count; k++) {
          pageIndices.push(threshold + 1 + k);
        }
        const result = {
          ...updatedPageInfos[i],
          pageIndices
        };
        delete result.insertAfter;
        updatedPageInfos[i] = result;
        offset += count;
        maxExistingPos += count;
      }
      return updatedPageInfos;
    }
    let offset = 0;
    for (const {
      i,
      insertAfter,
      count
    } of insertAfterList) {
      const insertPos = Math.max(insertAfter, -1) + 1 + offset;
      sequence.splice(insertPos, 0, ...new Array(count).fill(i));
      offset += count;
    }
    const pageIndicesArr = new Array(pageInfos.length);
    for (let pos = 0; pos < sequence.length; pos++) {
      const infoIdx = sequence[pos];
      (pageIndicesArr[infoIdx] ||= []).push(pos);
    }
    return pageInfos.map((info, i) => {
      if (!hasContent(info) || info.pageIndices) {
        return info;
      }
      const result = {
        ...info,
        pageIndices: pageIndicesArr[i] || []
      };
      delete result.insertAfter;
      return result;
    });
  }
  async extractPages(pageInfos, annotationStorage, primaryDocument, handler, task) {
    this.#primaryDocument = primaryDocument;
    pageInfos = this.#resolveInsertAfterIndices(pageInfos);
    const promises = [];
    let newIndex = 0;
    const reservePageSlot = newPageIndex => {
      if (!Number.isInteger(newPageIndex) || newPageIndex < 0) {
        throw new Error("extractPages: invalid page index.");
      }
      if (this.oldPages[newPageIndex] !== undefined) {
        throw new Error("extractPages: overlapping pageIndices.");
      }
      this.oldPages[newPageIndex] = null;
    };
    const allDocumentData = [];
    if (annotationStorage) {
      this.#newAnnotationsParams = {
        handler,
        task,
        newAnnotationsByPage: (0,_core_utils_js__WEBPACK_IMPORTED_MODULE_0__.getNewAnnotationsMap)(annotationStorage),
        imagesPromises: _annotation_js__WEBPACK_IMPORTED_MODULE_6__.AnnotationFactory.generateImages(annotationStorage.values(), this.xrefWrapper, true)
      };
    }
    const imageEntries = [];
    for (const pageInfo of pageInfos) {
      const {
        document,
        image,
        includePages,
        excludePages,
        pageIndices
      } = pageInfo;
      if (image) {
        if (pageIndices) {
          newIndex = -1;
          if (pageIndices.length > 1) {
            throw new Error("extractPages: too many pageIndices.");
          }
        }
        let newPageIndex;
        if (pageIndices?.length) {
          newPageIndex = pageIndices[0];
        } else if (newIndex !== -1) {
          newPageIndex = newIndex++;
        } else {
          for (newPageIndex = 0; this.oldPages[newPageIndex] !== undefined; newPageIndex++) {}
        }
        reservePageSlot(newPageIndex);
        imageEntries.push({
          image,
          slot: newPageIndex
        });
        continue;
      }
      if (!document) {
        continue;
      }
      if (pageIndices) {
        newIndex = -1;
      }
      const filteredPageIndices = this.#getFilteredPageIndices({
        document,
        includePages,
        excludePages
      });
      if (pageIndices && pageIndices.length > filteredPageIndices.length) {
        throw new Error("extractPages: too many pageIndices.");
      }
      const documentData = new DocumentData(document);
      allDocumentData.push(documentData);
      if (document === primaryDocument) {
        this.#primaryRefMappings.push(documentData.oldRefMapping);
      }
      promises.push(this.#collectDocumentData(documentData));
      let pageIndex = 0;
      for (const i of filteredPageIndices) {
        let newPageIndex;
        if (pageIndices) {
          newPageIndex = pageIndices[pageIndex++];
        }
        if (newPageIndex === undefined) {
          if (newIndex !== -1) {
            newPageIndex = newIndex++;
          } else {
            for (newPageIndex = 0; this.oldPages[newPageIndex] !== undefined; newPageIndex++) {}
          }
        }
        reservePageSlot(newPageIndex);
        promises.push(document.getPage(i).then(page => {
          this.oldPages[newPageIndex] = new PageData(page, documentData);
        }));
      }
    }
    await Promise.all(promises);
    if (this.oldPages.length === 0) {
      throw new Error("extractPages: nothing to extract.");
    }
    const copyCounts = new Map();
    const documents = new Set();
    for (let i = 0, ii = this.oldPages.length; i < ii; i++) {
      const pageData = this.oldPages[i];
      if (pageData === undefined) {
        throw new Error("extractPages: sparse pageIndices.");
      }
      if (pageData) {
        const {
          page
        } = pageData;
        const copyLevel = copyCounts.get(page) ?? 0;
        copyCounts.set(page, copyLevel + 1);
        pageData.copyLevel = copyLevel;
        documents.add(pageData.documentData.document);
      }
    }
    this.isSingleFile = documents.size === 1;
    promises.length = 0;
    this.#collectValidDestinations(allDocumentData);
    this.#collectOutlineDestinations(allDocumentData);
    this.#collectPageLabels();
    for (const page of this.oldPages) {
      if (page) {
        promises.push(this.#postCollectPageData(page));
      }
    }
    await Promise.all(promises);
    this.#findDuplicateNamedDestinations();
    this.#setPostponedRefCopies(allDocumentData);
    const imageSlots = new Map();
    for (const entry of imageEntries) {
      imageSlots.set(entry.slot, entry);
    }
    const modalPageSize = imageSlots.size > 0 ? this.#modalPageSize() : null;
    for (let i = 0, ii = this.oldPages.length; i < ii; i++) {
      const imageEntry = imageSlots.get(i);
      if (imageEntry) {
        this.newPages[i] = await this.#makeImagePage(imageEntry.image, modalPageSize);
      } else {
        this.newPages[i] = await this.#makePageCopy(i, null);
      }
    }
    this.#fixPostponedRefCopies(allDocumentData);
    await this.#mergeStructTrees(allDocumentData);
    await this.#mergeAcroForms(allDocumentData);
    this.#buildOutline(allDocumentData);
    await this.#collectEmbeddedFiles(allDocumentData);
    return this.writePDF();
  }
  async #collectDocumentData(documentData) {
    const {
      document: {
        pdfManager,
        xref
      }
    } = documentData;
    await Promise.all([pdfManager.ensureCatalog("destinations").then(destinations => documentData.destinations = destinations), pdfManager.ensureCatalog("rawPageLabels").then(pageLabels => documentData.pageLabels = pageLabels), pdfManager.ensureCatalog("structTreeRoot").then(structTreeRoot => documentData.structTreeRoot = structTreeRoot), pdfManager.ensureCatalog("acroForm").then(acroForm => documentData.acroForm = acroForm), pdfManager.ensureCatalog("documentOutlineForEditor").then(outline => documentData.outline = outline), pdfManager.ensureCatalog("rawEmbeddedFiles").then(ef => documentData.embeddedFiles = ef)]);
    const structTreeRoot = documentData.structTreeRoot;
    if (structTreeRoot) {
      const rootDict = structTreeRoot.dict;
      const parentTree = rootDict.get("ParentTree");
      if (parentTree) {
        const numberTree = new _name_number_tree_js__WEBPACK_IMPORTED_MODULE_4__.NumberTree(parentTree, xref);
        documentData.parentTree = numberTree.getAll(true);
      }
      const idTree = rootDict.get("IDTree");
      if (idTree) {
        const nameTree = new _name_number_tree_js__WEBPACK_IMPORTED_MODULE_4__.NameTree(idTree, xref);
        documentData.idTree = nameTree.getAll(true);
      }
      documentData.roleMap = rootDict.get("RoleMap") || null;
      documentData.classMap = rootDict.get("ClassMap") || null;
      let namespaces = rootDict.get("Namespaces") || null;
      if (namespaces && !Array.isArray(namespaces)) {
        namespaces = [namespaces];
      }
      documentData.namespaces = namespaces;
      documentData.structTreeAF = rootDict.get("AF") || null;
      documentData.structTreePronunciationLexicon = rootDict.get("PronunciationLexicon") || null;
    }
  }
  async #postCollectPageData(pageData) {
    const {
      page: {
        xref,
        annotations
      },
      documentData: {
        pagesMap,
        destinations,
        usedNamedDestinations,
        fieldToParent
      }
    } = pageData;
    if (!annotations) {
      return;
    }
    const promises = [];
    let newAnnotations = [];
    let newIndex = 0;
    let {
      hasSignatureAnnotations
    } = pageData.documentData;
    for (const annotationRef of annotations) {
      const newAnnotationIndex = newIndex++;
      promises.push(xref.fetchIfRefAsync(annotationRef).then(async annotationDict => {
        if (!(0,_primitives_js__WEBPACK_IMPORTED_MODULE_1__.isName)(annotationDict.get("Subtype"), "Link")) {
          if ((0,_primitives_js__WEBPACK_IMPORTED_MODULE_1__.isName)(annotationDict.get("Subtype"), "Widget")) {
            hasSignatureAnnotations ||= (0,_primitives_js__WEBPACK_IMPORTED_MODULE_1__.isName)((0,_core_utils_js__WEBPACK_IMPORTED_MODULE_0__.getInheritableProperty)({
              dict: annotationDict,
              key: "FT"
            }), "Sig");
            const parentRef = annotationDict.getRaw("Parent") || null;
            fieldToParent.put(annotationRef, parentRef);
          }
          newAnnotations[newAnnotationIndex] = annotationRef;
          return;
        }
        const action = annotationDict.get("A");
        if (action instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Dict && !(0,_primitives_js__WEBPACK_IMPORTED_MODULE_1__.isName)(action.get("S"), "GoTo")) {
          newAnnotations[newAnnotationIndex] = annotationRef;
          return;
        }
        const dest = action instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Dict ? action.get("D") : annotationDict.get("Dest");
        if (!dest || Array.isArray(dest) && (!(dest[0] instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Ref) || pagesMap.has(dest[0]))) {
          newAnnotations[newAnnotationIndex] = annotationRef;
        } else if (dest instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Name || typeof dest === "string") {
          const destString = (0,_string_utils_js__WEBPACK_IMPORTED_MODULE_5__.stringToPDFString)(dest instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Name ? dest.name : dest, true);
          if (destinations.has(destString)) {
            newAnnotations[newAnnotationIndex] = annotationRef;
            usedNamedDestinations.add(destString);
          }
        }
      }));
    }
    await Promise.all(promises);
    newAnnotations = newAnnotations.filter(Boolean);
    pageData.annotations = newAnnotations.length > 0 ? newAnnotations : null;
    pageData.documentData.hasSignatureAnnotations ||= hasSignatureAnnotations;
  }
  #setPostponedRefCopies(allDocumentData) {
    for (const {
      postponedRefCopies,
      pagesMap
    } of allDocumentData) {
      for (const oldPageRef of pagesMap.keys()) {
        postponedRefCopies.put(oldPageRef, []);
      }
    }
  }
  #fixPostponedRefCopies(allDocumentData) {
    for (const {
      postponedRefCopies,
      oldRefMapping
    } of allDocumentData) {
      for (const [oldRef, actions] of postponedRefCopies.items()) {
        const newRef = oldRefMapping.get(oldRef);
        for (const action of actions) {
          action(newRef);
        }
      }
      postponedRefCopies.clear();
    }
  }
  #visitObject(obj, callback, visited = new _primitives_js__WEBPACK_IMPORTED_MODULE_1__.RefSet()) {
    if (obj instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Ref) {
      if (!visited.has(obj)) {
        visited.put(obj);
        this.#visitObject(this.xref[obj.num], callback, visited);
      }
      return;
    }
    if (Array.isArray(obj)) {
      for (const item of obj) {
        this.#visitObject(item, callback, visited);
      }
      return;
    }
    let dict;
    if (obj instanceof _base_stream_js__WEBPACK_IMPORTED_MODULE_7__.BaseStream) {
      ({
        dict
      } = obj);
    } else if (obj instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Dict) {
      dict = obj;
    }
    if (dict) {
      callback(dict);
      for (const value of dict.getRawValues()) {
        this.#visitObject(value, callback, visited);
      }
    }
  }
  async #mergeStructTrees(allDocumentData) {
    let newStructParentId = 0;
    const {
      parentTree: newParentTree
    } = this;
    for (let i = 0, ii = this.newPages.length; i < ii; i++) {
      if (!this.oldPages[i]) {
        continue;
      }
      const {
        documentData: {
          parentTree,
          oldRefMapping,
          oldStructParentMapping,
          usedStructParents,
          document: {
            xref
          }
        }
      } = this.oldPages[i];
      if (!parentTree) {
        continue;
      }
      const pageRef = this.newPages[i];
      const pageDict = this.xref[pageRef.num];
      const visited = new _primitives_js__WEBPACK_IMPORTED_MODULE_1__.RefSet();
      visited.put(pageRef);
      this.#visitObject(pageDict, dict => {
        const structParent = dict.get("StructParent") ?? dict.get("StructParents");
        if (typeof structParent !== "number") {
          return;
        }
        usedStructParents.add(structParent);
        let parent = parentTree.get(structParent);
        const parentRef = parent instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Ref ? parent : null;
        if (parentRef) {
          const array = xref.fetch(parentRef);
          if (Array.isArray(array)) {
            parent = array;
          }
        }
        if (Array.isArray(parent) && parent.every(ref => ref === null)) {
          parent = null;
        }
        if (!parent) {
          if (dict.has("StructParent")) {
            dict.delete("StructParent");
          } else {
            dict.delete("StructParents");
          }
          return;
        }
        let newStructParent = oldStructParentMapping.get(structParent);
        if (newStructParent === undefined) {
          newStructParent = newStructParentId++;
          oldStructParentMapping.set(structParent, newStructParent);
          newParentTree.set(newStructParent, [oldRefMapping, parent]);
        }
        if (dict.has("StructParent")) {
          dict.set("StructParent", newStructParent);
        } else {
          dict.set("StructParents", newStructParent);
        }
      }, visited);
    }
    const {
      structTreeKids,
      idTree: newIdTree,
      classMap: newClassMap,
      roleMap: newRoleMap,
      namespaces: newNamespaces,
      structTreeAF: newStructTreeAF,
      structTreePronunciationLexicon: newStructTreePronunciationLexicon
    } = this;
    for (const documentData of allDocumentData) {
      const {
        document: {
          xref
        },
        oldRefMapping,
        parentTree,
        usedStructParents,
        structTreeRoot,
        idTree,
        classMap,
        roleMap,
        namespaces,
        structTreeAF,
        structTreePronunciationLexicon
      } = documentData;
      if (!structTreeRoot) {
        continue;
      }
      this.currentDocument = documentData;
      const removedStructElements = new _primitives_js__WEBPACK_IMPORTED_MODULE_1__.RefSet();
      for (const [key, value] of parentTree || []) {
        if (!usedStructParents.has(key) && value instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Ref) {
          removedStructElements.put(value);
        }
      }
      const dedupIDs = new Map();
      for (const [id, nodeRef] of idTree || []) {
        let _id = id;
        if (newIdTree.has(id)) {
          for (let i = 1;; i++) {
            const newId = `${id}_${i}`;
            if (!newIdTree.has(newId)) {
              dedupIDs.set(id, newId);
              _id = newId;
              break;
            }
          }
        }
        newIdTree.set(_id, nodeRef);
      }
      const dedupClasses = new Map();
      if (classMap?.size > 0) {
        for (let [className, classDict] of classMap) {
          classDict = await this.#collectDependencies(classDict, true, xref);
          if (newClassMap.has(className)) {
            for (let i = 1;; i++) {
              const newClassName = `${className}_${i}`;
              if (!newClassMap.has(newClassName)) {
                dedupClasses.set(className, newClassName);
                className = newClassName;
                break;
              }
            }
          }
          newClassMap.set(className, classDict);
        }
      }
      const dedupRoles = new Map();
      if (roleMap?.size > 0) {
        for (const [roleName, mappedName] of roleMap) {
          const newMappedName = newRoleMap.get(roleName);
          if (!newMappedName) {
            newRoleMap.set(roleName, mappedName);
            continue;
          }
          if (newMappedName === mappedName) {
            continue;
          }
          for (let i = 1;; i++) {
            const newRoleName = `${roleName}_${i}`;
            if (!newRoleMap.has(newRoleName)) {
              dedupRoles.set(roleName, newRoleName);
              newRoleMap.set(newRoleName, mappedName);
              break;
            }
          }
        }
      }
      if (namespaces?.length > 0) {
        for (const namespaceRef of namespaces) {
          const namespace = await xref.fetchIfRefAsync(namespaceRef);
          let ns = namespace.get("NS");
          if (!ns || newNamespaces.has(ns)) {
            continue;
          }
          ns = (0,_string_utils_js__WEBPACK_IMPORTED_MODULE_5__.stringToPDFString)(ns, false);
          const newNamespace = await this.#collectDependencies(namespace, true, xref);
          newNamespaces.set(ns, newNamespace);
        }
      }
      if (structTreeAF) {
        for (const afRef of structTreeAF) {
          newStructTreeAF.push(await this.#collectDependencies(afRef, true, xref));
        }
      }
      if (structTreePronunciationLexicon) {
        for (const lexiconRef of structTreePronunciationLexicon) {
          newStructTreePronunciationLexicon.push(await this.#collectDependencies(lexiconRef, true, xref));
        }
      }
      const rawKids = structTreeRoot.dict.getRaw("K");
      if (!rawKids) {
        continue;
      }
      const kids = await this.#resolveStructKids(rawKids, xref);
      for (let kid of kids) {
        const kidRef = kid instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Ref ? kid : null;
        kid = await xref.fetchIfRefAsync(kid);
        if (!(kid instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Dict)) {
          continue;
        }
        let setAsSpan = false;
        if (kidRef && removedStructElements.has(kidRef)) {
          if (!(0,_primitives_js__WEBPACK_IMPORTED_MODULE_1__.isName)(kid.get("S"), "Link")) {
            continue;
          }
          setAsSpan = true;
        }
        const newKidRef = await this.#cloneStructTreeNode(kidRef, kid, xref, removedStructElements, dedupIDs, dedupClasses, dedupRoles);
        if (newKidRef) {
          structTreeKids.push(newKidRef);
          if (kidRef) {
            oldRefMapping.put(kidRef, newKidRef);
          }
          if (setAsSpan) {
            this.xref[newKidRef.num].setIfName("S", "Span");
          }
        }
      }
      for (const [id, nodeRef] of idTree || []) {
        const newNodeRef = nodeRef instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Ref && oldRefMapping.get(nodeRef);
        const newId = dedupIDs.get(id) || id;
        if (newNodeRef) {
          newIdTree.set(newId, newNodeRef);
        } else {
          newIdTree.delete(newId);
        }
      }
    }
    for (const [key, [oldRefMapping, parent]] of newParentTree) {
      if (!parent) {
        newParentTree.delete(key);
        continue;
      }
      if (!Array.isArray(parent)) {
        const newParent = oldRefMapping.get(parent);
        if (newParent === undefined) {
          newParentTree.delete(key);
        } else {
          newParentTree.set(key, newParent);
        }
        continue;
      }
      const newParents = parent.map(ref => ref instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Ref && oldRefMapping.get(ref) || null);
      if (newParents.length === 0 || newParents.every(ref => ref === null)) {
        newParentTree.delete(key);
        continue;
      }
      newParentTree.set(key, newParents);
    }
    this.currentDocument = null;
  }
  #collectValidDestinations(allDocumentData) {
    for (const documentData of allDocumentData) {
      if (!documentData.destinations) {
        continue;
      }
      const {
        destinations,
        pagesMap
      } = documentData;
      const newDestinations = documentData.destinations = new Map();
      for (const [key, dest] of destinations) {
        const pageRef = dest[0];
        const pageData = pageRef instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Ref && pagesMap.get(pageRef);
        if (!pageData) {
          continue;
        }
        (pageData.pointingNamedDestinations ||= new Set()).add(key);
        newDestinations.set(key, dest);
      }
    }
  }
  #findDuplicateNamedDestinations() {
    const {
      namedDestinations
    } = this;
    const getUniqueDestinationName = name => {
      if (!namedDestinations.has(name)) {
        return name;
      }
      for (let i = 1;; i++) {
        const dedupedName = `${name}_${i}`;
        if (!namedDestinations.has(dedupedName)) {
          return dedupedName;
        }
      }
    };
    for (let i = 0, ii = this.oldPages.length; i < ii; i++) {
      const page = this.oldPages[i];
      if (!page) {
        continue;
      }
      const {
        documentData: {
          destinations,
          dedupNamedDestinations,
          usedNamedDestinations
        }
      } = page;
      let {
        pointingNamedDestinations
      } = page;
      if (!pointingNamedDestinations) {
        continue;
      }
      page.pointingNamedDestinations = pointingNamedDestinations = pointingNamedDestinations.intersection(usedNamedDestinations);
      for (const pointingDest of pointingNamedDestinations) {
        if (!usedNamedDestinations.has(pointingDest)) {
          continue;
        }
        const dest = destinations.get(pointingDest).slice();
        if (!namedDestinations.has(pointingDest)) {
          namedDestinations.set(pointingDest, dest);
          continue;
        }
        const newName = getUniqueDestinationName(`${pointingDest}_p${i + 1}`);
        dedupNamedDestinations.set(pointingDest, newName);
        namedDestinations.set(newName, dest);
      }
    }
  }
  #fixNamedDestinations(annotations, dedupNamedDestinations) {
    if (dedupNamedDestinations.size === 0) {
      return;
    }
    const fixDestination = (dict, key, dest) => {
      if (typeof dest === "string") {
        dict.set(key, dedupNamedDestinations.get((0,_string_utils_js__WEBPACK_IMPORTED_MODULE_5__.stringToPDFString)(dest, true)) || dest);
      }
    };
    for (const annotRef of annotations) {
      const annotDict = this.xref[annotRef.num];
      if (!(0,_primitives_js__WEBPACK_IMPORTED_MODULE_1__.isName)(annotDict.get("Subtype"), "Link")) {
        continue;
      }
      const action = annotDict.get("A");
      if (action instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Dict && action.has("D")) {
        const dest = action.get("D");
        fixDestination(action, "D", dest);
        continue;
      }
      const dest = annotDict.get("Dest");
      fixDestination(annotDict, "Dest", dest);
    }
  }
  #collectOutlineDestinations(allDocumentData) {
    const collect = (items, destinations, usedNamedDestinations) => {
      for (const item of items) {
        if (typeof item.dest === "string" && destinations?.has(item.dest)) {
          usedNamedDestinations.add(item.dest);
        }
        if (item.items.length > 0) {
          collect(item.items, destinations, usedNamedDestinations);
        }
      }
    };
    for (const documentData of allDocumentData) {
      const {
        outline,
        destinations,
        usedNamedDestinations
      } = documentData;
      if (outline?.length) {
        collect(outline, destinations, usedNamedDestinations);
      }
    }
  }
  #isValidOutlineDest(item, documentData) {
    const {
      dest,
      action,
      url,
      unsafeUrl,
      attachment,
      setOCGState
    } = item;
    if (action || url || unsafeUrl || attachment || setOCGState) {
      return true;
    }
    if (!dest) {
      return false;
    }
    if (typeof dest === "string") {
      const name = documentData.dedupNamedDestinations.get(dest) || dest;
      return this.namedDestinations.has(name);
    }
    return Array.isArray(dest) && dest[0] instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Ref && !!documentData.oldRefMapping.get(dest[0]);
  }
  #filterOutlineItems(items, documentData) {
    const result = [];
    for (const item of items) {
      const filteredChildren = this.#filterOutlineItems(item.items, documentData);
      const hasValidOwnDest = this.#isValidOutlineDest(item, documentData);
      if (hasValidOwnDest || filteredChildren.length > 0) {
        result.push({
          ...item,
          dest: hasValidOwnDest ? item.dest : null,
          rawDict: hasValidOwnDest ? item.rawDict : null,
          items: filteredChildren,
          _documentData: documentData
        });
      }
    }
    return result;
  }
  #buildOutline(allDocumentData) {
    const outlineItems = [];
    for (const documentData of allDocumentData) {
      const {
        outline
      } = documentData;
      if (!outline?.length) {
        continue;
      }
      outlineItems.push(...this.#filterOutlineItems(outline, documentData));
    }
    this.outlineItems = outlineItems.length > 0 ? outlineItems : null;
  }
  async #setOutlineItemDest(itemDict, item) {
    const {
      dest,
      rawDict
    } = item;
    const documentData = item._documentData;
    if (dest) {
      if (typeof dest === "string") {
        const name = documentData.dedupNamedDestinations.get(dest) || dest;
        itemDict.set("Dest", (0,_string_utils_js__WEBPACK_IMPORTED_MODULE_5__.stringToAsciiOrUTF16BE)(name));
      } else if (Array.isArray(dest)) {
        const newDest = dest.slice();
        if (newDest[0] instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Ref) {
          newDest[0] = documentData.oldRefMapping.get(newDest[0]) || newDest[0];
        }
        itemDict.set("Dest", newDest);
      }
      return;
    }
    const actionDict = rawDict?.get("A");
    if (actionDict instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Dict) {
      this.currentDocument = documentData;
      const actionRef = await this.#cloneObject(actionDict, documentData.document.xref);
      this.currentDocument = null;
      itemDict.set("A", actionRef);
    }
  }
  async #makeOutline() {
    const {
      outlineItems
    } = this;
    if (!outlineItems?.length) {
      return;
    }
    const [outlineRootRef, outlineRootDict] = this.newDict;
    outlineRootDict.setIfName("Type", "Outlines");
    const assignRefs = items => {
      for (const item of items) {
        [item._ref] = this.newDict;
        if (item.items.length > 0) {
          assignRefs(item.items);
        }
      }
    };
    assignRefs(outlineItems);
    const fillItems = async (items, parentRef) => {
      let totalCount = 0;
      for (let i = 0; i < items.length; i++) {
        const item = items[i];
        const dict = this.xref[item._ref.num];
        dict.set("Title", (0,_string_utils_js__WEBPACK_IMPORTED_MODULE_5__.stringToAsciiOrUTF16BE)(item.title));
        dict.set("Parent", parentRef);
        if (i > 0) {
          dict.set("Prev", items[i - 1]._ref);
        }
        if (i < items.length - 1) {
          dict.set("Next", items[i + 1]._ref);
        }
        if (item.items.length > 0) {
          dict.set("First", item.items[0]._ref);
          dict.set("Last", item.items.at(-1)._ref);
          const childCount = await fillItems(item.items, item._ref);
          if (item.count !== undefined) {
            dict.set("Count", item.count < 0 ? -childCount : childCount);
          }
          totalCount += item.count !== undefined && item.count < 0 ? 1 : childCount + 1;
        } else {
          totalCount += 1;
        }
        await this.#setOutlineItemDest(dict, item);
        const flags = (item.bold ? 2 : 0) | (item.italic ? 1 : 0);
        if (flags !== 0) {
          dict.set("F", flags);
        }
        if (item.color && (item.color[0] !== 0 || item.color[1] !== 0 || item.color[2] !== 0)) {
          dict.set("C", [item.color[0] / 255, item.color[1] / 255, item.color[2] / 255]);
        }
      }
      return totalCount;
    };
    const totalCount = await fillItems(outlineItems, outlineRootRef);
    outlineRootDict.set("First", outlineItems[0]._ref);
    outlineRootDict.set("Last", outlineItems.at(-1)._ref);
    outlineRootDict.set("Count", totalCount);
    this.rootDict.set("Outlines", outlineRootRef);
  }
  async #mergeAcroForms(allDocumentData) {
    this.#setAcroFormDefaultBasicValues(allDocumentData);
    this.#setAcroFormDefaultAppearance(allDocumentData);
    this.#setAcroFormQ(allDocumentData);
    await this.#setAcroFormDefaultResources(allDocumentData);
    const newFields = this.fields;
    for (const documentData of allDocumentData) {
      let fields = documentData.acroForm?.get("Fields") || null;
      if (!fields && documentData.fieldToParent.size > 0) {
        fields = this.#fixFields(documentData.fieldToParent, documentData.document.xref);
      }
      if (Array.isArray(fields) && fields.length > 0) {
        this.currentDocument = documentData;
        await this.#cloneFields(newFields, fields);
        this.currentDocument = null;
      }
    }
    this.#setAcroFormCalculationOrder(allDocumentData);
  }
  #setAcroFormQ(allDocumentData) {
    let firstQ = 0;
    let firstDocData = null;
    for (const documentData of allDocumentData) {
      const q = documentData.acroForm?.get("Q");
      if (typeof q !== "number" || q === 0) {
        continue;
      }
      if (firstDocData?.acroFormQ > 0) {
        documentData.acroFormQ = q;
        continue;
      }
      if (firstQ === 0) {
        firstQ = q;
        firstDocData = documentData;
        continue;
      }
      if (q === firstQ) {
        continue;
      }
      firstDocData.acroFormQ ||= firstQ;
      documentData.acroFormQ = q;
      firstQ = 0;
    }
    if (firstQ > 0) {
      this.acroFormQ = firstQ;
    }
  }
  #setAcroFormDefaultBasicValues(allDocumentData) {
    let sigFlags = 0;
    let needAppearances = false;
    for (const documentData of allDocumentData) {
      if (!documentData.acroForm) {
        continue;
      }
      const sf = documentData.acroForm.get("SigFlags");
      if (typeof sf === "number" && documentData.hasSignatureAnnotations) {
        sigFlags |= sf;
      }
      if (documentData.acroForm.get("NeedAppearances") === true) {
        needAppearances = true;
      }
    }
    this.acroFormSigFlags = sigFlags;
    this.acroFormNeedAppearances = needAppearances;
  }
  #setAcroFormCalculationOrder(allDocumentData) {
    const calculationOrder = [];
    for (const documentData of allDocumentData) {
      const co = documentData.acroForm?.get("CO") || null;
      if (!Array.isArray(co)) {
        continue;
      }
      const {
        oldRefMapping
      } = documentData;
      for (const coRef of co) {
        const newCoRef = coRef instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Ref && oldRefMapping.get(coRef);
        if (newCoRef) {
          calculationOrder.push(newCoRef);
        }
      }
    }
    this.acroFormCalculationOrder = calculationOrder.length > 0 ? calculationOrder : null;
  }
  #setAcroFormDefaultAppearance(allDocumentData) {
    let firstDA = null;
    let firstDocData = null;
    for (const documentData of allDocumentData) {
      const da = documentData.acroForm?.get("DA") || null;
      if (!da || typeof da !== "string") {
        continue;
      }
      if (firstDocData?.acroFormDefaultAppearance) {
        documentData.acroFormDefaultAppearance = da;
        continue;
      }
      if (!firstDA) {
        firstDA = da;
        firstDocData = documentData;
        continue;
      }
      if (da === firstDA) {
        continue;
      }
      firstDocData.acroFormDefaultAppearance ||= firstDA;
      documentData.acroFormDefaultAppearance = da;
      firstDA = null;
    }
    if (firstDA) {
      this.acroFormDefaultAppearance = firstDA;
    }
  }
  async #setAcroFormDefaultResources(allDocumentData) {
    let firstDR = null;
    let firstDRRef = null;
    let firstDocData = null;
    for (const documentData of allDocumentData) {
      const dr = documentData.acroForm?.get("DR") || null;
      if (!dr || !(dr instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Dict)) {
        continue;
      }
      if (firstDocData?.acroFormDefaultResources) {
        documentData.acroFormDefaultResources = dr;
        continue;
      }
      if (!firstDR) {
        firstDR = dr;
        firstDRRef = documentData.acroForm.getRaw("DR");
        firstDocData = documentData;
        continue;
      }
      if ((0,_core_utils_js__WEBPACK_IMPORTED_MODULE_0__.deepCompare)(firstDR, dr)) {
        continue;
      }
      firstDocData.acroFormDefaultResources ||= firstDR;
      documentData.acroFormDefaultResources = dr;
      firstDR = null;
      firstDRRef = null;
    }
    if (firstDR) {
      this.currentDocument = firstDocData;
      this.acroFormDefaultResources = await this.#collectDependencies(firstDRRef, true, firstDocData.document.xref);
      this.currentDocument = null;
    }
  }
  #fixFields(fieldToParent, xref) {
    const newFields = [];
    const processed = new _primitives_js__WEBPACK_IMPORTED_MODULE_1__.RefSet();
    for (const [fieldRef, parentRef] of fieldToParent.items()) {
      if (!parentRef) {
        newFields.push(fieldRef);
        continue;
      }
      let parent = parentRef;
      let lastNonNullParent = parentRef;
      const visited = new _primitives_js__WEBPACK_IMPORTED_MODULE_1__.RefSet();
      while (true) {
        if (parent instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Ref) {
          if (visited.has(parent)) {
            break;
          }
          visited.put(parent);
        }
        const parentDict = xref.fetchIfRef(parent);
        if (!(parentDict instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Dict)) {
          break;
        }
        parent = parentDict.getRaw("Parent") || null;
        if (!parent) {
          break;
        }
        lastNonNullParent = parent;
      }
      if (lastNonNullParent instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Ref && !processed.has(lastNonNullParent)) {
        newFields.push(lastNonNullParent);
        processed.put(lastNonNullParent);
      }
    }
    return newFields;
  }
  async #cloneFields(newFields, fields) {
    const processed = new _primitives_js__WEBPACK_IMPORTED_MODULE_1__.RefSet();
    const stack = [{
      kids: fields,
      newKids: newFields,
      pos: 0,
      oldParentRef: null,
      parentRef: null,
      parent: null
    }];
    const {
      document: {
        xref
      },
      oldRefMapping,
      fieldToParent,
      acroFormDefaultAppearance,
      acroFormDefaultResources,
      acroFormQ
    } = this.currentDocument;
    const daToFix = [];
    const drToFix = [];
    while (stack.length > 0) {
      const data = stack.at(-1);
      const {
        kids,
        newKids,
        parent,
        pos
      } = data;
      if (pos === kids.length) {
        stack.pop();
        if (newKids.length === 0 || !parent) {
          continue;
        }
        const parentDict = this.xref[data.parentRef.num] = this.cloneDict(parent);
        parentDict.delete("Parent");
        parentDict.delete("Kids");
        await this.#collectDependencies(parentDict, false, xref);
        parentDict.set("Kids", newKids);
        if (stack.length > 0) {
          const lastData = stack.at(-1);
          if (!lastData.parentRef && lastData.oldParentRef) {
            const parentRef = lastData.parentRef = this.newRef;
            parentDict.set("Parent", parentRef);
            oldRefMapping.put(lastData.oldParentRef, parentRef);
          }
          lastData.newKids.push(data.parentRef);
        }
        continue;
      }
      const oldKidRef = kids[data.pos++];
      if (!(oldKidRef instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Ref) || processed.has(oldKidRef)) {
        continue;
      }
      processed.put(oldKidRef);
      const kid = xref.fetchIfRef(oldKidRef);
      if (!(kid instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Dict)) {
        continue;
      }
      if (kid.has("Kids")) {
        const kidsArray = kid.get("Kids");
        if (!Array.isArray(kidsArray)) {
          continue;
        }
        stack.push({
          kids: kidsArray,
          newKids: [],
          pos: 0,
          oldParentRef: oldKidRef,
          parentRef: null,
          parent: kid
        });
        continue;
      }
      if (!fieldToParent.has(oldKidRef)) {
        continue;
      }
      const newRef = oldRefMapping.get(oldKidRef);
      if (!newRef) {
        continue;
      }
      newKids.push(newRef);
      if (!data.parentRef && data.oldParentRef) {
        data.parentRef = this.newRef;
        oldRefMapping.put(data.oldParentRef, data.parentRef);
      }
      const newKid = this.xref[newRef.num];
      if (data.parentRef) {
        newKid.set("Parent", data.parentRef);
      }
      if (acroFormDefaultAppearance && !newKid.has("DA")) {
        daToFix.push(newKid);
      }
      if (acroFormDefaultResources && !newKid.has("Kids") && newKid.get("AP") instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Dict) {
        drToFix.push(newKid);
      }
      if (acroFormQ && !newKid.has("Q")) {
        newKid.set("Q", acroFormQ);
      }
    }
    for (const field of daToFix) {
      const fieldType = (0,_core_utils_js__WEBPACK_IMPORTED_MODULE_0__.getInheritableProperty)({
        dict: field,
        key: "FT"
      });
      if (!(0,_primitives_js__WEBPACK_IMPORTED_MODULE_1__.isName)(fieldType, "Tx")) {
        continue;
      }
      const da = (0,_core_utils_js__WEBPACK_IMPORTED_MODULE_0__.getInheritableProperty)({
        dict: field,
        key: "DA"
      });
      if (!da) {
        field.set("DA", acroFormDefaultAppearance);
      }
    }
    const resourcesValuesCache = new Map();
    const fixAppearanceResources = async stream => {
      let resources = stream.dict.getRaw("Resources");
      resources &&= this.xrefWrapper.fetchIfRef(resources);
      if (!(resources instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Dict)) {
        const newResourcesRef = await resourcesValuesCache.getOrInsertComputed(acroFormDefaultResources, () => this.#cloneObject(acroFormDefaultResources, xref));
        stream.dict.set("Resources", newResourcesRef);
        return;
      }
      for (const [resKey, resValue] of acroFormDefaultResources.getRawEntries()) {
        if (resources.has(resKey)) {
          continue;
        }
        let newResValue = resValue;
        if (resValue instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Ref) {
          newResValue = await this.#collectDependencies(resValue, true, xref);
        } else if (resValue instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Dict || resValue instanceof _base_stream_js__WEBPACK_IMPORTED_MODULE_7__.BaseStream || Array.isArray(resValue)) {
          newResValue = await resourcesValuesCache.getOrInsertComputed(resValue, () => this.#cloneObject(resValue, xref));
        }
        resources.set(resKey, newResValue);
      }
    };
    for (const field of drToFix) {
      const ap = field.get("AP");
      for (const [, value] of ap) {
        if (value instanceof _base_stream_js__WEBPACK_IMPORTED_MODULE_7__.BaseStream) {
          await fixAppearanceResources(value);
        } else if (value instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Dict) {
          for (const [, stream] of value) {
            if (stream instanceof _base_stream_js__WEBPACK_IMPORTED_MODULE_7__.BaseStream) {
              await fixAppearanceResources(stream);
            }
          }
        }
      }
    }
  }
  async #collectPageLabels() {
    if (!this.isSingleFile) {
      return;
    }
    const firstRealPage = this.oldPages.find(p => !!p);
    if (!firstRealPage) {
      return;
    }
    const {
      documentData: {
        document,
        pageLabels
      }
    } = firstRealPage;
    if (!pageLabels) {
      return;
    }
    const numPages = document.numPages;
    const labelsByPageIndex = new Map();
    const oldPageIndices = new Set(this.oldPages.filter(Boolean).map(({
      page: {
        pageIndex
      }
    }) => pageIndex));
    let currentLabel = null;
    let stFirstIndex = -1;
    for (let i = 0; i < numPages; i++) {
      const newLabel = pageLabels.get(i);
      if (newLabel) {
        currentLabel = newLabel;
        stFirstIndex = currentLabel.has("St") ? i : -1;
      }
      if (!oldPageIndices.has(i)) {
        continue;
      }
      if (stFirstIndex !== -1) {
        const st = currentLabel.get("St");
        currentLabel = this.cloneDict(currentLabel);
        currentLabel.set("St", st + (i - stFirstIndex));
        stFirstIndex = -1;
      }
      labelsByPageIndex.set(i, currentLabel);
    }
    const defaultLabel = index => {
      const label = new _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Dict();
      label.setIfName("S", "D");
      label.set("St", index + 1);
      return label;
    };
    currentLabel = null;
    const newPageLabels = this.pageLabels = [];
    for (let i = 0, ii = this.oldPages.length; i < ii; i++) {
      const pageData = this.oldPages[i];
      const label = pageData ? labelsByPageIndex.get(pageData.page.pageIndex) || defaultLabel(i) : defaultLabel(i);
      if (label === currentLabel) {
        continue;
      }
      currentLabel = label;
      newPageLabels.push([i, currentLabel]);
    }
  }
  async #makePageCopy(pageIndex) {
    const {
      page,
      documentData,
      annotations,
      pointingNamedDestinations,
      copyLevel
    } = this.oldPages[pageIndex];
    this.currentDocument = documentData;
    const {
      dedupNamedDestinations,
      oldRefMapping
    } = documentData;
    const {
      xref,
      rotate,
      mediaBox,
      resources,
      ref: oldPageRef
    } = page;
    const pageRef = this.newRef;
    const pageDict = this.xref[pageRef.num] = this.cloneDict(page.pageDict);
    oldRefMapping.put(oldPageRef, pageRef);
    if (pointingNamedDestinations) {
      for (const pointingDest of pointingNamedDestinations) {
        const name = dedupNamedDestinations.get(pointingDest) || pointingDest;
        const dest = this.namedDestinations.get(name);
        dest[0] = pageRef;
      }
    }
    for (const key of ["Rotate", "MediaBox", "CropBox", "BleedBox", "TrimBox", "ArtBox", "Resources", "Annots", "Parent", "UserUnit"]) {
      pageDict.delete(key);
    }
    const lastRef = this.newRefCount;
    await this.#collectDependencies(pageDict, false, xref);
    pageDict.set("Rotate", rotate);
    pageDict.set("MediaBox", mediaBox);
    for (const boxName of ["CropBox", "BleedBox", "TrimBox", "ArtBox"]) {
      const box = page.getBoundingBox(boxName);
      if (box?.some((value, index) => value !== mediaBox[index])) {
        pageDict.set(boxName, box);
      }
    }
    const userUnit = page.userUnit;
    if (userUnit !== 1) {
      pageDict.set("UserUnit", userUnit);
    }
    pageDict.setIfDict("Resources", await this.#collectDependencies(resources, true, xref));
    const newAnnotations = documentData.document === this.#primaryDocument ? this.#newAnnotationsParams?.newAnnotationsByPage?.get(page.pageIndex)?.filter(({
      copyLevel: level
    }) => (level ?? 0) === copyLevel) : null;
    const deletedAnnotations = new _primitives_js__WEBPACK_IMPORTED_MODULE_1__.RefSet();
    for (const {
      id,
      deleted,
      popup,
      popupRef
    } of newAnnotations || []) {
      if (!_primitives_js__WEBPACK_IMPORTED_MODULE_1__.Ref.fromString(id)) {
        continue;
      }
      if (deleted) {
        deletedAnnotations.put(id);
      }
      if (popupRef && (deleted || popup?.deleted)) {
        deletedAnnotations.put(popupRef);
      }
    }
    let newAnnots = null;
    if (annotations) {
      const keptAnnotations = await this.#collectDependencies(annotations.filter(annotation => !(annotation instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Ref && deletedAnnotations.has(annotation))), true, xref);
      this.#fixNamedDestinations(keptAnnotations, dedupNamedDestinations);
      if (Array.isArray(keptAnnotations) && keptAnnotations.length > 0) {
        newAnnots = keptAnnotations;
      }
    }
    if (newAnnotations?.length) {
      const editedAnnotations = new _primitives_js__WEBPACK_IMPORTED_MODULE_1__.RefSet();
      const updateInPlace = (data, id) => {
        const ref = id && oldRefMapping.get(id);
        const dict = ref && this.xref[ref.num];
        if (!(dict instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Dict)) {
          return false;
        }
        data.ref = ref;
        data.oldAnnotation = dict;
        editedAnnotations.put(ref);
        return true;
      };
      for (const annotation of newAnnotations) {
        if (annotation.deleted || !updateInPlace(annotation, annotation.id)) {
          continue;
        }
        delete annotation.id;
        const {
          popup
        } = annotation;
        if (popup && !popup.deleted) {
          updateInPlace(popup, annotation.popupRef);
        }
      }
      const {
        handler,
        task,
        imagesPromises
      } = this.#newAnnotationsParams;
      const changes = new _primitives_js__WEBPACK_IMPORTED_MODULE_1__.RefMap();
      const newData = await _annotation_js__WEBPACK_IMPORTED_MODULE_6__.AnnotationFactory.saveNewAnnotations(page._createPartialEvaluator(handler), this.xrefWrapper, task, newAnnotations, imagesPromises, changes);
      for (const [ref, {
        data
      }] of changes.items()) {
        this.xref[ref.num] = data;
      }
      newAnnots ||= [];
      for (const {
        ref
      } of newData.annotations) {
        if (!editedAnnotations.has(ref)) {
          newAnnots.push(ref);
        }
      }
    }
    pageDict.setIfArray("Annots", newAnnots);
    if (this.useObjectStreams) {
      const newLastRef = this.newRefCount;
      const pageObjectRefs = [];
      for (let i = lastRef; i < newLastRef; i++) {
        const obj = this.xref[i];
        if (obj instanceof _base_stream_js__WEBPACK_IMPORTED_MODULE_7__.BaseStream) {
          continue;
        }
        pageObjectRefs.push(_primitives_js__WEBPACK_IMPORTED_MODULE_1__.Ref.get(i, 0));
      }
      for (let i = 0; i < pageObjectRefs.length; i += 0xffff) {
        const objStreamRef = this.newRef;
        this.objStreamRefs.add(objStreamRef.num);
        this.xref[objStreamRef.num] = pageObjectRefs.slice(i, i + 0xffff);
      }
    }
    this.currentDocument = null;
    return pageRef;
  }
  #modalPageSize() {
    const counts = new Map();
    for (const pageData of this.oldPages) {
      if (!pageData) {
        continue;
      }
      const {
        page
      } = pageData;
      const [x0, y0, x1, y1] = page.view;
      let width = x1 - x0;
      let height = y1 - y0;
      if (width <= 0 || height <= 0) {
        continue;
      }
      if (page.rotate % 180 !== 0) {
        [width, height] = [height, width];
      }
      const key = `${width}x${height}`;
      const entry = counts.get(key);
      if (entry) {
        entry.count++;
      } else {
        counts.set(key, {
          width,
          height,
          count: 1
        });
      }
    }
    if (counts.size === 0) {
      const [,, width, height] = _document_js__WEBPACK_IMPORTED_MODULE_9__.LETTER_SIZE_MEDIABOX;
      return {
        width,
        height
      };
    }
    let best = null;
    for (const entry of counts.values()) {
      if (!best || entry.count > best.count || entry.count === best.count && entry.width * entry.height > best.width * best.height) {
        best = entry;
      }
    }
    return {
      width: best.width,
      height: best.height
    };
  }
  async #makeImagePage(bitmap, pageSize) {
    const {
      width: pageW,
      height: pageH
    } = pageSize;
    const DEFAULT_MARGIN_RATIO = 0.1;
    const margin = pageW * DEFAULT_MARGIN_RATIO;
    const availW = Math.max(1, pageW - 2 * margin);
    const availH = Math.max(1, pageH - 2 * margin);
    const lastRef = this.newRefCount;
    const {
      imageStream,
      smaskStream,
      width: imgW,
      height: imgH
    } = await (0,_pdf_images_js__WEBPACK_IMPORTED_MODULE_8__.createImage)(bitmap, this.xrefWrapper, {
      closeBitmap: true
    });
    const scale = Math.min(availW / imgW, availH / imgH);
    const drawW = imgW * scale;
    const drawH = imgH * scale;
    const tx = (pageW - drawW) / 2;
    const ty = (pageH - drawH) / 2;
    if (smaskStream) {
      const smaskRef = this.newRef;
      this.xref[smaskRef.num] = smaskStream;
      imageStream.dict.set("SMask", smaskRef);
    }
    const imageRef = this.newRef;
    this.xref[imageRef.num] = imageStream;
    const xobjectDict = new _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Dict(this.xrefWrapper);
    xobjectDict.set("Im0", imageRef);
    const resourcesDict = new _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Dict(this.xrefWrapper);
    resourcesDict.set("XObject", xobjectDict);
    resourcesDict.set("ProcSet", [_primitives_js__WEBPACK_IMPORTED_MODULE_1__.Name.get("PDF"), _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Name.get("ImageC")]);
    const content = `q ${(0,_core_utils_js__WEBPACK_IMPORTED_MODULE_0__.numberToString)(drawW)} 0 0 ${(0,_core_utils_js__WEBPACK_IMPORTED_MODULE_0__.numberToString)(drawH)} ` + `${(0,_core_utils_js__WEBPACK_IMPORTED_MODULE_0__.numberToString)(tx)} ${(0,_core_utils_js__WEBPACK_IMPORTED_MODULE_0__.numberToString)(ty)} cm /Im0 Do Q`;
    const contentsStream = new _stream_js__WEBPACK_IMPORTED_MODULE_11__.StringStream(content, new _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Dict(this.xrefWrapper));
    const contentsRef = this.newRef;
    this.xref[contentsRef.num] = contentsStream;
    const pageRef = this.newRef;
    const pageDict = this.xref[pageRef.num] = new _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Dict(this.xrefWrapper);
    pageDict.setIfName("Type", "Page");
    pageDict.set("MediaBox", [0, 0, pageW, pageH]);
    pageDict.set("Resources", resourcesDict);
    pageDict.set("Contents", contentsRef);
    if (this.useObjectStreams) {
      const newLastRef = this.newRefCount;
      const pageObjectRefs = [];
      for (let i = lastRef; i < newLastRef; i++) {
        const obj = this.xref[i];
        if (obj instanceof _base_stream_js__WEBPACK_IMPORTED_MODULE_7__.BaseStream) {
          continue;
        }
        pageObjectRefs.push(_primitives_js__WEBPACK_IMPORTED_MODULE_1__.Ref.get(i, 0));
      }
      for (let i = 0; i < pageObjectRefs.length; i += 0xffff) {
        const objStreamRef = this.newRef;
        this.objStreamRefs.add(objStreamRef.num);
        this.xref[objStreamRef.num] = pageObjectRefs.slice(i, i + 0xffff);
      }
    }
    return pageRef;
  }
  #makePageTree() {
    const {
      newPages: pages,
      rootDict,
      pagesRef,
      pagesDict
    } = this;
    rootDict.set("Pages", pagesRef);
    pagesDict.setIfName("Type", "Pages");
    pagesDict.set("Count", pages.length);
    const maxLeaves =  false ? 0 : MAX_LEAVES_PER_PAGES_NODE;
    const stack = [{
      dict: pagesDict,
      kids: pages,
      parentRef: pagesRef
    }];
    while (stack.length > 0) {
      const {
        dict,
        kids,
        parentRef
      } = stack.pop();
      if (kids.length <= maxLeaves) {
        dict.set("Kids", kids);
        for (const ref of kids) {
          this.xref[ref.num].set("Parent", parentRef);
        }
        continue;
      }
      const chunkSize = Math.max(maxLeaves, Math.ceil(kids.length / maxLeaves));
      const kidsChunks = [];
      for (let i = 0; i < kids.length; i += chunkSize) {
        kidsChunks.push(kids.slice(i, i + chunkSize));
      }
      const kidsRefs = [];
      dict.set("Kids", kidsRefs);
      for (const chunk of kidsChunks) {
        const [kidRef, kidDict] = this.newDict;
        kidsRefs.push(kidRef);
        kidDict.setIfName("Type", "Pages");
        kidDict.set("Parent", parentRef);
        kidDict.set("Count", chunk.length);
        stack.push({
          dict: kidDict,
          kids: chunk,
          parentRef: kidRef
        });
      }
    }
  }
  #makeNameNumTree(map, areNames) {
    const allEntries = map.sort(areNames ? ([keyA], [keyB]) => {
      if (keyA < keyB) {
        return -1;
      }
      return keyA > keyB ? 1 : 0;
    } : ([keyA], [keyB]) => keyA - keyB);
    const maxLeaves =  false ? 0 : MAX_IN_NAME_TREE_NODE;
    const [treeRef, treeDict] = this.newDict;
    const stack = [{
      dict: treeDict,
      entries: allEntries,
      isRoot: true
    }];
    const valueType = areNames ? "Names" : "Nums";
    while (stack.length > 0) {
      const {
        dict,
        entries,
        isRoot
      } = stack.pop();
      if (entries.length <= maxLeaves) {
        if (!isRoot) {
          dict.set("Limits", [entries[0][0], entries.at(-1)[0]]);
        }
        dict.set(valueType, entries.flat());
        continue;
      }
      const entriesChunks = [];
      const chunkSize = Math.max(maxLeaves, Math.ceil(entries.length / maxLeaves));
      for (let i = 0; i < entries.length; i += chunkSize) {
        entriesChunks.push(entries.slice(i, i + chunkSize));
      }
      const entriesRefs = [];
      dict.set("Kids", entriesRefs);
      for (const chunk of entriesChunks) {
        const [entriesRef, entriesDict] = this.newDict;
        entriesRefs.push(entriesRef);
        entriesDict.set("Limits", [chunk[0][0], chunk.at(-1)[0]]);
        stack.push({
          dict: entriesDict,
          entries: chunk
        });
      }
    }
    return treeRef;
  }
  #makePageLabelsTree() {
    const {
      pageLabels
    } = this;
    if (!pageLabels?.length) {
      return;
    }
    const {
      rootDict
    } = this;
    const pageLabelsRef = this.#makeNameNumTree(this.pageLabels, false);
    rootDict.set("PageLabels", pageLabelsRef);
  }
  async #collectEmbeddedFiles(allDocumentData) {
    const {
      embeddedFiles
    } = this;
    for (const documentData of allDocumentData) {
      const {
        embeddedFiles: docEmbeddedFiles,
        document: {
          xref
        }
      } = documentData;
      if (!docEmbeddedFiles?.size) {
        continue;
      }
      this.currentDocument = documentData;
      for (const [key, valueRef] of docEmbeddedFiles) {
        let name = key;
        if (embeddedFiles.has(name)) {
          const displayName = (0,_string_utils_js__WEBPACK_IMPORTED_MODULE_5__.stringToPDFString)(key, true);
          for (let i = 1;; i++) {
            const deduped = (0,_string_utils_js__WEBPACK_IMPORTED_MODULE_5__.stringToAsciiOrUTF16BE)(`${displayName}_${i}`);
            if (!embeddedFiles.has(deduped)) {
              name = deduped;
              break;
            }
          }
        }
        embeddedFiles.set(name, await this.#collectDependencies(valueRef, true, xref));
      }
      this.currentDocument = null;
    }
  }
  #makeEmbeddedFilesTree() {
    const {
      embeddedFiles
    } = this;
    if (embeddedFiles.size === 0) {
      return;
    }
    if (!this.namesDict) {
      [this.namesRef, this.namesDict] = this.newDict;
      this.rootDict.set("Names", this.namesRef);
    }
    this.namesDict.set("EmbeddedFiles", this.#makeNameNumTree(Array.from(embeddedFiles.entries()), true));
  }
  #makeDestinationsTree() {
    const {
      namedDestinations
    } = this;
    if (namedDestinations.size === 0) {
      return;
    }
    if (!this.namesDict) {
      [this.namesRef, this.namesDict] = this.newDict;
      this.rootDict.set("Names", this.namesRef);
    }
    this.namesDict.set("Dests", this.#makeNameNumTree(Array.from(namedDestinations, ([name, dest]) => [(0,_string_utils_js__WEBPACK_IMPORTED_MODULE_5__.stringToAsciiOrUTF16BE)(name), dest]), true));
  }
  #makeStructTree() {
    const {
      structTreeKids
    } = this;
    if (!structTreeKids?.length) {
      return;
    }
    const {
      rootDict
    } = this;
    const structTreeRef = this.newRef;
    const structTree = this.xref[structTreeRef.num] = new _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Dict();
    structTree.setIfName("Type", "StructTreeRoot");
    structTree.setIfArray("K", structTreeKids);
    for (const kidRef of structTreeKids) {
      const kid = this.xref[kidRef.num];
      const type = kid.get("Type");
      if (!type || (0,_primitives_js__WEBPACK_IMPORTED_MODULE_1__.isName)(type, "StructElem")) {
        kid.set("P", structTreeRef);
      }
    }
    if (this.parentTree.size > 0) {
      const parentTreeRef = this.#makeNameNumTree(Array.from(this.parentTree.entries()), false);
      const parentTree = this.xref[parentTreeRef.num];
      parentTree.setIfName("Type", "ParentTree");
      structTree.set("ParentTree", parentTreeRef);
      let nextKey = 0;
      for (const key of this.parentTree.keys()) {
        nextKey = Math.max(nextKey, key + 1);
      }
      structTree.set("ParentTreeNextKey", nextKey);
    }
    if (this.idTree.size > 0) {
      const idTreeRef = this.#makeNameNumTree(Array.from(this.idTree.entries()), true);
      const idTree = this.xref[idTreeRef.num];
      idTree.setIfName("Type", "IDTree");
      structTree.set("IDTree", idTreeRef);
    }
    if (this.classMap.size > 0) {
      const classMapRef = this.newRef;
      this.xref[classMapRef.num] = this.classMap;
      structTree.set("ClassMap", classMapRef);
    }
    if (this.roleMap.size > 0) {
      const roleMapRef = this.newRef;
      this.xref[roleMapRef.num] = this.roleMap;
      structTree.set("RoleMap", roleMapRef);
    }
    if (this.namespaces.size > 0) {
      const namespacesRef = this.newRef;
      this.xref[namespacesRef.num] = Array.from(this.namespaces.values());
      structTree.set("Namespaces", namespacesRef);
    }
    if (this.structTreeAF.length > 0) {
      const structTreeAFRef = this.newRef;
      this.xref[structTreeAFRef.num] = this.structTreeAF;
      structTree.set("AF", structTreeAFRef);
    }
    if (this.structTreePronunciationLexicon.length > 0) {
      const structTreePronunciationLexiconRef = this.newRef;
      this.xref[structTreePronunciationLexiconRef.num] = this.structTreePronunciationLexicon;
      structTree.set("PronunciationLexicon", structTreePronunciationLexiconRef);
    }
    rootDict.set("StructTreeRoot", structTreeRef);
  }
  #makeAcroForm() {
    if (this.fields.length === 0) {
      return;
    }
    const {
      rootDict
    } = this;
    const acroFormRef = this.newRef;
    const acroForm = this.xref[acroFormRef.num] = new _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Dict();
    rootDict.set("AcroForm", acroFormRef);
    acroForm.set("Fields", this.fields);
    if (this.acroFormNeedAppearances) {
      acroForm.set("NeedAppearances", true);
    }
    if (this.acroFormSigFlags > 0) {
      acroForm.set("SigFlags", this.acroFormSigFlags);
    }
    acroForm.setIfArray("CO", this.acroFormCalculationOrder);
    acroForm.setIfDefined("DR", this.acroFormDefaultResources);
    if (this.acroFormDefaultAppearance) {
      acroForm.set("DA", this.acroFormDefaultAppearance);
    }
    if (this.acroFormQ > 0) {
      acroForm.set("Q", this.acroFormQ);
    }
  }
  async #makeRoot() {
    const {
      rootDict
    } = this;
    rootDict.setIfName("Type", "Catalog");
    rootDict.setIfName("Version", this.version);
    this.#makeAcroForm();
    this.#makePageTree();
    this.#makePageLabelsTree();
    this.#makeEmbeddedFilesTree();
    this.#makeDestinationsTree();
    this.#makeStructTree();
    await this.#makeOutline();
  }
  #makeInfo() {
    const infoMap = new Map();
    if (this.isSingleFile) {
      const firstRealPage = this.oldPages.find(p => !!p);
      const {
        xref: {
          trailer
        }
      } = firstRealPage.documentData.document;
      const oldInfoDict = trailer.get("Info");
      for (const [key, value] of oldInfoDict || []) {
        if (typeof value === "string") {
          infoMap.set(key, (0,_string_utils_js__WEBPACK_IMPORTED_MODULE_5__.stringToPDFString)(value));
        }
      }
      for (const key of INFO_CONFORMANCE_KEYS) {
        infoMap.delete(key);
      }
    }
    infoMap.delete("ModDate");
    infoMap.set("CreationDate", (0,_core_utils_js__WEBPACK_IMPORTED_MODULE_0__.getModificationDate)());
    infoMap.set("Creator", "PDF.js");
    infoMap.set("Producer", "Firefox");
    if (this.author) {
      infoMap.set("Author", this.author);
    }
    if (this.title) {
      infoMap.set("Title", this.title);
    }
    for (const [key, value] of infoMap) {
      this.infoDict.set(key, (0,_string_utils_js__WEBPACK_IMPORTED_MODULE_5__.stringToAsciiOrUTF16BE)(value));
    }
    return infoMap;
  }
  #getSingleDocumentData() {
    return this.isSingleFile ? this.oldPages.find(p => !!p).documentData : null;
  }
  get password() {
    return this.#getSingleDocumentData()?.document.pdfManager.password ?? null;
  }
  getCopiedAnnotationStorage(annotationStorage) {
    const storage = new Map();
    for (const [key, value] of annotationStorage ?? []) {
      const ref = _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Ref.fromString(key);
      if (!ref) {
        continue;
      }
      for (const oldRefMapping of this.#primaryRefMappings) {
        const newRef = oldRefMapping.get(ref);
        if (newRef) {
          storage.set(newRef.toString(), value);
        }
      }
    }
    return storage.size > 0 ? storage : null;
  }
  async #makeEncrypt() {
    const documentData = this.#getSingleDocumentData();
    if (!documentData) {
      return [null, null, null];
    }
    const {
      document: {
        xref: {
          trailer,
          encrypt
        }
      }
    } = documentData;
    if (!trailer.has("Encrypt")) {
      return [null, null, null];
    }
    const encryptDict = trailer.get("Encrypt");
    if (!(encryptDict instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Dict)) {
      return [null, null, null];
    }
    this.currentDocument = documentData;
    const result = [await this.#cloneObject(encryptDict, trailer.xref), encrypt, trailer.get("ID")];
    this.currentDocument = null;
    return result;
  }
  async #createChanges(encrypt, encryptRef) {
    const changes = new _primitives_js__WEBPACK_IMPORTED_MODULE_1__.RefMap();
    changes.put(_primitives_js__WEBPACK_IMPORTED_MODULE_1__.Ref.get(0, 0xffff), {
      data: null
    });
    for (let i = 1, ii = this.xref.length; i < ii; i++) {
      const ref = _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Ref.get(i, 0);
      const obj = this.xref[i];
      if (this.objStreamRefs?.has(i)) {
        await this.#createObjectStream(ref, obj, changes);
      } else if (typeof obj === "string") {
        const buffer = [];
        await (0,_writer_js__WEBPACK_IMPORTED_MODULE_2__.writeObject)(ref, obj, buffer, {
          encrypt,
          encryptRef
        });
        changes.put(ref, {
          data: buffer.join("")
        });
      } else {
        changes.put(ref, {
          data: obj
        });
      }
    }
    return [changes, this.newRef];
  }
  async #createObjectStream(objStreamRef, objRefs, changes) {
    const streamBuffer = [""];
    const objOffsets = [];
    let offset = 0;
    const buffer = [];
    for (let i = 0, ii = objRefs.length; i < ii; i++) {
      const objRef = objRefs[i];
      changes.put(objRef, {
        data: null,
        objStreamRef,
        index: i
      });
      objOffsets.push(`${objRef.num} ${offset}`);
      const data = this.xref[objRef.num];
      await (0,_writer_js__WEBPACK_IMPORTED_MODULE_2__.writeValue)(data, buffer, null);
      const obj = buffer.join("");
      buffer.length = 0;
      streamBuffer.push(obj);
      offset += obj.length + 1;
    }
    streamBuffer[0] = objOffsets.join("\n");
    const dict = new _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Dict();
    dict.setIfName("Type", "ObjStm");
    dict.set("N", objRefs.length);
    dict.set("First", streamBuffer[0].length + 1);
    const objStream = new _stream_js__WEBPACK_IMPORTED_MODULE_11__.StringStream(streamBuffer.join("\n"), dict);
    changes.put(objStreamRef, {
      data: objStream
    });
  }
  async writePDF() {
    await this.#makeRoot();
    const infoMap = this.#makeInfo();
    const [encryptRef, encrypt, fileIds] = await this.#makeEncrypt();
    const [changes, xrefTableRef] = await this.#createChanges(encrypt, encryptRef);
    const header = (0,_shared_util_js__WEBPACK_IMPORTED_MODULE_3__.stringToBytes)(`%PDF-${this.version}\n%\xfa\xde\xfa\xce`);
    return (0,_writer_js__WEBPACK_IMPORTED_MODULE_2__.incrementalUpdate)({
      originalData: header,
      changes,
      xrefInfo: {
        startXRef: null,
        rootRef: this.rootRef,
        infoRef: this.infoRef,
        encryptRef,
        newRef: xrefTableRef,
        fileIds: fileIds || [null, null],
        infoMap
      },
      useXrefStream: this.useObjectStreams,
      xref: {
        encrypt,
        encryptRef
      }
    });
  }
}


/***/ }

};
