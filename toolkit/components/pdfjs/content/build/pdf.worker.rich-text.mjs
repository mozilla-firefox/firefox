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
export const __webpack_esm_ids__ = ["rich-text"];
export const __webpack_esm_modules__ = {

/***/ 4028
(__unused_webpack___webpack_module__, __webpack_exports__, __webpack_require__) {

/* harmony export */ __webpack_require__.d(__webpack_exports__, {
/* harmony export */   FontFinder: () => (/* binding */ FontFinder),
/* harmony export */   getMetrics: () => (/* binding */ getMetrics),
/* harmony export */   selectFont: () => (/* binding */ selectFont)
/* harmony export */ });
/* harmony import */ var _shared_util_js__WEBPACK_IMPORTED_MODULE_0__ = __webpack_require__(4292);
/* harmony import */ var _symbol_utils_js__WEBPACK_IMPORTED_MODULE_1__ = __webpack_require__(1318);
/* harmony import */ var _utils_js__WEBPACK_IMPORTED_MODULE_2__ = __webpack_require__(8063);



class FontFinder {
  constructor(pdfFonts) {
    this.fonts = new Map();
    this.cache = new Map();
    this.warned = new Set();
    this.defaultFont = null;
    this.add(pdfFonts);
  }
  add(pdfFonts, reallyMissingFonts = null) {
    for (const pdfFont of pdfFonts) {
      this.addPdfFont(pdfFont);
    }
    for (const pdfFont of this.fonts.values()) {
      pdfFont.regular ||= pdfFont.italic || pdfFont.bold || pdfFont.bolditalic;
    }
    if (!reallyMissingFonts || reallyMissingFonts.size === 0) {
      return;
    }
    const myriad = this.fonts.get("PdfJS-Fallback-PdfJS-XFA");
    for (const missing of reallyMissingFonts) {
      this.fonts.set(missing, myriad);
    }
  }
  addPdfFont(pdfFont) {
    const cssFontInfo = pdfFont.cssFontInfo;
    const name = cssFontInfo.fontFamily;
    const font = this.fonts.getOrInsertComputed(name, _shared_util_js__WEBPACK_IMPORTED_MODULE_0__.makeObj);
    this.defaultFont ??= font;
    let property = "";
    const fontWeight = parseFloat(cssFontInfo.fontWeight);
    if (parseFloat(cssFontInfo.italicAngle) !== 0) {
      property = fontWeight >= 700 ? "bolditalic" : "italic";
    } else if (fontWeight >= 700) {
      property = "bold";
    }
    if (!property) {
      if (pdfFont.name.includes("Bold") || pdfFont.psName?.includes("Bold")) {
        property = "bold";
      }
      if (pdfFont.name.includes("Italic") || pdfFont.name.endsWith("It") || pdfFont.psName?.includes("Italic") || pdfFont.psName?.endsWith("It")) {
        property += "italic";
      }
    }
    property ||= "regular";
    font[property] = pdfFont;
  }
  getDefault() {
    return this.defaultFont;
  }
  find(fontName, mustWarn = true) {
    let font = this.fonts.get(fontName) || this.cache.get(fontName);
    if (font) {
      return font;
    }
    const pattern = /[,\-_ ]|bolditalic|bold|italic|regular|it/gi;
    let name = fontName.replaceAll(pattern, "");
    font = this.fonts.get(name);
    if (font) {
      this.cache.set(fontName, font);
      return font;
    }
    name = name.toLowerCase();
    const maybe = [];
    for (const [family, pdfFont] of this.fonts) {
      if (family.replaceAll(pattern, "").toLowerCase().startsWith(name)) {
        maybe.push(pdfFont);
      }
    }
    if (maybe.length === 0) {
      for (const pdfFont of this.fonts.values()) {
        if (pdfFont.regular.name?.replaceAll(pattern, "").toLowerCase().startsWith(name)) {
          maybe.push(pdfFont);
        }
      }
    }
    if (maybe.length === 0) {
      name = name.replaceAll(/psmt|mt/gi, "");
      for (const [family, pdfFont] of this.fonts) {
        if (family.replaceAll(pattern, "").toLowerCase().startsWith(name)) {
          maybe.push(pdfFont);
        }
      }
    }
    if (maybe.length === 0) {
      for (const pdfFont of this.fonts.values()) {
        if (pdfFont.regular.name?.replaceAll(pattern, "").toLowerCase().startsWith(name)) {
          maybe.push(pdfFont);
        }
      }
    }
    if (maybe.length >= 1) {
      if (maybe.length !== 1 && mustWarn) {
        (0,_shared_util_js__WEBPACK_IMPORTED_MODULE_0__.warn)(`XFA - Too many choices to guess the correct font: ${fontName}`);
      }
      this.cache.set(fontName, maybe[0]);
      return maybe[0];
    }
    if (mustWarn && !this.warned.has(fontName)) {
      this.warned.add(fontName);
      (0,_shared_util_js__WEBPACK_IMPORTED_MODULE_0__.warn)(`XFA - Cannot find the font: ${fontName}`);
    }
    return null;
  }
}
function selectFont(xfaFont, typeface) {
  if (xfaFont.posture === "italic") {
    return xfaFont.weight === "bold" ? typeface.bolditalic : typeface.italic;
  } else if (xfaFont.weight === "bold") {
    return typeface.bold;
  }
  return typeface.regular;
}
function getMetrics(xfaFont, real = false) {
  let pdfFont = null;
  if (xfaFont) {
    const name = (0,_utils_js__WEBPACK_IMPORTED_MODULE_2__.stripQuotes)(xfaFont.typeface);
    const typeface = xfaFont[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_1__.$globalData].fontFinder.find(name);
    pdfFont = selectFont(xfaFont, typeface);
  }
  if (!pdfFont) {
    return {
      lineHeight: 12,
      lineGap: 2,
      lineNoGap: 10
    };
  }
  const size = xfaFont.size || 10;
  const lineHeight = pdfFont.lineHeight ? Math.max(real ? 0 : 1.2, pdfFont.lineHeight) : 1.2;
  const lineGap = pdfFont.lineGap === undefined ? 0.2 : pdfFont.lineGap;
  return {
    lineHeight: lineHeight * size,
    lineGap: lineGap * size,
    lineNoGap: Math.max(1, lineHeight - lineGap) * size
  };
}


/***/ },

/***/ 1037
(__unused_webpack___webpack_module__, __webpack_exports__, __webpack_require__) {


// EXPORTS
__webpack_require__.d(__webpack_exports__, {
  computeBbox: () => (/* binding */ computeBbox),
  createWrapper: () => (/* binding */ createWrapper),
  fixDimensions: () => (/* binding */ fixDimensions),
  fixTextIndent: () => (/* binding */ fixTextIndent),
  fixURL: () => (/* binding */ fixURL),
  isPrintOnly: () => (/* binding */ isPrintOnly),
  layoutClass: () => (/* binding */ layoutClass),
  layoutNode: () => (/* binding */ layoutNode),
  measureToString: () => (/* binding */ measureToString),
  setAccess: () => (/* binding */ setAccess),
  setFontFamily: () => (/* binding */ setFontFamily),
  setMinMaxDimensions: () => (/* binding */ setMinMaxDimensions),
  setPara: () => (/* binding */ setPara),
  toStyle: () => (/* binding */ toStyle)
});

// EXTERNAL MODULE: ./src/core/xfa/symbol_utils.js
var symbol_utils = __webpack_require__(1318);
// EXTERNAL MODULE: ./src/shared/util.js
var util = __webpack_require__(4292);
// EXTERNAL MODULE: ./src/core/xfa/utils.js
var utils = __webpack_require__(8063);
// EXTERNAL MODULE: ./src/core/xfa/fonts.js
var fonts = __webpack_require__(4028);
// EXTERNAL MODULE: ./src/shared/css_utils.js
var css_utils = /*#__PURE__*/__webpack_require__(5075);
;// ./src/core/xfa/text.js

const WIDTH_FACTOR = 1.02;
class FontInfo {
  constructor(xfaFont, margin, lineHeight, fontFinder) {
    this.lineHeight = lineHeight;
    this.paraMargin = margin || {
      top: 0,
      bottom: 0,
      left: 0,
      right: 0
    };
    if (!xfaFont) {
      [this.pdfFont, this.xfaFont] = this.defaultFont(fontFinder);
      return;
    }
    this.xfaFont = {
      typeface: xfaFont.typeface,
      posture: xfaFont.posture,
      weight: xfaFont.weight,
      size: xfaFont.size,
      letterSpacing: xfaFont.letterSpacing
    };
    const typeface = fontFinder.find(xfaFont.typeface);
    if (!typeface) {
      [this.pdfFont, this.xfaFont] = this.defaultFont(fontFinder);
      return;
    }
    this.pdfFont = (0,fonts.selectFont)(xfaFont, typeface);
    if (!this.pdfFont) {
      [this.pdfFont, this.xfaFont] = this.defaultFont(fontFinder);
    }
  }
  defaultFont(fontFinder) {
    const font = fontFinder.find("Helvetica", false) || fontFinder.find("Myriad Pro", false) || fontFinder.find("Arial", false) || fontFinder.getDefault();
    if (font?.regular) {
      const pdfFont = font.regular;
      const info = pdfFont.cssFontInfo;
      const xfaFont = {
        typeface: info.fontFamily,
        posture: "normal",
        weight: "normal",
        size: 10,
        letterSpacing: 0
      };
      return [pdfFont, xfaFont];
    }
    const xfaFont = {
      typeface: "Courier",
      posture: "normal",
      weight: "normal",
      size: 10,
      letterSpacing: 0
    };
    return [null, xfaFont];
  }
}
class FontSelector {
  constructor(defaultXfaFont, defaultParaMargin, defaultLineHeight, fontFinder) {
    this.fontFinder = fontFinder;
    this.stack = [new FontInfo(defaultXfaFont, defaultParaMargin, defaultLineHeight, fontFinder)];
  }
  pushData(xfaFont, margin, lineHeight) {
    const lastFont = this.stack.at(-1);
    for (const name of ["typeface", "posture", "weight", "size", "letterSpacing"]) {
      xfaFont[name] ||= lastFont.xfaFont[name];
    }
    for (const name of ["top", "bottom", "left", "right"]) {
      if (isNaN(margin[name])) {
        margin[name] = lastFont.paraMargin[name];
      }
    }
    const fontInfo = new FontInfo(xfaFont, margin, lineHeight || lastFont.lineHeight, this.fontFinder);
    fontInfo.pdfFont ||= lastFont.pdfFont;
    this.stack.push(fontInfo);
  }
  popFont() {
    this.stack.pop();
  }
  topFont() {
    return this.stack.at(-1);
  }
}
class TextMeasure {
  constructor(defaultXfaFont, defaultParaMargin, defaultLineHeight, fonts) {
    this.glyphs = [];
    this.fontSelector = new FontSelector(defaultXfaFont, defaultParaMargin, defaultLineHeight, fonts);
    this.extraHeight = 0;
  }
  pushData(xfaFont, margin, lineHeight) {
    this.fontSelector.pushData(xfaFont, margin, lineHeight);
  }
  popFont(xfaFont) {
    return this.fontSelector.popFont();
  }
  addPara() {
    const lastFont = this.fontSelector.topFont();
    this.extraHeight += lastFont.paraMargin.top + lastFont.paraMargin.bottom;
  }
  addString(str) {
    if (!str) {
      return;
    }
    const lastFont = this.fontSelector.topFont();
    const fontSize = lastFont.xfaFont.size;
    if (lastFont.pdfFont) {
      const letterSpacing = lastFont.xfaFont.letterSpacing;
      const pdfFont = lastFont.pdfFont;
      const fontLineHeight = pdfFont.lineHeight || 1.2;
      const lineHeight = lastFont.lineHeight || Math.max(1.2, fontLineHeight) * fontSize;
      const lineGap = pdfFont.lineGap === undefined ? 0.2 : pdfFont.lineGap;
      const noGap = fontLineHeight - lineGap;
      const firstLineHeight = Math.max(1, noGap) * fontSize;
      const scale = fontSize / 1000;
      const fallbackWidth = pdfFont.defaultWidth || pdfFont.charsToGlyphs(" ")[0].width;
      for (const line of str.split(/[\u2029\n]/)) {
        const encodedLine = pdfFont.encodeString(line).join("");
        const glyphs = pdfFont.charsToGlyphs(encodedLine);
        for (const glyph of glyphs) {
          const width = glyph.width || fallbackWidth;
          this.glyphs.push([width * scale + letterSpacing, lineHeight, firstLineHeight, glyph.unicode, false]);
        }
        this.glyphs.push([0, 0, 0, "\n", true]);
      }
      this.glyphs.pop();
      return;
    }
    for (const line of str.split(/[\u2029\n]/)) {
      for (const char of line.split("")) {
        this.glyphs.push([fontSize, 1.2 * fontSize, fontSize, char, false]);
      }
      this.glyphs.push([0, 0, 0, "\n", true]);
    }
    this.glyphs.pop();
  }
  compute(maxWidth) {
    let lastSpacePos = -1,
      lastSpaceWidth = 0,
      width = 0,
      height = 0,
      currentLineWidth = 0,
      currentLineHeight = 0;
    let isBroken = false;
    let isFirstLine = true;
    for (let i = 0, ii = this.glyphs.length; i < ii; i++) {
      const [glyphWidth, lineHeight, firstLineHeight, char, isEOL] = this.glyphs[i];
      const isSpace = char === " ";
      const glyphHeight = isFirstLine ? firstLineHeight : lineHeight;
      if (isEOL) {
        width = Math.max(width, currentLineWidth);
        currentLineWidth = 0;
        height += currentLineHeight;
        currentLineHeight = glyphHeight;
        lastSpacePos = -1;
        lastSpaceWidth = 0;
        isFirstLine = false;
        continue;
      }
      if (isSpace) {
        if (currentLineWidth + glyphWidth > maxWidth) {
          width = Math.max(width, currentLineWidth);
          currentLineWidth = 0;
          height += currentLineHeight;
          currentLineHeight = glyphHeight;
          lastSpacePos = -1;
          lastSpaceWidth = 0;
          isBroken = true;
          isFirstLine = false;
        } else {
          currentLineHeight = Math.max(glyphHeight, currentLineHeight);
          lastSpaceWidth = currentLineWidth;
          currentLineWidth += glyphWidth;
          lastSpacePos = i;
        }
        continue;
      }
      if (currentLineWidth + glyphWidth > maxWidth) {
        height += currentLineHeight;
        currentLineHeight = glyphHeight;
        if (lastSpacePos !== -1) {
          i = lastSpacePos;
          width = Math.max(width, lastSpaceWidth);
          currentLineWidth = 0;
          lastSpacePos = -1;
          lastSpaceWidth = 0;
        } else {
          width = Math.max(width, currentLineWidth);
          currentLineWidth = glyphWidth;
        }
        isBroken = true;
        isFirstLine = false;
        continue;
      }
      currentLineWidth += glyphWidth;
      currentLineHeight = Math.max(glyphHeight, currentLineHeight);
    }
    width = Math.max(width, currentLineWidth);
    height += currentLineHeight + this.extraHeight;
    return {
      width: WIDTH_FACTOR * width,
      height,
      isBroken
    };
  }
}

// EXTERNAL MODULE: ./src/core/xfa/xfa_object.js
var xfa_object = __webpack_require__(7485);
;// ./src/core/xfa/html_utils.js







function measureToString(m) {
  if (typeof m === "string") {
    return "0px";
  }
  return Number.isInteger(m) ? `${m}px` : `${m.toFixed(2)}px`;
}
const converters = {
  anchorType(node, style) {
    const parent = node[symbol_utils.$getSubformParent]();
    if (!parent || parent.layout && parent.layout !== "position") {
      return;
    }
    if (!("transform" in style)) {
      style.transform = "";
    }
    switch (node.anchorType) {
      case "bottomCenter":
        style.transform += "translate(-50%, -100%)";
        break;
      case "bottomLeft":
        style.transform += "translate(0,-100%)";
        break;
      case "bottomRight":
        style.transform += "translate(-100%,-100%)";
        break;
      case "middleCenter":
        style.transform += "translate(-50%,-50%)";
        break;
      case "middleLeft":
        style.transform += "translate(0,-50%)";
        break;
      case "middleRight":
        style.transform += "translate(-100%,-50%)";
        break;
      case "topCenter":
        style.transform += "translate(-50%,0)";
        break;
      case "topRight":
        style.transform += "translate(-100%,0)";
        break;
    }
  },
  dimensions(node, style) {
    const parent = node[symbol_utils.$getSubformParent]();
    let width = node.w;
    const height = node.h;
    if (parent.layout?.includes("row")) {
      const extra = parent[symbol_utils.$extra];
      const colSpan = node.colSpan;
      let w;
      if (colSpan === -1) {
        w = Math.sumPrecise(extra.columnWidths.slice(extra.currentColumn));
        extra.currentColumn = 0;
      } else {
        w = Math.sumPrecise(extra.columnWidths.slice(extra.currentColumn, extra.currentColumn + colSpan));
        extra.currentColumn = (extra.currentColumn + node.colSpan) % extra.columnWidths.length;
      }
      if (!isNaN(w)) {
        width = node.w = w;
      }
    }
    style.width = width !== "" ? measureToString(width) : "auto";
    style.height = height !== "" ? measureToString(height) : "auto";
  },
  position(node, style) {
    const parent = node[symbol_utils.$getSubformParent]();
    if (parent?.layout && parent.layout !== "position") {
      return;
    }
    style.position = "absolute";
    style.left = measureToString(node.x);
    style.top = measureToString(node.y);
  },
  rotate(node, style) {
    if (node.rotate) {
      if (!("transform" in style)) {
        style.transform = "";
      }
      style.transform += `rotate(-${node.rotate}deg)`;
      style.transformOrigin = "top left";
    }
  },
  presence(node, style) {
    switch (node.presence) {
      case "invisible":
        style.visibility = "hidden";
        break;
      case "hidden":
      case "inactive":
        style.display = "none";
        break;
    }
  },
  hAlign(node, style) {
    if (node[symbol_utils.$nodeName] === "para") {
      switch (node.hAlign) {
        case "justifyAll":
          style.textAlign = "justify-all";
          break;
        case "radix":
          style.textAlign = "left";
          break;
        default:
          style.textAlign = node.hAlign;
      }
    } else {
      switch (node.hAlign) {
        case "left":
          style.alignSelf = "start";
          break;
        case "center":
          style.alignSelf = "center";
          break;
        case "right":
          style.alignSelf = "end";
          break;
      }
    }
  },
  margin(node, style) {
    if (node.margin) {
      style.margin = node.margin[symbol_utils.$toStyle]().margin;
    }
  }
};
function setMinMaxDimensions(node, style) {
  const parent = node[symbol_utils.$getSubformParent]();
  if (parent.layout === "position") {
    if (node.minW > 0) {
      style.minWidth = measureToString(node.minW);
    }
    if (node.maxW > 0) {
      style.maxWidth = measureToString(node.maxW);
    }
    if (node.minH > 0) {
      style.minHeight = measureToString(node.minH);
    }
    if (node.maxH > 0) {
      style.maxHeight = measureToString(node.maxH);
    }
  }
}
function layoutText(text, xfaFont, margin, lineHeight, fontFinder, width) {
  const measure = new TextMeasure(xfaFont, margin, lineHeight, fontFinder);
  if (typeof text === "string") {
    measure.addString(text);
  } else {
    text[symbol_utils.$pushGlyphs](measure);
  }
  return measure.compute(width);
}
function layoutNode(node, availableSpace) {
  let height = null;
  let width = null;
  let isBroken = false;
  if ((!node.w || !node.h) && node.value) {
    let marginH = 0;
    let marginV = 0;
    if (node.margin) {
      marginH = node.margin.leftInset + node.margin.rightInset;
      marginV = node.margin.topInset + node.margin.bottomInset;
    }
    let lineHeight = null;
    let margin = null;
    if (node.para) {
      margin = Object.create(null);
      lineHeight = node.para.lineHeight === "" ? null : node.para.lineHeight;
      margin.top = node.para.spaceAbove === "" ? 0 : node.para.spaceAbove;
      margin.bottom = node.para.spaceBelow === "" ? 0 : node.para.spaceBelow;
      margin.left = node.para.marginLeft === "" ? 0 : node.para.marginLeft;
      margin.right = node.para.marginRight === "" ? 0 : node.para.marginRight;
    }
    let font = node.font;
    if (!font) {
      const root = node[symbol_utils.$getTemplateRoot]();
      let parent = node[symbol_utils.$getParent]();
      while (parent && parent !== root) {
        if (parent.font) {
          font = parent.font;
          break;
        }
        parent = parent[symbol_utils.$getParent]();
      }
    }
    const maxWidth = (node.w || availableSpace.width) - marginH;
    const fontFinder = node[symbol_utils.$globalData].fontFinder;
    if (node.value.exData && node.value.exData[symbol_utils.$content] && node.value.exData.contentType === "text/html") {
      const res = layoutText(node.value.exData[symbol_utils.$content], font, margin, lineHeight, fontFinder, maxWidth);
      width = res.width;
      height = res.height;
      isBroken = res.isBroken;
    } else {
      const text = node.value[symbol_utils.$text]();
      if (text) {
        const res = layoutText(text, font, margin, lineHeight, fontFinder, maxWidth);
        width = res.width;
        height = res.height;
        isBroken = res.isBroken;
      }
    }
    if (width !== null && !node.w) {
      width += marginH;
    }
    if (height !== null && !node.h) {
      height += marginV;
    }
  }
  return {
    w: width,
    h: height,
    isBroken
  };
}
function computeBbox(node, html, availableSpace) {
  let bbox;
  if (node.w !== "" && node.h !== "") {
    bbox = [node.x, node.y, node.w, node.h];
  } else {
    if (!availableSpace) {
      return null;
    }
    let width = node.w;
    if (width === "") {
      if (node.maxW === 0) {
        const parent = node[symbol_utils.$getSubformParent]();
        width = parent.layout === "position" && parent.w !== "" ? 0 : node.minW;
      } else {
        width = Math.min(node.maxW, availableSpace.width);
      }
      html.attributes.style.width = measureToString(width);
    }
    let height = node.h;
    if (height === "") {
      if (node.maxH === 0) {
        const parent = node[symbol_utils.$getSubformParent]();
        height = parent.layout === "position" && parent.h !== "" ? 0 : node.minH;
      } else {
        height = Math.min(node.maxH, availableSpace.height);
      }
      html.attributes.style.height = measureToString(height);
    }
    bbox = [node.x, node.y, width, height];
  }
  return bbox;
}
function fixDimensions(node) {
  const parent = node[symbol_utils.$getSubformParent]();
  if (parent.layout?.includes("row")) {
    const extra = parent[symbol_utils.$extra];
    const colSpan = node.colSpan;
    let width;
    if (colSpan === -1) {
      width = Math.sumPrecise(extra.columnWidths.slice(extra.currentColumn));
    } else {
      width = Math.sumPrecise(extra.columnWidths.slice(extra.currentColumn, extra.currentColumn + colSpan));
    }
    if (!isNaN(width)) {
      node.w = width;
    }
  }
  if (parent.layout && parent.layout !== "position") {
    node.x = node.y = 0;
  }
  if (node.layout === "table") {
    if (node.w === "" && Array.isArray(node.columnWidths)) {
      node.w = Math.sumPrecise(node.columnWidths);
    }
  }
}
function layoutClass(node) {
  switch (node.layout) {
    case "position":
      return "xfaPosition";
    case "lr-tb":
      return "xfaLrTb";
    case "rl-row":
      return "xfaRlRow";
    case "rl-tb":
      return "xfaRlTb";
    case "row":
      return "xfaRow";
    case "table":
      return "xfaTable";
    case "tb":
      return "xfaTb";
    default:
      return "xfaPosition";
  }
}
function toStyle(node, ...names) {
  const style = Object.create(null);
  for (const name of names) {
    const value = node[name];
    if (value === null) {
      continue;
    }
    if (Object.hasOwn(converters, name)) {
      converters[name](node, style);
      continue;
    }
    if (value instanceof xfa_object.XFAObject) {
      const newStyle = value[symbol_utils.$toStyle]();
      if (newStyle) {
        Object.assign(style, newStyle);
      } else {
        (0,util.warn)(`(DEBUG) - XFA - style for ${name} not implemented yet`);
      }
    }
  }
  return style;
}
function createWrapper(node, html) {
  const {
    attributes
  } = html;
  const {
    style
  } = attributes;
  const wrapper = {
    name: "div",
    attributes: {
      class: ["xfaWrapper"],
      style: Object.create(null)
    },
    children: []
  };
  attributes.class.push("xfaWrapped");
  if (node.border) {
    const {
      widths,
      insets
    } = node.border[symbol_utils.$extra];
    let width, height;
    let top = insets[0];
    let left = insets[3];
    const insetsH = insets[0] + insets[2];
    const insetsW = insets[1] + insets[3];
    switch (node.border.hand) {
      case "even":
        top -= widths[0] / 2;
        left -= widths[3] / 2;
        width = `calc(100% + ${(widths[1] + widths[3]) / 2 - insetsW}px)`;
        height = `calc(100% + ${(widths[0] + widths[2]) / 2 - insetsH}px)`;
        break;
      case "left":
        top -= widths[0];
        left -= widths[3];
        width = `calc(100% + ${widths[1] + widths[3] - insetsW}px)`;
        height = `calc(100% + ${widths[0] + widths[2] - insetsH}px)`;
        break;
      case "right":
        width = insetsW ? `calc(100% - ${insetsW}px)` : "100%";
        height = insetsH ? `calc(100% - ${insetsH}px)` : "100%";
        break;
    }
    const classNames = ["xfaBorder"];
    if (isPrintOnly(node.border)) {
      classNames.push("xfaPrintOnly");
    }
    const border = {
      name: "div",
      attributes: {
        class: classNames,
        style: {
          top: `${top}px`,
          left: `${left}px`,
          width,
          height
        }
      },
      children: []
    };
    for (const key of ["border", "borderWidth", "borderColor", "borderRadius", "borderStyle"]) {
      if (style[key] !== undefined) {
        border.attributes.style[key] = style[key];
        delete style[key];
      }
    }
    wrapper.children.push(border, html);
  } else {
    wrapper.children.push(html);
  }
  for (const key of ["background", "backgroundClip", "top", "left", "width", "height", "minWidth", "minHeight", "maxWidth", "maxHeight", "transform", "transformOrigin", "visibility"]) {
    if (style[key] !== undefined) {
      wrapper.attributes.style[key] = style[key];
      delete style[key];
    }
  }
  wrapper.attributes.style.position = style.position === "absolute" ? "absolute" : "relative";
  delete style.position;
  if (style.alignSelf) {
    wrapper.attributes.style.alignSelf = style.alignSelf;
    delete style.alignSelf;
  }
  return wrapper;
}
function fixTextIndent(styles) {
  const indent = (0,utils.getMeasurement)(styles.textIndent, "0px");
  if (indent >= 0) {
    return;
  }
  const align = styles.textAlign === "right" ? "right" : "left";
  const name = "padding" + (align === "left" ? "Left" : "Right");
  const padding = (0,utils.getMeasurement)(styles[name], "0px");
  styles[name] = `${padding - indent}px`;
}
function setAccess(node, classNames) {
  switch (node.access) {
    case "nonInteractive":
      classNames.push("xfaNonInteractive");
      break;
    case "readOnly":
      classNames.push("xfaReadOnly");
      break;
    case "protected":
      classNames.push("xfaDisabled");
      break;
  }
}
function isPrintOnly(node) {
  return node.relevant.length > 0 && !node.relevant[0].excluded && node.relevant[0].viewname === "print";
}
function getCurrentPara(node) {
  const stack = node[symbol_utils.$getTemplateRoot]()[symbol_utils.$extra].paraStack;
  return stack.length ? stack.at(-1) : null;
}
function setPara(node, nodeStyle, value) {
  if (value.attributes.class?.includes("xfaRich")) {
    if (nodeStyle) {
      if (node.h === "") {
        nodeStyle.height = "auto";
      }
      if (node.w === "") {
        nodeStyle.width = "auto";
      }
    }
    const para = getCurrentPara(node);
    if (para) {
      const valueStyle = value.attributes.style;
      valueStyle.display = "flex";
      valueStyle.flexDirection = "column";
      switch (para.vAlign) {
        case "top":
          valueStyle.justifyContent = "start";
          break;
        case "bottom":
          valueStyle.justifyContent = "end";
          break;
        case "middle":
          valueStyle.justifyContent = "center";
          break;
      }
      const paraStyle = para[symbol_utils.$toStyle]();
      for (const [key, val] of Object.entries(paraStyle)) {
        if (!(key in valueStyle)) {
          valueStyle[key] = val;
        }
      }
    }
  }
}
function setFontFamily(xfaFont, node, fontFinder, style) {
  if (!fontFinder) {
    delete style.fontFamily;
    return;
  }
  const name = (0,utils.stripQuotes)(xfaFont.typeface);
  style.fontFamily = (0,css_utils.serializeFontFamily)(name);
  const typeface = fontFinder.find(name);
  if (typeface) {
    const {
      fontFamily
    } = typeface.regular.cssFontInfo;
    if (fontFamily !== name) {
      style.fontFamily = (0,css_utils.serializeFontFamily)(fontFamily);
    }
    const para = getCurrentPara(node);
    if (para && para.lineHeight !== "") {
      return;
    }
    if (style.lineHeight) {
      return;
    }
    const pdfFont = (0,fonts.selectFont)(xfaFont, typeface);
    if (pdfFont) {
      style.lineHeight = Math.max(1.2, pdfFont.lineHeight);
    }
  }
}
function fixURL(str) {
  const absoluteUrl = (0,util.createValidAbsoluteUrl)(str, null, {
    addDefaultProtocol: true,
    tryConvertEncoding: true
  });
  return absoluteUrl ? absoluteUrl.href : null;
}


/***/ },

/***/ 7088
(__unused_webpack___webpack_module__, __webpack_exports__, __webpack_require__) {

const $buildXFAObject = Symbol();
const NamespaceIds = {
  config: {
    id: 0,
    check: ns => ns.startsWith("http://www.xfa.org/schema/xci/")
  },
  connectionSet: {
    id: 1,
    check: ns => ns.startsWith("http://www.xfa.org/schema/xfa-connection-set/")
  },
  datasets: {
    id: 2,
    check: ns => ns.startsWith("http://www.xfa.org/schema/xfa-data/")
  },
  form: {
    id: 3,
    check: ns => ns.startsWith("http://www.xfa.org/schema/xfa-form/")
  },
  localeSet: {
    id: 4,
    check: ns => ns.startsWith("http://www.xfa.org/schema/xfa-locale-set/")
  },
  pdf: {
    id: 5,
    check: ns => ns === "http://ns.adobe.com/xdp/pdf/"
  },
  signature: {
    id: 6,
    check: ns => ns === "http://www.w3.org/2000/09/xmldsig#"
  },
  sourceSet: {
    id: 7,
    check: ns => ns.startsWith("http://www.xfa.org/schema/xfa-source-set/")
  },
  stylesheet: {
    id: 8,
    check: ns => ns === "http://www.w3.org/1999/XSL/Transform"
  },
  template: {
    id: 9,
    check: ns => ns.startsWith("http://www.xfa.org/schema/xfa-template/")
  },
  xdc: {
    id: 10,
    check: ns => ns.startsWith("http://www.xfa.org/schema/xdc/")
  },
  xdp: {
    id: 11,
    check: ns => ns === "http://ns.adobe.com/xdp/"
  },
  xfdf: {
    id: 12,
    check: ns => ns === "http://ns.adobe.com/xfdf/"
  },
  xhtml: {
    id: 13,
    check: ns => ns === "http://www.w3.org/1999/xhtml"
  },
  xmpmeta: {
    id: 14,
    check: ns => ns === "http://ns.adobe.com/xmpmeta/"
  }
};

/* harmony export */ __webpack_require__.d(__webpack_exports__, [
/* harmony export */   "$buildXFAObject", 0, /* binding */ $buildXFAObject,
/* harmony export */   "NamespaceIds", 0, /* binding */ NamespaceIds
/* harmony export */ ]);


/***/ },

/***/ 950
(__unused_webpack___webpack_module__, __webpack_exports__, __webpack_require__) {


// EXPORTS
__webpack_require__.d(__webpack_exports__, {
  XFAParserBase: () => (/* binding */ XFAParserBase)
});

// EXTERNAL MODULE: ./src/core/xfa/symbol_utils.js
var symbol_utils = __webpack_require__(1318);
// EXTERNAL MODULE: ./src/core/xml_parser.js
var xml_parser = __webpack_require__(3103);
// EXTERNAL MODULE: ./src/core/xfa/namespaces.js
var xfa_namespaces = __webpack_require__(7088);
// EXTERNAL MODULE: ./src/shared/util.js
var util = __webpack_require__(4292);
// EXTERNAL MODULE: ./src/core/xfa/xfa_object.js
var xfa_object = __webpack_require__(7485);
;// ./src/core/xfa/unknown.js


class UnknownNamespace {
  constructor(nsId) {
    this.namespaceId = nsId;
  }
  [xfa_namespaces.$buildXFAObject](name, attributes) {
    return new xfa_object.XmlObject(this.namespaceId, name, attributes);
  }
}

;// ./src/core/xfa/builder.js





class Empty extends xfa_object.XFAObject {
  constructor() {
    super(-1, "", Object.create(null));
  }
  [symbol_utils.$onChild](_) {
    return false;
  }
}
class Builder {
  constructor(namespaces, rootNamespace) {
    this._namespaceSetup = namespaces;
    this._namespaceStack = [];
    this._nsAgnosticLevel = 0;
    this._namespacePrefixes = new Map();
    this._namespaces = new Map();
    this._nextNsId = Math.max(...Object.values(xfa_namespaces.NamespaceIds).map(({
      id
    }) => id));
    this._currentNamespace = rootNamespace || new UnknownNamespace(++this._nextNsId);
  }
  build({
    nsPrefix,
    name,
    attributes,
    namespace,
    prefixes
  }) {
    const hasNamespaceDef = namespace !== null;
    if (hasNamespaceDef) {
      this._namespaceStack.push(this._currentNamespace);
      this._currentNamespace = this._searchNamespace(namespace);
    }
    if (prefixes) {
      this._addNamespacePrefix(prefixes);
    }
    if (Object.hasOwn(attributes, symbol_utils.$nsAttributes)) {
      const dataTemplate = this._namespaceSetup.datasets;
      const nsAttrs = attributes[symbol_utils.$nsAttributes];
      let xfaAttrs = null;
      for (const [ns, attrs] of Object.entries(nsAttrs)) {
        const nsToUse = this._getNamespaceToUse(ns);
        if (dataTemplate && nsToUse === dataTemplate) {
          xfaAttrs = {
            xfa: attrs
          };
          break;
        }
      }
      if (xfaAttrs) {
        attributes[symbol_utils.$nsAttributes] = xfaAttrs;
      } else {
        delete attributes[symbol_utils.$nsAttributes];
      }
    }
    const namespaceToUse = this._getNamespaceToUse(nsPrefix);
    const node = namespaceToUse?.[xfa_namespaces.$buildXFAObject](name, attributes) || new Empty();
    if (node[symbol_utils.$isNsAgnostic]()) {
      this._nsAgnosticLevel++;
    }
    if (hasNamespaceDef || prefixes || node[symbol_utils.$isNsAgnostic]()) {
      node[symbol_utils.$cleanup] = {
        hasNamespace: hasNamespaceDef,
        prefixes,
        nsAgnostic: node[symbol_utils.$isNsAgnostic]()
      };
    }
    return node;
  }
  isNsAgnostic() {
    return this._nsAgnosticLevel > 0;
  }
  _searchNamespace(nsName) {
    let ns = this._namespaces.get(nsName);
    if (ns) {
      return ns;
    }
    for (const [name, {
      check
    }] of Object.entries(xfa_namespaces.NamespaceIds)) {
      if (check(nsName)) {
        ns = this._namespaceSetup[name];
        if (ns) {
          this._namespaces.set(nsName, ns);
          return ns;
        }
        break;
      }
    }
    ns = new UnknownNamespace(++this._nextNsId);
    this._namespaces.set(nsName, ns);
    return ns;
  }
  _addNamespacePrefix(prefixes) {
    for (const {
      prefix,
      value
    } of prefixes) {
      const namespace = this._searchNamespace(value);
      this._namespacePrefixes.getOrInsertComputed(prefix, util.makeArr).push(namespace);
    }
  }
  _getNamespaceToUse(prefix) {
    if (!prefix) {
      return this._currentNamespace;
    }
    const prefixStack = this._namespacePrefixes.get(prefix);
    if (prefixStack?.length > 0) {
      return prefixStack.at(-1);
    }
    ;(0,util.warn)(`Unknown namespace prefix: ${prefix}.`);
    return null;
  }
  clean(data) {
    const {
      hasNamespace,
      prefixes,
      nsAgnostic
    } = data;
    if (hasNamespace) {
      this._currentNamespace = this._namespaceStack.pop();
    }
    prefixes?.forEach(({
      prefix
    }) => {
      this._namespacePrefixes.get(prefix).pop();
    });
    if (nsAgnostic) {
      this._nsAgnosticLevel--;
    }
  }
}

;// ./src/core/xfa/parser_base.js





class Root extends xfa_object.XFAObject {
  constructor() {
    super(-1, "root", Object.create(null));
    this.element = null;
  }
  [symbol_utils.$onChild](child) {
    this.element = child;
    return true;
  }
}
class XFAParserBase extends xml_parser.XMLParserBase {
  constructor({
    namespaces,
    templateClass,
    rootNamespace,
    richText = false
  }) {
    super();
    this._xfaOptions = {
      namespaces,
      templateClass
    };
    this._builder = new Builder(namespaces, rootNamespace);
    this._stack = [];
    this._globalData = {
      usedTypefaces: new Set()
    };
    this._ids = new Map();
    this._current = new Root();
    this._errorCode = xml_parser.XMLParserErrorCode.NoError;
    this._whiteRegex = /^\s+$/;
    this._nbsps = /\xa0+/g;
    this._richText = richText;
  }
  parse(data) {
    this.parseXml(data);
    if (this._errorCode !== xml_parser.XMLParserErrorCode.NoError) {
      return undefined;
    }
    const root = this._current.element;
    const {
      templateClass
    } = this._xfaOptions;
    if (templateClass && root?.template instanceof templateClass) {
      this._ids.set(symbol_utils.$root, root);
      root.template[symbol_utils.$resolvePrototypes](this._ids);
      root.template[symbol_utils.$ids] = this._ids;
    }
    return root;
  }
  onText(text) {
    text = text.replace(this._nbsps, match => match.slice(1) + " ");
    if (this._richText || this._current[symbol_utils.$acceptWhitespace]()) {
      this._current[symbol_utils.$onText](text, this._richText);
      return;
    }
    if (this._whiteRegex.test(text)) {
      return;
    }
    this._current[symbol_utils.$onText](text.trim());
  }
  onCdata(text) {
    this._current[symbol_utils.$onText](text);
  }
  _mkAttributes(attributes, tagName) {
    let namespace = null;
    let prefixes = null;
    const attributeObj = Object.create({});
    for (const {
      name,
      value
    } of attributes) {
      if (name === "xmlns") {
        if (!namespace) {
          namespace = value;
        } else {
          (0,util.warn)(`XFA - multiple namespace definition in <${tagName}>`);
        }
      } else if (name.startsWith("xmlns:")) {
        const prefix = name.substring("xmlns:".length);
        prefixes ??= [];
        prefixes.push({
          prefix,
          value
        });
      } else {
        const i = name.indexOf(":");
        if (i === -1) {
          attributeObj[name] = value;
        } else {
          const nsAttrs = attributeObj[symbol_utils.$nsAttributes] ??= Object.create(null);
          const [ns, attrName] = [name.slice(0, i), name.slice(i + 1)];
          const attrs = nsAttrs[ns] ||= Object.create(null);
          attrs[attrName] = value;
        }
      }
    }
    return [namespace, prefixes, attributeObj];
  }
  _getNameAndPrefix(name, nsAgnostic) {
    const i = name.indexOf(":");
    if (i === -1) {
      return [name, null];
    }
    return [name.substring(i + 1), nsAgnostic ? "" : name.substring(0, i)];
  }
  onBeginElement(tagName, attributes, isEmpty) {
    const [namespace, prefixes, attributesObj] = this._mkAttributes(attributes, tagName);
    const [name, nsPrefix] = this._getNameAndPrefix(tagName, this._builder.isNsAgnostic());
    const node = this._builder.build({
      nsPrefix,
      name,
      attributes: attributesObj,
      namespace,
      prefixes
    });
    node[symbol_utils.$globalData] = this._globalData;
    if (isEmpty) {
      this.#appendNode(node);
      return;
    }
    this._stack.push(this._current);
    this._current = node;
  }
  onEndElement() {
    const node = this._current;
    if (node[symbol_utils.$isCDATAXml]() && typeof node[symbol_utils.$content] === "string") {
      const parser = new XFAParserBase(this._xfaOptions);
      parser._globalData = this._globalData;
      const root = parser.parse(node[symbol_utils.$content]);
      node[symbol_utils.$content] = null;
      node[symbol_utils.$onChild](root);
    }
    this._current = this._stack.pop();
    this.#appendNode(node);
  }
  #appendNode(node) {
    node[symbol_utils.$finalize]();
    if (this._current[symbol_utils.$onChild](node)) {
      node[symbol_utils.$setId](this._ids);
    }
    node[symbol_utils.$clean](this._builder);
  }
  onError(code) {
    this._errorCode = code;
  }
}


/***/ },

