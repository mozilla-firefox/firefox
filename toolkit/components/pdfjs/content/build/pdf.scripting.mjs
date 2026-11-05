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
/******/ (() => { // webpackBootstrap
/******/ 	"use strict";

;// ./src/scripting_api/app_utils.js
const VIEWER_TYPE = "PDF.js";
const VIEWER_VARIATION = "Full";
const VIEWER_VERSION = 21.00720099;
const FORMS_VERSION = 21.00720099;
const USERACTIVATION_CALLBACKID = 0;
const USERACTIVATION_MAXTIME_VALIDITY = 5000;
const {
  apply
} = Reflect;
function uncurryThis(fn) {
  return (thisArg, ...args) => apply(fn, thisArg, args);
}
function serializeError(error, prefix = "") {
  let message;
  try {
    message = String(error);
  } catch {
    message = "Unknown error";
  }
  let stack;
  try {
    stack = error?.stack;
  } catch {}
  const value = typeof stack === "string" ? `${message}\n${stack}` : message;
  return {
    command: "error",
    value: `${prefix}${value}`
  };
}
const makeArr = () => [];
const makeMap = () => new Map();
const publicPrototypes = new Map();
const publicFunctions = new WeakSet();
function addPublicFunctions(...fns) {
  for (const fn of fns) {
    if (typeof fn === "function") {
      publicFunctions.add(fn);
    }
  }
}
function exposeObject(object, bindMethods = false) {
  const prototype = Object.getPrototypeOf(object);
  let publicPrototype = !bindMethods && publicPrototypes.get(prototype);
  if (!publicPrototype) {
    const descriptors = Object.getOwnPropertyDescriptors(prototype);
    delete descriptors.constructor;
    for (const descriptor of Object.values(descriptors)) {
      const {
        value,
        get,
        set
      } = descriptor;
      addPublicFunctions(value, get, set);
      if (bindMethods && typeof value === "function") {
        descriptor.value = value.bind(object);
      }
    }
    publicPrototype = Object.create(Object.prototype, descriptors);
    if (!bindMethods) {
      publicPrototypes.set(prototype, publicPrototype);
    }
  }
  return Object.setPrototypeOf(object, publicPrototype);
}
function useNativeToStringForPublicFunctions() {
  const nativeToString = Function.prototype.toString;
  const isPublic = uncurryThis(WeakSet.prototype.has);
  const [prefix, suffix] = apply(nativeToString, nativeToString, []).split("toString");
  const {
    toString
  } = {
    toString() {
      return isPublic(publicFunctions, this) ? `${prefix}${this.name}${suffix}` : apply(nativeToString, this, []);
    }
  };
  publicFunctions.add(toString);
  Object.defineProperty(Function.prototype, "toString", {
    value: toString,
    writable: true,
    configurable: true
  });
}

;// ./src/scripting_api/constants.js
const Border = Object.freeze({
  s: "solid",
  d: "dashed",
  b: "beveled",
  i: "inset",
  u: "underline"
});
const Cursor = Object.freeze({
  visible: 0,
  hidden: 1,
  delay: 2
});
const Display = Object.freeze({
  visible: 0,
  hidden: 1,
  noPrint: 2,
  noView: 3
});
const Font = Object.freeze({
  Times: "Times-Roman",
  TimesB: "Times-Bold",
  TimesI: "Times-Italic",
  TimesBI: "Times-BoldItalic",
  Helv: "Helvetica",
  HelvB: "Helvetica-Bold",
  HelvI: "Helvetica-Oblique",
  HelvBI: "Helvetica-BoldOblique",
  Cour: "Courier",
  CourB: "Courier-Bold",
  CourI: "Courier-Oblique",
  CourBI: "Courier-BoldOblique",
  Symbol: "Symbol",
  ZapfD: "ZapfDingbats",
  KaGo: "HeiseiKakuGo-W5-UniJIS-UCS2-H",
  KaMi: "HeiseiMin-W3-UniJIS-UCS2-H"
});
const Highlight = Object.freeze({
  n: "none",
  i: "invert",
  p: "push",
  o: "outline"
});
const Position = Object.freeze({
  textOnly: 0,
  iconOnly: 1,
  iconTextV: 2,
  textIconV: 3,
  iconTextH: 4,
  textIconH: 5,
  overlay: 6
});
const ScaleHow = Object.freeze({
  proportional: 0,
  anamorphic: 1
});
const ScaleWhen = Object.freeze({
  always: 0,
  never: 1,
  tooBig: 2,
  tooSmall: 3
});
const Style = Object.freeze({
  ch: "check",
  cr: "cross",
  di: "diamond",
  ci: "circle",
  st: "star",
  sq: "square"
});
const Trans = Object.freeze({
  blindsH: "BlindsHorizontal",
  blindsV: "BlindsVertical",
  boxI: "BoxIn",
  boxO: "BoxOut",
  dissolve: "Dissolve",
  glitterD: "GlitterDown",
  glitterR: "GlitterRight",
  glitterRD: "GlitterRightDown",
  random: "Random",
  replace: "Replace",
  splitHI: "SplitHorizontalIn",
  splitHO: "SplitHorizontalOut",
  splitVI: "SplitVerticalIn",
  splitVO: "SplitVerticalOut",
  wipeD: "WipeDown",
  wipeL: "WipeLeft",
  wipeR: "WipeRight",
  wipeU: "WipeUp"
});
const ZoomType = Object.freeze({
  none: "NoVary",
  fitP: "FitPage",
  fitW: "FitWidth",
  fitH: "FitHeight",
  fitV: "FitVisibleWidth",
  pref: "Preferred",
  refW: "ReflowWidth"
});
const GlobalConstants = Object.freeze({
  IDS_GREATER_THAN: "Invalid value: must be greater than or equal to % s.",
  IDS_GT_AND_LT: "Invalid value: must be greater than or equal to % s " + "and less than or equal to % s.",
  IDS_LESS_THAN: "Invalid value: must be less than or equal to % s.",
  IDS_INVALID_MONTH: "** Invalid **",
  IDS_INVALID_DATE: "Invalid date / time: please ensure that the date / time exists. Field",
  IDS_INVALID_DATE2: " should match format ",
  IDS_INVALID_VALUE: "The value entered does not match the format of the field",
  IDS_AM: "am",
  IDS_PM: "pm",
  IDS_MONTH_INFO: "January[1] February[2] March[3] April[4] May[5] " + "June[6] July[7] August[8] September[9] October[10] " + "November[11] December[12] Sept[9] Jan[1] Feb[2] Mar[3] " + "Apr[4] Jun[6] Jul[7] Aug[8] Sep[9] Oct[10] Nov[11] Dec[12]",
  IDS_STARTUP_CONSOLE_MSG: "** ^ _ ^ **",
  RE_NUMBER_ENTRY_DOT_SEP: ["[+-]?\\d*\\.?\\d*"],
  RE_NUMBER_COMMIT_DOT_SEP: ["[+-]?\\d+(\\.\\d+)?", "[+-]?\\.\\d+", "[+-]?\\d+\\."],
  RE_NUMBER_ENTRY_COMMA_SEP: ["[+-]?\\d*,?\\d*"],
  RE_NUMBER_COMMIT_COMMA_SEP: ["[+-]?\\d+([.,]\\d+)?", "[+-]?[.,]\\d+", "[+-]?\\d+[.,]"],
  RE_ZIP_ENTRY: ["\\d{0,5}"],
  RE_ZIP_COMMIT: ["\\d{5}"],
  RE_ZIP4_ENTRY: ["\\d{0,5}(\\.|[- ])?\\d{0,4}"],
  RE_ZIP4_COMMIT: ["\\d{5}(\\.|[- ])?\\d{4}"],
  RE_PHONE_ENTRY: ["\\d{0,3}(\\.|[- ])?\\d{0,3}(\\.|[- ])?\\d{0,4}", "\\(\\d{0,3}", "\\(\\d{0,3}\\)(\\.|[- ])?\\d{0,3}(\\.|[- ])?\\d{0,4}", "\\(\\d{0,3}(\\.|[- ])?\\d{0,3}(\\.|[- ])?\\d{0,4}", "\\d{0,3}\\)(\\.|[- ])?\\d{0,3}(\\.|[- ])?\\d{0,4}", "011(\\.|[- \\d])*"],
  RE_PHONE_COMMIT: ["\\d{3}(\\.|[- ])?\\d{4}", "\\d{3}(\\.|[- ])?\\d{3}(\\.|[- ])?\\d{4}", "\\(\\d{3}\\)(\\.|[- ])?\\d{3}(\\.|[- ])?\\d{4}", "011(\\.|[- \\d])*"],
  RE_SSN_ENTRY: ["\\d{0,3}(\\.|[- ])?\\d{0,2}(\\.|[- ])?\\d{0,4}"],
  RE_SSN_COMMIT: ["\\d{3}(\\.|[- ])?\\d{2}(\\.|[- ])?\\d{4}"]
});

;// ./src/scripting_api/fullscreen.js

class FullScreen {
  #backgroundColor = [];
  #clickAdvances = true;
  #cursor = Cursor.hidden;
  #defaultTransition = "";
  #escapeExits = true;
  #isFullScreen = true;
  #loop = false;
  #timeDelay = 3600;
  #usePageTiming = false;
  #useTimer = false;
  get backgroundColor() {
    return this.#backgroundColor;
  }
  set backgroundColor(_) {}
  get clickAdvances() {
    return this.#clickAdvances;
  }
  set clickAdvances(_) {}
  get cursor() {
    return this.#cursor;
  }
  set cursor(_) {}
  get defaultTransition() {
    return this.#defaultTransition;
  }
  set defaultTransition(_) {}
  get escapeExits() {
    return this.#escapeExits;
  }
  set escapeExits(_) {}
  get isFullScreen() {
    return this.#isFullScreen;
  }
  set isFullScreen(_) {}
  get loop() {
    return this.#loop;
  }
  set loop(_) {}
  get timeDelay() {
    return this.#timeDelay;
  }
  set timeDelay(_) {}
  get transitions() {
    return ["Replace", "WipeRight", "WipeLeft", "WipeDown", "WipeUp", "SplitHorizontalIn", "SplitHorizontalOut", "SplitVerticalIn", "SplitVerticalOut", "BlindsHorizontal", "BlindsVertical", "BoxIn", "BoxOut", "GlitterRight", "GlitterDown", "GlitterRightDown", "Dissolve", "Random"];
  }
  set transitions(_) {
    throw new Error("fullscreen.transitions is read-only");
  }
  get usePageTiming() {
    return this.#usePageTiming;
  }
  set usePageTiming(_) {}
  get useTimer() {
    return this.#useTimer;
  }
  set useTimer(_) {}
}

;// ./src/shared/math_clamp.js
function MathClamp(v, min, max) {
  return Math.min(Math.max(v, min), max);
}

;// ./src/shared/scripting_utils.js

function makeColorComp(n) {
  return Math.floor(MathClamp(n, 0, 1) * 255).toString(16).padStart(2, "0");
}
function scaleAndClamp(x) {
  return MathClamp(x, 0, 1) * 255;
}
class ColorConverters {
  static CMYK_G([c, y, m, k]) {
    return ["G", 1 - Math.min(1, 0.3 * c + 0.59 * m + 0.11 * y + k)];
  }
  static G_CMYK([g]) {
    return ["CMYK", 0, 0, 0, 1 - g];
  }
  static G_RGB([g]) {
    return ["RGB", g, g, g];
  }
  static G_rgb([g]) {
    g = scaleAndClamp(g);
    return [g, g, g];
  }
  static G_HTML([g]) {
    const G = makeColorComp(g);
    return `#${G}${G}${G}`;
  }
  static RGB_G([r, g, b]) {
    return ["G", 0.3 * r + 0.59 * g + 0.11 * b];
  }
  static RGB_rgb(color) {
    return color.map(scaleAndClamp);
  }
  static RGB_HTML(color) {
    return `#${color.map(makeColorComp).join("")}`;
  }
  static T_HTML() {
    return "#00000000";
  }
  static T_rgb() {
    return [null];
  }
  static CMYK_RGB([c, y, m, k]) {
    return ["RGB", 1 - Math.min(1, c + k), 1 - Math.min(1, m + k), 1 - Math.min(1, y + k)];
  }
  static CMYK_rgb([c, y, m, k]) {
    return [scaleAndClamp(1 - Math.min(1, c + k)), scaleAndClamp(1 - Math.min(1, m + k)), scaleAndClamp(1 - Math.min(1, y + k))];
  }
  static CMYK_HTML(components) {
    const rgb = this.CMYK_RGB(components).slice(1);
    return this.RGB_HTML(rgb);
  }
  static RGB_CMYK([r, g, b]) {
    const c = 1 - r;
    const m = 1 - g;
    const y = 1 - b;
    const k = Math.min(c, m, y);
    return ["CMYK", c, m, y, k];
  }
}
const DateFormats = ["m/d", "m/d/yy", "mm/dd/yy", "mm/yy", "d-mmm", "d-mmm-yy", "dd-mmm-yy", "yy-mm-dd", "mmm-yy", "mmmm-yy", "mmm d, yyyy", "mmmm d, yyyy", "m/d/yy h:MM tt", "m/d/yy HH:MM"];
const TimeFormats = ["HH:MM", "h:MM tt", "HH:MM:ss", "h:MM:ss tt"];

;// ./src/scripting_api/color.js

function isValidSpace(cColorSpace) {
  return typeof cColorSpace === "string" && (cColorSpace === "T" || cColorSpace === "G" || cColorSpace === "RGB" || cColorSpace === "CMYK");
}
function isValidColor(colorArray) {
  if (!Array.isArray(colorArray) || colorArray.length === 0) {
    return false;
  }
  const space = colorArray[0];
  if (!isValidSpace(space)) {
    return false;
  }
  const expectedLength = {
    T: 1,
    G: 2,
    RGB: 4,
    CMYK: 5
  }[space];
  return colorArray.length === expectedLength && colorArray.slice(1).every(c => typeof c === "number" && c >= 0 && c <= 1);
}
function getCorrectColor(colorArray) {
  return isValidColor(colorArray) ? colorArray : ["G", 0];
}
class Color {
  transparent = ["T"];
  black = ["G", 0];
  white = ["G", 1];
  red = ["RGB", 1, 0, 0];
  green = ["RGB", 0, 1, 0];
  blue = ["RGB", 0, 0, 1];
  cyan = ["CMYK", 1, 0, 0, 0];
  magenta = ["CMYK", 0, 1, 0, 0];
  yellow = ["CMYK", 0, 0, 1, 0];
  dkGray = ["G", 0.25];
  gray = ["G", 0.5];
  ltGray = ["G", 0.75];
  convert(colorArray, cColorSpace) {
    if (!isValidSpace(cColorSpace)) {
      return this.black;
    }
    if (cColorSpace === "T") {
      return ["T"];
    }
    colorArray = getCorrectColor(colorArray);
    if (colorArray[0] === cColorSpace) {
      return colorArray;
    }
    if (colorArray[0] === "T") {
      return this.convert(this.black, cColorSpace);
    }
    return ColorConverters[`${colorArray[0]}_${cColorSpace}`](colorArray.slice(1));
  }
  equal(colorArray1, colorArray2) {
    colorArray1 = getCorrectColor(colorArray1);
    colorArray2 = getCorrectColor(colorArray2);
    if (colorArray1[0] === "T" || colorArray2[0] === "T") {
      return colorArray1[0] === "T" && colorArray2[0] === "T";
    }
    if (colorArray1[0] !== colorArray2[0]) {
      colorArray2 = this.convert(colorArray2, colorArray1[0]);
    }
    return colorArray1.slice(1).every((c, i) => c === colorArray2[i + 1]);
  }
}

;// ./src/scripting_api/thermometer.js
class Thermometer {
  #cancelled = false;
  #duration = 100;
  #text = "";
  #value = 0;
  get cancelled() {
    return this.#cancelled;
  }
  set cancelled(_) {
    throw new Error("thermometer.cancelled is read-only");
  }
  get duration() {
    return this.#duration;
  }
  set duration(val) {
    this.#duration = val;
  }
  get text() {
    return this.#text;
  }
  set text(val) {
    this.#text = val;
  }
  get value() {
    return this.#value;
  }
  set value(val) {
    this.#value = val;
  }
  begin() {}
  end() {}
}

;// ./src/scripting_api/app.js




const mapDelete = uncurryThis(Map.prototype.delete);
const mapGet = uncurryThis(Map.prototype.get);
const mapSet = uncurryThis(Map.prototype.set);
const weakMapDelete = uncurryThis(WeakMap.prototype.delete);
const weakMapGet = uncurryThis(WeakMap.prototype.get);
const weakMapSet = uncurryThis(WeakMap.prototype.set);
const registerFinalizer = uncurryThis(FinalizationRegistry.prototype.register);
const unregisterFinalizer = uncurryThis(FinalizationRegistry.prototype.unregister);
const AppInternals = Object.create(null);
class App {
  #constants = null;
  #focusRect = true;
  #fs = exposeObject(new FullScreen(), true);
  #language;
  #openInPlace = false;
  #platform;
  #runtimeHighlight = false;
  #runtimeHighlightColor = ["T"];
  #thermometer = exposeObject(new Thermometer(), true);
  #toolbar = false;
  #doc;
  #timeoutIds = new WeakMap();
  #timeoutIdsRegistry = new FinalizationRegistry(this.#cleanTimeout.bind(this));
  #timeoutCallbackIds = new Map();
  #timeoutCallbackId = (/* inlined export .USERACTIVATION_CALLBACKID */0) + 1;
  #globalEval;
  #externalCall;
  #send;
  #userActivationData;
  constructor(data) {
    this.#send = data.send;
    this.#userActivationData = data.userActivationData;
    this.#language = App.#getLanguage(data.language);
    this.#platform = App.#getPlatform(data.platform);
    this.#doc = data.doc;
    this.#globalEval = data.globalEval;
    this.#externalCall = data.externalCall;
  }
  static {
    Object.assign(AppInternals, {
      evalCallback: (app, data) => app.#evalCallback(data)
    });
  }
  #registerTimeoutCallback(cExpr) {
    const id = this.#timeoutCallbackId++;
    mapSet(this.#timeoutCallbackIds, id, cExpr);
    return id;
  }
  #unregisterTimeoutCallback(id) {
    mapDelete(this.#timeoutCallbackIds, id);
  }
  #evalCallback({
    callbackId,
    interval
  }) {
    if (callbackId === (/* inlined export .USERACTIVATION_CALLBACKID */0)) {
      this.#userActivationData.userActivation = false;
      return;
    }
    const expr = mapGet(this.#timeoutCallbackIds, callbackId);
    if (!interval) {
      this.#unregisterTimeoutCallback(callbackId);
    }
    if (expr) {
      const saveUserActivation = this.#userActivationData.userActivation;
      this.#userActivationData.userActivation = false;
      this.#globalEval(expr);
      this.#userActivationData.userActivation = saveUserActivation;
    }
  }
  #registerTimeout(callbackId, interval) {
    const timeout = Object.create(null);
    const id = {
      callbackId,
      interval
    };
    weakMapSet(this.#timeoutIds, timeout, id);
    registerFinalizer(this.#timeoutIdsRegistry, timeout, id, timeout);
    return timeout;
  }
  #unregisterTimeout(timeout) {
    const data = weakMapGet(this.#timeoutIds, timeout);
    if (!data) {
      return;
    }
    weakMapDelete(this.#timeoutIds, timeout);
    unregisterFinalizer(this.#timeoutIdsRegistry, timeout);
    this.#cleanTimeout(data);
  }
  #cleanTimeout({
    callbackId,
    interval
  }) {
    this.#unregisterTimeoutCallback(callbackId);
    if (interval) {
      this.#externalCall("clearInterval", [callbackId]);
    } else {
      this.#externalCall("clearTimeout", [callbackId]);
    }
  }
  static #getPlatform(platform) {
    if (typeof platform === "string") {
      platform = platform.toLowerCase();
      if (platform.includes("win")) {
        return "WIN";
      } else if (platform.includes("mac")) {
        return "MAC";
      }
    }
    return "UNIX";
  }
  static #getLanguage(language) {
    const [main, sub] = language.toLowerCase().split(/[-_]/, 2);
    switch (main) {
      case "zh":
        return sub === "cn" || sub === "sg" ? "CHS" : "CHT";
      case "da":
        return "DAN";
      case "de":
        return "DEU";
      case "es":
        return "ESP";
      case "fr":
        return "FRA";
      case "it":
        return "ITA";
      case "ko":
        return "KOR";
      case "ja":
        return "JPN";
      case "nl":
        return "NLD";
      case "no":
        return "NOR";
      case "pt":
        return sub === "br" ? "PTB" : "ENU";
      case "fi":
        return "SUO";
      case "SV":
        return "SVE";
      default:
        return "ENU";
    }
  }
  get activeDocs() {
    return [globalThis];
  }
  set activeDocs(_) {
    throw new Error("app.activeDocs is read-only");
  }
  get calculate() {
    return this.#doc.calculate;
  }
  set calculate(calculate) {
    this.#doc.calculate = calculate;
  }
  get constants() {
    return this.#constants ??= Object.freeze({
      align: Object.freeze({
        left: 0,
        center: 1,
        right: 2,
        top: 3,
        bottom: 4
      })
    });
  }
  set constants(_) {
    throw new Error("app.constants is read-only");
  }
  get focusRect() {
    return this.#focusRect;
  }
  set focusRect(val) {
    this.#focusRect = val;
  }
  get formsVersion() {
    return FORMS_VERSION;
  }
  set formsVersion(_) {
    throw new Error("app.formsVersion is read-only");
  }
  get fromPDFConverters() {
    return [];
  }
  set fromPDFConverters(_) {
    throw new Error("app.fromPDFConverters is read-only");
  }
  get fs() {
    return this.#fs;
  }
  set fs(_) {
    throw new Error("app.fs is read-only");
  }
  get language() {
    return this.#language;
  }
  set language(_) {
    throw new Error("app.language is read-only");
  }
  get media() {
    return undefined;
  }
  set media(_) {
    throw new Error("app.media is read-only");
  }
  get monitors() {
    return [];
  }
  set monitors(_) {
    throw new Error("app.monitors is read-only");
  }
  get numPlugins() {
    return 0;
  }
  set numPlugins(_) {
    throw new Error("app.numPlugins is read-only");
  }
  get openInPlace() {
    return this.#openInPlace;
  }
  set openInPlace(val) {
    this.#openInPlace = val;
  }
  get platform() {
    return this.#platform;
  }
  set platform(_) {
    throw new Error("app.platform is read-only");
  }
  get plugins() {
    return [];
  }
  set plugins(_) {
    throw new Error("app.plugins is read-only");
  }
  get printColorProfiles() {
    return [];
  }
  set printColorProfiles(_) {
    throw new Error("app.printColorProfiles is read-only");
  }
  get printerNames() {
    return [];
  }
  set printerNames(_) {
    throw new Error("app.printerNames is read-only");
  }
  get runtimeHighlight() {
    return this.#runtimeHighlight;
  }
  set runtimeHighlight(val) {
    this.#runtimeHighlight = val;
  }
  get runtimeHighlightColor() {
    return this.#runtimeHighlightColor;
  }
  set runtimeHighlightColor(val) {
    if (isValidColor(val)) {
      this.#runtimeHighlightColor = val;
    }
  }
  get thermometer() {
    return this.#thermometer;
  }
  set thermometer(_) {
    throw new Error("app.thermometer is read-only");
  }
  get toolbar() {
    return this.#toolbar;
  }
  set toolbar(val) {
    this.#toolbar = val;
  }
  get toolbarHorizontal() {
    return this.toolbar;
  }
  set toolbarHorizontal(value) {
    this.toolbar = value;
  }
  get toolbarVertical() {
    return this.toolbar;
  }
  set toolbarVertical(value) {
    this.toolbar = value;
  }
  get viewerType() {
    return (/* inlined export .VIEWER_TYPE */"PDF.js");
  }
  set viewerType(_) {
    throw new Error("app.viewerType is read-only");
  }
  get viewerVariation() {
    return (/* inlined export .VIEWER_VARIATION */"Full");
  }
  set viewerVariation(_) {
    throw new Error("app.viewerVariation is read-only");
  }
  get viewerVersion() {
    return VIEWER_VERSION;
  }
  set viewerVersion(_) {
    throw new Error("app.viewerVersion is read-only");
  }
  addMenuItem() {}
  addSubMenu() {}
  addToolButton() {}
  alert(cMsg, nIcon = 0, nType = 0, cTitle = "PDF.js", oDoc = null, oCheckbox = null) {
    if (!this.#userActivationData.userActivation) {
      return 0;
    }
    this.#userActivationData.userActivation = false;
    if (cMsg && typeof cMsg === "object") {
      nType = cMsg.nType;
      cMsg = cMsg.cMsg;
    }
    cMsg = (cMsg || "").toString();
    if (!cMsg) {
      return 0;
    }
    nType = typeof nType !== "number" || isNaN(nType) || nType < 0 || nType > 3 ? 0 : nType;
    if (nType >= 2) {
      return this.#externalCall("confirm", [cMsg]) ? 4 : 3;
    }
    this.#externalCall("alert", [cMsg]);
    return 1;
  }
  beep() {}
  beginPriv() {}
  browseForDoc() {}
  clearInterval(oInterval) {
    this.#unregisterTimeout(oInterval);
  }
  clearTimeOut(oTime) {
    this.#unregisterTimeout(oTime);
  }
  endPriv() {}
  execDialog() {}
  execMenuItem(item) {
    if (!this.#userActivationData.userActivation) {
      return;
    }
    this.#userActivationData.userActivation = false;
    switch (item) {
      case "SaveAs":
        if (this.#userActivationData.disableSaving) {
          return;
        }
        this.#send({
          command: item
        });
        break;
      case "FirstPage":
      case "LastPage":
      case "NextPage":
      case "PrevPage":
      case "ZoomViewIn":
      case "ZoomViewOut":
        this.#send({
          command: item
        });
        break;
      case "FitPage":
        this.#send({
          command: "zoom",
          value: "page-fit"
        });
        break;
      case "Print":
        if (this.#userActivationData.disablePrinting) {
          return;
        }
        this.#send({
          command: "print"
        });
        break;
    }
  }
  getNthPlugInName() {}
  getPath() {}
  goBack() {}
  goForward() {}
  hideMenuItem() {}
  hideToolbarButton() {}
  launchURL() {}
  listMenuItems() {}
  listToolbarButtons() {}
  loadPolicyFile() {}
  mailGetAddrs() {}
  mailMsg() {}
  newDoc() {}
  newCollection() {}
  newFDF() {}
  openDoc() {}
  openFDF() {}
  popUpMenu() {}
  popUpMenuEx() {}
  removeToolButton() {}
  response(cQuestion, cTitle = "", cDefault = "", bPassword = "", cLabel = "") {
    if (!this.#userActivationData.userActivation) {
      return null;
    }
    this.#userActivationData.userActivation = false;
    if (cQuestion && typeof cQuestion === "object") {
      cDefault = cQuestion.cDefault;
      cQuestion = cQuestion.cQuestion;
    }
    cQuestion = (cQuestion || "").toString();
    cDefault = (cDefault || "").toString();
    return this.#externalCall("prompt", [cQuestion, cDefault || ""]);
  }
  setInterval(cExpr, nMilliseconds = 0) {
    if (cExpr && typeof cExpr === "object") {
      nMilliseconds = cExpr.nMilliseconds || 0;
      cExpr = cExpr.cExpr;
    }
    if (typeof cExpr !== "string") {
      throw new TypeError("First argument of app.setInterval must be a string");
    }
    if (typeof nMilliseconds !== "number") {
      throw new TypeError("Second argument of app.setInterval must be a number");
    }
    const callbackId = this.#registerTimeoutCallback(cExpr);
    this.#externalCall("setInterval", [callbackId, nMilliseconds]);
    return this.#registerTimeout(callbackId, true);
  }
  setTimeOut(cExpr, nMilliseconds = 0) {
    if (cExpr && typeof cExpr === "object") {
      nMilliseconds = cExpr.nMilliseconds || 0;
      cExpr = cExpr.cExpr;
    }
    if (typeof cExpr !== "string") {
      throw new TypeError("First argument of app.setTimeOut must be a string");
    }
    if (typeof nMilliseconds !== "number") {
      throw new TypeError("Second argument of app.setTimeOut must be a number");
    }
    const callbackId = this.#registerTimeoutCallback(cExpr);
    this.#externalCall("setTimeout", [callbackId, nMilliseconds]);
    return this.#registerTimeout(callbackId, false);
  }
  trustedFunction() {}
  trustPropagatorFunction() {}
}

