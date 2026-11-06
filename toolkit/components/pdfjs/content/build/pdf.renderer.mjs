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
 * pdfjsVersion = 6.5.472
 * pdfjsBuild = 1c3fdc259
 */

;// ./src/shared/util.js
const isNodeJS = false;
const BBOX_INIT = [Infinity, Infinity, -Infinity, -Infinity];
const F32_BBOX_INIT = new Float32Array(BBOX_INIT);
const FONT_IDENTITY_MATRIX = [0.001, 0, 0, 0.001, 0, 0];
const DEFAULT_VERTICAL_ORIGIN_Y = 880;
const DEFAULT_VERTICAL_ADVANCE = 1000;
const LINE_FACTOR = 1.35;
const LINE_DESCENT_FACTOR = 0.35;
const BASELINE_FACTOR = (/* unused pure expression or super */ null && (LINE_DESCENT_FACTOR / LINE_FACTOR));
const SVG_NS = "http://www.w3.org/2000/svg";
const RenderingIntentFlag = {
  ANY: 0x01,
  DISPLAY: 0x02,
  PRINT: 0x04,
  SAVE: 0x08,
  ANNOTATIONS_FORMS: 0x10,
  ANNOTATIONS_STORAGE: 0x20,
  ANNOTATIONS_DISABLE: 0x40,
  IS_EDITING: 0x80,
  OPLIST: 0x100
};
const AnnotationMode = (/* unused pure expression or super */ null && ({
  DISABLE: 0,
  ENABLE: 1,
  ENABLE_FORMS: 2,
  ENABLE_STORAGE: 3
}));
const AnnotationPrefix = "pdfjs_internal_id_";
const AnnotationEditorPrefix = "pdfjs_internal_editor_";
const AnnotationEditorType = (/* unused pure expression or super */ null && ({
  DISABLE: -1,
  NONE: 0,
  FREETEXT: 3,
  HIGHLIGHT: 9,
  STAMP: 13,
  INK: 15,
  POPUP: 16,
  SIGNATURE: 101,
  COMMENT: 102
}));
const AnnotationEditorParamsType = (/* unused pure expression or super */ null && ({
  RESIZE: 1,
  CREATE: 2,
  FREETEXT_SIZE: 11,
  FREETEXT_COLOR: 12,
  FREETEXT_OPACITY: 13,
  INK_COLOR: 21,
  INK_THICKNESS: 22,
  INK_OPACITY: 23,
  INK_COLOR_AND_OPACITY: 24,
  HIGHLIGHT_COLOR: 31,
  HIGHLIGHT_THICKNESS: 32,
  HIGHLIGHT_FREE: 33,
  HIGHLIGHT_SHOW_ALL: 34,
  DRAW_STEP: 41
}));
const PermissionFlag = (/* unused pure expression or super */ null && ({
  PRINT: 0x04,
  MODIFY_CONTENTS: 0x08,
  COPY: 0x10,
  MODIFY_ANNOTATIONS: 0x20,
  FILL_INTERACTIVE_FORMS: 0x100,
  COPY_FOR_ACCESSIBILITY: 0x200,
  ASSEMBLE: 0x400,
  PRINT_HIGH_QUALITY: 0x800
}));
const MeshFigureType = (/* unused pure expression or super */ null && ({
  TRIANGLES: 1,
  LATTICE: 2,
  PATCH: 3
}));
const TextRenderingMode = {
  FILL: 0,
  STROKE: 1,
  FILL_STROKE: 2,
  INVISIBLE: 3,
  FILL_ADD_TO_PATH: 4,
  STROKE_ADD_TO_PATH: 5,
  FILL_STROKE_ADD_TO_PATH: 6,
  ADD_TO_PATH: 7,
  FILL_STROKE_MASK: 3,
  ADD_TO_PATH_FLAG: 4
};
const ImageKind = {
  GRAYSCALE_1BPP: 1,
  RGB_24BPP: 2,
  RGBA_32BPP: 3
};
const AnnotationType = (/* unused pure expression or super */ null && ({
  TEXT: 1,
  LINK: 2,
  FREETEXT: 3,
  LINE: 4,
  SQUARE: 5,
  CIRCLE: 6,
  POLYGON: 7,
  POLYLINE: 8,
  HIGHLIGHT: 9,
  UNDERLINE: 10,
  SQUIGGLY: 11,
  STRIKEOUT: 12,
  STAMP: 13,
  CARET: 14,
  INK: 15,
  POPUP: 16,
  FILEATTACHMENT: 17,
  SOUND: 18,
  MOVIE: 19,
  WIDGET: 20,
  SCREEN: 21,
  PRINTERMARK: 22,
  TRAPNET: 23,
  WATERMARK: 24,
  THREED: 25,
  REDACT: 26,
  RICHMEDIA: 27
}));
const AnnotationReplyType = (/* unused pure expression or super */ null && ({
  GROUP: "Group",
  REPLY: "R"
}));
const AnnotationRenditionOperation = (/* unused pure expression or super */ null && ({
  PLAY_OR_RESUME: 0,
  STOP: 1,
  PAUSE: 2,
  RESUME: 3,
  PLAY: 4
}));
const AnnotationFlag = (/* unused pure expression or super */ null && ({
  INVISIBLE: 0x01,
  HIDDEN: 0x02,
  PRINT: 0x04,
  NOZOOM: 0x08,
  NOROTATE: 0x10,
  NOVIEW: 0x20,
  READONLY: 0x40,
  LOCKED: 0x80,
  TOGGLENOVIEW: 0x100,
  LOCKEDCONTENTS: 0x200
}));
const AnnotationFieldFlag = (/* unused pure expression or super */ null && ({
  READONLY: 0x0000001,
  REQUIRED: 0x0000002,
  NOEXPORT: 0x0000004,
  MULTILINE: 0x0001000,
  PASSWORD: 0x0002000,
  NOTOGGLETOOFF: 0x0004000,
  RADIO: 0x0008000,
  PUSHBUTTON: 0x0010000,
  COMBO: 0x0020000,
  EDIT: 0x0040000,
  SORT: 0x0080000,
  FILESELECT: 0x0100000,
  MULTISELECT: 0x0200000,
  DONOTSPELLCHECK: 0x0400000,
  DONOTSCROLL: 0x0800000,
  COMB: 0x1000000,
  RICHTEXT: 0x2000000,
  RADIOSINUNISON: 0x2000000,
  COMMITONSELCHANGE: 0x4000000
}));
const AnnotationBorderStyleType = (/* unused pure expression or super */ null && ({
  SOLID: 1,
  DASHED: 2,
  BEVELED: 3,
  INSET: 4,
  UNDERLINE: 5
}));
const AnnotationActionEventType = (/* unused pure expression or super */ null && ({
  E: "Mouse Enter",
  X: "Mouse Exit",
  D: "Mouse Down",
  U: "Mouse Up",
  Fo: "Focus",
  Bl: "Blur",
  PO: "PageOpen",
  PC: "PageClose",
  PV: "PageVisible",
  PI: "PageInvisible",
  K: "Keystroke",
  F: "Format",
  V: "Validate",
  C: "Calculate"
}));
const DocumentActionEventType = (/* unused pure expression or super */ null && ({
  WC: "WillClose",
  WS: "WillSave",
  DS: "DidSave",
  WP: "WillPrint",
  DP: "DidPrint"
}));
const PageActionEventType = (/* unused pure expression or super */ null && ({
  O: "PageOpen",
  C: "PageClose"
}));
const VerbosityLevel = {
  ERRORS: 0,
  WARNINGS: 1,
  INFOS: 5
};
const OPS = {
  dependency: 1,
  setLineWidth: 2,
  setLineCap: 3,
  setLineJoin: 4,
  setMiterLimit: 5,
  setDash: 6,
  setRenderingIntent: 7,
  setFlatness: 8,
  setGState: 9,
  save: 10,
  restore: 11,
  transform: 12,
  moveTo: 13,
  lineTo: 14,
  curveTo: 15,
  curveTo2: 16,
  curveTo3: 17,
  closePath: 18,
  rectangle: 19,
  stroke: 20,
  closeStroke: 21,
  fill: 22,
  eoFill: 23,
  fillStroke: 24,
  eoFillStroke: 25,
  closeFillStroke: 26,
  closeEOFillStroke: 27,
  endPath: 28,
  clip: 29,
  eoClip: 30,
  beginText: 31,
  endText: 32,
  setCharSpacing: 33,
  setWordSpacing: 34,
  setHScale: 35,
  setLeading: 36,
  setFont: 37,
  setTextRenderingMode: 38,
  setTextRise: 39,
  moveText: 40,
  setLeadingMoveText: 41,
  setTextMatrix: 42,
  nextLine: 43,
  showText: 44,
  showSpacedText: 45,
  nextLineShowText: 46,
  nextLineSetSpacingShowText: 47,
  setCharWidth: 48,
  setCharWidthAndBounds: 49,
  setStrokeColorSpace: 50,
  setFillColorSpace: 51,
  setStrokeColor: 52,
  setStrokeColorN: 53,
  setFillColor: 54,
  setFillColorN: 55,
  setStrokeGray: 56,
  setFillGray: 57,
  setStrokeRGBColor: 58,
  setFillRGBColor: 59,
  setStrokeCMYKColor: 60,
  setFillCMYKColor: 61,
  shadingFill: 62,
  beginInlineImage: 63,
  beginImageData: 64,
  endInlineImage: 65,
  paintXObject: 66,
  markPoint: 67,
  markPointProps: 68,
  beginMarkedContent: 69,
  beginMarkedContentProps: 70,
  endMarkedContent: 71,
  beginCompat: 72,
  endCompat: 73,
  paintFormXObjectBegin: 74,
  paintFormXObjectEnd: 75,
  beginGroup: 76,
  endGroup: 77,
  beginAnnotation: 80,
  endAnnotation: 81,
  paintImageMaskXObject: 83,
  paintImageMaskXObjectGroup: 84,
  paintImageXObject: 85,
  paintInlineImageXObject: 86,
  paintInlineImageXObjectGroup: 87,
  paintImageXObjectRepeat: 88,
  paintImageMaskXObjectRepeat: 89,
  paintSolidColorImageMask: 90,
  constructPath: 91,
  setStrokeTransparent: 92,
  setFillTransparent: 93,
  rawFillPath: 94
};
const DrawOPS = {
  moveTo: 0,
  lineTo: 1,
  curveTo: 2,
  quadraticCurveTo: 3,
  closePath: 4
};
const PasswordResponses = (/* unused pure expression or super */ null && ({
  NEED_PASSWORD: 1,
  INCORRECT_PASSWORD: 2
}));
let verbosity = VerbosityLevel.WARNINGS;
function setVerbosityLevel(level) {
  if (Number.isInteger(level)) {
    verbosity = level;
  }
}
function getVerbosityLevel() {
  return verbosity;
}
function info(msg) {
  if (verbosity >= VerbosityLevel.INFOS) {
    console.info(`Info: ${msg}`);
  }
}
function warn(msg) {
  if (verbosity >= VerbosityLevel.WARNINGS) {
    console.warn(`Warning: ${msg}`);
  }
}
function unreachable(msg) {
  throw new Error(msg);
}
function assert(cond, msg) {
  if (!cond) {
    unreachable(msg);
  }
}
function _isValidProtocol(url) {
  switch (url?.protocol) {
    case "http:":
    case "https:":
    case "ftp:":
    case "mailto:":
    case "tel:":
      return true;
    default:
      return false;
  }
}
function createValidAbsoluteUrl(url, baseUrl = null, options = null) {
  if (!url) {
    return null;
  }
  if (options && typeof url === "string") {
    if (options.addDefaultProtocol && url.startsWith("www.")) {
      const dots = url.match(/\./g);
      if (dots?.length >= 2) {
        url = `http://${url}`;
      }
    }
    if (options.tryConvertEncoding) {
      try {
        url = stringToUTF8String(url);
      } catch {}
    }
  }
  const absoluteUrl = baseUrl ? URL.parse(url, baseUrl) : URL.parse(url);
  return _isValidProtocol(absoluteUrl) ? absoluteUrl : null;
}
function updateUrlHash(url, hash, allowRel = false) {
  const res = URL.parse(url);
  if (res) {
    res.hash = hash;
    return res.href;
  }
  if (allowRel && createValidAbsoluteUrl(url, "http://example.com")) {
    return url.split("#", 1)[0] + `${hash ? `#${hash}` : ""}`;
  }
  return "";
}
function stripPath(str) {
  return str.substring(str.lastIndexOf("/") + 1);
}
function shadow(obj, prop, value, nonSerializable = false) {
  Object.defineProperty(obj, prop, {
    value,
    enumerable: !nonSerializable,
    configurable: true,
    writable: false
  });
  return value;
}
const BaseException = function BaseExceptionClosure() {
  function BaseException(message, name) {
    this.message = message;
    this.name = name;
  }
  BaseException.prototype = new Error();
  BaseException.constructor = BaseException;
  return BaseException;
}();
class PasswordException extends BaseException {
  constructor(msg, code) {
    super(msg, "PasswordException");
    this.code = code;
  }
}
class UnknownErrorException extends BaseException {
  constructor(msg, details) {
    super(msg, "UnknownErrorException");
    this.details = details;
  }
}
class InvalidPDFException extends BaseException {
  constructor(msg) {
    super(msg, "InvalidPDFException");
  }
}
class ResponseException extends BaseException {
  constructor(msg, status, missing) {
    super(msg, "ResponseException");
    this.status = status;
    this.missing = missing;
  }
}
class FormatError extends BaseException {
  constructor(msg) {
    super(msg, "FormatError");
  }
}
class AbortException extends BaseException {
  constructor(msg) {
    super(msg, "AbortException");
  }
}
function bytesToString(bytes) {
  if (typeof bytes !== "object" || bytes?.length === undefined) {
    unreachable("Invalid argument for bytesToString");
  }
  const length = bytes.length;
  const MAX_ARGUMENT_COUNT = 8192;
  if (length < MAX_ARGUMENT_COUNT) {
    return String.fromCharCode.apply(null, bytes);
  }
  const strBuf = [];
  for (let i = 0; i < length; i += MAX_ARGUMENT_COUNT) {
    const chunkEnd = Math.min(i + MAX_ARGUMENT_COUNT, length);
    const chunk = bytes.subarray(i, chunkEnd);
    strBuf.push(String.fromCharCode.apply(null, chunk));
  }
  return strBuf.join("");
}
function stringToBytes(str) {
  if (typeof str !== "string") {
    unreachable("Invalid argument for stringToBytes");
  }
  const length = str.length;
  const bytes = new Uint8Array(length);
  for (let i = 0; i < length; ++i) {
    bytes[i] = str.charCodeAt(i) & 0xff;
  }
  return bytes;
}
class FeatureTest {
  static get isLittleEndian() {
    const buffer8 = new Uint8Array(4);
    buffer8[0] = 1;
    const view32 = new Uint32Array(buffer8.buffer, 0, 1);
    return shadow(this, "isLittleEndian", view32[0] === 1);
  }
  static get isOffscreenCanvasSupported() {
    return shadow(this, "isOffscreenCanvasSupported", typeof OffscreenCanvas !== "undefined");
  }
  static get isImageDecoderSupported() {
    return shadow(this, "isImageDecoderSupported", typeof ImageDecoder !== "undefined");
  }
  static get isVideoFrameSupported() {
    return shadow(this, "isVideoFrameSupported", typeof VideoFrame !== "undefined");
  }
  static get isFloat16ArraySupported() {
    return shadow(this, "isFloat16ArraySupported", typeof Float16Array !== "undefined");
  }
  static get isSanitizerSupported() {
    return shadow(this, "isSanitizerSupported", typeof Sanitizer !== "undefined");
  }
  static get platform() {
    const {
      platform,
      userAgent
    } = navigator;
    return shadow(this, "platform", {
      isAndroid: userAgent.includes("Android"),
      isLinux: platform.includes("Linux"),
      isMac: platform.includes("Mac"),
      isWindows: platform.includes("Win"),
      isFirefox: true
    });
  }
  static get isCanvasFilterSupported() {
    let ctx;
    if (this.isOffscreenCanvasSupported) {
      ctx = new OffscreenCanvas(1, 1).getContext("2d");
    }
    return shadow(this, "isCanvasFilterSupported", ctx?.filter !== undefined);
  }
  static get isAlphaColorInputSupported() {
    return shadow(this, "isAlphaColorInputSupported", false);
  }
  static get isBackdropFilterSupported() {
    return shadow(this, "isBackdropFilterSupported", typeof CSS !== "undefined" && CSS.supports("backdrop-filter", "blur(1px)"));
  }
}
class Util {
  static get hexNums() {
    return shadow(this, "hexNums", Array.from({
      length: 256
    }, (_, n) => n.toString(16).padStart(2, "0")));
  }
  static makeHexColor(r, g, b) {
    return `#${this.hexNums[r]}${this.hexNums[g]}${this.hexNums[b]}`;
  }
  static get identity0to256Map() {
    return shadow(this, "identity0to256Map", Uint8Array.from({
      length: 256
    }, (_, i) => i));
  }
  static transform(m1, m2) {
    return [m1[0] * m2[0] + m1[2] * m2[1], m1[1] * m2[0] + m1[3] * m2[1], m1[0] * m2[2] + m1[2] * m2[3], m1[1] * m2[2] + m1[3] * m2[3], m1[0] * m2[4] + m1[2] * m2[5] + m1[4], m1[1] * m2[4] + m1[3] * m2[5] + m1[5]];
  }
  static multiplyByDOMMatrix(m, md) {
    return [m[0] * md.a + m[2] * md.b, m[1] * md.a + m[3] * md.b, m[0] * md.c + m[2] * md.d, m[1] * md.c + m[3] * md.d, m[0] * md.e + m[2] * md.f + m[4], m[1] * md.e + m[3] * md.f + m[5]];
  }
  static applyTransform(p, m, pos = 0) {
    const p0 = p[pos];
    const p1 = p[pos + 1];
    p[pos] = p0 * m[0] + p1 * m[2] + m[4];
    p[pos + 1] = p0 * m[1] + p1 * m[3] + m[5];
  }
  static applyTransformToBezier(p, transform, pos = 0) {
    const m0 = transform[0];
    const m1 = transform[1];
    const m2 = transform[2];
    const m3 = transform[3];
    const m4 = transform[4];
    const m5 = transform[5];
    for (let i = 0; i < 6; i += 2) {
      const pI = p[pos + i];
      const pI1 = p[pos + i + 1];
      p[pos + i] = pI * m0 + pI1 * m2 + m4;
      p[pos + i + 1] = pI * m1 + pI1 * m3 + m5;
    }
  }
  static applyInverseTransform(p, m) {
    const p0 = p[0];
    const p1 = p[1];
    const d = m[0] * m[3] - m[1] * m[2];
    p[0] = (p0 * m[3] - p1 * m[2] + m[2] * m[5] - m[4] * m[3]) / d;
    p[1] = (-p0 * m[1] + p1 * m[0] + m[4] * m[1] - m[5] * m[0]) / d;
  }
  static axialAlignedBoundingBox(rect, transform, output) {
    const m0 = transform[0];
    const m1 = transform[1];
    const m2 = transform[2];
    const m3 = transform[3];
    const m4 = transform[4];
    const m5 = transform[5];
    const r0 = rect[0];
    const r1 = rect[1];
    const r2 = rect[2];
    const r3 = rect[3];
    let a0 = m0 * r0 + m4;
    let a2 = a0;
    let a1 = m0 * r2 + m4;
    let a3 = a1;
    let b0 = m3 * r1 + m5;
    let b2 = b0;
    let b1 = m3 * r3 + m5;
    let b3 = b1;
    if (m1 !== 0 || m2 !== 0) {
      const m1r0 = m1 * r0;
      const m1r2 = m1 * r2;
      const m2r1 = m2 * r1;
      const m2r3 = m2 * r3;
      a0 += m2r1;
      a3 += m2r1;
      a1 += m2r3;
      a2 += m2r3;
      b0 += m1r0;
      b3 += m1r0;
      b1 += m1r2;
      b2 += m1r2;
    }
    output[0] = Math.min(output[0], a0, a1, Math.min(a2, a3));
    output[1] = Math.min(output[1], b0, b1, Math.min(b2, b3));
    output[2] = Math.max(output[2], a0, a1, Math.max(a2, a3));
    output[3] = Math.max(output[3], b0, b1, Math.max(b2, b3));
  }
  static inverseTransform(m) {
    const d = m[0] * m[3] - m[1] * m[2];
    return [m[3] / d, -m[1] / d, -m[2] / d, m[0] / d, (m[2] * m[5] - m[4] * m[3]) / d, (m[4] * m[1] - m[5] * m[0]) / d];
  }
  static singularValueDecompose2dScale(matrix, output) {
    const m0 = matrix[0];
    const m1 = matrix[1];
    const m2 = matrix[2];
    const m3 = matrix[3];
    const a = m0 ** 2 + m1 ** 2;
    const b = m0 * m2 + m1 * m3;
    const c = m2 ** 2 + m3 ** 2;
    const first = (a + c) / 2;
    const second = Math.sqrt(first ** 2 - (a * c - b ** 2));
    output[0] = Math.sqrt(first + second || 1);
    output[1] = Math.sqrt(first - second || 1);
  }
  static normalizeRect(rect) {
    const r = rect.slice(0);
    if (rect[0] > rect[2]) {
      r[0] = rect[2];
      r[2] = rect[0];
    }
    if (rect[1] > rect[3]) {
      r[1] = rect[3];
      r[3] = rect[1];
    }
    return r;
  }
  static intersect(rect1, rect2) {
    const xLow = Math.max(Math.min(rect1[0], rect1[2]), Math.min(rect2[0], rect2[2]));
    const xHigh = Math.min(Math.max(rect1[0], rect1[2]), Math.max(rect2[0], rect2[2]));
    if (xLow > xHigh) {
      return null;
    }
    const yLow = Math.max(Math.min(rect1[1], rect1[3]), Math.min(rect2[1], rect2[3]));
    const yHigh = Math.min(Math.max(rect1[1], rect1[3]), Math.max(rect2[1], rect2[3]));
    return yLow > yHigh ? null : [xLow, yLow, xHigh, yHigh];
  }
  static pointBoundingBox(x, y, minMax) {
    minMax[0] = Math.min(minMax[0], x);
    minMax[1] = Math.min(minMax[1], y);
    minMax[2] = Math.max(minMax[2], x);
    minMax[3] = Math.max(minMax[3], y);
  }
  static rectBoundingBox(x0, y0, x1, y1, minMax) {
    minMax[0] = Math.min(minMax[0], x0, x1);
    minMax[1] = Math.min(minMax[1], y0, y1);
    minMax[2] = Math.max(minMax[2], x0, x1);
    minMax[3] = Math.max(minMax[3], y0, y1);
  }
  static #getExtremumOnCurve(x0, x1, x2, x3, y0, y1, y2, y3, t, minMax) {
    if (t <= 0 || t >= 1) {
      return;
    }
    const mt = 1 - t;
    const tt = t * t;
    const ttt = tt * t;
    const x = mt * (mt * (mt * x0 + 3 * t * x1) + 3 * tt * x2) + ttt * x3;
    const y = mt * (mt * (mt * y0 + 3 * t * y1) + 3 * tt * y2) + ttt * y3;
    minMax[0] = Math.min(minMax[0], x);
    minMax[1] = Math.min(minMax[1], y);
    minMax[2] = Math.max(minMax[2], x);
    minMax[3] = Math.max(minMax[3], y);
  }
  static #getExtremum(x0, x1, x2, x3, y0, y1, y2, y3, a, b, c, minMax) {
    if (Math.abs(a) < 1e-12) {
      if (Math.abs(b) >= 1e-12) {
        this.#getExtremumOnCurve(x0, x1, x2, x3, y0, y1, y2, y3, -c / b, minMax);
      }
      return;
    }
    const delta = b ** 2 - 4 * c * a;
    if (delta < 0) {
      return;
    }
    const sqrtDelta = Math.sqrt(delta);
    const a2 = 2 * a;
    this.#getExtremumOnCurve(x0, x1, x2, x3, y0, y1, y2, y3, (-b + sqrtDelta) / a2, minMax);
    this.#getExtremumOnCurve(x0, x1, x2, x3, y0, y1, y2, y3, (-b - sqrtDelta) / a2, minMax);
  }
  static bezierBoundingBox(x0, y0, x1, y1, x2, y2, x3, y3, minMax) {
    minMax[0] = Math.min(minMax[0], x0, x3);
    minMax[1] = Math.min(minMax[1], y0, y3);
    minMax[2] = Math.max(minMax[2], x0, x3);
    minMax[3] = Math.max(minMax[3], y0, y3);
    this.#getExtremum(x0, x1, x2, x3, y0, y1, y2, y3, 3 * (-x0 + 3 * (x1 - x2) + x3), 6 * (x0 - 2 * x1 + x2), 3 * (x1 - x0), minMax);
    this.#getExtremum(x0, x1, x2, x3, y0, y1, y2, y3, 3 * (-y0 + 3 * (y1 - y2) + y3), 6 * (y0 - 2 * y1 + y2), 3 * (y1 - y0), minMax);
  }
}
function stringToUTF8String(str) {
  return decodeURIComponent(escape(str));
}
function utf8StringToString(str) {
  return unescape(encodeURIComponent(str));
}
function isArrayEqual(arr1, arr2) {
  if (arr1.length !== arr2.length) {
    return false;
  }
  for (let i = 0, ii = arr1.length; i < ii; i++) {
    if (arr1[i] !== arr2[i]) {
      return false;
    }
  }
  return true;
}
let NormalizeRegex = null;
let NormalizationMap = null;
function normalizeUnicode(str) {
  if (!NormalizeRegex) {
    NormalizeRegex = /([\u00a0\u00b5\u037e\u0eb3\u2000-\u200a\u202f\u2126\ufb00-\ufb04\ufb06\ufb20-\ufb36\ufb38-\ufb3c\ufb3e\ufb40\ufb41\ufb43\ufb44\ufb46-\ufba1\ufba4-\ufba9\ufbae-\ufbb1\ufbd3-\ufbdc\ufbde-\ufbe7\ufbea-\ufbf8\ufbfc\ufbfd\ufc00-\ufc5d\ufc64-\ufcf1\ufcf5-\ufd3d\ufd88\ufdf4\ufdfa\ufdfb\ufe71\ufe77\ufe79\ufe7b\ufe7d]+)|(\ufb05+)/gu;
    NormalizationMap = new Map([["ﬅ", "ſt"]]);
  }
  return str.replaceAll(NormalizeRegex, (_, p1, p2) => p1 ? p1.normalize("NFKC") : NormalizationMap.get(p2));
}
function getUuid() {
  if (typeof crypto.randomUUID === "function") {
    return crypto.randomUUID();
  }
  const buf = new Uint8Array(32);
  crypto.getRandomValues(buf);
  return bytesToString(buf);
}
function _isValidExplicitDest(validRef, validName, dest) {
  if (!Array.isArray(dest) || dest.length < 2) {
    return false;
  }
  const [page, zoom, ...args] = dest;
  if (!validRef(page) && !Number.isInteger(page)) {
    return false;
  }
  if (!validName(zoom)) {
    return false;
  }
  const argsLen = args.length;
  let allowNull = true;
  switch (zoom.name) {
    case "XYZ":
      if (argsLen < 2 || argsLen > 3) {
        return false;
      }
      break;
    case "Fit":
    case "FitB":
      return argsLen === 0;
    case "FitH":
    case "FitBH":
    case "FitV":
    case "FitBV":
      if (argsLen > 1) {
        return false;
      }
      break;
    case "FitR":
      if (argsLen !== 4) {
        return false;
      }
      allowNull = false;
      break;
    default:
      return false;
  }
  for (const arg of args) {
    if (typeof arg === "number" || allowNull && arg === null) {
      continue;
    }
    return false;
  }
  return true;
}
const makeArr = () => [];
const makeMap = () => new Map();
const makeObj = () => Object.create(null);
const makeSet = () => new Set();
if (typeof Iterator.prototype.join !== "function") {
  Iterator.prototype.join = function (separator) {
    return [...this].join(separator);
  };
}

;// ./src/display/display_utils.js
/* unused harmony import specifier */ var display_utils_warn;
/* unused harmony import specifier */ var display_utils_Util;
/* unused harmony import specifier */ var MathClamp;


class PixelsPerInch {
  static CSS = 96.0;
  static PDF = 72.0;
  static PDF_TO_CSS_UNITS = this.CSS / this.PDF;
}
class RenderingCancelledException extends BaseException {
  constructor(msg, extraDelay = 0) {
    super(msg, "RenderingCancelledException");
    this.extraDelay = extraDelay;
  }
}
function getRGBA(color) {
  if (color.startsWith("#")) {
    const hex = color.slice(1);
    return [parseInt(hex.slice(0, 2), 16), parseInt(hex.slice(2, 4), 16), parseInt(hex.slice(4, 6), 16), hex.length >= 8 ? parseInt(hex.slice(6, 8), 16) / 255 : 1];
  }
  if (color.startsWith("rgb(")) {
    const [r, g, b] = color.slice(4, -1).split(",").map(x => parseInt(x, 10));
    return [r, g, b, 1];
  }
  if (color.startsWith("rgba(")) {
    const parts = color.slice(5, -1).split(",");
    return [parseInt(parts[0], 10), parseInt(parts[1], 10), parseInt(parts[2], 10), parseFloat(parts[3])];
  }
  const m = color.match(/^color\(srgb\s+([\d.]+)\s+([\d.]+)\s+([\d.]+)(?:\s*\/\s*([\d.]+|none))?\)$/);
  if (m) {
    return [Math.round(parseFloat(m[1]) * 255), Math.round(parseFloat(m[2]) * 255), Math.round(parseFloat(m[3]) * 255), m[4] !== undefined && m[4] !== "none" ? parseFloat(m[4]) : 1];
  }
  return null;
}
function getRGB(color) {
  const rgba = getRGBA(color);
  if (!rgba) {
    display_utils_warn(`Not a valid color format: "${color}"`);
    return [0, 0, 0];
  }
  return rgba.slice(0, 3);
}
function getCurrentTransform(ctx) {
  const {
    a,
    b,
    c,
    d,
    e,
    f
  } = ctx.getTransform();
  return [a, b, c, d, e, f];
}
function getCurrentTransformInverse(ctx) {
  const {
    a,
    b,
    c,
    d,
    e,
    f
  } = ctx.getTransform().invertSelf();
  return [a, b, c, d, e, f];
}
class OutputScale {
  constructor() {
    const {
      pixelRatio
    } = OutputScale;
    this.sx = pixelRatio;
    this.sy = pixelRatio;
  }
  get scaled() {
    return this.sx !== 1 || this.sy !== 1;
  }
  get symmetric() {
    return this.sx === this.sy;
  }
  limitCanvas(width, height, maxPixels, maxDim, capAreaFactor = -1) {
    return false;
  }
  static get pixelRatio() {
    return globalThis.devicePixelRatio || 1;
  }
  static capPixels(maxPixels, capAreaFactor) {
    return maxPixels;
  }
}
function applyOpacity(color, opacity) {
  opacity = MathClamp(opacity ?? 1, 0, 1);
  const white = 255 * (1 - opacity);
  return color.map(c => Math.round(c * opacity + white));
}
function RGBToHSL(rgb, output) {
  const r = rgb[0] / 255;
  const g = rgb[1] / 255;
  const b = rgb[2] / 255;
  const max = Math.max(r, g, b);
  const min = Math.min(r, g, b);
  const l = (max + min) / 2;
  if (max === min) {
    output[0] = output[1] = 0;
  } else {
    const d = max - min;
    output[1] = l < 0.5 ? d / (max + min) : d / (2 - max - min);
    switch (max) {
      case r:
        output[0] = ((g - b) / d + (g < b ? 6 : 0)) * 60;
        break;
      case g:
        output[0] = ((b - r) / d + 2) * 60;
        break;
      case b:
        output[0] = ((r - g) / d + 4) * 60;
        break;
    }
  }
  output[2] = l;
}
function HSLToRGB(hsl, output) {
  const h = hsl[0];
  const s = hsl[1];
  const l = hsl[2];
  const c = (1 - Math.abs(2 * l - 1)) * s;
  const x = c * (1 - Math.abs(h / 60 % 2 - 1));
  const m = l - c / 2;
  switch (Math.floor(h / 60)) {
    case 0:
      output[0] = c + m;
      output[1] = x + m;
      output[2] = m;
      break;
    case 1:
      output[0] = x + m;
      output[1] = c + m;
      output[2] = m;
      break;
    case 2:
      output[0] = m;
      output[1] = c + m;
      output[2] = x + m;
      break;
    case 3:
      output[0] = m;
      output[1] = x + m;
      output[2] = c + m;
      break;
    case 4:
      output[0] = x + m;
      output[1] = m;
      output[2] = c + m;
      break;
    case 5:
    case 6:
      output[0] = c + m;
      output[1] = m;
      output[2] = x + m;
      break;
  }
}
function computeLuminance(x) {
  return x <= 0.03928 ? x / 12.92 : ((x + 0.055) / 1.055) ** 2.4;
}
function contrastRatio(hsl1, hsl2, output) {
  HSLToRGB(hsl1, output);
  output.map(computeLuminance);
  const lum1 = 0.2126 * output[0] + 0.7152 * output[1] + 0.0722 * output[2];
  HSLToRGB(hsl2, output);
  output.map(computeLuminance);
  const lum2 = 0.2126 * output[0] + 0.7152 * output[1] + 0.0722 * output[2];
  return lum1 > lum2 ? (lum1 + 0.05) / (lum2 + 0.05) : (lum2 + 0.05) / (lum1 + 0.05);
}
const contrastCache = new Map();
function findContrastColor(baseColor, fixedColor) {
  const key = baseColor[0] + baseColor[1] * 0x100 + baseColor[2] * 0x10000 + fixedColor[0] * 0x1000000 + fixedColor[1] * 0x100000000 + fixedColor[2] * 0x10000000000;
  let cachedValue = contrastCache.get(key);
  if (cachedValue) {
    return cachedValue;
  }
  const array = new Float32Array(9);
  const output = array.subarray(0, 3);
  const baseHSL = array.subarray(3, 6);
  RGBToHSL(baseColor, baseHSL);
  const fixedHSL = array.subarray(6, 9);
  RGBToHSL(fixedColor, fixedHSL);
  const isFixedColorDark = fixedHSL[2] < 0.5;
  const minContrast = isFixedColorDark ? 12 : 4.5;
  baseHSL[2] = isFixedColorDark ? Math.sqrt(baseHSL[2]) : 1 - Math.sqrt(1 - baseHSL[2]);
  if (contrastRatio(baseHSL, fixedHSL, output) < minContrast) {
    let start, end;
    if (isFixedColorDark) {
      start = baseHSL[2];
      end = 1;
    } else {
      start = 0;
      end = baseHSL[2];
    }
    const PRECISION = 0.005;
    while (end - start > PRECISION) {
      const mid = baseHSL[2] = (start + end) / 2;
      if (isFixedColorDark === contrastRatio(baseHSL, fixedHSL, output) < minContrast) {
        start = mid;
      } else {
        end = mid;
      }
    }
    baseHSL[2] = isFixedColorDark ? end : start;
  }
  HSLToRGB(baseHSL, output);
  cachedValue = display_utils_Util.makeHexColor(Math.round(output[0] * 255), Math.round(output[1] * 255), Math.round(output[2] * 255));
  contrastCache.set(key, cachedValue);
  return cachedValue;
}
function makePathFromDrawOPS(data) {
  const path = new Path2D();
  if (!data) {
    return path;
  }
  for (let i = 0, ii = data.length; i < ii;) {
    switch (data[i++]) {
      case DrawOPS.moveTo:
        path.moveTo(data[i++], data[i++]);
        break;
      case DrawOPS.lineTo:
        path.lineTo(data[i++], data[i++]);
        break;
      case DrawOPS.curveTo:
        path.bezierCurveTo(data[i++], data[i++], data[i++], data[i++], data[i++], data[i++]);
        break;
      case DrawOPS.quadraticCurveTo:
        path.quadraticCurveTo(data[i++], data[i++], data[i++], data[i++]);
        break;
      case DrawOPS.closePath:
        path.closePath();
        break;
      default:
        warn(`Unrecognized drawing path operator: ${data[i - 1]}`);
        break;
    }
  }
  return path;
}