/***/ 3874
(__unused_webpack___webpack_module__, __webpack_exports__, __webpack_require__) {

/* harmony export */ __webpack_require__.d(__webpack_exports__, {
/* harmony export */   getRichTextAsHtml: () => (/* binding */ getRichTextAsHtml)
/* harmony export */ });
/* harmony import */ var _symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__ = __webpack_require__(1318);
/* harmony import */ var _shared_util_js__WEBPACK_IMPORTED_MODULE_1__ = __webpack_require__(4292);
/* harmony import */ var _parser_base_js__WEBPACK_IMPORTED_MODULE_2__ = __webpack_require__(950);
/* harmony import */ var _xhtml_js__WEBPACK_IMPORTED_MODULE_3__ = __webpack_require__(407);




function getRichTextAsHtml(rc) {
  try {
    let root = new _parser_base_js__WEBPACK_IMPORTED_MODULE_2__.XFAParserBase({
      namespaces: {
        xhtml: _xhtml_js__WEBPACK_IMPORTED_MODULE_3__.XhtmlNamespace
      },
      rootNamespace: _xhtml_js__WEBPACK_IMPORTED_MODULE_3__.XhtmlNamespace,
      richText: true
    }).parse(rc);
    if (!["body", "xhtml"].includes(root[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$nodeName])) {
      const newRoot = _xhtml_js__WEBPACK_IMPORTED_MODULE_3__.XhtmlNamespace.body({});
      newRoot[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$appendChild](root);
      root = newRoot;
    }
    const result = root[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$toHTML]();
    if (!result.success) {
      return null;
    }
    const {
      html
    } = result;
    const {
      attributes
    } = html;
    if (attributes) {
      attributes.class &&= attributes.class.filter(attr => !attr.startsWith("xfa"));
      attributes.dir = "auto";
    }
    return {
      html,
      str: root[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$text]()
    };
  } catch (e) {
    ;(0,_shared_util_js__WEBPACK_IMPORTED_MODULE_1__.warn)(`XFA - an error occurred during parsing of rich text: ${e}`);
  }
  return null;
}


/***/ },