;// ./src/scripting_api/common.js
const FieldType = {
  none: 0,
  number: 1,
  percent: 2,
  date: 3,
  time: 4
};
function createMap(val) {
  return val instanceof Map ? val : new Map(val ? Object.entries(val) : null);
}
function getFieldType(actions) {
  let format = actions.get("Format");
  if (!format) {
    return FieldType.none;
  }
  format = format[0].trim();
  if (format.startsWith("AFNumber_")) {
    return FieldType.number;
  }
  if (format.startsWith("AFPercent_")) {
    return FieldType.percent;
  }
  if (format.startsWith("AFDate_")) {
    return FieldType.date;
  }
  return format.startsWith("AFTime_") ? FieldType.time : FieldType.none;
}

;// ./src/scripting_api/field.js



const FieldInternals = Object.create(null);
let containerDescriptors = null;
const VIEW_PROPERTIES = ["display", "editable", "hidden", "multipleSelection", "print", "readonly", "required", "userName"];
class Field {
  #actions;
  #appObjects;
  #browseForFileToSubmit;
  #buttonCaption = null;
  #buttonIcon = null;
  #charLimit;
  #children = null;
  #containerProps = null;
  #currentValueIndices;
  #datetimeFormat;
  #fieldPath;
  #fieldType;
  #fillColor;
  #getTerminalChildren;
  #globalEval;
  #hasDateOrTime;
  #hasValue;
  #id;
  #isCheckbox = false;
  #isChoice;
  #items;
  #kidIds;
  #originalValue;
  #otherWidgetIds = null;
  #page;
  #radioActions = null;
  #radioIds = null;
  #radioPages = null;
  #rotation;
  #send;
  #sendError;
  #strokeColor;
  #textColor;
  #util;
  #value = null;
  #viewProps;
  constructor(data, otherWidgets = []) {
    this.alignment = data.alignment || "left";
    this.borderStyle = data.borderStyle || "";
    this.buttonAlignX = data.buttonAlignX || 50;
    this.buttonAlignY = data.buttonAlignY || 50;
    this.buttonFitBounds = data.buttonFitBounds;
    this.buttonPosition = data.buttonPosition;
    this.buttonScaleHow = data.buttonScaleHow;
    this.ButtonScaleWhen = data.buttonScaleWhen;
    this.calcOrderIndex = data.calcOrderIndex;
    this.comb = data.comb;
    this.commitOnSelChange = data.commitOnSelChange;
    this.defaultStyle = data.defaultStyle;
    this.defaultValue = data.defaultValue;
    this.doNotScroll = data.doNotScroll;
    this.doNotSpellCheck = data.doNotSpellCheck;
    this.delay = data.delay;
    this.doc = globalThis;
    this.exportValues = data.exportValues;
    this.fileSelect = data.fileSelect;
    this.highlight = data.highlight;
    this.lineWidth = data.lineWidth;
    this.multiline = data.multiline;
    this.name = data.name;
    this.password = data.password;
    this.radiosInUnison = data.radiosInUnison;
    this.rect = data.rect;
    this.richText = data.richText;
    this.richValue = data.richValue;
    this.style = data.style;
    this.submitName = data.submitName;
    this.textFont = data.textFont;
    this.textSize = data.textSize;
    this.type = data.type;
    this.#actions = createMap(data.actions);
    this.#appObjects = data.appObjects;
    this.#browseForFileToSubmit = data.browseForFileToSubmit || null;
    this.#charLimit = data.charLimit;
    this.#currentValueIndices = data.currentValueIndices || 0;
    this.#datetimeFormat = data.datetimeFormat || null;
    this.#fieldPath = data.fieldPath;
    this.#fieldType = getFieldType(this.#actions);
    this.#fillColor = data.fillColor || ["T"];
    this.#getTerminalChildren = data.getTerminalChildren;
    this.#globalEval = data.globalEval;
    this.#hasDateOrTime = !!data.hasDatetimeHTML;
    this.#hasValue = Object.hasOwn(data, "value");
    this.#id = data.id;
    this.#isChoice = Array.isArray(data.items);
    this.#items = data.items || [];
    this.#kidIds = data.kidIds || null;
    this.#page = data.page || 0;
    this.#rotation = data.rotation || 0;
    this.#send = data.send || (() => {});
    this.#sendError = data.sendError;
    this.#strokeColor = data.strokeColor || ["G", 0];
    this.#textColor = data.textColor || ["G", 0];
    this.#util = data.util;
    this.#viewProps = Object.fromEntries(VIEW_PROPERTIES.map(name => [name, data[name]]));
    this.#viewProps.multipleSelection = !!data.multipleSelection;
    if (data.type === "radiobutton" || data.type === "checkbox") {
      this.#isCheckbox = data.type === "checkbox";
      this.exportValues = [this.exportValues];
      this.#radioIds = [this.#id];
      this.#radioActions = [this.#actions];
      this.#radioPages = [this.#page];
      for (const widget of otherWidgets) {
        this.exportValues.push(widget.exportValues);
        this.#radioIds.push(widget.id);
        this.#radioActions.push(createMap(widget.actions));
        this.#radioPages.push(widget.page || 0);
        if (data.value === widget.exportValues) {
          this.#id = widget.id;
        }
      }
      this.#value = data.value || "";
    } else {
      if (otherWidgets.length > 0) {
        this.#otherWidgetIds = otherWidgets.map(widget => widget.id);
      }
      this.#setValue(data.value || "");
    }
    if (this.#kidIds) {
      this.#containerProps = {
        ...this
      };
      containerDescriptors ??= Field.#getContainerDescriptors(Object.keys(this));
      Object.defineProperties(this, containerDescriptors);
    }
  }
  static #getContainerDescriptors(names) {
    const descriptors = Object.create(null);
    for (const name of names) {
      const descriptor = {
        get() {
          return this.#containerProps[name];
        },
        set(value) {
          this.#containerProps[name] = value;
          this.#setOnKids(name, value);
        },
        enumerable: true,
        configurable: true
      };
      addPublicFunctions(descriptor.get, descriptor.set);
      descriptors[name] = descriptor;
    }
    return descriptors;
  }
  static {
    Object.assign(FieldInternals, {
      getComputedValue: f => f.#originalValue ?? f.#value,
      getHasValue: f => f.#hasValue,
      getInitialValue: f => f.#hasDateOrTime && f.#originalValue || null,
      getIsChoice: f => f.#isChoice,
      getKidIds: f => f.#kidIds,
      getPageActions: f => f.#getPageActions(),
      getValue: f => f.#value,
      setValue: (f, value) => f.#setValue(value),
      prepareEvent(f, id, event) {
        if (f.#radioIds) {
          f.#id = id;
          event.value = f.#isCheckbox && !event.value ? "Off" : f.exportValues[f.#checkedIndex];
          if (event.name === "Action") {
            f.#value = event.value;
          }
        }
      },
      runActions: (f, event) => f.#runActions(event),
      send: (f, data) => f.#sendToView(data)
    });
    for (const name of VIEW_PROPERTIES) {
      Object.defineProperty(Field.prototype, name, {
        get() {
          return this.#viewProps[name];
        },
        set(value) {
          this.#viewProps[name] = value;
          this.#update(name, value);
        },
        configurable: true
      });
    }
    for (const [name, {
      value
    }] of Object.entries(Object.getOwnPropertyDescriptors(Field.prototype))) {
      if (name !== "constructor" && typeof value === "function") {
        Object.defineProperty(Field.prototype, name, {
          writable: false
        });
      }
    }
  }
  get #siblings() {
    return this.#radioIds?.filter(id => id !== this.#id) ?? this.#otherWidgetIds;
  }
  #getPageActions() {
    const actions = this.#radioActions ?? [this.#actions];
    const pages = this.#radioPages ?? [this.#page];
    const pageActions = [];
    for (let i = 0, ii = actions.length; i < ii; i++) {
      for (const eventName of ["PageOpen", "PageClose"]) {
        const scripts = actions[i].get(eventName);
        if (scripts) {
          pageActions.push([pages[i], eventName, scripts]);
        }
      }
    }
    return pageActions;
  }
  #setOnKids(name, value) {
    if (this.#kidIds) {
      for (const id of this.#kidIds) {
        const kid = this.#appObjects[id];
        if (kid) {
          kid[name] = value;
        }
      }
    }
  }
  #sendToView(data) {
    const siblings = this.#siblings;
    this.#send(siblings ? {
      id: this.#id,
      siblings,
      ...data
    } : {
      id: this.#id,
      ...data
    });
  }
  #update(name, value, sentValue = value) {
    this.#setOnKids(name, value);
    this.#sendToView({
      [name]: sentValue
    });
  }
  get currentValueIndices() {
    return !this.#isChoice ? 0 : this.#currentValueIndices;
  }
  set currentValueIndices(indices) {
    this.#setOnKids("currentValueIndices", indices);
    if (!this.#isChoice) {
      return;
    }
    if (!Array.isArray(indices)) {
      indices = [indices];
    }
    if (!indices.every(i => Number.isInteger(i) && i >= 0 && i < this.numItems)) {
      return;
    }
    indices.sort((a, b) => a - b);
    if (this.#viewProps.multipleSelection) {
      this.#currentValueIndices = indices;
      this.#value = indices.map(i => this.#items[i].exportValue);
    } else if (indices.length > 0) {
      indices = [indices[0]];
      this.#currentValueIndices = indices[0];
      this.#value = this.#items[indices[0]].exportValue;
    }
    this.#send({
      id: this.#id,
      indices
    });
  }
  get fillColor() {
    return this.#fillColor;
  }
  set fillColor(color) {
    if (isValidColor(color)) {
      this.#fillColor = color;
      this.#update("fillColor", color);
    }
  }
  get bgColor() {
    return this.fillColor;
  }
  set bgColor(color) {
    this.fillColor = color;
  }
  get charLimit() {
    return this.#charLimit;
  }
  set charLimit(limit) {
    if (typeof limit !== "number") {
      throw new Error("Invalid argument value");
    }
    this.#charLimit = Math.max(0, Math.floor(limit));
    this.#update("charLimit", this.#charLimit);
  }
  get numItems() {
    if (!this.#isChoice) {
      throw new Error("Not a choice widget");
    }
    return this.#items.length;
  }
  set numItems(_) {
    throw new Error("field.numItems is read-only");
  }
  get strokeColor() {
    return this.#strokeColor;
  }
  set strokeColor(color) {
    if (isValidColor(color)) {
      this.#strokeColor = color;
      this.#update("strokeColor", color);
    }
  }
  get borderColor() {
    return this.strokeColor;
  }
  set borderColor(color) {
    this.strokeColor = color;
  }
  get page() {
    return this.#page;
  }
  set page(_) {
    throw new Error("field.page is read-only");
  }
  get rotation() {
    return this.#rotation;
  }
  set rotation(angle) {
    angle = Math.floor(angle);
    if (angle % 90 !== 0) {
      throw new Error("Invalid rotation: must be a multiple of 90");
    }
    angle %= 360;
    if (angle < 0) {
      angle += 360;
    }
    this.#rotation = angle;
    this.#update("rotation", angle);
  }
  get textColor() {
    return this.#textColor;
  }
  set textColor(color) {
    if (isValidColor(color)) {
      this.#textColor = color;
      this.#update("textColor", color);
    }
  }
  get fgColor() {
    return this.textColor;
  }
  set fgColor(color) {
    this.textColor = color;
  }
  get value() {
    return this.#value;
  }
  set value(value) {
    if (this.#setValue(value)) {
      this.#update("value", value, this.#originalValue ?? this.#value);
    }
  }
  #setValue(value) {
    if (this.#radioIds) {
      return this.#setRadioValue(value);
    }
    if (this.#isChoice) {
      this.#setChoiceValue(value);
      return true;
    }
    if (this.#hasDateOrTime && value) {
      const date = this.#util.scand(this.#datetimeFormat, value);
      if (date) {
        this.#originalValue = date.valueOf();
        value = this.#util.printd(this.#datetimeFormat, date);
        this.#value = !isNaN(value) ? parseFloat(value) : value;
        return true;
      }
    }
    if (value === "" || typeof value !== "string" || this.#fieldType >= FieldType.date) {
      this.#originalValue = undefined;
      this.#value = value;
      return true;
    }
    this.#originalValue = value;
    const _value = value.trim().replace(",", ".");
    this.#value = !isNaN(_value) ? parseFloat(_value) : value;
    return true;
  }
  get #checkedIndex() {
    return this.#radioIds.indexOf(this.#id);
  }
  #getWidgetIndex(nWidget) {
    if (!this.#radioIds) {
      return -1;
    }
    const index = Number(nWidget);
    return Number.isInteger(index) && index >= 0 && index < this.#radioIds.length && `${index}` === `${nWidget}` ? index : -1;
  }
  #check(index, value) {
    this.#id = this.#radioIds[index];
    this.#value = value;
  }
  #setRadioValue(value) {
    if (this.#isCheckbox && (!value || value === "Off" || !this.exportValues.includes(value))) {
      this.#value = "Off";
      return true;
    }
    if (value === null || value === undefined) {
      this.#value = "";
      return true;
    }
    const i = this.exportValues.indexOf(value);
    if (i !== -1) {
      this.#check(i, value);
      return true;
    }
    if (value === "Off" && this.#radioIds.length === 2) {
      const nextI = (1 + this.#checkedIndex) % 2;
      this.#check(nextI, this.exportValues[nextI]);
      return true;
    }
    return false;
  }
  #setChoiceValue(value) {
    if (this.#viewProps.multipleSelection) {
      if (!Array.isArray(value)) {
        value = [value];
      }
      const values = new Set(value);
      if (Array.isArray(this.#currentValueIndices)) {
        this.#currentValueIndices.length = 0;
        this.#value.length = 0;
      } else {
        this.#currentValueIndices = [];
        this.#value = [];
      }
      this.#items.forEach((item, i) => {
        if (values.has(item.exportValue)) {
          this.#currentValueIndices.push(i);
          this.#value.push(item.exportValue);
        }
      });
    } else {
      if (Array.isArray(value)) {
        value = value[0];
      }
      const index = this.#items.findIndex(({
        exportValue
      }) => value === exportValue);
      if (index !== -1) {
        this.#currentValueIndices = index;
        this.#value = this.#items[index].exportValue;
      }
    }
  }
  get valueAsString() {
    return (this.#value ?? "").toString();
  }
  set valueAsString(_) {}
  browseForFileToSubmit() {
    if (this.#browseForFileToSubmit) {
      this.#browseForFileToSubmit();
    }
  }
  buttonGetCaption(nFace = 0) {
    return this.#buttonCaption ? this.#buttonCaption[nFace] : "";
  }
  buttonGetIcon(nFace = 0) {
    return this.#buttonIcon ? this.#buttonIcon[nFace] : null;
  }
  buttonImportIcon(cPath = null, nPave = 0) {}
  buttonSetCaption(cCaption, nFace = 0) {
    this.#buttonCaption ??= ["", "", ""];
    this.#buttonCaption[nFace] = cCaption;
  }
  buttonSetIcon(oIcon, nFace = 0) {
    this.#buttonIcon ??= [null, null, null];
    this.#buttonIcon[nFace] = oIcon;
  }
  checkThisBox(nWidget, bCheckIt = true) {
    const index = this.#getWidgetIndex(nWidget);
    if (index === -1 || !bCheckIt && !this.#isCheckbox) {
      return;
    }
    const value = bCheckIt ? this.exportValues[index] : "Off";
    this.#check(index, value);
    this.#send({
      id: this.#id,
      value
    });
  }
  clearItems() {
    if (!this.#isChoice) {
      throw new Error("Not a choice widget");
    }
    this.#items = [];
    this.#send({
      id: this.#id,
      clear: null
    });
  }
  deleteItemAt(nIdx = null) {
    if (!this.#isChoice) {
      throw new Error("Not a choice widget");
    }
    if (!this.numItems) {
      return;
    }
    if (nIdx === null) {
      nIdx = Array.isArray(this.#currentValueIndices) ? this.#currentValueIndices[0] : this.#currentValueIndices;
      nIdx ||= 0;
    }
    if (nIdx < 0 || nIdx >= this.numItems) {
      nIdx = this.numItems - 1;
    }
    this.#items.splice(nIdx, 1);
    if (Array.isArray(this.#currentValueIndices)) {
      let index = this.#currentValueIndices.findIndex(i => i >= nIdx);
      if (index !== -1) {
        if (this.#currentValueIndices[index] === nIdx) {
          this.#currentValueIndices.splice(index, 1);
        }
        for (const ii = this.#currentValueIndices.length; index < ii; index++) {
          --this.#currentValueIndices[index];
        }
      }
    } else if (this.#currentValueIndices === nIdx) {
      this.#currentValueIndices = this.numItems > 0 ? 0 : -1;
    } else if (this.#currentValueIndices > nIdx) {
      --this.#currentValueIndices;
    }
    this.#send({
      id: this.#id,
      remove: nIdx
    });
  }
  getItemAt(nIdx = -1, bExportValue = false) {
    if (!this.#isChoice) {
      throw new Error("Not a choice widget");
    }
    if (nIdx < 0 || nIdx >= this.numItems) {
      nIdx = this.numItems - 1;
    }
    const item = this.#items[nIdx];
    return bExportValue ? item.exportValue : item.displayValue;
  }
  getArray() {
    if (this.#kidIds) {
      const array = [];
      const fillArrayWithKids = kidIds => {
        for (const id of kidIds) {
          const obj = this.#appObjects[id];
          if (!obj) {
            continue;
          }
          if (obj.#hasValue) {
            array.push(obj);
          }
          if (obj.#kidIds) {
            fillArrayWithKids(obj.#kidIds);
          }
        }
      };
      fillArrayWithKids(this.#kidIds);
      return array;
    }
    return this.#children ??= this.#getTerminalChildren(this.#fieldPath);
  }
  getLock() {
    return undefined;
  }
  isBoxChecked(nWidget) {
    const index = this.#getWidgetIndex(nWidget);
    return index !== -1 && this.#id === this.#radioIds[index] && this.#value === this.exportValues[index];
  }
  isDefaultChecked(nWidget) {
    const index = this.#getWidgetIndex(nWidget);
    return index !== -1 && this.defaultValue === this.exportValues[index];
  }
  insertItemAt(cName, cExport = undefined, nIdx = 0) {
    if (!this.#isChoice) {
      throw new Error("Not a choice widget");
    }
    if (!cName) {
      return;
    }
    if (nIdx < 0 || nIdx > this.numItems) {
      nIdx = this.numItems;
    }
    if (this.#items.some(({
      displayValue
    }) => displayValue === cName)) {
      return;
    }
    if (cExport === undefined) {
      cExport = cName;
    }
    const data = {
      displayValue: cName,
      exportValue: cExport
    };
    this.#items.splice(nIdx, 0, data);
    if (Array.isArray(this.#currentValueIndices)) {
      let index = this.#currentValueIndices.findIndex(i => i >= nIdx);
      if (index !== -1) {
        for (const ii = this.#currentValueIndices.length; index < ii; index++) {
          ++this.#currentValueIndices[index];
        }
      }
    } else if (this.#currentValueIndices >= nIdx) {
      ++this.#currentValueIndices;
    }
    this.#send({
      id: this.#id,
      insert: {
        index: nIdx,
        ...data
      }
    });
  }
  setAction(cTrigger, cScript) {
    if (typeof cTrigger !== "string" || typeof cScript !== "string") {
      return;
    }
    this.#actions.getOrInsertComputed(cTrigger, makeArr).push(cScript);
  }
  setFocus() {
    this.#send({
      id: this.#id,
      focus: true
    });
  }
  setItems(oArray) {
    if (!this.#isChoice) {
      throw new Error("Not a choice widget");
    }
    this.#items.length = 0;
    for (const element of oArray) {
      let displayValue, exportValue;
      if (Array.isArray(element)) {
        displayValue = element[0]?.toString() || "";
        exportValue = element[1]?.toString() || "";
      } else {
        displayValue = exportValue = element?.toString() || "";
      }
      this.#items.push({
        displayValue,
        exportValue
      });
    }
    this.#currentValueIndices = 0;
    this.#send({
      id: this.#id,
      items: this.#items
    });
  }
  setLock() {}
  signatureGetModifications() {}
  signatureGetSeedValue() {}
  signatureInfo() {}
  signatureSetSeedValue() {}
  signatureSign() {}
  signatureValidate() {}
  #runActions(event) {
    if (this.#radioIds) {
      this.#actions = this.#radioActions[this.#checkedIndex];
    }
    const eventName = event.name;
    const actions = this.#actions.get(eventName);
    if (!actions) {
      return false;
    }
    for (const action of actions) {
      try {
        this.#globalEval(action);
      } catch (error) {
        this.#sendError(serializeError(error, `Error when executing "${eventName}" for field "${this.#id}"\n`));
      }
    }
    return true;
  }
}

