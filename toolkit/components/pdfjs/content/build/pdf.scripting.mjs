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
 *
 * @licend The above is the entire license notice for the
 * JavaScript code in this page
 */


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

;// ./src/scripting_api/common.js
const FieldType = {
  none: 0,
  number: 1,
  percent: 2,
  date: 3,
  time: 4
};
function createActionsMap(actions) {
  const actionsMap = new Map();
  if (actions) {
    for (const [eventType, actionsForEvent] of Object.entries(actions)) {
      actionsMap.set(eventType, actionsForEvent);
    }
  }
  return actionsMap;
}
function getFieldType(actions) {
  let format = actions.get("Format");
  if (!format) {
    return FieldType.none;
  }
  format = format[0];
  format = format.trim();
  if (format.startsWith("AFNumber_")) {
    return FieldType.number;
  }
  if (format.startsWith("AFPercent_")) {
    return FieldType.percent;
  }
  if (format.startsWith("AFDate_")) {
    return FieldType.date;
  }
  if (format.startsWith("AFTime_")) {
    return FieldType.time;
  }
  return FieldType.none;
}

;// ./src/shared/scripting_utils.js
function makeColorComp(n) {
  return Math.floor(Math.max(0, Math.min(1, n)) * 255).toString(16).padStart(2, "0");
}
function scaleAndClamp(x) {
  return Math.max(0, Math.min(255, 255 * x));
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

;// ./src/scripting_api/pdf_object.js
class PDFObject {
  constructor(data) {
    this._expandos = Object.create(null);
    this._send = data.send || null;
    this._id = data.id || null;
  }
}

;// ./src/scripting_api/color.js


class Color extends PDFObject {
  constructor() {
    super({});
    this.transparent = ["T"];
    this.black = ["G", 0];
    this.white = ["G", 1];
    this.red = ["RGB", 1, 0, 0];
    this.green = ["RGB", 0, 1, 0];
    this.blue = ["RGB", 0, 0, 1];
    this.cyan = ["CMYK", 1, 0, 0, 0];
    this.magenta = ["CMYK", 0, 1, 0, 0];
    this.yellow = ["CMYK", 0, 0, 1, 0];
    this.dkGray = ["G", 0.25];
    this.gray = ["G", 0.5];
    this.ltGray = ["G", 0.75];
  }
  static _isValidSpace(cColorSpace) {
    return typeof cColorSpace === "string" && (cColorSpace === "T" || cColorSpace === "G" || cColorSpace === "RGB" || cColorSpace === "CMYK");
  }
  static _isValidColor(colorArray) {
    if (!Array.isArray(colorArray) || colorArray.length === 0) {
      return false;
    }
    const space = colorArray[0];
    if (!Color._isValidSpace(space)) {
      return false;
    }
    switch (space) {
      case "T":
        if (colorArray.length !== 1) {
          return false;
        }
        break;
      case "G":
        if (colorArray.length !== 2) {
          return false;
        }
        break;
      case "RGB":
        if (colorArray.length !== 4) {
          return false;
        }
        break;
      case "CMYK":
        if (colorArray.length !== 5) {
          return false;
        }
        break;
      default:
        return false;
    }
    return colorArray.slice(1).every(c => typeof c === "number" && c >= 0 && c <= 1);
  }
  static _getCorrectColor(colorArray) {
    return Color._isValidColor(colorArray) ? colorArray : ["G", 0];
  }
  convert(colorArray, cColorSpace) {
    if (!Color._isValidSpace(cColorSpace)) {
      return this.black;
    }
    if (cColorSpace === "T") {
      return ["T"];
    }
    colorArray = Color._getCorrectColor(colorArray);
    if (colorArray[0] === cColorSpace) {
      return colorArray;
    }
    if (colorArray[0] === "T") {
      return this.convert(this.black, cColorSpace);
    }
    return ColorConverters[`${colorArray[0]}_${cColorSpace}`](colorArray.slice(1));
  }
  equal(colorArray1, colorArray2) {
    colorArray1 = Color._getCorrectColor(colorArray1);
    colorArray2 = Color._getCorrectColor(colorArray2);
    if (colorArray1[0] === "T" || colorArray2[0] === "T") {
      return colorArray1[0] === "T" && colorArray2[0] === "T";
    }
    if (colorArray1[0] !== colorArray2[0]) {
      colorArray2 = this.convert(colorArray2, colorArray1[0]);
    }
    return colorArray1.slice(1).every((c, i) => c === colorArray2[i + 1]);
  }
}

;// ./src/scripting_api/app_utils.js
const VIEWER_TYPE = "PDF.js";
const VIEWER_VARIATION = "Full";
const VIEWER_VERSION = 21.00720099;
const FORMS_VERSION = 21.00720099;
const USERACTIVATION_CALLBACKID = 0;
const USERACTIVATION_MAXTIME_VALIDITY = 5000;
function serializeError(error) {
  const value = `${error.toString()}\n${error.stack}`;
  return {
    command: "error",
    value
  };
}

;// ./src/scripting_api/field.js




class Field extends PDFObject {
  constructor(data) {
    super(data);
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
    this.currentValueIndices = data.currentValueIndices;
    this.defaultStyle = data.defaultStyle;
    this.defaultValue = data.defaultValue;
    this.doNotScroll = data.doNotScroll;
    this.doNotSpellCheck = data.doNotSpellCheck;
    this.delay = data.delay;
    this.display = data.display;
    this.doc = data.doc.wrapped;
    this.editable = data.editable;
    this.exportValues = data.exportValues;
    this.fileSelect = data.fileSelect;
    this.hidden = data.hidden;
    this.highlight = data.highlight;
    this.lineWidth = data.lineWidth;
    this.multiline = data.multiline;
    this.multipleSelection = !!data.multipleSelection;
    this.name = data.name;
    this.password = data.password;
    this.print = data.print;
    this.radiosInUnison = data.radiosInUnison;
    this.readonly = data.readonly;
    this.rect = data.rect;
    this.required = data.required;
    this.richText = data.richText;
    this.richValue = data.richValue;
    this.style = data.style;
    this.submitName = data.submitName;
    this.textFont = data.textFont;
    this.textSize = data.textSize;
    this.type = data.type;
    this.userName = data.userName;
    this._actions = createActionsMap(data.actions);
    this._browseForFileToSubmit = data.browseForFileToSubmit || null;
    this._buttonCaption = null;
    this._buttonIcon = null;
    this._charLimit = data.charLimit;
    this._children = null;
    this._currentValueIndices = data.currentValueIndices || 0;
    this._document = data.doc;
    this._fieldPath = data.fieldPath;
    this._fillColor = data.fillColor || ["T"];
    this._isChoice = Array.isArray(data.items);
    this._items = data.items || [];
    this._hasValue = data.hasOwnProperty("value");
    this._page = data.page || 0;
    this._strokeColor = data.strokeColor || ["G", 0];
    this._textColor = data.textColor || ["G", 0];
    this._value = null;
    this._kidIds = data.kidIds || null;
    this._fieldType = getFieldType(this._actions);
    this._siblings = data.siblings || null;
    this._rotation = data.rotation || 0;
    this._globalEval = data.globalEval;
    this._appObjects = data.appObjects;
    this.value = data.value || "";
  }
  get currentValueIndices() {
    if (!this._isChoice) {
      return 0;
    }
    return this._currentValueIndices;
  }
  set currentValueIndices(indices) {
    if (!this._isChoice) {
      return;
    }
    if (!Array.isArray(indices)) {
      indices = [indices];
    }
    if (!indices.every(i => typeof i === "number" && Number.isInteger(i) && i >= 0 && i < this.numItems)) {
      return;
    }
    indices.sort();
    if (this.multipleSelection) {
      this._currentValueIndices = indices;
      this._value = [];
      indices.forEach(i => {
        this._value.push(this._items[i].displayValue);
      });
    } else if (indices.length > 0) {
      indices = indices.splice(1, indices.length - 1);
      this._currentValueIndices = indices[0];
      this._value = this._items[this._currentValueIndices];
    }
    this._send({
      id: this._id,
      indices
    });
  }
  get fillColor() {
    return this._fillColor;
  }
  set fillColor(color) {
    if (Color._isValidColor(color)) {
      this._fillColor = color;
    }
  }
  get bgColor() {
    return this.fillColor;
  }
  set bgColor(color) {
    this.fillColor = color;
  }
  get charLimit() {
    return this._charLimit;
  }
  set charLimit(limit) {
    if (typeof limit !== "number") {
      throw new Error("Invalid argument value");
    }
    this._charLimit = Math.max(0, Math.floor(limit));
  }
  get numItems() {
    if (!this._isChoice) {
      throw new Error("Not a choice widget");
    }
    return this._items.length;
  }
  set numItems(_) {
    throw new Error("field.numItems is read-only");
  }
  get strokeColor() {
    return this._strokeColor;
  }
  set strokeColor(color) {
    if (Color._isValidColor(color)) {
      this._strokeColor = color;
    }
  }
  get borderColor() {
    return this.strokeColor;
  }
  set borderColor(color) {
    this.strokeColor = color;
  }
  get page() {
    return this._page;
  }
  set page(_) {
    throw new Error("field.page is read-only");
  }
  get rotation() {
    return this._rotation;
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
    this._rotation = angle;
  }
  get textColor() {
    return this._textColor;
  }
  set textColor(color) {
    if (Color._isValidColor(color)) {
      this._textColor = color;
    }
  }
  get fgColor() {
    return this.textColor;
  }
  set fgColor(color) {
    this.textColor = color;
  }
  get value() {
    return this._value;
  }
  set value(value) {
    if (this._isChoice) {
      this._setChoiceValue(value);
      return;
    }
    if (value === "" || typeof value !== "string" || this._fieldType >= FieldType.date) {
      this._originalValue = undefined;
      this._value = value;
      return;
    }
    this._originalValue = value;
    const _value = value.trim().replace(",", ".");
    this._value = !isNaN(_value) ? parseFloat(_value) : value;
  }
  _getValue() {
    return this._originalValue ?? this.value;
  }
  _setChoiceValue(value) {
    if (this.multipleSelection) {
      if (!Array.isArray(value)) {
        value = [value];
      }
      const values = new Set(value);
      if (Array.isArray(this._currentValueIndices)) {
        this._currentValueIndices.length = 0;
        this._value.length = 0;
      } else {
        this._currentValueIndices = [];
        this._value = [];
      }
      this._items.forEach((item, i) => {
        if (values.has(item.exportValue)) {
          this._currentValueIndices.push(i);
          this._value.push(item.exportValue);
        }
      });
    } else {
      if (Array.isArray(value)) {
        value = value[0];
      }
      const index = this._items.findIndex(({
        exportValue
      }) => value === exportValue);
      if (index !== -1) {
        this._currentValueIndices = index;
        this._value = this._items[index].exportValue;
      }
    }
  }
  get valueAsString() {
    return (this._value ?? "").toString();
  }
  set valueAsString(_) {}
  browseForFileToSubmit() {
    if (this._browseForFileToSubmit) {
      this._browseForFileToSubmit();
    }
  }
  buttonGetCaption(nFace = 0) {
    if (this._buttonCaption) {
      return this._buttonCaption[nFace];
    }
    return "";
  }
  buttonGetIcon(nFace = 0) {
    if (this._buttonIcon) {
      return this._buttonIcon[nFace];
    }
    return null;
  }
  buttonImportIcon(cPath = null, nPave = 0) {}
  buttonSetCaption(cCaption, nFace = 0) {
    if (!this._buttonCaption) {
      this._buttonCaption = ["", "", ""];
    }
    this._buttonCaption[nFace] = cCaption;
  }
  buttonSetIcon(oIcon, nFace = 0) {
    if (!this._buttonIcon) {
      this._buttonIcon = [null, null, null];
    }
    this._buttonIcon[nFace] = oIcon;
  }
  checkThisBox(nWidget, bCheckIt = true) {}
  clearItems() {
    if (!this._isChoice) {
      throw new Error("Not a choice widget");
    }
    this._items = [];
    this._send({
      id: this._id,
      clear: null
    });
  }
  deleteItemAt(nIdx = null) {
    if (!this._isChoice) {
      throw new Error("Not a choice widget");
    }
    if (!this.numItems) {
      return;
    }
    if (nIdx === null) {
      nIdx = Array.isArray(this._currentValueIndices) ? this._currentValueIndices[0] : this._currentValueIndices;
      nIdx ||= 0;
    }
    if (nIdx < 0 || nIdx >= this.numItems) {
      nIdx = this.numItems - 1;
    }
    this._items.splice(nIdx, 1);
    if (Array.isArray(this._currentValueIndices)) {
      let index = this._currentValueIndices.findIndex(i => i >= nIdx);
      if (index !== -1) {
        if (this._currentValueIndices[index] === nIdx) {
          this._currentValueIndices.splice(index, 1);
        }
        for (const ii = this._currentValueIndices.length; index < ii; index++) {
          --this._currentValueIndices[index];
        }
      }
    } else if (this._currentValueIndices === nIdx) {
      this._currentValueIndices = this.numItems > 0 ? 0 : -1;
    } else if (this._currentValueIndices > nIdx) {
      --this._currentValueIndices;
    }
    this._send({
      id: this._id,
      remove: nIdx
    });
  }
  getItemAt(nIdx = -1, bExportValue = false) {
    if (!this._isChoice) {
      throw new Error("Not a choice widget");
    }
    if (nIdx < 0 || nIdx >= this.numItems) {
      nIdx = this.numItems - 1;
    }
    const item = this._items[nIdx];
    return bExportValue ? item.exportValue : item.displayValue;
  }
  getArray() {
    if (this._kidIds) {
      const array = [];
      const fillArrayWithKids = kidIds => {
        for (const id of kidIds) {
          const obj = this._appObjects[id];
          if (!obj) {
            continue;
          }
          if (obj.obj._hasValue) {
            array.push(obj.wrapped);
          }
          if (obj.obj._kidIds) {
            fillArrayWithKids(obj.obj._kidIds);
          }
        }
      };
      fillArrayWithKids(this._kidIds);
      return array;
    }
    if (this._children === null) {
      this._children = this._document.obj._getTerminalChildren(this._fieldPath);
    }
    return this._children;
  }
  getLock() {
    return undefined;
  }
  isBoxChecked(nWidget) {
    return false;
  }
  isDefaultChecked(nWidget) {
    return false;
  }
  insertItemAt(cName, cExport = undefined, nIdx = 0) {
    if (!this._isChoice) {
      throw new Error("Not a choice widget");
    }
    if (!cName) {
      return;
    }
    if (nIdx < 0 || nIdx > this.numItems) {
      nIdx = this.numItems;
    }
    if (this._items.some(({
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
    this._items.splice(nIdx, 0, data);
    if (Array.isArray(this._currentValueIndices)) {
      let index = this._currentValueIndices.findIndex(i => i >= nIdx);
      if (index !== -1) {
        for (const ii = this._currentValueIndices.length; index < ii; index++) {
          ++this._currentValueIndices[index];
        }
      }
    } else if (this._currentValueIndices >= nIdx) {
      ++this._currentValueIndices;
    }
    this._send({
      id: this._id,
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
    if (!(cTrigger in this._actions)) {
      this._actions[cTrigger] = [];
    }
    this._actions[cTrigger].push(cScript);
  }
  setFocus() {
    this._send({
      id: this._id,
      focus: true
    });
  }
  setItems(oArray) {
    if (!this._isChoice) {
      throw new Error("Not a choice widget");
    }
    this._items.length = 0;
    for (const element of oArray) {
      let displayValue, exportValue;
      if (Array.isArray(element)) {
        displayValue = element[0]?.toString() || "";
        exportValue = element[1]?.toString() || "";
      } else {
        displayValue = exportValue = element?.toString() || "";
      }
      this._items.push({
        displayValue,
        exportValue
      });
    }
    this._currentValueIndices = 0;
    this._send({
      id: this._id,
      items: this._items
    });
  }
  setLock() {}
  signatureGetModifications() {}
  signatureGetSeedValue() {}
  signatureInfo() {}
  signatureSetSeedValue() {}
  signatureSign() {}
  signatureValidate() {}
  _isButton() {
    return false;
  }
  _reset() {
    this.value = this.defaultValue;
  }
  _runActions(event) {
    const eventName = event.name;
    if (!this._actions.has(eventName)) {
      return false;
    }
    const actions = this._actions.get(eventName);
    for (const action of actions) {
      try {
        this._globalEval(action);
      } catch (error) {
        const serializedError = serializeError(error);
        serializedError.value = `Error when executing "${eventName}" for field "${this._id}"\n${serializedError.value}`;
        this._send(serializedError);
      }
    }
    return true;
  }
}
class RadioButtonField extends Field {
  constructor(otherButtons, data) {
    super(data);
    this.exportValues = [this.exportValues];
    this._radioIds = [this._id];
    this._radioActions = [this._actions];
    for (const radioData of otherButtons) {
      this.exportValues.push(radioData.exportValues);
      this._radioIds.push(radioData.id);
      this._radioActions.push(createActionsMap(radioData.actions));
      if (this._value === radioData.exportValues) {
        this._id = radioData.id;
      }
    }
    this._hasBeenInitialized = true;
    this._value = data.value || "";
  }
  get _siblings() {
    return this._radioIds.filter(id => id !== this._id);
  }
  set _siblings(_) {}
  get value() {
    return this._value;
  }
  set value(value) {
    if (!this._hasBeenInitialized) {
      return;
    }
    if (value === null || value === undefined) {
      this._value = "";
    }
    const i = this.exportValues.indexOf(value);
    if (0 <= i && i < this._radioIds.length) {
      this._id = this._radioIds[i];
      this._value = value;
    } else if (value === "Off" && this._radioIds.length === 2) {
      const nextI = (1 + this._radioIds.indexOf(this._id)) % 2;
      this._id = this._radioIds[nextI];
      this._value = this.exportValues[nextI];
    }
  }
  checkThisBox(nWidget, bCheckIt = true) {
    if (nWidget < 0 || nWidget >= this._radioIds.length || !bCheckIt) {
      return;
    }
    this._id = this._radioIds[nWidget];
    this._value = this.exportValues[nWidget];
    this._send({
      id: this._id,
      value: this._value
    });
  }
  isBoxChecked(nWidget) {
    return nWidget >= 0 && nWidget < this._radioIds.length && this._id === this._radioIds[nWidget];
  }
  isDefaultChecked(nWidget) {
    return nWidget >= 0 && nWidget < this.exportValues.length && this.defaultValue === this.exportValues[nWidget];
  }
  _getExportValue(state) {
    const i = this._radioIds.indexOf(this._id);
    return this.exportValues[i];
  }
  _runActions(event) {
    const i = this._radioIds.indexOf(this._id);
    this._actions = this._radioActions[i];
    return super._runActions(event);
  }
  _isButton() {
    return true;
  }
}
class CheckboxField extends RadioButtonField {
  get value() {
    return this._value;
  }
  set value(value) {
    if (!value || value === "Off") {
      this._value = "Off";
    } else {
      super.value = value;
    }
  }
  _getExportValue(state) {
    return state ? super._getExportValue(state) : "Off";
  }
  isBoxChecked(nWidget) {
    if (this._value === "Off") {
      return false;
    }
    return super.isBoxChecked(nWidget);
  }
  isDefaultChecked(nWidget) {
    if (this.defaultValue === "Off") {
      return this._value === "Off";
    }
    return super.isDefaultChecked(nWidget);
  }
  checkThisBox(nWidget, bCheckIt = true) {
    if (nWidget < 0 || nWidget >= this._radioIds.length) {
      return;
    }
    this._id = this._radioIds[nWidget];
    this._value = bCheckIt ? this.exportValues[nWidget] : "Off";
    this._send({
      id: this._id,
      value: this._value
    });
  }
}

;// ./src/scripting_api/aform.js

class AForm {
  constructor(document, app, util, color) {
    this._document = document;
    this._app = app;
    this._util = util;
    this._color = color;
    this._dateFormats = ["m/d", "m/d/yy", "mm/dd/yy", "mm/yy", "d-mmm", "d-mmm-yy", "dd-mmm-yy", "yy-mm-dd", "mmm-yy", "mmmm-yy", "mmm d, yyyy", "mmmm d, yyyy", "m/d/yy h:MM tt", "m/d/yy HH:MM"];
    this._timeFormats = ["HH:MM", "h:MM tt", "HH:MM:ss", "h:MM:ss tt"];
    this._emailRegex = new RegExp("^[a-zA-Z0-9.!#$%&'*+\\/=?^_`{|}~-]+" + "@[a-zA-Z0-9](?:[a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?" + "(?:\\.[a-zA-Z0-9](?:[a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?)*$");
  }
  _mkTargetName(event) {
    return event.target ? `[ ${event.target.name} ]` : "";
  }
  _parseDate(cFormat, cDate, strict = false) {
    let date = null;
    try {
      date = this._util._scand(cFormat, cDate, strict);
    } catch {}
    if (date) {
      return date;
    }
    if (strict) {
      return null;
    }
    date = Date.parse(cDate);
    return isNaN(date) ? null : new Date(date);
  }
  AFMergeChange(event = globalThis.event) {
    if (event.willCommit) {
      return event.value.toString();
    }
    return this._app._eventDispatcher.mergeChange(event);
  }
  AFParseDateEx(cString, cOrder) {
    return this._parseDate(cOrder, cString);
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
    if (numbers.length === 0) {
      return null;
    }
    return numbers;
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
    if (isNaN(number) || !isFinite(number)) {
      return null;
    }
    return number;
  }
  AFMakeArrayFromList(string) {
    if (typeof string === "string") {
      return string.split(/, ?/g);
    }
    return string;
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
    sepStyle = Math.min(Math.max(0, Math.floor(sepStyle)), 4);
    buf.push("%,", sepStyle, ".", nDec.toString(), "f");
    if (!bCurrencyPrepend) {
      buf.push(strCurrency);
    }
    if (hasParen) {
      buf.push(")");
    }
    if (negStyle === 1 || negStyle === 3) {
      event.target.textColor = sign === 1 ? this._color.black : this._color.red;
    }
    if ((negStyle !== 0 || bCurrencyPrepend) && sign === -1) {
      value = -value;
    }
    const formatStr = buf.join("");
    event.value = this._util.printf(formatStr, value);
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
      pattern = event.willCommit ? /^[+-]?(\d+(,\d*)?|,\d+)$/ : /^[+-]?\d*,?\d*$/;
    } else {
      pattern = event.willCommit ? /^[+-]?(\d+(\.\d*)?|\.\d+)$/ : /^[+-]?\d*\.?\d*$/;
    }
    if (!pattern.test(value)) {
      if (event.willCommit) {
        const err = `${GlobalConstants.IDS_INVALID_VALUE} ${this._mkTargetName(event)}`;
        this._app.alert(err);
      }
      event.rc = false;
    }
    if (event.willCommit && sepStyle > 1) {
      event.value = parseFloat(value.replace(",", "."));
    }
  }
  AFPercent_Format(nDec, sepStyle, percentPrepend = false) {
    if (typeof nDec !== "number") {
      return;
    }
    if (typeof sepStyle !== "number") {
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
    sepStyle = Math.min(Math.max(0, Math.floor(sepStyle)), 4);
    let value = this.AFMakeNumber(event.value);
    if (value === null) {
      event.value = "%";
      return;
    }
    const formatStr = `%,${sepStyle}.${nDec}f`;
    value = this._util.printf(formatStr, value * 100);
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
    const date = this._parseDate(cFormat, value);
    if (date !== null) {
      event.value = this._util.printd(cFormat, date);
    }
  }
  AFDate_Format(pdf) {
    if (pdf >= 0 && pdf < this._dateFormats.length) {
      this.AFDate_FormatEx(this._dateFormats[pdf]);
    }
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
    if (this._parseDate(cFormat, value, true) === null) {
      const invalid = GlobalConstants.IDS_INVALID_DATE;
      const invalid2 = GlobalConstants.IDS_INVALID_DATE2;
      const err = `${invalid} ${this._mkTargetName(event)}${invalid2}${cFormat}`;
      this._app.alert(err);
      event.rc = false;
    }
  }
  AFDate_Keystroke(pdf) {
    if (pdf >= 0 && pdf < this._dateFormats.length) {
      this.AFDate_KeystrokeEx(this._dateFormats[pdf]);
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
        err = this._util.printf(GlobalConstants.IDS_GT_AND_LT, nGreaterThan, nLessThan);
      }
    } else if (bGreaterThan) {
      if (value < nGreaterThan) {
        err = this._util.printf(GlobalConstants.IDS_GREATER_THAN, nGreaterThan);
      }
    } else if (value > nLessThan) {
      err = this._util.printf(GlobalConstants.IDS_LESS_THAN, nLessThan);
    }
    if (err) {
      this._app.alert(err);
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
      AVG: args => args.reduce((acc, value) => acc + value, 0) / args.length,
      SUM: args => args.reduce((acc, value) => acc + value, 0),
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
      const field = this._document.getField(cField);
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
        formatStr = this._util.printx("9999999999", event.value).length >= 10 ? "(999) 999-9999" : "999-9999";
        break;
      case 3:
        formatStr = "999-99-9999";
        break;
      default:
        throw new Error("Invalid psf in AFSpecial_Format");
    }
    event.value = this._util.printx(formatStr, event.value);
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
        this._app.alert(err);
      }
      event.rc = false;
      return;
    }
    if (event.willCommit) {
      if (value.length < cMask.length) {
        if (warn) {
          this._app.alert(err);
        }
        event.rc = false;
        return;
      }
      if (!_checkValidity(value, cMask)) {
        if (warn) {
          this._app.alert(err);
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
        this._app.alert(err);
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
    const re = /([-()]|\s)+/g;
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
    if (pdf >= 0 && pdf < this._timeFormats.length) {
      this.AFDate_FormatEx(this._timeFormats[pdf]);
    }
  }
  AFTime_KeystrokeEx(cFormat) {
    this.AFDate_KeystrokeEx(cFormat);
  }
  AFTime_Keystroke(pdf) {
    if (pdf >= 0 && pdf < this._timeFormats.length) {
      this.AFDate_KeystrokeEx(this._timeFormats[pdf]);
    }
  }
  eMailValidate(str) {
    return this._emailRegex.test(str);
  }
  AFExactMatch(rePatterns, str) {
    if (rePatterns instanceof RegExp) {
      return str.match(rePatterns)?.[0] === str || 0;
    }
    return rePatterns.findIndex(re => str.match(re)?.[0] === str) + 1;
  }
}

;// ./src/scripting_api/event.js

class Event {
  constructor(data) {
    this.change = data.change || "";
    this.changeEx = data.changeEx || null;
    this.commitKey = data.commitKey || 0;
    this.fieldFull = data.fieldFull || false;
    this.keyDown = data.keyDown || false;
    this.modifier = data.modifier || false;
    this.name = data.name;
    this.rc = true;
    this.richChange = data.richChange || [];
    this.richChangeEx = data.richChangeEx || [];
    this.richValue = data.richValue || [];
    this.selEnd = data.selEnd ?? -1;
    this.selStart = data.selStart ?? -1;
    this.shift = data.shift || false;
    this.source = data.source || null;
    this.target = data.target || null;
    this.targetName = "";
    this.type = "Field";
    this.value = data.value || "";
    this.willCommit = data.willCommit || false;
  }
}
class EventDispatcher {
  constructor(document, calculationOrder, objects, externalCall) {
    this._document = document;
    this._calculationOrder = calculationOrder;
    this._objects = objects;
    this._externalCall = externalCall;
    this._document.obj._eventDispatcher = this;
    this._isCalculating = false;
  }
  mergeChange(event) {
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
  userActivation() {
    this._document.obj._userActivation = true;
    this._externalCall("setTimeout", [USERACTIVATION_CALLBACKID, USERACTIVATION_MAXTIME_VALIDITY]);
  }
  dispatch(baseEvent) {
    const id = baseEvent.id;
    if (!(id in this._objects)) {
      let event;
      if (id === "doc" || id === "page") {
        event = globalThis.event = new Event(baseEvent);
        event.source = event.target = this._document.wrapped;
        event.name = baseEvent.name;
      }
      if (id === "doc") {
        const eventName = event.name;
        if (eventName === "Open") {
          this.userActivation();
          this._document.obj._initActions();
          this.formatAll();
        }
        if (!["DidPrint", "DidSave", "WillPrint", "WillSave"].includes(eventName)) {
          this.userActivation();
        }
        this._document.obj._dispatchDocEvent(event.name);
      } else if (id === "page") {
        this.userActivation();
        this._document.obj._dispatchPageEvent(event.name, baseEvent.actions, baseEvent.pageNumber);
      } else if (id === "app" && baseEvent.name === "ResetForm") {
        this.userActivation();
        for (const fieldId of baseEvent.ids) {
          const obj = this._objects[fieldId];
          obj?.obj._reset();
        }
      }
      return;
    }
    const name = baseEvent.name;
    const source = this._objects[id];
    const event = globalThis.event = new Event(baseEvent);
    let savedChange;
    this.userActivation();
    if (source.obj._isButton()) {
      source.obj._id = id;
      event.value = source.obj._getExportValue(event.value);
      if (name === "Action") {
        source.obj._value = event.value;
      }
    }
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
        this.runValidation(source, event);
        return;
      case "Action":
        this.runActions(source, source, event, name);
        this.runCalculate(source, event);
        return;
    }
    this.runActions(source, source, event, name);
    if (name !== "Keystroke") {
      return;
    }
    if (event.rc) {
      if (event.willCommit) {
        this.runValidation(source, event);
      } else {
        if (source.obj._isChoice) {
          source.obj.value = savedChange.changeEx;
          source.obj._send({
            id: source.obj._id,
            siblings: source.obj._siblings,
            value: source.obj.value
          });
          return;
        }
        const value = source.obj.value = this.mergeChange(event);
        let selStart, selEnd;
        if (event.selStart !== savedChange.selStart || event.selEnd !== savedChange.selEnd) {
          selStart = event.selStart;
          selEnd = event.selEnd;
        } else {
          selEnd = selStart = savedChange.selStart + event.change.length;
        }
        source.obj._send({
          id: source.obj._id,
          siblings: source.obj._siblings,
          value,
          selRange: [selStart, selEnd]
        });
      }
    } else if (!event.willCommit) {
      source.obj._send({
        id: source.obj._id,
        siblings: source.obj._siblings,
        value: savedChange.value,
        selRange: [savedChange.selStart, savedChange.selEnd]
      });
    } else {
      source.obj._send({
        id: source.obj._id,
        siblings: source.obj._siblings,
        value: "",
        formattedValue: null,
        selRange: [0, 0]
      });
    }
  }
  formatAll() {
    const event = globalThis.event = new Event({});
    for (const source of Object.values(this._objects)) {
      event.value = source.obj._getValue();
      this.runActions(source, source, event, "Format");
    }
  }
  runValidation(source, event) {
    const didValidateRun = this.runActions(source, source, event, "Validate");
    if (event.rc) {
      source.obj.value = event.value;
      this.runCalculate(source, event);
      const savedValue = event.value = source.obj._getValue();
      let formattedValue = null;
      if (this.runActions(source, source, event, "Format")) {
        formattedValue = event.value?.toString?.();
      }
      source.obj._send({
        id: source.obj._id,
        siblings: source.obj._siblings,
        value: savedValue,
        formattedValue
      });
      event.value = savedValue;
    } else if (didValidateRun) {
      source.obj._send({
        id: source.obj._id,
        siblings: source.obj._siblings,
        value: "",
        formattedValue: null,
        selRange: [0, 0],
        focus: true
      });
    }
  }
  runActions(source, target, event, eventName) {
    event.source = source.wrapped;
    event.target = target.wrapped;
    event.name = eventName;
    event.targetName = target.obj.name;
    event.rc = true;
    return target.obj._runActions(event);
  }
  calculateNow() {
    if (!this._calculationOrder || this._isCalculating || !this._document.obj.calculate) {
      return;
    }
    this._isCalculating = true;
    const first = this._calculationOrder[0];
    const source = this._objects[first];
    globalThis.event = new Event({});
    this.runCalculate(source, globalThis.event);
    this._isCalculating = false;
  }
  runCalculate(source, event) {
    if (!this._calculationOrder || !this._document.obj.calculate) {
      return;
    }
    for (const targetId of this._calculationOrder) {
      if (!(targetId in this._objects)) {
        continue;
      }
      if (!this._document.obj.calculate) {
        break;
      }
      event.value = null;
      const target = this._objects[targetId];
      let savedValue = target.obj._getValue();
      this.runActions(source, target, event, "Calculate");
      if (!event.rc) {
        continue;
      }
      if (event.value !== null) {
        target.obj.value = event.value;
      } else {
        event.value = target.obj._getValue();
      }
      this.runActions(target, target, event, "Validate");
      if (!event.rc) {
        if (target.obj._getValue() !== savedValue) {
          target.wrapped.value = savedValue;
        }
        continue;
      }
      if (event.value === null) {
        event.value = target.obj._getValue();
      }
      savedValue = target.obj._getValue();
      let formattedValue = null;
      if (this.runActions(target, target, event, "Format")) {
        formattedValue = event.value?.toString?.();
      }
      target.obj._send({
        id: target.obj._id,
        siblings: target.obj._siblings,
        value: savedValue,
        formattedValue
      });
    }
  }
}

;// ./src/scripting_api/fullscreen.js


class FullScreen extends PDFObject {
  constructor(data) {
    super(data);
    this._backgroundColor = [];
    this._clickAdvances = true;
    this._cursor = Cursor.hidden;
    this._defaultTransition = "";
    this._escapeExits = true;
    this._isFullScreen = true;
    this._loop = false;
    this._timeDelay = 3600;
    this._usePageTiming = false;
    this._useTimer = false;
  }
  get backgroundColor() {
    return this._backgroundColor;
  }
  set backgroundColor(_) {}
  get clickAdvances() {
    return this._clickAdvances;
  }
  set clickAdvances(_) {}
  get cursor() {
    return this._cursor;
  }
  set cursor(_) {}
  get defaultTransition() {
    return this._defaultTransition;
  }
  set defaultTransition(_) {}
  get escapeExits() {
    return this._escapeExits;
  }
  set escapeExits(_) {}
  get isFullScreen() {
    return this._isFullScreen;
  }
  set isFullScreen(_) {}
  get loop() {
    return this._loop;
  }
  set loop(_) {}
  get timeDelay() {
    return this._timeDelay;
  }
  set timeDelay(_) {}
  get transitions() {
    return ["Replace", "WipeRight", "WipeLeft", "WipeDown", "WipeUp", "SplitHorizontalIn", "SplitHorizontalOut", "SplitVerticalIn", "SplitVerticalOut", "BlindsHorizontal", "BlindsVertical", "BoxIn", "BoxOut", "GlitterRight", "GlitterDown", "GlitterRightDown", "Dissolve", "Random"];
  }
  set transitions(_) {
    throw new Error("fullscreen.transitions is read-only");
  }
  get usePageTiming() {
    return this._usePageTiming;
  }
  set usePageTiming(_) {}
  get useTimer() {
    return this._useTimer;
  }
  set useTimer(_) {}
}

;// ./src/scripting_api/thermometer.js

class Thermometer extends PDFObject {
  constructor(data) {
    super(data);
    this._cancelled = false;
    this._duration = 100;
    this._text = "";
    this._value = 0;
  }
  get cancelled() {
    return this._cancelled;
  }
  set cancelled(_) {
    throw new Error("thermometer.cancelled is read-only");
  }
  get duration() {
    return this._duration;
  }
  set duration(val) {
    this._duration = val;
  }
  get text() {
    return this._text;
  }
  set text(val) {
    this._text = val;
  }
  get value() {
    return this._value;
  }
  set value(val) {
    this._value = val;
  }
  begin() {}
  end() {}
}

;// ./src/scripting_api/app.js






class App extends PDFObject {
  constructor(data) {
    super(data);
    this._constants = null;
    this._focusRect = true;
    this._fs = null;
    this._language = App._getLanguage(data.language);
    this._openInPlace = false;
    this._platform = App._getPlatform(data.platform);
    this._runtimeHighlight = false;
    this._runtimeHighlightColor = ["T"];
    this._thermometer = null;
    this._toolbar = false;
    this._document = data._document;
    this._proxyHandler = data.proxyHandler;
    this._objects = Object.create(null);
    this._eventDispatcher = new EventDispatcher(this._document, data.calculationOrder, this._objects, data.externalCall);
    this._timeoutIds = new WeakMap();
    if (typeof FinalizationRegistry !== "undefined") {
      this._timeoutIdsRegistry = new FinalizationRegistry(this._cleanTimeout.bind(this));
    } else {
      this._timeoutIdsRegistry = null;
    }
    this._timeoutCallbackIds = new Map();
    this._timeoutCallbackId = USERACTIVATION_CALLBACKID + 1;
    this._globalEval = data.globalEval;
    this._externalCall = data.externalCall;
  }
  _dispatchEvent(pdfEvent) {
    this._eventDispatcher.dispatch(pdfEvent);
  }
  _registerTimeoutCallback(cExpr) {
    const id = this._timeoutCallbackId++;
    this._timeoutCallbackIds.set(id, cExpr);
    return id;
  }
  _unregisterTimeoutCallback(id) {
    this._timeoutCallbackIds.delete(id);
  }
  _evalCallback({
    callbackId,
    interval
  }) {
    const documentObj = this._document.obj;
    if (callbackId === USERACTIVATION_CALLBACKID) {
      documentObj._userActivation = false;
      return;
    }
    const expr = this._timeoutCallbackIds.get(callbackId);
    if (!interval) {
      this._unregisterTimeoutCallback(callbackId);
    }
    if (expr) {
      const saveUserActivation = documentObj._userActivation;
      documentObj._userActivation = false;
      this._globalEval(expr);
      documentObj._userActivation = saveUserActivation;
    }
  }
  _registerTimeout(callbackId, interval) {
    const timeout = Object.create(null);
    const id = {
      callbackId,
      interval
    };
    this._timeoutIds.set(timeout, id);
    this._timeoutIdsRegistry?.register(timeout, id);
    return timeout;
  }
  _unregisterTimeout(timeout) {
    this._timeoutIdsRegistry?.unregister(timeout);
    const data = this._timeoutIds.get(timeout);
    if (!data) {
      return;
    }
    this._timeoutIds.delete(timeout);
    this._cleanTimeout(data);
  }
  _cleanTimeout({
    callbackId,
    interval
  }) {
    this._unregisterTimeoutCallback(callbackId);
    if (interval) {
      this._externalCall("clearInterval", [callbackId]);
    } else {
      this._externalCall("clearTimeout", [callbackId]);
    }
  }
  static _getPlatform(platform) {
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
  static _getLanguage(language) {
    const [main, sub] = language.toLowerCase().split(/[-_]/);
    switch (main) {
      case "zh":
        if (sub === "cn" || sub === "sg") {
          return "CHS";
        }
        return "CHT";
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
        if (sub === "br") {
          return "PTB";
        }
        return "ENU";
      case "fi":
        return "SUO";
      case "SV":
        return "SVE";
      default:
        return "ENU";
    }
  }
  get activeDocs() {
    return [this._document.wrapped];
  }
  set activeDocs(_) {
    throw new Error("app.activeDocs is read-only");
  }
  get calculate() {
    return this._document.obj.calculate;
  }
  set calculate(calculate) {
    this._document.obj.calculate = calculate;
  }
  get constants() {
    return this._constants ??= Object.freeze({
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
    return this._focusRect;
  }
  set focusRect(val) {
    this._focusRect = val;
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
    if (this._fs === null) {
      this._fs = new Proxy(new FullScreen({
        send: this._send
      }), this._proxyHandler);
    }
    return this._fs;
  }
  set fs(_) {
    throw new Error("app.fs is read-only");
  }
  get language() {
    return this._language;
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
    return this._openInPlace;
  }
  set openInPlace(val) {
    this._openInPlace = val;
  }
  get platform() {
    return this._platform;
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
    return this._runtimeHighlight;
  }
  set runtimeHighlight(val) {
    this._runtimeHighlight = val;
  }
  get runtimeHighlightColor() {
    return this._runtimeHighlightColor;
  }
  set runtimeHighlightColor(val) {
    if (Color._isValidColor(val)) {
      this._runtimeHighlightColor = val;
    }
  }
  get thermometer() {
    if (this._thermometer === null) {
      this._thermometer = new Proxy(new Thermometer({
        send: this._send
      }), this._proxyHandler);
    }
    return this._thermometer;
  }
  set thermometer(_) {
    throw new Error("app.thermometer is read-only");
  }
  get toolbar() {
    return this._toolbar;
  }
  set toolbar(val) {
    this._toolbar = val;
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
    return VIEWER_TYPE;
  }
  set viewerType(_) {
    throw new Error("app.viewerType is read-only");
  }
  get viewerVariation() {
    return VIEWER_VARIATION;
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
    if (!this._document.obj._userActivation) {
      return 0;
    }
    this._document.obj._userActivation = false;
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
      return this._externalCall("confirm", [cMsg]) ? 4 : 3;
    }
    this._externalCall("alert", [cMsg]);
    return 1;
  }
  beep() {}
  beginPriv() {}
  browseForDoc() {}
  clearInterval(oInterval) {
    this._unregisterTimeout(oInterval);
  }
  clearTimeOut(oTime) {
    this._unregisterTimeout(oTime);
  }
  endPriv() {}
  execDialog() {}
  execMenuItem(item) {
    if (!this._document.obj._userActivation) {
      return;
    }
    this._document.obj._userActivation = false;
    switch (item) {
      case "SaveAs":
        if (this._document.obj._disableSaving) {
          return;
        }
        this._send({
          command: item
        });
        break;
      case "FirstPage":
      case "LastPage":
      case "NextPage":
      case "PrevPage":
      case "ZoomViewIn":
      case "ZoomViewOut":
        this._send({
          command: item
        });
        break;
      case "FitPage":
        this._send({
          command: "zoom",
          value: "page-fit"
        });
        break;
      case "Print":
        if (this._document.obj._disablePrinting) {
          return;
        }
        this._send({
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
    if (cQuestion && typeof cQuestion === "object") {
      cDefault = cQuestion.cDefault;
      cQuestion = cQuestion.cQuestion;
    }
    cQuestion = (cQuestion || "").toString();
    cDefault = (cDefault || "").toString();
    return this._externalCall("prompt", [cQuestion, cDefault || ""]);
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
    const callbackId = this._registerTimeoutCallback(cExpr);
    this._externalCall("setInterval", [callbackId, nMilliseconds]);
    return this._registerTimeout(callbackId, true);
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
    const callbackId = this._registerTimeoutCallback(cExpr);
    this._externalCall("setTimeout", [callbackId, nMilliseconds]);
    return this._registerTimeout(callbackId, false);
  }
  trustedFunction() {}
  trustPropagatorFunction() {}
}

;// ./src/scripting_api/console.js

class Console extends PDFObject {
  clear() {
    this._send({
      id: "clear"
    });
  }
  hide() {}
  println(msg) {
    if (typeof msg === "string") {
      this._send({
        command: "println",
        value: "PDF.js Console:: " + msg
      });
    }
  }
  show() {}
}

;// ./src/scripting_api/print_params.js
class PrintParams {
  constructor(data) {
    this.binaryOk = true;
    this.bitmapDPI = 150;
    this.booklet = {
      binding: 0,
      duplexMode: 0,
      subsetFrom: 0,
      subsetTo: -1
    };
    this.colorOverride = 0;
    this.colorProfile = "";
    this.constants = Object.freeze({
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
        suppressCropClip: 1 << 1,
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
    this.downloadFarEastFonts = false;
    this.fileName = "";
    this.firstPage = 0;
    this.flags = 0;
    this.fontPolicy = 0;
    this.gradientDPI = 150;
    this.interactive = 1;
    this.lastPage = data.lastPage;
    this.npUpAutoRotate = false;
    this.npUpNumPagesH = 2;
    this.npUpNumPagesV = 2;
    this.npUpPageBorder = false;
    this.npUpPageOrder = 0;
    this.pageHandling = 0;
    this.pageSubset = 0;
    this.printAsImage = false;
    this.printContent = 0;
    this.printerName = "";
    this.psLevel = 0;
    this.rasterFlags = 0;
    this.reversePages = false;
    this.tileLabel = false;
    this.tileMark = 0;
    this.tileOverlap = 0;
    this.tileScale = 1.0;
    this.transparencyLevel = 75;
    this.usePrinterCRD = 0;
    this.useT1Conversion = 0;
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
class Doc extends PDFObject {
  constructor(data) {
    super(data);
    this._expandos = globalThis;
    this._baseURL = data.baseURL || "";
    this._calculate = true;
    this._delay = false;
    this._dirty = false;
    this._disclosed = false;
    this._media = undefined;
    this._metadata = data.metadata || "";
    this._noautocomplete = undefined;
    this._nocache = undefined;
    this._spellDictionaryOrder = [];
    this._spellLanguageOrder = [];
    this._printParams = null;
    this._fields = new Map();
    this._fieldNames = [];
    this._event = null;
    this._author = data.Author || "";
    this._creator = data.Creator || "";
    this._creationDate = this._getDate(data.CreationDate) || null;
    this._docID = data.docID || ["", ""];
    this._documentFileName = data.filename || "";
    this._filesize = data.filesize || 0;
    this._keywords = data.Keywords || "";
    this._layout = data.layout || "";
    this._modDate = this._getDate(data.ModDate) || null;
    this._numFields = 0;
    this._numPages = data.numPages || 1;
    this._pageNum = data.pageNum || 0;
    this._producer = data.Producer || "";
    this._securityHandler = data.EncryptFilterName || null;
    this._subject = data.Subject || "";
    this._title = data.Title || "";
    this._URL = data.URL || "";
    this._info = new Proxy({
      title: this._title,
      author: this._author,
      authors: data.authors || [this._author],
      subject: this._subject,
      keywords: this._keywords,
      creator: this._creator,
      producer: this._producer,
      creationdate: this._creationDate,
      moddate: this._modDate,
      trapped: data.Trapped || "Unknown"
    }, InfoProxyHandler);
    this._zoomType = ZoomType.none;
    this._zoom = data.zoom || 100;
    this._actions = createActionsMap(data.actions);
    this._globalEval = data.globalEval;
    this._pageActions = null;
    this._userActivation = false;
    this._disablePrinting = false;
    this._disableSaving = false;
    this._otherPageActions = null;
  }
  _initActions() {
    const dontRun = new Set(["WillClose", "WillSave", "DidSave", "WillPrint", "DidPrint", "OpenAction"]);
    this._disableSaving = true;
    for (const actionName of this._actions.keys()) {
      if (!dontRun.has(actionName)) {
        this._runActions(actionName);
      }
    }
    this._runActions("OpenAction");
    this._disableSaving = false;
  }
  _dispatchDocEvent(name) {
    switch (name) {
      case "Open":
        this._disableSaving = true;
        this._runActions("OpenAction");
        this._disableSaving = false;
        break;
      case "WillPrint":
        this._disablePrinting = true;
        try {
          this._runActions(name);
        } catch (error) {
          this._send(serializeError(error));
        }
        this._send({
          command: "WillPrintFinished"
        });
        this._disablePrinting = false;
        break;
      case "WillSave":
        this._disableSaving = true;
        this._runActions(name);
        this._disableSaving = false;
        break;
      default:
        this._runActions(name);
    }
  }
  _dispatchPageEvent(name, actions, pageNumber) {
    if (name === "PageOpen") {
      this._pageActions ||= new Map();
      if (!this._pageActions.has(pageNumber)) {
        this._pageActions.set(pageNumber, createActionsMap(actions));
      }
      this._pageNum = pageNumber - 1;
    }
    for (const acts of [this._pageActions, this._otherPageActions]) {
      actions = acts?.get(pageNumber)?.get(name);
      if (actions) {
        for (const action of actions) {
          this._globalEval(action);
        }
      }
    }
  }
  _runActions(name) {
    const actions = this._actions.get(name);
    if (!actions) {
      return;
    }
    for (const action of actions) {
      try {
        this._globalEval(action);
      } catch (error) {
        const serializedError = serializeError(error);
        serializedError.value = `Error when executing "${name}" for document\n${serializedError.value}`;
        this._send(serializedError);
      }
    }
  }
  _addField(name, field) {
    this._fields.set(name, field);
    this._fieldNames.push(name);
    this._numFields++;
    const po = field.obj._actions.get("PageOpen");
    const pc = field.obj._actions.get("PageClose");
    if (po || pc) {
      this._otherPageActions ||= new Map();
      let actions = this._otherPageActions.get(field.obj._page + 1);
      if (!actions) {
        actions = new Map();
        this._otherPageActions.set(field.obj._page + 1, actions);
      }
      if (po) {
        let poActions = actions.get("PageOpen");
        if (!poActions) {
          poActions = [];
          actions.set("PageOpen", poActions);
        }
        poActions.push(...po);
      }
      if (pc) {
        let pcActions = actions.get("PageClose");
        if (!pcActions) {
          pcActions = [];
          actions.set("PageClose", pcActions);
        }
        pcActions.push(...pc);
      }
    }
  }
  _getDate(date) {
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
    return this._author;
  }
  set author(_) {
    throw new Error("doc.author is read-only");
  }
  get baseURL() {
    return this._baseURL;
  }
  set baseURL(baseURL) {
    this._baseURL = baseURL;
  }
  get bookmarkRoot() {
    return undefined;
  }
  set bookmarkRoot(_) {
    throw new Error("doc.bookmarkRoot is read-only");
  }
  get calculate() {
    return this._calculate;
  }
  set calculate(calculate) {
    this._calculate = calculate;
  }
  get creator() {
    return this._creator;
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
    return this._delay;
  }
  set delay(delay) {
    this._delay = delay;
  }
  get dirty() {
    return this._dirty;
  }
  set dirty(dirty) {
    this._dirty = dirty;
  }
  get disclosed() {
    return this._disclosed;
  }
  set disclosed(disclosed) {
    this._disclosed = disclosed;
  }
  get docID() {
    return this._docID;
  }
  set docID(_) {
    throw new Error("doc.docID is read-only");
  }
  get documentFileName() {
    return this._documentFileName;
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
    return this._filesize;
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
    return this._info;
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
    return this._keywords;
  }
  set keywords(_) {
    throw new Error("doc.keywords is read-only");
  }
  get layout() {
    return this._layout;
  }
  set layout(value) {
    if (!this._userActivation) {
      return;
    }
    this._userActivation = false;
    if (typeof value !== "string") {
      return;
    }
    if (value !== "SinglePage" && value !== "OneColumn" && value !== "TwoColumnLeft" && value !== "TwoPageLeft" && value !== "TwoColumnRight" && value !== "TwoPageRight") {
      value = "SinglePage";
    }
    this._send({
      command: "layout",
      value
    });
    this._layout = value;
  }
  get media() {
    return this._media;
  }
  set media(media) {
    this._media = media;
  }
  get metadata() {
    return this._metadata;
  }
  set metadata(metadata) {
    this._metadata = metadata;
  }
  get modDate() {
    return this._modDate;
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
    return this._noautocomplete;
  }
  set noautocomplete(noautocomplete) {
    this._noautocomplete = noautocomplete;
  }
  get nocache() {
    return this._nocache;
  }
  set nocache(nocache) {
    this._nocache = nocache;
  }
  get numFields() {
    return this._numFields;
  }
  set numFields(_) {
    throw new Error("doc.numFields is read-only");
  }
  get numPages() {
    return this._numPages;
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
    return this._pageNum;
  }
  set pageNum(value) {
    if (!this._userActivation) {
      return;
    }
    this._userActivation = false;
    if (typeof value !== "number" || value < 0 || value >= this._numPages) {
      return;
    }
    this._send({
      command: "page-num",
      value
    });
    this._pageNum = value;
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
    return this._producer;
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
    return this._securityHandler;
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
    return this._spellDictionaryOrder;
  }
  set spellDictionaryOrder(spellDictionaryOrder) {
    this._spellDictionaryOrder = spellDictionaryOrder;
  }
  get spellLanguageOrder() {
    return this._spellLanguageOrder;
  }
  set spellLanguageOrder(spellLanguageOrder) {
    this._spellLanguageOrder = spellLanguageOrder;
  }
  get subject() {
    return this._subject;
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
    return this._title;
  }
  set title(_) {
    throw new Error("doc.title is read-only");
  }
  get URL() {
    return this._URL;
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
    return this._xfa;
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
    return this._zoomType;
  }
  set zoomType(type) {
    if (!this._userActivation) {
      return;
    }
    this._userActivation = false;
    if (typeof type !== "string") {
      return;
    }
    switch (type) {
      case ZoomType.none:
        this._send({
          command: "zoom",
          value: 1
        });
        break;
      case ZoomType.fitP:
        this._send({
          command: "zoom",
          value: "page-fit"
        });
        break;
      case ZoomType.fitW:
        this._send({
          command: "zoom",
          value: "page-width"
        });
        break;
      case ZoomType.fitH:
        this._send({
          command: "zoom",
          value: "page-height"
        });
        break;
      case ZoomType.fitV:
        this._send({
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
    this._zoomType = type;
  }
  get zoom() {
    return this._zoom;
  }
  set zoom(value) {
    if (!this._userActivation) {
      return;
    }
    this._userActivation = false;
    if (typeof value !== "number" || value < 8.33 || value > 6400) {
      return;
    }
    this._send({
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
    this._eventDispatcher.calculateNow();
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
  _getField(cName) {
    if (cName && typeof cName === "object") {
      cName = cName.cName;
    }
    if (typeof cName !== "string") {
      throw new TypeError("Invalid field name: must be a string");
    }
    const searchedField = this._fields.get(cName);
    if (searchedField) {
      return searchedField;
    }
    const parts = cName.split("#");
    let childIndex = NaN;
    if (parts.length === 2) {
      childIndex = Math.floor(parseFloat(parts[1]));
      cName = parts[0];
    }
    for (const [name, field] of this._fields.entries()) {
      if (name.endsWith(cName)) {
        if (!isNaN(childIndex)) {
          const children = this._getChildren(name);
          if (childIndex < 0 || childIndex >= children.length) {
            childIndex = 0;
          }
          if (childIndex < children.length) {
            this._fields.set(cName, children[childIndex]);
            return children[childIndex];
          }
        }
        this._fields.set(cName, field);
        return field;
      }
    }
    return null;
  }
  getField(cName) {
    const field = this._getField(cName);
    if (!field) {
      return null;
    }
    return field.wrapped;
  }
  _getChildren(fieldName) {
    const len = fieldName.length;
    const children = [];
    const pattern = /^\.[^.]+$/;
    for (const [name, field] of this._fields.entries()) {
      if (name.startsWith(fieldName)) {
        const finalPart = name.slice(len);
        if (pattern.test(finalPart)) {
          children.push(field);
        }
      }
    }
    return children;
  }
  _getTerminalChildren(fieldName) {
    const children = [];
    const len = fieldName.length;
    for (const [name, field] of this._fields.entries()) {
      if (name.startsWith(fieldName)) {
        const finalPart = name.slice(len);
        if (field.obj._hasValue && (finalPart === "" || finalPart.startsWith("."))) {
          children.push(field.wrapped);
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
    if (0 <= nIndex && nIndex < this.numFields) {
      return this._fieldNames[Math.trunc(nIndex)];
    }
    return null;
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
    return this._printParams ||= new PrintParams({
      lastPage: this._numPages - 1
    });
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
    if (this._disablePrinting || !this._userActivation) {
      return;
    }
    this._userActivation = false;
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
    this._send({
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
      fieldsToReset = [];
      for (const fieldName of aFields) {
        if (!fieldName) {
          continue;
        }
        if (typeof fieldName !== "string") {
          fieldsToReset = null;
          break;
        }
        const field = this._getField(fieldName);
        if (!field) {
          continue;
        }
        fieldsToReset.push(field);
        mustCalculate = true;
      }
    }
    if (!fieldsToReset) {
      fieldsToReset = this._fields.values();
      mustCalculate = this._fields.size !== 0;
    }
    for (const field of fieldsToReset) {
      field.obj.value = field.obj.defaultValue;
      this._send({
        id: field.obj._id,
        siblings: field.obj._siblings,
        value: field.obj.defaultValue,
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

;// ./src/scripting_api/proxy.js
class ProxyHandler {
  constructor() {
    this.nosend = new Set(["delay"]);
  }
  get(obj, prop) {
    if (prop in obj._expandos) {
      const val = obj._expandos[prop];
      if (typeof val === "function") {
        return val.bind(obj);
      }
      return val;
    }
    if (typeof prop === "string" && !prop.startsWith("_") && prop in obj) {
      const val = obj[prop];
      if (typeof val === "function") {
        return val.bind(obj);
      }
      return val;
    }
    return undefined;
  }
  set(obj, prop, value) {
    if (obj._kidIds) {
      obj._kidIds.forEach(id => {
        obj._appObjects[id].wrapped[prop] = value;
      });
    }
    if (typeof prop === "string" && !prop.startsWith("_") && prop in obj) {
      const old = obj[prop];
      obj[prop] = value;
      if (!this.nosend.has(prop) && obj._send && obj._id !== null && typeof old !== "function") {
        const data = {
          id: obj._id
        };
        data[prop] = prop === "value" ? obj._getValue() : obj[prop];
        if (!obj._siblings) {
          obj._send(data);
        } else {
          data.siblings = obj._siblings;
          obj._send(data);
        }
      }
    } else {
      obj._expandos[prop] = value;
    }
    return true;
  }
  has(obj, prop) {
    return prop in obj._expandos || typeof prop === "string" && !prop.startsWith("_") && prop in obj;
  }
  getPrototypeOf(obj) {
    return null;
  }
  setPrototypeOf(obj, proto) {
    return false;
  }
  isExtensible(obj) {
    return true;
  }
  preventExtensions(obj) {
    return false;
  }
  getOwnPropertyDescriptor(obj, prop) {
    if (prop in obj._expandos) {
      return {
        configurable: true,
        enumerable: true,
        value: obj._expandos[prop]
      };
    }
    if (typeof prop === "string" && !prop.startsWith("_") && prop in obj) {
      return {
        configurable: true,
        enumerable: true,
        value: obj[prop]
      };
    }
    return undefined;
  }
  defineProperty(obj, key, descriptor) {
    Object.defineProperty(obj._expandos, key, descriptor);
    return true;
  }
  deleteProperty(obj, prop) {
    if (prop in obj._expandos) {
      delete obj._expandos[prop];
    }
  }
  ownKeys(obj) {
    const fromExpandos = Reflect.ownKeys(obj._expandos);
    const fromObj = Reflect.ownKeys(obj).filter(k => !k.startsWith("_"));
    return fromExpandos.concat(fromObj);
  }
}

;// ./src/scripting_api/util.js

class Util extends PDFObject {
  #dateActionsCache = null;
  constructor(data) {
    super(data);
    this._scandCache = new Map();
    this._months = ["January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"];
    this._days = ["Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"];
    this.MILLISECONDS_IN_DAY = 86400000;
    this.MILLISECONDS_IN_WEEK = 604800000;
    this._externalCall = data.externalCall;
  }
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
    return args[0].replaceAll(pattern, function (match, nDecSep, cFlags, nWidth, nPrecision, cConvChar) {
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
      if (nWidth) {
        nWidth = parseInt(nWidth);
      }
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
      if (nPrecision) {
        nPrecision = parseInt(nPrecision.substring(1));
      }
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
      if (intPart < 0) {
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
      mmmm: data => this._months[data.month],
      mmm: data => this._months[data.month].substring(0, 3),
      mm: data => (data.month + 1).toString().padStart(2, "0"),
      m: data => (data.month + 1).toString(),
      dddd: data => this._days[data.dayOfWeek],
      ddd: data => this._days[data.dayOfWeek].substring(0, 3),
      dd: data => data.day.toString().padStart(2, "0"),
      d: data => data.day.toString(),
      yyyy: data => data.year.toString(),
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
    return cFormat.replaceAll(patterns, function (match, pattern) {
      if (pattern in handlers) {
        return handlers[pattern](data);
      }
      return pattern.charCodeAt(1);
    });
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
  #tryToGuessDate(cFormat, cDate) {
    let actions = (this.#dateActionsCache ||= new Map()).get(cFormat);
    if (!actions) {
      actions = [];
      this.#dateActionsCache.set(cFormat, actions);
      cFormat.replaceAll(/(d+)|(m+)|(y+)|(H+)|(M+)|(s+)/g, function (_match, d, m, y, H, M, s) {
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
    }
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
  scand(cFormat, cDate) {
    return this._scand(cFormat, cDate);
  }
  _scand(cFormat, cDate, strict = false) {
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
    if (!this._scandCache.has(cFormat)) {
      const months = this._months;
      const days = this._days;
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
      const re = escapedFormat.replaceAll(patterns, function (match, patternElement) {
        const {
          pattern,
          action
        } = handlers[patternElement];
        actions.push(action);
        return pattern;
      });
      this._scandCache.set(cFormat, [re, actions]);
    }
    const [re, actions] = this._scandCache.get(cFormat);
    const matches = new RegExp(`^${re}$`, "g").exec(cDate);
    if (!matches || matches.length !== actions.length + 1) {
      return strict ? null : this.#tryToGuessDate(cFormat, cDate);
    }
    const data = {
      year: new Date().getFullYear(),
      month: 0,
      day: 1,
      hours: 12,
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
  const globalEval = code => globalThis.eval(code);
  const send = data => externalCall("send", [data]);
  const proxyHandler = new ProxyHandler();
  const {
    data
  } = params;
  const doc = new Doc({
    send,
    globalEval,
    ...data.docInfo
  });
  const _document = {
    obj: doc,
    wrapped: new Proxy(doc, proxyHandler)
  };
  const app = new App({
    send,
    globalEval,
    externalCall,
    _document,
    calculationOrder: data.calculationOrder,
    proxyHandler,
    ...data.appInfo
  });
  const util = new Util({
    externalCall
  });
  const appObjects = app._objects;
  if (data.objects) {
    const annotations = [];
    for (const [name, objs] of Object.entries(data.objects)) {
      annotations.length = 0;
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
      obj.doc = _document;
      obj.fieldPath = name;
      obj.appObjects = appObjects;
      const otherFields = annotations.slice(1);
      let field;
      switch (obj.type) {
        case "radiobutton":
          {
            field = new RadioButtonField(otherFields, obj);
            break;
          }
        case "checkbox":
          {
            field = new CheckboxField(otherFields, obj);
            break;
          }
        default:
          if (otherFields.length > 0) {
            obj.siblings = otherFields.map(x => x.id);
          }
          field = new Field(obj);
      }
      const wrapped = new Proxy(field, proxyHandler);
      const _object = {
        obj: field,
        wrapped
      };
      doc._addField(name, _object);
      for (const object of objs) {
        appObjects[object.id] = _object;
      }
      if (container) {
        appObjects[container.id] = _object;
      }
    }
  }
  const color = new Color();
  globalThis.event = null;
  globalThis.global = Object.create(null);
  globalThis.app = new Proxy(app, proxyHandler);
  globalThis.color = new Proxy(color, proxyHandler);
  globalThis.console = new Proxy(new Console({
    send
  }), proxyHandler);
  globalThis.util = new Proxy(util, proxyHandler);
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
    if (name !== "constructor" && !name.startsWith("_")) {
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
    if (name === "constructor" || name.startsWith("_")) {
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
  const functions = {
    dispatchEvent: app._dispatchEvent.bind(app),
    timeoutCb: app._evalCallback.bind(app)
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

const pdfjsVersion = "5.2.135";
const pdfjsBuild = "b47b248e1";
globalThis.pdfjsScripting = {
  initSandbox: initSandbox
};