/***/ 1723
(__unused_webpack___webpack_module__, __webpack_exports__, __webpack_require__) {

/* harmony export */ __webpack_require__.d(__webpack_exports__, {
/* harmony export */   createDataNode: () => (/* binding */ createDataNode),
/* harmony export */   searchNode: () => (/* binding */ searchNode)
/* harmony export */ });
/* harmony import */ var _symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__ = __webpack_require__(1318);
/* harmony import */ var _shared_util_js__WEBPACK_IMPORTED_MODULE_1__ = __webpack_require__(4292);


const namePattern = /^[^.[]+/;
const indexPattern = /^[^\]]+/;
const operators = {
  dot: 0,
  dotDot: 1,
  dotHash: 2,
  dotBracket: 3,
  dotParen: 4
};
const shortcuts = new Map([["$data", (root, current) => root.datasets ? root.datasets.data : root], ["$record", (root, current) => (root.datasets ? root.datasets.data : root)[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$getChildren]()[0]], ["$template", (root, current) => root.template], ["$connectionSet", (root, current) => root.connectionSet], ["$form", (root, current) => root.form], ["$layout", (root, current) => root.layout], ["$host", (root, current) => root.host], ["$dataWindow", (root, current) => root.dataWindow], ["$event", (root, current) => root.event], ["!", (root, current) => root.datasets], ["$xfa", (root, current) => root], ["xfa", (root, current) => root], ["$", (root, current) => current]]);
const somCache = new WeakMap();
function parseIndex(index) {
  index = index.trim();
  return index === "*" ? Infinity : parseInt(index, 10) || 0;
}
function parseExpression(expr, dotDotAllowed, noExpr = true) {
  let match = expr.match(namePattern);
  if (!match) {
    return null;
  }
  let [name] = match;
  const parsed = [{
    name,
    cacheName: "." + name,
    index: 0,
    js: null,
    formCalc: null,
    operator: operators.dot
  }];
  let pos = name.length;
  while (pos < expr.length) {
    const spos = pos;
    const char = expr.charAt(pos++);
    if (char === "[") {
      match = expr.slice(pos).match(indexPattern);
      if (!match) {
        (0,_shared_util_js__WEBPACK_IMPORTED_MODULE_1__.warn)("XFA - Invalid index in SOM expression");
        return null;
      }
      parsed.at(-1).index = parseIndex(match[0]);
      pos += match[0].length + 1;
      continue;
    }
    let operator;
    switch (expr.charAt(pos)) {
      case ".":
        if (!dotDotAllowed) {
          return null;
        }
        pos++;
        operator = operators.dotDot;
        break;
      case "#":
        pos++;
        operator = operators.dotHash;
        break;
      case "[":
        if (noExpr) {
          (0,_shared_util_js__WEBPACK_IMPORTED_MODULE_1__.warn)("XFA - SOM expression contains a FormCalc subexpression which is not supported for now.");
          return null;
        }
        operator = operators.dotBracket;
        break;
      case "(":
        if (noExpr) {
          (0,_shared_util_js__WEBPACK_IMPORTED_MODULE_1__.warn)("XFA - SOM expression contains a JavaScript subexpression which is not supported for now.");
          return null;
        }
        operator = operators.dotParen;
        break;
      default:
        operator = operators.dot;
        break;
    }
    match = expr.slice(pos).match(namePattern);
    if (!match) {
      break;
    }
    [name] = match;
    pos += name.length;
    parsed.push({
      name,
      cacheName: expr.slice(spos, pos),
      operator,
      index: 0,
      js: null,
      formCalc: null
    });
  }
  return parsed;
}
function searchNode(root, container, expr, dotDotAllowed = true, useCache = true) {
  const parsed = parseExpression(expr, dotDotAllowed);
  if (!parsed) {
    return null;
  }
  const fn = shortcuts.get(parsed[0].name);
  let i = 0;
  let isQualified;
  if (fn) {
    isQualified = true;
    root = [fn(root, container)];
    i = 1;
  } else {
    isQualified = container === null;
    root = [container || root];
  }
  for (let ii = parsed.length; i < ii; i++) {
    const {
      name,
      cacheName,
      operator,
      index
    } = parsed[i];
    const nodes = [];
    for (const node of root) {
      if (!node.isXFAObject) {
        continue;
      }
      let children, cached;
      if (useCache) {
        cached = somCache.getOrInsertComputed(node, _shared_util_js__WEBPACK_IMPORTED_MODULE_1__.makeMap);
        children = cached.get(cacheName);
      }
      if (!children) {
        switch (operator) {
          case operators.dot:
            children = node[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$getChildrenByName](name, false);
            break;
          case operators.dotDot:
            children = node[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$getChildrenByName](name, true);
            break;
          case operators.dotHash:
            children = node[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$getChildrenByClass](name);
            children = children.isXFAObjectArray ? children.children : [children];
            break;
          default:
            break;
        }
        if (useCache) {
          cached.set(cacheName, children);
        }
      }
      if (children.length > 0) {
        nodes.push(children);
      }
    }
    if (nodes.length === 0 && !isQualified && i === 0) {
      const parent = container[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$getParent]();
      container = parent;
      if (!container) {
        return null;
      }
      i = -1;
      root = [container];
      continue;
    }
    root = isFinite(index) ? nodes.filter(node => index < node.length).map(node => node[index]) : nodes.flat();
  }
  return root.length === 0 ? null : root;
}
function createDataNode(root, container, expr) {
  const parsed = parseExpression(expr);
  if (!parsed || parsed.some(x => x.operator === operators.dotDot)) {
    return null;
  }
  const fn = shortcuts.get(parsed[0].name);
  let i = 0;
  if (fn) {
    root = fn(root, container);
    i = 1;
  } else {
    root = container || root;
  }
  for (let ii = parsed.length; i < ii; i++) {
    const {
      name,
      operator,
      index
    } = parsed[i];
    if (!isFinite(index)) {
      parsed[i].index = 0;
      return root.createNodes(parsed.slice(i));
    }
    let children;
    switch (operator) {
      case operators.dot:
        children = root[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$getChildrenByName](name, false);
        break;
      case operators.dotDot:
        children = root[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$getChildrenByName](name, true);
        break;
      case operators.dotHash:
        children = root[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$getChildrenByClass](name);
        children = children.isXFAObjectArray ? children.children : [children];
        break;
      default:
        break;
    }
    if (children.length === 0) {
      return root.createNodes(parsed.slice(i));
    }
    if (index < children.length) {
      const child = children[index];
      if (!child.isXFAObject) {
        (0,_shared_util_js__WEBPACK_IMPORTED_MODULE_1__.warn)(`XFA - Cannot create a node.`);
        return null;
      }
      root = child;
    } else {
      parsed[i].index = index - children.length;
      return root.createNodes(parsed.slice(i));
    }
  }
  return null;
}


/***/ },