;// ./src/scripting_api/print_params.js
class PrintParams {
  binaryOk = true;
  bitmapDPI = 150;
  booklet = {
    binding: 0,
    duplexMode: 0,
    subsetFrom: 0,
    subsetTo: -1
  };
  colorOverride = 0;
  colorProfile = "";
  constants = Object.freeze({
    bookletBindings: Object.freeze({
      Left: 0,
      Right: 1,
      LeftTall: 2,
      RightTall: 3
    }),
    bookletDuplexMode: Object.freeze({
      BothSides: 0,
      FrontSideOnly: 1,
      BasicSideOnly: 2
    }),
    colorOverrides: Object.freeze({
      auto: 0,
      gray: 1,
      mono: 2
    }),
    fontPolicies: Object.freeze({
      everyPage: 0,
      jobStart: 1,
      pageRange: 2
    }),
    handling: Object.freeze({
      none: 0,
      fit: 1,
      shrink: 2,
      tileAll: 3,
      tileLarge: 4,
      nUp: 5,
      booklet: 6
    }),
    interactionLevel: Object.freeze({
      automatic: 0,
      full: 1,
      silent: 2
    }),
    nUpPageOrders: Object.freeze({
      Horizontal: 0,
      HorizontalReversed: 1,
      Vertical: 2
    }),
    printContents: Object.freeze({
      doc: 0,
      docAndComments: 1,
      formFieldsOnly: 2
    }),
    flagValues: Object.freeze({
      applyOverPrint: 1,
      applySoftProofSettings: 1 << 1,
      applyWorkingColorSpaces: 1 << 2,
      emitHalftones: 1 << 3,
      emitPostScriptXObjects: 1 << 4,
      emitFormsAsPSForms: 1 << 5,
      maxJP2KRes: 1 << 6,
      setPageSize: 1 << 7,
      suppressBG: 1 << 8,
      suppressCenter: 1 << 9,
      suppressCJKFontSubst: 1 << 10,
      suppressCropClip: 1 << 11,
      suppressRotate: 1 << 12,
      suppressTransfer: 1 << 13,
      suppressUCR: 1 << 14,
      useTrapAnnots: 1 << 15,
      usePrintersMarks: 1 << 16
    }),
    rasterFlagValues: Object.freeze({
      textToOutline: 1,
      strokesToOutline: 1 << 1,
      allowComplexClip: 1 << 2,
      preserveOverprint: 1 << 3
    }),
    subsets: Object.freeze({
      all: 0,
      even: 1,
      odd: 2
    }),
    tileMarks: Object.freeze({
      none: 0,
      west: 1,
      east: 2
    }),
    usages: Object.freeze({
      auto: 0,
      use: 1,
      noUse: 2
    })
  });
  downloadFarEastFonts = false;
  fileName = "";
  firstPage = 0;
  flags = 0;
  fontPolicy = 0;
  gradientDPI = 150;
  interactive = 1;
  npUpAutoRotate = false;
  npUpNumPagesH = 2;
  npUpNumPagesV = 2;
  npUpPageBorder = false;
  npUpPageOrder = 0;
  pageHandling = 0;
  pageSubset = 0;
  printAsImage = false;
  printContent = 0;
  printerName = "";
  psLevel = 0;
  rasterFlags = 0;
  reversePages = false;
  tileLabel = false;
  tileMark = 0;
  tileOverlap = 0;
  tileScale = 1.0;
  transparencyLevel = 75;
  usePrinterCRD = 0;
  useT1Conversion = 0;
  constructor(data) {
    this.lastPage = data.lastPage;
  }
}