;// ./src/shared/math_clamp.js
function math_clamp_MathClamp(v, min, max) {
  return Math.min(Math.max(v, min), max);
}

;// ./src/display/canvas_dependency_tracker.js



const FORCED_DEPENDENCY_LABEL = "__forcedDependency";
const {
  floor,
  ceil
} = Math;
function expandBBox(array, index, minX, minY, maxX, maxY) {
  array[index * 4 + 0] = Math.min(array[index * 4 + 0], minX);
  array[index * 4 + 1] = Math.min(array[index * 4 + 1], minY);
  array[index * 4 + 2] = Math.max(array[index * 4 + 2], maxX);
  array[index * 4 + 3] = Math.max(array[index * 4 + 3], maxY);
}
function scaleCharBBox(scaleX, scaleY, x, y, bbox) {
  let temp;
  if (scaleX) {
    if (scaleX < 0) {
      temp = bbox[0];
      bbox[0] = bbox[2];
      bbox[2] = temp;
    }
    bbox[0] *= scaleX;
    bbox[2] *= scaleX;
    if (scaleY < 0) {
      temp = bbox[1];
      bbox[1] = bbox[3];
      bbox[3] = temp;
    }
    bbox[1] *= scaleY;
    bbox[3] *= scaleY;
  } else {
    bbox.fill(0);
  }
  bbox[0] += x;
  bbox[1] += y;
  bbox[2] += x;
  bbox[3] += y;
}
const EMPTY_BBOX = new Uint32Array(new Uint8Array([255, 255, 0, 0]).buffer)[0];
class BBoxReader {
  #bboxes;
  #coords;
  constructor(bboxes, coords) {
    this.#bboxes = bboxes;
    this.#coords = coords;
  }
  static fromBuffer(buffer) {
    return new BBoxReader(new Uint32Array(buffer), new Uint8ClampedArray(buffer));
  }
  get buffer() {
    return this.#bboxes.buffer;
  }
  get length() {
    return this.#bboxes.length;
  }
  isEmpty(i) {
    return this.#bboxes[i] === EMPTY_BBOX;
  }
  minX(i) {
    return this.#coords[i * 4 + 0] / 256;
  }
  minY(i) {
    return this.#coords[i * 4 + 1] / 256;
  }
  maxX(i) {
    return (this.#coords[i * 4 + 2] + 1) / 256;
  }
  maxY(i) {
    return (this.#coords[i * 4 + 3] + 1) / 256;
  }
}
const ensureDebugMetadata = (map, key) => map?.getOrInsertComputed(key, () => ({
  dependencies: new Set(),
  isRenderingOperation: false
}));
class CanvasBBoxTracker {
  #baseTransformStack = [[1, 0, 0, 1, 0, 0]];
  #clipBox = [-Infinity, -Infinity, Infinity, Infinity];
  #pendingBBox = new Float64Array(BBOX_INIT);
  _pendingBBoxIdx = -1;
  #canvasWidth;
  #canvasHeight;
  #bboxesCoords;
  #bboxes;
  _savesStack = [];
  _markedContentStack = [];
  constructor(canvas, operationsCount) {
    this.#canvasWidth = canvas.width;
    this.#canvasHeight = canvas.height;
    this.#initializeBBoxes(operationsCount);
  }
  growOperationsCount(operationsCount) {
    if (operationsCount >= this.#bboxes.length) {
      this.#initializeBBoxes(operationsCount, this.#bboxes);
    }
  }
  #initializeBBoxes(operationsCount, oldBBoxes) {
    const buffer = new ArrayBuffer(operationsCount * 4);
    this.#bboxesCoords = new Uint8ClampedArray(buffer);
    this.#bboxes = new Uint32Array(buffer);
    if (oldBBoxes && oldBBoxes.length > 0) {
      this.#bboxes.set(oldBBoxes);
      this.#bboxes.fill(EMPTY_BBOX, oldBBoxes.length);
    } else {
      this.#bboxes.fill(EMPTY_BBOX);
    }
  }
  get clipBox() {
    return this.#clipBox;
  }
  save(opIdx) {
    this.#clipBox = {
      __proto__: this.#clipBox
    };
    this._savesStack.push(opIdx);
    return this;
  }
  restore(opIdx, onSavePopped) {
    const previous = Object.getPrototypeOf(this.#clipBox);
    if (previous === null) {
      return this;
    }
    this.#clipBox = previous;
    const lastSave = this._savesStack.pop();
    if (lastSave !== undefined) {
      onSavePopped?.(lastSave, opIdx);
      this.#bboxes[opIdx] = this.#bboxes[lastSave];
    }
    return this;
  }
  recordOpenMarker(idx) {
    this._savesStack.push(idx);
    return this;
  }
  getOpenMarker() {
    return this._savesStack.length === 0 ? null : this._savesStack.at(-1);
  }
  recordCloseMarker(opIdx, onSavePopped) {
    const lastSave = this._savesStack.pop();
    if (lastSave !== undefined) {
      onSavePopped?.(lastSave, opIdx);
      this.#bboxes[opIdx] = this.#bboxes[lastSave];
    }
    return this;
  }
  beginMarkedContent(opIdx) {
    this._markedContentStack.push(opIdx);
    return this;
  }
  endMarkedContent(opIdx, onSavePopped) {
    const lastSave = this._markedContentStack.pop();
    if (lastSave !== undefined) {
      onSavePopped?.(lastSave, opIdx);
      this.#bboxes[opIdx] = this.#bboxes[lastSave];
    }
    return this;
  }
  pushBaseTransform(ctx) {
    this.#baseTransformStack.push(Util.multiplyByDOMMatrix(this.#baseTransformStack.at(-1), ctx.getTransform()));
    return this;
  }
  popBaseTransform() {
    if (this.#baseTransformStack.length > 1) {
      this.#baseTransformStack.pop();
    }
    return this;
  }
  resetBBox(idx) {
    if (this._pendingBBoxIdx !== idx) {
      this._pendingBBoxIdx = idx;
      this.#pendingBBox.set(BBOX_INIT, 0);
    }
    return this;
  }
  recordClipBox(idx, ctx, minX, maxX, minY, maxY) {
    const transform = Util.multiplyByDOMMatrix(this.#baseTransformStack.at(-1), ctx.getTransform());
    const clipBox = BBOX_INIT.slice();
    Util.axialAlignedBoundingBox([minX, minY, maxX, maxY], transform, clipBox);
    const intersection = Util.intersect(this.#clipBox, clipBox);
    if (intersection) {
      this.#clipBox[0] = intersection[0];
      this.#clipBox[1] = intersection[1];
      this.#clipBox[2] = intersection[2];
      this.#clipBox[3] = intersection[3];
    } else {
      this.#clipBox[0] = this.#clipBox[1] = Infinity;
      this.#clipBox[2] = this.#clipBox[3] = -Infinity;
    }
    return this;
  }
  recordBBox(idx, ctx, minX, maxX, minY, maxY) {
    const clipBox = this.#clipBox;
    if (clipBox[0] === Infinity) {
      return this;
    }
    const transform = Util.multiplyByDOMMatrix(this.#baseTransformStack.at(-1), ctx.getTransform());
    if (clipBox[0] === -Infinity) {
      Util.axialAlignedBoundingBox([minX, minY, maxX, maxY], transform, this.#pendingBBox);
      return this;
    }
    const bbox = BBOX_INIT.slice();
    Util.axialAlignedBoundingBox([minX, minY, maxX, maxY], transform, bbox);
    this.#pendingBBox[0] = math_clamp_MathClamp(bbox[0], clipBox[0], this.#pendingBBox[0]);
    this.#pendingBBox[1] = math_clamp_MathClamp(bbox[1], clipBox[1], this.#pendingBBox[1]);
    this.#pendingBBox[2] = math_clamp_MathClamp(bbox[2], this.#pendingBBox[2], clipBox[2]);
    this.#pendingBBox[3] = math_clamp_MathClamp(bbox[3], this.#pendingBBox[3], clipBox[3]);
    return this;
  }
  recordFullPageBBox(idx) {
    this.#pendingBBox[0] = Math.max(0, this.#clipBox[0]);
    this.#pendingBBox[1] = Math.max(0, this.#clipBox[1]);
    this.#pendingBBox[2] = Math.min(this.#canvasWidth, this.#clipBox[2]);
    this.#pendingBBox[3] = Math.min(this.#canvasHeight, this.#clipBox[3]);
    return this;
  }
  recordOperation(idx, preserve = false, dependencyLists) {
    if (this._pendingBBoxIdx !== idx) {
      return this;
    }
    const minX = floor(this.#pendingBBox[0] * 256 / this.#canvasWidth);
    const minY = floor(this.#pendingBBox[1] * 256 / this.#canvasHeight);
    const maxX = ceil(this.#pendingBBox[2] * 256 / this.#canvasWidth);
    const maxY = ceil(this.#pendingBBox[3] * 256 / this.#canvasHeight);
    expandBBox(this.#bboxesCoords, idx, minX, minY, maxX, maxY);
    if (dependencyLists) {
      for (const dependencies of dependencyLists) {
        for (const depIdx of dependencies) {
          if (depIdx !== idx) {
            expandBBox(this.#bboxesCoords, depIdx, minX, minY, maxX, maxY);
          }
        }
      }
    }
    if (!preserve) {
      this._pendingBBoxIdx = -1;
    }
    return this;
  }
  bboxToClipBoxDropOperation(idx) {
    if (this._pendingBBoxIdx === idx) {
      this._pendingBBoxIdx = -1;
      this.#clipBox[0] = Math.max(this.#clipBox[0], this.#pendingBBox[0]);
      this.#clipBox[1] = Math.max(this.#clipBox[1], this.#pendingBBox[1]);
      this.#clipBox[2] = Math.min(this.#clipBox[2], this.#pendingBBox[2]);
      this.#clipBox[3] = Math.min(this.#clipBox[3], this.#pendingBBox[3]);
    }
    return this;
  }
  take() {
    return new BBoxReader(this.#bboxes, this.#bboxesCoords);
  }
  takeDebugMetadata() {
    throw new Error("Unreachable");
  }
  recordSimpleData(name, idx) {
    return this;
  }
  recordIncrementalData(name, idx) {
    return this;
  }
  resetIncrementalData(name, idx) {
    return this;
  }
  recordNamedData(name, idx) {
    return this;
  }
  recordSimpleDataFromNamed(name, depName, fallbackIdx) {
    return this;
  }
  recordFutureForcedDependency(name, idx) {
    return this;
  }
  inheritSimpleDataAsFutureForcedDependencies(names) {
    return this;
  }
  inheritPendingDependenciesAsFutureForcedDependencies() {
    return this;
  }
  recordCharacterBBox(idx, ctx, font, scale = 1, x = 0, y = 0, getMeasure) {
    return this;
  }
  getSimpleIndex(dependencyName) {
    return undefined;
  }
  recordDependencies(idx, dependencyNames) {
    return this;
  }
  recordNamedDependency(idx, name) {
    return this;
  }
  recordShowTextOperation(idx, preserve = false) {
    return this;
  }
}
class CanvasDependencyTracker {
  #simple = {
    __proto__: null
  };
  #incremental = {
    __proto__: null,
    transform: [],
    moveText: [],
    sameLineText: [],
    [FORCED_DEPENDENCY_LABEL]: []
  };
  #namedDependencies = new Map();
  #pendingDependencies = new Set();
  #fontBBoxTrustworthy = new Map();
  #debugMetadata;
  #recordDebugMetadataDepenencyAfterRestore;
  #bboxTracker;
  constructor(bboxTracker, recordDebugMetadata = false) {
    this.#bboxTracker = bboxTracker;
    if (recordDebugMetadata) {
      this.#debugMetadata = new Map();
      this.#recordDebugMetadataDepenencyAfterRestore = (lastSave, opIdx) => {
        ensureDebugMetadata(this.#debugMetadata, opIdx).dependencies.add(lastSave);
      };
    }
  }
  get clipBox() {
    return this.#bboxTracker.clipBox;
  }
  growOperationsCount(operationsCount) {
    this.#bboxTracker.growOperationsCount(operationsCount);
  }
  save(opIdx) {
    this.#simple = {
      __proto__: this.#simple
    };
    this.#incremental = {
      __proto__: this.#incremental,
      transform: {
        __proto__: this.#incremental.transform
      },
      moveText: {
        __proto__: this.#incremental.moveText
      },
      sameLineText: {
        __proto__: this.#incremental.sameLineText
      },
      [FORCED_DEPENDENCY_LABEL]: {
        __proto__: this.#incremental[FORCED_DEPENDENCY_LABEL]
      }
    };
    this.#bboxTracker.save(opIdx);
    return this;
  }
  restore(opIdx) {
    this.#bboxTracker.restore(opIdx, this.#recordDebugMetadataDepenencyAfterRestore);
    const previous = Object.getPrototypeOf(this.#simple);
    if (previous === null) {
      return this;
    }
    this.#simple = previous;
    this.#incremental = Object.getPrototypeOf(this.#incremental);
    return this;
  }
  recordOpenMarker(opIdx) {
    this.#bboxTracker.recordOpenMarker(opIdx, this.#recordDebugMetadataDepenencyAfterRestore);
    return this;
  }
  getOpenMarker() {
    return this.#bboxTracker.getOpenMarker();
  }
  recordCloseMarker(opIdx) {
    this.#bboxTracker.recordCloseMarker(opIdx, this.#recordDebugMetadataDepenencyAfterRestore);
    return this;
  }
  beginMarkedContent(opIdx) {
    this.#bboxTracker.beginMarkedContent(opIdx);
    return this;
  }
  endMarkedContent(opIdx) {
    this.#bboxTracker.endMarkedContent(opIdx, this.#recordDebugMetadataDepenencyAfterRestore);
    return this;
  }
  pushBaseTransform(ctx) {
    this.#bboxTracker.pushBaseTransform(ctx);
    return this;
  }
  popBaseTransform() {
    this.#bboxTracker.popBaseTransform();
    return this;
  }
  recordSimpleData(name, idx) {
    this.#simple[name] = idx;
    return this;
  }
  recordIncrementalData(name, idx) {
    this.#incremental[name].push(idx);
    return this;
  }
  resetIncrementalData(name, idx) {
    this.#incremental[name].length = 0;
    return this;
  }
  recordNamedData(name, idx) {
    this.#namedDependencies.set(name, idx);
    return this;
  }
  recordSimpleDataFromNamed(name, depName, fallbackIdx) {
    this.#simple[name] = this.#namedDependencies.get(depName) ?? fallbackIdx;
  }
  recordFutureForcedDependency(name, idx) {
    this.recordIncrementalData(FORCED_DEPENDENCY_LABEL, idx);
    return this;
  }
  inheritSimpleDataAsFutureForcedDependencies(names) {
    for (const name of names) {
      if (name in this.#simple) {
        this.recordFutureForcedDependency(name, this.#simple[name]);
      }
    }
    return this;
  }
  inheritPendingDependenciesAsFutureForcedDependencies() {
    for (const dep of this.#pendingDependencies) {
      this.recordFutureForcedDependency(FORCED_DEPENDENCY_LABEL, dep);
    }
    return this;
  }
  resetBBox(idx) {
    this.#bboxTracker.resetBBox(idx);
    return this;
  }
  recordClipBox(idx, ctx, minX, maxX, minY, maxY) {
    this.#bboxTracker.recordClipBox(idx, ctx, minX, maxX, minY, maxY);
    return this;
  }
  recordBBox(idx, ctx, minX, maxX, minY, maxY) {
    this.#bboxTracker.recordBBox(idx, ctx, minX, maxX, minY, maxY);
    return this;
  }
  recordCharacterBBox(idx, ctx, font, scale = 1, x = 0, y = 0, getMeasure) {
    const fontBBox = font.bbox;
    let isBBoxTrustworthy;
    let computedBBox;
    if (fontBBox) {
      isBBoxTrustworthy = fontBBox[2] !== fontBBox[0] && fontBBox[3] !== fontBBox[1] && this.#fontBBoxTrustworthy.get(font);
      if (isBBoxTrustworthy !== false) {
        computedBBox = [0, 0, 0, 0];
        Util.axialAlignedBoundingBox(fontBBox, font.fontMatrix, computedBBox);
        if (scale !== 1 || x !== 0 || y !== 0) {
          scaleCharBBox(scale, -scale, x, y, computedBBox);
        }
        if (isBBoxTrustworthy) {
          return this.recordBBox(idx, ctx, computedBBox[0], computedBBox[2], computedBBox[1], computedBBox[3]);
        }
      }
    }
    if (!getMeasure) {
      return this.recordFullPageBBox(idx);
    }
    const measure = getMeasure();
    if (fontBBox && computedBBox && isBBoxTrustworthy === undefined) {
      isBBoxTrustworthy = computedBBox[0] <= x - measure.actualBoundingBoxLeft && computedBBox[2] >= x + measure.actualBoundingBoxRight && computedBBox[1] <= y - measure.actualBoundingBoxAscent && computedBBox[3] >= y + measure.actualBoundingBoxDescent;
      this.#fontBBoxTrustworthy.set(font, isBBoxTrustworthy);
      if (isBBoxTrustworthy) {
        return this.recordBBox(idx, ctx, computedBBox[0], computedBBox[2], computedBBox[1], computedBBox[3]);
      }
    }
    return this.recordBBox(idx, ctx, x - measure.actualBoundingBoxLeft, x + measure.actualBoundingBoxRight, y - measure.actualBoundingBoxAscent, y + measure.actualBoundingBoxDescent);
  }
  recordFullPageBBox(idx) {
    this.#bboxTracker.recordFullPageBBox(idx);
    return this;
  }
  getSimpleIndex(dependencyName) {
    return this.#simple[dependencyName];
  }
  recordDependencies(idx, dependencyNames) {
    const pendingDependencies = this.#pendingDependencies;
    const simple = this.#simple;
    const incremental = this.#incremental;
    for (const name of dependencyNames) {
      if (name in this.#simple) {
        pendingDependencies.add(simple[name]);
      } else if (name in incremental) {
        incremental[name].forEach(pendingDependencies.add, pendingDependencies);
      }
    }
    return this;
  }
  recordNamedDependency(idx, name) {
    if (this.#namedDependencies.has(name)) {
      this.#pendingDependencies.add(this.#namedDependencies.get(name));
    }
    return this;
  }
  recordOperation(idx, preserve = false) {
    this.recordDependencies(idx, [FORCED_DEPENDENCY_LABEL]);
    if (this.#debugMetadata) {
      const metadata = ensureDebugMetadata(this.#debugMetadata, idx);
      const {
        dependencies
      } = metadata;
      this.#pendingDependencies.forEach(dependencies.add, dependencies);
      this.#bboxTracker._savesStack.forEach(dependencies.add, dependencies);
      this.#bboxTracker._markedContentStack.forEach(dependencies.add, dependencies);
      dependencies.delete(idx);
      metadata.isRenderingOperation = true;
    }
    const needsCleanup = !preserve && idx === this.#bboxTracker._pendingBBoxIdx;
    this.#bboxTracker.recordOperation(idx, preserve, [this.#pendingDependencies, this.#bboxTracker._savesStack, this.#bboxTracker._markedContentStack]);
    if (needsCleanup) {
      this.#pendingDependencies.clear();
    }
    return this;
  }
  recordShowTextOperation(idx, preserve = false) {
    const deps = Array.from(this.#pendingDependencies);
    this.recordOperation(idx, preserve);
    this.recordIncrementalData("sameLineText", idx);
    for (const dep of deps) {
      this.recordIncrementalData("sameLineText", dep);
    }
    return this;
  }
  bboxToClipBoxDropOperation(idx, preserve = false) {
    const needsCleanup = !preserve && idx === this.#bboxTracker._pendingBBoxIdx;
    this.#bboxTracker.bboxToClipBoxDropOperation(idx);
    if (needsCleanup) {
      this.#pendingDependencies.clear();
    }
    return this;
  }
  take() {
    this.#fontBBoxTrustworthy.clear();
    return this.#bboxTracker.take();
  }
  takeDebugMetadata() {
    return this.#debugMetadata;
  }
}
class CanvasNestedDependencyTracker {
  #dependencyTracker;
  #opIdx;
  #ignoreBBoxes;
  #nestingLevel = 0;
  #savesLevel = 0;
  constructor(dependencyTracker, opIdx, ignoreBBoxes) {
    if (dependencyTracker instanceof CanvasNestedDependencyTracker && dependencyTracker.#ignoreBBoxes === !!ignoreBBoxes) {
      return dependencyTracker;
    }
    this.#dependencyTracker = dependencyTracker;
    this.#opIdx = opIdx;
    this.#ignoreBBoxes = !!ignoreBBoxes;
  }
  get clipBox() {
    return this.#dependencyTracker.clipBox;
  }
  growOperationsCount() {
    throw new Error("Unreachable");
  }
  save(opIdx) {
    this.#savesLevel++;
    this.#dependencyTracker.save(this.#opIdx);
    return this;
  }
  restore(opIdx) {
    if (this.#savesLevel > 0) {
      this.#dependencyTracker.restore(this.#opIdx);
      this.#savesLevel--;
    }
    return this;
  }
  recordOpenMarker(idx) {
    this.#nestingLevel++;
    return this;
  }
  getOpenMarker() {
    return this.#nestingLevel > 0 ? this.#opIdx : this.#dependencyTracker.getOpenMarker();
  }
  recordCloseMarker(idx) {
    this.#nestingLevel--;
    return this;
  }
  beginMarkedContent(opIdx) {
    return this;
  }
  endMarkedContent(opIdx) {
    return this;
  }
  pushBaseTransform(ctx) {
    this.#dependencyTracker.pushBaseTransform(ctx);
    return this;
  }
  popBaseTransform() {
    this.#dependencyTracker.popBaseTransform();
    return this;
  }
  recordSimpleData(name, idx) {
    this.#dependencyTracker.recordSimpleData(name, this.#opIdx);
    return this;
  }
  recordIncrementalData(name, idx) {
    this.#dependencyTracker.recordIncrementalData(name, this.#opIdx);
    return this;
  }
  resetIncrementalData(name, idx) {
    this.#dependencyTracker.resetIncrementalData(name, this.#opIdx);
    return this;
  }
  recordNamedData(name, idx) {
    return this;
  }
  recordSimpleDataFromNamed(name, depName, fallbackIdx) {
    this.#dependencyTracker.recordSimpleDataFromNamed(name, depName, this.#opIdx);
    return this;
  }
  recordFutureForcedDependency(name, idx) {
    this.#dependencyTracker.recordFutureForcedDependency(name, this.#opIdx);
    return this;
  }
  inheritSimpleDataAsFutureForcedDependencies(names) {
    this.#dependencyTracker.inheritSimpleDataAsFutureForcedDependencies(names);
    return this;
  }
  inheritPendingDependenciesAsFutureForcedDependencies() {
    this.#dependencyTracker.inheritPendingDependenciesAsFutureForcedDependencies();
    return this;
  }
  resetBBox(idx) {
    if (!this.#ignoreBBoxes) {
      this.#dependencyTracker.resetBBox(this.#opIdx);
    }
    return this;
  }
  recordClipBox(idx, ctx, minX, maxX, minY, maxY) {
    if (!this.#ignoreBBoxes) {
      this.#dependencyTracker.recordClipBox(this.#opIdx, ctx, minX, maxX, minY, maxY);
    }
    return this;
  }
  recordBBox(idx, ctx, minX, maxX, minY, maxY) {
    if (!this.#ignoreBBoxes) {
      this.#dependencyTracker.recordBBox(this.#opIdx, ctx, minX, maxX, minY, maxY);
    }
    return this;
  }
  recordCharacterBBox(idx, ctx, font, scale, x, y, getMeasure) {
    if (!this.#ignoreBBoxes) {
      this.#dependencyTracker.recordCharacterBBox(this.#opIdx, ctx, font, scale, x, y, getMeasure);
    }
    return this;
  }
  recordFullPageBBox(idx) {
    if (!this.#ignoreBBoxes) {
      this.#dependencyTracker.recordFullPageBBox(this.#opIdx);
    }
    return this;
  }
  getSimpleIndex(dependencyName) {
    return this.#dependencyTracker.getSimpleIndex(dependencyName);
  }
  recordDependencies(idx, dependencyNames) {
    this.#dependencyTracker.recordDependencies(this.#opIdx, dependencyNames);
    return this;
  }
  recordNamedDependency(idx, name) {
    this.#dependencyTracker.recordNamedDependency(this.#opIdx, name);
    return this;
  }
  recordOperation(idx) {
    this.#dependencyTracker.recordOperation(this.#opIdx, true);
    return this;
  }
  recordShowTextOperation(idx) {
    this.#dependencyTracker.recordShowTextOperation(this.#opIdx, true);
    return this;
  }
  bboxToClipBoxDropOperation(idx) {
    if (!this.#ignoreBBoxes) {
      this.#dependencyTracker.bboxToClipBoxDropOperation(this.#opIdx, true);
    }
    return this;
  }
  take() {
    throw new Error("Unreachable");
  }
  takeDebugMetadata() {
    throw new Error("Unreachable");
  }
}
const Dependencies = {
  stroke: ["path", "transform", "filter", "strokeColor", "strokeAlpha", "lineWidth", "lineCap", "lineJoin", "miterLimit", "dash"],
  fill: ["path", "transform", "filter", "fillColor", "fillAlpha", "globalCompositeOperation", "SMask"],
  imageXObject: ["transform", "SMask", "filter", "fillAlpha", "strokeAlpha", "globalCompositeOperation"],
  rawFillPath: ["filter", "fillColor", "fillAlpha"],
  showText: ["transform", "leading", "charSpacing", "wordSpacing", "hScale", "textRise", "moveText", "textMatrix", "font", "fontObj", "filter", "fillColor", "textRenderingMode", "SMask", "fillAlpha", "strokeAlpha", "globalCompositeOperation", "sameLineText"],
  transform: ["transform"],
  transformAndFill: ["transform", "filter", "fillColor"]
};
class CanvasImagesTracker {
  #canvasWidth;
  #canvasHeight;
  #capacity = 4;
  #count = 0;
  #coords = new CanvasImagesTracker.#CoordsArray(this.#capacity * 6);
  static #CoordsArray = Float16Array;
  constructor(canvas) {
    this.#canvasWidth = canvas.width;
    this.#canvasHeight = canvas.height;
  }
  record(ctx, width, height, clipBox) {
    if (this.#count === this.#capacity) {
      this.#capacity *= 2;
      const newCoords = new CanvasImagesTracker.#CoordsArray(this.#capacity * 6);
      newCoords.set(this.#coords);
      this.#coords = newCoords;
    }
    const transform = getCurrentTransform(ctx);
    let coords;
    if (clipBox[0] !== Infinity) {
      const bbox = BBOX_INIT.slice();
      Util.axialAlignedBoundingBox([0, -height, width, 0], transform, bbox);
      const finalBBox = Util.intersect(clipBox, bbox);
      if (!finalBBox) {
        return;
      }
      const [minX, minY, maxX, maxY] = finalBBox;
      if (minX !== bbox[0] || minY !== bbox[1] || maxX !== bbox[2] || maxY !== bbox[3]) {
        const rotationAngle = Math.atan2(transform[1], transform[0]);
        const sin = Math.abs(Math.sin(rotationAngle));
        const cos = Math.abs(Math.cos(rotationAngle));
        if (sin < 1e-6 || cos < 1e-6 || Math.abs(sin - cos) < 1e-6) {
          coords = [minX, minY, minX, maxY, maxX, minY];
        } else {
          const finalBBoxWidth = maxX - minX;
          const finalBBoxHeight = maxY - minY;
          const sin2 = sin * sin;
          const cos2 = cos * cos;
          const cosSin = cos * sin;
          const denom = cos2 - sin2;
          const a = (finalBBoxHeight * cos2 - finalBBoxWidth * cosSin) / denom;
          const b = (finalBBoxHeight * cosSin - finalBBoxWidth * sin2) / denom;
          coords = [minX + b, minY, minX, minY + a, maxX, maxY - a];
        }
      }
    }
    if (!coords) {
      coords = [0, -height, 0, 0, width, -height];
      Util.applyTransform(coords, transform, 0);
      Util.applyTransform(coords, transform, 2);
      Util.applyTransform(coords, transform, 4);
    }
    coords[0] /= this.#canvasWidth;
    coords[1] /= this.#canvasHeight;
    coords[2] /= this.#canvasWidth;
    coords[3] /= this.#canvasHeight;
    coords[4] /= this.#canvasWidth;
    coords[5] /= this.#canvasHeight;
    this.#coords.set(coords, this.#count * 6);
    this.#count++;
  }
  take() {
    return this.#coords.subarray(0, this.#count * 6);
  }
}
function createCanvasTrackers(canvas, operationsCount, {
  recordOperations = false,
  recordImages = false,
  recordDebugMetadata = false
}) {
  const bboxTracker = recordOperations || recordImages ? new CanvasBBoxTracker(canvas, operationsCount) : null;
  return {
    dependencyTracker: recordOperations ? new CanvasDependencyTracker(bboxTracker, recordDebugMetadata) : bboxTracker,
    imagesTracker: recordImages ? new CanvasImagesTracker(canvas) : null
  };
}

;// ./src/shared/image_utils.js
/* unused harmony import specifier */ var image_utils_ImageKind;
/* unused harmony import specifier */ var image_utils_FeatureTest;


function convertToRGBA(params) {
  switch (params.kind) {
    case image_utils_ImageKind.GRAYSCALE_1BPP:
      return convertBlackAndWhiteToRGBA(params);
    case image_utils_ImageKind.RGB_24BPP:
      return convertRGBToRGBA(params);
  }
  return null;
}
function convertBlackAndWhiteToRGBA({
  src,
  srcPos = 0,
  dest,
  width,
  height,
  nonBlackColor = 0xffffffff,
  inverseDecode = false
}) {
  const black = FeatureTest.isLittleEndian ? 0xff000000 : 0x000000ff;
  const [zeroMapping, oneMapping] = inverseDecode ? [nonBlackColor, black] : [black, nonBlackColor];
  const widthInSource = width >> 3;
  const widthRemainder = width & 7;
  const xorMask = zeroMapping ^ oneMapping;
  const srcLength = src.length;
  dest = new Uint32Array(dest.buffer);
  let destPos = 0;
  for (let i = 0; i < height; ++i) {
    for (const max = srcPos + widthInSource; srcPos < max; ++srcPos, destPos += 8) {
      const elem = src[srcPos];
      dest[destPos] = zeroMapping ^ -(elem >> 7 & 1) & xorMask;
      dest[destPos + 1] = zeroMapping ^ -(elem >> 6 & 1) & xorMask;
      dest[destPos + 2] = zeroMapping ^ -(elem >> 5 & 1) & xorMask;
      dest[destPos + 3] = zeroMapping ^ -(elem >> 4 & 1) & xorMask;
      dest[destPos + 4] = zeroMapping ^ -(elem >> 3 & 1) & xorMask;
      dest[destPos + 5] = zeroMapping ^ -(elem >> 2 & 1) & xorMask;
      dest[destPos + 6] = zeroMapping ^ -(elem >> 1 & 1) & xorMask;
      dest[destPos + 7] = zeroMapping ^ -(elem & 1) & xorMask;
    }
    if (widthRemainder === 0) {
      continue;
    }
    const elem = srcPos < srcLength ? src[srcPos++] : 255;
    for (let j = 0; j < widthRemainder; ++j, ++destPos) {
      dest[destPos] = zeroMapping ^ -(elem >> 7 - j & 1) & xorMask;
    }
  }
  return {
    srcPos,
    destPos
  };
}
function convertRGBToRGBA({
  src,
  srcPos = 0,
  dest,
  destPos = 0,
  width,
  height
}) {
  let i = 0;
  const len = width * height * 3;
  const byteOffset = src.byteOffset + srcPos;
  const len32 = byteOffset % 4 === 0 ? Math.floor(math_clamp_MathClamp(src.length - srcPos, 0, len) / 4) : 0;
  const src32 = len32 > 0 ? new Uint32Array(src.buffer, byteOffset, len32) : null;
  const alphaMask = FeatureTest.isLittleEndian ? 0xff000000 : 0xff;
  if (FeatureTest.isLittleEndian) {
    for (; i < len32 - 2; i += 3, destPos += 4) {
      const s1 = src32[i],
        s2 = src32[i + 1],
        s3 = src32[i + 2];
      dest[destPos] = s1 | alphaMask;
      dest[destPos + 1] = s1 >>> 24 | s2 << 8 | alphaMask;
      dest[destPos + 2] = s2 >>> 16 | s3 << 16 | alphaMask;
      dest[destPos + 3] = s3 >>> 8 | alphaMask;
    }
    for (let j = srcPos + i * 4, jj = srcPos + len; j < jj; j += 3) {
      dest[destPos++] = src[j] | src[j + 1] << 8 | src[j + 2] << 16 | alphaMask;
    }
  } else {
    for (; i < len32 - 2; i += 3, destPos += 4) {
      const s1 = src32[i],
        s2 = src32[i + 1],
        s3 = src32[i + 2];
      dest[destPos] = s1 | alphaMask;
      dest[destPos + 1] = s1 << 24 | s2 >>> 8 | alphaMask;
      dest[destPos + 2] = s2 << 16 | s3 >>> 16 | alphaMask;
      dest[destPos + 3] = s3 << 8 | alphaMask;
    }
    for (let j = srcPos + i * 4, jj = srcPos + len; j < jj; j += 3) {
      dest[destPos++] = src[j] << 24 | src[j + 1] << 16 | src[j + 2] << 8 | alphaMask;
    }
  }
  return {
    srcPos: srcPos + len,
    destPos
  };
}
function grayToRGBA(src, dest) {
  if (image_utils_FeatureTest.isLittleEndian) {
    for (let i = 0, ii = src.length; i < ii; i++) {
      dest[i] = src[i] * 0x10101 | 0xff000000;
    }
  } else {
    for (let i = 0, ii = src.length; i < ii; i++) {
      dest[i] = src[i] * 0x1010100 | 0x000000ff;
    }
  }
}

;// ./src/display/webgpu.js
const MESH_WGSL = `
struct Uniforms {
  offsetX      : f32,
  offsetY      : f32,
  scaleX       : f32,
  scaleY       : f32,
  paddedWidth  : f32,
  paddedHeight : f32,
  borderSize   : f32,
  _pad         : f32,
};

@group(0) @binding(0) var<uniform> u : Uniforms;

struct VertexInput {
  @location(0) position : vec2<f32>,
  @location(1) color    : vec4<f32>,
};

struct VertexOutput {
  @builtin(position) position : vec4<f32>,
  @location(0)       color    : vec3<f32>,
};

@vertex
fn vs_main(in : VertexInput) -> VertexOutput {
  var out : VertexOutput;
  let cx = (in.position.x + u.offsetX) * u.scaleX;
  let cy = (in.position.y + u.offsetY) * u.scaleY;
  out.position = vec4<f32>(
    ((cx + u.borderSize) / u.paddedWidth) * 2.0 - 1.0,
    1.0 - ((cy + u.borderSize) / u.paddedHeight) * 2.0,
    0.0,
    1.0
  );
  out.color = in.color.rgb;
  return out;
}

@fragment
fn fs_main(in : VertexOutput) -> @location(0) vec4<f32> {
  return vec4<f32>(in.color, 1.0);
}
`;
class WebGPU {
  #initPromise = null;
  #device = null;
  #meshPipeline = null;
  #preferredFormat = null;
  async #initGPU() {
    if (!globalThis.navigator?.gpu) {
      return false;
    }
    try {
      const adapter = await navigator.gpu.requestAdapter();
      if (!adapter) {
        return false;
      }
      this.#preferredFormat = navigator.gpu.getPreferredCanvasFormat();
      this.#device = await adapter.requestDevice();
      return true;
    } catch {
      return false;
    }
  }
  init() {
    return this.#initPromise ||= this.#initGPU();
  }
  get isReady() {
    return this.#device !== null;
  }
  loadMeshShader() {
    if (!this.#device || this.#meshPipeline) {
      return;
    }
    const shaderModule = this.#device.createShaderModule({
      code: MESH_WGSL
    });
    this.#meshPipeline = this.#device.createRenderPipeline({
      layout: "auto",
      vertex: {
        module: shaderModule,
        entryPoint: "vs_main",
        buffers: [{
          arrayStride: 2 * 4,
          attributes: [{
            shaderLocation: 0,
            offset: 0,
            format: "float32x2"
          }]
        }, {
          arrayStride: 4,
          attributes: [{
            shaderLocation: 1,
            offset: 0,
            format: "unorm8x4"
          }]
        }]
      },
      fragment: {
        module: shaderModule,
        entryPoint: "fs_main",
        targets: [{
          format: this.#preferredFormat
        }]
      },
      primitive: {
        topology: "triangle-list"
      }
    });
  }
  draw(posData, colData, vertexCount, context, backgroundColor, paddedWidth, paddedHeight, borderSize) {
    this.loadMeshShader();
    const device = this.#device;
    const {
      offsetX,
      offsetY,
      scaleX,
      scaleY
    } = context;
    const posBuffer = device.createBuffer({
      size: Math.max(posData.byteLength, 4),
      usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST
    });
    if (posData.byteLength > 0) {
      device.queue.writeBuffer(posBuffer, 0, posData);
    }
    const colBuffer = device.createBuffer({
      size: Math.max(colData.byteLength, 4),
      usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST
    });
    if (colData.byteLength > 0) {
      device.queue.writeBuffer(colBuffer, 0, colData);
    }
    const uniformBuffer = device.createBuffer({
      size: 8 * 4,
      usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST
    });
    device.queue.writeBuffer(uniformBuffer, 0, new Float32Array([offsetX, offsetY, scaleX, scaleY, paddedWidth, paddedHeight, borderSize, 0]));
    const bindGroup = device.createBindGroup({
      layout: this.#meshPipeline.getBindGroupLayout(0),
      entries: [{
        binding: 0,
        resource: {
          buffer: uniformBuffer
        }
      }]
    });
    const offscreen = new OffscreenCanvas(paddedWidth, paddedHeight);
    const gpuCtx = offscreen.getContext("webgpu");
    gpuCtx.configure({
      device,
      format: this.#preferredFormat,
      alphaMode: backgroundColor ? "opaque" : "premultiplied"
    });
    const clearValue = backgroundColor ? {
      r: backgroundColor[0] / 255,
      g: backgroundColor[1] / 255,
      b: backgroundColor[2] / 255,
      a: 1
    } : {
      r: 0,
      g: 0,
      b: 0,
      a: 0
    };
    const commandEncoder = device.createCommandEncoder();
    const renderPass = commandEncoder.beginRenderPass({
      colorAttachments: [{
        view: gpuCtx.getCurrentTexture().createView(),
        clearValue,
        loadOp: "clear",
        storeOp: "store"
      }]
    });
    if (vertexCount > 0) {
      renderPass.setPipeline(this.#meshPipeline);
      renderPass.setBindGroup(0, bindGroup);
      renderPass.setVertexBuffer(0, posBuffer);
      renderPass.setVertexBuffer(1, colBuffer);
      renderPass.draw(vertexCount);
    }
    renderPass.end();
    device.queue.submit([commandEncoder.finish()]);
    posBuffer.destroy();
    colBuffer.destroy();
    uniformBuffer.destroy();
    return offscreen.transferToImageBitmap();
  }
}
const _webGPU = new WebGPU();
function initGPU() {
  return _webGPU.init();
}
function isGPUReady() {
  return _webGPU.isReady;
}
function loadMeshShader() {
  _webGPU.loadMeshShader();
}
function drawMeshWithGPU(posData, colData, vertexCount, context, backgroundColor, paddedWidth, paddedHeight, borderSize) {
  return _webGPU.draw(posData, colData, vertexCount, context, backgroundColor, paddedWidth, paddedHeight, borderSize);
}

;// ./src/display/pattern_helper.js




const PathType = {
  FILL: "Fill",
  STROKE: "Stroke",
  SHADING: "Shading"
};
function applyBoundingBox(ctx, bbox) {
  if (!bbox) {
    return;
  }
  const width = bbox[2] - bbox[0];
  const height = bbox[3] - bbox[1];
  const region = new Path2D();
  region.rect(bbox[0], bbox[1], width, height);
  ctx.clip(region);
}
class BaseShadingPattern {
  matrix = null;
  isModifyingCurrentTransform() {
    return false;
  }
  getPattern() {
    unreachable("Abstract method `getPattern` called.");
  }
}
class RadialAxialShadingPattern extends BaseShadingPattern {
  constructor(IR) {
    super();
    this._type = IR[1];
    this._bbox = IR[2];
    this._colorStops = IR[3];
    this._p0 = IR[4];
    this._p1 = IR[5];
    this._r0 = IR[6];
    this._r1 = IR[7];
  }
  isOriginBased() {
    return this._p0[0] === 0 && this._p0[1] === 0 && (!this.isRadial() || this._p1[0] === 0 && this._p1[1] === 0);
  }
  isRadial() {
    return this._type === "radial";
  }
  areConic() {
    if (!this.isRadial()) {
      return false;
    }
    const dist = Math.hypot(this._p0[0] - this._p1[0], this._p0[1] - this._p1[1]);
    return dist + this._r1 > this._r0 && dist + this._r0 > this._r1;
  }
  _createGradient(ctx, transform = null) {
    let grad;
    let firstPoint = this._p0;
    let secondPoint = this._p1;
    if (transform) {
      firstPoint = firstPoint.slice();
      secondPoint = secondPoint.slice();
      Util.applyTransform(firstPoint, transform);
      Util.applyTransform(secondPoint, transform);
    }
    if (this._type === "axial") {
      grad = ctx.createLinearGradient(firstPoint[0], firstPoint[1], secondPoint[0], secondPoint[1]);
    } else if (this._type === "radial") {
      let r0 = this._r0;
      let r1 = this._r1;
      if (transform) {
        const scale = new Float32Array(2);
        Util.singularValueDecompose2dScale(transform, scale);
        r0 *= scale[0];
        r1 *= scale[0];
      }
      grad = ctx.createRadialGradient(firstPoint[0], firstPoint[1], r0, secondPoint[0], secondPoint[1], r1);
    }
    for (const colorStop of this._colorStops) {
      grad.addColorStop(colorStop[0], colorStop[1]);
    }
    return grad;
  }
  _createReversedGradient(ctx, transform = null) {
    let firstPoint = this._p1;
    let secondPoint = this._p0;
    if (transform) {
      firstPoint = firstPoint.slice();
      secondPoint = secondPoint.slice();
      Util.applyTransform(firstPoint, transform);
      Util.applyTransform(secondPoint, transform);
    }
    let r0 = this._r1;
    let r1 = this._r0;
    if (transform) {
      const scale = new Float32Array(2);
      Util.singularValueDecompose2dScale(transform, scale);
      r0 *= scale[0];
      r1 *= scale[0];
    }
    const grad = ctx.createRadialGradient(firstPoint[0], firstPoint[1], r0, secondPoint[0], secondPoint[1], r1);
    const reversedStops = this._colorStops.map(([t, c]) => [1 - t, c]).reverse();
    for (const [t, c] of reversedStops) {
      grad.addColorStop(t, c);
    }
    return grad;
  }
  _createRasterPattern(ctx, owner, inverse, bbox, transform, transferMaps) {
    const width = Math.ceil(bbox[2] - bbox[0]) || 1;
    const height = Math.ceil(bbox[3] - bbox[1]) || 1;
    const tmpCanvas = owner.canvasFactory.create(width, height);
    const tmpCtx = tmpCanvas.context;
    tmpCtx.clearRect(0, 0, width, height);
    tmpCtx.beginPath();
    tmpCtx.rect(0, 0, width, height);
    tmpCtx.translate(-bbox[0], -bbox[1]);
    inverse = Util.transform(inverse, [1, 0, 0, 1, bbox[0], bbox[1]]);
    tmpCtx.transform(...transform);
    applyBoundingBox(tmpCtx, this._bbox);
    if (this.areConic()) {
      tmpCtx.fillStyle = this._createReversedGradient(tmpCtx);
      tmpCtx.fill();
    }
    tmpCtx.fillStyle = this._createGradient(tmpCtx);
    tmpCtx.fill();
    transferMaps?.applyToCanvas(tmpCtx);
    const pattern = ctx.createPattern(tmpCanvas.canvas, "no-repeat");
    owner.canvasFactory.destroy(tmpCanvas);
    pattern.setTransform(new DOMMatrix(inverse));
    return pattern;
  }
  getPattern(ctx, owner, inverse, pathType) {
    const transferMaps = owner.current.transferMapsFallback;
    if (pathType === PathType.STROKE || pathType === PathType.FILL) {
      if (this.isOriginBased() && !transferMaps) {
        let transf = Util.transform(inverse, owner.baseTransform);
        if (this.matrix) {
          transf = Util.transform(transf, this.matrix);
        }
        const precision = 1e-3;
        const n1 = Math.hypot(transf[0], transf[1]);
        const n2 = Math.hypot(transf[2], transf[3]);
        const ps = (transf[0] * transf[2] + transf[1] * transf[3]) / (n1 * n2);
        if (Math.abs(ps) < precision) {
          if (this.isRadial()) {
            if (Math.abs(n1 - n2) < precision) {
              return this._createGradient(ctx, transf);
            }
          } else {
            return this._createGradient(ctx, transf);
          }
        }
      }
      const ownerBBox = owner.current.getClippedPathBoundingBox(pathType, getCurrentTransform(ctx)) || [0, 0, 0, 0];
      const transform = this.matrix ? Util.transform(owner.baseTransform, this.matrix) : owner.baseTransform;
      return this._createRasterPattern(ctx, owner, inverse, ownerBBox, transform, transferMaps);
    }
    if (transferMaps && inverse) {
      return this._createRasterPattern(ctx, owner, inverse, owner.current.clipBox, getCurrentTransform(ctx), transferMaps);
    }
    if (this.areConic()) {
      ctx.save();
      applyBoundingBox(ctx, this._bbox);
      ctx.fillStyle = this._createReversedGradient(ctx);
      ctx.fillRect(-1e10, -1e10, 2e10, 2e10);
      ctx.restore();
    }
    applyBoundingBox(ctx, this._bbox);
    return this._createGradient(ctx);
  }
}
function drawTriangle(data, context, p1, p2, p3, c1, c2, c3) {
  const coords = context.coords,
    colors = context.colors;
  const bytes = data.data,
    rowSize = data.width * 4;
  let tmp;
  if (coords[p1 * 2 + 1] > coords[p2 * 2 + 1]) {
    tmp = p1;
    p1 = p2;
    p2 = tmp;
    tmp = c1;
    c1 = c2;
    c2 = tmp;
  }
  if (coords[p2 * 2 + 1] > coords[p3 * 2 + 1]) {
    tmp = p2;
    p2 = p3;
    p3 = tmp;
    tmp = c2;
    c2 = c3;
    c3 = tmp;
  }
  if (coords[p1 * 2 + 1] > coords[p2 * 2 + 1]) {
    tmp = p1;
    p1 = p2;
    p2 = tmp;
    tmp = c1;
    c1 = c2;
    c2 = tmp;
  }
  const x1 = (coords[p1 * 2] + context.offsetX) * context.scaleX;
  const y1 = (coords[p1 * 2 + 1] + context.offsetY) * context.scaleY;
  const x2 = (coords[p2 * 2] + context.offsetX) * context.scaleX;
  const y2 = (coords[p2 * 2 + 1] + context.offsetY) * context.scaleY;
  const x3 = (coords[p3 * 2] + context.offsetX) * context.scaleX;
  const y3 = (coords[p3 * 2 + 1] + context.offsetY) * context.scaleY;
  if (y1 >= y3) {
    return;
  }
  const c1r = colors[c1 * 4],
    c1g = colors[c1 * 4 + 1],
    c1b = colors[c1 * 4 + 2];
  const c2r = colors[c2 * 4],
    c2g = colors[c2 * 4 + 1],
    c2b = colors[c2 * 4 + 2];
  const c3r = colors[c3 * 4],
    c3g = colors[c3 * 4 + 1],
    c3b = colors[c3 * 4 + 2];
  const minY = Math.round(y1),
    maxY = Math.round(y3);
  let xa, car, cag, cab;
  let xb, cbr, cbg, cbb;
  for (let y = minY; y <= maxY; y++) {
    if (y < y2) {
      const k = y < y1 ? 0 : (y1 - y) / (y1 - y2);
      xa = x1 - (x1 - x2) * k;
      car = c1r - (c1r - c2r) * k;
      cag = c1g - (c1g - c2g) * k;
      cab = c1b - (c1b - c2b) * k;
    } else {
      let k;
      if (y > y3) {
        k = 1;
      } else if (y2 === y3) {
        k = 0;
      } else {
        k = (y2 - y) / (y2 - y3);
      }
      xa = x2 - (x2 - x3) * k;
      car = c2r - (c2r - c3r) * k;
      cag = c2g - (c2g - c3g) * k;
      cab = c2b - (c2b - c3b) * k;
    }
    let k;
    if (y < y1) {
      k = 0;
    } else if (y > y3) {
      k = 1;
    } else {
      k = (y1 - y) / (y1 - y3);
    }
    xb = x1 - (x1 - x3) * k;
    cbr = c1r - (c1r - c3r) * k;
    cbg = c1g - (c1g - c3g) * k;
    cbb = c1b - (c1b - c3b) * k;
    const x1_ = Math.round(Math.min(xa, xb));
    const x2_ = Math.round(Math.max(xa, xb));
    let j = rowSize * y + x1_ * 4;
    for (let x = x1_; x <= x2_; x++) {
      k = (xa - x) / (xa - xb);
      if (k < 0) {
        k = 0;
      } else if (k > 1) {
        k = 1;
      }
      bytes[j++] = car - (car - cbr) * k | 0;
      bytes[j++] = cag - (cag - cbg) * k | 0;
      bytes[j++] = cab - (cab - cbb) * k | 0;
      bytes[j++] = 255;
    }
  }
}
class MeshShadingPattern extends BaseShadingPattern {
  constructor(IR) {
    super();
    this._posData = IR[2];
    this._colData = IR[3];
    this._vertexCount = IR[4];
    this._bounds = IR[5];
    this._bbox = IR[6];
    this._background = IR[7];
    loadMeshShader();
  }
  _createMeshCanvas(combinedScale, backgroundColor, canvasFactory, transferMaps = null) {
    const EXPECTED_SCALE = 1.1;
    const MAX_PATTERN_SIZE = 3000;
    const BORDER_SIZE = 2;
    const offsetX = Math.floor(this._bounds[0]);
    const offsetY = Math.floor(this._bounds[1]);
    const boundsWidth = Math.ceil(this._bounds[2]) - offsetX;
    const boundsHeight = Math.ceil(this._bounds[3]) - offsetY;
    const width = Math.min(Math.ceil(Math.abs(boundsWidth * combinedScale[0] * EXPECTED_SCALE)), MAX_PATTERN_SIZE) || 1;
    const height = Math.min(Math.ceil(Math.abs(boundsHeight * combinedScale[1] * EXPECTED_SCALE)), MAX_PATTERN_SIZE) || 1;
    const scaleX = boundsWidth ? boundsWidth / width : 1;
    const scaleY = boundsHeight ? boundsHeight / height : 1;
    const context = {
      coords: this._posData,
      colors: this._colData,
      offsetX: -offsetX,
      offsetY: -offsetY,
      scaleX: 1 / scaleX,
      scaleY: 1 / scaleY
    };
    const paddedWidth = width + BORDER_SIZE * 2;
    const paddedHeight = height + BORDER_SIZE * 2;
    const tmpCanvas = canvasFactory.create(paddedWidth, paddedHeight);
    if (isGPUReady() && this._vertexCount > 48) {
      tmpCanvas.context.drawImage(drawMeshWithGPU(this._posData, this._colData, this._vertexCount, context, backgroundColor, paddedWidth, paddedHeight, BORDER_SIZE), 0, 0);
    } else {
      const data = tmpCanvas.context.createImageData(width, height);
      if (backgroundColor) {
        const bytes = data.data;
        for (let i = 0, ii = bytes.length; i < ii; i += 4) {
          bytes[i] = backgroundColor[0];
          bytes[i + 1] = backgroundColor[1];
          bytes[i + 2] = backgroundColor[2];
          bytes[i + 3] = 255;
        }
      }
      for (let i = 0, ii = this._vertexCount; i < ii; i += 3) {
        drawTriangle(data, context, i, i + 1, i + 2, i, i + 1, i + 2);
      }
      tmpCanvas.context.putImageData(data, BORDER_SIZE, BORDER_SIZE);
    }
    transferMaps?.applyToCanvas(tmpCanvas.context);
    return {
      canvas: tmpCanvas.canvas,
      offsetX: offsetX - BORDER_SIZE * scaleX,
      offsetY: offsetY - BORDER_SIZE * scaleY,
      scaleX,
      scaleY
    };
  }
  isModifyingCurrentTransform() {
    return true;
  }
  getPattern(ctx, owner, inverse, pathType) {
    applyBoundingBox(ctx, this._bbox);
    const scale = new Float32Array(2);
    if (pathType === PathType.SHADING) {
      Util.singularValueDecompose2dScale(getCurrentTransform(ctx), scale);
    } else if (this.matrix) {
      Util.singularValueDecompose2dScale(this.matrix, scale);
      const [matrixScaleX, matrixScaleY] = scale;
      Util.singularValueDecompose2dScale(owner.baseTransform, scale);
      scale[0] *= matrixScaleX;
      scale[1] *= matrixScaleY;
    } else {
      Util.singularValueDecompose2dScale(owner.baseTransform, scale);
    }
    const temporaryPatternCanvas = this._createMeshCanvas(scale, pathType === PathType.SHADING ? null : this._background, owner.canvasFactory, owner.current.transferMapsFallback);
    if (pathType !== PathType.SHADING) {
      ctx.setTransform(...owner.baseTransform);
      if (this.matrix) {
        ctx.transform(...this.matrix);
      }
    }
    ctx.translate(temporaryPatternCanvas.offsetX, temporaryPatternCanvas.offsetY);
    ctx.scale(temporaryPatternCanvas.scaleX, temporaryPatternCanvas.scaleY);
    const pattern = ctx.createPattern(temporaryPatternCanvas.canvas, "no-repeat");
    owner.canvasFactory.destroy(temporaryPatternCanvas);
    return pattern;
  }
}
class DummyShadingPattern extends BaseShadingPattern {
  getPattern() {
    return "hotpink";
  }
}
function getShadingPattern(IR) {
  switch (IR[0]) {
    case "RadialAxial":
      return new RadialAxialShadingPattern(IR);
    case "Mesh":
      return new MeshShadingPattern(IR);
    case "Dummy":
      return new DummyShadingPattern();
  }
  throw new Error(`Unknown IR type: ${IR[0]}`);
}
const PaintType = {
  COLORED: 1,
  UNCOLORED: 2
};
class TilingPattern {
  static MAX_PATTERN_SIZE = 3000;
  constructor(IR, ctx, canvasGraphicsFactory, baseTransform) {
    this.color = IR[1];
    this.operatorList = IR[2];
    this.matrix = IR[3];
    this.bbox = IR[4];
    this.xstep = IR[5];
    this.ystep = IR[6];
    this.paintType = IR[7];
    this.tilingType = IR[8];
    this.needsIsolation = IR[9] ?? true;
    this.ctx = ctx;
    this.canvasGraphicsFactory = canvasGraphicsFactory;
    this.baseTransform = baseTransform;
    this.patternBaseMatrix = this.matrix ? Util.transform(baseTransform, this.matrix) : baseTransform;
  }
  canSkipPatternCanvas([width, height, offsetX, offsetY]) {
    const [x0, y0, x1, y1] = this.bbox;
    const absXStep = Math.abs(this.xstep);
    const absYStep = Math.abs(this.ystep);
    if (width > absXStep + 1e-6 || height > absYStep + 1e-6) {
      return null;
    }
    const nXFirst = Math.floor((offsetX - x1) / absXStep) + 1;
    const nXLast = Math.ceil((offsetX + width - x0) / absXStep) - 1;
    const nYFirst = Math.floor((offsetY - y1) / absYStep) + 1;
    const nYLast = Math.ceil((offsetY + height - y0) / absYStep) - 1;
    return nXLast <= nXFirst && nYLast <= nYFirst ? [nXFirst, nYFirst] : null;
  }
  updatePatternDims(clippedBBox, dims) {
    const bbox = BBOX_INIT.slice();
    Util.axialAlignedBoundingBox(clippedBBox, Util.inverseTransform(this.patternBaseMatrix), bbox);
    dims[0] = bbox[2] - bbox[0];
    dims[1] = bbox[3] - bbox[1];
    dims[2] = bbox[0];
    dims[3] = bbox[1];
  }
  _renderTileCanvas(owner, opIdx, dimx, dimy) {
    const [x0, y0, x1, y1] = this.bbox;
    const tmpCanvas = owner.canvasFactory.create(dimx.size, dimy.size);
    const tmpCtx = tmpCanvas.context;
    const graphics = this.canvasGraphicsFactory.createCanvasGraphics(tmpCtx, opIdx);
    graphics.groupLevel = owner.groupLevel;
    graphics.current.transferMapsFallback = owner.current.transferMapsFallback;
    this.setFillAndStrokeStyleToContext(graphics, this.paintType, this.color);
    tmpCtx.translate(-dimx.scale * x0, -dimy.scale * y0);
    graphics.transform(0, dimx.scale, 0, 0, dimy.scale, 0, 0);
    tmpCtx.save();
    graphics.dependencyTracker?.save();
    this.clipBbox(graphics, x0, y0, x1, y1);
    graphics.baseTransform = getCurrentTransform(graphics.ctx);
    graphics.executeOperatorList(this.operatorList);
    graphics.endDrawing();
    graphics.dependencyTracker?.restore();
    tmpCtx.restore();
    return tmpCanvas;
  }
  _getCombinedScales() {
    const scale = new Float32Array(2);
    Util.singularValueDecompose2dScale(this.matrix, scale);
    const [matrixScaleX, matrixScaleY] = scale;
    Util.singularValueDecompose2dScale(this.baseTransform, scale);
    return [matrixScaleX * scale[0], matrixScaleY * scale[1]];
  }
  drawPattern(owner, path, useEOFill = false, [n, m], opIdx) {
    const [x0, y0, x1, y1] = this.bbox;
    const dependencyTracker = owner.dependencyTracker;
    if (dependencyTracker) {
      owner.dependencyTracker = new CanvasNestedDependencyTracker(dependencyTracker, opIdx);
    }
    try {
      owner.save();
      if (useEOFill) {
        owner.ctx.clip(path, "evenodd");
      } else {
        owner.ctx.clip(path);
      }
      owner.ctx.setTransform(...this.patternBaseMatrix);
      owner.ctx.translate(n * this.xstep, m * this.ystep);
      if (this.needsIsolation || owner.ctx.globalAlpha !== 1 || owner.ctx.globalCompositeOperation !== "source-over" || owner.inSMaskMode) {
        const bboxWidth = x1 - x0;
        const bboxHeight = y1 - y0;
        const [combinedScaleX, combinedScaleY] = this._getCombinedScales();
        const dimx = this.getSizeAndScale(bboxWidth, this.ctx.canvas.width, combinedScaleX);
        const dimy = this.getSizeAndScale(bboxHeight, this.ctx.canvas.height, combinedScaleY);
        const tmpCanvas = this._renderTileCanvas(owner, opIdx, dimx, dimy);
        owner.ctx.drawImage(tmpCanvas.canvas, x0, y0, bboxWidth, bboxHeight);
        owner.canvasFactory.destroy(tmpCanvas);
      } else {
        owner.resetGraphicsState();
        this.setFillAndStrokeStyleToContext(owner, this.paintType, this.color);
        this.clipBbox(owner, x0, y0, x1, y1);
        owner.baseTransformStack.push(owner.baseTransform);
        owner.baseTransform = getCurrentTransform(owner.ctx);
        owner.executeOperatorList(this.operatorList);
        owner.baseTransform = owner.baseTransformStack.pop();
      }
      owner.restore();
    } finally {
      if (dependencyTracker) {
        owner.dependencyTracker = dependencyTracker;
      }
    }
  }
  createPatternCanvas(owner, opIdx) {
    const [x0, y0, x1, y1] = this.bbox;
    const width = x1 - x0;
    const height = y1 - y0;
    let {
      xstep,
      ystep
    } = this;
    xstep = Math.abs(xstep);
    ystep = Math.abs(ystep);
    info("TilingType: " + this.tilingType);
    const [combinedScaleX, combinedScaleY] = this._getCombinedScales();
    let canvasWidth = width,
      canvasHeight = height,
      redrawHorizontally = false,
      redrawVertically = false;
    if (Math.ceil(xstep * combinedScaleX) >= Math.ceil(width * combinedScaleX)) {
      canvasWidth = xstep;
    } else {
      redrawHorizontally = true;
    }
    if (Math.ceil(ystep * combinedScaleY) >= Math.ceil(height * combinedScaleY)) {
      canvasHeight = ystep;
    } else {
      redrawVertically = true;
    }
    const dimx = this.getSizeAndScale(canvasWidth, this.ctx.canvas.width, combinedScaleX);
    const dimy = this.getSizeAndScale(canvasHeight, this.ctx.canvas.height, combinedScaleY);
    const tmpCanvas = this._renderTileCanvas(owner, opIdx, dimx, dimy);
    if (redrawHorizontally || redrawVertically) {
      const image = tmpCanvas.canvas;
      if (redrawHorizontally) {
        canvasWidth = xstep;
      }
      if (redrawVertically) {
        canvasHeight = ystep;
      }
      const dimx2 = this.getSizeAndScale(canvasWidth, this.ctx.canvas.width, combinedScaleX);
      const dimy2 = this.getSizeAndScale(canvasHeight, this.ctx.canvas.height, combinedScaleY);
      const xSize = dimx2.size;
      const ySize = dimy2.size;
      const tmpCanvas2 = owner.canvasFactory.create(xSize, ySize);
      const tmpCtx2 = tmpCanvas2.context;
      const ii = redrawHorizontally ? Math.min(Math.floor(width / xstep), Math.ceil(image.width / xSize)) : 0;
      const jj = redrawVertically ? Math.min(Math.floor(height / ystep), Math.ceil(image.height / ySize)) : 0;
      let rowSource = image;
      let bandCanvas = null;
      if (redrawVertically) {
        bandCanvas = owner.canvasFactory.create(image.width, ySize);
        const bandCtx = bandCanvas.context;
        for (let j = jj; j >= 0; j--) {
          bandCtx.drawImage(image, 0, ySize * j, image.width, ySize, 0, 0, image.width, ySize);
        }
        rowSource = bandCanvas.canvas;
      }
      for (let i = ii; i >= 0; i--) {
        tmpCtx2.drawImage(rowSource, xSize * i, 0, xSize, ySize, 0, 0, xSize, ySize);
      }
      if (bandCanvas) {
        owner.canvasFactory.destroy(bandCanvas);
      }
      owner.canvasFactory.destroy(tmpCanvas);
      return {
        canvas: tmpCanvas2.canvas,
        canvasEntry: tmpCanvas2,
        scaleX: dimx2.scale,
        scaleY: dimy2.scale,
        offsetX: x0,
        offsetY: y0
      };
    }
    return {
      canvas: tmpCanvas.canvas,
      canvasEntry: tmpCanvas,
      scaleX: dimx.scale,
      scaleY: dimy.scale,
      offsetX: x0,
      offsetY: y0
    };
  }
  getSizeAndScale(step, realOutputSize, scale) {
    const maxSize = Math.max(TilingPattern.MAX_PATTERN_SIZE, realOutputSize);
    let size = Math.ceil(step * scale);
    if (size >= maxSize) {
      size = maxSize;
    } else {
      scale = size / step;
    }
    return {
      scale,
      size
    };
  }
  clipBbox(graphics, x0, y0, x1, y1) {
    const bboxWidth = x1 - x0;
    const bboxHeight = y1 - y0;
    const clip = new Path2D();
    clip.rect(x0, y0, bboxWidth, bboxHeight);
    Util.axialAlignedBoundingBox([x0, y0, x1, y1], getCurrentTransform(graphics.ctx), graphics.current.minMax);
    graphics.ctx.clip(clip);
    graphics.current.updateClipFromPath();
  }
  setFillAndStrokeStyleToContext(graphics, paintType, color) {
    switch (paintType) {
      case PaintType.COLORED:
        color = "#000000";
        break;
      case PaintType.UNCOLORED:
        break;
      default:
        throw new FormatError(`Unsupported paint type: ${paintType}`);
    }
    const {
      ctx,
      current
    } = graphics;
    current.patternFill = current.patternStroke = false;
    ctx.fillStyle = ctx.strokeStyle = current.transferMapsFallback?.applyToColor(color) ?? color;
    current.fillColor = current.strokeColor = color;
  }
  isModifyingCurrentTransform() {
    return false;
  }
  getPattern(ctx, owner, inverse, pathType, opIdx) {
    const matrix = pathType !== PathType.SHADING ? Util.transform(inverse, this.patternBaseMatrix) : inverse;
    const temporaryPatternCanvas = this.createPatternCanvas(owner, opIdx);
    let domMatrix = new DOMMatrix(matrix);
    domMatrix = domMatrix.translate(temporaryPatternCanvas.offsetX, temporaryPatternCanvas.offsetY);
    domMatrix = domMatrix.scale(1 / temporaryPatternCanvas.scaleX, 1 / temporaryPatternCanvas.scaleY);
    const pattern = ctx.createPattern(temporaryPatternCanvas.canvas, "repeat");
    owner.canvasFactory.destroy(temporaryPatternCanvas.canvasEntry);
    pattern.setTransform(domMatrix);
    return pattern;
  }
}

;// ./src/display/canvas.js






const MIN_FONT_SIZE = 16;
const MAX_FONT_SIZE = 100;
const EXECUTION_TIME = 15;
const EXECUTION_STEPS = 10;
const FULL_CHUNK_HEIGHT = 16;
const XY = new Float32Array(2);
function mirrorContextOperations(ctx, destCtx) {
  if (ctx._removeMirroring) {
    throw new Error("Context is already forwarding operations.");
  }
  const originalMethods = new Map();
  for (const name of ["save", "restore", "rotate", "scale", "translate", "transform", "setTransform", "resetTransform", "clip", "moveTo", "lineTo", "bezierCurveTo", "quadraticCurveTo", "arc", "arcTo", "ellipse", "rect", "roundRect", "closePath", "beginPath"]) {
    const original = ctx[name];
    if (typeof original !== "function" || typeof destCtx[name] !== "function") {
      continue;
    }
    originalMethods.set(name, original);
    ctx[name] = function (...args) {
      destCtx[name](...args);
      return original.apply(this, args);
    };
  }
  ctx._removeMirroring = () => {
    for (const [name, original] of originalMethods) {
      ctx[name] = original;
    }
    delete ctx._removeMirroring;
  };
}
function drawImageAtIntegerCoords(ctx, srcImg, srcX, srcY, srcW, srcH, destX, destY, destW, destH) {
  const [a, b, c, d, tx, ty] = getCurrentTransform(ctx);
  if (b === 0 && c === 0) {
    const tlX = destX * a + tx;
    const rTlX = Math.round(tlX);
    const tlY = destY * d + ty;
    const rTlY = Math.round(tlY);
    const brX = (destX + destW) * a + tx;
    const rWidth = Math.abs(Math.round(brX) - rTlX) || 1;
    const brY = (destY + destH) * d + ty;
    const rHeight = Math.abs(Math.round(brY) - rTlY) || 1;
    ctx.setTransform(Math.sign(a), 0, 0, Math.sign(d), rTlX, rTlY);
    ctx.drawImage(srcImg, srcX, srcY, srcW, srcH, 0, 0, rWidth, rHeight);
    ctx.setTransform(a, b, c, d, tx, ty);
    return [rWidth, rHeight];
  }
  if (a === 0 && d === 0) {
    const tlX = destY * c + tx;
    const rTlX = Math.round(tlX);
    const tlY = destX * b + ty;
    const rTlY = Math.round(tlY);
    const brX = (destY + destH) * c + tx;
    const rWidth = Math.abs(Math.round(brX) - rTlX) || 1;
    const brY = (destX + destW) * b + ty;
    const rHeight = Math.abs(Math.round(brY) - rTlY) || 1;
    ctx.setTransform(0, Math.sign(b), Math.sign(c), 0, rTlX, rTlY);
    ctx.drawImage(srcImg, srcX, srcY, srcW, srcH, 0, 0, rHeight, rWidth);
    ctx.setTransform(a, b, c, d, tx, ty);
    return [rHeight, rWidth];
  }
  ctx.drawImage(srcImg, srcX, srcY, srcW, srcH, destX, destY, destW, destH);
  const scaleX = Math.hypot(a, b);
  const scaleY = Math.hypot(c, d);
  return [scaleX * destW, scaleY * destH];
}
class CanvasExtraState {
  alphaIsShape = false;
  fontSize = 0;
  fontSizeScale = 1;
  textMatrix = null;
  textMatrixScale = 1;
  fontMatrix = FONT_IDENTITY_MATRIX;
  leading = 0;
  x = 0;
  y = 0;
  lineX = 0;
  lineY = 0;
  charSpacing = 0;
  wordSpacing = 0;
  textHScale = 1;
  textRenderingMode = TextRenderingMode.FILL;
  textRise = 0;
  fillColor = "#000000";
  strokeColor = "#000000";
  tilingPatternDims = null;
  patternFill = false;
  patternStroke = false;
  fillAlpha = 1;
  strokeAlpha = 1;
  lineWidth = 1;
  activeSMask = null;
  transferMaps = "none";
  transferMapsFallback = null;
  minMax = F32_BBOX_INIT.slice();
  constructor(width, height) {
    this.clipBox = new Float32Array([0, 0, width, height]);
  }
  clone() {
    const clone = Object.create(this);
    clone.clipBox = this.clipBox.slice();
    clone.minMax = this.minMax.slice();
    clone.tilingPatternDims = this.tilingPatternDims?.slice();
    return clone;
  }
  getPathBoundingBox(pathType = PathType.FILL, transform = null) {
    const box = this.minMax.slice();
    if (pathType === PathType.STROKE) {
      if (!transform) {
        unreachable("Stroke bounding box must include transform.");
      }
      Util.singularValueDecompose2dScale(transform, XY);
      const xStrokePad = XY[0] * this.lineWidth / 2;
      const yStrokePad = XY[1] * this.lineWidth / 2;
      box[0] -= xStrokePad;
      box[1] -= yStrokePad;
      box[2] += xStrokePad;
      box[3] += yStrokePad;
    }
    return box;
  }
  updateClipFromPath() {
    const intersect = Util.intersect(this.clipBox, this.getPathBoundingBox());
    this.startNewPathAndClipBox(intersect || [0, 0, 0, 0]);
  }
  isEmptyClip() {
    return this.minMax[0] === Infinity;
  }
  startNewPathAndClipBox(box) {
    this.clipBox.set(box, 0);
    this.minMax.set(F32_BBOX_INIT, 0);
  }
  getClippedPathBoundingBox(pathType = PathType.FILL, transform = null) {
    return Util.intersect(this.clipBox, this.getPathBoundingBox(pathType, transform));
  }
}
function putBinaryImageData(ctx, imgData) {
  const {
    width,
    height,
    kind
  } = imgData;
  const partialChunkHeight = height % FULL_CHUNK_HEIGHT;
  const fullChunks = (height - partialChunkHeight) / FULL_CHUNK_HEIGHT;
  const totalChunks = partialChunkHeight === 0 ? fullChunks : fullChunks + 1;
  const chunkImgData = ctx.createImageData(width, FULL_CHUNK_HEIGHT);
  let srcPos = 0;
  const src = imgData.data;
  const dest = chunkImgData.data;
  let i;
  if (kind === ImageKind.GRAYSCALE_1BPP) {
    for (i = 0; i < totalChunks; i++) {
      ({
        srcPos
      } = convertBlackAndWhiteToRGBA({
        src,
        srcPos,
        dest,
        width,
        height: i < fullChunks ? FULL_CHUNK_HEIGHT : partialChunkHeight
      }));
      ctx.putImageData(chunkImgData, 0, i * FULL_CHUNK_HEIGHT);
    }
  } else if (kind === ImageKind.RGBA_32BPP) {
    let j = 0;
    let elemsInThisChunk = width * FULL_CHUNK_HEIGHT * 4;
    for (i = 0; i < fullChunks; i++) {
      dest.set(src.subarray(srcPos, srcPos + elemsInThisChunk));
      srcPos += elemsInThisChunk;
      ctx.putImageData(chunkImgData, 0, j);
      j += FULL_CHUNK_HEIGHT;
    }
    if (i < totalChunks) {
      elemsInThisChunk = width * partialChunkHeight * 4;
      dest.set(src.subarray(srcPos, srcPos + elemsInThisChunk));
      ctx.putImageData(chunkImgData, 0, j);
    }
  } else if (kind === ImageKind.RGB_24BPP) {
    for (i = 0; i < totalChunks; i++) {
      ({
        srcPos
      } = convertRGBToRGBA({
        src,
        srcPos,
        dest: new Uint32Array(dest.buffer),
        width,
        height: i < fullChunks ? FULL_CHUNK_HEIGHT : partialChunkHeight
      }));
      ctx.putImageData(chunkImgData, 0, i * FULL_CHUNK_HEIGHT);
    }
  } else {
    throw new Error(`bad image kind: ${kind}`);
  }
}
function putBinaryImageMask(ctx, imgData) {
  if (imgData.bitmap) {
    ctx.drawImage(imgData.bitmap, 0, 0);
    return;
  }
  const {
    width,
    height
  } = imgData;
  const partialChunkHeight = height % FULL_CHUNK_HEIGHT;
  const fullChunks = (height - partialChunkHeight) / FULL_CHUNK_HEIGHT;
  const totalChunks = partialChunkHeight === 0 ? fullChunks : fullChunks + 1;
  const chunkImgData = ctx.createImageData(width, FULL_CHUNK_HEIGHT);
  let srcPos = 0;
  const src = imgData.data;
  const dest = chunkImgData.data;
  for (let i = 0; i < totalChunks; i++) {
    ({
      srcPos
    } = convertBlackAndWhiteToRGBA({
      src,
      srcPos,
      dest,
      width,
      height: i < fullChunks ? FULL_CHUNK_HEIGHT : partialChunkHeight,
      nonBlackColor: 0
    }));
    ctx.putImageData(chunkImgData, 0, i * FULL_CHUNK_HEIGHT);
  }
}
function copyCtxState(sourceCtx, destCtx) {
  const properties = ["strokeStyle", "fillStyle", "fillRule", "globalAlpha", "lineWidth", "lineCap", "lineJoin", "miterLimit", "globalCompositeOperation", "font", "filter"];
  for (const property of properties) {
    if (sourceCtx[property] !== undefined) {
      destCtx[property] = sourceCtx[property];
    }
  }
  if (sourceCtx.setLineDash !== undefined) {
    destCtx.setLineDash(sourceCtx.getLineDash());
    destCtx.lineDashOffset = sourceCtx.lineDashOffset;
  }
}
function setAnnotationCanvasName(canvas, canvasName) {
  canvas.setAttribute?.("data-canvas-name", canvasName);
  canvas._pdfjsCanvasName = canvasName;
}
function getAnnotationCanvasName(canvas) {
  return canvas._pdfjsCanvasName ?? null;
}
function resetCtxToDefault(ctx) {
  ctx.strokeStyle = ctx.fillStyle = "#000000";
  ctx.fillRule = "nonzero";
  ctx.globalAlpha = 1;
  ctx.lineWidth = 1;
  ctx.lineCap = "butt";
  ctx.lineJoin = "miter";
  ctx.miterLimit = 10;
  ctx.globalCompositeOperation = "source-over";
  ctx.font = "10px sans-serif";
  if (ctx.setLineDash !== undefined) {
    ctx.setLineDash([]);
    ctx.lineDashOffset = 0;
  }
  const {
    filter
  } = ctx;
  if (filter !== "none" && filter !== "") {
    ctx.filter = "none";
  }
}
class TransferMapsFallback {
  #maps;
  constructor(maps) {
    const [mapR, mapG = mapR, mapB = mapR] = maps;
    const identityMap = Util.identity0to256Map.slice();
    this.#maps = [mapR || identityMap, mapG || identityMap, mapB || identityMap];
  }
  applyToColor(color) {
    if (typeof color !== "string" || !color.startsWith("#")) {
      return color;
    }
    const [r, g, b] = getRGBA(color);
    const [mapR, mapG, mapB] = this.#maps;
    return Util.makeHexColor(mapR[r], mapG[g], mapB[b]);
  }
  applyToImageData({
    data
  }) {
    const [mapR, mapG, mapB] = this.#maps;
    for (let i = 0, ii = data.length; i < ii; i += 4) {
      data[i] = mapR[data[i]];
      data[i + 1] = mapG[data[i + 1]];
      data[i + 2] = mapB[data[i + 2]];
    }
  }
  applyToCanvas(ctx) {
    const {
      width,
      height
    } = ctx.canvas;
    const imgData = ctx.getImageData(0, 0, width, height);
    this.applyToImageData(imgData);
    ctx.putImageData(imgData, 0, 0);
  }
}
function getImageSmoothingEnabled(transform, interpolate, pixelRatio) {
  if (interpolate) {
    return true;
  }
  Util.singularValueDecompose2dScale(transform, XY);
  const actualScale = Math.fround(pixelRatio * PixelsPerInch.PDF_TO_CSS_UNITS);
  return XY[0] <= actualScale && XY[1] <= actualScale;
}
const LINE_CAP_STYLES = ["butt", "round", "square"];
const LINE_JOIN_STYLES = ["miter", "round", "bevel"];
const NORMAL_CLIP = {};
const EO_CLIP = {};
class CanvasGraphics {
  static #SCALE_MATRIX = null;
  static #pathCaches = new WeakMap();
  #knockoutGroupLevel = 0;
  #knockoutElementDepth = 0;
  #knockoutTempCanvasEntry = null;
  #knockoutSavedCtx = null;
  #knockoutSavedSMaskCtx = null;
  #knockoutSavedGCO = null;
  #knockoutElementAlpha = 1;
  #knockoutFilterCache;
  #knockoutElementGroupMeta = null;
  #groupStackMeta = [];
  constructor(canvasCtx, commonObjs, objs, canvasFactory, filterFactory, {
    optionalContentConfig,
    markedContentStack = null,
    pixelRatio = OutputScale.pixelRatio
  }, annotationCanvasMap, pageColors, dependencyTracker, imagesTracker) {
    this.ctx = canvasCtx;
    this.current = new CanvasExtraState(this.ctx.canvas.width, this.ctx.canvas.height);
    this.stateStack = [];
    this.pendingClip = null;
    this.pendingEOFill = false;
    this.commonObjs = commonObjs;
    this.objs = objs;
    this.canvasFactory = canvasFactory;
    this.filterFactory = filterFactory;
    this.groupStack = [];
    this.baseTransform = null;
    this.baseTransformStack = [];
    this.groupLevel = 0;
    this.smaskStack = [];
    this.tempSMask = null;
    this.smaskGroupCanvases = [];
    this.smaskPreparedEntry = null;
    this.smaskPreparedFor = null;
    this.smaskPreparedOffsetX = 0;
    this.smaskPreparedOffsetY = 0;
    this.smaskPreparedOOBAlpha = null;
    this.suspendedCtx = null;
    this.contentVisible = true;
    this.markedContentStack = markedContentStack || [];
    this.optionalContentConfig = optionalContentConfig;
    this.pixelRatio = pixelRatio;
    this.cachedPatterns = new Map();
    this.annotationCanvasMap = annotationCanvasMap;
    this.viewportScale = 1;
    this.outputScaleX = 1;
    this.outputScaleY = 1;
    this.pageColors = pageColors;
    this._cachedScaleForStroking = [-1, 0];
    this._cachedBitmapsMap = new Map();
    this.dependencyTracker = dependencyTracker ?? null;
    this.imagesTracker = imagesTracker ?? null;
  }
  getObject(opIdx, data, fallback = null) {
    if (typeof data === "string") {
      this.dependencyTracker?.recordNamedDependency(opIdx, data);
      return data.startsWith("g_") ? this.commonObjs.get(data) : this.objs.get(data);
    }
    return fallback;
  }
  beginDrawing({
    transform,
    viewport,
    transparency = false,
    background = null
  }) {
    const width = this.ctx.canvas.width;
    const height = this.ctx.canvas.height;
    const savedFillStyle = this.ctx.fillStyle;
    this.ctx.fillStyle = background || "#ffffff";
    this.ctx.fillRect(0, 0, width, height);
    this.ctx.fillStyle = savedFillStyle;
    if (transparency) {
      const transparentCanvas = this.transparentCanvasEntry = this.canvasFactory.create(width, height);
      this.compositeCtx = this.ctx;
      ({
        canvas: this.transparentCanvas,
        context: this.ctx
      } = transparentCanvas);
      this.ctx.save();
      this.ctx.transform(...getCurrentTransform(this.compositeCtx));
    }
    this.ctx.save();
    resetCtxToDefault(this.ctx);
    if (transform) {
      this.ctx.transform(...transform);
      this.outputScaleX = transform[0];
      this.outputScaleY = transform[3];
    }
    this.ctx.transform(...viewport.transform);
    this.viewportScale = viewport.scale;
    this.baseTransform = getCurrentTransform(this.ctx);
  }
  executeOperatorList(operatorList, executionStartIdx, continueCallback, errorCallback, stepper, operationsFilter) {
    const argsArray = operatorList.argsArray;
    const fnArray = operatorList.fnArray;
    const prevPathCache = this._pathCache;
    this._pathCache = operatorList.pathCache ?? CanvasGraphics.#pathCaches.getOrInsertComputed(operatorList, makeMap);
    try {
      let i = executionStartIdx || 0;
      const argsArrayLen = argsArray.length;
      if (argsArrayLen === i) {
        return i;
      }
      const chunkOperations = argsArrayLen - i > EXECUTION_STEPS && typeof continueCallback === "function";
      const endTime = chunkOperations ? Date.now() + EXECUTION_TIME : 0;
      let steps = 0;
      const commonObjs = this.commonObjs;
      const objs = this.objs;
      let fnId, fnArgs;
      while (true) {
        if (stepper !== undefined) {
          if (i === stepper.nextBreakPoint) {
            stepper.breakIt(i, continueCallback);
            return i;
          }
          if (stepper.shouldSkip(i)) {
            if (++i === argsArrayLen) {
              return i;
            }
            continue;
          }
        }
        if (!operationsFilter || operationsFilter(i, operatorList)) {
          fnId = fnArray[i];
          fnArgs = argsArray[i] ?? null;
          if (fnId !== OPS.dependency) {
            if (fnArgs === null) {
              this[fnId](i);
            } else {
              this[fnId](i, ...fnArgs);
            }
          } else {
            for (const depObjId of fnArgs) {
              this.dependencyTracker?.recordNamedData(depObjId, i);
              const objsPool = depObjId.startsWith("g_") ? commonObjs : objs;
              if (!objsPool.has(depObjId)) {
                objsPool.get(depObjId, continueCallback, errorCallback);
                return i;
              }
            }
          }
        }
        i++;
        if (i === argsArrayLen) {
          return i;
        }
        if (chunkOperations && ++steps > EXECUTION_STEPS) {
          if (Date.now() > endTime) {
            continueCallback();
            return i;
          }
          steps = 0;
        }
      }
    } finally {
      this._pathCache = prevPathCache;
    }
  }
  #restoreInitialState() {
    while (this.stateStack.length || this.inSMaskMode) {
      this.restore();
    }
    this.current.activeSMask = null;
    this.ctx.restore();
    if (this.transparentCanvas) {
      this.ctx = this.compositeCtx;
      this.ctx.save();
      this.ctx.setTransform(1, 0, 0, 1, 0, 0);
      this.ctx.drawImage(this.transparentCanvas, 0, 0);
      this.ctx.restore();
      this.canvasFactory.destroy(this.transparentCanvasEntry);
      this.transparentCanvas = null;
      this.transparentCanvasEntry = null;
    }
  }
  endDrawing() {
    this.#restoreInitialState();
    for (const canvas of this.smaskGroupCanvases) {
      this.canvasFactory.destroy(canvas);
    }
    this.smaskGroupCanvases.length = 0;
    this._clearPreparedSMask();
    this.tempSMask = null;
    this.smaskStack.length = 0;
    for (const meta of this.#groupStackMeta) {
      this.#destroyKnockoutPools(meta);
    }
    this.#groupStackMeta.length = 0;
    this.#knockoutTempCanvasEntry = null;
    this.#knockoutSavedCtx = null;
    this.#knockoutSavedSMaskCtx = null;
    this.#knockoutSavedGCO = null;
    this.#knockoutElementAlpha = 1;
    this.#knockoutElementGroupMeta = null;
    this.#knockoutElementDepth = 0;
    this.#knockoutGroupLevel = 0;
    this.cachedPatterns.clear();
    for (const cache of this._cachedBitmapsMap.values()) {
      for (const canvas of cache.values()) {
        if (typeof HTMLCanvasElement !== "undefined" && canvas instanceof HTMLCanvasElement) {
          canvas.width = canvas.height = 0;
        }
      }
      cache.clear();
    }
    this._cachedBitmapsMap.clear();
    this.#drawFilter();
  }
  #drawFilter() {
    if (this.pageColors) {
      const hcmFilterId = this.filterFactory.addHCMFilter(this.pageColors.foreground, this.pageColors.background);
      if (hcmFilterId !== "none") {
        const savedFilter = this.ctx.filter;
        this.ctx.filter = hcmFilterId;
        this.ctx.drawImage(this.ctx.canvas, 0, 0);
        this.ctx.filter = savedFilter;
      }
    }
  }
  _scaleImage(img, inverseTransform) {
    const width = img.width ?? img.displayWidth;
    const height = img.height ?? img.displayHeight;
    const widthScale = Math.max(Math.hypot(inverseTransform[0], inverseTransform[1]), 1);
    const heightScale = Math.max(Math.hypot(inverseTransform[2], inverseTransform[3]), 1);
    const scaleSteps = [];
    let ws = widthScale,
      hs = heightScale,
      pw = width,
      ph = height;
    while (ws > 2 && pw > 1 || hs > 2 && ph > 1) {
      let nw = pw,
        nh = ph;
      if (ws > 2 && pw > 1) {
        nw = Math.ceil(pw / 2);
        ws /= pw / nw;
      }
      if (hs > 2 && ph > 1) {
        nh = Math.ceil(ph / 2);
        hs /= ph / nh;
      }
      scaleSteps.push({
        newWidth: nw,
        newHeight: nh
      });
      pw = nw;
      ph = nh;
    }
    if (scaleSteps.length === 0) {
      return {
        img,
        paintWidth: width,
        paintHeight: height,
        tmpCanvas: null
      };
    }
    if (scaleSteps.length === 1) {
      const {
        newWidth,
        newHeight
      } = scaleSteps[0];
      const tmpCanvas = this.canvasFactory.create(newWidth, newHeight);
      tmpCanvas.context.drawImage(img, 0, 0, width, height, 0, 0, newWidth, newHeight);
      return {
        img: tmpCanvas.canvas,
        paintWidth: newWidth,
        paintHeight: newHeight,
        tmpCanvas
      };
    }
    let readEntry = this.canvasFactory.create(1, 1);
    let writeEntry = this.canvasFactory.create(1, 1);
    let paintWidth = width,
      paintHeight = height;
    let source = img;
    for (const {
      newWidth,
      newHeight
    } of scaleSteps) {
      this.canvasFactory.reset(writeEntry, newWidth, newHeight);
      writeEntry.context.drawImage(source, 0, 0, paintWidth, paintHeight, 0, 0, newWidth, newHeight);
      [readEntry, writeEntry] = [writeEntry, readEntry];
      source = readEntry.canvas;
      paintWidth = newWidth;
      paintHeight = newHeight;
    }
    this.canvasFactory.destroy(writeEntry);
    return {
      img: readEntry.canvas,
      paintWidth,
      paintHeight,
      tmpCanvas: readEntry
    };
  }
  _createMaskCanvas(opIdx, img) {
    const ctx = this.ctx;
    const {
      width,
      height
    } = img;
    const isPatternFill = this.current.patternFill;
    const fillColor = isPatternFill ? this.current.fillColor : ctx.fillStyle;
    const currentTransform = getCurrentTransform(ctx);
    let cache, cacheKey, scaled, maskCanvas;
    if ((img.bitmap || img.data) && img.count > 1) {
      const mainKey = img.bitmap || img.data.buffer;
      cacheKey = JSON.stringify(isPatternFill ? currentTransform : [currentTransform.slice(0, 4), fillColor]);
      cache = this._cachedBitmapsMap.getOrInsertComputed(mainKey, makeMap);
      const cachedImage = cache.get(cacheKey);
      if (cachedImage && !isPatternFill) {
        const offsetX = Math.round(Math.min(currentTransform[0], currentTransform[2]) + currentTransform[4]);
        const offsetY = Math.round(Math.min(currentTransform[1], currentTransform[3]) + currentTransform[5]);
        this.dependencyTracker?.recordDependencies(opIdx, Dependencies.transformAndFill);
        return {
          canvas: cachedImage,
          offsetX,
          offsetY
        };
      }
      scaled = cachedImage;
    }
    if (!scaled) {
      maskCanvas = this.canvasFactory.create(width, height);
      putBinaryImageMask(maskCanvas.context, img);
    }
    let maskToCanvas = Util.transform(currentTransform, [1 / width, 0, 0, -1 / height, 0, 0]);
    maskToCanvas = Util.transform(maskToCanvas, [1, 0, 0, 1, 0, -height]);
    const minMax = F32_BBOX_INIT.slice();
    Util.axialAlignedBoundingBox([0, 0, width, height], maskToCanvas, minMax);
    const [minX, minY, maxX, maxY] = minMax;
    const drawnWidth = Math.round(maxX - minX) || 1;
    const drawnHeight = Math.round(maxY - minY) || 1;
    const fillCanvas = this.canvasFactory.create(drawnWidth, drawnHeight);
    const fillCtx = fillCanvas.context;
    const offsetX = minX;
    const offsetY = minY;
    fillCtx.translate(-offsetX, -offsetY);
    fillCtx.transform(...maskToCanvas);
    let scaledEntry = null;
    if (!scaled) {
      const scaleResult = this._scaleImage(maskCanvas.canvas, getCurrentTransformInverse(fillCtx));
      scaled = scaleResult.img;
      scaledEntry = scaleResult.tmpCanvas;
      if (scaled !== maskCanvas.canvas) {
        this.canvasFactory.destroy(maskCanvas);
        maskCanvas = null;
      }
      if (cache && isPatternFill) {
        cache.set(cacheKey, scaled);
        scaledEntry = null;
        maskCanvas = null;
      }
    }
    fillCtx.imageSmoothingEnabled = getImageSmoothingEnabled(getCurrentTransform(fillCtx), img.interpolate, this.pixelRatio);
    drawImageAtIntegerCoords(fillCtx, scaled, 0, 0, scaled.width, scaled.height, 0, 0, width, height);
    if (scaledEntry) {
      this.canvasFactory.destroy(scaledEntry);
    }
    if (maskCanvas) {
      this.canvasFactory.destroy(maskCanvas);
    }
    fillCtx.globalCompositeOperation = "source-in";
    const inverse = Util.transform(getCurrentTransformInverse(fillCtx), [1, 0, 0, 1, -offsetX, -offsetY]);
    fillCtx.fillStyle = isPatternFill ? fillColor.getPattern(ctx, this, inverse, PathType.FILL, opIdx) : fillColor;
    fillCtx.fillRect(0, 0, width, height);
    if (cache && !isPatternFill) {
      cache.set(cacheKey, fillCanvas.canvas);
    }
    this.dependencyTracker?.recordDependencies(opIdx, Dependencies.transformAndFill);
    return {
      canvas: fillCanvas.canvas,
      canvasEntry: cache && !isPatternFill ? null : fillCanvas,
      offsetX: Math.round(offsetX),
      offsetY: Math.round(offsetY)
    };
  }
  setLineWidth(opIdx, width) {
    this.dependencyTracker?.recordSimpleData("lineWidth", opIdx);
    if (width !== this.current.lineWidth) {
      this._cachedScaleForStroking[0] = -1;
    }
    this.current.lineWidth = width;
    this.ctx.lineWidth = width;
  }
  setLineCap(opIdx, style) {
    this.dependencyTracker?.recordSimpleData("lineCap", opIdx);
    this.ctx.lineCap = LINE_CAP_STYLES[style];
  }
  setLineJoin(opIdx, style) {
    this.dependencyTracker?.recordSimpleData("lineJoin", opIdx);
    this.ctx.lineJoin = LINE_JOIN_STYLES[style];
  }
  setMiterLimit(opIdx, limit) {
    this.dependencyTracker?.recordSimpleData("miterLimit", opIdx);
    this.ctx.miterLimit = limit;
  }
  setDash(opIdx, dashArray, dashPhase) {
    this.dependencyTracker?.recordSimpleData("dash", opIdx);
    const ctx = this.ctx;
    if (ctx.setLineDash !== undefined) {
      ctx.setLineDash(dashArray);
      ctx.lineDashOffset = dashPhase;
    }
  }
  setRenderingIntent(opIdx, intent) {}
  setFlatness(opIdx, flatness) {}
  setGState(opIdx, states) {
    for (const [key, value] of states) {
      switch (key) {
        case "LW":
          this.setLineWidth(opIdx, value);
          break;
        case "LC":
          this.setLineCap(opIdx, value);
          break;
        case "LJ":
          this.setLineJoin(opIdx, value);
          break;
        case "ML":
          this.setMiterLimit(opIdx, value);
          break;
        case "D":
          this.setDash(opIdx, value[0], value[1]);
          break;
        case "RI":
          this.setRenderingIntent(opIdx, value);
          break;
        case "FL":
          this.setFlatness(opIdx, value);
          break;
        case "Font":
          this.setFont(opIdx, value[0], value[1]);
          break;
        case "CA":
          this.dependencyTracker?.recordSimpleData("strokeAlpha", opIdx);
          this.current.strokeAlpha = value;
          break;
        case "ca":
          this.dependencyTracker?.recordSimpleData("fillAlpha", opIdx);
          this.ctx.globalAlpha = this.current.fillAlpha = value;
          break;
        case "BM":
          this.dependencyTracker?.recordSimpleData("globalCompositeOperation", opIdx);
          this.ctx.globalCompositeOperation = value;
          break;
        case "SMask":
          this.dependencyTracker?.recordSimpleData("SMask", opIdx);
          this.current.activeSMask = value ? this.tempSMask : null;
          if (this.current.activeSMask) {
            this.current.activeSMask.blendMode = this.ctx.globalCompositeOperation;
          }
          this.tempSMask = null;
          this.checkSMaskState(opIdx);
          break;
        case "TR":
          {
            this.dependencyTracker?.recordSimpleData("filter", opIdx);
            let filter = this.filterFactory.addFilter(value);
            this.ctx.filter = filter;
            let fallback = null;
            if (value && (filter === "none" || !FeatureTest.isCanvasFilterSupported || this.ctx.filter === "none" || this.ctx.filter === "")) {
              this.ctx.filter = filter = "none";
              fallback = new TransferMapsFallback(value);
            }
            this.current.transferMaps = filter;
            if (fallback || this.current.transferMapsFallback) {
              this.current.transferMapsFallback = fallback;
              if (!this.current.patternFill) {
                this.ctx.fillStyle = this.#transferColor(this.current.fillColor);
              }
              if (!this.current.patternStroke) {
                this.ctx.strokeStyle = this.#transferColor(this.current.strokeColor);
              }
            }
            break;
          }
      }
    }
  }
  get inSMaskMode() {
    return !!this.suspendedCtx;
  }
  _clearPreparedSMask() {
    if (this.smaskPreparedEntry) {
      this.canvasFactory.destroy(this.smaskPreparedEntry);
      this.smaskPreparedEntry = null;
    }
    this.smaskPreparedFor = null;
    this.smaskPreparedOffsetX = 0;
    this.smaskPreparedOffsetY = 0;
    this.smaskPreparedOOBAlpha = null;
  }
  _ensurePreparedSMask(smask) {
    if (smask === this.smaskPreparedFor) {
      return;
    }
    this._clearPreparedSMask();
    this._prepareSMaskCanvas(smask);
  }
  checkSMaskState(opIdx) {
    const inSMaskMode = this.inSMaskMode;
    if (this.current.activeSMask && !inSMaskMode) {
      this.beginSMaskMode(opIdx);
    } else if (!this.current.activeSMask && inSMaskMode) {
      this.endSMaskMode();
    } else if (this.current.activeSMask && inSMaskMode) {
      this._ensurePreparedSMask(this.current.activeSMask);
    }
  }
  _prepareSMaskCanvas(smask) {
    const {
      canvas: maskCanvas,
      subtype,
      backdrop,
      transferMap
    } = smask;
    const hasFilter = subtype === "Luminosity" || subtype === "Alpha" && transferMap;
    if (!hasFilter && !(subtype === "Luminosity" && backdrop)) {
      this.smaskPreparedFor = smask;
      return;
    }
    let filteredOOBAlpha;
    if (subtype === "Luminosity" && backdrop) {
      const [r, g, b] = getRGBA(backdrop);
      const inputAlpha = Math.round(0.3 * r + 0.59 * g + 0.11 * b);
      filteredOOBAlpha = transferMap?.[inputAlpha] ?? inputAlpha;
    } else {
      filteredOOBAlpha = transferMap?.[0] ?? 0;
    }
    const SMASK_LAYER_TO_MASK_AREA_RATIO = 4;
    const {
      width: layerW,
      height: layerH
    } = this.ctx.canvas;
    const maskArea = maskCanvas.width * maskCanvas.height;
    const useLayerSize = layerW * layerH < SMASK_LAYER_TO_MASK_AREA_RATIO * maskArea;
    const filterSpec = hasFilter ? {
      url: subtype === "Alpha" ? this.filterFactory.addAlphaFilter(transferMap) : this.filterFactory.addLuminosityFilter(transferMap),
      subtype,
      transferMap
    } : null;
    const bakedBackdrop = subtype === "Luminosity" ? backdrop : null;
    let preparedEntry, offsetX, offsetY;
    if (useLayerSize) {
      preparedEntry = this._bakeSMaskCanvas(maskCanvas, smask.offsetX, smask.offsetY, layerW, layerH, bakedBackdrop, filterSpec);
      offsetX = 0;
      offsetY = 0;
    } else {
      preparedEntry = this._bakeSMaskCanvas(maskCanvas, 0, 0, maskCanvas.width, maskCanvas.height, bakedBackdrop, filterSpec);
      offsetX = smask.offsetX;
      offsetY = smask.offsetY;
    }
    this.smaskPreparedEntry = preparedEntry;
    this.smaskPreparedFor = smask;
    this.smaskPreparedOffsetX = offsetX;
    this.smaskPreparedOffsetY = offsetY;
    this.smaskPreparedOOBAlpha = !useLayerSize && filteredOOBAlpha !== 0 ? filteredOOBAlpha : null;
  }
  _bakeSMaskCanvas(maskCanvas, drawX, drawY, w, h, backdrop, filterSpec) {
    if (!backdrop && !filterSpec) {
      unreachable("_bakeSMaskCanvas with neither backdrop nor filter");
    }
    const srcEntry = this.canvasFactory.create(w, h);
    const sCtx = srcEntry.context;
    sCtx.drawImage(maskCanvas, drawX, drawY);
    if (backdrop) {
      sCtx.globalCompositeOperation = "destination-atop";
      sCtx.fillStyle = backdrop;
      sCtx.fillRect(0, 0, w, h);
    }
    if (!filterSpec) {
      return srcEntry;
    }
    const preparedEntry = this.canvasFactory.create(w, h);
    const pCtx = preparedEntry.context;
    pCtx.filter = filterSpec.url;
    const filterApplied = FeatureTest.isCanvasFilterSupported && pCtx.filter !== "none" && pCtx.filter !== "";
    pCtx.drawImage(srcEntry.canvas, 0, 0);
    if (FeatureTest.isCanvasFilterSupported) {
      pCtx.filter = "none";
    }
    if (!filterApplied) {
      const img = pCtx.getImageData(0, 0, w, h);
      const {
        data
      } = img;
      const {
        transferMap
      } = filterSpec;
      if (filterSpec.subtype === "Luminosity") {
        for (let i = 0, ii = data.length; i < ii; i += 4) {
          const a = 0.3 * data[i] + 0.59 * data[i + 1] + 0.11 * data[i + 2] + 0.5 | 0;
          data[i] = data[i + 1] = data[i + 2] = 0;
          data[i + 3] = transferMap?.[a] ?? a;
        }
      } else {
        for (let i = 3, ii = data.length; i < ii; i += 4) {
          data[i] = transferMap[data[i]];
        }
      }
      pCtx.putImageData(img, 0, 0);
    }
    this.canvasFactory.destroy(srcEntry);
    return preparedEntry;
  }
  beginSMaskMode(opIdx) {
    if (this.inSMaskMode) {
      throw new Error("beginSMaskMode called while already in smask mode");
    }
    const {
      width: drawnWidth,
      height: drawnHeight
    } = this.ctx.canvas;
    const scratchCanvas = this.canvasFactory.create(drawnWidth, drawnHeight);
    this.smaskScratchCanvas = scratchCanvas;
    this.suspendedCtx = this.ctx;
    const ctx = this.ctx = scratchCanvas.context;
    ctx.setTransform(this.suspendedCtx.getTransform());
    copyCtxState(this.suspendedCtx, ctx);
    mirrorContextOperations(ctx, this.suspendedCtx);
    this._ensurePreparedSMask(this.current.activeSMask);
    this.setGState(opIdx, [["BM", "source-over"]]);
  }
  endSMaskMode() {
    if (!this.inSMaskMode) {
      throw new Error("endSMaskMode called while not in smask mode");
    }
    this.ctx._removeMirroring();
    copyCtxState(this.ctx, this.suspendedCtx);
    this.ctx = this.suspendedCtx;
    this.suspendedCtx = null;
    this.canvasFactory.destroy(this.smaskScratchCanvas);
    this.smaskScratchCanvas = null;
    this._clearPreparedSMask();
  }
  #createKnockoutMaskCanvas(sourceCanvas, reuseEntry = null, alpha = 1) {
    const {
      width,
      height
    } = sourceCanvas;
    const maskEntry = reuseEntry ?? this.canvasFactory.create(width, height);
    const maskCtx = maskEntry.context;
    alpha = Math.round(alpha * 255) / 255;
    const needsAlphaScaling = alpha < 1;
    if (needsAlphaScaling && this.#knockoutFilterCache === undefined) {
      this.#knockoutFilterCache = FeatureTest.isCanvasFilterSupported ? new Map() : "none";
    }
    let knockoutFilter = "none";
    if (needsAlphaScaling && this.#knockoutFilterCache instanceof Map) {
      knockoutFilter = this.#knockoutFilterCache.getOrInsertComputed(alpha, () => this.filterFactory.addKnockoutFilter(alpha));
    }
    if (!needsAlphaScaling || knockoutFilter !== "none") {
      if (reuseEntry) {
        maskCtx.save();
        maskCtx.setTransform(1, 0, 0, 1, 0, 0);
        maskCtx.clearRect(0, 0, width, height);
        maskCtx.restore();
      }
      maskCtx.filter = knockoutFilter;
      maskCtx.drawImage(sourceCanvas, 0, 0);
      maskCtx.filter = "none";
      return maskEntry;
    }
    const sourceData = sourceCanvas.getContext("2d", {
      willReadFrequently: true
    }).getImageData(0, 0, width, height);
    const maskData = maskCtx.createImageData(width, height);
    const sourcePixels = sourceData.data,
      maskPixels = maskData.data;
    const alphaScale = alpha > 0 ? 1 / alpha : 1e6;
    for (let i = 3, ii = sourcePixels.length; i < ii; i += 4) {
      maskPixels[i] = Math.min(Math.round(sourcePixels[i] * alphaScale), 255);
    }
    maskCtx.putImageData(maskData, 0, 0);
    return maskEntry;
  }
  #getOrCreatePooledEntry(meta, key, width, height) {
    let entry = meta?.[key] ?? null;
    if (entry && (entry.canvas.width !== width || entry.canvas.height !== height)) {
      this.canvasFactory.destroy(entry);
      entry = null;
    }
    if (!entry) {
      entry = this.canvasFactory.create(width, height);
      if (meta) {
        meta[key] = entry;
      }
      return entry;
    }
    const ctx = entry.context;
    ctx.save();
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    ctx.clearRect(0, 0, width, height);
    ctx.restore();
    return entry;
  }
  #compositeKnockoutSurface(destCtx, surfaceCanvas, options = {}) {
    const {
      backdropCanvas = null,
      destTransform = [1, 0, 0, 1, 0, 0],
      backdropOffset = [0, 0],
      reuseMaskEntry = null,
      poolMeta = null,
      sourceAlpha = 1,
      sourceFilter = "none",
      knockoutAlpha = 1
    } = options;
    const {
      width,
      height
    } = surfaceCanvas;
    const knockoutMaskEntry = this.#createKnockoutMaskCanvas(surfaceCanvas, reuseMaskEntry, knockoutAlpha);
    const sourceCompositeOperation = destCtx.globalCompositeOperation;
    destCtx.save();
    destCtx.setTransform(...destTransform);
    destCtx.globalAlpha = 1;
    if (FeatureTest.isCanvasFilterSupported) {
      destCtx.filter = "none";
    }
    destCtx.globalCompositeOperation = "destination-out";
    destCtx.drawImage(knockoutMaskEntry.canvas, 0, 0);
    if (backdropCanvas) {
      const [bx, by] = backdropOffset;
      const backdropEntry = this.#getOrCreatePooledEntry(poolMeta, "knockoutBackdropEntry", width, height);
      const backdropCtx = backdropEntry.context;
      backdropCtx.drawImage(backdropCanvas, bx, by, width, height, 0, 0, width, height);
      backdropCtx.globalCompositeOperation = "destination-in";
      backdropCtx.drawImage(knockoutMaskEntry.canvas, 0, 0);
      backdropCtx.globalCompositeOperation = "source-over";
      destCtx.globalCompositeOperation = "destination-over";
      destCtx.drawImage(backdropEntry.canvas, 0, 0);
      if (!poolMeta) {
        this.canvasFactory.destroy(backdropEntry);
      }
    }
    destCtx.globalCompositeOperation = sourceCompositeOperation;
    destCtx.globalAlpha = sourceAlpha;
    if (FeatureTest.isCanvasFilterSupported) {
      destCtx.filter = sourceFilter ?? "none";
    }
    destCtx.drawImage(surfaceCanvas, 0, 0);
    destCtx.restore();
    if (!reuseMaskEntry) {
      this.canvasFactory.destroy(knockoutMaskEntry);
    }
  }
  #beginKnockoutElement(alpha = 1) {
    if (this.#knockoutGroupLevel === 0 || this.#knockoutElementDepth > 0 || !this.contentVisible) {
      return false;
    }
    this.#knockoutElementDepth++;
    this.#knockoutElementAlpha = alpha;
    const groupMeta = this.#groupStackMeta.at(-1);
    const {
      canvas
    } = this.ctx;
    const tempEntry = this.#getOrCreatePooledEntry(groupMeta, "knockoutTempEntry", canvas.width, canvas.height);
    this.#knockoutTempCanvasEntry = tempEntry;
    const tempCtx = tempEntry.context;
    tempCtx.save();
    tempCtx.setTransform(this.ctx.getTransform());
    copyCtxState(this.ctx, tempCtx);
    this.#knockoutSavedGCO = tempCtx.globalCompositeOperation;
    tempCtx.globalCompositeOperation = "source-over";
    mirrorContextOperations(tempCtx, this.ctx);
    this.#knockoutElementGroupMeta = groupMeta;
    this.#knockoutSavedCtx = this.ctx;
    this.#knockoutSavedSMaskCtx = this.suspendedCtx;
    this.ctx = tempCtx;
    if (this.inSMaskMode) {
      this.suspendedCtx = tempCtx;
    }
    return true;
  }
  #endKnockoutElement(started) {
    if (!started) {
      return;
    }
    const tempEntry = this.#knockoutTempCanvasEntry;
    const savedCtx = this.#knockoutSavedCtx;
    const savedSMaskCtx = this.#knockoutSavedSMaskCtx;
    const tempCtx = tempEntry.context;
    this.#knockoutTempCanvasEntry = null;
    this.#knockoutSavedCtx = null;
    this.#knockoutSavedSMaskCtx = null;
    if (this.inSMaskMode && this.suspendedCtx === tempCtx && this.ctx !== tempCtx) {
      this.endSMaskMode();
    }
    if (this.inSMaskMode) {
      this.suspendedCtx = savedSMaskCtx;
    }
    this.ctx._removeMirroring();
    this.ctx.globalCompositeOperation = this.#knockoutSavedGCO;
    this.#knockoutSavedGCO = null;
    copyCtxState(this.ctx, savedCtx);
    this.ctx = savedCtx;
    const groupMeta = this.#knockoutElementGroupMeta;
    this.#knockoutElementGroupMeta = null;
    const knockoutAlpha = this.#knockoutElementAlpha;
    this.#knockoutElementAlpha = 1;
    try {
      this.#compositeKnockoutSurface(savedSMaskCtx ?? savedCtx, tempEntry.canvas, {
        backdropCanvas: groupMeta?.backdropCtx?.canvas ?? null,
        backdropOffset: groupMeta?.backdropCtx ? [groupMeta.offsetX, groupMeta.offsetY] : [0, 0],
        reuseMaskEntry: groupMeta?.knockoutMaskEntry ?? null,
        poolMeta: groupMeta,
        knockoutAlpha
      });
    } finally {
      tempCtx.restore();
      this.#knockoutElementDepth--;
      if (!groupMeta) {
        this.canvasFactory.destroy(tempEntry);
      }
    }
  }
  compose(dirtyBox) {
    if (!this.current.activeSMask) {
      return;
    }
    dirtyBox = dirtyBox ? [Math.floor(dirtyBox[0]), Math.floor(dirtyBox[1]), Math.ceil(dirtyBox[2]), Math.ceil(dirtyBox[3])] : [0, 0, this.ctx.canvas.width, this.ctx.canvas.height];
    const smask = this.current.activeSMask;
    const suspendedCtx = this.suspendedCtx;
    const applySMaskInPlace = this.#knockoutElementDepth > 0 && suspendedCtx === this.ctx;
    this.composeSMask(applySMaskInPlace ? null : suspendedCtx, smask, this.ctx, dirtyBox);
    if (applySMaskInPlace) {
      return;
    }
    this.ctx.save();
    this.ctx.setTransform(1, 0, 0, 1, 0, 0);
    this.ctx.clearRect(0, 0, this.ctx.canvas.width, this.ctx.canvas.height);
    this.ctx.restore();
  }
  composeSMask(ctx, smask, layerCtx, layerBox) {
    const layerOffsetX = layerBox[0];
    const layerOffsetY = layerBox[1];
    const layerWidth = layerBox[2] - layerOffsetX;
    const layerHeight = layerBox[3] - layerOffsetY;
    if (layerWidth === 0 || layerHeight === 0) {
      return;
    }
    const preparedEntry = this.smaskPreparedEntry;
    if (preparedEntry) {
      let clipX = layerOffsetX;
      let clipY = layerOffsetY;
      let clipW = layerWidth;
      let clipH = layerHeight;
      const oobAlpha = this.smaskPreparedOOBAlpha;
      const hasOOBAlpha = oobAlpha !== null;
      if (hasOOBAlpha) {
        clipX = Math.max(layerOffsetX, smask.offsetX);
        clipY = Math.max(layerOffsetY, smask.offsetY);
        const x1 = Math.min(layerOffsetX + layerWidth, smask.offsetX + smask.canvas.width);
        const y1 = Math.min(layerOffsetY + layerHeight, smask.offsetY + smask.canvas.height);
        clipW = x1 - clipX;
        clipH = y1 - clipY;
      }
      if (clipW > 0 && clipH > 0) {
        const srcX = clipX - this.smaskPreparedOffsetX;
        const srcY = clipY - this.smaskPreparedOffsetY;
        layerCtx.save();
        layerCtx.globalAlpha = 1;
        layerCtx.setTransform(1, 0, 0, 1, 0, 0);
        const clip = new Path2D();
        clip.rect(clipX, clipY, clipW, clipH);
        layerCtx.clip(clip);
        layerCtx.globalCompositeOperation = "destination-in";
        layerCtx.drawImage(preparedEntry.canvas, srcX, srcY, clipW, clipH, clipX, clipY, clipW, clipH);
        layerCtx.restore();
      }
      if (hasOOBAlpha && oobAlpha < 255) {
        this._applySMaskOOBAlpha(layerCtx, layerOffsetX, layerOffsetY, layerWidth, layerHeight, clipX, clipY, clipX + clipW, clipY + clipH, oobAlpha);
      }
    } else {
      this.genericComposeSMask(smask, layerCtx, layerWidth, layerHeight, layerOffsetX, layerOffsetY);
    }
    if (!ctx) {
      return;
    }
    ctx.save();
    ctx.globalAlpha = 1;
    ctx.globalCompositeOperation = smask.blendMode || "source-over";
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    ctx.drawImage(layerCtx.canvas, layerOffsetX, layerOffsetY, layerWidth, layerHeight, layerOffsetX, layerOffsetY, layerWidth, layerHeight);
    ctx.restore();
  }
  _applySMaskOOBAlpha(layerCtx, layerOffsetX, layerOffsetY, layerWidth, layerHeight, maskX0, maskY0, maskX1, maskY1, alpha) {
    const hasInnerCutout = maskX0 < maskX1 && maskY0 < maskY1;
    if (hasInnerCutout && maskX0 === layerOffsetX && maskY0 === layerOffsetY && maskX1 === layerOffsetX + layerWidth && maskY1 === layerOffsetY + layerHeight) {
      return;
    }
    const path = new Path2D();
    path.rect(layerOffsetX, layerOffsetY, layerWidth, layerHeight);
    if (hasInnerCutout) {
      path.rect(maskX0, maskY0, maskX1 - maskX0, maskY1 - maskY0);
    }
    layerCtx.save();
    layerCtx.globalAlpha = alpha / 255;
    layerCtx.setTransform(1, 0, 0, 1, 0, 0);
    layerCtx.clip(path, "evenodd");
    layerCtx.globalCompositeOperation = "destination-in";
    layerCtx.fillStyle = "#000000";
    layerCtx.fillRect(layerOffsetX, layerOffsetY, layerWidth, layerHeight);
    layerCtx.restore();
  }
  genericComposeSMask(smask, layerCtx, width, height, layerOffsetX, layerOffsetY) {
    const {
      context: maskCtx,
      offsetX: maskOffsetX,
      offsetY: maskOffsetY
    } = smask;
    layerCtx.save();
    layerCtx.globalAlpha = 1;
    layerCtx.setTransform(1, 0, 0, 1, 0, 0);
    const clip = new Path2D();
    clip.rect(layerOffsetX, layerOffsetY, width, height);
    layerCtx.clip(clip);
    layerCtx.globalCompositeOperation = "destination-in";
    layerCtx.drawImage(maskCtx.canvas, layerOffsetX - maskOffsetX, layerOffsetY - maskOffsetY, width, height, layerOffsetX, layerOffsetY, width, height);
    layerCtx.restore();
  }
  save(opIdx) {
    if (this.inSMaskMode) {
      copyCtxState(this.ctx, this.suspendedCtx);
    }
    this.ctx.save();
    const old = this.current;
    this.stateStack.push(old);
    this.current = old.clone();
    this.dependencyTracker?.save(opIdx);
  }
  restore(opIdx) {
    this.dependencyTracker?.restore(opIdx);
    if (this.stateStack.length === 0) {
      if (this.inSMaskMode) {
        this.endSMaskMode();
      }
      return;
    }
    this.current = this.stateStack.pop();
    this.ctx.restore();
    if (this.inSMaskMode) {
      copyCtxState(this.suspendedCtx, this.ctx);
      this.ctx.setTransform(this.suspendedCtx.getTransform());
    }
    this.checkSMaskState(opIdx);
    this.pendingClip = null;
    this._cachedScaleForStroking[0] = -1;
  }
  resetGraphicsState() {
    const {
      clipBox,
      transferMaps,
      transferMapsFallback
    } = this.current;
    const current = this.current = new CanvasExtraState(0, 0);
    current.clipBox = clipBox;
    current.transferMaps = transferMaps;
    current.transferMapsFallback = transferMapsFallback;
    const ctx = this.ctx;
    ctx.lineWidth = 1;
    ctx.lineCap = "butt";
    ctx.lineJoin = "miter";
    ctx.miterLimit = 10;
    if (ctx.setLineDash !== undefined) {
      ctx.setLineDash([]);
      ctx.lineDashOffset = 0;
    }
    this._cachedScaleForStroking[0] = -1;
  }
  transform(opIdx, a, b, c, d, e, f) {
    this.dependencyTracker?.recordIncrementalData("transform", opIdx);
    this.ctx.transform(a, b, c, d, e, f);
    this._cachedScaleForStroking[0] = -1;
  }
  constructPath(opIdx, op, data, minMax) {
    let path = this._pathCache.get(opIdx);
    if (!minMax) {
      if (!path) {
        path = new Path2D();
        this._pathCache.set(opIdx, path);
      }
      if (op !== OPS.stroke && op !== OPS.closeStroke) {
        this.current.tilingPatternDims = null;
      }
      this[op](opIdx, path);
      return;
    }
    if (this.dependencyTracker !== null) {
      const outerExtraSize = op === OPS.stroke ? this.current.lineWidth / 2 : 0;
      this.dependencyTracker.resetBBox(opIdx).recordBBox(opIdx, this.ctx, minMax[0] - outerExtraSize, minMax[2] + outerExtraSize, minMax[1] - outerExtraSize, minMax[3] + outerExtraSize).recordDependencies(opIdx, ["transform"]);
    }
    if (!path) {
      path = makePathFromDrawOPS(data[0]);
      this._pathCache.set(opIdx, path);
    }
    Util.axialAlignedBoundingBox(minMax, getCurrentTransform(this.ctx), this.current.minMax);
    const tilingDims = this.current.tilingPatternDims;
    if (tilingDims && op !== OPS.stroke && op !== OPS.closeStroke && this.current.fillColor instanceof TilingPattern) {
      const clippedBBox = Util.intersect(this.current.clipBox, this.current.minMax);
      if (!clippedBBox) {
        this.current.tilingPatternDims = null;
      } else {
        this.current.fillColor.updatePatternDims(clippedBBox, tilingDims);
      }
    }
    this[op](opIdx, path);
    this._pathStartIdx = opIdx;
  }
  closePath(opIdx) {
    this.ctx.closePath();
  }
  stroke(opIdx, path, consumePath = true) {
    const started = consumePath && this.#beginKnockoutElement(this.current.strokeAlpha);
    const ctx = this.ctx;
    const strokeColor = this.current.strokeColor;
    ctx.globalAlpha = this.current.strokeAlpha;
    if (this.contentVisible) {
      if (typeof strokeColor === "object" && strokeColor?.getPattern) {
        const baseTransform = strokeColor.isModifyingCurrentTransform() ? ctx.getTransform() : null;
        ctx.save();
        ctx.strokeStyle = strokeColor.getPattern(ctx, this, getCurrentTransformInverse(ctx), PathType.STROKE, opIdx);
        if (baseTransform) {
          const newPath = new Path2D();
          newPath.addPath(path, ctx.getTransform().invertSelf().multiplySelf(baseTransform));
          path = newPath;
        }
        this.rescaleAndStroke(path, false);
        ctx.restore();
      } else {
        this.rescaleAndStroke(path, true);
      }
    }
    this.dependencyTracker?.recordDependencies(opIdx, Dependencies.stroke);
    if (consumePath) {
      this.consumePath(opIdx, path, this.current.getClippedPathBoundingBox(PathType.STROKE, getCurrentTransform(this.ctx)));
    }
    ctx.globalAlpha = this.current.fillAlpha;
    this.#endKnockoutElement(started);
  }
  closeStroke(opIdx, path) {
    this.stroke(opIdx, path);
  }
  fill(opIdx, path, consumePath = true) {
    const started = consumePath && this.#beginKnockoutElement(this.current.fillAlpha);
    const ctx = this.ctx;
    const fillColor = this.current.fillColor;
    const isPatternFill = this.current.patternFill;
    let needRestore = false;
    const intersect = this.current.getClippedPathBoundingBox();
    this.dependencyTracker?.recordDependencies(opIdx, Dependencies.fill);
    if (isPatternFill) {
      const dims = this.current.tilingPatternDims;
      const tileIdx = dims && fillColor.canSkipPatternCanvas(dims);
      if (tileIdx) {
        const {
          pendingClip,
          pendingEOFill
        } = this;
        this.pendingClip = null;
        this.pendingEOFill = false;
        fillColor.drawPattern(this, path, pendingEOFill, tileIdx, opIdx);
        this.pendingClip = pendingClip;
        if (consumePath) {
          this.consumePath(opIdx, path, intersect);
        }
        this.current.tilingPatternDims = null;
        this.#endKnockoutElement(started);
        return;
      }
      const baseTransform = fillColor.isModifyingCurrentTransform() ? ctx.getTransform() : null;
      this.dependencyTracker?.save(opIdx);
      ctx.save();
      ctx.fillStyle = fillColor.getPattern(ctx, this, getCurrentTransformInverse(ctx), PathType.FILL, opIdx);
      if (baseTransform) {
        const newPath = new Path2D();
        newPath.addPath(path, ctx.getTransform().invertSelf().multiplySelf(baseTransform));
        path = newPath;
      }
      needRestore = true;
    }
    if (this.contentVisible && intersect !== null) {
      if (this.pendingEOFill) {
        ctx.fill(path, "evenodd");
        this.pendingEOFill = false;
      } else {
        ctx.fill(path);
      }
    }
    if (needRestore) {
      ctx.restore();
      this.dependencyTracker?.restore(opIdx);
    }
    if (consumePath) {
      this.consumePath(opIdx, path, intersect);
    }
    this.#endKnockoutElement(started);
  }
  eoFill(opIdx, path) {
    this.pendingEOFill = true;
    this.fill(opIdx, path);
  }
  fillStroke(opIdx, path) {
    const started = this.#beginKnockoutElement(Math.min(this.current.fillAlpha, this.current.strokeAlpha));
    this.fill(opIdx, path, false);
    this.stroke(opIdx, path, false);
    this.consumePath(opIdx, path);
    this.#endKnockoutElement(started);
  }
  eoFillStroke(opIdx, path) {
    this.pendingEOFill = true;
    this.fillStroke(opIdx, path);
  }
  closeFillStroke(opIdx, path) {
    this.fillStroke(opIdx, path);
  }
  closeEOFillStroke(opIdx, path) {
    this.pendingEOFill = true;
    this.fillStroke(opIdx, path);
  }
  endPath(opIdx, path) {
    this.consumePath(opIdx, path);
  }
  rawFillPath(opIdx, path) {
    const started = this.#beginKnockoutElement(this.current.fillAlpha);
    this.ctx.fill(path);
    this.dependencyTracker?.recordDependencies(opIdx, Dependencies.rawFillPath).recordOperation(opIdx);
    this.#endKnockoutElement(started);
  }
  clip(opIdx) {
    this.dependencyTracker?.recordFutureForcedDependency("clipMode", opIdx);
    this.pendingClip = NORMAL_CLIP;
  }
  eoClip(opIdx) {
    this.dependencyTracker?.recordFutureForcedDependency("clipMode", opIdx);
    this.pendingClip = EO_CLIP;
  }
  beginText(opIdx) {
    this.current.textMatrix = null;
    this.current.textMatrixScale = 1;
    this.current.x = this.current.lineX = 0;
    this.current.y = this.current.lineY = 0;
    this.dependencyTracker?.recordOpenMarker(opIdx).resetIncrementalData("sameLineText").resetIncrementalData("moveText", opIdx);
  }
  endText(opIdx) {
    const paths = this.pendingTextPaths;
    const ctx = this.ctx;
    if (this.dependencyTracker) {
      const {
        dependencyTracker
      } = this;
      if (paths !== undefined) {
        dependencyTracker.recordFutureForcedDependency("textClip", dependencyTracker.getOpenMarker()).recordFutureForcedDependency("textClip", opIdx);
      }
      dependencyTracker.recordCloseMarker(opIdx);
    }
    if (paths !== undefined) {
      const newPath = new Path2D();
      const invTransf = ctx.getTransform().invertSelf();
      for (const {
        transform,
        x,
        y,
        fontSize,
        path
      } of paths) {
        if (!path) {
          continue;
        }
        newPath.addPath(path, new DOMMatrix(transform).preMultiplySelf(invTransf).translate(x, y).scale(fontSize, -fontSize));
      }
      ctx.clip(newPath);
    }
    delete this.pendingTextPaths;
  }
  setCharSpacing(opIdx, spacing) {
    this.dependencyTracker?.recordSimpleData("charSpacing", opIdx);
    this.current.charSpacing = spacing;
  }
  setWordSpacing(opIdx, spacing) {
    this.dependencyTracker?.recordSimpleData("wordSpacing", opIdx);
    this.current.wordSpacing = spacing;
  }
  setHScale(opIdx, scale) {
    this.dependencyTracker?.recordSimpleData("hScale", opIdx);
    this.current.textHScale = scale / 100;
  }
  setLeading(opIdx, leading) {
    this.dependencyTracker?.recordSimpleData("leading", opIdx);
    this.current.leading = -leading;
  }
  setFont(opIdx, fontRefName, size) {
    this.dependencyTracker?.recordSimpleData("font", opIdx).recordSimpleDataFromNamed("fontObj", fontRefName, opIdx);
    const fontObj = this.commonObjs.get(fontRefName);
    const current = this.current;
    if (!fontObj) {
      throw new Error(`Can't find font for ${fontRefName}`);
    }
    current.fontMatrix = fontObj.fontMatrix || FONT_IDENTITY_MATRIX;
    if (current.fontMatrix[0] === 0 || current.fontMatrix[3] === 0) {
      warn("Invalid font matrix for font " + fontRefName);
    }
    if (size < 0) {
      size = -size;
      current.fontDirection = -1;
    } else {
      current.fontDirection = 1;
    }
    this.current.font = fontObj;
    this.current.fontSize = size;
    if (fontObj.isType3Font) {
      return;
    }
    const name = fontObj.loadedName || "sans-serif";
    const typeface = fontObj.systemFontInfo?.css || `"${name}", ${fontObj.fallbackName}`;
    let bold = "normal";
    if (fontObj.black) {
      bold = "900";
    } else if (fontObj.bold) {
      bold = "bold";
    }
    const italic = fontObj.italic ? "italic" : "normal";
    const browserFontSize = math_clamp_MathClamp(size, MIN_FONT_SIZE, MAX_FONT_SIZE);
    this.current.fontSizeScale = size / browserFontSize;
    this.ctx.font = `${italic} ${bold} ${browserFontSize}px ${typeface}`;
  }
  setTextRenderingMode(opIdx, mode) {
    this.dependencyTracker?.recordSimpleData("textRenderingMode", opIdx);
    this.current.textRenderingMode = mode;
  }
  setTextRise(opIdx, rise) {
    this.dependencyTracker?.recordSimpleData("textRise", opIdx);
    this.current.textRise = rise;
  }
  moveText(opIdx, x, y) {
    this.dependencyTracker?.resetIncrementalData("sameLineText").recordIncrementalData("moveText", opIdx);
    this.current.x = this.current.lineX += x;
    this.current.y = this.current.lineY += y;
  }
  setLeadingMoveText(opIdx, x, y) {
    this.setLeading(opIdx, -y);
    this.moveText(opIdx, x, y);
  }
  setTextMatrix(opIdx, matrix) {
    this.dependencyTracker?.resetIncrementalData("sameLineText").recordSimpleData("textMatrix", opIdx);
    const {
      current
    } = this;
    current.textMatrix = matrix;
    current.textMatrixScale = Math.hypot(matrix[0], matrix[1]);
    current.x = current.lineX = 0;
    current.y = current.lineY = 0;
  }
  nextLine(opIdx) {
    this.moveText(opIdx, 0, this.current.leading);
    this.dependencyTracker?.recordIncrementalData("moveText", this.dependencyTracker.getSimpleIndex("leading") ?? opIdx);
  }
  #getScaledPath(path, currentTransform, transform) {
    const newPath = new Path2D();
    newPath.addPath(path, new DOMMatrix(transform).invertSelf().multiplySelf(currentTransform));
    return newPath;
  }
  paintChar(opIdx, character, x, y, patternFillTransform, patternStrokeTransform) {
    const ctx = this.ctx;
    const current = this.current;
    const font = current.font;
    const textRenderingMode = current.textRenderingMode;
    const fontSize = current.fontSize / current.fontSizeScale;
    const fillStrokeMode = textRenderingMode & TextRenderingMode.FILL_STROKE_MASK;
    const isAddToPathSet = !!(textRenderingMode & TextRenderingMode.ADD_TO_PATH_FLAG);
    const patternFill = current.patternFill && !font.missingFile;
    const patternStroke = current.patternStroke && !font.missingFile;
    let path;
    if ((font.disableFontFace || isAddToPathSet || patternFill || patternStroke) && !font.missingFile) {
      path = font.getPathGenerator(this.commonObjs, character);
    }
    if (path && (font.disableFontFace || patternFill || patternStroke)) {
      ctx.save();
      ctx.translate(x, y);
      ctx.scale(fontSize, -fontSize);
      this.dependencyTracker?.recordCharacterBBox(opIdx, ctx, font);
      let currentTransform;
      if (fillStrokeMode === TextRenderingMode.FILL || fillStrokeMode === TextRenderingMode.FILL_STROKE) {
        if (patternFillTransform) {
          currentTransform = ctx.getTransform();
          ctx.setTransform(...patternFillTransform);
          const scaledPath = this.#getScaledPath(path, currentTransform, patternFillTransform);
          ctx.fill(scaledPath);
        } else {
          ctx.fill(path);
        }
      }
      if (fillStrokeMode === TextRenderingMode.STROKE || fillStrokeMode === TextRenderingMode.FILL_STROKE) {
        if (patternStrokeTransform) {
          currentTransform ||= ctx.getTransform();
          ctx.setTransform(...patternStrokeTransform);
          const {
            a,
            b,
            c,
            d
          } = currentTransform;
          const invPatternTransform = Util.inverseTransform(patternStrokeTransform);
          const transf = Util.transform([a, b, c, d, 0, 0], invPatternTransform);
          Util.singularValueDecompose2dScale(transf, XY);
          ctx.lineWidth *= Math.max(XY[0], XY[1]) / fontSize;
          ctx.stroke(this.#getScaledPath(path, currentTransform, patternStrokeTransform));
        } else {
          ctx.lineWidth /= fontSize;
          ctx.stroke(path);
        }
      }
      ctx.restore();
    } else {
      if (fillStrokeMode === TextRenderingMode.FILL || fillStrokeMode === TextRenderingMode.FILL_STROKE) {
        ctx.fillText(character, x, y);
        this.dependencyTracker?.recordCharacterBBox(opIdx, ctx, font, fontSize, x, y, () => ctx.measureText(character));
      }
      if (fillStrokeMode === TextRenderingMode.STROKE || fillStrokeMode === TextRenderingMode.FILL_STROKE) {
        if (this.dependencyTracker) {
          this.dependencyTracker?.recordCharacterBBox(opIdx, ctx, font, fontSize, x, y, () => ctx.measureText(character)).recordDependencies(opIdx, Dependencies.stroke);
        }
        ctx.strokeText(character, x, y);
      }
    }
    if (isAddToPathSet) {
      const paths = this.pendingTextPaths ||= [];
      paths.push({
        transform: getCurrentTransform(ctx),
        x,
        y,
        fontSize,
        path
      });
      this.dependencyTracker?.recordCharacterBBox(opIdx, ctx, font, fontSize, x, y);
    }
  }
  get isFontSubpixelAAEnabled() {
    const tmpCanvas = this.canvasFactory.create(10, 10);
    const ctx = tmpCanvas.context;
    ctx.scale(1.5, 1);
    ctx.fillText("I", 0, 10);
    const data = ctx.getImageData(0, 0, 10, 10).data;
    this.canvasFactory.destroy(tmpCanvas);
    let enabled = false;
    for (let i = 3; i < data.length; i += 4) {
      if (data[i] > 0 && data[i] < 255) {
        enabled = true;
        break;
      }
    }
    return shadow(this, "isFontSubpixelAAEnabled", enabled);
  }
  #getTextPattern(color, pathType, opIdx) {
    const ctx = this.ctx;
    ctx.save();
    const pattern = color.getPattern(ctx, this, getCurrentTransformInverse(ctx), pathType, opIdx);
    const transform = getCurrentTransform(ctx);
    ctx.restore();
    return [pattern, transform];
  }
  showText(opIdx, glyphs) {
    if (this.dependencyTracker) {
      this.dependencyTracker.recordDependencies(opIdx, Dependencies.showText).resetBBox(opIdx);
      if (this.current.textRenderingMode & TextRenderingMode.ADD_TO_PATH_FLAG) {
        this.dependencyTracker.recordFutureForcedDependency("textClip", opIdx).inheritPendingDependenciesAsFutureForcedDependencies();
      }
    }
    const current = this.current;
    const font = current.font;
    if (font.isType3Font) {
      const started = this.#beginKnockoutElement(current.fillAlpha);
      this.showType3Text(opIdx, glyphs);
      this.dependencyTracker?.recordShowTextOperation(opIdx);
      this.#endKnockoutElement(started);
      return;
    }
    const fontSize = current.fontSize;
    if (fontSize === 0) {
      this.dependencyTracker?.recordOperation(opIdx);
      return;
    }
    const started = this.#beginKnockoutElement(current.fillAlpha);
    const ctx = this.ctx;
    const fontSizeScale = current.fontSizeScale;
    const charSpacing = current.charSpacing;
    const wordSpacing = current.wordSpacing;
    const fontDirection = current.fontDirection;
    const textHScale = current.textHScale * fontDirection;
    const glyphsLength = glyphs.length;
    const vertical = font.vertical;
    const spacingDir = vertical ? 1 : -1;
    const widthAdvanceScale = fontSize * current.fontMatrix[0];
    const simpleFillText = current.textRenderingMode === TextRenderingMode.FILL && !font.disableFontFace && !current.patternFill;
    ctx.save();
    if (current.textMatrix) {
      ctx.transform(...current.textMatrix);
    }
    ctx.translate(current.x, current.y + current.textRise);
    if (fontDirection > 0) {
      ctx.scale(textHScale, -1);
    } else {
      ctx.scale(textHScale, 1);
    }
    let patternFillTransform, patternStrokeTransform;
    const fillStrokeMode = current.textRenderingMode & TextRenderingMode.FILL_STROKE_MASK;
    const needsFill = fillStrokeMode === TextRenderingMode.FILL || fillStrokeMode === TextRenderingMode.FILL_STROKE;
    const needsStroke = fillStrokeMode === TextRenderingMode.STROKE || fillStrokeMode === TextRenderingMode.FILL_STROKE;
    let lineWidth = current.lineWidth;
    const scale = current.textMatrixScale;
    if (scale === 0 || lineWidth === 0) {
      if (needsStroke) {
        lineWidth = this.getSinglePixelWidth();
      }
    } else {
      lineWidth /= scale;
    }
    if (fontSizeScale !== 1.0) {
      ctx.scale(fontSizeScale, fontSizeScale);
      lineWidth /= fontSizeScale;
    }
    ctx.lineWidth = lineWidth;
    if (needsFill && current.patternFill) {
      [ctx.fillStyle, patternFillTransform] = this.#getTextPattern(current.fillColor, PathType.FILL, opIdx);
    }
    if (needsStroke && current.patternStroke) {
      [ctx.strokeStyle, patternStrokeTransform] = this.#getTextPattern(current.strokeColor, PathType.STROKE, opIdx);
    }
    if (font.isInvalidPDFjsFont) {
      const chars = [];
      let width = 0;
      for (const glyph of glyphs) {
        chars.push(glyph.unicode);
        width += glyph.width;
      }
      const joinedChars = chars.join("");
      ctx.fillText(joinedChars, 0, 0);
      if (this.dependencyTracker !== null) {
        const measure = ctx.measureText(joinedChars);
        this.dependencyTracker.recordBBox(opIdx, this.ctx, -measure.actualBoundingBoxLeft, measure.actualBoundingBoxRight, -measure.actualBoundingBoxAscent, measure.actualBoundingBoxDescent).recordShowTextOperation(opIdx);
      }
      current.x += width * widthAdvanceScale * textHScale;
      ctx.restore();
      this.compose();
      this.#endKnockoutElement(started);
      return;
    }
    let x = 0,
      i,
      glyphPatterns = null;
    for (i = 0; i < glyphsLength; ++i) {
      const glyph = glyphs[i];
      if (typeof glyph === "number") {
        x += spacingDir * glyph * fontSize / 1000;
        continue;
      }
      let restoreNeeded = false;
      const spacing = (glyph.isSpace ? wordSpacing : 0) + charSpacing;
      const character = glyph.fontChar;
      const accent = glyph.accent;
      let scaledX, scaledY, measure;
      let width = glyph.width;
      if (vertical) {
        const vmetric = glyph.vmetric;
        const vx = -vmetric[1] * widthAdvanceScale;
        const vy = vmetric[2] * widthAdvanceScale;
        width = -vmetric[0];
        scaledX = vx / fontSizeScale;
        scaledY = (x + vy) / fontSizeScale;
        if (glyph.isRotated) {
          measure = ctx.measureText(character);
          const glyphScale = widthAdvanceScale / fontSizeScale;
          const halfWidth = glyph.width / 2 * glyphScale;
          const centerX = scaledX + halfWidth;
          scaledX = scaledY - ((/* inlined export .DEFAULT_VERTICAL_ORIGIN_Y */880) - (/* inlined export .DEFAULT_VERTICAL_ADVANCE */1000) / 2) * glyphScale - halfWidth;
          scaledY = (measure.actualBoundingBoxAscent - measure.actualBoundingBoxDescent) / 2 - centerX;
          restoreNeeded = true;
          ctx.save();
          ctx.rotate(Math.PI / 2);
        }
      } else {
        scaledX = x / fontSizeScale;
        scaledY = 0;
      }
      const glyphWidth = glyph.isRotated ? glyph.width : width;
      let characterScaleX = 1;
      if ((font.remeasure || glyph.isRotated) && glyphWidth > 0) {
        measure ??= ctx.measureText(character);
        const measuredWidth = measure.width * 1000 / fontSize * fontSizeScale;
        if (glyphWidth < measuredWidth && this.isFontSubpixelAAEnabled) {
          characterScaleX = glyphWidth / measuredWidth;
          if (!restoreNeeded) {
            restoreNeeded = true;
            ctx.save();
          }
          ctx.scale(characterScaleX, 1);
          scaledX /= characterScaleX;
        } else if (glyphWidth !== measuredWidth) {
          scaledX += (glyphWidth - measuredWidth) / 2000 * fontSize / fontSizeScale;
        }
      }
      if (restoreNeeded && font.missingFile && (patternFillTransform || patternStrokeTransform)) {
        glyphPatterns ||= new Map();
        [ctx.fillStyle, ctx.strokeStyle] = glyphPatterns.getOrInsertComputed(glyph.isRotated ? -characterScaleX : characterScaleX, () => [patternFillTransform ? this.#getTextPattern(current.fillColor, PathType.FILL, opIdx)[0] : ctx.fillStyle, patternStrokeTransform ? this.#getTextPattern(current.strokeColor, PathType.STROKE, opIdx)[0] : ctx.strokeStyle]);
      }
      if (this.contentVisible && (glyph.isInFont || font.missingFile)) {
        if (simpleFillText && !accent) {
          ctx.fillText(character, scaledX, scaledY);
          this.dependencyTracker?.recordCharacterBBox(opIdx, ctx, measure ? {
            bbox: null
          } : font, fontSize / fontSizeScale, scaledX, scaledY, () => measure ?? ctx.measureText(character));
        } else {
          this.paintChar(opIdx, character, scaledX, scaledY, patternFillTransform, patternStrokeTransform);
          if (accent) {
            const scaledAccentX = scaledX + fontSize * accent.offset.x / fontSizeScale;
            const scaledAccentY = scaledY - fontSize * accent.offset.y / fontSizeScale;
            this.paintChar(opIdx, accent.fontChar, scaledAccentX, scaledAccentY, patternFillTransform, patternStrokeTransform);
          }
        }
      }
      const charWidth = vertical ? width * widthAdvanceScale - spacing * fontDirection : width * widthAdvanceScale + spacing * fontDirection;
      x += charWidth;
      if (restoreNeeded) {
        ctx.restore();
      }
    }
    if (vertical) {
      current.y -= x;
    } else {
      current.x += x * textHScale;
    }
    ctx.restore();
    this.compose();
    this.dependencyTracker?.recordShowTextOperation(opIdx);
    this.#endKnockoutElement(started);
  }
  showType3Text(opIdx, glyphs) {
    const ctx = this.ctx;
    const current = this.current;
    const font = current.font;
    const fontSize = current.fontSize;
    const fontDirection = current.fontDirection;
    const spacingDir = font.vertical ? 1 : -1;
    const charSpacing = current.charSpacing;
    const wordSpacing = current.wordSpacing;
    const textHScale = current.textHScale * fontDirection;
    const fontMatrix = current.fontMatrix || FONT_IDENTITY_MATRIX;
    const glyphsLength = glyphs.length;
    const isTextVisible = current.textRenderingMode !== TextRenderingMode.INVISIBLE;
    let i, glyph, width, spacingLength;
    if (fontSize === 0) {
      return;
    }
    this._cachedScaleForStroking[0] = -1;
    ctx.save();
    if (current.textMatrix) {
      ctx.transform(...current.textMatrix);
    }
    ctx.translate(current.x, current.y + current.textRise);
    ctx.scale(textHScale, fontDirection);
    const dependencyTracker = this.dependencyTracker;
    this.dependencyTracker = dependencyTracker ? new CanvasNestedDependencyTracker(dependencyTracker, opIdx) : null;
    try {
      for (i = 0; i < glyphsLength; ++i) {
        glyph = glyphs[i];
        if (typeof glyph === "number") {
          spacingLength = spacingDir * glyph * fontSize / 1000;
          this.ctx.translate(spacingLength, 0);
          current.x += spacingLength * textHScale;
          continue;
        }
        const spacing = (glyph.isSpace ? wordSpacing : 0) + charSpacing;
        const operatorList = font.charProcOperatorList.get(glyph.operatorListId);
        if (!operatorList) {
          warn(`Type3 character "${glyph.operatorListId}" is not available.`);
        } else if (this.contentVisible && isTextVisible) {
          this.save();
          ctx.scale(fontSize, fontSize);
          ctx.transform(...fontMatrix);
          this.executeOperatorList(operatorList);
          this.restore();
        }
        width = glyph.width * fontMatrix[0] * fontSize + spacing * fontDirection;
        ctx.translate(width, 0);
        current.x += width * textHScale;
      }
    } finally {
      if (dependencyTracker) {
        this.dependencyTracker = dependencyTracker;
      }
    }
    ctx.restore();
  }
  setCharWidth(opIdx, xWidth, yWidth) {}
  setCharWidthAndBounds(opIdx, xWidth, yWidth, llx, lly, urx, ury) {
    const clip = new Path2D();
    clip.rect(llx, lly, urx - llx, ury - lly);
    this.ctx.clip(clip);
    this.dependencyTracker?.recordBBox(opIdx, this.ctx, llx, urx, lly, ury).recordClipBox(opIdx, this.ctx, llx, urx, lly, ury);
    this.endPath(opIdx);
  }
  getColorN_Pattern(opIdx, IR) {
    let pattern;
    if (IR[0] === "TilingPattern") {
      const baseTransform = this.baseTransform || getCurrentTransform(this.ctx);
      const canvasGraphicsFactory = {
        createCanvasGraphics: (ctx, renderingOpIdx) => new CanvasGraphics(ctx, this.commonObjs, this.objs, this.canvasFactory, this.filterFactory, {
          optionalContentConfig: this.optionalContentConfig,
          markedContentStack: this.markedContentStack,
          pixelRatio: this.pixelRatio
        }, undefined, undefined, this.dependencyTracker ? new CanvasNestedDependencyTracker(this.dependencyTracker, renderingOpIdx, true) : null)
      };
      pattern = new TilingPattern(IR, this.ctx, canvasGraphicsFactory, baseTransform);
    } else {
      pattern = this._getPattern(opIdx, IR[1], IR[2]);
    }
    return pattern;
  }
  setStrokeColorN(opIdx, ...args) {
    this.dependencyTracker?.recordSimpleData("strokeColor", opIdx);
    this.current.strokeColor = this.getColorN_Pattern(opIdx, args);
    this.current.patternStroke = true;
  }
  setFillColorN(opIdx, ...args) {
    this.dependencyTracker?.recordSimpleData("fillColor", opIdx);
    const pattern = this.current.fillColor = this.getColorN_Pattern(opIdx, args);
    this.current.patternFill = true;
    this.current.tilingPatternDims = pattern instanceof TilingPattern ? [0, 0, 0, 0] : null;
  }
  #transferColor(color) {
    return this.current.transferMapsFallback?.applyToColor(color) ?? color;
  }
  setStrokeRGBColor(opIdx, color) {
    this.dependencyTracker?.recordSimpleData("strokeColor", opIdx);
    this.current.strokeColor = color;
    this.ctx.strokeStyle = this.#transferColor(color);
    this.current.patternStroke = false;
  }
  setStrokeTransparent(opIdx) {
    this.dependencyTracker?.recordSimpleData("strokeColor", opIdx);
    this.ctx.strokeStyle = this.current.strokeColor = "transparent";
    this.current.patternStroke = false;
  }
  setFillRGBColor(opIdx, color) {
    this.dependencyTracker?.recordSimpleData("fillColor", opIdx);
    this.current.fillColor = color;
    this.ctx.fillStyle = this.#transferColor(color);
    this.current.patternFill = false;
    this.current.tilingPatternDims = null;
  }
  setFillTransparent(opIdx) {
    this.dependencyTracker?.recordSimpleData("fillColor", opIdx);
    this.ctx.fillStyle = this.current.fillColor = "transparent";
    this.current.patternFill = false;
    this.current.tilingPatternDims = null;
  }
  _getPattern(opIdx, objId, matrix = null) {
    const pattern = this.cachedPatterns.getOrInsertComputed(objId, () => getShadingPattern(this.getObject(opIdx, objId)));
    if (matrix) {
      pattern.matrix = matrix;
    }
    return pattern;
  }
  shadingFill(opIdx, objId) {
    if (!this.contentVisible) {
      return;
    }
    const started = this.#beginKnockoutElement(this.current.fillAlpha);
    const ctx = this.ctx;
    this.save(opIdx);
    const pattern = this._getPattern(opIdx, objId);
    ctx.fillStyle = pattern.getPattern(ctx, this, getCurrentTransformInverse(ctx), PathType.SHADING, opIdx);
    const inv = getCurrentTransformInverse(ctx);
    if (inv) {
      const {
        width,
        height
      } = ctx.canvas;
      const minMax = F32_BBOX_INIT.slice();
      Util.axialAlignedBoundingBox([0, 0, width, height], inv, minMax);
      const [x0, y0, x1, y1] = minMax;
      this.ctx.fillRect(x0, y0, x1 - x0, y1 - y0);
    } else {
      this.ctx.fillRect(-1e10, -1e10, 2e10, 2e10);
    }
    this.dependencyTracker?.resetBBox(opIdx).recordFullPageBBox(opIdx).recordDependencies(opIdx, Dependencies.transform).recordDependencies(opIdx, Dependencies.fill).recordOperation(opIdx);
    this.compose(this.current.getClippedPathBoundingBox());
    this.restore(opIdx);
    this.#endKnockoutElement(started);
  }
  beginInlineImage() {
    unreachable("Should not call beginInlineImage");
  }
  beginImageData() {
    unreachable("Should not call beginImageData");
  }
  paintFormXObjectBegin(opIdx, matrix, bbox) {
    if (!this.contentVisible) {
      return;
    }
    this.save(opIdx);
    this.baseTransformStack.push(this.baseTransform);
    if (matrix) {
      this.transform(opIdx, ...matrix);
    }
    this.baseTransform = getCurrentTransform(this.ctx);
    if (bbox) {
      Util.axialAlignedBoundingBox(bbox, this.baseTransform, this.current.minMax);
      const [x0, y0, x1, y1] = bbox;
      const clip = new Path2D();
      clip.rect(x0, y0, x1 - x0, y1 - y0);
      this.ctx.clip(clip);
      this.dependencyTracker?.recordClipBox(opIdx, this.ctx, x0, x1, y0, y1);
      this.endPath(opIdx);
    }
  }
  paintFormXObjectEnd(opIdx) {
    if (!this.contentVisible) {
      return;
    }
    this.restore(opIdx);
    this.baseTransform = this.baseTransformStack.pop();
  }
  beginGroup(opIdx, group) {
    if (!this.contentVisible) {
      return;
    }
    this.save(opIdx);
    const {
      inSMaskMode
    } = this;
    if (inSMaskMode) {
      this.endSMaskMode();
      this.current.activeSMask = null;
    }
    const currentCtx = this.ctx;
    if ((!group.needsIsolation || !group.isolated && !group.hasSoftMask) && !group.knockout && !group.isGray && this.#knockoutGroupLevel === 0 && currentCtx.globalAlpha === 1 && currentCtx.globalCompositeOperation === "source-over" && !inSMaskMode) {
      if (group.bbox) {
        let clip = new Path2D();
        const [x0, y0, x1, y1] = group.bbox;
        clip.rect(x0, y0, x1 - x0, y1 - y0);
        if (group.matrix) {
          const path = new Path2D();
          path.addPath(clip, new DOMMatrix(group.matrix));
          clip = path;
        }
        currentCtx.clip(clip);
      }
      this.groupStack.push(null);
      this.#groupStackMeta.push(null);
      this.groupLevel++;
      return;
    }
    if (!group.isolated && !group.knockout && this.#knockoutGroupLevel === 0) {
      info("TODO: Fully support non-isolated non-knockout groups.");
    }
    const currentTransform = getCurrentTransform(currentCtx);
    if (group.matrix) {
      currentCtx.transform(...group.matrix);
    }
    const canvasBounds = [0, 0, currentCtx.canvas.width, currentCtx.canvas.height];
    let bounds;
    if (group.bbox) {
      bounds = F32_BBOX_INIT.slice();
      Util.axialAlignedBoundingBox(group.bbox, getCurrentTransform(currentCtx), bounds);
      bounds = Util.intersect(bounds, canvasBounds) || [0, 0, 0, 0];
    } else {
      bounds = canvasBounds;
    }
    const offsetX = Math.floor(bounds[0]);
    const offsetY = Math.floor(bounds[1]);
    const drawnWidth = Math.max(Math.ceil(bounds[2]) - offsetX, 1);
    const drawnHeight = Math.max(Math.ceil(bounds[3]) - offsetY, 1);
    this.current.startNewPathAndClipBox([0, 0, drawnWidth, drawnHeight]);
    const scratchCanvas = this.canvasFactory.create(drawnWidth, drawnHeight);
    if (group.smask) {
      this.smaskGroupCanvases.push(scratchCanvas);
    }
    const groupCtx = scratchCanvas.context;
    const backdropCtx = group.knockout && !group.isolated ? currentCtx : null;
    const hasInnerBackdrop = !group.isolated && !group.knockout && !group.smask && group.needsIsolation && this.#knockoutGroupLevel > 0;
    const knockoutMaskEntry = group.knockout ? this.canvasFactory.create(drawnWidth, drawnHeight) : null;
    const savedKnockoutLevel = this.#knockoutGroupLevel;
    if (group.knockout) {
      this.#knockoutGroupLevel++;
    } else {
      this.#knockoutGroupLevel = 0;
    }
    groupCtx.translate(-offsetX, -offsetY);
    groupCtx.transform(...currentTransform);
    const needsBackdropCopy = !group.isolated && !group.smask && group.needsIsolation;
    const replaceBackdrop = needsBackdropCopy && !inSMaskMode && savedKnockoutLevel === 0 && !group.knockout && !group.isGray && group.hasSoftMask && currentCtx.globalAlpha === 1 && currentCtx.globalCompositeOperation === "source-over" && this.current.transferMaps === "none" && !this.current.transferMapsFallback;
    if (needsBackdropCopy && (inSMaskMode || replaceBackdrop)) {
      groupCtx.save();
      groupCtx.setTransform(1, 0, 0, 1, 0, 0);
      groupCtx.drawImage(currentCtx.canvas, -offsetX, -offsetY);
      groupCtx.restore();
    }
    if (group.bbox) {
      let clip = new Path2D();
      const [x0, y0, x1, y1] = group.bbox;
      clip.rect(x0, y0, x1 - x0, y1 - y0);
      if (group.matrix) {
        const path = new Path2D();
        path.addPath(clip, new DOMMatrix(group.matrix));
        clip = path;
      }
      groupCtx.clip(clip);
    }
    if (group.smask) {
      this.smaskStack.push({
        canvas: scratchCanvas.canvas,
        context: groupCtx,
        offsetX,
        offsetY,
        subtype: group.smask.subtype,
        backdrop: group.smask.backdrop,
        transferMap: group.smask.transferMap || null
      });
    }
    if (!group.smask || this.dependencyTracker) {
      currentCtx.setTransform(1, 0, 0, 1, 0, 0);
      currentCtx.translate(offsetX, offsetY);
      currentCtx.save();
    }
    copyCtxState(currentCtx, groupCtx);
    this.ctx = groupCtx;
    this.dependencyTracker?.inheritSimpleDataAsFutureForcedDependencies(["fillAlpha", "strokeAlpha", "globalCompositeOperation"]).pushBaseTransform(currentCtx);
    this.setGState(opIdx, [["BM", "source-over"], ["ca", 1], ["CA", 1], ["TR", null]]);
    this.groupStack.push(currentCtx);
    this.#groupStackMeta.push({
      backdropCtx,
      savedKnockoutLevel,
      offsetX,
      offsetY,
      hasInnerBackdrop,
      replaceBackdrop,
      knockoutMaskEntry,
      knockoutTempEntry: null,
      knockoutBackdropEntry: null
    });
    this.groupLevel++;
  }
  endGroup(opIdx, group) {
    if (!this.contentVisible) {
      return;
    }
    this.groupLevel--;
    const groupCtx = this.ctx;
    const ctx = this.groupStack.pop();
    const groupMeta = this.#groupStackMeta.pop();
    if (groupMeta) {
      this.#knockoutGroupLevel = groupMeta.savedKnockoutLevel;
    }
    if (ctx === null) {
      this.restore(opIdx);
      return;
    }
    if (group.isGray) {
      this.#convertGroupToGray(groupCtx);
    }
    this.ctx = ctx;
    this.ctx.imageSmoothingEnabled = false;
    this.dependencyTracker?.popBaseTransform();
    if (group.smask) {
      this.tempSMask = this.smaskStack.pop();
      this.restore(opIdx);
      if (this.dependencyTracker) {
        this.ctx.restore();
        if (this.inSMaskMode) {
          this.ctx.setTransform(this.suspendedCtx.getTransform());
        }
      }
      this.#destroyKnockoutPools(groupMeta);
    } else {
      this.ctx.restore();
      const currentMtx = getCurrentTransform(this.ctx);
      this.restore(opIdx);
      this.current.transferMapsFallback?.applyToCanvas(groupCtx);
      this.ctx.save();
      this.ctx.setTransform(...currentMtx);
      const dirtyBox = F32_BBOX_INIT.slice();
      Util.axialAlignedBoundingBox([0, 0, groupCtx.canvas.width, groupCtx.canvas.height], currentMtx, dirtyBox);
      const parentGroupMeta = this.#groupStackMeta.at(-1);
      if (this.#knockoutGroupLevel > 0) {
        if (groupMeta.hasInnerBackdrop) {
          const {
            width,
            height
          } = groupCtx.canvas;
          const colorEntry = this.canvasFactory.create(width, height);
          const colorCtx = colorEntry.context;
          colorCtx.drawImage(ctx.canvas, groupMeta.offsetX, groupMeta.offsetY, width, height, 0, 0, width, height);
          colorCtx.globalCompositeOperation = "source-over";
          colorCtx.drawImage(groupCtx.canvas, 0, 0);
          const shapeMaskEntry = this.#createKnockoutMaskCanvas(groupCtx.canvas);
          colorCtx.globalCompositeOperation = "destination-in";
          colorCtx.drawImage(shapeMaskEntry.canvas, 0, 0);
          const sourceCompositeOperation = this.ctx.globalCompositeOperation;
          const sourceAlpha = this.ctx.globalAlpha;
          const sourceFilter = this.ctx.filter;
          this.ctx.save();
          this.ctx.setTransform(...currentMtx);
          this.ctx.globalAlpha = 1;
          if (FeatureTest.isCanvasFilterSupported) {
            this.ctx.filter = "none";
          }
          this.ctx.globalCompositeOperation = "destination-out";
          this.ctx.drawImage(shapeMaskEntry.canvas, 0, 0);
          this.ctx.globalCompositeOperation = sourceCompositeOperation;
          this.ctx.globalAlpha = sourceAlpha;
          if (FeatureTest.isCanvasFilterSupported) {
            this.ctx.filter = sourceFilter ?? "none";
          }
          this.ctx.drawImage(colorEntry.canvas, 0, 0);
          this.ctx.restore();
          this.canvasFactory.destroy(shapeMaskEntry);
          this.canvasFactory.destroy(colorEntry);
        } else {
          const backdropCtx = parentGroupMeta?.backdropCtx ?? null;
          this.#compositeKnockoutSurface(this.ctx, groupCtx.canvas, {
            backdropCanvas: backdropCtx?.canvas ?? null,
            destTransform: currentMtx,
            backdropOffset: backdropCtx ? [parentGroupMeta.offsetX + groupMeta.offsetX, parentGroupMeta.offsetY + groupMeta.offsetY] : [0, 0],
            sourceAlpha: this.ctx.globalAlpha,
            sourceFilter: this.ctx.filter
          });
        }
      } else {
        if (groupMeta.replaceBackdrop) {
          const clip = new Path2D();
          clip.rect(0, 0, groupCtx.canvas.width, groupCtx.canvas.height);
          this.ctx.clip(clip);
          this.ctx.globalCompositeOperation = "copy";
        }
        this.ctx.drawImage(groupCtx.canvas, 0, 0);
      }
      this.ctx.restore();
      this.canvasFactory.destroy({
        canvas: groupCtx.canvas,
        context: groupCtx
      });
      this.#destroyKnockoutPools(groupMeta);
      this.compose(dirtyBox);
    }
  }
  #convertGroupToGray(groupCtx) {
    const {
      canvas
    } = groupCtx;
    const {
      width,
      height
    } = canvas;
    if (FeatureTest.isCanvasFilterSupported) {
      groupCtx.save();
      groupCtx.setTransform(1, 0, 0, 1, 0, 0);
      groupCtx.filter = "grayscale(1)";
      groupCtx.globalAlpha = 1;
      groupCtx.globalCompositeOperation = "copy";
      groupCtx.drawImage(canvas, 0, 0);
      groupCtx.restore();
      return;
    }
    const imageData = groupCtx.getImageData(0, 0, width, height);
    const {
      data
    } = imageData;
    for (let i = 0, ii = data.length; i < ii; i += 4) {
      const gray = data[i] * 0.2126 + data[i + 1] * 0.7152 + data[i + 2] * 0.0722 + 0.5 | 0;
      data[i] = data[i + 1] = data[i + 2] = gray;
    }
    groupCtx.putImageData(imageData, 0, 0);
  }
  #destroyKnockoutPools(groupMeta) {
    if (!groupMeta) {
      return;
    }
    if (groupMeta.knockoutMaskEntry) {
      this.canvasFactory.destroy(groupMeta.knockoutMaskEntry);
      groupMeta.knockoutMaskEntry = null;
    }
    if (groupMeta.knockoutTempEntry) {
      this.canvasFactory.destroy(groupMeta.knockoutTempEntry);
      groupMeta.knockoutTempEntry = null;
    }
    if (groupMeta.knockoutBackdropEntry) {
      this.canvasFactory.destroy(groupMeta.knockoutBackdropEntry);
      groupMeta.knockoutBackdropEntry = null;
    }
  }
  beginAnnotation(opIdx, id, rect, transform, matrix, hasOwnCanvas, canvasName) {
    this.#restoreInitialState();
    resetCtxToDefault(this.ctx);
    this.ctx.save();
    this.save(opIdx);
    if (this.baseTransform) {
      this.ctx.setTransform(...this.baseTransform);
    }
    if (rect) {
      const width = rect[2] - rect[0];
      const height = rect[3] - rect[1];
      if (hasOwnCanvas && this.annotationCanvasMap) {
        transform = transform.slice();
        transform[4] -= rect[0];
        transform[5] -= rect[1];
        Util.singularValueDecompose2dScale(getCurrentTransform(this.ctx), XY);
        const {
          viewportScale
        } = this;
        const canvasWidth = Math.ceil(width * this.outputScaleX * viewportScale);
        const canvasHeight = Math.ceil(height * this.outputScaleY * viewportScale);
        this.annotationCanvas = this.canvasFactory.create(canvasWidth, canvasHeight);
        const {
          canvas,
          context
        } = this.annotationCanvas;
        if (canvasName) {
          const canvases = this.annotationCanvasMap.getOrInsertComputed(id, makeArr);
          setAnnotationCanvasName(canvas, canvasName);
          const index = canvases.findIndex(c => getAnnotationCanvasName(c) === canvasName);
          if (index === -1) {
            canvases.push(canvas);
          } else {
            canvases[index] = canvas;
          }
        } else {
          this.annotationCanvasMap.set(id, canvas);
        }
        this.annotationCanvas.savedCtx = this.ctx;
        this.ctx = context;
        this.ctx.save();
        this.ctx.setTransform(XY[0], 0, 0, -XY[1], 0, height * XY[1]);
        resetCtxToDefault(this.ctx);
      } else {
        resetCtxToDefault(this.ctx);
        this.endPath(opIdx);
        const clip = new Path2D();
        clip.rect(rect[0], rect[1], width, height);
        this.ctx.clip(clip);
      }
    }
    this.current = new CanvasExtraState(this.ctx.canvas.width, this.ctx.canvas.height);
    this.baseTransformStack.push(this.baseTransform);
    this.transform(opIdx, ...transform);
    this.transform(opIdx, ...matrix);
    this.baseTransform = getCurrentTransform(this.ctx);
  }
  endAnnotation(opIdx) {
    if (this.annotationCanvas) {
      this.ctx.restore();
      this.#drawFilter();
      this.ctx = this.annotationCanvas.savedCtx;
      delete this.annotationCanvas.savedCtx;
      delete this.annotationCanvas;
    }
    this.baseTransform = this.baseTransformStack.pop();
  }
  paintImageMaskXObject(opIdx, img) {
    if (!this.contentVisible) {
      return;
    }
    const count = img.count;
    img = this.getObject(opIdx, img.data, img);
    img.count = count;
    const started = this.#beginKnockoutElement(this.current.fillAlpha);
    const ctx = this.ctx;
    const mask = this._createMaskCanvas(opIdx, img);
    const maskCanvas = mask.canvas;
    ctx.save();
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    ctx.drawImage(maskCanvas, mask.offsetX, mask.offsetY);
    this.dependencyTracker?.resetBBox(opIdx).recordBBox(opIdx, this.ctx, mask.offsetX, mask.offsetX + maskCanvas.width, mask.offsetY, mask.offsetY + maskCanvas.height).recordOperation(opIdx);
    ctx.restore();
    if (mask.canvasEntry) {
      this.canvasFactory.destroy(mask.canvasEntry);
    }
    this.compose();
    this.#endKnockoutElement(started);
  }
  paintImageMaskXObjectRepeat(opIdx, img, scaleX, skewX = 0, skewY = 0, scaleY, positions) {
    if (!this.contentVisible) {
      return;
    }
    img = this.getObject(opIdx, img.data, img);
    const started = this.#beginKnockoutElement(this.current.fillAlpha);
    const ctx = this.ctx;
    ctx.save();
    const currentTransform = getCurrentTransform(ctx);
    ctx.transform(scaleX, skewX, skewY, scaleY, 0, 0);
    const mask = this._createMaskCanvas(opIdx, img);
    ctx.setTransform(1, 0, 0, 1, mask.offsetX - currentTransform[4], mask.offsetY - currentTransform[5]);
    this.dependencyTracker?.resetBBox(opIdx);
    for (let i = 0, ii = positions.length; i < ii; i += 2) {
      const trans = Util.transform(currentTransform, [scaleX, skewX, skewY, scaleY, positions[i], positions[i + 1]]);
      ctx.drawImage(mask.canvas, trans[4], trans[5]);
      this.dependencyTracker?.recordBBox(opIdx, this.ctx, trans[4], trans[4] + mask.canvas.width, trans[5], trans[5] + mask.canvas.height);
    }
    ctx.restore();
    if (mask.canvasEntry) {
      this.canvasFactory.destroy(mask.canvasEntry);
    }
    this.compose();
    this.dependencyTracker?.recordOperation(opIdx);
    this.#endKnockoutElement(started);
  }
  paintImageMaskXObjectGroup(opIdx, images) {
    if (!this.contentVisible) {
      return;
    }
    const started = this.#beginKnockoutElement(this.current.fillAlpha);
    const ctx = this.ctx;
    const isPatternFill = this.current.patternFill;
    const fillColor = isPatternFill ? this.current.fillColor : ctx.fillStyle;
    this.dependencyTracker?.resetBBox(opIdx).recordDependencies(opIdx, Dependencies.transformAndFill);
    for (const image of images) {
      const {
        data,
        width,
        height,
        transform
      } = image;
      const maskCanvas = this.canvasFactory.create(width, height);
      const maskCtx = maskCanvas.context;
      maskCtx.save();
      const img = this.getObject(opIdx, data, image);
      putBinaryImageMask(maskCtx, img);
      maskCtx.globalCompositeOperation = "source-in";
      maskCtx.fillStyle = isPatternFill ? fillColor.getPattern(maskCtx, this, getCurrentTransformInverse(ctx), PathType.FILL, opIdx) : fillColor;
      maskCtx.fillRect(0, 0, width, height);
      maskCtx.restore();
      ctx.save();
      ctx.transform(...transform);
      ctx.scale(1, -1);
      drawImageAtIntegerCoords(ctx, maskCanvas.canvas, 0, 0, width, height, 0, -1, 1, 1);
      this.canvasFactory.destroy(maskCanvas);
      this.dependencyTracker?.recordBBox(opIdx, ctx, 0, width, 0, height);
      ctx.restore();
    }
    this.compose();
    this.dependencyTracker?.recordOperation(opIdx);
    this.#endKnockoutElement(started);
  }
  paintImageXObject(opIdx, objId) {
    if (!this.contentVisible) {
      return;
    }
    const imgData = this.getObject(opIdx, objId);
    if (!imgData) {
      warn("Dependent image isn't ready yet");
      return;
    }
    this.paintInlineImageXObject(opIdx, imgData);
  }
  paintImageXObjectRepeat(opIdx, objId, scaleX, scaleY, positions) {
    if (!this.contentVisible) {
      return;
    }
    const imgData = this.getObject(opIdx, objId);
    if (!imgData) {
      warn("Dependent image isn't ready yet");
      return;
    }
    const width = imgData.width;
    const height = imgData.height;
    const map = [];
    for (let i = 0, ii = positions.length; i < ii; i += 2) {
      map.push({
        transform: [scaleX, 0, 0, scaleY, positions[i], positions[i + 1]],
        x: 0,
        y: 0,
        w: width,
        h: height
      });
    }
    this.paintInlineImageXObjectGroup(opIdx, imgData, map);
  }
  applyTransferMapsToCanvas(ctx) {
    if (this.current.transferMaps !== "none") {
      ctx.filter = this.current.transferMaps;
      ctx.drawImage(ctx.canvas, 0, 0);
      ctx.filter = "none";
    } else {
      this.current.transferMapsFallback?.applyToCanvas(ctx);
    }
    return ctx.canvas;
  }
  applyTransferMapsToBitmap(imgData) {
    const {
      transferMaps,
      transferMapsFallback
    } = this.current;
    if (transferMaps === "none" && !transferMapsFallback) {
      return {
        img: imgData.bitmap,
        canvasEntry: null
      };
    }
    const {
      bitmap,
      width,
      height
    } = imgData;
    const tmpCanvas = this.canvasFactory.create(width, height);
    const tmpCtx = tmpCanvas.context;
    tmpCtx.filter = transferMaps;
    tmpCtx.drawImage(bitmap, 0, 0);
    tmpCtx.filter = "none";
    transferMapsFallback?.applyToCanvas(tmpCtx);
    return {
      img: tmpCanvas.canvas,
      canvasEntry: tmpCanvas
    };
  }
  paintInlineImageXObject(opIdx, imgData) {
    if (!this.contentVisible) {
      return;
    }
    const width = imgData.width;
    const height = imgData.height;
    const started = this.#beginKnockoutElement(this.current.fillAlpha);
    const ctx = this.ctx;
    this.save(opIdx);
    const {
      filter
    } = ctx;
    if (filter !== "none" && filter !== "") {
      ctx.filter = "none";
    }
    ctx.scale(1 / width, -1 / height);
    let imgToPaint;
    let inlineImgCanvas = null;
    if (imgData.bitmap) {
      const result = this.applyTransferMapsToBitmap(imgData);
      imgToPaint = result.img;
      inlineImgCanvas = result.canvasEntry;
    } else {
      const tmpCanvas = this.canvasFactory.create(width, height);
      putBinaryImageData(tmpCanvas.context, imgData);
      imgToPaint = this.applyTransferMapsToCanvas(tmpCanvas.context);
      inlineImgCanvas = tmpCanvas;
    }
    const scaled = this._scaleImage(imgToPaint, getCurrentTransformInverse(ctx));
    ctx.imageSmoothingEnabled = getImageSmoothingEnabled(getCurrentTransform(ctx), imgData.interpolate, this.pixelRatio);
    if (this.dependencyTracker) {
      this.dependencyTracker.resetBBox(opIdx).recordBBox(opIdx, ctx, 0, width, -height, 0).recordDependencies(opIdx, Dependencies.imageXObject).recordOperation(opIdx);
      this.imagesTracker?.record(ctx, width, height, this.dependencyTracker.clipBox);
    }
    drawImageAtIntegerCoords(ctx, scaled.img, 0, 0, scaled.paintWidth, scaled.paintHeight, 0, -height, width, height);
    if (scaled.tmpCanvas) {
      this.canvasFactory.destroy(scaled.tmpCanvas);
    }
    if (inlineImgCanvas) {
      this.canvasFactory.destroy(inlineImgCanvas);
    }
    this.compose();
    this.restore(opIdx);
    this.#endKnockoutElement(started);
  }
  paintInlineImageXObjectGroup(opIdx, imgData, map) {
    if (!this.contentVisible) {
      return;
    }
    const started = this.#beginKnockoutElement(this.current.fillAlpha);
    const ctx = this.ctx;
    let imgToPaint;
    let inlineImgCanvas = null;
    if (imgData.bitmap && !this.current.transferMapsFallback) {
      imgToPaint = imgData.bitmap;
    } else if (imgData.bitmap) {
      ({
        img: imgToPaint,
        canvasEntry: inlineImgCanvas
      } = this.applyTransferMapsToBitmap(imgData));
    } else {
      const w = imgData.width;
      const h = imgData.height;
      const tmpCanvas = this.canvasFactory.create(w, h);
      putBinaryImageData(tmpCanvas.context, imgData);
      imgToPaint = this.applyTransferMapsToCanvas(tmpCanvas.context);
      inlineImgCanvas = tmpCanvas;
    }
    this.dependencyTracker?.resetBBox(opIdx);
    for (const entry of map) {
      ctx.save();
      ctx.transform(...entry.transform);
      ctx.scale(1, -1);
      drawImageAtIntegerCoords(ctx, imgToPaint, entry.x, entry.y, entry.w, entry.h, 0, -1, 1, 1);
      this.dependencyTracker?.recordBBox(opIdx, ctx, 0, 1, -1, 0);
      ctx.restore();
    }
    if (inlineImgCanvas) {
      this.canvasFactory.destroy(inlineImgCanvas);
    }
    this.dependencyTracker?.recordOperation(opIdx);
    this.compose();
    this.#endKnockoutElement(started);
  }
  paintSolidColorImageMask(opIdx) {
    if (!this.contentVisible) {
      return;
    }
    const started = this.#beginKnockoutElement(this.current.fillAlpha);
    this.dependencyTracker?.resetBBox(opIdx).recordBBox(opIdx, this.ctx, 0, 1, 0, 1).recordDependencies(opIdx, Dependencies.fill).recordOperation(opIdx);
    this.ctx.fillRect(0, 0, 1, 1);
    this.compose();
    this.#endKnockoutElement(started);
  }
  markPoint(opIdx, tag) {}
  markPointProps(opIdx, tag, properties) {}
  beginMarkedContent(opIdx, tag) {
    this.dependencyTracker?.beginMarkedContent(opIdx);
    this.markedContentStack.push({
      visible: true
    });
  }
  beginMarkedContentProps(opIdx, tag, properties) {
    this.dependencyTracker?.beginMarkedContent(opIdx);
    if (tag === "OC") {
      this.markedContentStack.push({
        visible: this.optionalContentConfig.isVisible(properties)
      });
    } else {
      this.markedContentStack.push({
        visible: true
      });
    }
    this.contentVisible = this.isContentVisible();
  }
  endMarkedContent(opIdx) {
    this.dependencyTracker?.endMarkedContent(opIdx);
    this.markedContentStack.pop();
    this.contentVisible = this.isContentVisible();
  }
  beginCompat(opIdx) {}
  endCompat(opIdx) {}
  consumePath(opIdx, path, clipBox) {
    const isEmpty = this.current.isEmptyClip();
    if (this.pendingClip) {
      this.current.updateClipFromPath();
    }
    if (!this.pendingClip) {
      this.compose(clipBox);
    }
    const ctx = this.ctx;
    if (this.pendingClip) {
      if (!isEmpty) {
        if (this.pendingClip === EO_CLIP) {
          ctx.clip(path, "evenodd");
        } else {
          ctx.clip(path);
        }
      }
      this.pendingClip = null;
      this.dependencyTracker?.bboxToClipBoxDropOperation(opIdx).recordFutureForcedDependency("clipPath", opIdx);
    } else {
      this.dependencyTracker?.recordOperation(opIdx);
    }
    this.current.startNewPathAndClipBox(this.current.clipBox);
  }
  getSinglePixelWidth() {
    const m = getCurrentTransform(this.ctx);
    if (m[1] === 0 && m[2] === 0) {
      return 1 / Math.min(Math.abs(m[0]), Math.abs(m[3]));
    }
    const absDet = Math.abs(m[0] * m[3] - m[2] * m[1]);
    const normX = Math.hypot(m[0], m[2]);
    const normY = Math.hypot(m[1], m[3]);
    return Math.max(normX, normY) / absDet;
  }
  getScaleForStroking() {
    if (this._cachedScaleForStroking[0] === -1) {
      const {
        lineWidth
      } = this.current;
      const {
        a,
        b,
        c,
        d
      } = this.ctx.getTransform();
      let scaleX, scaleY;
      if (b === 0 && c === 0) {
        const normX = Math.abs(a);
        const normY = Math.abs(d);
        if (normX === normY) {
          if (lineWidth === 0) {
            scaleX = scaleY = 1 / normX;
          } else {
            const scaledLineWidth = normX * lineWidth;
            scaleX = scaleY = scaledLineWidth < 1 ? 1 / scaledLineWidth : 1;
          }
        } else if (lineWidth === 0) {
          scaleX = 1 / normX;
          scaleY = 1 / normY;
        } else {
          const scaledXLineWidth = normX * lineWidth;
          const scaledYLineWidth = normY * lineWidth;
          scaleX = scaledXLineWidth < 1 ? 1 / scaledXLineWidth : 1;
          scaleY = scaledYLineWidth < 1 ? 1 / scaledYLineWidth : 1;
        }
      } else {
        const absDet = Math.abs(a * d - b * c);
        const normX = Math.hypot(a, b);
        const normY = Math.hypot(c, d);
        if (lineWidth === 0) {
          scaleX = normY / absDet;
          scaleY = normX / absDet;
        } else {
          const baseArea = lineWidth * absDet;
          scaleX = normY > baseArea ? normY / baseArea : 1;
          scaleY = normX > baseArea ? normX / baseArea : 1;
        }
      }
      this._cachedScaleForStroking[0] = scaleX;
      this._cachedScaleForStroking[1] = scaleY;
    }
    return this._cachedScaleForStroking;
  }
  rescaleAndStroke(path, saveRestore) {
    const {
      ctx,
      current: {
        lineWidth
      }
    } = this;
    const [scaleX, scaleY] = this.getScaleForStroking();
    if (scaleX === scaleY) {
      ctx.lineWidth = (lineWidth || 1) * scaleX;
      ctx.stroke(path);
      return;
    }
    const SCALE_MATRIX = CanvasGraphics.#SCALE_MATRIX ??= new DOMMatrix();
    const dashes = ctx.getLineDash();
    if (saveRestore) {
      ctx.save();
    }
    ctx.scale(scaleX, scaleY);
    SCALE_MATRIX.a = 1 / scaleX;
    SCALE_MATRIX.d = 1 / scaleY;
    const newPath = new Path2D();
    newPath.addPath(path, SCALE_MATRIX);
    if (dashes.length > 0) {
      const scale = Math.max(scaleX, scaleY);
      ctx.setLineDash(dashes.map(x => x / scale));
      ctx.lineDashOffset /= scale;
    }
    ctx.lineWidth = lineWidth || 1;
    ctx.stroke(newPath);
    if (saveRestore) {
      ctx.restore();
    }
  }
  isContentVisible() {
    for (let i = this.markedContentStack.length - 1; i >= 0; i--) {
      if (!this.markedContentStack[i].visible) {
        return false;
      }
    }
    return true;
  }
}
for (const op in OPS) {
  if (CanvasGraphics.prototype[op] !== undefined) {
    CanvasGraphics.prototype[OPS[op]] = CanvasGraphics.prototype[op];
  }
}