/***/ 1318
(__unused_webpack___webpack_module__, __webpack_exports__, __webpack_require__) {

const $acceptWhitespace = Symbol();
const $addHTML = Symbol();
const $appendChild = Symbol();
const $childrenToHTML = Symbol();
const $clean = Symbol();
const $cleanPage = Symbol();
const $cleanup = Symbol();
const $clone = Symbol();
const $consumed = Symbol();
const $content = Symbol("content");
const $data = Symbol("data");
const $dump = Symbol();
const $extra = Symbol("extra");
const $finalize = Symbol();
const $flushHTML = Symbol();
const $getAttributeIt = Symbol();
const $getAttributes = Symbol();
const $getAvailableSpace = Symbol();
const $getChildrenByClass = Symbol();
const $getChildrenByName = Symbol();
const $getChildrenByNameIt = Symbol();
const $getDataValue = Symbol();
const $getExtra = Symbol();
const $getRealChildrenByNameIt = Symbol();
const $getChildren = Symbol();
const $getContainedChildren = Symbol();
const $getNextPage = Symbol();
const $getSubformParent = Symbol();
const $getParent = Symbol();
const $getTemplateRoot = Symbol();
const $globalData = Symbol();
const $hasSettableValue = Symbol();
const $ids = Symbol();
const $indexOf = Symbol();
const $insertAt = Symbol();
const $isCDATAXml = Symbol();
const $isBindable = Symbol();
const $isDataValue = Symbol();
const $isDescendent = Symbol();
const $isNsAgnostic = Symbol();
const $isSplittable = Symbol();
const $isThereMoreWidth = Symbol();
const $isTransparent = Symbol();
const $isUsable = Symbol();
const $lastAttribute = Symbol();
const $namespaceId = Symbol("namespaceId");
const $nodeName = Symbol("nodeName");
const $nsAttributes = Symbol();
const $onChild = Symbol();
const $onChildCheck = Symbol();
const $onText = Symbol();
const $pushGlyphs = Symbol();
const $popPara = Symbol();
const $pushPara = Symbol();
const $removeChild = Symbol();
const $root = Symbol("root");
const $resolvePrototypes = Symbol();
const $searchNode = Symbol();
const $setId = Symbol();
const $setSetAttributes = Symbol();
const $setValue = Symbol();
const $tabIndex = Symbol();
const $text = Symbol();
const $toPages = Symbol();
const $toHTML = Symbol();
const $toString = Symbol();
const $toStyle = Symbol();
const $uid = Symbol("uid");

/* harmony export */ __webpack_require__.d(__webpack_exports__, [
/* harmony export */   "$acceptWhitespace", 0, /* binding */ $acceptWhitespace,
/* harmony export */   "$addHTML", 0, /* binding */ $addHTML,
/* harmony export */   "$appendChild", 0, /* binding */ $appendChild,
/* harmony export */   "$childrenToHTML", 0, /* binding */ $childrenToHTML,
/* harmony export */   "$clean", 0, /* binding */ $clean,
/* harmony export */   "$cleanPage", 0, /* binding */ $cleanPage,
/* harmony export */   "$cleanup", 0, /* binding */ $cleanup,
/* harmony export */   "$clone", 0, /* binding */ $clone,
/* harmony export */   "$consumed", 0, /* binding */ $consumed,
/* harmony export */   "$content", 0, /* binding */ $content,
/* harmony export */   "$data", 0, /* binding */ $data,
/* harmony export */   "$dump", 0, /* binding */ $dump,
/* harmony export */   "$extra", 0, /* binding */ $extra,
/* harmony export */   "$finalize", 0, /* binding */ $finalize,
/* harmony export */   "$flushHTML", 0, /* binding */ $flushHTML,
/* harmony export */   "$getAttributeIt", 0, /* binding */ $getAttributeIt,
/* harmony export */   "$getAttributes", 0, /* binding */ $getAttributes,
/* harmony export */   "$getAvailableSpace", 0, /* binding */ $getAvailableSpace,
/* harmony export */   "$getChildren", 0, /* binding */ $getChildren,
/* harmony export */   "$getChildrenByClass", 0, /* binding */ $getChildrenByClass,
/* harmony export */   "$getChildrenByName", 0, /* binding */ $getChildrenByName,
/* harmony export */   "$getChildrenByNameIt", 0, /* binding */ $getChildrenByNameIt,
/* harmony export */   "$getContainedChildren", 0, /* binding */ $getContainedChildren,
/* harmony export */   "$getDataValue", 0, /* binding */ $getDataValue,
/* harmony export */   "$getExtra", 0, /* binding */ $getExtra,
/* harmony export */   "$getNextPage", 0, /* binding */ $getNextPage,
/* harmony export */   "$getParent", 0, /* binding */ $getParent,
/* harmony export */   "$getRealChildrenByNameIt", 0, /* binding */ $getRealChildrenByNameIt,
/* harmony export */   "$getSubformParent", 0, /* binding */ $getSubformParent,
/* harmony export */   "$getTemplateRoot", 0, /* binding */ $getTemplateRoot,
/* harmony export */   "$globalData", 0, /* binding */ $globalData,
/* harmony export */   "$hasSettableValue", 0, /* binding */ $hasSettableValue,
/* harmony export */   "$ids", 0, /* binding */ $ids,
/* harmony export */   "$indexOf", 0, /* binding */ $indexOf,
/* harmony export */   "$insertAt", 0, /* binding */ $insertAt,
/* harmony export */   "$isBindable", 0, /* binding */ $isBindable,
/* harmony export */   "$isCDATAXml", 0, /* binding */ $isCDATAXml,
/* harmony export */   "$isDataValue", 0, /* binding */ $isDataValue,
/* harmony export */   "$isDescendent", 0, /* binding */ $isDescendent,
/* harmony export */   "$isNsAgnostic", 0, /* binding */ $isNsAgnostic,
/* harmony export */   "$isSplittable", 0, /* binding */ $isSplittable,
/* harmony export */   "$isThereMoreWidth", 0, /* binding */ $isThereMoreWidth,
/* harmony export */   "$isTransparent", 0, /* binding */ $isTransparent,
/* harmony export */   "$isUsable", 0, /* binding */ $isUsable,
/* harmony export */   "$lastAttribute", 0, /* binding */ $lastAttribute,
/* harmony export */   "$namespaceId", 0, /* binding */ $namespaceId,
/* harmony export */   "$nodeName", 0, /* binding */ $nodeName,
/* harmony export */   "$nsAttributes", 0, /* binding */ $nsAttributes,
/* harmony export */   "$onChild", 0, /* binding */ $onChild,
/* harmony export */   "$onChildCheck", 0, /* binding */ $onChildCheck,
/* harmony export */   "$onText", 0, /* binding */ $onText,
/* harmony export */   "$popPara", 0, /* binding */ $popPara,
/* harmony export */   "$pushGlyphs", 0, /* binding */ $pushGlyphs,
/* harmony export */   "$pushPara", 0, /* binding */ $pushPara,
/* harmony export */   "$removeChild", 0, /* binding */ $removeChild,
/* harmony export */   "$resolvePrototypes", 0, /* binding */ $resolvePrototypes,
/* harmony export */   "$root", 0, /* binding */ $root,
/* harmony export */   "$searchNode", 0, /* binding */ $searchNode,
/* harmony export */   "$setId", 0, /* binding */ $setId,
/* harmony export */   "$setSetAttributes", 0, /* binding */ $setSetAttributes,
/* harmony export */   "$setValue", 0, /* binding */ $setValue,
/* harmony export */   "$tabIndex", 0, /* binding */ $tabIndex,
/* harmony export */   "$text", 0, /* binding */ $text,
/* harmony export */   "$toHTML", 0, /* binding */ $toHTML,
/* harmony export */   "$toPages", 0, /* binding */ $toPages,
/* harmony export */   "$toString", 0, /* binding */ $toString,
/* harmony export */   "$toStyle", 0, /* binding */ $toStyle,
/* harmony export */   "$uid", 0, /* binding */ $uid
/* harmony export */ ]);


/***/ },