;// ./src/scripting_api/doc.js





const DOC_EXTERNAL = false;
class InfoProxyHandler {
  static get(obj, prop) {
    return obj[prop.toLowerCase()];
  }
  static set(obj, prop, value) {
    throw new Error(`doc.info.${prop} is read-only`);
  }
}
const DocInternals = Object.create(null);
class Doc {
  #pageActions = null;
  #otherPageActions = null;
  #baseURL;
  #calculate = true;
  #delay = false;
  #dirty = false;
  #disclosed = false;
  #media;
  #metadata;
  #noautocomplete;
  #nocache;
  #spellDictionaryOrder = [];
  #spellLanguageOrder = [];
  #printParams;
  #fields = new Map();
  #fieldNames = [];
  #author;
  #creator;
  #creationDate;
  #docID;
  #documentFileName;
  #filesize;
  #keywords;
  #layout;
  #modDate;
  #numPages;
  #pageNum;
  #producer;
  #securityHandler;
  #subject;
  #title;
  #URL;
  #info;
  #zoomType = ZoomType.none;
  #zoom;
  #actions;
  #appObjects;
  #globalEval;
  #send;
  #userActivationData;
  #calculateNow;
  constructor(data) {
    this.#baseURL = data.baseURL || "";
    this.#metadata = data.metadata || "";
    this.#author = data.Author || "";
    this.#creator = data.Creator || "";
    this.#creationDate = this.#getDate(data.CreationDate) || null;
    this.#docID = data.docID || ["", ""];
    this.#documentFileName = data.filename || "";
    this.#filesize = data.filesize || 0;
    this.#keywords = data.Keywords || "";
    this.#layout = data.layout || "";
    this.#modDate = this.#getDate(data.ModDate) || null;
    this.#numPages = data.numPages || 1;
    this.#pageNum = data.pageNum || 0;
    this.#producer = data.Producer || "";
    this.#securityHandler = data.EncryptFilterName || null;
    this.#subject = data.Subject || "";
    this.#title = data.Title || "";
    this.#URL = data.URL || "";
    this.#info = new Proxy(Object.assign(Object.create(null), {
      title: this.#title,
      author: this.#author,
      authors: data.authors || [this.#author],
      subject: this.#subject,
      keywords: this.#keywords,
      creator: this.#creator,
      producer: this.#producer,
      creationdate: this.#creationDate,
      moddate: this.#modDate,
      trapped: data.Trapped || "Unknown"
    }), InfoProxyHandler);
    this.#zoom = data.zoom || 100;
    this.#actions = createMap(data.actions);
    this.#appObjects = data.appObjects;
    this.#calculateNow = data.calculateNow;
    this.#globalEval = data.globalEval;
    this.#send = data.send;
    this.#userActivationData = data.userActivationData;
    this.#printParams = exposeObject(new PrintParams({
      lastPage: this.#numPages - 1
    }), true);
  }
  static {
    Object.assign(DocInternals, {
      addField: (doc, name, field) => doc.#addField(name, field),
      dispatchDocEvent: (doc, name) => doc.#dispatchDocEvent(name),
      dispatchPageEvent: (doc, name, actions, pageNumber) => doc.#dispatchPageEvent(name, actions, pageNumber),
      getTerminalChildren: (doc, fieldName) => doc.#getTerminalChildren(fieldName),
      initActions: doc => doc.#initActions()
    });
  }
  #initActions() {
    for (const field of this.#fields.values()) {
      const initialValue = FieldInternals.getInitialValue(field);
      if (initialValue) {
        FieldInternals.send(field, {
          value: initialValue,
          formattedValue: FieldInternals.getValue(field).toString()
        });
      }
    }
    const dontRun = new Set(["WillClose", "WillSave", "DidSave", "WillPrint", "DidPrint", "OpenAction"]);
    this.#userActivationData.disableSaving = true;
    for (const actionName of this.#actions.keys()) {
      if (!dontRun.has(actionName)) {
        this.#runActions(actionName);
      }
    }
    this.#userActivationData.disableSaving = false;
  }
  #dispatchDocEvent(name) {
    switch (name) {
      case "Open":
        this.#userActivationData.disableSaving = true;
        this.#runActions("OpenAction");
        this.#userActivationData.disableSaving = false;
        break;
      case "WillPrint":
        this.#userActivationData.disablePrinting = true;
        try {
          this.#runActions(name);
        } catch (error) {
          this.#send(serializeError(error));
        }
        this.#send({
          command: "WillPrintFinished"
        });
        this.#userActivationData.disablePrinting = false;
        break;
      case "WillSave":
        this.#userActivationData.disableSaving = true;
        try {
          this.#runActions(name);
        } catch (error) {
          this.#send(serializeError(error));
        }
        this.#send({
          command: "WillSaveFinished"
        });
        this.#userActivationData.disableSaving = false;
        break;
      default:
        this.#runActions(name);
    }
  }
  #dispatchPageEvent(name, actions, pageNumber) {
    if (name === "PageOpen") {
      this.#pageActions ??= new Map();
      this.#pageActions.getOrInsertComputed(pageNumber, () => createMap(actions));
      this.#pageNum = pageNumber - 1;
    }
    for (const acts of [this.#pageActions, this.#otherPageActions]) {
      actions = acts?.get(pageNumber)?.get(name);
      if (actions) {
        for (const action of actions) {
          this.#globalEval(action);
        }
      }
    }
  }
  #runActions(name) {
    const actions = this.#actions.get(name);
    if (!actions) {
      return;
    }
    for (const action of actions) {
      try {
        this.#globalEval(action);
      } catch (error) {
        this.#send(serializeError(error, `Error when executing "${name}" for document\n`));
      }
    }
  }
  #addField(name, field) {
    this.#fields.set(name, field);
    this.#fieldNames.push(name);
    for (const [page, eventName, scripts] of FieldInternals.getPageActions(field)) {
      this.#otherPageActions ??= new Map();
      this.#otherPageActions.getOrInsertComputed(page + 1, makeMap).getOrInsertComputed(eventName, makeArr).push(...scripts);
    }
  }
  #getDate(date) {
    if (!date || date.length < 15 || !date.startsWith("D:")) {
      return date;
    }
    date = date.substring(2);
    const year = date.substring(0, 4);
    const month = date.substring(4, 6);
    const day = date.substring(6, 8);
    const hour = date.substring(8, 10);
    const minute = date.substring(10, 12);
    const o = date.charAt(12);
    let second, offsetPos;
    if (o === "Z" || o === "+" || o === "-") {
      second = "00";
      offsetPos = 12;
    } else {
      second = date.substring(12, 14);
      offsetPos = 14;
    }
    const offset = date.substring(offsetPos).replaceAll("'", "");
    return new Date(`${year}-${month}-${day}T${hour}:${minute}:${second}${offset}`);
  }
  get author() {
    return this.#author;
  }
  set author(_) {
    throw new Error("doc.author is read-only");
  }
  get baseURL() {
    return this.#baseURL;
  }
  set baseURL(baseURL) {
    this.#baseURL = baseURL;
  }
  get bookmarkRoot() {
    return undefined;
  }
  set bookmarkRoot(_) {
    throw new Error("doc.bookmarkRoot is read-only");
  }
  get calculate() {
    return this.#calculate;
  }
  set calculate(calculate) {
    this.#calculate = calculate;
  }
  get creator() {
    return this.#creator;
  }
  set creator(_) {
    throw new Error("doc.creator is read-only");
  }
  get dataObjects() {
    return [];
  }
  set dataObjects(_) {
    throw new Error("doc.dataObjects is read-only");
  }
  get delay() {
    return this.#delay;
  }
  set delay(delay) {
    this.#delay = delay;
  }
  get dirty() {
    return this.#dirty;
  }
  set dirty(dirty) {
    this.#dirty = dirty;
  }
  get disclosed() {
    return this.#disclosed;
  }
  set disclosed(disclosed) {
    this.#disclosed = disclosed;
  }
  get docID() {
    return this.#docID;
  }
  set docID(_) {
    throw new Error("doc.docID is read-only");
  }
  get documentFileName() {
    return this.#documentFileName;
  }
  set documentFileName(_) {
    throw new Error("doc.documentFileName is read-only");
  }
  get dynamicXFAForm() {
    return false;
  }
  set dynamicXFAForm(_) {
    throw new Error("doc.dynamicXFAForm is read-only");
  }
  get external() {
    return DOC_EXTERNAL;
  }
  set external(_) {
    throw new Error("doc.external is read-only");
  }
  get filesize() {
    return this.#filesize;
  }
  set filesize(_) {
    throw new Error("doc.filesize is read-only");
  }
  get hidden() {
    return false;
  }
  set hidden(_) {
    throw new Error("doc.hidden is read-only");
  }
  get hostContainer() {
    return undefined;
  }
  set hostContainer(_) {
    throw new Error("doc.hostContainer is read-only");
  }
  get icons() {
    return undefined;
  }
  set icons(_) {
    throw new Error("doc.icons is read-only");
  }
  get info() {
    return this.#info;
  }
  set info(_) {
    throw new Error("doc.info is read-only");
  }
  get innerAppWindowRect() {
    return [0, 0, 0, 0];
  }
  set innerAppWindowRect(_) {
    throw new Error("doc.innerAppWindowRect is read-only");
  }
  get innerDocWindowRect() {
    return [0, 0, 0, 0];
  }
  set innerDocWindowRect(_) {
    throw new Error("doc.innerDocWindowRect is read-only");
  }
  get isModal() {
    return false;
  }
  set isModal(_) {
    throw new Error("doc.isModal is read-only");
  }
  get keywords() {
    return this.#keywords;
  }
  set keywords(_) {
    throw new Error("doc.keywords is read-only");
  }
  get layout() {
    return this.#layout;
  }
  set layout(value) {
    if (!this.#userActivationData.userActivation) {
      return;
    }
    this.#userActivationData.userActivation = false;
    if (typeof value !== "string") {
      return;
    }
    if (value !== "SinglePage" && value !== "OneColumn" && value !== "TwoColumnLeft" && value !== "TwoPageLeft" && value !== "TwoColumnRight" && value !== "TwoPageRight") {
      value = "SinglePage";
    }
    this.#send({
      command: "layout",
      value
    });
    this.#layout = value;
  }
  get media() {
    return this.#media;
  }
  set media(media) {
    this.#media = media;
  }
  get metadata() {
    return this.#metadata;
  }
  set metadata(metadata) {
    this.#metadata = metadata;
  }
  get modDate() {
    return this.#modDate;
  }
  set modDate(_) {
    throw new Error("doc.modDate is read-only");
  }
  get mouseX() {
    return 0;
  }
  set mouseX(_) {
    throw new Error("doc.mouseX is read-only");
  }
  get mouseY() {
    return 0;
  }
  set mouseY(_) {
    throw new Error("doc.mouseY is read-only");
  }
  get noautocomplete() {
    return this.#noautocomplete;
  }
  set noautocomplete(noautocomplete) {
    this.#noautocomplete = noautocomplete;
  }
  get nocache() {
    return this.#nocache;
  }
  set nocache(nocache) {
    this.#nocache = nocache;
  }
  get numFields() {
    return this.#fieldNames.length;
  }
  set numFields(_) {
    throw new Error("doc.numFields is read-only");
  }
  get numPages() {
    return this.#numPages;
  }
  set numPages(_) {
    throw new Error("doc.numPages is read-only");
  }
  get numTemplates() {
    return 0;
  }
  set numTemplates(_) {
    throw new Error("doc.numTemplates is read-only");
  }
  get outerAppWindowRect() {
    return [0, 0, 0, 0];
  }
  set outerAppWindowRect(_) {
    throw new Error("doc.outerAppWindowRect is read-only");
  }
  get outerDocWindowRect() {
    return [0, 0, 0, 0];
  }
  set outerDocWindowRect(_) {
    throw new Error("doc.outerDocWindowRect is read-only");
  }
  get pageNum() {
    return this.#pageNum;
  }
  set pageNum(value) {
    if (!this.#userActivationData.userActivation) {
      return;
    }
    this.#userActivationData.userActivation = false;
    if (typeof value !== "number" || value < 0 || value >= this.#numPages) {
      return;
    }
    this.#send({
      command: "page-num",
      value
    });
    this.#pageNum = value;
  }
  get pageWindowRect() {
    return [0, 0, 0, 0];
  }
  set pageWindowRect(_) {
    throw new Error("doc.pageWindowRect is read-only");
  }
  get path() {
    return "";
  }
  set path(_) {
    throw new Error("doc.path is read-only");
  }
  get permStatusReady() {
    return true;
  }
  set permStatusReady(_) {
    throw new Error("doc.permStatusReady is read-only");
  }
  get producer() {
    return this.#producer;
  }
  set producer(_) {
    throw new Error("doc.producer is read-only");
  }
  get requiresFullSave() {
    return false;
  }
  set requiresFullSave(_) {
    throw new Error("doc.requiresFullSave is read-only");
  }
  get securityHandler() {
    return this.#securityHandler;
  }
  set securityHandler(_) {
    throw new Error("doc.securityHandler is read-only");
  }
  get selectedAnnots() {
    return [];
  }
  set selectedAnnots(_) {
    throw new Error("doc.selectedAnnots is read-only");
  }
  get sounds() {
    return [];
  }
  set sounds(_) {
    throw new Error("doc.sounds is read-only");
  }
  get spellDictionaryOrder() {
    return this.#spellDictionaryOrder;
  }
  set spellDictionaryOrder(spellDictionaryOrder) {
    this.#spellDictionaryOrder = spellDictionaryOrder;
  }
  get spellLanguageOrder() {
    return this.#spellLanguageOrder;
  }
  set spellLanguageOrder(spellLanguageOrder) {
    this.#spellLanguageOrder = spellLanguageOrder;
  }
  get subject() {
    return this.#subject;
  }
  set subject(_) {
    throw new Error("doc.subject is read-only");
  }
  get templates() {
    return [];
  }
  set templates(_) {
    throw new Error("doc.templates is read-only");
  }
  get title() {
    return this.#title;
  }
  set title(_) {
    throw new Error("doc.title is read-only");
  }
  get URL() {
    return this.#URL;
  }
  set URL(_) {
    throw new Error("doc.URL is read-only");
  }
  get viewState() {
    return undefined;
  }
  set viewState(_) {
    throw new Error("doc.viewState is read-only");
  }
  get xfa() {
    return undefined;
  }
  set xfa(_) {
    throw new Error("doc.xfa is read-only");
  }
  get XFAForeground() {
    return false;
  }
  set XFAForeground(_) {
    throw new Error("doc.XFAForeground is read-only");
  }
  get zoomType() {
    return this.#zoomType;
  }
  set zoomType(type) {
    if (!this.#userActivationData.userActivation) {
      return;
    }
    this.#userActivationData.userActivation = false;
    if (typeof type !== "string") {
      return;
    }
    switch (type) {
      case ZoomType.none:
        this.#send({
          command: "zoom",
          value: 1
        });
        break;
      case ZoomType.fitP:
        this.#send({
          command: "zoom",
          value: "page-fit"
        });
        break;
      case ZoomType.fitW:
        this.#send({
          command: "zoom",
          value: "page-width"
        });
        break;
      case ZoomType.fitH:
        this.#send({
          command: "zoom",
          value: "page-height"
        });
        break;
      case ZoomType.fitV:
        this.#send({
          command: "zoom",
          value: "auto"
        });
        break;
      case ZoomType.pref:
      case ZoomType.refW:
        break;
      default:
        return;
    }
    this.#zoomType = type;
  }
  get zoom() {
    return this.#zoom;
  }
  set zoom(value) {
    if (!this.#userActivationData.userActivation) {
      return;
    }
    this.#userActivationData.userActivation = false;
    if (typeof value !== "number" || value < 8.33 || value > 6400) {
      return;
    }
    this.#send({
      command: "zoom",
      value: value / 100
    });
  }
  addAnnot() {}
  addField() {}
  addIcon() {}
  addLink() {}
  addRecipientListCryptFilter() {}
  addRequirement() {}
  addScript() {}
  addThumbnails() {}
  addWatermarkFromFile() {}
  addWatermarkFromText() {}
  addWeblinks() {}
  bringToFront() {}
  calculateNow() {
    this.#calculateNow();
  }
  closeDoc() {}
  colorConvertPage() {}
  createDataObject() {}
  createTemplate() {}
  deletePages() {}
  deleteSound() {}
  embedDocAsDataObject() {}
  embedOutputIntent() {}
  encryptForRecipients() {}
  encryptUsingPolicy() {}
  exportAsFDF() {}
  exportAsFDFStr() {}
  exportAsText() {}
  exportAsXFDF() {}
  exportAsXFDFStr() {}
  exportDataObject() {}
  exportXFAData() {}
  extractPages() {}
  flattenPages() {}
  getAnnot() {}
  getAnnots() {}
  getAnnot3D() {}
  getAnnots3D() {}
  getColorConvertAction() {}
  getDataObject() {}
  getDataObjectContents() {}
  getField(cName) {
    if (cName && typeof cName === "object") {
      cName = cName.cName;
    }
    if (typeof cName !== "string") {
      throw new TypeError("Invalid field name: must be a string");
    }
    const searchedField = this.#fields.get(cName);
    if (searchedField) {
      return searchedField;
    }
    const parts = cName.split("#");
    let childIndex = NaN;
    if (parts.length === 2) {
      childIndex = Math.floor(parseFloat(parts[1]));
      cName = parts[0];
    }
    for (const [name, field] of this.#fields) {
      if (name.endsWith(cName)) {
        if (!isNaN(childIndex)) {
          const children = this.#getChildren(name);
          if (childIndex < 0 || childIndex >= children.length) {
            childIndex = 0;
          }
          if (childIndex < children.length) {
            this.#fields.set(cName, children[childIndex]);
            return children[childIndex];
          }
        }
        this.#fields.set(cName, field);
        return field;
      }
    }
    return null;
  }
  #getChildren(fieldName) {
    const len = fieldName.length;
    const children = [];
    const pattern = /^\.[^.]+$/;
    for (const [name, field] of this.#fields) {
      if (name.startsWith(fieldName)) {
        const finalPart = name.slice(len);
        if (pattern.test(finalPart)) {
          children.push(field);
        }
      }
    }
    return children;
  }
  #getTerminalChildren(fieldName) {
    const children = [];
    const len = fieldName.length;
    for (const [name, field] of this.#fields) {
      if (name.startsWith(fieldName)) {
        const finalPart = name.slice(len);
        if (FieldInternals.getHasValue(field) && (finalPart === "" || finalPart.startsWith("."))) {
          children.push(field);
        }
      }
    }
    return children;
  }
  getIcon() {}
  getLegalWarnings() {}
  getLinks() {}
  getNthFieldName(nIndex) {
    if (nIndex && typeof nIndex === "object") {
      nIndex = nIndex.nIndex;
    }
    if (typeof nIndex !== "number") {
      throw new TypeError("Invalid field index: must be a number");
    }
    return nIndex >= 0 && nIndex < this.numFields ? this.#fieldNames[Math.trunc(nIndex)] : null;
  }
  getNthTemplate() {
    return null;
  }
  getOCGs() {}
  getOCGOrder() {}
  getPageBox() {}
  getPageLabel() {}
  getPageNthWord() {}
  getPageNthWordQuads() {}
  getPageNumWords() {}
  getPageRotation() {}
  getPageTransition() {}
  getPrintParams() {
    return this.#printParams;
  }
  getSound() {}
  getTemplate() {}
  getURL() {}
  gotoNamedDest() {}
  importAnFDF() {}
  importAnXFDF() {}
  importDataObject() {}
  importIcon() {}
  importSound() {}
  importTextData() {}
  importXFAData() {}
  insertPages() {}
  mailDoc() {}
  mailForm() {}
  movePage() {}
  newPage() {}
  openDataObject() {}
  print(bUI = true, nStart = 0, nEnd = -1, bSilent = false, bShrinkToFit = false, bPrintAsImage = false, bReverse = false, bAnnotations = true, printParams = null) {
    if (this.#userActivationData.disablePrinting || !this.#userActivationData.userActivation) {
      return;
    }
    this.#userActivationData.userActivation = false;
    if (bUI && typeof bUI === "object") {
      nStart = bUI.nStart;
      nEnd = bUI.nEnd;
      bSilent = bUI.bSilent;
      bShrinkToFit = bUI.bShrinkToFit;
      bPrintAsImage = bUI.bPrintAsImage;
      bReverse = bUI.bReverse;
      bAnnotations = bUI.bAnnotations;
      printParams = bUI.printParams;
      bUI = bUI.bUI;
    }
    if (printParams) {
      nStart = printParams.firstPage;
      nEnd = printParams.lastPage;
    }
    nStart = typeof nStart === "number" ? Math.max(0, Math.trunc(nStart)) : 0;
    nEnd = typeof nEnd === "number" ? Math.max(0, Math.trunc(nEnd)) : -1;
    this.#send({
      command: "print",
      start: nStart,
      end: nEnd
    });
  }
  removeDataObject() {}
  removeField() {}
  removeIcon() {}
  removeLinks() {}
  removeRequirement() {}
  removeScript() {}
  removeTemplate() {}
  removeThumbnails() {}
  removeWeblinks() {}
  replacePages() {}
  resetForm(aFields = null) {
    if (aFields && typeof aFields === "object" && !Array.isArray(aFields)) {
      aFields = aFields.aFields;
    }
    if (aFields && !Array.isArray(aFields)) {
      aFields = [aFields];
    }
    let mustCalculate = false;
    let fieldsToReset;
    if (aFields) {
      fieldsToReset = new Set();
      for (const fieldName of aFields) {
        if (!fieldName) {
          continue;
        }
        if (typeof fieldName !== "string") {
          fieldsToReset = null;
          break;
        }
        const field = this.getField(fieldName);
        if (!field) {
          continue;
        }
        fieldsToReset.add(field);
        mustCalculate = true;
      }
    }
    if (fieldsToReset) {
      for (const field of fieldsToReset) {
        for (const id of FieldInternals.getKidIds(field) || []) {
          const kid = this.#appObjects[id];
          if (kid) {
            fieldsToReset.add(kid);
          }
        }
      }
    } else {
      fieldsToReset = this.#fields.values();
      mustCalculate = this.#fields.size !== 0;
    }
    for (const field of fieldsToReset) {
      FieldInternals.setValue(field, field.defaultValue);
      FieldInternals.send(field, {
        value: field.defaultValue,
        formattedValue: null,
        selRange: [0, 0]
      });
    }
    if (mustCalculate) {
      this.calculateNow();
    }
  }
  saveAs() {}
  scroll() {}
  selectPageNthWord() {}
  setAction() {}
  setDataObjectContents() {}
  setOCGOrder() {}
  setPageAction() {}
  setPageBoxes() {}
  setPageLabels() {}
  setPageRotations() {}
  setPageTabOrder() {}
  setPageTransitions() {}
  spawnPageFromTemplate() {}
  submitForm() {}
  syncAnnotScan() {}
}