;// ./src/shared/css_utils.js
const CONTROL_CHAR_REGEXP = /\p{Cc}/u;
function isCSSString(str) {
  const quote = str[0];
  if (str.length < 2 || quote !== `"` && quote !== `'` || str.at(-1) !== quote) {
    return false;
  }
  const end = str.length - 1;
  for (let i = 1; i < end; i++) {
    const char = str[i];
    if (char === quote || CONTROL_CHAR_REGEXP.test(char)) {
      return false;
    }
    if (char === "\\") {
      if (++i >= end || CONTROL_CHAR_REGEXP.test(str[i])) {
        return false;
      }
    }
  }
  return true;
}
function serializeFontFamily(fontFamily) {
  if (isCSSString(fontFamily)) {
    return fontFamily;
  }
  const escaped = fontFamily.replaceAll(/["\\\p{Cc}]/gu, char => char === `"` || char === "\\" ? `\\${char}` : `\\${char.codePointAt(0).toString(16)} `);
  return `"${escaped}"`;
}

;// ./src/display/font_loader.js



class FontLoader {
  #nativeFontFaces = new Set();
  #systemFonts = new Set();
  #styleSheet = null;
  constructor({
    ownerDocument = globalThis.document,
    styleElement = null
  }) {
    this._document = ownerDocument;
    this.styleElement = null;
  }
  addNativeFontFace(nativeFontFace) {
    this.#nativeFontFaces.add(nativeFontFace);
    this._document.fonts.add(nativeFontFace);
  }
  removeNativeFontFace(nativeFontFace) {
    this.#nativeFontFaces.delete(nativeFontFace);
    this._document.fonts.delete(nativeFontFace);
  }
  insertRule(rule) {
    throw new Error("Not implemented: insertRule");
  }
  #getStyleSheet() {
    throw new Error("Not implemented: #getStyleSheet");
  }
  clear() {
    for (const nativeFontFace of this.#nativeFontFaces) {
      this._document.fonts.delete(nativeFontFace);
    }
    this.#nativeFontFaces.clear();
    this.#systemFonts.clear();
  }
  async loadSystemFont({
    systemFontInfo: info,
    disableFontFace,
    _inspectFont
  }) {
    if (!info || this.#systemFonts.has(info.loadedName)) {
      return;
    }
    assert(!disableFontFace, "loadSystemFont shouldn't be called when `disableFontFace` is set.");
    if (this.isFontLoadingAPISupported) {
      const {
        loadedName,
        src,
        style
      } = info;
      const fontFace = new FontFace(loadedName, src, style);
      this.addNativeFontFace(fontFace);
      try {
        await fontFace.load();
        this.#systemFonts.add(loadedName);
        _inspectFont?.(info);
      } catch {
        warn(`Cannot load system font: ${info.baseFontName}, installing it could help to improve PDF rendering.`);
        this.removeNativeFontFace(fontFace);
      }
      return;
    }
    unreachable("Not implemented: loadSystemFont without the Font Loading API.");
  }
  async #loadFontFace(font) {
    if (!this.isFontLoadingAPISupported) {
      return;
    }
    const fontFace = font.createNativeFontFace();
    if (!fontFace) {
      return;
    }
    this.addNativeFontFace(fontFace);
    try {
      await fontFace.loaded;
    } catch (ex) {
      warn(`Failed to load font '${fontFace.family}': '${ex}'.`);
      this.removeNativeFontFace(fontFace);
      if (!font.missingFile) {
        font.disableFontFace = true;
        throw ex;
      }
    }
  }
  async bind(font) {
    if (font.attached || font.missingFile && !font.systemFontInfo && !font.generatedFontName) {
      return;
    }
    font.attached = true;
    if (font.systemFontInfo) {
      await Promise.all([this.loadSystemFont(font), this.#loadFontFace(font)]);
      return;
    }
    if (this.isFontLoadingAPISupported) {
      await this.#loadFontFace(font);
      return;
    }
    throw new Error("Not implemented: DOM font loading");
  }
  get isFontLoadingAPISupported() {
    return shadow(this, "isFontLoadingAPISupported", !!this._document?.fonts);
  }
  get isSyncFontLoadingSupported() {
    return shadow(this, "isSyncFontLoadingSupported", true);
  }
  #testFontLoaded(font) {
    throw new Error("Not implemented: #testFontLoaded");
  }
}
class FontFaceObject {
  #compiledPaths = new Map();
  #fontData;
  constructor(translatedData, inspectFont = null, charProcOperatorList, extra) {
    this.#fontData = translatedData;
    this._inspectFont = inspectFont;
    if (charProcOperatorList) {
      this.charProcOperatorList = charProcOperatorList;
    }
    if (extra) {
      Object.assign(this, extra);
    }
  }
  #getFontFaceProperties() {
    const {
      generatedFontName
    } = this;
    let family = generatedFontName || this.loadedName;
    const css = Object.create(null);
    if (generatedFontName) {
      css.style = this.italic ? "italic" : "normal";
      css.weight = "normal";
      if (this.black) {
        css.weight = "900";
      } else if (this.bold) {
        css.weight = "bold";
      }
    } else if (this.cssFontInfo) {
      family = serializeFontFamily(this.cssFontInfo.fontFamily);
      css.weight = this.cssFontInfo.fontWeight;
      if (this.cssFontInfo.italicAngle) {
        css.style = `oblique ${this.cssFontInfo.italicAngle}deg`;
      }
    }
    return {
      family,
      css
    };
  }
  createNativeFontFace() {
    const {
      data
    } = this;
    if (!data || this.disableFontFace) {
      return null;
    }
    const {
      family,
      css
    } = this.#getFontFaceProperties();
    const nativeFontFace = new FontFace(family, data, css);
    if (!this.generatedFontName) {
      this._inspectFont?.(this);
    }
    return nativeFontFace;
  }
  createFontFaceRule() {
    throw new Error("Not implemented: createFontFaceRule");
  }
  getPathGenerator(objs, character) {
    let path = this.#compiledPaths.get(character);
    if (path) {
      return path;
    }
    const objId = `${this.loadedName}_path_${character}`;
    let cmds;
    try {
      cmds = objs.get(objId);
    } catch (ex) {
      warn(`getPathGenerator - ignoring character: "${ex}".`);
    }
    path = makePathFromDrawOPS(cmds?.path);
    if (!this.fontExtraProperties) {
      objs.delete(objId);
    }
    this.#compiledPaths.set(character, path);
    return path;
  }
  get black() {
    return this.#fontData.black;
  }
  get bold() {
    return this.#fontData.bold;
  }
  get disableFontFace() {
    return this.#fontData.disableFontFace;
  }
  set disableFontFace(value) {
    shadow(this, "disableFontFace", !!value);
  }
  get fontExtraProperties() {
    return this.#fontData.fontExtraProperties;
  }
  get isInvalidPDFjsFont() {
    return this.#fontData.isInvalidPDFjsFont;
  }
  get isType3Font() {
    return this.#fontData.isType3Font;
  }
  get italic() {
    return this.#fontData.italic;
  }
  get missingFile() {
    return this.#fontData.missingFile;
  }
  get remeasure() {
    return this.#fontData.remeasure;
  }
  get vertical() {
    return this.#fontData.vertical;
  }
  get bbox() {
    return this.#fontData.bbox;
  }
  get fontMatrix() {
    return this.#fontData.fontMatrix;
  }
  get fallbackName() {
    return this.#fontData.fallbackName;
  }
  get generatedFontName() {
    return this.#fontData.generatedFontName;
  }
  get loadedName() {
    return this.#fontData.loadedName;
  }
  get mimetype() {
    return this.missingFile ? null : "font/opentype";
  }
  get data() {
    return this.#fontData.data;
  }
  clearData() {
    this.#fontData.clearData();
  }
  get cssFontInfo() {
    return this.#fontData.cssFontInfo;
  }
  get systemFontInfo() {
    return this.#fontData.systemFontInfo;
  }
}

;// ./src/shared/message_handler.js

const CallbackKind = {
  DATA: 1,
  ERROR: 2
};
const StreamKind = {
  CANCEL: 1,
  CANCEL_COMPLETE: 2,
  CLOSE: 3,
  ENQUEUE: 4,
  ERROR: 5,
  PULL: 6,
  PULL_COMPLETE: 7,
  START_COMPLETE: 8
};
function onFn() {}
function wrapReason(ex) {
  if (ex instanceof AbortException || ex instanceof InvalidPDFException || ex instanceof PasswordException || ex instanceof ResponseException || ex instanceof UnknownErrorException) {
    return ex;
  }
  if (typeof ex !== "object" || ex === null) {
    return new UnknownErrorException(String(ex), String(ex));
  }
  switch (ex.name) {
    case "AbortException":
      return new AbortException(ex.message);
    case "InvalidPDFException":
      return new InvalidPDFException(ex.message);
    case "PasswordException":
      return new PasswordException(ex.message, ex.code);
    case "ResponseException":
      return new ResponseException(ex.message, ex.status, ex.missing);
    case "UnknownErrorException":
      return new UnknownErrorException(ex.message, ex.details);
  }
  return new UnknownErrorException(ex.message, ex.toString());
}
class MessageHandler {
  #actions = new Map();
  #callbackCapabilities = new Map();
  #callbackId = 1;
  #comObj;
  #messageAC = new AbortController();
  #sourceName;
  #streamControllers = new Map();
  #streamId = 1;
  #streamSinks = new Map();
  #targetName;
  constructor(sourceName, targetName, comObj) {
    this.#sourceName = sourceName;
    this.#targetName = targetName;
    this.#comObj = comObj;
    comObj.addEventListener("message", this.#onMessage.bind(this), {
      signal: this.#messageAC.signal
    });
  }
  #onMessage({
    data
  }) {
    if (data.targetName !== this.#sourceName) {
      return;
    }
    if (data.stream) {
      this.#processStreamMessage(data);
      return;
    }
    if (data.callback) {
      const {
        callbackId,
        callback
      } = data;
      const capability = this.#callbackCapabilities.get(callbackId);
      if (!capability) {
        throw new Error(`Cannot resolve callback ${callbackId}`);
      }
      this.#callbackCapabilities.delete(callbackId);
      if (callback === CallbackKind.DATA) {
        capability.resolve(data.data);
      } else if (callback === CallbackKind.ERROR) {
        capability.reject(wrapReason(data.reason));
      } else {
        throw new Error("Unexpected callback case");
      }
      return;
    }
    const action = this.#actions.get(data.action);
    if (!action) {
      throw new Error(`Unknown action from worker: ${data.action}`);
    }
    if (data.callbackId) {
      const sourceName = this.#sourceName,
        targetName = data.sourceName,
        comObj = this.#comObj;
      Promise.try(action, data.data).then(result => {
        comObj.postMessage({
          sourceName,
          targetName,
          callback: CallbackKind.DATA,
          callbackId: data.callbackId,
          data: result
        });
      }).catch(reason => {
        comObj.postMessage({
          sourceName,
          targetName,
          callback: CallbackKind.ERROR,
          callbackId: data.callbackId,
          reason: wrapReason(reason)
        });
      });
      return;
    }
    if (data.streamId) {
      this.#createStreamSink(data);
      return;
    }
    action(data.data);
  }
  on(actionName, handler) {
    const ah = this.#actions;
    if (ah.has(actionName)) {
      throw new Error(`There is already a "${actionName}" handler.`);
    }
    ah.set(actionName, handler);
  }
  send(actionName, data, transfers) {
    this.#comObj.postMessage({
      sourceName: this.#sourceName,
      targetName: this.#targetName,
      action: actionName,
      data
    }, transfers);
  }
  sendWithPromise(actionName, data, transfers) {
    const callbackId = this.#callbackId++,
      capability = Promise.withResolvers();
    this.#callbackCapabilities.set(callbackId, capability);
    try {
      this.#comObj.postMessage({
        sourceName: this.#sourceName,
        targetName: this.#targetName,
        action: actionName,
        callbackId,
        data
      }, transfers);
    } catch (ex) {
      capability.reject(ex);
    }
    return capability.promise;
  }
  sendWithStream(actionName, data, queueingStrategy, transfers) {
    const streamId = this.#streamId++,
      sourceName = this.#sourceName,
      targetName = this.#targetName,
      comObj = this.#comObj;
    return new ReadableStream({
      start: controller => {
        const startCapability = Promise.withResolvers();
        this.#streamControllers.set(streamId, {
          controller,
          startCall: startCapability,
          pullCall: null,
          cancelCall: null,
          isClosed: false
        });
        comObj.postMessage({
          sourceName,
          targetName,
          action: actionName,
          streamId,
          data,
          desiredSize: controller.desiredSize
        }, transfers);
        return startCapability.promise;
      },
      pull: controller => {
        const pullCapability = Promise.withResolvers();
        this.#streamControllers.get(streamId).pullCall = pullCapability;
        comObj.postMessage({
          sourceName,
          targetName,
          stream: StreamKind.PULL,
          streamId,
          desiredSize: controller.desiredSize
        });
        return pullCapability.promise;
      },
      cancel: reason => {
        assert(reason instanceof Error, "cancel must have a valid reason");
        const cancelCapability = Promise.withResolvers();
        this.#streamControllers.get(streamId).cancelCall = cancelCapability;
        this.#streamControllers.get(streamId).isClosed = true;
        comObj.postMessage({
          sourceName,
          targetName,
          stream: StreamKind.CANCEL,
          streamId,
          reason: wrapReason(reason)
        });
        return cancelCapability.promise;
      }
    }, queueingStrategy);
  }
  #createStreamSink(data) {
    const streamId = data.streamId,
      sourceName = this.#sourceName,
      targetName = data.sourceName,
      comObj = this.#comObj;
    const streamSinks = this.#streamSinks,
      action = this.#actions.get(data.action);
    const streamSink = {
      enqueue(chunk, size = 1, transfers) {
        if (this.isCancelled) {
          return;
        }
        const lastDesiredSize = this.desiredSize;
        this.desiredSize -= size;
        if (lastDesiredSize > 0 && this.desiredSize <= 0) {
          this.sinkCapability = Promise.withResolvers();
          this.ready = this.sinkCapability.promise;
        }
        comObj.postMessage({
          sourceName,
          targetName,
          stream: StreamKind.ENQUEUE,
          streamId,
          chunk
        }, transfers);
      },
      close() {
        if (this.isCancelled) {
          return;
        }
        this.isCancelled = true;
        comObj.postMessage({
          sourceName,
          targetName,
          stream: StreamKind.CLOSE,
          streamId
        });
        streamSinks.delete(streamId);
      },
      error(reason) {
        if (this.isCancelled) {
          return;
        }
        this.isCancelled = true;
        comObj.postMessage({
          sourceName,
          targetName,
          stream: StreamKind.ERROR,
          streamId,
          reason: wrapReason(reason)
        });
      },
      sinkCapability: Promise.withResolvers(),
      onPull: null,
      onCancel: null,
      isCancelled: false,
      desiredSize: data.desiredSize,
      ready: null
    };
    streamSink.sinkCapability.resolve();
    streamSink.ready = streamSink.sinkCapability.promise;
    streamSinks.set(streamId, streamSink);
    Promise.try(action, data.data, streamSink).then(() => {
      comObj.postMessage({
        sourceName,
        targetName,
        stream: StreamKind.START_COMPLETE,
        streamId,
        success: true
      });
    }, reason => {
      comObj.postMessage({
        sourceName,
        targetName,
        stream: StreamKind.START_COMPLETE,
        streamId,
        reason: wrapReason(reason)
      });
    });
  }
  #processStreamMessage(data) {
    const streamId = data.streamId,
      sourceName = this.#sourceName,
      targetName = data.sourceName,
      comObj = this.#comObj;
    const streamController = this.#streamControllers.get(streamId),
      streamSink = this.#streamSinks.get(streamId);
    switch (data.stream) {
      case StreamKind.START_COMPLETE:
        if (data.success) {
          streamController.startCall.resolve();
        } else {
          streamController.startCall.reject(wrapReason(data.reason));
        }
        break;
      case StreamKind.PULL_COMPLETE:
        if (data.success) {
          streamController.pullCall.resolve();
        } else {
          streamController.pullCall.reject(wrapReason(data.reason));
        }
        break;
      case StreamKind.PULL:
        if (!streamSink) {
          comObj.postMessage({
            sourceName,
            targetName,
            stream: StreamKind.PULL_COMPLETE,
            streamId,
            success: true
          });
          break;
        }
        if (streamSink.desiredSize <= 0 && data.desiredSize > 0) {
          streamSink.sinkCapability.resolve();
        }
        streamSink.desiredSize = data.desiredSize;
        Promise.try(streamSink.onPull || onFn).then(() => {
          comObj.postMessage({
            sourceName,
            targetName,
            stream: StreamKind.PULL_COMPLETE,
            streamId,
            success: true
          });
        }, reason => {
          comObj.postMessage({
            sourceName,
            targetName,
            stream: StreamKind.PULL_COMPLETE,
            streamId,
            reason: wrapReason(reason)
          });
        });
        break;
      case StreamKind.ENQUEUE:
        assert(streamController, "enqueue should have stream controller");
        if (streamController.isClosed) {
          break;
        }
        streamController.controller.enqueue(data.chunk);
        break;
      case StreamKind.CLOSE:
        assert(streamController, "close should have stream controller");
        if (streamController.isClosed) {
          break;
        }
        streamController.isClosed = true;
        streamController.controller.close();
        this.#deleteStreamController(streamController, streamId);
        break;
      case StreamKind.ERROR:
        assert(streamController, "error should have stream controller");
        streamController.controller.error(wrapReason(data.reason));
        this.#deleteStreamController(streamController, streamId);
        break;
      case StreamKind.CANCEL_COMPLETE:
        if (data.success) {
          streamController.cancelCall.resolve();
        } else {
          streamController.cancelCall.reject(wrapReason(data.reason));
        }
        this.#deleteStreamController(streamController, streamId);
        break;
      case StreamKind.CANCEL:
        if (!streamSink) {
          break;
        }
        const dataReason = wrapReason(data.reason);
        Promise.try(streamSink.onCancel || onFn, dataReason).then(() => {
          comObj.postMessage({
            sourceName,
            targetName,
            stream: StreamKind.CANCEL_COMPLETE,
            streamId,
            success: true
          });
        }, reason => {
          comObj.postMessage({
            sourceName,
            targetName,
            stream: StreamKind.CANCEL_COMPLETE,
            streamId,
            reason: wrapReason(reason)
          });
        });
        streamSink.sinkCapability.reject(dataReason);
        streamSink.isCancelled = true;
        this.#streamSinks.delete(streamId);
        break;
      default:
        throw new Error("Unexpected stream case");
    }
  }
  async #deleteStreamController(streamController, streamId) {
    await Promise.allSettled([streamController.startCall?.promise, streamController.pullCall?.promise, streamController.cancelCall?.promise]);
    this.#streamControllers.delete(streamId);
  }
  destroy() {
    this.#messageAC?.abort();
    this.#messageAC = null;
  }
  get destroyed() {
    return !this.#messageAC;
  }
}

;// ./src/shared/obj_bin_transform_utils.js

class CSS_FONT_INFO {
  static strings = ["fontFamily", "fontWeight", "italicAngle"];
}
class SYSTEM_FONT_INFO {
  static strings = ["css", "loadedName", "baseFontName", "src"];
}
class FONT_INFO {
  static bools = ["black", "bold", "disableFontFace", "fontExtraProperties", "isInvalidPDFjsFont", "isType3Font", "italic", "missingFile", "remeasure", "vertical"];
  static strings = ["fallbackName", "generatedFontName", "loadedName"];
  static OFFSET_BBOX = Math.ceil(this.bools.length * 2 / 8);
  static OFFSET_FONT_MATRIX = this.OFFSET_BBOX + 1 + 2 * 4;
  static OFFSET_STRINGS = this.OFFSET_FONT_MATRIX + 1 + 8 * 6;
}
class PATTERN_INFO {
  static KIND = 0;
  static HAS_BBOX = 1;
  static HAS_BACKGROUND = 2;
  static SHADING_TYPE = 3;
  static N_COORD = 4;
  static N_COLOR = 8;
  static N_STOP = 12;
  static N_FIGURES = 16;
}
class InfoUtils {
  static get decoder() {
    return shadow(this, "decoder", new TextDecoder());
  }
  static get encoder() {
    return shadow(this, "encoder", new TextEncoder());
  }
}

;// ./src/display/obj_bin_transform_display.js


function readString(buffer, view, index, offset = 0) {
  const {
    decoder
  } = InfoUtils;
  for (let i = 0; i < index; i++) {
    offset += view.getUint32(offset) + 4;
  }
  const length = view.getUint32(offset);
  return decoder.decode(new Uint8Array(buffer, offset + 4, length));
}
class CssFontInfo {
  #buffer;
  #view;
  constructor(buffer) {
    this.#buffer = buffer;
    this.#view = new DataView(buffer);
  }
  #readString(index) {
    assert(index < CSS_FONT_INFO.strings.length, "Invalid string index");
    return readString(this.#buffer, this.#view, index);
  }
  get fontFamily() {
    return shadow(this, "fontFamily", this.#readString(0));
  }
  get fontWeight() {
    return shadow(this, "fontWeight", this.#readString(1));
  }
  get italicAngle() {
    return shadow(this, "italicAngle", this.#readString(2));
  }
}
class SystemFontInfo {
  #buffer;
  #view;
  constructor(buffer) {
    this.#buffer = buffer;
    this.#view = new DataView(buffer);
  }
  #readString(index) {
    assert(index < SYSTEM_FONT_INFO.strings.length, "Invalid string index");
    return readString(this.#buffer, this.#view, index, 4);
  }
  get css() {
    return shadow(this, "css", this.#readString(0));
  }
  get loadedName() {
    return shadow(this, "loadedName", this.#readString(1));
  }
  get baseFontName() {
    return shadow(this, "baseFontName", this.#readString(2));
  }
  get src() {
    return shadow(this, "src", this.#readString(3));
  }
  get style() {
    let offset = 0;
    offset += 4 + this.#view.getUint32(offset);
    const style = readString(this.#buffer, this.#view, 0, offset),
      weight = readString(this.#buffer, this.#view, 1, offset);
    return shadow(this, "style", {
      style,
      weight
    });
  }
}
class FontInfo {
  #buffer;
  #view;
  constructor(buffer) {
    this.#buffer = buffer;
    this.#view = new DataView(buffer);
  }
  #readBoolean(index) {
    assert(index < FONT_INFO.bools.length, "Invalid boolean index");
    const byteOffset = Math.floor(index / 4);
    const bitOffset = index * 2 % 8;
    const value = this.#view.getUint8(byteOffset) >> bitOffset & 0x03;
    return value === 0x00 ? undefined : value === 0x02;
  }
  get black() {
    return shadow(this, "black", this.#readBoolean(0));
  }
  get bold() {
    return shadow(this, "bold", this.#readBoolean(1));
  }
  get disableFontFace() {
    return shadow(this, "disableFontFace", this.#readBoolean(2));
  }
  get fontExtraProperties() {
    return shadow(this, "fontExtraProperties", this.#readBoolean(3));
  }
  get isInvalidPDFjsFont() {
    return shadow(this, "isInvalidPDFjsFont", this.#readBoolean(4));
  }
  get isType3Font() {
    return shadow(this, "isType3Font", this.#readBoolean(5));
  }
  get italic() {
    return shadow(this, "italic", this.#readBoolean(6));
  }
  get missingFile() {
    return shadow(this, "missingFile", this.#readBoolean(7));
  }
  get remeasure() {
    return shadow(this, "remeasure", this.#readBoolean(8));
  }
  get vertical() {
    return shadow(this, "vertical", this.#readBoolean(9));
  }
  #readArray(offset, arrLen, lookupName, increment) {
    const len = this.#view.getUint8(offset++);
    if (len === 0) {
      return undefined;
    }
    assert(len === arrLen, "Invalid array length.");
    const arr = new Array(len);
    for (let i = 0; i < len; i++) {
      arr[i] = this.#view[lookupName](offset, true);
      offset += increment;
    }
    return arr;
  }
  get bbox() {
    return shadow(this, "bbox", this.#readArray(FONT_INFO.OFFSET_BBOX, 4, "getInt16", 2));
  }
  get fontMatrix() {
    return shadow(this, "fontMatrix", this.#readArray(FONT_INFO.OFFSET_FONT_MATRIX, 6, "getFloat64", 8));
  }
  #readString(index) {
    assert(index < FONT_INFO.strings.length, "Invalid string index");
    return readString(this.#buffer, this.#view, index, FONT_INFO.OFFSET_STRINGS + 4);
  }
  get fallbackName() {
    return shadow(this, "fallbackName", this.#readString(0));
  }
  get generatedFontName() {
    return shadow(this, "generatedFontName", this.#readString(1));
  }
  get loadedName() {
    return shadow(this, "loadedName", this.#readString(2));
  }
  #getBufferOffset(index) {
    let offset = FONT_INFO.OFFSET_STRINGS;
    for (let i = 0; i <= index; i++) {
      offset += 4 + this.#view.getUint32(offset);
    }
    const length = this.#view.getUint32(offset);
    return {
      offset,
      length
    };
  }
  get data() {
    const {
      offset,
      length
    } = this.#getBufferOffset(2);
    return !length ? undefined : new Uint8Array(this.#buffer, offset + 4, length);
  }
  clearData() {
    const {
      offset,
      length
    } = this.#getBufferOffset(2);
    if (!length) {
      return;
    }
    this.#view.setUint32(offset, 0);
    this.#buffer = new Uint8Array(this.#buffer, 0, offset + 4).slice().buffer;
    this.#view = new DataView(this.#buffer);
  }
  get cssFontInfo() {
    const {
      offset,
      length
    } = this.#getBufferOffset(1);
    let info = null;
    if (length) {
      const data = new Uint8Array(this.#buffer, offset + 4, length).slice();
      info = new CssFontInfo(data.buffer);
    }
    return shadow(this, "cssFontInfo", info);
  }
  get systemFontInfo() {
    const {
      offset,
      length
    } = this.#getBufferOffset(0);
    let info = null;
    if (length) {
      const data = new Uint8Array(this.#buffer, offset + 4, length).slice();
      info = new SystemFontInfo(data.buffer);
    }
    return shadow(this, "systemFontInfo", info);
  }
}
class PatternInfo {
  constructor(buffer) {
    this.buffer = buffer;
    this.view = new DataView(buffer);
    this.data = new Uint8Array(buffer);
  }
  getIR() {
    const dataView = this.view;
    const kind = this.data[PATTERN_INFO.KIND];
    const hasBBox = !!this.data[PATTERN_INFO.HAS_BBOX];
    const hasBackground = !!this.data[PATTERN_INFO.HAS_BACKGROUND];
    const nCoord = dataView.getUint32(PATTERN_INFO.N_COORD, true);
    const nColor = dataView.getUint32(PATTERN_INFO.N_COLOR, true);
    const nStop = dataView.getUint32(PATTERN_INFO.N_STOP, true);
    let offset = 20;
    const coords = new Float32Array(this.buffer, offset, nCoord * 2);
    offset += nCoord * 8;
    const colors = new Uint8Array(this.buffer, offset, nColor * 4);
    offset += nColor * 4;
    const stops = [];
    for (let i = 0; i < nStop; ++i) {
      const p = dataView.getFloat32(offset, true);
      offset += 4;
      const rgb = dataView.getUint32(offset, true);
      offset += 4;
      stops.push([p, `#${rgb.toString(16).padStart(6, "0")}`]);
    }
    let bbox = null;
    if (hasBBox) {
      bbox = [];
      for (let i = 0; i < 4; ++i) {
        bbox.push(dataView.getFloat32(offset, true));
        offset += 4;
      }
    }
    let background = null;
    if (hasBackground) {
      background = new Uint8Array(this.buffer, offset, 3);
      offset += 3;
    }
    if (kind === 1) {
      return ["RadialAxial", "axial", bbox, stops, [coords[0], coords[1]], [coords[2], coords[3]], null, null];
    }
    if (kind === 2) {
      return ["RadialAxial", "radial", bbox, stops, [coords[0], coords[1]], [coords[3], coords[4]], coords[2], coords[5]];
    }
    if (kind === 3) {
      const shadingType = this.data[PATTERN_INFO.SHADING_TYPE];
      let bounds = null;
      if (coords.length > 0) {
        bounds = BBOX_INIT.slice();
        for (let i = 0, ii = coords.length; i < ii; i += 2) {
          Util.pointBoundingBox(coords[i], coords[i + 1], bounds);
        }
      }
      return ["Mesh", shadingType, coords, colors, nCoord, bounds, bbox, background];
    }
    throw new Error(`Unsupported pattern kind: ${kind}`);
  }
}
class FontPathInfo {
  #buffer;
  constructor(buffer) {
    this.#buffer = buffer;
  }
  get path() {
    return new Float16Array(this.#buffer);
  }
}

;// ./src/display/pdf_objects.js
const INITIAL_DATA = Symbol("INITIAL_DATA");
const dataObj = () => ({
  ...Promise.withResolvers(),
  data: INITIAL_DATA
});
class PDFObjects {
  #objs = new Map();
  get(objId, callback = null, errorCallback = null) {
    if (callback) {
      const obj = this.#objs.getOrInsertComputed(objId, dataObj);
      obj.promise.then(() => callback(obj.data), errorCallback);
      return null;
    }
    const obj = this.#objs.get(objId);
    if (!obj || obj.data === INITIAL_DATA) {
      throw new Error(`Requesting object that isn't resolved yet ${objId}.`);
    }
    return obj.data;
  }
  has(objId) {
    const obj = this.#objs.get(objId);
    return !!obj && obj.data !== INITIAL_DATA;
  }
  delete(objId) {
    const obj = this.#objs.get(objId);
    if (!obj || obj.data === INITIAL_DATA) {
      return false;
    }
    this.#objs.delete(objId);
    return true;
  }
  resolve(objId, data = null) {
    const obj = this.#objs.getOrInsertComputed(objId, dataObj);
    if (obj.data !== INITIAL_DATA) {
      throw new Error(`Object already resolved ${objId}.`);
    }
    obj.data = data;
    obj.resolve();
  }
  reject(objId, reason) {
    const obj = this.#objs.getOrInsertComputed(objId, dataObj);
    if (obj.data !== INITIAL_DATA) {
      return;
    }
    obj.promise.catch(() => {});
    obj.reject(reason);
  }
  clear() {
    for (const {
      data
    } of this.#objs.values()) {
      data?.bitmap?.close();
    }
    this.#objs.clear();
  }
  *[Symbol.iterator]() {
    for (const [objId, {
      data
    }] of this.#objs) {
      if (data !== INITIAL_DATA) {
        yield [objId, data];
      }
    }
  }
}

;// ./src/display/object_handler.js




class ObjectHandler {
  constructor({
    messageHandler,
    commonObjs,
    fontLoader,
    pageCache,
    pdfBug = false,
    shouldCreatePageObjs = false
  }) {
    this.messageHandler = messageHandler;
    this.commonObjs = commonObjs;
    this.fontLoader = fontLoader;
    this.pageCache = pageCache;
    this.pdfBug = pdfBug;
    this.shouldCreatePageObjs = shouldCreatePageObjs;
  }
  resolveCommonObject(id, type, exportedData) {
    switch (type) {
      case "Font":
        if ("error" in exportedData) {
          const exportedError = exportedData.error;
          warn(`Error during font loading: ${exportedError}`);
          this.commonObjs.resolve(id, exportedError);
          break;
        }
        const fontData = new FontInfo(exportedData.buffer);
        const inspectFont = null;
        const font = new FontFaceObject(fontData, inspectFont, exportedData.charProcOperatorList, exportedData.extra);
        this.fontLoader.bind(font).catch(() => this.messageHandler.sendWithPromise("FontFallback", {
          id
        }).catch(reason => {
          warn(`FontFallback failed for "${id}": ${reason}`);
        })).finally(() => {
          if (!font.fontExtraProperties) {
            font.clearData();
          }
          this.commonObjs.resolve(id, font);
        });
        break;
      case "FontPath":
        this.commonObjs.resolve(id, new FontPathInfo(exportedData));
        break;
      case "Image":
        this.commonObjs.resolve(id, exportedData);
        break;
      case "Pattern":
        const pattern = new PatternInfo(exportedData);
        this.commonObjs.resolve(id, pattern.getIR());
        break;
      default:
        throw new Error(`Got unknown common object type ${type}`);
    }
    return null;
  }
  resolveObject(id, pageProxyId, type, exportedData) {
    let pageOrObjs = this.pageCache.get(pageProxyId);
    if (!pageOrObjs) {
      if (!this.shouldCreatePageObjs) {
        return false;
      }
      pageOrObjs = new PDFObjects();
      this.pageCache.set(pageProxyId, pageOrObjs);
    }
    const objs = pageOrObjs.objs || pageOrObjs;
    if (objs.has(id)) {
      return false;
    }
    if (pageOrObjs._intentStates?.size === 0) {
      exportedData?.bitmap?.close();
      return false;
    }
    switch (type) {
      case "Image":
      case "Pattern":
        objs.resolve(id, exportedData);
        return true;
      default:
        throw new Error(`Got unknown object type ${type}`);
    }
  }
}

;// ./src/display/canvas_factory.js

class BaseCanvasFactory {
  #enableHWA = false;
  constructor({
    enableHWA = false
  }) {
    this.#enableHWA = enableHWA;
  }
  create(width, height) {
    if (width <= 0 || height <= 0) {
      throw new Error("Invalid canvas size");
    }
    const canvas = this._createCanvas(width, height);
    return {
      canvas,
      context: canvas.getContext("2d", {
        willReadFrequently: !this.#enableHWA
      })
    };
  }
  reset({
    canvas
  }, width, height) {
    if (!canvas) {
      throw new Error("Canvas is not specified");
    }
    if (width <= 0 || height <= 0) {
      throw new Error("Invalid canvas size");
    }
    canvas.width = width;
    canvas.height = height;
  }
  destroy(canvasAndContext) {
    const {
      canvas
    } = canvasAndContext;
    if (!canvas) {
      throw new Error("Canvas is not specified");
    }
    canvas.width = canvas.height = 0;
    canvasAndContext.canvas = null;
    canvasAndContext.context = null;
  }
  getNoAlphaContext(canvas) {
    return canvas.getContext("2d", {
      alpha: false,
      willReadFrequently: !this.#enableHWA
    });
  }
  _createCanvas(width, height) {
    unreachable("Abstract method `_createCanvas` called.");
  }
}

;// ./src/display/offscreen_canvas_factory.js

class OffscreenCanvasFactory extends BaseCanvasFactory {
  _createCanvas(width, height) {
    return new OffscreenCanvas(width, height);
  }
}

;// ./src/shared/murmurhash3.js
const SEED = 0xc3d2e1f0;
const MASK_HIGH = 0xffff0000;
const MASK_LOW = 0xffff;
class MurmurHash3_64 {
  constructor(seed) {
    this.h1 = seed ? seed & 0xffffffff : SEED;
    this.h2 = seed ? seed & 0xffffffff : SEED;
  }
  update(input) {
    let data, length;
    if (typeof input === "string") {
      data = new Uint8Array(input.length * 2);
      length = 0;
      for (let i = 0, ii = input.length; i < ii; i++) {
        const code = input.charCodeAt(i);
        if (code <= 0xff) {
          data[length++] = code;
        } else {
          data[length++] = code >>> 8;
          data[length++] = code & 0xff;
        }
      }
    } else if (ArrayBuffer.isView(input)) {
      data = input.slice();
      length = data.byteLength;
    } else {
      throw new Error("Invalid data format, must be a string or TypedArray.");
    }
    const blockCounts = length >> 2;
    const tailLength = length - blockCounts * 4;
    const dataUint32 = new Uint32Array(data.buffer, 0, blockCounts);
    let k1 = 0,
      k2 = 0;
    let h1 = this.h1,
      h2 = this.h2;
    const C1 = 0xcc9e2d51,
      C2 = 0x1b873593;
    const C1_LOW = C1 & MASK_LOW,
      C2_LOW = C2 & MASK_LOW;
    for (let i = 0; i < blockCounts; i++) {
      if (i & 1) {
        k1 = dataUint32[i];
        k1 = k1 * C1 & MASK_HIGH | k1 * C1_LOW & MASK_LOW;
        k1 = k1 << 15 | k1 >>> 17;
        k1 = k1 * C2 & MASK_HIGH | k1 * C2_LOW & MASK_LOW;
        h1 ^= k1;
        h1 = h1 << 13 | h1 >>> 19;
        h1 = h1 * 5 + 0xe6546b64;
      } else {
        k2 = dataUint32[i];
        k2 = k2 * C1 & MASK_HIGH | k2 * C1_LOW & MASK_LOW;
        k2 = k2 << 15 | k2 >>> 17;
        k2 = k2 * C2 & MASK_HIGH | k2 * C2_LOW & MASK_LOW;
        h2 ^= k2;
        h2 = h2 << 13 | h2 >>> 19;
        h2 = h2 * 5 + 0xe6546b64;
      }
    }
    k1 = 0;
    switch (tailLength) {
      case 3:
        k1 ^= data[blockCounts * 4 + 2] << 16;
      case 2:
        k1 ^= data[blockCounts * 4 + 1] << 8;
      case 1:
        k1 ^= data[blockCounts * 4];
        k1 = k1 * C1 & MASK_HIGH | k1 * C1_LOW & MASK_LOW;
        k1 = k1 << 15 | k1 >>> 17;
        k1 = k1 * C2 & MASK_HIGH | k1 * C2_LOW & MASK_LOW;
        if (blockCounts & 1) {
          h1 ^= k1;
        } else {
          h2 ^= k1;
        }
    }
    this.h1 = h1;
    this.h2 = h2;
  }
  hexdigest() {
    let h1 = this.h1,
      h2 = this.h2;
    h1 ^= h2 >>> 1;
    h1 = h1 * 0xed558ccd & MASK_HIGH | h1 * 0x8ccd & MASK_LOW;
    h2 = h2 * 0xff51afd7 & MASK_HIGH | ((h2 << 16 | h1 >>> 16) * 0xafd7ed55 & MASK_HIGH) >>> 16;
    h1 ^= h2 >>> 1;
    h1 = h1 * 0x1a85ec53 & MASK_HIGH | h1 * 0xec53 & MASK_LOW;
    h2 = h2 * 0xc4ceb9fe & MASK_HIGH | ((h2 << 16 | h1 >>> 16) * 0xb9fe1a85 & MASK_HIGH) >>> 16;
    h1 ^= h2 >>> 1;
    return (h1 >>> 0).toString(16).padStart(8, "0") + (h2 >>> 0).toString(16).padStart(8, "0");
  }
}

;// ./src/display/optional_content_config.js


const INTERNAL = Symbol("INTERNAL");
class OptionalContentGroup {
  #isDisplay = false;
  #isPrint = false;
  #userSet = false;
  #visible = true;
  constructor(renderingIntent, {
    name,
    intent,
    usage,
    rbGroups
  }) {
    this.#isDisplay = !!(renderingIntent & RenderingIntentFlag.DISPLAY);
    this.#isPrint = !!(renderingIntent & RenderingIntentFlag.PRINT);
    this.name = name;
    this.intent = intent;
    this.usage = usage;
    this.rbGroups = rbGroups;
  }
  get visible() {
    if (this.#userSet) {
      return this.#visible;
    }
    if (!this.#visible) {
      return false;
    }
    const {
      print,
      view
    } = this.usage;
    if (this.#isDisplay) {
      return view?.viewState !== "OFF";
    } else if (this.#isPrint) {
      return print?.printState !== "OFF";
    }
    return true;
  }
  _setVisible(internal, visible, userSet = false) {
    if (internal !== INTERNAL) {
      unreachable("Internal method `_setVisible` called.");
    }
    this.#userSet = userSet;
    this.#visible = visible;
  }
  get serializable() {
    return {
      userSet: this.#userSet,
      visible: this.#visible
    };
  }
}
class OptionalContentConfig {
  #cachedGetHash = null;
  #groups = new Map();
  #initialHash = null;
  #order = null;
  #rawData;
  creator = null;
  name = null;
  constructor(data, renderingIntent = RenderingIntentFlag.DISPLAY, groupState = null) {
    this.#rawData = data;
    this.renderingIntent = renderingIntent;
    if (data === null) {
      return;
    }
    this.name = data.name;
    this.creator = data.creator;
    this.#order = data.order;
    for (const group of data.groups) {
      this.#groups.set(group.id, new OptionalContentGroup(renderingIntent, group));
    }
    if (groupState) {
      if (groupState.size !== this.#groups.size) {
        unreachable("Incorrect serialized groupState.");
      }
      for (const [id, group] of groupState) {
        this.#groups.get(id)._setVisible(INTERNAL, group.visible, group.userSet);
      }
    } else {
      if (data.baseState === "OFF") {
        for (const group of this.#groups.values()) {
          group._setVisible(INTERNAL, false);
        }
      }
      for (const on of data.on) {
        this.#groups.get(on)._setVisible(INTERNAL, true);
      }
      for (const off of data.off) {
        this.#groups.get(off)._setVisible(INTERNAL, false);
      }
    }
    this.#initialHash = this.getHash();
  }
  #evaluateVisibilityExpression(array) {
    const length = array.length;
    if (length < 2) {
      return true;
    }
    const operator = array[0];
    for (let i = 1; i < length; i++) {
      const element = array[i];
      let state;
      if (Array.isArray(element)) {
        state = this.#evaluateVisibilityExpression(element);
      } else if (this.#groups.has(element)) {
        state = this.#groups.get(element).visible;
      } else {
        warn(`Optional content group not found: ${element}`);
        return true;
      }
      switch (operator) {
        case "And":
          if (!state) {
            return false;
          }
          break;
        case "Or":
          if (state) {
            return true;
          }
          break;
        case "Not":
          return !state;
        default:
          return true;
      }
    }
    return operator === "And";
  }
  isVisible(group) {
    if (this.#groups.size === 0) {
      return true;
    }
    if (!group) {
      info("Optional content group not defined.");
      return true;
    }
    if (group.type === "OCG") {
      if (!this.#groups.has(group.id)) {
        warn(`Optional content group not found: ${group.id}`);
        return true;
      }
      return this.#groups.get(group.id).visible;
    } else if (group.type === "OCMD") {
      if (group.expression) {
        return this.#evaluateVisibilityExpression(group.expression);
      }
      if (!group.policy || group.policy === "AnyOn") {
        for (const id of group.ids) {
          if (!this.#groups.has(id)) {
            warn(`Optional content group not found: ${id}`);
            return true;
          }
          if (this.#groups.get(id).visible) {
            return true;
          }
        }
        return false;
      } else if (group.policy === "AllOn") {
        for (const id of group.ids) {
          if (!this.#groups.has(id)) {
            warn(`Optional content group not found: ${id}`);
            return true;
          }
          if (!this.#groups.get(id).visible) {
            return false;
          }
        }
        return true;
      } else if (group.policy === "AnyOff") {
        for (const id of group.ids) {
          if (!this.#groups.has(id)) {
            warn(`Optional content group not found: ${id}`);
            return true;
          }
          if (!this.#groups.get(id).visible) {
            return true;
          }
        }
        return false;
      } else if (group.policy === "AllOff") {
        for (const id of group.ids) {
          if (!this.#groups.has(id)) {
            warn(`Optional content group not found: ${id}`);
            return true;
          }
          if (this.#groups.get(id).visible) {
            return false;
          }
        }
        return true;
      }
      warn(`Unknown optional content policy ${group.policy}.`);
      return true;
    }
    warn(`Unknown group type ${group.type}.`);
    return true;
  }
  setVisibility(id, visible = true, preserveRB = true) {
    throw new Error("Not implemented: setVisibility");
  }
  setOCGState({
    state,
    preserveRB
  }) {
    throw new Error("Not implemented: setOCGState");
  }
  get hasInitialVisibility() {
    return this.#initialHash === null || this.getHash() === this.#initialHash;
  }
  getOrder() {
    throw new Error("Not implemented: getOrder");
  }
  getGroup(id) {
    return this.#groups.get(id) || null;
  }
  getHash() {
    if (this.#cachedGetHash !== null) {
      return this.#cachedGetHash;
    }
    const hash = new MurmurHash3_64();
    for (const [id, group] of this.#groups) {
      hash.update(`${id}:${group.visible}`);
    }
    return this.#cachedGetHash = hash.hexdigest();
  }
  [Symbol.iterator]() {
    return this.#groups.entries();
  }
  get serializable() {
    const groupState = new Map();
    for (const [id, group] of this.#groups) {
      groupState.set(id, group.serializable);
    }
    return {
      data: this.#rawData,
      renderingIntent: this.renderingIntent,
      groupState
    };
  }
  static fromSerializable({
    data,
    renderingIntent,
    groupState
  }) {
    return new OptionalContentConfig(data, renderingIntent, groupState);
  }
}