/***/ 8063
(__unused_webpack___webpack_module__, __webpack_exports__, __webpack_require__) {

/* harmony export */ __webpack_require__.d(__webpack_exports__, {
/* harmony export */   HTMLResult: () => (/* binding */ HTMLResult),
/* harmony export */   getBBox: () => (/* binding */ getBBox),
/* harmony export */   getColor: () => (/* binding */ getColor),
/* harmony export */   getFloat: () => (/* binding */ getFloat),
/* harmony export */   getInteger: () => (/* binding */ getInteger),
/* harmony export */   getKeyword: () => (/* binding */ getKeyword),
/* harmony export */   getMeasurement: () => (/* binding */ getMeasurement),
/* harmony export */   getRatio: () => (/* binding */ getRatio),
/* harmony export */   getRelevant: () => (/* binding */ getRelevant),
/* harmony export */   getStringOption: () => (/* binding */ getStringOption),
/* harmony export */   stripQuotes: () => (/* binding */ stripQuotes)
/* harmony export */ });
/* harmony import */ var _shared_math_clamp_js__WEBPACK_IMPORTED_MODULE_0__ = /*#__PURE__*/__webpack_require__(4566);
/* harmony import */ var _shared_util_js__WEBPACK_IMPORTED_MODULE_1__ = __webpack_require__(4292);


const dimConverters = {
  pt: x => x,
  cm: x => x / 2.54 * 72,
  mm: x => x / (10 * 2.54) * 72,
  in: x => x * 72,
  px: x => x
};
const measurementPattern = /([+-]?\d+\.?\d*)(.*)/;
function stripQuotes(str) {
  return str.startsWith("'") || str.startsWith('"') ? str.slice(1, -1) : str;
}
function getInteger({
  data,
  defaultValue,
  validate
}) {
  if (!data) {
    return defaultValue;
  }
  data = data.trim();
  const n = parseInt(data, 10);
  return !isNaN(n) && validate(n) ? n : defaultValue;
}
function getFloat({
  data,
  defaultValue,
  validate
}) {
  if (!data) {
    return defaultValue;
  }
  data = data.trim();
  const n = parseFloat(data);
  return !isNaN(n) && validate(n) ? n : defaultValue;
}
function getKeyword({
  data,
  defaultValue,
  validate
}) {
  if (!data) {
    return defaultValue;
  }
  data = data.trim();
  return validate(data) ? data : defaultValue;
}
function getStringOption(data, options) {
  return getKeyword({
    data,
    defaultValue: options[0],
    validate: k => options.includes(k)
  });
}
function getMeasurement(str, def = "0") {
  def ||= "0";
  if (!str) {
    return getMeasurement(def);
  }
  const match = str.trim().match(measurementPattern);
  if (!match) {
    return getMeasurement(def);
  }
  const [, valueStr, unit] = match;
  const value = parseFloat(valueStr);
  if (isNaN(value)) {
    return getMeasurement(def);
  }
  if (value === 0) {
    return 0;
  }
  const conv = dimConverters[unit];
  return conv ? conv(value) : value;
}
function getRatio(data) {
  if (!data) {
    return {
      num: 1,
      den: 1
    };
  }
  const ratio = data.split(":", 2).map(x => parseFloat(x.trim())).filter(x => !isNaN(x));
  if (ratio.length === 1) {
    ratio.push(1);
  }
  if (ratio.length === 0) {
    return {
      num: 1,
      den: 1
    };
  }
  const [num, den] = ratio;
  return {
    num,
    den
  };
}
function getRelevant(data) {
  if (!data) {
    return [];
  }
  return data.trim().split(/\s+/).map(e => ({
    excluded: e[0] === "-",
    viewname: e.substring(1)
  }));
}
function getColor(data, def = [0, 0, 0]) {
  let [r, g, b] = def;
  if (!data) {
    return {
      r,
      g,
      b
    };
  }
  const color = data.split(",", 3).map(c => {
    c = parseInt(c.trim(), 10);
    return isNaN(c) ? 0 : (0,_shared_math_clamp_js__WEBPACK_IMPORTED_MODULE_0__.MathClamp)(c, 0, 255);
  });
  if (color.length < 3) {
    return {
      r,
      g,
      b
    };
  }
  [r, g, b] = color;
  return {
    r,
    g,
    b
  };
}
function getBBox(data) {
  const def = -1;
  if (!data) {
    return {
      x: def,
      y: def,
      width: def,
      height: def
    };
  }
  const bbox = data.split(",", 4).map(m => getMeasurement(m.trim(), "-1"));
  if (bbox.length < 4 || bbox[2] < 0 || bbox[3] < 0) {
    return {
      x: def,
      y: def,
      width: def,
      height: def
    };
  }
  const [x, y, width, height] = bbox;
  return {
    x,
    y,
    width,
    height
  };
}
class HTMLResult {
  static get FAILURE() {
    return (0,_shared_util_js__WEBPACK_IMPORTED_MODULE_1__.shadow)(this, "FAILURE", new HTMLResult(false, null, null, null));
  }
  static get EMPTY() {
    return (0,_shared_util_js__WEBPACK_IMPORTED_MODULE_1__.shadow)(this, "EMPTY", new HTMLResult(true, null, null, null));
  }
  constructor(success, html, bbox, breakNode) {
    this.success = success;
    this.html = html;
    this.bbox = bbox;
    this.breakNode = breakNode;
  }
  isBreak() {
    return !!this.breakNode;
  }
  static breakNode(node) {
    return new HTMLResult(false, null, null, node);
  }
  static success(html, bbox = null) {
    return new HTMLResult(true, html, bbox, null);
  }
}


/***/ },