;// ./src/scripting_api/event.js



const {
  values
} = Object;
const EVENT_PROTOTYPE = Object.create(Object.prototype, {
  [Symbol.toStringTag]: {
    value: "Event"
  }
});
function createEvent(data) {
  return {
    __proto__: EVENT_PROTOTYPE,
    change: data.change || "",
    changeEx: data.changeEx || null,
    commitKey: data.commitKey || 0,
    fieldFull: data.fieldFull || false,
    keyDown: data.keyDown || false,
    modifier: data.modifier || false,
    name: data.name,
    rc: true,
    richChange: data.richChange || [],
    richChangeEx: data.richChangeEx || [],
    richValue: data.richValue || [],
    selEnd: data.selEnd ?? -1,
    selStart: data.selStart ?? -1,
    shift: data.shift || false,
    source: data.source || null,
    target: data.target || null,
    targetName: "",
    type: "Field",
    value: data.value || "",
    willCommit: data.willCommit || false
  };
}
function mergeChange(event) {
  let value = event.value;
  if (Array.isArray(value)) {
    return value;
  }
  if (typeof value !== "string") {
    value = value.toString();
  }
  const prefix = event.selStart >= 0 ? value.substring(0, event.selStart) : "";
  const postfix = event.selEnd >= 0 && event.selEnd <= value.length ? value.substring(event.selEnd) : "";
  return `${prefix}${event.change}${postfix}`;
}
class EventDispatcher {
  #calculationOrder;
  #doc;
  #externalCall;
  #isCalculating = false;
  #objects;
  #userActivationData;
  constructor(doc, calculationOrder, objects, externalCall, userActivationData) {
    this.#doc = doc;
    this.#calculationOrder = calculationOrder;
    this.#objects = objects;
    this.#externalCall = externalCall;
    this.#userActivationData = userActivationData;
  }
  #userActivation() {
    this.#userActivationData.userActivation = true;
    this.#externalCall("setTimeout", [(/* inlined export .USERACTIVATION_CALLBACKID */0), (/* inlined export .USERACTIVATION_MAXTIME_VALIDITY */5000)]);
  }
  dispatch(baseEvent) {
    const id = baseEvent.id;
    if (!(id in this.#objects)) {
      let event;
      if (id === "doc" || id === "page") {
        event = globalThis.event = createEvent(baseEvent);
        event.source = event.target = globalThis;
      }
      if (id === "doc") {
        const eventName = event.name;
        if (eventName === "Open") {
          this.#userActivation();
          DocInternals.initActions(this.#doc);
          this.#formatAll();
        }
        if (!["DidPrint", "DidSave", "WillPrint", "WillSave"].includes(eventName)) {
          this.#userActivation();
        }
        DocInternals.dispatchDocEvent(this.#doc, event.name);
      } else if (id === "page") {
        this.#userActivation();
        DocInternals.dispatchPageEvent(this.#doc, event.name, baseEvent.actions, baseEvent.pageNumber);
      } else if (id === "app" && baseEvent.name === "ResetForm") {
        this.#userActivation();
        for (const fieldId of baseEvent.ids) {
          const field = this.#objects[fieldId];
          if (field) {
            FieldInternals.setValue(field, field.defaultValue);
          }
        }
      }
      return;
    }
    const name = baseEvent.name;
    const source = this.#objects[id];
    const event = globalThis.event = createEvent(baseEvent);
    let savedChange;
    this.#userActivation();
    FieldInternals.prepareEvent(source, id, event);
    switch (name) {
      case "Keystroke":
        savedChange = {
          value: event.value,
          changeEx: event.changeEx,
          change: event.change,
          selStart: event.selStart,
          selEnd: event.selEnd
        };
        break;
      case "Blur":
      case "Focus":
        Object.defineProperty(event, "value", {
          configurable: false,
          writable: false,
          enumerable: true,
          value: event.value
        });
        break;
      case "Validate":
        this.#runValidation(source, event);
        return;
      case "Action":
        this.#runActions(source, source, event, name);
        this.#runCalculate(source, event);
        return;
    }
    this.#runActions(source, source, event, name);
    if (name !== "Keystroke") {
      return;
    }
    if (event.rc) {
      if (event.willCommit) {
        this.#runValidation(source, event);
      } else {
        if (FieldInternals.getIsChoice(source)) {
          FieldInternals.setValue(source, savedChange.changeEx);
          FieldInternals.send(source, {
            value: FieldInternals.getValue(source)
          });
          return;
        }
        const value = mergeChange(event);
        FieldInternals.setValue(source, value);
        let selStart, selEnd;
        if (event.selStart !== savedChange.selStart || event.selEnd !== savedChange.selEnd) {
          selStart = event.selStart;
          selEnd = event.selEnd;
        } else {
          selEnd = selStart = savedChange.selStart + event.change.length;
        }
        FieldInternals.send(source, {
          value,
          selRange: [selStart, selEnd]
        });
      }
    } else if (!event.willCommit) {
      FieldInternals.send(source, {
        value: savedChange.value,
        selRange: [savedChange.selStart, savedChange.selEnd]
      });
    } else {
      FieldInternals.send(source, {
        value: "",
        formattedValue: null,
        selRange: [0, 0]
      });
    }
  }
  #formatAll() {
    const event = globalThis.event = createEvent({});
    for (const source of values(this.#objects)) {
      event.value = FieldInternals.getComputedValue(source);
      this.#runActions(source, source, event, "Format");
    }
  }
  #runValidation(source, event) {
    const didValidateRun = this.#runActions(source, source, event, "Validate");
    if (event.rc) {
      FieldInternals.setValue(source, event.value);
      this.#runCalculate(source, event);
      const savedValue = event.value = FieldInternals.getComputedValue(source);
      const formattedValue = this.#runActions(source, source, event, "Format") ? event.value?.toString?.() : null;
      FieldInternals.send(source, {
        value: savedValue,
        formattedValue
      });
      event.value = savedValue;
    } else if (didValidateRun) {
      FieldInternals.send(source, {
        value: "",
        formattedValue: null,
        selRange: [0, 0],
        focus: true
      });
    }
  }
  #runActions(source, target, event, eventName) {
    event.source = source;
    event.target = target;
    event.name = eventName;
    event.targetName = target.name;
    event.rc = true;
    return FieldInternals.runActions(target, event);
  }
  calculateNow() {
    if (!this.#calculationOrder || this.#isCalculating || !this.#doc.calculate) {
      return;
    }
    this.#isCalculating = true;
    const first = this.#calculationOrder[0];
    const source = this.#objects[first];
    globalThis.event = createEvent({});
    this.#runCalculate(source, globalThis.event);
    this.#isCalculating = false;
  }
  #runCalculate(source, event) {
    if (!this.#calculationOrder || !this.#doc.calculate) {
      return;
    }
    for (const targetId of this.#calculationOrder) {
      if (!(targetId in this.#objects)) {
        continue;
      }
      if (!this.#doc.calculate) {
        break;
      }
      event.value = null;
      const target = this.#objects[targetId];
      let savedValue = FieldInternals.getComputedValue(target);
      this.#runActions(source, target, event, "Calculate");
      if (!event.rc) {
        continue;
      }
      if (event.value !== null) {
        FieldInternals.setValue(target, event.value);
      } else {
        event.value = FieldInternals.getComputedValue(target);
      }
      this.#runActions(target, target, event, "Validate");
      if (!event.rc) {
        if (FieldInternals.getComputedValue(target) !== savedValue) {
          target.value = savedValue;
        }
        continue;
      }
      if (event.value === null) {
        event.value = FieldInternals.getComputedValue(target);
      }
      savedValue = FieldInternals.getComputedValue(target);
      const formattedValue = this.#runActions(target, target, event, "Format") ? event.value?.toString?.() : null;
      FieldInternals.send(target, {
        value: savedValue,
        formattedValue
      });
    }
  }
}

;// ./src/scripting_api/aform.js




class AForm {
  #document;
  #app;
  #util;
  #color;
  #emailRegex = new RegExp("^[\\w.!#$%&'*+/=?^`{|}~-]+" + "@[a-zA-Z0-9](?:[a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?" + "(?:\\.[a-zA-Z0-9](?:[a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?)*$");
  constructor(document, app, util, color) {
    this.#document = document;
    this.#app = app;
    this.#util = util;
    this.#color = color;
  }
  #mkTargetName(event) {
    return event.target ? `[ ${event.target.name} ]` : "";
  }
  #parseDate(cFormat, cDate) {
    let date = null;
    try {
      date = this.#util.scand(cFormat, cDate);
    } catch {}
    if (date) {
      return date;
    }
    date = Date.parse(cDate);
    return isNaN(date) ? null : new Date(date);
  }
  AFMergeChange(event = globalThis.event) {
    return event.willCommit ? event.value.toString() : mergeChange(event);
  }
  AFParseDateEx(cString, cOrder) {
    return this.#parseDate(cOrder, cString);
  }
  AFExtractNums(str) {
    if (typeof str === "number") {
      return [str];
    }
    if (!str || typeof str !== "string") {
      return null;
    }
    const first = str.charAt(0);
    if (first === "." || first === ",") {
      str = `0${str}`;
    }
    const numbers = str.match(/(\d+)/g);
    return numbers.length === 0 ? null : numbers;
  }
  AFMakeNumber(str) {
    if (typeof str === "number") {
      return str;
    }
    if (typeof str !== "string") {
      return null;
    }
    str = str.trim().replace(",", ".");
    const number = parseFloat(str);
    return isNaN(number) || !isFinite(number) ? null : number;
  }
  AFMakeArrayFromList(string) {
    return typeof string === "string" ? string.split(/, ?/g) : string;
  }
  AFNumber_Format(nDec, sepStyle, negStyle, currStyle, strCurrency, bCurrencyPrepend) {
    const event = globalThis.event;
    let value = this.AFMakeNumber(event.value);
    if (value === null) {
      event.value = "";
      return;
    }
    const sign = Math.sign(value);
    const buf = [];
    let hasParen = false;
    if (sign === -1 && bCurrencyPrepend && negStyle === 0) {
      buf.push("-");
    }
    if ((negStyle === 2 || negStyle === 3) && sign === -1) {
      buf.push("(");
      hasParen = true;
    }
    if (bCurrencyPrepend) {
      buf.push(strCurrency);
    }
    sepStyle = MathClamp(Math.floor(sepStyle), 0, 4);
    buf.push("%,", sepStyle, ".", nDec.toString(), "f");
    if (!bCurrencyPrepend) {
      buf.push(strCurrency);
    }
    if (hasParen) {
      buf.push(")");
    }
    if (negStyle === 1 || negStyle === 3) {
      event.target.textColor = sign === 1 ? this.#color.black : this.#color.red;
    }
    if ((negStyle !== 0 || bCurrencyPrepend) && sign === -1) {
      value = -value;
    }
    const formatStr = buf.join("");
    event.value = this.#util.printf(formatStr, value);
  }
  AFNumber_Keystroke(nDec, sepStyle, negStyle, currStyle, strCurrency, bCurrencyPrepend) {
    const event = globalThis.event;
    let value = this.AFMergeChange(event);
    if (!value) {
      return;
    }
    value = value.trim();
    let pattern;
    if (sepStyle > 1) {
      pattern = event.willCommit ? /^[+-]?(\d+(,\d*)?|,\d+)$/ : /^[+-]?\d*(?:,\d*)?$/;
    } else {
      pattern = event.willCommit ? /^[+-]?(\d+(\.\d*)?|\.\d+)$/ : /^[+-]?\d*(?:\.\d*)?$/;
    }
    if (!pattern.test(value)) {
      if (event.willCommit) {
        const err = `${GlobalConstants.IDS_INVALID_VALUE} ${this.#mkTargetName(event)}`;
        this.#app.alert(err);
      }
      event.rc = false;
    }
    if (event.willCommit && sepStyle > 1) {
      event.value = parseFloat(value.replace(",", "."));
    }
  }
  AFPercent_Format(nDec, sepStyle, percentPrepend = false) {
    if (typeof nDec !== "number" || typeof sepStyle !== "number") {
      return;
    }
    if (nDec < 0) {
      throw new Error("Invalid nDec value in AFPercent_Format");
    }
    const event = globalThis.event;
    if (nDec > 512) {
      event.value = "%";
      return;
    }
    nDec = Math.floor(nDec);
    sepStyle = MathClamp(Math.floor(sepStyle), 0, 4);
    let value = this.AFMakeNumber(event.value);
    if (value === null) {
      event.value = "%";
      return;
    }
    const formatStr = `%,${sepStyle}.${nDec}f`;
    value = this.#util.printf(formatStr, value * 100);
    event.value = percentPrepend ? `%${value}` : `${value}%`;
  }
  AFPercent_Keystroke(nDec, sepStyle) {
    this.AFNumber_Keystroke(nDec, sepStyle, 0, 0, "", true);
  }
  AFDate_FormatEx(cFormat) {
    const event = globalThis.event;
    const value = event.value;
    if (!value) {
      return;
    }
    const date = this.#parseDate(cFormat, value);
    if (date !== null) {
      event.value = this.#util.printd(cFormat, date);
    }
  }
  AFDate_Format(pdf) {
    this.AFDate_FormatEx(DateFormats[pdf] ?? pdf);
  }
  AFDate_KeystrokeEx(cFormat) {
    const event = globalThis.event;
    if (!event.willCommit) {
      return;
    }
    const value = this.AFMergeChange(event);
    if (!value) {
      return;
    }
    if (this.#parseDate(cFormat, value) === null) {
      const invalid = GlobalConstants.IDS_INVALID_DATE;
      const invalid2 = GlobalConstants.IDS_INVALID_DATE2;
      const err = `${invalid} ${this.#mkTargetName(event)}${invalid2}${cFormat}`;
      this.#app.alert(err);
      event.rc = false;
    }
  }
  AFDate_Keystroke(pdf) {
    if (pdf >= 0 && pdf < DateFormats.length) {
      this.AFDate_KeystrokeEx(DateFormats[pdf]);
    }
  }
  AFRange_Validate(bGreaterThan, nGreaterThan, bLessThan, nLessThan) {
    const event = globalThis.event;
    if (!event.value) {
      return;
    }
    const value = this.AFMakeNumber(event.value);
    if (value === null) {
      return;
    }
    bGreaterThan = !!bGreaterThan;
    bLessThan = !!bLessThan;
    if (bGreaterThan) {
      nGreaterThan = this.AFMakeNumber(nGreaterThan);
      if (nGreaterThan === null) {
        return;
      }
    }
    if (bLessThan) {
      nLessThan = this.AFMakeNumber(nLessThan);
      if (nLessThan === null) {
        return;
      }
    }
    let err = "";
    if (bGreaterThan && bLessThan) {
      if (value < nGreaterThan || value > nLessThan) {
        err = this.#util.printf(GlobalConstants.IDS_GT_AND_LT, nGreaterThan, nLessThan);
      }
    } else if (bGreaterThan) {
      if (value < nGreaterThan) {
        err = this.#util.printf(GlobalConstants.IDS_GREATER_THAN, nGreaterThan);
      }
    } else if (value > nLessThan) {
      err = this.#util.printf(GlobalConstants.IDS_LESS_THAN, nLessThan);
    }
    if (err) {
      this.#app.alert(err);
      event.rc = false;
    }
  }
  AFSimple(cFunction, nValue1, nValue2) {
    const value1 = this.AFMakeNumber(nValue1);
    if (value1 === null) {
      throw new Error("Invalid nValue1 in AFSimple");
    }
    const value2 = this.AFMakeNumber(nValue2);
    if (value2 === null) {
      throw new Error("Invalid nValue2 in AFSimple");
    }
    switch (cFunction) {
      case "AVG":
        return (value1 + value2) / 2;
      case "SUM":
        return value1 + value2;
      case "PRD":
        return value1 * value2;
      case "MIN":
        return Math.min(value1, value2);
      case "MAX":
        return Math.max(value1, value2);
    }
    throw new Error("Invalid cFunction in AFSimple");
  }
  AFSimple_Calculate(cFunction, cFields) {
    const actions = {
      AVG: args => Math.sumPrecise(args) / args.length,
      SUM: args => Math.sumPrecise(args),
      PRD: args => args.reduce((acc, value) => acc * value, 1),
      MIN: args => Math.min(...args),
      MAX: args => Math.max(...args)
    };
    if (!(cFunction in actions)) {
      throw new TypeError("Invalid function in AFSimple_Calculate");
    }
    const event = globalThis.event;
    const values = [];
    cFields = this.AFMakeArrayFromList(cFields);
    for (const cField of cFields) {
      const field = this.#document.getField(cField);
      if (!field) {
        continue;
      }
      for (const child of field.getArray()) {
        const number = this.AFMakeNumber(child.value);
        values.push(number ?? 0);
      }
    }
    if (values.length === 0) {
      event.value = 0;
      return;
    }
    const res = actions[cFunction](values);
    event.value = Math.round(1e6 * res) / 1e6;
  }
  AFSpecial_Format(psf) {
    const event = globalThis.event;
    if (!event.value) {
      return;
    }
    psf = this.AFMakeNumber(psf);
    let formatStr;
    switch (psf) {
      case 0:
        formatStr = "99999";
        break;
      case 1:
        formatStr = "99999-9999";
        break;
      case 2:
        formatStr = this.#util.printx("9999999999", event.value).length >= 10 ? "(999) 999-9999" : "999-9999";
        break;
      case 3:
        formatStr = "999-99-9999";
        break;
      default:
        throw new Error("Invalid psf in AFSpecial_Format");
    }
    event.value = this.#util.printx(formatStr, event.value);
  }
  AFSpecial_KeystrokeEx(cMask) {
    const event = globalThis.event;
    const simplifiedFormatStr = cMask.replaceAll(/[^9AOX]/g, "");
    this.#AFSpecial_KeystrokeEx_helper(simplifiedFormatStr, null, false);
    if (event.rc) {
      return;
    }
    event.rc = true;
    this.#AFSpecial_KeystrokeEx_helper(cMask, null, true);
  }
  #AFSpecial_KeystrokeEx_helper(cMask, value, warn) {
    if (!cMask) {
      return;
    }
    const event = globalThis.event;
    value ||= this.AFMergeChange(event);
    if (!value) {
      return;
    }
    const checkers = new Map([["9", char => char >= "0" && char <= "9"], ["A", char => "a" <= char && char <= "z" || "A" <= char && char <= "Z"], ["O", char => "a" <= char && char <= "z" || "A" <= char && char <= "Z" || "0" <= char && char <= "9"], ["X", char => true]]);
    function _checkValidity(_value, _cMask) {
      for (let i = 0, ii = _value.length; i < ii; i++) {
        const mask = _cMask.charAt(i);
        const char = _value.charAt(i);
        const checker = checkers.get(mask);
        if (checker) {
          if (!checker(char)) {
            return false;
          }
        } else if (mask !== char) {
          return false;
        }
      }
      return true;
    }
    const err = `${GlobalConstants.IDS_INVALID_VALUE} = "${cMask}"`;
    if (value.length > cMask.length) {
      if (warn) {
        this.#app.alert(err);
      }
      event.rc = false;
      return;
    }
    if (event.willCommit) {
      if (value.length < cMask.length) {
        if (warn) {
          this.#app.alert(err);
        }
        event.rc = false;
        return;
      }
      if (!_checkValidity(value, cMask)) {
        if (warn) {
          this.#app.alert(err);
        }
        event.rc = false;
        return;
      }
      event.value += cMask.substring(value.length);
      return;
    }
    if (value.length < cMask.length) {
      cMask = cMask.substring(0, value.length);
    }
    if (!_checkValidity(value, cMask)) {
      if (warn) {
        this.#app.alert(err);
      }
      event.rc = false;
    }
  }
  AFSpecial_Keystroke(psf) {
    const event = globalThis.event;
    psf = this.AFMakeNumber(psf);
    let value = this.AFMergeChange(event);
    let formatStr, secondFormatStr;
    switch (psf) {
      case 0:
        formatStr = "99999";
        break;
      case 1:
        formatStr = "99999-9999";
        break;
      case 2:
        formatStr = "999-9999";
        secondFormatStr = "(999) 999-9999";
        break;
      case 3:
        formatStr = "999-99-9999";
        break;
      default:
        throw new Error("Invalid psf in AFSpecial_Keystroke");
    }
    const formats = secondFormatStr ? [formatStr, secondFormatStr] : [formatStr];
    for (const format of formats) {
      this.#AFSpecial_KeystrokeEx_helper(format, value, false);
      if (event.rc) {
        return;
      }
      event.rc = true;
    }
    const re = /[-()\s]+/g;
    value = value.replaceAll(re, "");
    for (const format of formats) {
      this.#AFSpecial_KeystrokeEx_helper(format.replaceAll(re, ""), value, false);
      if (event.rc) {
        return;
      }
      event.rc = true;
    }
    this.AFSpecial_KeystrokeEx((secondFormatStr && value.match(/\d/g) || []).length > 7 ? secondFormatStr : formatStr);
  }
  AFTime_FormatEx(cFormat) {
    this.AFDate_FormatEx(cFormat);
  }
  AFTime_Format(pdf) {
    this.AFDate_FormatEx(TimeFormats[pdf] ?? pdf);
  }
  AFTime_KeystrokeEx(cFormat) {
    this.AFDate_KeystrokeEx(cFormat);
  }
  AFTime_Keystroke(pdf) {
    if (pdf >= 0 && pdf < TimeFormats.length) {
      this.AFDate_KeystrokeEx(TimeFormats[pdf]);
    }
  }
  eMailValidate(str) {
    return this.#emailRegex.test(str);
  }
  AFExactMatch(rePatterns, str) {
    return rePatterns instanceof RegExp ? str.match(rePatterns)?.[0] === str || 0 : rePatterns.findIndex(re => str.match(re)?.[0] === str) + 1;
  }
}