;// ./src/display/filter_factory.js

class BaseFilterFactory {
  addFilter(maps) {
    return "none";
  }
  addHCMFilter(fgColor, bgColor) {
    return "none";
  }
  addAlphaFilter(map) {
    return "none";
  }
  addLuminosityFilter(map) {
    return "none";
  }
  addKnockoutFilter(alpha = 0) {
    return "none";
  }
  addHighlightHCMFilter(filterName, fgColor, bgColor, newFgColor, newBgColor) {
    return "none";
  }
  addSelectionHCMFilter(fgColor, bgColor) {
    return "none";
  }
  addSelectionFilter() {
    return "none";
  }
  createSelectionStyle(pageColors = null) {
    return null;
  }
  destroy(keepHCM = false) {}
}

;// ./src/display/worker_filter_factory.js

class WorkerFilterFactory extends BaseFilterFactory {}

;// ./src/display/renderer_worker.js











const PARTIAL_FRAME_TIME = 500;
class RendererMessageHandler {
  static #canvasFactory;
  static #commonObjs = new PDFObjects();
  static #connection = null;
  static #enableHWA = false;
  static #enableWebGPU = false;
  static #filterFactory;
  static #fontLoader = new FontLoader({
    ownerDocument: globalThis
  });
  static #hasMeshPattern = false;
  static #objsMap = new Map();
  static #operatorLists = new Map();
  static #renderTaskStates = new Map();
  static #yieldChannel = null;
  static #yieldResolvers = [];
  static {
    if (typeof window === "undefined" && !(/* inlined export .isNodeJS */false) && typeof self !== "undefined" && typeof self.postMessage === "function" && "onmessage" in self) {
      this.#initializeFromPort(self);
    }
  }
  static #getPageObjs(pageProxyId) {
    return this.#objsMap.getOrInsertComputed(pageProxyId, () => new PDFObjects());
  }
  static #getOperatorList(pageProxyId, operatorListId) {
    return this.#operatorLists.getOrInsertComputed(pageProxyId, makeMap).getOrInsertComputed(operatorListId, () => ({
      fnArray: [],
      argsArray: [],
      pathCache: new Map()
    }));
  }
  static #collectAnnotationBitmaps(renderTaskState, transfers) {
    const map = renderTaskState.gfx?.annotationCanvasMap;
    if (!map?.size) {
      return null;
    }
    const tuples = [];
    for (const [id, value] of map) {
      for (const canvas of Array.isArray(value) ? value : [value]) {
        const bitmap = canvas.transferToImageBitmap();
        tuples.push([id, getAnnotationCanvasName(canvas), bitmap]);
        transfers.push(bitmap);
      }
    }
    return tuples;
  }
  static async #sendFrame(handler, renderTaskState, isFinal) {
    const {
      canvas,
      renderTaskId
    } = renderTaskState;
    let bitmap = null;
    if (canvas.width === 0 || canvas.height === 0) {
      if (!isFinal) {
        return;
      }
    } else if (isFinal) {
      bitmap = canvas.transferToImageBitmap();
    } else {
      const {
        transparentCanvas
      } = renderTaskState.gfx;
      if (transparentCanvas) {
        const composite = new OffscreenCanvas(canvas.width, canvas.height);
        const ctx = composite.getContext("2d");
        ctx.drawImage(canvas, 0, 0);
        ctx.drawImage(transparentCanvas, 0, 0);
        bitmap = composite.transferToImageBitmap();
        composite.width = composite.height = 0;
      } else {
        bitmap = await createImageBitmap(canvas);
        if (renderTaskState.aborted) {
          bitmap.close();
          return;
        }
      }
    }
    const transfers = bitmap ? [bitmap] : [];
    const annotationBitmaps = isFinal ? this.#collectAnnotationBitmaps(renderTaskState, transfers) : null;
    handler.send("RenderFrame", {
      renderTaskId,
      bitmap,
      annotationBitmaps
    }, transfers);
  }
  static async #maybeSendInterimFrame(handler, renderTaskState) {
    if (!renderTaskState.partialFrames || renderTaskState.aborted || Date.now() - renderTaskState.lastFrameTime < PARTIAL_FRAME_TIME || renderTaskState.operatorListIdx === renderTaskState.lastFrameIdx) {
      return;
    }
    try {
      await this.#sendFrame(handler, renderTaskState, false);
    } catch (ex) {
      warn(`Failed to send an interim frame: ${ex.message}`);
    }
    renderTaskState.lastFrameTime = Date.now();
    renderTaskState.lastFrameIdx = renderTaskState.operatorListIdx;
  }
  static #cleanupPage(pageProxyId) {
    this.#objsMap.get(pageProxyId)?.clear();
    this.#objsMap.delete(pageProxyId);
    this.#operatorLists.delete(pageProxyId);
    for (const [renderTaskId, renderTaskState] of this.#renderTaskStates) {
      if (renderTaskState.pageProxyId === pageProxyId) {
        this.#cleanupRenderTask(renderTaskId);
      }
    }
  }
  static #cleanupRenderTask(renderTaskId, keepCanvas = false) {
    const renderTaskState = this.#renderTaskStates.get(renderTaskId);
    if (!renderTaskState) {
      return;
    }
    renderTaskState.aborted = true;
    renderTaskState.continueResolve?.();
    renderTaskState.gfx?.endDrawing();
    if (!keepCanvas) {
      this.#releaseCanvases(renderTaskState);
    }
    this.#renderTaskStates.delete(renderTaskId);
  }
  static #releaseCanvases({
    canvas,
    gfx
  }) {
    canvas.width = canvas.height = 0;
    const map = gfx?.annotationCanvasMap;
    if (!map) {
      return;
    }
    for (const value of map.values()) {
      for (const annotationCanvas of Array.isArray(value) ? value : [value]) {
        annotationCanvas.width = annotationCanvas.height = 0;
      }
    }
    map.clear();
  }
  static #appendFromSharedOperatorList(renderTaskState, length, operationsFilterMask, lastChunk) {
    const {
      operatorList,
      sharedOperatorList
    } = renderTaskState;
    const {
      fnArray,
      argsArray
    } = sharedOperatorList;
    for (let i = operatorList.fnArray.length; i < length; i++) {
      operatorList.fnArray.push(fnArray[i]);
      operatorList.argsArray.push(argsArray[i]);
    }
    if (operationsFilterMask) {
      const mask = renderTaskState.operationsFilterMask ||= [];
      for (let i = 0, ii = operationsFilterMask.length; i < ii; i++) {
        mask.push(operationsFilterMask[i]);
      }
    }
    operatorList.lastChunk = lastChunk;
    renderTaskState.dependencyTracker?.growOperationsCount(operatorList.fnArray.length);
  }
  static async #executeOperatorList(handler, renderTaskState) {
    const {
      operatorList,
      gfx,
      operationsFilterMask
    } = renderTaskState;
    const operationsFilter = operationsFilterMask ? i => operationsFilterMask[i] : null;
    while (!renderTaskState.aborted) {
      const {
        promise,
        resolve,
        reject
      } = Promise.withResolvers();
      renderTaskState.continueResolve = resolve;
      renderTaskState.operatorListIdx = gfx.executeOperatorList(operatorList, renderTaskState.operatorListIdx, resolve, reject, undefined, operationsFilter);
      if (renderTaskState.operatorListIdx === operatorList.argsArray.length) {
        return renderTaskState.operatorListIdx;
      }
      await this.#maybeSendInterimFrame(handler, renderTaskState);
      await promise;
      if (renderTaskState.aborted) {
        break;
      }
      await this.#yield();
    }
    return renderTaskState.operatorListIdx;
  }
  static #yield() {
    if (!this.#yieldChannel) {
      this.#yieldChannel = new MessageChannel();
      this.#yieldChannel.port1.onmessage = () => {
        this.#yieldResolvers.shift()();
      };
    }
    const {
      promise,
      resolve
    } = Promise.withResolvers();
    this.#yieldResolvers.push(resolve);
    this.#yieldChannel.port2.postMessage(null);
    return promise;
  }
  static #setupObjectHandler(handler) {
    const objectHandler = new ObjectHandler({
      messageHandler: handler,
      commonObjs: this.#commonObjs,
      fontLoader: this.#fontLoader,
      pageCache: this.#objsMap,
      shouldCreatePageObjs: true
    });
    handler.on("commonobj", ([id, type, exportedData]) => {
      if (this.#commonObjs.has(id)) {
        return;
      }
      objectHandler.resolveCommonObject(id, type, exportedData);
      if (type === "Pattern" && this.#commonObjs.get(id)[0] === "Mesh") {
        this.#hasMeshPattern = true;
      }
    });
    handler.on("obj", ([id, pageProxyId, type, imageData]) => {
      if (type === "Pattern" && imageData[0] === "Mesh") {
        this.#hasMeshPattern = true;
      }
      objectHandler.resolveObject(id, pageProxyId, type, imageData);
    });
    handler.on("objFailed", ({
      id,
      pageProxyId,
      reason
    }) => {
      const error = new Error(reason);
      if (pageProxyId === null) {
        this.#commonObjs.reject(id, error);
        return;
      }
      this.#getPageObjs(pageProxyId).reject(id, error);
    });
  }
  static #setup(handler) {
    handler.on("configure", data => {
      setVerbosityLevel(data.verbosity);
      this.#enableHWA = data.enableHWA;
      this.#enableWebGPU = data.enableWebGPU;
      if (this.#enableWebGPU) {
        initGPU();
      }
    });
    this.#setupObjectHandler(handler);
    handler.on("cleanupPage", ({
      pageProxyId
    }) => {
      this.#cleanupPage(pageProxyId);
    });
    handler.on("Cleanup", ({
      keepLoadedFonts
    }) => {
      this.#commonObjs.clear();
      if (!keepLoadedFonts) {
        this.#fontLoader.clear();
      }
      this.#filterFactory?.destroy(true);
    });
    handler.on("CleanupRenderTask", ({
      renderTaskId
    }) => {
      this.#cleanupRenderTask(renderTaskId);
    });
    handler.on("AppendOperatorList", ({
      pageProxyId,
      operatorListId,
      fnArray,
      argsArray
    }) => {
      const operatorList = this.#getOperatorList(pageProxyId, operatorListId);
      for (let i = 0, ii = fnArray.length; i < ii; i++) {
        operatorList.fnArray.push(fnArray[i]);
        operatorList.argsArray.push(argsArray[i]);
      }
    });
    handler.on("InitializeGraphics", async data => {
      const {
        width,
        height,
        pageProxyId,
        operatorListId,
        renderTaskId,
        hasAnnotationCanvasMap = false,
        transform,
        viewport,
        transparency,
        background,
        pixelRatio,
        recordOperations = false,
        recordImages = false,
        partialFrames = false
      } = data;
      const canvas = new OffscreenCanvas(width, height);
      const sharedOperatorList = this.#getOperatorList(pageProxyId, operatorListId);
      const renderTaskState = {
        pageProxyId,
        renderTaskId,
        canvas,
        partialFrames,
        lastFrameTime: Date.now(),
        gfx: null,
        sharedOperatorList,
        dependencyTracker: null,
        imagesTracker: null,
        operatorList: {
          fnArray: [],
          argsArray: [],
          lastChunk: false,
          pathCache: sharedOperatorList.pathCache
        },
        operatorListIdx: 0,
        lastFrameIdx: 0,
        operationsFilterMask: null,
        continueResolve: null,
        aborted: false
      };
      this.#renderTaskStates.set(renderTaskId, renderTaskState);
      try {
        const objs = this.#getPageObjs(pageProxyId);
        const optionalContentConfig = OptionalContentConfig.fromSerializable(data.optionalContentConfig);
        const canvasFactory = this.#canvasFactory ??= new OffscreenCanvasFactory({
          enableHWA: this.#enableHWA
        });
        const filterFactory = this.#filterFactory ??= new WorkerFilterFactory();
        const annotationCanvases = hasAnnotationCanvasMap ? new Map() : null;
        const {
          dependencyTracker,
          imagesTracker
        } = createCanvasTrackers(canvas, 0, {
          recordOperations,
          recordImages
        });
        const gfx = new CanvasGraphics(canvasFactory.getNoAlphaContext(canvas), this.#commonObjs, objs, canvasFactory, filterFactory, {
          optionalContentConfig,
          pixelRatio
        }, annotationCanvases, null, dependencyTracker, imagesTracker);
        gfx.beginDrawing({
          transform,
          viewport,
          transparency,
          background
        });
        renderTaskState.gfx = gfx;
        renderTaskState.dependencyTracker = dependencyTracker;
        renderTaskState.imagesTracker = imagesTracker;
      } catch (ex) {
        this.#cleanupRenderTask(renderTaskId);
        throw ex;
      }
    });
    handler.on("ExecuteOperatorList", async data => {
      const {
        renderTaskId,
        operatorListIdx,
        operatorListLength,
        operationsFilterMask,
        lastChunk
      } = data;
      const renderTaskState = this.#renderTaskStates.get(renderTaskId);
      if (!renderTaskState) {
        return {
          operatorListIdx,
          aborted: true
        };
      }
      if (this.#hasMeshPattern && this.#enableWebGPU && !isGPUReady()) {
        await initGPU();
        if (renderTaskState.aborted) {
          return {
            operatorListIdx,
            aborted: true
          };
        }
      }
      renderTaskState.operatorListIdx = operatorListIdx;
      this.#appendFromSharedOperatorList(renderTaskState, operatorListLength, operationsFilterMask, lastChunk);
      const currentOperatorListIdx = await this.#executeOperatorList(handler, renderTaskState);
      let recordedBBoxesBuffer = null;
      let imageCoordinates = null;
      if (renderTaskState.operatorList.lastChunk && currentOperatorListIdx === renderTaskState.operatorList.argsArray.length) {
        recordedBBoxesBuffer = renderTaskState.dependencyTracker?.take().buffer ?? null;
        imageCoordinates = renderTaskState.imagesTracker?.take() ?? null;
        const aborted = renderTaskState.aborted;
        this.#cleanupRenderTask(renderTaskId, !aborted);
        if (!aborted) {
          try {
            await this.#sendFrame(handler, renderTaskState, true);
          } finally {
            this.#releaseCanvases(renderTaskState);
          }
        }
      } else {
        await this.#maybeSendInterimFrame(handler, renderTaskState);
      }
      return {
        operatorListIdx: currentOperatorListIdx,
        recordedBBoxesBuffer,
        imageCoordinates
      };
    });
  }
  static #initializeFromPort(port) {
    const handler = new MessageHandler("renderer", "main", port);
    this.#setup(handler);
    handler.on("Connect", ({
      sourceName,
      targetName
    }) => {
      this.#disconnect();
      const connection = new MessageHandler(sourceName, targetName, port);
      this.#connection = connection;
      this.#setup(connection);
      connection.on("Disconnect", () => {
        this.#disconnect();
      });
      this.#sendReady(connection);
    });
    this.#sendReady(handler);
  }
  static #disconnect() {
    if (!this.#connection) {
      return;
    }
    for (const renderTaskId of this.#renderTaskStates.keys()) {
      this.#cleanupRenderTask(renderTaskId);
    }
    for (const objs of this.#objsMap.values()) {
      objs.clear();
    }
    this.#objsMap.clear();
    this.#operatorLists.clear();
    this.#hasMeshPattern = false;
    this.#commonObjs.clear();
    this.#fontLoader.clear();
    this.#commonObjs = new PDFObjects();
    this.#fontLoader = new FontLoader({
      ownerDocument: globalThis
    });
    this.#canvasFactory = null;
    this.#filterFactory?.destroy();
    this.#filterFactory = null;
    this.#connection.send("Disconnected", null);
    this.#connection.destroy();
    this.#connection = null;
  }
  static #sendReady(handler) {
    const workerVersion = "6.5.472";
    const testObj = new Uint8Array();
    handler.send("ready", {
      testObj,
      workerVersion
    }, [testObj.buffer]);
  }
}

;// ./src/pdf.renderer.js