/***/ 7485
(__unused_webpack___webpack_module__, __webpack_exports__, __webpack_require__) {

/* harmony export */ __webpack_require__.d(__webpack_exports__, {
/* harmony export */   ContentObject: () => (/* binding */ ContentObject),
/* harmony export */   IntegerObject: () => (/* binding */ IntegerObject),
/* harmony export */   Option01: () => (/* binding */ Option01),
/* harmony export */   Option10: () => (/* binding */ Option10),
/* harmony export */   OptionObject: () => (/* binding */ OptionObject),
/* harmony export */   StringObject: () => (/* binding */ StringObject),
/* harmony export */   XFAAttribute: () => (/* binding */ XFAAttribute),
/* harmony export */   XFAObject: () => (/* binding */ XFAObject),
/* harmony export */   XFAObjectArray: () => (/* binding */ XFAObjectArray),
/* harmony export */   XmlObject: () => (/* binding */ XmlObject)
/* harmony export */ });
/* harmony import */ var _symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__ = __webpack_require__(1318);
/* harmony import */ var _utils_js__WEBPACK_IMPORTED_MODULE_1__ = __webpack_require__(8063);
/* harmony import */ var _shared_util_js__WEBPACK_IMPORTED_MODULE_2__ = __webpack_require__(4292);
/* harmony import */ var _core_utils_js__WEBPACK_IMPORTED_MODULE_3__ = __webpack_require__(5399);
/* harmony import */ var _namespaces_js__WEBPACK_IMPORTED_MODULE_4__ = __webpack_require__(7088);
/* harmony import */ var _som_js__WEBPACK_IMPORTED_MODULE_5__ = __webpack_require__(1723);






const _applyPrototype = Symbol();
const _attributes = Symbol();
const _attributeNames = Symbol();
const _children = Symbol("_children");
const _cloneAttribute = Symbol();
const _dataValue = Symbol();
const _defaultValue = Symbol();
const _filteredChildrenGenerator = Symbol();
const _getPrototype = Symbol();
const _getUnsetAttributes = Symbol();
const _hasChildren = Symbol();
const _max = Symbol();
const _options = Symbol();
const _parent = Symbol("parent");
const _resolvePrototypesHelper = Symbol();
const _setAttributes = Symbol();
const _validator = Symbol();
let uid = 0;
const NS_DATASETS = _namespaces_js__WEBPACK_IMPORTED_MODULE_4__.NamespaceIds.datasets.id;
class XFAObject {
  constructor(nsId, name, hasChildren = false) {
    this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$namespaceId] = nsId;
    this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$nodeName] = name;
    this[_hasChildren] = hasChildren;
    this[_parent] = null;
    this[_children] = [];
    this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$uid] = `${name}${uid++}`;
    this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$globalData] = null;
  }
  get isXFAObject() {
    return true;
  }
  get isXFAObjectArray() {
    return false;
  }
  createNodes(path) {
    let root = this,
      node = null;
    for (const {
      name,
      index
    } of path) {
      for (let i = 0, ii = isFinite(index) ? index : 0; i <= ii; i++) {
        const nsId = root[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$namespaceId] === NS_DATASETS ? -1 : root[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$namespaceId];
        node = new XmlObject(nsId, name);
        root[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$appendChild](node);
      }
      root = node;
    }
    return node;
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$onChild](child) {
    if (!this[_hasChildren] || !this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$onChildCheck](child)) {
      return false;
    }
    const name = child[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$nodeName];
    const node = this[name];
    if (node instanceof XFAObjectArray) {
      if (node.push(child)) {
        this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$appendChild](child);
        return true;
      }
    } else {
      if (node !== null) {
        this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$removeChild](node);
      }
      this[name] = child;
      this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$appendChild](child);
      return true;
    }
    let id = "";
    if (this.id) {
      id = ` (id: ${this.id})`;
    } else if (this.name) {
      id = ` (name: ${this.name} ${this.h.value})`;
    }
    ;(0,_shared_util_js__WEBPACK_IMPORTED_MODULE_2__.warn)(`XFA - node "${this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$nodeName]}"${id} has already enough "${name}"!`);
    return false;
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$onChildCheck](child) {
    return Object.hasOwn(this, child[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$nodeName]) && child[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$namespaceId] === this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$namespaceId];
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$isNsAgnostic]() {
    return false;
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$acceptWhitespace]() {
    return false;
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$isCDATAXml]() {
    return false;
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$isBindable]() {
    return false;
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$popPara]() {
    if (this.para) {
      this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$getTemplateRoot]()[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$extra].paraStack.pop();
    }
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$pushPara]() {
    this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$getTemplateRoot]()[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$extra].paraStack.push(this.para);
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$setId](ids) {
    if (this.id && this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$namespaceId] === _namespaces_js__WEBPACK_IMPORTED_MODULE_4__.NamespaceIds.template.id) {
      ids.set(this.id, this);
    }
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$getTemplateRoot]() {
    return this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$globalData].template;
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$isSplittable]() {
    return false;
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$isThereMoreWidth]() {
    return false;
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$appendChild](child) {
    child[_parent] = this;
    this[_children].push(child);
    if (!child[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$globalData] && this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$globalData]) {
      child[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$globalData] = this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$globalData];
    }
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$removeChild](child) {
    const i = this[_children].indexOf(child);
    this[_children].splice(i, 1);
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$hasSettableValue]() {
    return Object.hasOwn(this, "value");
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$setValue](_) {}
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$onText](_) {}
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$finalize]() {}
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$clean](builder) {
    delete this[_hasChildren];
    if (this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$cleanup]) {
      builder.clean(this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$cleanup]);
      delete this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$cleanup];
    }
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$indexOf](child) {
    return this[_children].indexOf(child);
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$insertAt](i, child) {
    child[_parent] = this;
    this[_children].splice(i, 0, child);
    if (!child[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$globalData] && this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$globalData]) {
      child[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$globalData] = this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$globalData];
    }
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$isTransparent]() {
    return !this.name;
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$lastAttribute]() {
    return "";
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$text]() {
    return this[_children].length === 0 ? this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$content] : this[_children].map(c => c[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$text]()).join("");
  }
  get [_attributeNames]() {
    const proto = Object.getPrototypeOf(this);
    if (!proto._attributes) {
      const attributes = proto._attributes = new Set();
      for (const name of Object.getOwnPropertyNames(this)) {
        if (this[name] === null || this[name] instanceof XFAObject || this[name] instanceof XFAObjectArray) {
          break;
        }
        attributes.add(name);
      }
    }
    return (0,_shared_util_js__WEBPACK_IMPORTED_MODULE_2__.shadow)(this, _attributeNames, proto._attributes);
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$isDescendent](parent) {
    let node = this;
    while (node) {
      if (node === parent) {
        return true;
      }
      node = node[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$getParent]();
    }
    return false;
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$getParent]() {
    return this[_parent];
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$getSubformParent]() {
    return this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$getParent]();
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$dump]() {
    const dumped = Object.create(null);
    if (this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$content]) {
      dumped.$content = this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$content];
    }
    for (const name of Object.getOwnPropertyNames(this)) {
      const value = this[name];
      if (value === null) {
        continue;
      }
      if (value instanceof XFAObject) {
        dumped[name] = value[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$dump]();
      } else if (value instanceof XFAObjectArray) {
        if (!value.isEmpty()) {
          dumped[name] = value.dump();
        }
      } else {
        dumped[name] = value;
      }
    }
    return dumped;
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$toStyle]() {
    return null;
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$toHTML]() {
    return _utils_js__WEBPACK_IMPORTED_MODULE_1__.HTMLResult.EMPTY;
  }
  *[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$getContainedChildren]() {
    for (const node of this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$getChildren]()) {
      yield node;
    }
  }
  *[_filteredChildrenGenerator](filter, include) {
    for (const node of this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$getContainedChildren]()) {
      if (!filter || include === filter.has(node[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$nodeName])) {
        const availableSpace = this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$getAvailableSpace]();
        const res = node[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$toHTML](availableSpace);
        if (!res.success) {
          this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$extra].failingNode = node;
        }
        yield res;
      }
    }
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$flushHTML]() {
    return null;
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$addHTML](html, bbox) {
    this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$extra].children.push(html);
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$getAvailableSpace]() {}
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$childrenToHTML]({
    filter = null,
    include = true
  }) {
    if (!this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$extra].generator) {
      this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$extra].generator = this[_filteredChildrenGenerator](filter, include);
    } else {
      const availableSpace = this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$getAvailableSpace]();
      const res = this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$extra].failingNode[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$toHTML](availableSpace);
      if (!res.success) {
        return res;
      }
      if (res.html) {
        this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$addHTML](res.html, res.bbox);
      }
      delete this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$extra].failingNode;
    }
    while (true) {
      const gen = this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$extra].generator.next();
      if (gen.done) {
        break;
      }
      const res = gen.value;
      if (!res.success) {
        return res;
      }
      if (res.html) {
        this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$addHTML](res.html, res.bbox);
      }
    }
    this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$extra].generator = null;
    return _utils_js__WEBPACK_IMPORTED_MODULE_1__.HTMLResult.EMPTY;
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$setSetAttributes](attributes) {
    this[_setAttributes] = new Set(Object.keys(attributes));
  }
  [_getUnsetAttributes](protoAttributes) {
    const allAttr = this[_attributeNames];
    const setAttr = this[_setAttributes];
    return protoAttributes.keys().filter(x => allAttr.has(x) && !setAttr.has(x)).toArray();
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$resolvePrototypes](ids, ancestors = new Set()) {
    for (const child of this[_children]) {
      child[_resolvePrototypesHelper](ids, ancestors);
    }
  }
  [_resolvePrototypesHelper](ids, ancestors) {
    const proto = this[_getPrototype](ids, ancestors);
    if (proto) {
      this[_applyPrototype](proto, ids, ancestors);
    } else {
      this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$resolvePrototypes](ids, ancestors);
    }
  }
  [_getPrototype](ids, ancestors) {
    const {
      use,
      usehref
    } = this;
    if (!use && !usehref) {
      return null;
    }
    let proto = null;
    let somExpression = null;
    let id = null;
    let ref = use;
    if (usehref) {
      ref = usehref;
      if (usehref.startsWith("#som(") && usehref.endsWith(")")) {
        somExpression = usehref.slice("#som(".length, -1);
      } else if (usehref.startsWith(".#som(") && usehref.endsWith(")")) {
        somExpression = usehref.slice(".#som(".length, -1);
      } else if (usehref.startsWith("#")) {
        id = usehref.slice(1);
      } else if (usehref.startsWith(".#")) {
        id = usehref.slice(2);
      }
    } else if (use.startsWith("#")) {
      id = use.slice(1);
    } else {
      somExpression = use;
    }
    this.use = this.usehref = "";
    if (id) {
      proto = ids.get(id);
    } else {
      proto = (0,_som_js__WEBPACK_IMPORTED_MODULE_5__.searchNode)(ids.get(_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$root), this, somExpression, true, false);
      proto &&= proto[0];
    }
    if (!proto) {
      (0,_shared_util_js__WEBPACK_IMPORTED_MODULE_2__.warn)(`XFA - Invalid prototype reference: ${ref}.`);
      return null;
    }
    if (proto[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$nodeName] !== this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$nodeName]) {
      (0,_shared_util_js__WEBPACK_IMPORTED_MODULE_2__.warn)(`XFA - Incompatible prototype: ${proto[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$nodeName]} !== ${this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$nodeName]}.`);
      return null;
    }
    if (ancestors.has(proto)) {
      (0,_shared_util_js__WEBPACK_IMPORTED_MODULE_2__.warn)(`XFA - Cycle detected in prototypes use.`);
      return null;
    }
    ancestors.add(proto);
    const protoProto = proto[_getPrototype](ids, ancestors);
    if (protoProto) {
      proto[_applyPrototype](protoProto, ids, ancestors);
    }
    proto[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$resolvePrototypes](ids, ancestors);
    ancestors.delete(proto);
    return proto;
  }
  [_applyPrototype](proto, ids, ancestors) {
    if (ancestors.has(proto)) {
      (0,_shared_util_js__WEBPACK_IMPORTED_MODULE_2__.warn)(`XFA - Cycle detected in prototypes use.`);
      return;
    }
    if (!this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$content] && proto[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$content]) {
      this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$content] = proto[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$content];
    }
    const newAncestors = new Set(ancestors);
    newAncestors.add(proto);
    for (const unsetAttrName of this[_getUnsetAttributes](proto[_setAttributes])) {
      this[unsetAttrName] = proto[unsetAttrName];
      if (this[_setAttributes]) {
        this[_setAttributes].add(unsetAttrName);
      }
    }
    for (const name of Object.getOwnPropertyNames(this)) {
      if (this[_attributeNames].has(name)) {
        continue;
      }
      const value = this[name];
      const protoValue = proto[name];
      if (value instanceof XFAObjectArray) {
        for (const child of value[_children]) {
          child[_resolvePrototypesHelper](ids, ancestors);
        }
        for (let i = value[_children].length, ii = protoValue[_children].length; i < ii; i++) {
          const child = proto[_children][i][_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$clone]();
          if (value.push(child)) {
            child[_parent] = this;
            this[_children].push(child);
            child[_resolvePrototypesHelper](ids, ancestors);
          } else {
            break;
          }
        }
        continue;
      }
      if (value !== null) {
        value[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$resolvePrototypes](ids, ancestors);
        if (protoValue) {
          value[_applyPrototype](protoValue, ids, ancestors);
        }
        continue;
      }
      if (protoValue !== null) {
        const child = protoValue[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$clone]();
        child[_parent] = this;
        this[name] = child;
        this[_children].push(child);
        child[_resolvePrototypesHelper](ids, ancestors);
      }
    }
  }
  static [_cloneAttribute](obj) {
    if (Array.isArray(obj)) {
      return obj.map(x => XFAObject[_cloneAttribute](x));
    }
    return typeof obj === "object" && obj !== null ? Object.assign({}, obj) : obj;
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$clone]() {
    const clone = Object.create(Object.getPrototypeOf(this));
    for (const $symbol of Object.getOwnPropertySymbols(this)) {
      try {
        clone[$symbol] = this[$symbol];
      } catch {
        ;(0,_shared_util_js__WEBPACK_IMPORTED_MODULE_2__.shadow)(clone, $symbol, this[$symbol]);
      }
    }
    clone[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$uid] = `${clone[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$nodeName]}${uid++}`;
    clone[_children] = [];
    for (const name of Object.getOwnPropertyNames(this)) {
      if (this[_attributeNames].has(name)) {
        clone[name] = XFAObject[_cloneAttribute](this[name]);
        continue;
      }
      const value = this[name];
      clone[name] = value instanceof XFAObjectArray ? new XFAObjectArray(value[_max]) : null;
    }
    for (const child of this[_children]) {
      const name = child[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$nodeName];
      const clonedChild = child[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$clone]();
      clone[_children].push(clonedChild);
      clonedChild[_parent] = clone;
      if (clone[name] === null) {
        clone[name] = clonedChild;
      } else {
        clone[name][_children].push(clonedChild);
      }
    }
    return clone;
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$getChildren](name = null) {
    return !name ? this[_children] : this[_children].filter(c => c[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$nodeName] === name);
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$getChildrenByClass](name) {
    return this[name];
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$getChildrenByName](name, allTransparent, first = true) {
    return Array.from(this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$getChildrenByNameIt](name, allTransparent, first));
  }
  *[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$getChildrenByNameIt](name, allTransparent, first = true) {
    if (name === "parent") {
      yield this[_parent];
      return;
    }
    for (const child of this[_children]) {
      if (child[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$nodeName] === name) {
        yield child;
      }
      if (child.name === name) {
        yield child;
      }
      if (allTransparent || child[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$isTransparent]()) {
        yield* child[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$getChildrenByNameIt](name, allTransparent, false);
      }
    }
    if (first && this[_attributeNames].has(name)) {
      yield new XFAAttribute(this, name, this[name]);
    }
  }
}
class XFAObjectArray {
  constructor(max = Infinity) {
    this[_max] = max;
    this[_children] = [];
  }
  get isXFAObject() {
    return false;
  }
  get isXFAObjectArray() {
    return true;
  }
  push(child) {
    const len = this[_children].length;
    if (len <= this[_max]) {
      this[_children].push(child);
      return true;
    }
    ;(0,_shared_util_js__WEBPACK_IMPORTED_MODULE_2__.warn)(`XFA - node "${child[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$nodeName]}" accepts no more than ${this[_max]} children`);
    return false;
  }
  isEmpty() {
    return this[_children].length === 0;
  }
  dump() {
    return this[_children].length === 1 ? this[_children][0][_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$dump]() : this[_children].map(x => x[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$dump]());
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$clone]() {
    const clone = new XFAObjectArray(this[_max]);
    clone[_children] = this[_children].map(c => c[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$clone]());
    return clone;
  }
  get children() {
    return this[_children];
  }
  clear() {
    this[_children].length = 0;
  }
}
class XFAAttribute {
  constructor(node, name, value) {
    this[_parent] = node;
    this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$nodeName] = name;
    this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$content] = value;
    this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$consumed] = false;
    this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$uid] = `attribute${uid++}`;
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$getParent]() {
    return this[_parent];
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$isDataValue]() {
    return true;
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$getDataValue]() {
    return this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$content].trim();
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$setValue](value) {
    value = value.value || "";
    this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$content] = value.toString();
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$text]() {
    return this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$content];
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$isDescendent](parent) {
    return this[_parent] === parent || this[_parent][_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$isDescendent](parent);
  }
}
class XmlObject extends XFAObject {
  constructor(nsId, name, attributes = {}) {
    super(nsId, name);
    this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$content] = "";
    this[_dataValue] = null;
    if (name !== "#text") {
      const map = new Map();
      this[_attributes] = map;
      for (const [attrName, value] of Object.entries(attributes)) {
        map.set(attrName, new XFAAttribute(this, attrName, value));
      }
      if (Object.hasOwn(attributes, _symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$nsAttributes)) {
        const dataNode = attributes[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$nsAttributes].xfa.dataNode;
        if (dataNode !== undefined) {
          if (dataNode === "dataGroup") {
            this[_dataValue] = false;
          } else if (dataNode === "dataValue") {
            this[_dataValue] = true;
          }
        }
      }
    }
    this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$consumed] = false;
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$toString](buf) {
    const tagName = this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$nodeName];
    if (tagName === "#text") {
      buf.push((0,_core_utils_js__WEBPACK_IMPORTED_MODULE_3__.encodeToXmlString)(this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$content]));
      return;
    }
    const utf8TagName = (0,_shared_util_js__WEBPACK_IMPORTED_MODULE_2__.utf8StringToString)(tagName);
    const prefix = this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$namespaceId] === NS_DATASETS ? "xfa:" : "";
    buf.push(`<${prefix}${utf8TagName}`);
    for (const [name, value] of this[_attributes]) {
      const utf8Name = (0,_shared_util_js__WEBPACK_IMPORTED_MODULE_2__.utf8StringToString)(name);
      buf.push(` ${utf8Name}="${(0,_core_utils_js__WEBPACK_IMPORTED_MODULE_3__.encodeToXmlString)(value[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$content])}"`);
    }
    if (this[_dataValue] !== null) {
      if (this[_dataValue]) {
        buf.push(` xfa:dataNode="dataValue"`);
      } else {
        buf.push(` xfa:dataNode="dataGroup"`);
      }
    }
    if (!this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$content] && this[_children].length === 0) {
      buf.push("/>");
      return;
    }
    buf.push(">");
    if (this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$content]) {
      if (typeof this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$content] === "string") {
        buf.push((0,_core_utils_js__WEBPACK_IMPORTED_MODULE_3__.encodeToXmlString)(this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$content]));
      } else {
        this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$content][_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$toString](buf);
      }
    } else {
      for (const child of this[_children]) {
        child[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$toString](buf);
      }
    }
    buf.push(`</${prefix}${utf8TagName}>`);
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$onChild](child) {
    if (this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$content]) {
      const node = new XmlObject(this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$namespaceId], "#text");
      this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$appendChild](node);
      node[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$content] = this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$content];
      this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$content] = "";
    }
    this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$appendChild](child);
    return true;
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$onText](str) {
    this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$content] += str;
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$finalize]() {
    if (this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$content] && this[_children].length > 0) {
      const node = new XmlObject(this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$namespaceId], "#text");
      this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$appendChild](node);
      node[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$content] = this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$content];
      delete this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$content];
    }
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$toHTML]() {
    if (this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$nodeName] === "#text") {
      return _utils_js__WEBPACK_IMPORTED_MODULE_1__.HTMLResult.success({
        name: "#text",
        value: this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$content]
      });
    }
    return _utils_js__WEBPACK_IMPORTED_MODULE_1__.HTMLResult.EMPTY;
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$getAttributes]() {
    return this[_attributes];
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$getChildrenByClass](name) {
    const value = this[_attributes].get(name);
    return value !== undefined ? value : this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$getChildren](name);
  }
  *[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$getChildrenByNameIt](name, allTransparent) {
    const value = this[_attributes].get(name);
    if (value) {
      yield value;
    }
    for (const child of this[_children]) {
      if (child[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$nodeName] === name) {
        yield child;
      }
      if (allTransparent) {
        yield* child[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$getChildrenByNameIt](name, allTransparent);
      }
    }
  }
  *[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$getAttributeIt](name, skipConsumed) {
    const value = this[_attributes].get(name);
    if (value && (!skipConsumed || !value[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$consumed])) {
      yield value;
    }
    for (const child of this[_children]) {
      yield* child[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$getAttributeIt](name, skipConsumed);
    }
  }
  *[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$getRealChildrenByNameIt](name, allTransparent, skipConsumed) {
    for (const child of this[_children]) {
      if (child[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$nodeName] === name && (!skipConsumed || !child[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$consumed])) {
        yield child;
      }
      if (allTransparent) {
        yield* child[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$getRealChildrenByNameIt](name, allTransparent, skipConsumed);
      }
    }
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$isDataValue]() {
    if (this[_dataValue] === null) {
      return this[_children].length === 0 || this[_children][0][_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$namespaceId] === _namespaces_js__WEBPACK_IMPORTED_MODULE_4__.NamespaceIds.xhtml.id;
    }
    return this[_dataValue];
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$getDataValue]() {
    if (this[_dataValue] === null) {
      if (this[_children].length === 0) {
        return this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$content].trim();
      }
      return this[_children][0][_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$namespaceId] === _namespaces_js__WEBPACK_IMPORTED_MODULE_4__.NamespaceIds.xhtml.id ? this[_children][0][_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$text]().trim() : null;
    }
    return this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$content].trim();
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$setValue](value) {
    value = value.value || "";
    this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$content] = value.toString();
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$dump](hasNS = false) {
    const dumped = Object.create(null);
    if (hasNS) {
      dumped.$ns = this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$namespaceId];
    }
    if (this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$content]) {
      dumped.$content = this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$content];
    }
    dumped.$name = this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$nodeName];
    dumped.children = [];
    for (const child of this[_children]) {
      dumped.children.push(child[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$dump](hasNS));
    }
    dumped.attributes = Object.create(null);
    for (const [name, value] of this[_attributes]) {
      dumped.attributes[name] = value[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$content];
    }
    return dumped;
  }
}
class ContentObject extends XFAObject {
  constructor(nsId, name) {
    super(nsId, name);
    this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$content] = "";
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$onText](text) {
    this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$content] += text;
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$finalize]() {}
}
class OptionObject extends ContentObject {
  constructor(nsId, name, options) {
    super(nsId, name);
    this[_options] = options;
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$finalize]() {
    this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$content] = (0,_utils_js__WEBPACK_IMPORTED_MODULE_1__.getKeyword)({
      data: this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$content],
      defaultValue: this[_options][0],
      validate: k => this[_options].includes(k)
    });
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$clean](builder) {
    super[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$clean](builder);
    delete this[_options];
  }
}
class StringObject extends ContentObject {
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$finalize]() {
    this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$content] = this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$content].trim();
  }
}
class IntegerObject extends ContentObject {
  constructor(nsId, name, defaultValue, validator) {
    super(nsId, name);
    this[_defaultValue] = defaultValue;
    this[_validator] = validator;
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$finalize]() {
    this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$content] = (0,_utils_js__WEBPACK_IMPORTED_MODULE_1__.getInteger)({
      data: this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$content],
      defaultValue: this[_defaultValue],
      validate: this[_validator]
    });
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$clean](builder) {
    super[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$clean](builder);
    delete this[_defaultValue];
    delete this[_validator];
  }
}
class Option01 extends IntegerObject {
  constructor(nsId, name) {
    super(nsId, name, 0, n => n === 1);
  }
}
class Option10 extends IntegerObject {
  constructor(nsId, name) {
    super(nsId, name, 1, n => n === 0);
  }
}