;// ./src/scripting_api/console.js
class Console {
  #send;
  constructor({
    send
  }) {
    this.#send = send;
  }
  clear() {
    this.#send({
      id: "clear"
    });
  }
  hide() {}
  println(msg) {
    if (typeof msg !== "string") {
      try {
        msg = JSON.stringify(msg);
      } catch {
        msg = msg.toString?.() || "[Unserializable object]";
      }
    }
    this.#send({
      command: "println",
      value: `PDF.js Console:: ${msg}`
    });
  }
  show() {}
}

;// ./src/scripting_api/util.js
class Util {
  #createDateActionsBound = this.#createDateActions.bind(this);
  #createScandDataBound = this.#createScandData.bind(this);
  #dateActionsCache = null;
  #scandCache = null;
  #months = ["January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"];
  #days = ["Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"];
  MILLISECONDS_IN_DAY = 86400000;
  MILLISECONDS_IN_WEEK = 604800000;
  printf(...args) {
    if (args.length === 0) {
      throw new Error("Invalid number of params in printf");
    }
    if (typeof args[0] !== "string") {
      throw new TypeError("First argument of printf must be a string");
    }
    const pattern = /%(,[0-4])?([+ 0#]+)?(\d+)?(\.\d+)?(.)/g;
    const PLUS = 1;
    const SPACE = 2;
    const ZERO = 4;
    const HASH = 8;
    let i = 0;
    return args[0].replaceAll(pattern, function (_, nDecSep, cFlags, nWidth, nPrecision, cConvChar) {
      if (cConvChar !== "d" && cConvChar !== "f" && cConvChar !== "s" && cConvChar !== "x") {
        const buf = ["%"];
        for (const str of [nDecSep, cFlags, nWidth, nPrecision, cConvChar]) {
          if (str) {
            buf.push(str);
          }
        }
        return buf.join("");
      }
      i++;
      if (i === args.length) {
        throw new Error("Not enough arguments in printf");
      }
      const arg = args[i];
      if (cConvChar === "s") {
        return arg.toString();
      }
      let flags = 0;
      if (cFlags) {
        for (const flag of cFlags) {
          switch (flag) {
            case "+":
              flags |= PLUS;
              break;
            case " ":
              flags |= SPACE;
              break;
            case "0":
              flags |= ZERO;
              break;
            case "#":
              flags |= HASH;
              break;
          }
        }
      }
      cFlags = flags;
      nWidth &&= parseInt(nWidth);
      let intPart = Math.trunc(arg);
      if (cConvChar === "x") {
        let hex = Math.abs(intPart).toString(16).toUpperCase();
        if (nWidth !== undefined) {
          hex = hex.padStart(nWidth, cFlags & ZERO ? "0" : " ");
        }
        if (cFlags & HASH) {
          hex = `0x${hex}`;
        }
        return hex;
      }
      nPrecision &&= parseInt(nPrecision.substring(1));
      nDecSep = nDecSep ? nDecSep.substring(1) : "0";
      const separators = {
        0: [",", "."],
        1: ["", "."],
        2: [".", ","],
        3: ["", ","],
        4: ["'", "."]
      };
      const [thousandSep, decimalSep] = separators[nDecSep];
      let decPart = "";
      if (cConvChar === "f") {
        decPart = nPrecision !== undefined ? Math.abs(arg - intPart).toFixed(nPrecision) : Math.abs(arg - intPart).toString();
        if (decPart.length > 2) {
          if (/^1\.0+$/.test(decPart)) {
            intPart += Math.sign(arg);
            decPart = `${decimalSep}${decPart.split(".")[1]}`;
          } else {
            decPart = `${decimalSep}${decPart.substring(2)}`;
          }
        } else {
          if (decPart === "1") {
            intPart += Math.sign(arg);
          }
          decPart = cFlags & HASH ? "." : "";
        }
      }
      let sign = "";
      if (intPart < 0 || cConvChar === "f" && arg < 0) {
        sign = "-";
        intPart = -intPart;
      } else if (cFlags & PLUS) {
        sign = "+";
      } else if (cFlags & SPACE) {
        sign = " ";
      }
      if (thousandSep && intPart >= 1000) {
        const buf = [];
        while (true) {
          buf.push((intPart % 1000).toString().padStart(3, "0"));
          intPart = Math.trunc(intPart / 1000);
          if (intPart < 1000) {
            buf.push(intPart.toString());
            break;
          }
        }
        intPart = buf.reverse().join(thousandSep);
      } else {
        intPart = intPart.toString();
      }
      let n = `${intPart}${decPart}`;
      if (nWidth !== undefined) {
        n = n.padStart(nWidth - sign.length, cFlags & ZERO ? "0" : " ");
      }
      return `${sign}${n}`;
    });
  }
  iconStreamFromIcon() {}
  printd(cFormat, oDate) {
    switch (cFormat) {
      case 0:
        return this.printd("D:yyyymmddHHMMss", oDate);
      case 1:
        return this.printd("yyyy.mm.dd HH:MM:ss", oDate);
      case 2:
        return this.printd("m/d/yy h:MM:ss tt", oDate);
    }
    const handlers = {
      mmmm: data => this.#months[data.month],
      mmm: data => this.#months[data.month].substring(0, 3),
      mm: data => (data.month + 1).toString().padStart(2, "0"),
      m: data => (data.month + 1).toString(),
      dddd: data => this.#days[data.dayOfWeek],
      ddd: data => this.#days[data.dayOfWeek].substring(0, 3),
      dd: data => data.day.toString().padStart(2, "0"),
      d: data => data.day.toString(),
      yyyy: data => data.year.toString().padStart(4, "0"),
      yy: data => (data.year % 100).toString().padStart(2, "0"),
      HH: data => data.hours.toString().padStart(2, "0"),
      H: data => data.hours.toString(),
      hh: data => (1 + (data.hours + 11) % 12).toString().padStart(2, "0"),
      h: data => (1 + (data.hours + 11) % 12).toString(),
      MM: data => data.minutes.toString().padStart(2, "0"),
      M: data => data.minutes.toString(),
      ss: data => data.seconds.toString().padStart(2, "0"),
      s: data => data.seconds.toString(),
      tt: data => data.hours < 12 ? "am" : "pm",
      t: data => data.hours < 12 ? "a" : "p"
    };
    const data = {
      year: oDate.getFullYear(),
      month: oDate.getMonth(),
      day: oDate.getDate(),
      dayOfWeek: oDate.getDay(),
      hours: oDate.getHours(),
      minutes: oDate.getMinutes(),
      seconds: oDate.getSeconds()
    };
    const patterns = /(mmmm|mmm|mm|m|dddd|ddd|dd|d|yyyy|yy|HH|H|hh|h|MM|M|ss|s|tt|t|\\.)/g;
    return cFormat.replaceAll(patterns, (_, pattern) => pattern in handlers ? handlers[pattern](data) : pattern.charAt(1));
  }
  printx(cFormat, cSource) {
    cSource = (cSource ?? "").toString();
    const handlers = [x => x, x => x.toUpperCase(), x => x.toLowerCase()];
    const buf = [];
    let i = 0;
    const ii = cSource.length;
    let currCase = handlers[0];
    let escaped = false;
    for (const command of cFormat) {
      if (escaped) {
        buf.push(command);
        escaped = false;
        continue;
      }
      if (i >= ii) {
        break;
      }
      switch (command) {
        case "?":
          buf.push(currCase(cSource.charAt(i++)));
          break;
        case "X":
          while (i < ii) {
            const char = cSource.charAt(i++);
            if ("a" <= char && char <= "z" || "A" <= char && char <= "Z" || "0" <= char && char <= "9") {
              buf.push(currCase(char));
              break;
            }
          }
          break;
        case "A":
          while (i < ii) {
            const char = cSource.charAt(i++);
            if ("a" <= char && char <= "z" || "A" <= char && char <= "Z") {
              buf.push(currCase(char));
              break;
            }
          }
          break;
        case "9":
          while (i < ii) {
            const char = cSource.charAt(i++);
            if ("0" <= char && char <= "9") {
              buf.push(char);
              break;
            }
          }
          break;
        case "*":
          while (i < ii) {
            buf.push(currCase(cSource.charAt(i++)));
          }
          break;
        case "\\":
          escaped = true;
          break;
        case ">":
          currCase = handlers[1];
          break;
        case "<":
          currCase = handlers[2];
          break;
        case "=":
          currCase = handlers[0];
          break;
        default:
          buf.push(command);
      }
    }
    return buf.join("");
  }
  #createDateActions(cFormat) {
    const actions = [];
    cFormat.replaceAll(/(d+)|(m+)|(y+)|(H+)|(M+)|(s+)/g, function (_, d, m, y, H, M, s) {
      if (d) {
        actions.push((n, data) => {
          if (n >= 1 && n <= 31) {
            data.day = n;
            return true;
          }
          return false;
        });
      } else if (m) {
        actions.push((n, data) => {
          if (n >= 1 && n <= 12) {
            data.month = n - 1;
            return true;
          }
          return false;
        });
      } else if (y) {
        actions.push((n, data) => {
          if (n < 50) {
            n += 2000;
          } else if (n < 100) {
            n += 1900;
          }
          data.year = n;
          return true;
        });
      } else if (H) {
        actions.push((n, data) => {
          if (n >= 0 && n <= 23) {
            data.hours = n;
            return true;
          }
          return false;
        });
      } else if (M) {
        actions.push((n, data) => {
          if (n >= 0 && n <= 59) {
            data.minutes = n;
            return true;
          }
          return false;
        });
      } else if (s) {
        actions.push((n, data) => {
          if (n >= 0 && n <= 59) {
            data.seconds = n;
            return true;
          }
          return false;
        });
      }
      return "";
    });
    return actions;
  }
  #tryToGuessDate(cFormat, cDate) {
    const actions = (this.#dateActionsCache ??= new Map()).getOrInsertComputed(cFormat, this.#createDateActionsBound);
    const number = /\d+/g;
    let i = 0;
    let array;
    const data = {
      year: new Date().getFullYear(),
      month: 0,
      day: 1,
      hours: 12,
      minutes: 0,
      seconds: 0
    };
    while ((array = number.exec(cDate)) !== null) {
      if (i < actions.length) {
        if (!actions[i++](parseInt(array[0]), data)) {
          return null;
        }
      } else {
        break;
      }
    }
    if (i === 0) {
      return null;
    }
    return new Date(data.year, data.month, data.day, data.hours, data.minutes, data.seconds);
  }
  #createScandData(cFormat) {
    const months = this.#months,
      days = this.#days;
    const handlers = {
      mmmm: {
        pattern: `(${months.join("|")})`,
        action: (value, data) => {
          data.month = months.indexOf(value);
        }
      },
      mmm: {
        pattern: `(${months.map(month => month.substring(0, 3)).join("|")})`,
        action: (value, data) => {
          data.month = months.findIndex(month => month.substring(0, 3) === value);
        }
      },
      mm: {
        pattern: `(\\d{2})`,
        action: (value, data) => {
          data.month = parseInt(value) - 1;
        }
      },
      m: {
        pattern: `(\\d{1,2})`,
        action: (value, data) => {
          data.month = parseInt(value) - 1;
        }
      },
      dddd: {
        pattern: `(${days.join("|")})`,
        action: (value, data) => {
          data.day = days.indexOf(value);
        }
      },
      ddd: {
        pattern: `(${days.map(day => day.substring(0, 3)).join("|")})`,
        action: (value, data) => {
          data.day = days.findIndex(day => day.substring(0, 3) === value);
        }
      },
      dd: {
        pattern: "(\\d{2})",
        action: (value, data) => {
          data.day = parseInt(value);
        }
      },
      d: {
        pattern: "(\\d{1,2})",
        action: (value, data) => {
          data.day = parseInt(value);
        }
      },
      yyyy: {
        pattern: "(\\d{4})",
        action: (value, data) => {
          data.year = parseInt(value);
        }
      },
      yy: {
        pattern: "(\\d{2})",
        action: (value, data) => {
          data.year = 2000 + parseInt(value);
        }
      },
      HH: {
        pattern: "(\\d{2})",
        action: (value, data) => {
          data.hours = parseInt(value);
        }
      },
      H: {
        pattern: "(\\d{1,2})",
        action: (value, data) => {
          data.hours = parseInt(value);
        }
      },
      hh: {
        pattern: "(\\d{2})",
        action: (value, data) => {
          data.hours = parseInt(value);
        }
      },
      h: {
        pattern: "(\\d{1,2})",
        action: (value, data) => {
          data.hours = parseInt(value);
        }
      },
      MM: {
        pattern: "(\\d{2})",
        action: (value, data) => {
          data.minutes = parseInt(value);
        }
      },
      M: {
        pattern: "(\\d{1,2})",
        action: (value, data) => {
          data.minutes = parseInt(value);
        }
      },
      ss: {
        pattern: "(\\d{2})",
        action: (value, data) => {
          data.seconds = parseInt(value);
        }
      },
      s: {
        pattern: "(\\d{1,2})",
        action: (value, data) => {
          data.seconds = parseInt(value);
        }
      },
      tt: {
        pattern: "([aApP][mM])",
        action: (value, data) => {
          const char = value.charAt(0);
          data.am = char === "a" || char === "A";
        }
      },
      t: {
        pattern: "([aApP])",
        action: (value, data) => {
          data.am = value === "a" || value === "A";
        }
      }
    };
    const escapedFormat = cFormat.replaceAll(/[.*+\-?^${}()|[\]\\]/g, "\\$&");
    const patterns = /(mmmm|mmm|mm|m|dddd|ddd|dd|d|yyyy|yy|HH|H|hh|h|MM|M|ss|s|tt|t)/g;
    const actions = [];
    const re = escapedFormat.replaceAll(patterns, function (_, patternElement) {
      const {
        pattern,
        action
      } = handlers[patternElement];
      actions.push(action);
      return pattern.includes(",") ? `(?=${pattern})\\${actions.length}` : pattern;
    });
    return [new RegExp(`^${re}$`), actions];
  }
  scand(cFormat, cDate) {
    if (typeof cDate !== "string") {
      return new Date(cDate);
    }
    if (cDate === "") {
      return new Date();
    }
    switch (cFormat) {
      case 0:
        return this.scand("D:yyyymmddHHMMss", cDate);
      case 1:
        return this.scand("yyyy.mm.dd HH:MM:ss", cDate);
      case 2:
        return this.scand("m/d/yy h:MM:ss tt", cDate);
    }
    const [regex, actions] = (this.#scandCache ??= new Map()).getOrInsertComputed(cFormat, this.#createScandDataBound);
    const matches = regex.exec(cDate);
    if (!matches || matches.length !== actions.length + 1) {
      return this.#tryToGuessDate(cFormat, cDate);
    }
    const data = {
      year: 2000,
      month: 0,
      day: 1,
      hours: 0,
      minutes: 0,
      seconds: 0,
      am: null
    };
    actions.forEach((action, i) => action(matches[i + 1], data));
    if (data.am !== null) {
      data.hours = data.hours % 12 + (data.am ? 0 : 12);
    }
    return new Date(data.year, data.month, data.day, data.hours, data.minutes, data.seconds);
  }
  spansToXML() {}
  stringFromStream() {}
  xmlToSpans() {}
}

;// ./src/scripting_api/initialization.js











function initSandbox(params) {
  delete globalThis.pdfjsScripting;
  const externalCall = globalThis.callExternalFunction;
  delete globalThis.callExternalFunction;
  const globalEval = globalThis.eval.bind(globalThis);
  const send = data => externalCall("send", [data]);
  const {
    data
  } = params;
  const appObjects = Object.create(null);
  const userActivationData = {
    userActivation: false,
    disablePrinting: false,
    disableSaving: false
  };
  let eventDispatcher = null;
  const doc = new Doc({
    ...data.docInfo,
    send,
    globalEval,
    appObjects,
    userActivationData,
    calculateNow: () => eventDispatcher.calculateNow()
  });
  eventDispatcher = new EventDispatcher(doc, data.calculationOrder, appObjects, externalCall, userActivationData);
  const app = new App({
    ...data.appInfo,
    send,
    globalEval,
    externalCall,
    doc,
    userActivationData
  });
  const util = new Util();
  const getTerminalChildren = fieldName => DocInternals.getTerminalChildren(doc, fieldName);
  for (const [name, objs] of createMap(data.objects)) {
    const annotations = [];
    let container = null;
    for (const obj of objs) {
      if (obj.type !== "") {
        annotations.push(obj);
      } else {
        container = obj;
      }
    }
    let obj = container;
    if (annotations.length > 0) {
      obj = annotations[0];
      obj.send = send;
    }
    obj.globalEval = globalEval;
    obj.sendError = send;
    obj.fieldPath = name;
    obj.appObjects = appObjects;
    obj.util = util;
    obj.getTerminalChildren = getTerminalChildren;
    const field = exposeObject(new Field(obj, annotations.slice(1)));
    DocInternals.addField(doc, name, field);
    for (const object of objs) {
      appObjects[object.id] = field;
    }
    if (container) {
      appObjects[container.id] = field;
    }
  }
  const color = exposeObject(new Color(), true);
  globalThis.event = null;
  globalThis.global = Object.create(null);
  globalThis.app = exposeObject(app, true);
  globalThis.color = color;
  globalThis.console = exposeObject(new Console({
    send
  }), true);
  globalThis.util = exposeObject(util, true);
  globalThis.border = Border;
  globalThis.cursor = Cursor;
  globalThis.display = Display;
  globalThis.font = Font;
  globalThis.highlight = Highlight;
  globalThis.position = Position;
  globalThis.scaleHow = ScaleHow;
  globalThis.scaleWhen = ScaleWhen;
  globalThis.style = Style;
  globalThis.trans = Trans;
  globalThis.zoomtype = ZoomType;
  globalThis.ADBE = {
    Reader_Value_Asked: true,
    Viewer_Value_Asked: true
  };
  const aform = new AForm(doc, app, util, color);
  for (const name of Object.getOwnPropertyNames(AForm.prototype)) {
    if (name !== "constructor") {
      globalThis[name] = aform[name].bind(aform);
    }
  }
  for (const [name, value] of Object.entries(GlobalConstants)) {
    Object.defineProperty(globalThis, name, {
      value,
      writable: false
    });
  }
  Object.defineProperties(globalThis, {
    ColorConvert: {
      value: color.convert.bind(color),
      writable: true
    },
    ColorEqual: {
      value: color.equal.bind(color),
      writable: true
    }
  });
  const properties = Object.create(null);
  for (const name of Object.getOwnPropertyNames(Doc.prototype)) {
    if (name === "constructor") {
      continue;
    }
    const descriptor = Object.getOwnPropertyDescriptor(Doc.prototype, name);
    if (descriptor.get) {
      properties[name] = {
        get: descriptor.get.bind(doc),
        set: descriptor.set.bind(doc)
      };
    } else {
      properties[name] = {
        value: Doc.prototype[name].bind(doc)
      };
    }
  }
  Object.defineProperties(globalThis, properties);
  useNativeToStringForPublicFunctions();
  const functions = {
    dispatchEvent: event => eventDispatcher.dispatch(event),
    timeoutCb: timeout => AppInternals.evalCallback(app, timeout)
  };
  return (name, args) => {
    try {
      functions[name](args);
    } catch (error) {
      send(serializeError(error));
    }
  };
}

;// ./src/pdf.scripting.js

globalThis.pdfjsScripting = {
  initSandbox: initSandbox
};
/******/ })()
;