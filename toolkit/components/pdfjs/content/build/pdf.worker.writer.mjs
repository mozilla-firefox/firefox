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
export const __webpack_esm_ids__ = ["writer"];
export const __webpack_esm_modules__ = {

/***/ 6721
(__unused_webpack___webpack_module__, __webpack_exports__, __webpack_require__) {

/* harmony export */ __webpack_require__.d(__webpack_exports__, {
/* harmony export */   incrementalUpdate: () => (/* binding */ incrementalUpdate),
/* harmony export */   numberToPDFString: () => (/* binding */ numberToPDFString),
/* harmony export */   writeObject: () => (/* binding */ writeObject),
/* harmony export */   writeValue: () => (/* binding */ writeValue)
/* harmony export */ });
/* unused harmony exports writeChanges, writeDict */
/* harmony import */ var _shared_util_js__WEBPACK_IMPORTED_MODULE_0__ = __webpack_require__(4292);
/* harmony import */ var _primitives_js__WEBPACK_IMPORTED_MODULE_1__ = __webpack_require__(5632);
/* harmony import */ var _core_utils_js__WEBPACK_IMPORTED_MODULE_2__ = __webpack_require__(5399);
/* harmony import */ var _xml_parser_js__WEBPACK_IMPORTED_MODULE_3__ = __webpack_require__(3103);
/* harmony import */ var _stream_js__WEBPACK_IMPORTED_MODULE_4__ = __webpack_require__(4106);
/* harmony import */ var _base_stream_js__WEBPACK_IMPORTED_MODULE_5__ = __webpack_require__(1834);
/* harmony import */ var _calculate_md5_js__WEBPACK_IMPORTED_MODULE_6__ = __webpack_require__(3343);







async function writeObject(ref, obj, buffer, {
  encrypt = null,
  encryptRef = null
}) {
  const transform = encrypt && encryptRef !== ref ? encrypt.createCipherTransform(ref.num, ref.gen) : null;
  buffer.push(`${ref.num} ${ref.gen} obj\n`);
  await writeValue(obj, buffer, transform);
  buffer.push("\nendobj\n");
}
async function writeDict(dict, buffer, transform) {
  buffer.push("<<");
  for (const [key, rawObj] of dict.getRawEntries()) {
    buffer.push(` /${(0,_core_utils_js__WEBPACK_IMPORTED_MODULE_2__.escapePDFName)(key)} `);
    await writeValue(rawObj, buffer, transform);
  }
  buffer.push(">>");
}
async function writeStream(stream, buffer, transform) {
  stream = stream.getOriginalStream();
  stream.reset();
  let bytes = stream.getBytes();
  const {
    dict
  } = stream;
  let [filter, params] = await Promise.all([dict.getAsync("Filter"), dict.getAsync("DecodeParms")]);
  const filters = Array.isArray(filter) ? await Promise.all(filter.map(f => dict.xref.fetchIfRefAsync(f))) : [filter];
  let cryptFilterName = null;
  const cryptFilterIndex = filters.findIndex(f => (0,_primitives_js__WEBPACK_IMPORTED_MODULE_1__.isName)(f, "Crypt"));
  if (cryptFilterIndex !== -1) {
    if (transform) {
      const cryptParams = Array.isArray(params) ? await dict.xref.fetchIfRefAsync(params[cryptFilterIndex]) : params;
      const name = cryptParams instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Dict ? cryptParams.get("Name") : null;
      cryptFilterName = name instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Name ? name : _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Name.get("Identity");
    } else {
      filters.splice(cryptFilterIndex, 1);
      if (filters.length === 0) {
        filter = params = undefined;
        dict.delete("Filter");
        dict.delete("DecodeParms");
      } else {
        filter = filter.toSpliced(cryptFilterIndex, 1);
        dict.set("Filter", filter);
        if (Array.isArray(params)) {
          params = params.toSpliced(cryptFilterIndex, 1);
          dict.set("DecodeParms", params);
        }
      }
    }
  }
  const filterZero = filters[0];
  const isFilterZeroFlateDecode = (0,_primitives_js__WEBPACK_IMPORTED_MODULE_1__.isName)(filterZero, "FlateDecode");
  const isFilterZeroImageDecode = (0,_primitives_js__WEBPACK_IMPORTED_MODULE_1__.isName)(filterZero, "DCTDecode") || (0,_primitives_js__WEBPACK_IMPORTED_MODULE_1__.isName)(filterZero, "JPXDecode") || (0,_primitives_js__WEBPACK_IMPORTED_MODULE_1__.isName)(filterZero, "JBIG2Decode") || (0,_primitives_js__WEBPACK_IMPORTED_MODULE_1__.isName)(filterZero, "CCITTFaxDecode") || (0,_primitives_js__WEBPACK_IMPORTED_MODULE_1__.isName)(filterZero, "LZWDecode");
  const isFilterZeroCompressedObject = isFilterZeroFlateDecode || isFilterZeroImageDecode || (0,_primitives_js__WEBPACK_IMPORTED_MODULE_1__.isName)(filterZero, "BrotliDecode");
  const MIN_LENGTH_FOR_COMPRESSING = 256;
  if (!isFilterZeroCompressedObject && !cryptFilterName && !(0,_primitives_js__WEBPACK_IMPORTED_MODULE_1__.isDict)(dict, "Metadata") && bytes.length >= MIN_LENGTH_FOR_COMPRESSING) {
    try {
      const cs = new CompressionStream("deflate");
      const writer = cs.writable.getWriter();
      await writer.ready;
      writer.write(bytes).then(async () => {
        await writer.ready;
        await writer.close();
      }).catch(_shared_util_js__WEBPACK_IMPORTED_MODULE_0__.noopFn);
      bytes = await new Response(cs.readable).bytes();
      let newFilter, newParams;
      if (!filter) {
        newFilter = _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Name.get("FlateDecode");
      } else if (!isFilterZeroFlateDecode) {
        newFilter = Array.isArray(filter) ? [_primitives_js__WEBPACK_IMPORTED_MODULE_1__.Name.get("FlateDecode"), ...filter] : [_primitives_js__WEBPACK_IMPORTED_MODULE_1__.Name.get("FlateDecode"), filter];
        if (params) {
          newParams = Array.isArray(params) ? [null, ...params] : [null, params];
        }
      }
      if (newFilter) {
        dict.set("Filter", newFilter);
      }
      if (newParams) {
        dict.set("DecodeParms", newParams);
      }
    } catch (ex) {
      ;(0,_shared_util_js__WEBPACK_IMPORTED_MODULE_0__.info)(`writeStream - cannot compress data: "${ex}".`);
    }
  }
  let string = (0,_shared_util_js__WEBPACK_IMPORTED_MODULE_0__.bytesToString)(bytes);
  if (transform) {
    string = transform.encryptStream(string, dict, cryptFilterName);
  }
  dict.set("Length", string.length);
  await writeDict(dict, buffer, transform);
  buffer.push(" stream\n", string, "\nendstream");
}
async function writeArray(array, buffer, transform) {
  buffer.push("[");
  for (let i = 0, ii = array.length; i < ii; i++) {
    await writeValue(array[i], buffer, transform);
    if (i < ii - 1) {
      buffer.push(" ");
    }
  }
  buffer.push("]");
}
function numberToPDFString(value) {
  if (Number.isInteger(value) && Math.abs(value) >= 1e21) {
    return BigInt(value).toString();
  }
  const str = value.toFixed(10);
  let end = str.length;
  while (str[end - 1] === "0") {
    end--;
  }
  if (str[end - 1] === ".") {
    end--;
  }
  return str.slice(0, end);
}
async function writeValue(value, buffer, transform) {
  if (value instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Name) {
    buffer.push(`/${(0,_core_utils_js__WEBPACK_IMPORTED_MODULE_2__.escapePDFName)(value.name)}`);
  } else if (value instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Ref) {
    buffer.push(`${value.num} ${value.gen} R`);
  } else if (Array.isArray(value) || ArrayBuffer.isView(value)) {
    await writeArray(value, buffer, transform);
  } else if (typeof value === "string") {
    if (transform) {
      value = transform.encryptString(value);
    }
    buffer.push(`(${(0,_core_utils_js__WEBPACK_IMPORTED_MODULE_2__.escapeString)(value)})`);
  } else if (typeof value === "number") {
    buffer.push(numberToPDFString(value));
  } else if (typeof value === "boolean") {
    buffer.push(value.toString());
  } else if (value instanceof _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Dict) {
    await writeDict(value, buffer, transform);
  } else if (value instanceof _base_stream_js__WEBPACK_IMPORTED_MODULE_5__.BaseStream) {
    await writeStream(value, buffer, transform);
  } else if (value === null) {
    buffer.push("null");
  } else {
    (0,_shared_util_js__WEBPACK_IMPORTED_MODULE_0__.warn)(`Unhandled value in writer: ${typeof value}, please file a bug.`);
  }
}
function writeInt(number, size, offset, buffer) {
  for (let i = size + offset - 1; i > offset - 1; i--) {
    buffer[i] = number & 0xff;
    number >>= 8;
  }
  return offset + size;
}
function writeString(string, offset, buffer) {
  const ii = string.length;
  for (let i = 0; i < ii; i++) {
    buffer[offset + i] = string.charCodeAt(i) & 0xff;
  }
  return offset + ii;
}
function computeMD5(filesize, xrefInfo) {
  const time = Math.floor(Date.now() / 1000);
  const filename = xrefInfo.filename || "";
  const md5Buffer = [time.toString(), filename, filesize.toString(), ...xrefInfo.infoMap.values()];
  const md5BufferLen = Math.sumPrecise(md5Buffer.map(str => str.length));
  const array = new Uint8Array(md5BufferLen);
  let offset = 0;
  for (const str of md5Buffer) {
    offset = writeString(str, offset, array);
  }
  return (0,_shared_util_js__WEBPACK_IMPORTED_MODULE_0__.bytesToString)((0,_calculate_md5_js__WEBPACK_IMPORTED_MODULE_6__.calculateMD5)(array, 0, array.length));
}
function writeXFADataForAcroform(str, changes) {
  const xml = new _xml_parser_js__WEBPACK_IMPORTED_MODULE_3__.SimpleXMLParser({
    hasAttributes: true
  }).parseFromString(str);
  for (const {
    xfa
  } of changes) {
    if (!xfa) {
      continue;
    }
    const {
      path,
      value
    } = xfa;
    if (!path || value === null) {
      continue;
    }
    const nodePath = (0,_core_utils_js__WEBPACK_IMPORTED_MODULE_2__.parseXFAPath)(path);
    let node = xml.documentElement.searchNode(nodePath, 0);
    if (!node && nodePath.length > 1) {
      node = xml.documentElement.searchNode([nodePath.at(-1)], 0);
    }
    if (node) {
      node.childNodes = Array.isArray(value) ? value.map(val => new _xml_parser_js__WEBPACK_IMPORTED_MODULE_3__.SimpleDOMNode("value", val)) : [new _xml_parser_js__WEBPACK_IMPORTED_MODULE_3__.SimpleDOMNode("#text", value)];
    } else {
      (0,_shared_util_js__WEBPACK_IMPORTED_MODULE_0__.warn)(`Node not found for path: ${path}`);
    }
  }
  const buffer = [];
  xml.documentElement.dump(buffer);
  return buffer.join("");
}
async function updateAcroform({
  xref,
  acroForm,
  acroFormRef,
  hasXfa,
  hasXfaDatasetsEntry,
  xfaDatasetsRef,
  needAppearances,
  changes
}) {
  if (hasXfa && !hasXfaDatasetsEntry && !xfaDatasetsRef) {
    (0,_shared_util_js__WEBPACK_IMPORTED_MODULE_0__.warn)("XFA - Cannot save it");
  }
  if (!needAppearances && (!hasXfa || !xfaDatasetsRef || hasXfaDatasetsEntry)) {
    return;
  }
  const dict = acroForm.clone();
  if (hasXfa && !hasXfaDatasetsEntry) {
    const newXfa = acroForm.get("XFA").slice();
    newXfa.splice(2, 0, "datasets");
    newXfa.splice(3, 0, xfaDatasetsRef);
    dict.set("XFA", newXfa);
  }
  if (needAppearances) {
    dict.set("NeedAppearances", true);
  }
  changes.put(acroFormRef, {
    data: dict
  });
}
function updateXFA({
  xfaData,
  xfaDatasetsRef,
  changes,
  xref
}) {
  if (xfaData === null) {
    const datasets = xref.fetchIfRef(xfaDatasetsRef);
    xfaData = writeXFADataForAcroform(datasets.getString(), changes);
  }
  const xfaDataStream = new _stream_js__WEBPACK_IMPORTED_MODULE_4__.StringStream(xfaData, new _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Dict(xref));
  xfaDataStream.dict.setIfName("Type", "EmbeddedFile");
  changes.put(xfaDatasetsRef, {
    data: xfaDataStream
  });
}
async function getXRefTable(xrefInfo, baseOffset, newRefs, newXref, buffer) {
  buffer.push("xref\n");
  const indexes = getIndexes(newRefs);
  let indexesPosition = 0;
  for (const {
    ref,
    data
  } of newRefs) {
    if (ref.num === indexes[indexesPosition]) {
      buffer.push(`${indexes[indexesPosition]} ${indexes[indexesPosition + 1]}\n`);
      indexesPosition += 2;
    }
    if (data !== null) {
      buffer.push(`${baseOffset.toString().padStart(10, "0")} ${Math.min(ref.gen, 0xffff).toString().padStart(5, "0")} n\r\n`);
      baseOffset += data.length;
    } else {
      buffer.push(`0000000000 ${Math.min(ref.gen + 1, 0xffff).toString().padStart(5, "0")} f\r\n`);
    }
  }
  computeIDs(baseOffset, xrefInfo, newXref);
  buffer.push("trailer\n");
  await writeDict(newXref, buffer, null);
  buffer.push("\nstartxref\n", baseOffset.toString(), "\n%%EOF\n");
}
function getIndexes(newRefs) {
  const indexes = [];
  for (const {
    ref
  } of newRefs) {
    if (ref.num === indexes.at(-2) + indexes.at(-1)) {
      indexes[indexes.length - 1] += 1;
    } else {
      indexes.push(ref.num, 1);
    }
  }
  return indexes;
}
async function getXRefStreamTable(xrefInfo, baseOffset, newRefs, newXref, buffer) {
  const xrefTableData = [];
  let maxOffset = 0;
  let maxGen = 0;
  for (const {
    ref,
    data,
    objStreamRef,
    index
  } of newRefs) {
    let gen;
    maxOffset = Math.max(maxOffset, baseOffset);
    if (objStreamRef) {
      gen = index;
      xrefTableData.push([2, objStreamRef.num, gen]);
    } else if (data !== null) {
      gen = Math.min(ref.gen, 0xffff);
      xrefTableData.push([1, baseOffset, gen]);
      baseOffset += data.length;
    } else {
      gen = Math.min(ref.gen + 1, 0xffff);
      xrefTableData.push([0, 0, gen]);
    }
    maxGen = Math.max(maxGen, gen);
  }
  newXref.set("Index", getIndexes(newRefs));
  const offsetSize = (0,_core_utils_js__WEBPACK_IMPORTED_MODULE_2__.getSizeInBytes)(maxOffset);
  const maxGenSize = (0,_core_utils_js__WEBPACK_IMPORTED_MODULE_2__.getSizeInBytes)(maxGen);
  const sizes = [1, offsetSize, maxGenSize];
  newXref.set("W", sizes);
  computeIDs(baseOffset, xrefInfo, newXref);
  const structSize = Math.sumPrecise(sizes);
  const data = new Uint8Array(structSize * xrefTableData.length);
  const stream = new _stream_js__WEBPACK_IMPORTED_MODULE_4__.Stream(data);
  stream.dict = newXref;
  let offset = 0;
  for (const [type, objOffset, gen] of xrefTableData) {
    offset = writeInt(type, sizes[0], offset, data);
    offset = writeInt(objOffset, sizes[1], offset, data);
    offset = writeInt(gen, sizes[2], offset, data);
  }
  await writeObject(xrefInfo.newRef, stream, buffer, {});
  buffer.push("startxref\n", baseOffset.toString(), "\n%%EOF\n");
}
function computeIDs(baseOffset, xrefInfo, newXref) {
  if (Array.isArray(xrefInfo.fileIds) && xrefInfo.fileIds.length > 0) {
    const md5 = computeMD5(baseOffset, xrefInfo);
    newXref.set("ID", [xrefInfo.fileIds[0] || md5, md5]);
  }
}
function getTrailerDict(xrefInfo, changes, useXrefStream) {
  const newXref = new _primitives_js__WEBPACK_IMPORTED_MODULE_1__.Dict(null);
  newXref.setIfDefined("Prev", xrefInfo?.startXRef);
  const refForXrefTable = xrefInfo.newRef;
  if (useXrefStream) {
    changes.put(refForXrefTable, {
      data: ""
    });
    newXref.set("Size", refForXrefTable.num + 1);
    newXref.setIfName("Type", "XRef");
  } else {
    newXref.set("Size", refForXrefTable.num);
  }
  newXref.setIfDefined("Root", xrefInfo?.rootRef);
  newXref.setIfDefined("Info", xrefInfo?.infoRef);
  newXref.setIfDefined("Encrypt", xrefInfo?.encryptRef);
  return newXref;
}
async function writeChanges(changes, xref, buffer = []) {
  const newRefs = [];
  for (const [ref, {
    data,
    objStreamRef,
    index
  }] of changes.items()) {
    if (objStreamRef) {
      newRefs.push({
        ref,
        data,
        objStreamRef,
        index
      });
      continue;
    }
    if (data === null || typeof data === "string") {
      newRefs.push({
        ref,
        data
      });
      continue;
    }
    await writeObject(ref, data, buffer, xref);
    newRefs.push({
      ref,
      data: buffer.join("")
    });
    buffer.length = 0;
  }
  return newRefs.sort((a, b) => a.ref.num - b.ref.num);
}
async function incrementalUpdate({
  originalData,
  xrefInfo,
  changes,
  xref = null,
  hasXfa = false,
  xfaDatasetsRef = null,
  hasXfaDatasetsEntry = false,
  needAppearances,
  acroFormRef = null,
  acroForm = null,
  xfaData = null,
  useXrefStream = false
}) {
  await updateAcroform({
    xref,
    acroForm,
    acroFormRef,
    hasXfa,
    hasXfaDatasetsEntry,
    xfaDatasetsRef,
    needAppearances,
    changes
  });
  if (hasXfa) {
    updateXFA({
      xfaData,
      xfaDatasetsRef,
      changes,
      xref
    });
  }
  const newXref = getTrailerDict(xrefInfo, changes, useXrefStream);
  const buffer = [];
  const newRefs = await writeChanges(changes, xref, buffer);
  let baseOffset = originalData.length;
  const lastByte = originalData.at(-1);
  if (lastByte !== 0x0a && lastByte !== 0x0d) {
    buffer.push("\n");
    baseOffset += 1;
  }
  for (const {
    data
  } of newRefs) {
    if (data !== null) {
      buffer.push(data);
    }
  }
  await (useXrefStream ? getXRefStreamTable(xrefInfo, baseOffset, newRefs, newXref, buffer) : getXRefTable(xrefInfo, baseOffset, newRefs, newXref, buffer));
  const totalLength = originalData.length + Math.sumPrecise(buffer.map(str => str.length));
  const array = new Uint8Array(totalLength);
  array.set(originalData);
  let offset = originalData.length;
  for (const str of buffer) {
    offset = writeString(str, offset, array);
  }
  return array;
}


/***/ }

};