/***/ },

/***/ 407
(__unused_webpack___webpack_module__, __webpack_exports__, __webpack_require__) {

/* harmony export */ __webpack_require__.d(__webpack_exports__, {
/* harmony export */   XhtmlNamespace: () => (/* binding */ XhtmlNamespace)
/* harmony export */ });
/* harmony import */ var _symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__ = __webpack_require__(1318);
/* harmony import */ var _namespaces_js__WEBPACK_IMPORTED_MODULE_1__ = __webpack_require__(7088);
/* harmony import */ var _html_utils_js__WEBPACK_IMPORTED_MODULE_2__ = __webpack_require__(1037);
/* harmony import */ var _utils_js__WEBPACK_IMPORTED_MODULE_3__ = __webpack_require__(8063);
/* harmony import */ var _xfa_object_js__WEBPACK_IMPORTED_MODULE_4__ = __webpack_require__(7485);





const XHTML_NS_ID = _namespaces_js__WEBPACK_IMPORTED_MODULE_1__.NamespaceIds.xhtml.id;
const $richText = Symbol();
const VALID_STYLES = new Set(["color", "font", "font-family", "font-size", "font-stretch", "font-style", "font-weight", "margin", "margin-bottom", "margin-left", "margin-right", "margin-top", "letter-spacing", "line-height", "orphans", "page-break-after", "page-break-before", "page-break-inside", "tab-interval", "tab-stop", "text-align", "text-decoration", "text-indent", "vertical-align", "widows", "kerning-mode", "xfa-font-horizontal-scale", "xfa-font-vertical-scale", "xfa-spacerun", "xfa-tab-stops"]);
const StyleMapping = new Map([["page-break-after", "breakAfter"], ["page-break-before", "breakBefore"], ["page-break-inside", "breakInside"], ["kerning-mode", value => value === "none" ? "none" : "normal"], ["xfa-font-horizontal-scale", value => `scaleX(${Math.max(0, parseInt(value, 10) / 100).toFixed(2)})`], ["xfa-font-vertical-scale", value => `scaleY(${Math.max(0, parseInt(value, 10) / 100).toFixed(2)})`], ["xfa-spacerun", ""], ["xfa-tab-stops", ""], ["font-size", (value, original) => {
  value = original.fontSize = Math.abs((0,_utils_js__WEBPACK_IMPORTED_MODULE_3__.getMeasurement)(value));
  return (0,_html_utils_js__WEBPACK_IMPORTED_MODULE_2__.measureToString)(0.99 * value);
}], ["letter-spacing", value => (0,_html_utils_js__WEBPACK_IMPORTED_MODULE_2__.measureToString)((0,_utils_js__WEBPACK_IMPORTED_MODULE_3__.getMeasurement)(value))], ["line-height", value => (0,_html_utils_js__WEBPACK_IMPORTED_MODULE_2__.measureToString)((0,_utils_js__WEBPACK_IMPORTED_MODULE_3__.getMeasurement)(value))], ["margin", value => (0,_html_utils_js__WEBPACK_IMPORTED_MODULE_2__.measureToString)((0,_utils_js__WEBPACK_IMPORTED_MODULE_3__.getMeasurement)(value))], ["margin-bottom", value => (0,_html_utils_js__WEBPACK_IMPORTED_MODULE_2__.measureToString)((0,_utils_js__WEBPACK_IMPORTED_MODULE_3__.getMeasurement)(value))], ["margin-left", value => (0,_html_utils_js__WEBPACK_IMPORTED_MODULE_2__.measureToString)((0,_utils_js__WEBPACK_IMPORTED_MODULE_3__.getMeasurement)(value))], ["margin-right", value => (0,_html_utils_js__WEBPACK_IMPORTED_MODULE_2__.measureToString)((0,_utils_js__WEBPACK_IMPORTED_MODULE_3__.getMeasurement)(value))], ["margin-top", value => (0,_html_utils_js__WEBPACK_IMPORTED_MODULE_2__.measureToString)((0,_utils_js__WEBPACK_IMPORTED_MODULE_3__.getMeasurement)(value))], ["text-indent", value => (0,_html_utils_js__WEBPACK_IMPORTED_MODULE_2__.measureToString)((0,_utils_js__WEBPACK_IMPORTED_MODULE_3__.getMeasurement)(value))], ["font-family", value => value], ["vertical-align", value => (0,_html_utils_js__WEBPACK_IMPORTED_MODULE_2__.measureToString)((0,_utils_js__WEBPACK_IMPORTED_MODULE_3__.getMeasurement)(value))]]);
const spacesRegExp = /\s+/g;
const crlfRegExp = /[\r\n]+/g;
const crlfForRichTextRegExp = /\r\n?/g;
function mapStyle(styleStr, node, richText) {
  const style = Object.create(null);
  if (!styleStr) {
    return style;
  }
  const original = Object.create(null);
  for (const [key, value] of styleStr.split(";").map(s => s.split(":", 2))) {
    const mapping = StyleMapping.get(key);
    if (mapping === "") {
      continue;
    }
    let newValue = value;
    if (mapping) {
      newValue = typeof mapping === "string" ? mapping : mapping(value, original);
    }
    if (key.endsWith("scale")) {
      style.transform = style.transform ? `${style[key]} ${newValue}` : newValue;
    } else {
      style[key.replaceAll(/-([a-z])/gi, (_, x) => x.toUpperCase())] = newValue;
    }
  }
  if (style.fontFamily) {
    (0,_html_utils_js__WEBPACK_IMPORTED_MODULE_2__.setFontFamily)({
      typeface: style.fontFamily,
      weight: style.fontWeight || "normal",
      posture: style.fontStyle || "normal",
      size: original.fontSize || 0
    }, node, node[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$globalData].fontFinder, style);
  }
  if (richText && style.verticalAlign && style.verticalAlign !== "0px" && style.fontSize) {
    const SUB_SUPER_SCRIPT_FACTOR = 0.583;
    const VERTICAL_FACTOR = 0.333;
    const fontSize = (0,_utils_js__WEBPACK_IMPORTED_MODULE_3__.getMeasurement)(style.fontSize);
    style.fontSize = (0,_html_utils_js__WEBPACK_IMPORTED_MODULE_2__.measureToString)(fontSize * SUB_SUPER_SCRIPT_FACTOR);
    style.verticalAlign = (0,_html_utils_js__WEBPACK_IMPORTED_MODULE_2__.measureToString)(Math.sign((0,_utils_js__WEBPACK_IMPORTED_MODULE_3__.getMeasurement)(style.verticalAlign)) * fontSize * VERTICAL_FACTOR);
  }
  if (richText && style.fontSize) {
    style.fontSize = `calc(${style.fontSize} * var(--total-scale-factor))`;
  }
  ;(0,_html_utils_js__WEBPACK_IMPORTED_MODULE_2__.fixTextIndent)(style);
  return style;
}
function checkStyle(node) {
  if (!node.style) {
    return "";
  }
  return node.style.split(";").filter(s => !!s.trim()).map(s => s.split(":", 2).map(t => t.trim())).filter(([key, value]) => {
    if (key === "font-family") {
      node[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$globalData].usedTypefaces.add(value);
    }
    return VALID_STYLES.has(key);
  }).map(kv => kv.join(":")).join(";");
}
const NoWhites = new Set(["body", "html"]);
class XhtmlObject extends _xfa_object_js__WEBPACK_IMPORTED_MODULE_4__.XmlObject {
  constructor(attributes, name) {
    super(XHTML_NS_ID, name);
    this[$richText] = false;
    this.style = attributes.style || "";
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$clean](builder) {
    super[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$clean](builder);
    this.style = checkStyle(this);
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$acceptWhitespace]() {
    return !NoWhites.has(this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$nodeName]);
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$onText](str, richText = false) {
    if (!richText) {
      str = str.replaceAll(crlfRegExp, "");
      if (!this.style.includes("xfa-spacerun:yes")) {
        str = str.replaceAll(spacesRegExp, " ");
      }
    } else {
      this[$richText] = true;
    }
    if (str) {
      this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$content] += str;
    }
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$pushGlyphs](measure, mustPop = true, xfaFont = Object.create(null)) {
    const margin = {
      top: NaN,
      bottom: NaN,
      left: NaN,
      right: NaN
    };
    let lineHeight = null;
    for (const [key, value] of this.style.split(";").map(s => s.split(":", 2))) {
      switch (key) {
        case "font-family":
          xfaFont.typeface = (0,_utils_js__WEBPACK_IMPORTED_MODULE_3__.stripQuotes)(value);
          break;
        case "font-size":
          xfaFont.size = (0,_utils_js__WEBPACK_IMPORTED_MODULE_3__.getMeasurement)(value);
          break;
        case "font-weight":
          xfaFont.weight = value;
          break;
        case "font-style":
          xfaFont.posture = value;
          break;
        case "letter-spacing":
          xfaFont.letterSpacing = (0,_utils_js__WEBPACK_IMPORTED_MODULE_3__.getMeasurement)(value);
          break;
        case "margin":
          const values = value.split(/ \t/).map(_utils_js__WEBPACK_IMPORTED_MODULE_3__.getMeasurement);
          switch (values.length) {
            case 1:
              margin.top = margin.bottom = margin.left = margin.right = values[0];
              break;
            case 2:
              margin.top = margin.bottom = values[0];
              margin.left = margin.right = values[1];
              break;
            case 3:
              margin.top = values[0];
              margin.bottom = values[2];
              margin.left = margin.right = values[1];
              break;
            case 4:
              margin.top = values[0];
              margin.left = values[1];
              margin.bottom = values[2];
              margin.right = values[3];
              break;
          }
          break;
        case "margin-top":
          margin.top = (0,_utils_js__WEBPACK_IMPORTED_MODULE_3__.getMeasurement)(value);
          break;
        case "margin-bottom":
          margin.bottom = (0,_utils_js__WEBPACK_IMPORTED_MODULE_3__.getMeasurement)(value);
          break;
        case "margin-left":
          margin.left = (0,_utils_js__WEBPACK_IMPORTED_MODULE_3__.getMeasurement)(value);
          break;
        case "margin-right":
          margin.right = (0,_utils_js__WEBPACK_IMPORTED_MODULE_3__.getMeasurement)(value);
          break;
        case "line-height":
          lineHeight = (0,_utils_js__WEBPACK_IMPORTED_MODULE_3__.getMeasurement)(value);
          break;
      }
    }
    measure.pushData(xfaFont, margin, lineHeight);
    if (this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$content]) {
      measure.addString(this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$content]);
    } else {
      for (const child of this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$getChildren]()) {
        if (child[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$nodeName] === "#text") {
          measure.addString(child[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$content]);
          continue;
        }
        child[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$pushGlyphs](measure);
      }
    }
    if (mustPop) {
      measure.popFont();
    }
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$toHTML](availableSpace) {
    const children = [];
    this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$extra] = {
      children
    };
    this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$childrenToHTML]({});
    if (children.length === 0 && !this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$content]) {
      return _utils_js__WEBPACK_IMPORTED_MODULE_3__.HTMLResult.EMPTY;
    }
    let value;
    if (this[$richText]) {
      value = this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$content] ? this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$content].replaceAll(crlfForRichTextRegExp, "\n") : undefined;
    } else {
      value = this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$content] || undefined;
    }
    return _utils_js__WEBPACK_IMPORTED_MODULE_3__.HTMLResult.success({
      name: this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$nodeName],
      attributes: {
        href: this.href,
        style: mapStyle(this.style, this, this[$richText])
      },
      children,
      value
    });
  }
}
class A extends XhtmlObject {
  constructor(attributes) {
    super(attributes, "a");
    this.href = (0,_html_utils_js__WEBPACK_IMPORTED_MODULE_2__.fixURL)(attributes.href) || "";
  }
}
class B extends XhtmlObject {
  constructor(attributes) {
    super(attributes, "b");
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$pushGlyphs](measure) {
    super[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$pushGlyphs](measure, true, {
      weight: "bold"
    });
  }
}
class Body extends XhtmlObject {
  constructor(attributes) {
    super(attributes, "body");
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$toHTML](availableSpace) {
    const res = super[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$toHTML](availableSpace);
    const {
      html
    } = res;
    if (!html) {
      return _utils_js__WEBPACK_IMPORTED_MODULE_3__.HTMLResult.EMPTY;
    }
    html.name = "div";
    html.attributes.class = ["xfaRich"];
    return res;
  }
}
class Br extends XhtmlObject {
  constructor(attributes) {
    super(attributes, "br");
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$text]() {
    return "\n";
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$pushGlyphs](measure) {
    measure.addString("\n");
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$toHTML](availableSpace) {
    return _utils_js__WEBPACK_IMPORTED_MODULE_3__.HTMLResult.success({
      name: "br"
    });
  }
}
class Html extends XhtmlObject {
  constructor(attributes) {
    super(attributes, "html");
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$toHTML](availableSpace) {
    const children = [];
    this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$extra] = {
      children
    };
    this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$childrenToHTML]({});
    if (children.length === 0) {
      return _utils_js__WEBPACK_IMPORTED_MODULE_3__.HTMLResult.success({
        name: "div",
        attributes: {
          class: ["xfaRich"],
          style: {}
        },
        value: this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$content] || ""
      });
    }
    if (children.length === 1) {
      const child = children[0];
      if (child.attributes?.class.includes("xfaRich")) {
        return _utils_js__WEBPACK_IMPORTED_MODULE_3__.HTMLResult.success(child);
      }
    }
    return _utils_js__WEBPACK_IMPORTED_MODULE_3__.HTMLResult.success({
      name: "div",
      attributes: {
        class: ["xfaRich"],
        style: {}
      },
      children
    });
  }
}
class I extends XhtmlObject {
  constructor(attributes) {
    super(attributes, "i");
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$pushGlyphs](measure) {
    super[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$pushGlyphs](measure, true, {
      posture: "italic"
    });
  }
}
class Li extends XhtmlObject {
  constructor(attributes) {
    super(attributes, "li");
  }
}
class Ol extends XhtmlObject {
  constructor(attributes) {
    super(attributes, "ol");
  }
}
class P extends XhtmlObject {
  constructor(attributes) {
    super(attributes, "p");
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$pushGlyphs](measure) {
    super[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$pushGlyphs](measure, false);
    measure.addString("\n");
    measure.addPara();
    measure.popFont();
  }
  [_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$text]() {
    const siblings = this[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$getParent]()[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$getChildren]();
    return siblings.at(-1) === this ? super[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$text]() : super[_symbol_utils_js__WEBPACK_IMPORTED_MODULE_0__.$text]() + "\n";
  }
}
class Span extends XhtmlObject {
  constructor(attributes) {
    super(attributes, "span");
  }
}
class Sub extends XhtmlObject {
  constructor(attributes) {
    super(attributes, "sub");
  }
}
class Sup extends XhtmlObject {
  constructor(attributes) {
    super(attributes, "sup");
  }
}
class Ul extends XhtmlObject {
  constructor(attributes) {
    super(attributes, "ul");
  }
}
class XhtmlNamespace {
  static [_namespaces_js__WEBPACK_IMPORTED_MODULE_1__.$buildXFAObject](name, attributes) {
    return Object.hasOwn(XhtmlNamespace, name) ? XhtmlNamespace[name](attributes) : undefined;
  }
  static a(attributes) {
    return new A(attributes);
  }
  static b(attributes) {
    return new B(attributes);
  }
  static body(attributes) {
    return new Body(attributes);
  }
  static br(attributes) {
    return new Br(attributes);
  }
  static html(attributes) {
    return new Html(attributes);
  }
  static i(attributes) {
    return new I(attributes);
  }
  static li(attributes) {
    return new Li(attributes);
  }
  static ol(attributes) {
    return new Ol(attributes);
  }
  static p(attributes) {
    return new P(attributes);
  }
  static span(attributes) {
    return new Span(attributes);
  }
  static sub(attributes) {
    return new Sub(attributes);
  }
  static sup(attributes) {
    return new Sup(attributes);
  }
  static ul(attributes) {
    return new Ul(attributes);
  }
}


/***/ }

};
