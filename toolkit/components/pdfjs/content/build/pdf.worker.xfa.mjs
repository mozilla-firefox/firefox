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
 * pdfjsVersion = 7.0.213
 * pdfjsBuild = 541d197e6
 */
export const __webpack_esm_ids__ = ["xfa"];
export const __webpack_esm_modules__ = {

/***/ 12
(__unused_webpack___webpack_module__, __webpack_exports__, __webpack_require__) {


// EXPORTS
__webpack_require__.d(__webpack_exports__, {
  XFAFactory: () => (/* binding */ XFAFactory),
  getXfaFontDict: () => (/* reexport */ getXfaFontDict),
  getXfaFontName: () => (/* reexport */ getXfaFontName)
});

// EXTERNAL MODULE: ./src/core/xfa/symbol_utils.js
var symbol_utils = __webpack_require__(1318);
// EXTERNAL MODULE: ./src/core/xfa/namespaces.js
var namespaces = __webpack_require__(7088);
// EXTERNAL MODULE: ./src/shared/math_clamp.js
var math_clamp = /*#__PURE__*/__webpack_require__(4566);
// EXTERNAL MODULE: ./src/core/xfa/html_utils.js + 1 modules
var html_utils = __webpack_require__(1037);
;// ./src/core/xfa/layout.js



function createLine(node, children) {
  return {
    name: "div",
    attributes: {
      class: [node.layout === "lr-tb" ? "xfaLr" : "xfaRl"]
    },
    children
  };
}
function flushHTML(node) {
  if (!node[symbol_utils.$extra]) {
    return null;
  }
  const attributes = node[symbol_utils.$extra].attributes;
  const html = {
    name: "div",
    attributes,
    children: node[symbol_utils.$extra].children
  };
  if (node[symbol_utils.$extra].failingNode) {
    const htmlFromFailing = node[symbol_utils.$extra].failingNode[symbol_utils.$flushHTML]();
    if (htmlFromFailing) {
      if (node.layout.endsWith("-tb")) {
        html.children.push(createLine(node, [htmlFromFailing]));
      } else {
        html.children.push(htmlFromFailing);
      }
    }
  }
  return html.children.length === 0 ? null : html;
}
function addHTML(node, html, bbox) {
  const extra = node[symbol_utils.$extra];
  const availableSpace = extra.availableSpace;
  const [x, y, w, h] = bbox;
  switch (node.layout) {
    case "position":
      {
        extra.width = Math.max(extra.width, x + w);
        extra.height = Math.max(extra.height, y + h);
        extra.children.push(html);
        break;
      }
    case "lr-tb":
    case "rl-tb":
      if (!extra.line || extra.attempt === 1) {
        extra.line = createLine(node, []);
        extra.children.push(extra.line);
        extra.numberInLine = 0;
      }
      extra.numberInLine += 1;
      extra.line.children.push(html);
      if (extra.attempt === 0) {
        extra.currentWidth += w;
        extra.height = Math.max(extra.height, extra.prevHeight + h);
      } else {
        extra.currentWidth = w;
        extra.prevHeight = extra.height;
        extra.height += h;
        extra.attempt = 0;
      }
      extra.width = Math.max(extra.width, extra.currentWidth);
      break;
    case "rl-row":
    case "row":
      {
        extra.children.push(html);
        extra.width += w;
        extra.height = Math.max(extra.height, h);
        const height = (0,html_utils.measureToString)(extra.height);
        for (const child of extra.children) {
          child.attributes.style.height = height;
        }
        break;
      }
    case "table":
      {
        extra.width = (0,math_clamp.MathClamp)(w, extra.width, availableSpace.width);
        extra.height += h;
        extra.children.push(html);
        break;
      }
    case "tb":
      {
        extra.width = (0,math_clamp.MathClamp)(w, extra.width, availableSpace.width);
        extra.height += h;
        extra.children.push(html);
        break;
      }
  }
}
function getAvailableSpace(node) {
  const availableSpace = node[symbol_utils.$extra].availableSpace;
  const marginV = node.margin ? node.margin.topInset + node.margin.bottomInset : 0;
  const marginH = node.margin ? node.margin.leftInset + node.margin.rightInset : 0;
  switch (node.layout) {
    case "lr-tb":
    case "rl-tb":
      if (node[symbol_utils.$extra].attempt === 0) {
        return {
          width: availableSpace.width - marginH - node[symbol_utils.$extra].currentWidth,
          height: availableSpace.height - marginV - node[symbol_utils.$extra].prevHeight
        };
      }
      return {
        width: availableSpace.width - marginH,
        height: availableSpace.height - marginV - node[symbol_utils.$extra].height
      };
    case "rl-row":
    case "row":
      const width = Math.sumPrecise(node[symbol_utils.$extra].columnWidths.slice(node[symbol_utils.$extra].currentColumn));
      return {
        width,
        height: availableSpace.height - marginH
      };
    case "table":
    case "tb":
      return {
        width: availableSpace.width - marginH,
        height: availableSpace.height - marginV - node[symbol_utils.$extra].height
      };
    case "position":
    default:
      return availableSpace;
  }
}
function getTransformedBBox(node) {
  let w = node.w === "" ? NaN : node.w;
  let h = node.h === "" ? NaN : node.h;
  let [centerX, centerY] = [0, 0];
  switch (node.anchorType || "") {
    case "bottomCenter":
      [centerX, centerY] = [w / 2, h];
      break;
    case "bottomLeft":
      [centerX, centerY] = [0, h];
      break;
    case "bottomRight":
      [centerX, centerY] = [w, h];
      break;
    case "middleCenter":
      [centerX, centerY] = [w / 2, h / 2];
      break;
    case "middleLeft":
      [centerX, centerY] = [0, h / 2];
      break;
    case "middleRight":
      [centerX, centerY] = [w, h / 2];
      break;
    case "topCenter":
      [centerX, centerY] = [w / 2, 0];
      break;
    case "topRight":
      [centerX, centerY] = [w, 0];
      break;
  }
  let x, y;
  switch (node.rotate || 0) {
    case 0:
      [x, y] = [-centerX, -centerY];
      break;
    case 90:
      [x, y] = [-centerY, centerX];
      [w, h] = [h, -w];
      break;
    case 180:
      [x, y] = [centerX, centerY];
      [w, h] = [-w, -h];
      break;
    case 270:
      [x, y] = [centerY, -centerX];
      [w, h] = [-h, w];
      break;
  }
  return [node.x + x + Math.min(0, w), node.y + y + Math.min(0, h), Math.abs(w), Math.abs(h)];
}
function checkDimensions(node, space) {
  if (node[symbol_utils.$getTemplateRoot]()[symbol_utils.$extra].firstUnsplittable === null || node.w === 0 || node.h === 0) {
    return true;
  }
  const ERROR = 2;
  const parent = node[symbol_utils.$getSubformParent]();
  const attempt = parent[symbol_utils.$extra]?.attempt || 0;
  const [, y, w, h] = getTransformedBBox(node);
  switch (parent.layout) {
    case "lr-tb":
    case "rl-tb":
      if (attempt === 0) {
        if (!node[symbol_utils.$getTemplateRoot]()[symbol_utils.$extra].noLayoutFailure) {
          if (node.h !== "" && Math.round(h - space.height) > ERROR) {
            return false;
          }
          if (node.w !== "") {
            if (Math.round(w - space.width) <= ERROR) {
              return true;
            }
            if (parent[symbol_utils.$extra].numberInLine === 0) {
              return space.height > ERROR;
            }
            return false;
          }
          return space.width > ERROR;
        }
        if (node.w !== "") {
          return Math.round(w - space.width) <= ERROR;
        }
        return space.width > ERROR;
      }
      if (node[symbol_utils.$getTemplateRoot]()[symbol_utils.$extra].noLayoutFailure) {
        return true;
      }
      if (node.h !== "" && Math.round(h - space.height) > ERROR) {
        return false;
      }
      if (node.w === "" || Math.round(w - space.width) <= ERROR) {
        return space.height > ERROR;
      }
      if (parent[symbol_utils.$isThereMoreWidth]()) {
        return false;
      }
      return space.height > ERROR;
    case "table":
    case "tb":
      if (node[symbol_utils.$getTemplateRoot]()[symbol_utils.$extra].noLayoutFailure) {
        return true;
      }
      if (node.h !== "" && !node[symbol_utils.$isSplittable]()) {
        return Math.round(h - space.height) <= ERROR;
      }
      if (node.w === "" || Math.round(w - space.width) <= ERROR) {
        return space.height > ERROR;
      }
      if (parent[symbol_utils.$isThereMoreWidth]()) {
        return false;
      }
      return space.height > ERROR;
    case "position":
      if (node[symbol_utils.$getTemplateRoot]()[symbol_utils.$extra].noLayoutFailure || node.h === "" || Math.round(h + y - space.height) <= ERROR) {
        return true;
      }
      const area = node[symbol_utils.$getTemplateRoot]()[symbol_utils.$extra].currentContentArea;
      return h + y > area.h;
    case "rl-row":
    case "row":
      if (node[symbol_utils.$getTemplateRoot]()[symbol_utils.$extra].noLayoutFailure) {
        return true;
      }
      if (node.h !== "") {
        return Math.round(h - space.height) <= ERROR;
      }
      return true;
    default:
      return true;
  }
}

// EXTERNAL MODULE: ./src/core/xfa/xfa_object.js
var xfa_object = __webpack_require__(7485);
// EXTERNAL MODULE: ./src/core/xfa/utils.js
var utils = __webpack_require__(8063);
// EXTERNAL MODULE: ./src/shared/util.js
var util = __webpack_require__(4292);
// EXTERNAL MODULE: ./src/core/xfa/fonts.js
var xfa_fonts = __webpack_require__(4028);
// EXTERNAL MODULE: ./src/core/core_utils.js
var core_utils = __webpack_require__(5399);
// EXTERNAL MODULE: ./src/core/xfa/som.js
var som = __webpack_require__(1723);
;// ./src/core/xfa/template.js










const TEMPLATE_NS_ID = namespaces.NamespaceIds.template.id;
const MAX_ATTEMPTS_FOR_LRTB_LAYOUT = 2;
const MAX_EMPTY_PAGES = 3;
const DEFAULT_TAB_INDEX = 5000;
const HEADING_PATTERN = /^H(\d+)$/;
const MIMES = new Set(["image/gif", "image/jpeg", "image/jpg", "image/pjpeg", "image/png", "image/apng", "image/x-png", "image/bmp", "image/x-ms-bmp", "image/tiff", "image/tif", "application/octet-stream"]);
const IMAGES_HEADERS = [[[0x42, 0x4d], "image/bmp"], [[0xff, 0xd8, 0xff], "image/jpeg"], [[0x49, 0x49, 0x2a, 0x00], "image/tiff"], [[0x4d, 0x4d, 0x00, 0x2a], "image/tiff"], [[0x47, 0x49, 0x46, 0x38, 0x39, 0x61], "image/gif"], [[0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a], "image/png"]];
function getBorderDims(node) {
  if (!node || !node.border) {
    return {
      w: 0,
      h: 0
    };
  }
  const borderExtra = node.border[symbol_utils.$getExtra]();
  if (!borderExtra) {
    return {
      w: 0,
      h: 0
    };
  }
  return {
    w: borderExtra.widths[0] + borderExtra.widths[2] + borderExtra.insets[0] + borderExtra.insets[2],
    h: borderExtra.widths[1] + borderExtra.widths[3] + borderExtra.insets[1] + borderExtra.insets[3]
  };
}
function hasMargin(node) {
  return node.margin && (node.margin.topInset || node.margin.rightInset || node.margin.bottomInset || node.margin.leftInset);
}
function _setValue(templateNode, value) {
  if (!templateNode.value) {
    const nodeValue = new Value({});
    templateNode[symbol_utils.$appendChild](nodeValue);
    templateNode.value = nodeValue;
  }
  templateNode.value[symbol_utils.$setValue](value);
}
function* getContainedChildren(node) {
  for (const child of node[symbol_utils.$getChildren]()) {
    if (child instanceof SubformSet) {
      yield* child[symbol_utils.$getContainedChildren]();
      continue;
    }
    yield child;
  }
}
function isRequired(node) {
  return node.validate?.nullTest === "error";
}
function setTabIndex(node) {
  while (node) {
    if (!node.traversal) {
      node[symbol_utils.$tabIndex] = node[symbol_utils.$getParent]()[symbol_utils.$tabIndex];
      return;
    }
    if (node[symbol_utils.$tabIndex]) {
      return;
    }
    let next = null;
    for (const child of node.traversal[symbol_utils.$getChildren]()) {
      if (child.operation === "next") {
        next = child;
        break;
      }
    }
    if (!next || !next.ref) {
      node[symbol_utils.$tabIndex] = node[symbol_utils.$getParent]()[symbol_utils.$tabIndex];
      return;
    }
    const root = node[symbol_utils.$getTemplateRoot]();
    node[symbol_utils.$tabIndex] = ++root[symbol_utils.$tabIndex];
    const ref = root[symbol_utils.$searchNode](next.ref, node);
    if (!ref) {
      return;
    }
    node = ref[0];
  }
}
function applyAssist(obj, attributes) {
  const assist = obj.assist;
  if (assist) {
    const assistTitle = assist[symbol_utils.$toHTML]();
    if (assistTitle) {
      attributes.title = assistTitle;
    }
    const role = assist.role;
    const match = role.match(HEADING_PATTERN);
    if (match) {
      const ariaRole = "heading";
      const ariaLevel = match[1];
      attributes.role = ariaRole;
      attributes["aria-level"] = ariaLevel;
    }
  }
  if (obj.layout === "table") {
    attributes.role = "table";
  } else if (obj.layout === "row") {
    attributes.role = "row";
  } else {
    const parent = obj[symbol_utils.$getParent]();
    if (parent.layout === "row") {
      attributes.role = parent.assist?.role === "TH" ? "columnheader" : "cell";
    }
  }
}
function ariaLabel(obj) {
  if (!obj.assist) {
    return null;
  }
  const assist = obj.assist;
  if (assist.speak && assist.speak[symbol_utils.$content] !== "") {
    return assist.speak[symbol_utils.$content];
  }
  if (assist.toolTip) {
    return assist.toolTip[symbol_utils.$content];
  }
  return null;
}
function valueToHtml(value) {
  return utils.HTMLResult.success({
    name: "div",
    attributes: {
      class: ["xfaRich"],
      style: Object.create(null)
    },
    children: [{
      name: "span",
      attributes: {
        style: Object.create(null)
      },
      value
    }]
  });
}
function setFirstUnsplittable(node) {
  const root = node[symbol_utils.$getTemplateRoot]();
  if (root[symbol_utils.$extra].firstUnsplittable === null) {
    root[symbol_utils.$extra].firstUnsplittable = node;
    root[symbol_utils.$extra].noLayoutFailure = true;
  }
}
function unsetFirstUnsplittable(node) {
  const root = node[symbol_utils.$getTemplateRoot]();
  if (root[symbol_utils.$extra].firstUnsplittable === node) {
    root[symbol_utils.$extra].noLayoutFailure = false;
  }
}
function handleBreak(node) {
  if (node[symbol_utils.$extra]) {
    return false;
  }
  node[symbol_utils.$extra] = Object.create(null);
  if (node.targetType === "auto") {
    return false;
  }
  const root = node[symbol_utils.$getTemplateRoot]();
  let target = null;
  if (node.target) {
    target = root[symbol_utils.$searchNode](node.target, node[symbol_utils.$getParent]());
    if (!target) {
      return false;
    }
    target = target[0];
  }
  const {
    currentPageArea,
    currentContentArea
  } = root[symbol_utils.$extra];
  if (node.targetType === "pageArea") {
    if (!(target instanceof PageArea)) {
      target = null;
    }
    if (node.startNew) {
      node[symbol_utils.$extra].target = target || currentPageArea;
      return true;
    } else if (target && target !== currentPageArea) {
      node[symbol_utils.$extra].target = target;
      return true;
    }
    return false;
  }
  if (!(target instanceof ContentArea)) {
    target = null;
  }
  const pageArea = target && target[symbol_utils.$getParent]();
  let index;
  let nextPageArea = pageArea;
  if (node.startNew) {
    if (target) {
      const contentAreas = pageArea.contentArea.children;
      const indexForCurrent = contentAreas.indexOf(currentContentArea);
      const indexForTarget = contentAreas.indexOf(target);
      if (indexForCurrent !== -1 && indexForCurrent < indexForTarget) {
        nextPageArea = null;
      }
      index = indexForTarget - 1;
    } else {
      index = currentPageArea.contentArea.children.indexOf(currentContentArea);
    }
  } else if (target && target !== currentContentArea) {
    const contentAreas = pageArea.contentArea.children;
    index = contentAreas.indexOf(target) - 1;
    nextPageArea = pageArea === currentPageArea ? null : pageArea;
  } else {
    return false;
  }
  node[symbol_utils.$extra].target = nextPageArea;
  node[symbol_utils.$extra].index = index;
  return true;
}
function handleOverflow(node, extraNode, space) {
  const root = node[symbol_utils.$getTemplateRoot]();
  const saved = root[symbol_utils.$extra].noLayoutFailure;
  const savedMethod = extraNode[symbol_utils.$getSubformParent];
  extraNode[symbol_utils.$getSubformParent] = () => node;
  root[symbol_utils.$extra].noLayoutFailure = true;
  const res = extraNode[symbol_utils.$toHTML](space);
  node[symbol_utils.$addHTML](res.html, res.bbox);
  root[symbol_utils.$extra].noLayoutFailure = saved;
  extraNode[symbol_utils.$getSubformParent] = savedMethod;
}
class AppearanceFilter extends xfa_object.StringObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "appearanceFilter");
    this.id = attributes.id || "";
    this.type = (0,utils.getStringOption)(attributes.type, ["optional", "required"]);
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
  }
}
class Arc extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "arc", true);
    this.circular = (0,utils.getInteger)({
      data: attributes.circular,
      defaultValue: 0,
      validate: x => x === 1
    });
    this.hand = (0,utils.getStringOption)(attributes.hand, ["even", "left", "right"]);
    this.id = attributes.id || "";
    this.startAngle = (0,utils.getFloat)({
      data: attributes.startAngle,
      defaultValue: 0,
      validate: x => true
    });
    this.sweepAngle = (0,utils.getFloat)({
      data: attributes.sweepAngle,
      defaultValue: 360,
      validate: x => true
    });
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.edge = null;
    this.fill = null;
  }
  [symbol_utils.$toHTML]() {
    const edge = this.edge || new Edge({});
    const edgeStyle = edge[symbol_utils.$toStyle]();
    const style = Object.create(null);
    if (this.fill?.presence === "visible") {
      Object.assign(style, this.fill[symbol_utils.$toStyle]());
    } else {
      style.fill = "transparent";
    }
    style.strokeWidth = (0,html_utils.measureToString)(edge.presence === "visible" ? edge.thickness : 0);
    style.stroke = edgeStyle.color;
    let arc;
    const attributes = {
      xmlns: util.SVG_NS,
      style: {
        width: "100%",
        height: "100%",
        overflow: "visible"
      }
    };
    if (this.sweepAngle === 360) {
      arc = {
        name: "ellipse",
        attributes: {
          xmlns: util.SVG_NS,
          cx: "50%",
          cy: "50%",
          rx: "50%",
          ry: "50%",
          style
        }
      };
    } else {
      const startAngle = this.startAngle * Math.PI / 180;
      const sweepAngle = this.sweepAngle * Math.PI / 180;
      const largeArc = this.sweepAngle > 180 ? 1 : 0;
      const [x1, y1, x2, y2] = [50 * (1 + Math.cos(startAngle)), 50 * (1 - Math.sin(startAngle)), 50 * (1 + Math.cos(startAngle + sweepAngle)), 50 * (1 - Math.sin(startAngle + sweepAngle))];
      arc = {
        name: "path",
        attributes: {
          xmlns: util.SVG_NS,
          d: `M ${x1} ${y1} A 50 50 0 ${largeArc} 0 ${x2} ${y2}`,
          vectorEffect: "non-scaling-stroke",
          style
        }
      };
      Object.assign(attributes, {
        viewBox: "0 0 100 100",
        preserveAspectRatio: "none"
      });
    }
    const svg = {
      name: "svg",
      children: [arc],
      attributes
    };
    const parent = this[symbol_utils.$getParent]()[symbol_utils.$getParent]();
    if (hasMargin(parent)) {
      return utils.HTMLResult.success({
        name: "div",
        attributes: {
          style: {
            display: "inline",
            width: "100%",
            height: "100%"
          }
        },
        children: [svg]
      });
    }
    svg.attributes.style.position = "absolute";
    return utils.HTMLResult.success(svg);
  }
}
class Area extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "area", true);
    this.colSpan = (0,utils.getInteger)({
      data: attributes.colSpan,
      defaultValue: 1,
      validate: n => n >= 1 || n === -1
    });
    this.id = attributes.id || "";
    this.name = attributes.name || "";
    this.relevant = (0,utils.getRelevant)(attributes.relevant);
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.x = (0,utils.getMeasurement)(attributes.x, "0pt");
    this.y = (0,utils.getMeasurement)(attributes.y, "0pt");
    this.desc = null;
    this.extras = null;
    this.area = new xfa_object.XFAObjectArray();
    this.draw = new xfa_object.XFAObjectArray();
    this.exObject = new xfa_object.XFAObjectArray();
    this.exclGroup = new xfa_object.XFAObjectArray();
    this.field = new xfa_object.XFAObjectArray();
    this.subform = new xfa_object.XFAObjectArray();
    this.subformSet = new xfa_object.XFAObjectArray();
  }
  *[symbol_utils.$getContainedChildren]() {
    yield* getContainedChildren(this);
  }
  [symbol_utils.$isTransparent]() {
    return true;
  }
  [symbol_utils.$isBindable]() {
    return true;
  }
  [symbol_utils.$addHTML](html, bbox) {
    const [x, y, w, h] = bbox;
    this[symbol_utils.$extra].width = Math.max(this[symbol_utils.$extra].width, x + w);
    this[symbol_utils.$extra].height = Math.max(this[symbol_utils.$extra].height, y + h);
    this[symbol_utils.$extra].children.push(html);
  }
  [symbol_utils.$getAvailableSpace]() {
    return this[symbol_utils.$extra].availableSpace;
  }
  [symbol_utils.$toHTML](availableSpace) {
    const style = (0,html_utils.toStyle)(this, "position");
    const attributes = {
      style,
      id: this[symbol_utils.$uid],
      class: ["xfaArea"]
    };
    if ((0,html_utils.isPrintOnly)(this)) {
      attributes.class.push("xfaPrintOnly");
    }
    if (this.name) {
      attributes.xfaName = this.name;
    }
    const children = [];
    this[symbol_utils.$extra] = {
      children,
      width: 0,
      height: 0,
      availableSpace
    };
    const result = this[symbol_utils.$childrenToHTML]({
      filter: new Set(["area", "draw", "field", "exclGroup", "subform", "subformSet"]),
      include: true
    });
    if (!result.success) {
      if (result.isBreak()) {
        return result;
      }
      delete this[symbol_utils.$extra];
      return utils.HTMLResult.FAILURE;
    }
    style.width = (0,html_utils.measureToString)(this[symbol_utils.$extra].width);
    style.height = (0,html_utils.measureToString)(this[symbol_utils.$extra].height);
    const html = {
      name: "div",
      attributes,
      children
    };
    const bbox = [this.x, this.y, this[symbol_utils.$extra].width, this[symbol_utils.$extra].height];
    delete this[symbol_utils.$extra];
    return utils.HTMLResult.success(html, bbox);
  }
}
class Assist extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "assist", true);
    this.id = attributes.id || "";
    this.role = attributes.role || "";
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.speak = null;
    this.toolTip = null;
  }
  [symbol_utils.$toHTML]() {
    return this.toolTip?.[symbol_utils.$content] || null;
  }
}
class Barcode extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "barcode", true);
    this.charEncoding = (0,utils.getKeyword)({
      data: attributes.charEncoding ? attributes.charEncoding.toLowerCase() : "",
      defaultValue: "",
      validate: k => ["utf-8", "big-five", "fontspecific", "gbk", "gb-18030", "gb-2312", "ksc-5601", "none", "shift-jis", "ucs-2", "utf-16"].includes(k) || k.match(/iso-8859-\d{2}/)
    });
    this.checksum = (0,utils.getStringOption)(attributes.checksum, ["none", "1mod10", "1mod10_1mod11", "2mod10", "auto"]);
    this.dataColumnCount = (0,utils.getInteger)({
      data: attributes.dataColumnCount,
      defaultValue: -1,
      validate: x => x >= 0
    });
    this.dataLength = (0,utils.getInteger)({
      data: attributes.dataLength,
      defaultValue: -1,
      validate: x => x >= 0
    });
    this.dataPrep = (0,utils.getStringOption)(attributes.dataPrep, ["none", "flateCompress"]);
    this.dataRowCount = (0,utils.getInteger)({
      data: attributes.dataRowCount,
      defaultValue: -1,
      validate: x => x >= 0
    });
    this.endChar = attributes.endChar || "";
    this.errorCorrectionLevel = (0,utils.getInteger)({
      data: attributes.errorCorrectionLevel,
      defaultValue: -1,
      validate: x => x >= 0 && x <= 8
    });
    this.id = attributes.id || "";
    this.moduleHeight = (0,utils.getMeasurement)(attributes.moduleHeight, "5mm");
    this.moduleWidth = (0,utils.getMeasurement)(attributes.moduleWidth, "0.25mm");
    this.printCheckDigit = (0,utils.getInteger)({
      data: attributes.printCheckDigit,
      defaultValue: 0,
      validate: x => x === 1
    });
    this.rowColumnRatio = (0,utils.getRatio)(attributes.rowColumnRatio);
    this.startChar = attributes.startChar || "";
    this.textLocation = (0,utils.getStringOption)(attributes.textLocation, ["below", "above", "aboveEmbedded", "belowEmbedded", "none"]);
    this.truncate = (0,utils.getInteger)({
      data: attributes.truncate,
      defaultValue: 0,
      validate: x => x === 1
    });
    this.type = (0,utils.getStringOption)(attributes.type ? attributes.type.toLowerCase() : "", ["aztec", "codabar", "code2of5industrial", "code2of5interleaved", "code2of5matrix", "code2of5standard", "code3of9", "code3of9extended", "code11", "code49", "code93", "code128", "code128a", "code128b", "code128c", "code128sscc", "datamatrix", "ean8", "ean8add2", "ean8add5", "ean13", "ean13add2", "ean13add5", "ean13pwcd", "fim", "logmars", "maxicode", "msi", "pdf417", "pdf417macro", "plessey", "postauscust2", "postauscust3", "postausreplypaid", "postausstandard", "postukrm4scc", "postusdpbc", "postusimb", "postusstandard", "postus5zip", "qrcode", "rfid", "rss14", "rss14expanded", "rss14limited", "rss14stacked", "rss14stackedomni", "rss14truncated", "telepen", "ucc128", "ucc128random", "ucc128sscc", "upca", "upcaadd2", "upcaadd5", "upcapwcd", "upce", "upceadd2", "upceadd5", "upcean2", "upcean5", "upsmaxicode"]);
    this.upsMode = (0,utils.getStringOption)(attributes.upsMode, ["usCarrier", "internationalCarrier", "secureSymbol", "standardSymbol"]);
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.wideNarrowRatio = (0,utils.getRatio)(attributes.wideNarrowRatio);
    this.encrypt = null;
    this.extras = null;
  }
}
class Bind extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "bind", true);
    this.match = (0,utils.getStringOption)(attributes.match, ["once", "dataRef", "global", "none"]);
    this.ref = attributes.ref || "";
    this.picture = null;
  }
}
class BindItems extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "bindItems");
    this.connection = attributes.connection || "";
    this.labelRef = attributes.labelRef || "";
    this.ref = attributes.ref || "";
    this.valueRef = attributes.valueRef || "";
  }
}
class Bookend extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "bookend");
    this.id = attributes.id || "";
    this.leader = attributes.leader || "";
    this.trailer = attributes.trailer || "";
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
  }
}
class BooleanElement extends xfa_object.Option01 {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "boolean");
    this.id = attributes.id || "";
    this.name = attributes.name || "";
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
  }
  [symbol_utils.$toHTML](availableSpace) {
    return valueToHtml(this[symbol_utils.$content] === 1 ? "1" : "0");
  }
}
class Border extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "border", true);
    this.break = (0,utils.getStringOption)(attributes.break, ["close", "open"]);
    this.hand = (0,utils.getStringOption)(attributes.hand, ["even", "left", "right"]);
    this.id = attributes.id || "";
    this.presence = (0,utils.getStringOption)(attributes.presence, ["visible", "hidden", "inactive", "invisible"]);
    this.relevant = (0,utils.getRelevant)(attributes.relevant);
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.corner = new xfa_object.XFAObjectArray(4);
    this.edge = new xfa_object.XFAObjectArray(4);
    this.extras = null;
    this.fill = null;
    this.margin = null;
  }
  [symbol_utils.$getExtra]() {
    if (!this[symbol_utils.$extra]) {
      const edges = this.edge.children.slice();
      if (edges.length < 4) {
        const defaultEdge = edges.at(-1) || new Edge({});
        for (let i = edges.length; i < 4; i++) {
          edges.push(defaultEdge);
        }
      }
      const widths = edges.map(edge => edge.thickness);
      const insets = [0, 0, 0, 0];
      if (this.margin) {
        insets[0] = this.margin.topInset;
        insets[1] = this.margin.rightInset;
        insets[2] = this.margin.bottomInset;
        insets[3] = this.margin.leftInset;
      }
      this[symbol_utils.$extra] = {
        widths,
        insets,
        edges
      };
    }
    return this[symbol_utils.$extra];
  }
  [symbol_utils.$toStyle]() {
    const {
      edges
    } = this[symbol_utils.$getExtra]();
    const edgeStyles = edges.map(node => {
      const style = node[symbol_utils.$toStyle]();
      style.color ||= "#000000";
      return style;
    });
    const style = Object.create(null);
    if (this.margin) {
      Object.assign(style, this.margin[symbol_utils.$toStyle]());
    }
    if (this.fill?.presence === "visible") {
      Object.assign(style, this.fill[symbol_utils.$toStyle]());
    }
    if (this.corner.children.some(node => node.radius !== 0)) {
      const cornerStyles = this.corner.children.map(node => node[symbol_utils.$toStyle]());
      if (cornerStyles.length === 2 || cornerStyles.length === 3) {
        const last = cornerStyles.at(-1);
        for (let i = cornerStyles.length; i < 4; i++) {
          cornerStyles.push(last);
        }
      }
      style.borderRadius = cornerStyles.map(s => s.radius).join(" ");
    }
    switch (this.presence) {
      case "invisible":
      case "hidden":
        style.borderStyle = "";
        break;
      case "inactive":
        style.borderStyle = "none";
        break;
      default:
        style.borderStyle = edgeStyles.map(s => s.style).join(" ");
        break;
    }
    style.borderWidth = edgeStyles.map(s => s.width).join(" ");
    style.borderColor = edgeStyles.map(s => s.color).join(" ");
    return style;
  }
}
class Break extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "break", true);
    this.after = (0,utils.getStringOption)(attributes.after, ["auto", "contentArea", "pageArea", "pageEven", "pageOdd"]);
    this.afterTarget = attributes.afterTarget || "";
    this.before = (0,utils.getStringOption)(attributes.before, ["auto", "contentArea", "pageArea", "pageEven", "pageOdd"]);
    this.beforeTarget = attributes.beforeTarget || "";
    this.bookendLeader = attributes.bookendLeader || "";
    this.bookendTrailer = attributes.bookendTrailer || "";
    this.id = attributes.id || "";
    this.overflowLeader = attributes.overflowLeader || "";
    this.overflowTarget = attributes.overflowTarget || "";
    this.overflowTrailer = attributes.overflowTrailer || "";
    this.startNew = (0,utils.getInteger)({
      data: attributes.startNew,
      defaultValue: 0,
      validate: x => x === 1
    });
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.extras = null;
  }
}
class BreakAfter extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "breakAfter", true);
    this.id = attributes.id || "";
    this.leader = attributes.leader || "";
    this.startNew = (0,utils.getInteger)({
      data: attributes.startNew,
      defaultValue: 0,
      validate: x => x === 1
    });
    this.target = attributes.target || "";
    this.targetType = (0,utils.getStringOption)(attributes.targetType, ["auto", "contentArea", "pageArea"]);
    this.trailer = attributes.trailer || "";
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.script = null;
  }
}
class BreakBefore extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "breakBefore", true);
    this.id = attributes.id || "";
    this.leader = attributes.leader || "";
    this.startNew = (0,utils.getInteger)({
      data: attributes.startNew,
      defaultValue: 0,
      validate: x => x === 1
    });
    this.target = attributes.target || "";
    this.targetType = (0,utils.getStringOption)(attributes.targetType, ["auto", "contentArea", "pageArea"]);
    this.trailer = attributes.trailer || "";
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.script = null;
  }
  [symbol_utils.$toHTML](availableSpace) {
    this[symbol_utils.$extra] = {};
    return utils.HTMLResult.FAILURE;
  }
}
class Button extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "button", true);
    this.highlight = (0,utils.getStringOption)(attributes.highlight, ["inverted", "none", "outline", "push"]);
    this.id = attributes.id || "";
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.extras = null;
  }
  [symbol_utils.$toHTML](availableSpace) {
    const parent = this[symbol_utils.$getParent]();
    const grandpa = parent[symbol_utils.$getParent]();
    const htmlButton = {
      name: "button",
      attributes: {
        id: this[symbol_utils.$uid],
        class: ["xfaButton"],
        style: {}
      },
      children: []
    };
    for (const event of grandpa.event.children) {
      if (event.activity !== "click" || !event.script) {
        continue;
      }
      const jsURL = (0,core_utils.recoverJsURL)(event.script[symbol_utils.$content]);
      if (!jsURL) {
        continue;
      }
      const href = (0,html_utils.fixURL)(jsURL.url);
      if (!href) {
        continue;
      }
      htmlButton.children.push({
        name: "a",
        attributes: {
          id: "link" + this[symbol_utils.$uid],
          href,
          newWindow: jsURL.newWindow,
          class: ["xfaLink"],
          style: {}
        },
        children: []
      });
    }
    return utils.HTMLResult.success(htmlButton);
  }
}
class Calculate extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "calculate", true);
    this.id = attributes.id || "";
    this.override = (0,utils.getStringOption)(attributes.override, ["disabled", "error", "ignore", "warning"]);
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.extras = null;
    this.message = null;
    this.script = null;
  }
}
class Caption extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "caption", true);
    this.id = attributes.id || "";
    this.placement = (0,utils.getStringOption)(attributes.placement, ["left", "bottom", "inline", "right", "top"]);
    this.presence = (0,utils.getStringOption)(attributes.presence, ["visible", "hidden", "inactive", "invisible"]);
    this.reserve = Math.ceil((0,utils.getMeasurement)(attributes.reserve));
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.extras = null;
    this.font = null;
    this.margin = null;
    this.para = null;
    this.value = null;
  }
  [symbol_utils.$setValue](value) {
    _setValue(this, value);
  }
  [symbol_utils.$getExtra](availableSpace) {
    if (!this[symbol_utils.$extra]) {
      let {
        width,
        height
      } = availableSpace;
      switch (this.placement) {
        case "left":
        case "right":
        case "inline":
          width = this.reserve <= 0 ? width : this.reserve;
          break;
        case "top":
        case "bottom":
          height = this.reserve <= 0 ? height : this.reserve;
          break;
      }
      this[symbol_utils.$extra] = (0,html_utils.layoutNode)(this, {
        width,
        height
      });
    }
    return this[symbol_utils.$extra];
  }
  [symbol_utils.$toHTML](availableSpace) {
    if (!this.value) {
      return utils.HTMLResult.EMPTY;
    }
    this[symbol_utils.$pushPara]();
    const value = this.value[symbol_utils.$toHTML](availableSpace).html;
    if (!value) {
      this[symbol_utils.$popPara]();
      return utils.HTMLResult.EMPTY;
    }
    const savedReserve = this.reserve;
    if (this.reserve <= 0) {
      const {
        w,
        h
      } = this[symbol_utils.$getExtra](availableSpace);
      switch (this.placement) {
        case "left":
        case "right":
        case "inline":
          this.reserve = w;
          break;
        case "top":
        case "bottom":
          this.reserve = h;
          break;
      }
    }
    const children = [];
    if (typeof value === "string") {
      children.push({
        name: "#text",
        value
      });
    } else {
      children.push(value);
    }
    const style = (0,html_utils.toStyle)(this, "font", "margin", "visibility");
    switch (this.placement) {
      case "left":
      case "right":
        if (this.reserve > 0) {
          style.width = (0,html_utils.measureToString)(this.reserve);
        }
        break;
      case "top":
      case "bottom":
        if (this.reserve > 0) {
          style.height = (0,html_utils.measureToString)(this.reserve);
        }
        break;
    }
    ;(0,html_utils.setPara)(this, null, value);
    this[symbol_utils.$popPara]();
    this.reserve = savedReserve;
    return utils.HTMLResult.success({
      name: "div",
      attributes: {
        style,
        class: ["xfaCaption"]
      },
      children
    });
  }
}
class Certificate extends xfa_object.StringObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "certificate");
    this.id = attributes.id || "";
    this.name = attributes.name || "";
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
  }
}
class Certificates extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "certificates", true);
    this.credentialServerPolicy = (0,utils.getStringOption)(attributes.credentialServerPolicy, ["optional", "required"]);
    this.id = attributes.id || "";
    this.url = attributes.url || "";
    this.urlPolicy = attributes.urlPolicy || "";
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.encryption = null;
    this.issuers = null;
    this.keyUsage = null;
    this.oids = null;
    this.signing = null;
    this.subjectDNs = null;
  }
}
class CheckButton extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "checkButton", true);
    this.id = attributes.id || "";
    this.mark = (0,utils.getStringOption)(attributes.mark, ["default", "check", "circle", "cross", "diamond", "square", "star"]);
    this.shape = (0,utils.getStringOption)(attributes.shape, ["square", "round"]);
    this.size = (0,utils.getMeasurement)(attributes.size, "10pt");
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.border = null;
    this.extras = null;
    this.margin = null;
  }
  [symbol_utils.$toHTML](availableSpace) {
    const style = (0,html_utils.toStyle)(this, "margin");
    const size = (0,html_utils.measureToString)(this.size);
    style.width = style.height = size;
    let type;
    let className;
    let groupId;
    const field = this[symbol_utils.$getParent]()[symbol_utils.$getParent]();
    const items = field.items.children.length && field.items.children[0][symbol_utils.$toHTML]().html || [];
    const exportedValue = {
      on: (items[0] !== undefined ? items[0] : "on").toString(),
      off: (items[1] !== undefined ? items[1] : "off").toString()
    };
    const value = field.value?.[symbol_utils.$text]() || "off";
    const checked = value === exportedValue.on || undefined;
    const container = field[symbol_utils.$getSubformParent]();
    const fieldId = field[symbol_utils.$uid];
    let dataId;
    if (container instanceof ExclGroup) {
      groupId = container[symbol_utils.$uid];
      type = "radio";
      className = "xfaRadio";
      dataId = container[symbol_utils.$data]?.[symbol_utils.$uid] || container[symbol_utils.$uid];
    } else {
      type = "checkbox";
      className = "xfaCheckbox";
      dataId = field[symbol_utils.$data]?.[symbol_utils.$uid] || field[symbol_utils.$uid];
    }
    const input = {
      name: "input",
      attributes: {
        class: [className],
        style,
        fieldId,
        dataId,
        type,
        checked,
        xfaOn: exportedValue.on,
        xfaOff: exportedValue.off,
        "aria-label": ariaLabel(field),
        "aria-required": false
      }
    };
    if (groupId) {
      input.attributes.name = groupId;
    }
    if (isRequired(field)) {
      input.attributes["aria-required"] = true;
      input.attributes.required = true;
    }
    return utils.HTMLResult.success({
      name: "label",
      attributes: {
        class: ["xfaLabel"]
      },
      children: [input]
    });
  }
}
class ChoiceList extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "choiceList", true);
    this.commitOn = (0,utils.getStringOption)(attributes.commitOn, ["select", "exit"]);
    this.id = attributes.id || "";
    this.open = (0,utils.getStringOption)(attributes.open, ["userControl", "always", "multiSelect", "onEntry"]);
    this.textEntry = (0,utils.getInteger)({
      data: attributes.textEntry,
      defaultValue: 0,
      validate: x => x === 1
    });
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.border = null;
    this.extras = null;
    this.margin = null;
  }
  [symbol_utils.$toHTML](availableSpace) {
    const style = (0,html_utils.toStyle)(this, "border", "margin");
    const ui = this[symbol_utils.$getParent]();
    const field = ui[symbol_utils.$getParent]();
    const fontSize = field.font?.size || 10;
    const optionStyle = {
      fontSize: `calc(${fontSize}px * var(--total-scale-factor))`
    };
    const children = [];
    if (field.items.children.length > 0) {
      const items = field.items;
      let displayedIndex = 0;
      let saveIndex = 0;
      if (items.children.length === 2) {
        displayedIndex = items.children[0].save;
        saveIndex = 1 - displayedIndex;
      }
      const displayed = items.children[displayedIndex][symbol_utils.$toHTML]().html;
      const values = items.children[saveIndex][symbol_utils.$toHTML]().html;
      let selected = false;
      const value = field.value?.[symbol_utils.$text]() || "";
      for (let i = 0, ii = displayed.length; i < ii; i++) {
        const option = {
          name: "option",
          attributes: {
            value: values[i] || displayed[i],
            style: optionStyle
          },
          value: displayed[i]
        };
        if (values[i] === value) {
          option.attributes.selected = selected = true;
        }
        children.push(option);
      }
      if (!selected) {
        children.splice(0, 0, {
          name: "option",
          attributes: {
            hidden: true,
            selected: true
          },
          value: " "
        });
      }
    }
    const selectAttributes = {
      class: ["xfaSelect"],
      fieldId: field[symbol_utils.$uid],
      dataId: field[symbol_utils.$data]?.[symbol_utils.$uid] || field[symbol_utils.$uid],
      style,
      "aria-label": ariaLabel(field),
      "aria-required": false
    };
    if (isRequired(field)) {
      selectAttributes["aria-required"] = true;
      selectAttributes.required = true;
    }
    if (this.open === "multiSelect") {
      selectAttributes.multiple = true;
    }
    return utils.HTMLResult.success({
      name: "label",
      attributes: {
        class: ["xfaLabel"]
      },
      children: [{
        name: "select",
        children,
        attributes: selectAttributes
      }]
    });
  }
}
class Color extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "color", true);
    this.cSpace = (0,utils.getStringOption)(attributes.cSpace, ["SRGB"]);
    this.id = attributes.id || "";
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.value = attributes.value ? (0,utils.getColor)(attributes.value) : "";
    this.extras = null;
  }
  [symbol_utils.$hasSettableValue]() {
    return false;
  }
  [symbol_utils.$toStyle]() {
    return this.value ? util.Util.makeHexColor(this.value.r, this.value.g, this.value.b) : null;
  }
}
class Comb extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "comb");
    this.id = attributes.id || "";
    this.numberOfCells = (0,utils.getInteger)({
      data: attributes.numberOfCells,
      defaultValue: 0,
      validate: x => x >= 0
    });
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
  }
}
class Connect extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "connect", true);
    this.connection = attributes.connection || "";
    this.id = attributes.id || "";
    this.ref = attributes.ref || "";
    this.usage = (0,utils.getStringOption)(attributes.usage, ["exportAndImport", "exportOnly", "importOnly"]);
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.picture = null;
  }
}
class ContentArea extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "contentArea", true);
    this.h = (0,utils.getMeasurement)(attributes.h);
    this.id = attributes.id || "";
    this.name = attributes.name || "";
    this.relevant = (0,utils.getRelevant)(attributes.relevant);
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.w = (0,utils.getMeasurement)(attributes.w);
    this.x = (0,utils.getMeasurement)(attributes.x, "0pt");
    this.y = (0,utils.getMeasurement)(attributes.y, "0pt");
    this.desc = null;
    this.extras = null;
  }
  [symbol_utils.$toHTML](availableSpace) {
    const left = (0,html_utils.measureToString)(this.x);
    const top = (0,html_utils.measureToString)(this.y);
    const style = {
      left,
      top,
      width: (0,html_utils.measureToString)(this.w),
      height: (0,html_utils.measureToString)(this.h)
    };
    const classNames = ["xfaContentarea"];
    if ((0,html_utils.isPrintOnly)(this)) {
      classNames.push("xfaPrintOnly");
    }
    return utils.HTMLResult.success({
      name: "div",
      children: [],
      attributes: {
        style,
        class: classNames,
        id: this[symbol_utils.$uid]
      }
    });
  }
}
class Corner extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "corner", true);
    this.id = attributes.id || "";
    this.inverted = (0,utils.getInteger)({
      data: attributes.inverted,
      defaultValue: 0,
      validate: x => x === 1
    });
    this.join = (0,utils.getStringOption)(attributes.join, ["square", "round"]);
    this.presence = (0,utils.getStringOption)(attributes.presence, ["visible", "hidden", "inactive", "invisible"]);
    this.radius = (0,utils.getMeasurement)(attributes.radius);
    this.stroke = (0,utils.getStringOption)(attributes.stroke, ["solid", "dashDot", "dashDotDot", "dashed", "dotted", "embossed", "etched", "lowered", "raised"]);
    this.thickness = (0,utils.getMeasurement)(attributes.thickness, "0.5pt");
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.color = null;
    this.extras = null;
  }
  [symbol_utils.$toStyle]() {
    const style = (0,html_utils.toStyle)(this, "visibility");
    style.radius = (0,html_utils.measureToString)(this.join === "square" ? 0 : this.radius);
    return style;
  }
}
class DateElement extends xfa_object.ContentObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "date");
    this.id = attributes.id || "";
    this.name = attributes.name || "";
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
  }
  [symbol_utils.$finalize]() {
    const date = this[symbol_utils.$content].trim();
    this[symbol_utils.$content] = date ? new Date(date) : null;
  }
  [symbol_utils.$toHTML](availableSpace) {
    return valueToHtml(this[symbol_utils.$content] ? this[symbol_utils.$content].toString() : "");
  }
}
class DateTime extends xfa_object.ContentObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "dateTime");
    this.id = attributes.id || "";
    this.name = attributes.name || "";
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
  }
  [symbol_utils.$finalize]() {
    const date = this[symbol_utils.$content].trim();
    this[symbol_utils.$content] = date ? new Date(date) : null;
  }
  [symbol_utils.$toHTML](availableSpace) {
    return valueToHtml(this[symbol_utils.$content] ? this[symbol_utils.$content].toString() : "");
  }
}
class DateTimeEdit extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "dateTimeEdit", true);
    this.hScrollPolicy = (0,utils.getStringOption)(attributes.hScrollPolicy, ["auto", "off", "on"]);
    this.id = attributes.id || "";
    this.picker = (0,utils.getStringOption)(attributes.picker, ["host", "none"]);
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.border = null;
    this.comb = null;
    this.extras = null;
    this.margin = null;
  }
  [symbol_utils.$toHTML](availableSpace) {
    const style = (0,html_utils.toStyle)(this, "border", "font", "margin");
    const field = this[symbol_utils.$getParent]()[symbol_utils.$getParent]();
    const html = {
      name: "input",
      attributes: {
        type: "text",
        fieldId: field[symbol_utils.$uid],
        dataId: field[symbol_utils.$data]?.[symbol_utils.$uid] || field[symbol_utils.$uid],
        class: ["xfaTextfield"],
        style,
        "aria-label": ariaLabel(field),
        "aria-required": false
      }
    };
    if (isRequired(field)) {
      html.attributes["aria-required"] = true;
      html.attributes.required = true;
    }
    return utils.HTMLResult.success({
      name: "label",
      attributes: {
        class: ["xfaLabel"]
      },
      children: [html]
    });
  }
}
class Decimal extends xfa_object.ContentObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "decimal");
    this.fracDigits = (0,utils.getInteger)({
      data: attributes.fracDigits,
      defaultValue: 2,
      validate: x => true
    });
    this.id = attributes.id || "";
    this.leadDigits = (0,utils.getInteger)({
      data: attributes.leadDigits,
      defaultValue: -1,
      validate: x => true
    });
    this.name = attributes.name || "";
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
  }
  [symbol_utils.$finalize]() {
    const number = parseFloat(this[symbol_utils.$content].trim());
    this[symbol_utils.$content] = isNaN(number) ? null : number;
  }
  [symbol_utils.$toHTML](availableSpace) {
    return valueToHtml(this[symbol_utils.$content] !== null ? this[symbol_utils.$content].toString() : "");
  }
}
class DefaultUi extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "defaultUi", true);
    this.id = attributes.id || "";
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.extras = null;
  }
}
class Desc extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "desc", true);
    this.id = attributes.id || "";
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.boolean = new xfa_object.XFAObjectArray();
    this.date = new xfa_object.XFAObjectArray();
    this.dateTime = new xfa_object.XFAObjectArray();
    this.decimal = new xfa_object.XFAObjectArray();
    this.exData = new xfa_object.XFAObjectArray();
    this.float = new xfa_object.XFAObjectArray();
    this.image = new xfa_object.XFAObjectArray();
    this.integer = new xfa_object.XFAObjectArray();
    this.text = new xfa_object.XFAObjectArray();
    this.time = new xfa_object.XFAObjectArray();
  }
}
class DigestMethod extends xfa_object.OptionObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "digestMethod", ["", "SHA1", "SHA256", "SHA512", "RIPEMD160"]);
    this.id = attributes.id || "";
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
  }
}
class DigestMethods extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "digestMethods", true);
    this.id = attributes.id || "";
    this.type = (0,utils.getStringOption)(attributes.type, ["optional", "required"]);
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.digestMethod = new xfa_object.XFAObjectArray();
  }
}
class Draw extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "draw", true);
    this.anchorType = (0,utils.getStringOption)(attributes.anchorType, ["topLeft", "bottomCenter", "bottomLeft", "bottomRight", "middleCenter", "middleLeft", "middleRight", "topCenter", "topRight"]);
    this.colSpan = (0,utils.getInteger)({
      data: attributes.colSpan,
      defaultValue: 1,
      validate: n => n >= 1 || n === -1
    });
    this.h = attributes.h ? (0,utils.getMeasurement)(attributes.h) : "";
    this.hAlign = (0,utils.getStringOption)(attributes.hAlign, ["left", "center", "justify", "justifyAll", "radix", "right"]);
    this.id = attributes.id || "";
    this.locale = attributes.locale || "";
    this.maxH = (0,utils.getMeasurement)(attributes.maxH, "0pt");
    this.maxW = (0,utils.getMeasurement)(attributes.maxW, "0pt");
    this.minH = (0,utils.getMeasurement)(attributes.minH, "0pt");
    this.minW = (0,utils.getMeasurement)(attributes.minW, "0pt");
    this.name = attributes.name || "";
    this.presence = (0,utils.getStringOption)(attributes.presence, ["visible", "hidden", "inactive", "invisible"]);
    this.relevant = (0,utils.getRelevant)(attributes.relevant);
    this.rotate = (0,utils.getInteger)({
      data: attributes.rotate,
      defaultValue: 0,
      validate: x => x % 90 === 0
    });
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.w = attributes.w ? (0,utils.getMeasurement)(attributes.w) : "";
    this.x = (0,utils.getMeasurement)(attributes.x, "0pt");
    this.y = (0,utils.getMeasurement)(attributes.y, "0pt");
    this.assist = null;
    this.border = null;
    this.caption = null;
    this.desc = null;
    this.extras = null;
    this.font = null;
    this.keep = null;
    this.margin = null;
    this.para = null;
    this.traversal = null;
    this.ui = null;
    this.value = null;
    this.setProperty = new xfa_object.XFAObjectArray();
  }
  [symbol_utils.$setValue](value) {
    _setValue(this, value);
  }
  [symbol_utils.$toHTML](availableSpace) {
    setTabIndex(this);
    if (this.presence === "hidden" || this.presence === "inactive") {
      return utils.HTMLResult.EMPTY;
    }
    ;(0,html_utils.fixDimensions)(this);
    this[symbol_utils.$pushPara]();
    const savedW = this.w;
    const savedH = this.h;
    const {
      w,
      h,
      isBroken
    } = (0,html_utils.layoutNode)(this, availableSpace);
    if (w && this.w === "") {
      if (isBroken && this[symbol_utils.$getSubformParent]()[symbol_utils.$isThereMoreWidth]()) {
        this[symbol_utils.$popPara]();
        return utils.HTMLResult.FAILURE;
      }
      this.w = w;
    }
    if (h && this.h === "") {
      this.h = h;
    }
    setFirstUnsplittable(this);
    if (!checkDimensions(this, availableSpace)) {
      this.w = savedW;
      this.h = savedH;
      this[symbol_utils.$popPara]();
      return utils.HTMLResult.FAILURE;
    }
    unsetFirstUnsplittable(this);
    const style = (0,html_utils.toStyle)(this, "font", "hAlign", "dimensions", "position", "presence", "rotate", "anchorType", "border", "margin");
    (0,html_utils.setMinMaxDimensions)(this, style);
    if (style.margin) {
      style.padding = style.margin;
      delete style.margin;
    }
    const classNames = ["xfaDraw"];
    if (this.font) {
      classNames.push("xfaFont");
    }
    if ((0,html_utils.isPrintOnly)(this)) {
      classNames.push("xfaPrintOnly");
    }
    const attributes = {
      style,
      id: this[symbol_utils.$uid],
      class: classNames
    };
    if (this.name) {
      attributes.xfaName = this.name;
    }
    const html = {
      name: "div",
      attributes,
      children: []
    };
    applyAssist(this, attributes);
    const bbox = (0,html_utils.computeBbox)(this, html, availableSpace);
    const value = this.value ? this.value[symbol_utils.$toHTML](availableSpace).html : null;
    if (value === null) {
      this.w = savedW;
      this.h = savedH;
      this[symbol_utils.$popPara]();
      return utils.HTMLResult.success((0,html_utils.createWrapper)(this, html), bbox);
    }
    html.children.push(value);
    (0,html_utils.setPara)(this, style, value);
    this.w = savedW;
    this.h = savedH;
    this[symbol_utils.$popPara]();
    return utils.HTMLResult.success((0,html_utils.createWrapper)(this, html), bbox);
  }
}
class Edge extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "edge", true);
    this.cap = (0,utils.getStringOption)(attributes.cap, ["square", "butt", "round"]);
    this.id = attributes.id || "";
    this.presence = (0,utils.getStringOption)(attributes.presence, ["visible", "hidden", "inactive", "invisible"]);
    this.stroke = (0,utils.getStringOption)(attributes.stroke, ["solid", "dashDot", "dashDotDot", "dashed", "dotted", "embossed", "etched", "lowered", "raised"]);
    this.thickness = (0,utils.getMeasurement)(attributes.thickness, "0.5pt");
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.color = null;
    this.extras = null;
  }
  [symbol_utils.$toStyle]() {
    const style = (0,html_utils.toStyle)(this, "visibility");
    Object.assign(style, {
      linecap: this.cap,
      width: (0,html_utils.measureToString)(this.thickness),
      color: this.color ? this.color[symbol_utils.$toStyle]() : "#000000",
      style: ""
    });
    if (this.presence !== "visible") {
      style.style = "none";
    } else {
      switch (this.stroke) {
        case "solid":
          style.style = "solid";
          break;
        case "dashDot":
          style.style = "dashed";
          break;
        case "dashDotDot":
          style.style = "dashed";
          break;
        case "dashed":
          style.style = "dashed";
          break;
        case "dotted":
          style.style = "dotted";
          break;
        case "embossed":
          style.style = "ridge";
          break;
        case "etched":
          style.style = "groove";
          break;
        case "lowered":
          style.style = "inset";
          break;
        case "raised":
          style.style = "outset";
          break;
      }
    }
    return style;
  }
}
class Encoding extends xfa_object.OptionObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "encoding", ["adbe.x509.rsa_sha1", "adbe.pkcs7.detached", "adbe.pkcs7.sha1"]);
    this.id = attributes.id || "";
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
  }
}
class Encodings extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "encodings", true);
    this.id = attributes.id || "";
    this.type = (0,utils.getStringOption)(attributes.type, ["optional", "required"]);
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.encoding = new xfa_object.XFAObjectArray();
  }
}
class Encrypt extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "encrypt", true);
    this.id = attributes.id || "";
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.certificate = null;
  }
}
class EncryptData extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "encryptData", true);
    this.id = attributes.id || "";
    this.operation = (0,utils.getStringOption)(attributes.operation, ["encrypt", "decrypt"]);
    this.target = attributes.target || "";
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.filter = null;
    this.manifest = null;
  }
}
class Encryption extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "encryption", true);
    this.id = attributes.id || "";
    this.type = (0,utils.getStringOption)(attributes.type, ["optional", "required"]);
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.certificate = new xfa_object.XFAObjectArray();
  }
}
class EncryptionMethod extends xfa_object.OptionObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "encryptionMethod", ["", "AES256-CBC", "TRIPLEDES-CBC", "AES128-CBC", "AES192-CBC"]);
    this.id = attributes.id || "";
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
  }
}
class EncryptionMethods extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "encryptionMethods", true);
    this.id = attributes.id || "";
    this.type = (0,utils.getStringOption)(attributes.type, ["optional", "required"]);
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.encryptionMethod = new xfa_object.XFAObjectArray();
  }
}
class Event extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "event", true);
    this.activity = (0,utils.getStringOption)(attributes.activity, ["click", "change", "docClose", "docReady", "enter", "exit", "full", "indexChange", "initialize", "mouseDown", "mouseEnter", "mouseExit", "mouseUp", "postExecute", "postOpen", "postPrint", "postSave", "postSign", "postSubmit", "preExecute", "preOpen", "prePrint", "preSave", "preSign", "preSubmit", "ready", "validationState"]);
    this.id = attributes.id || "";
    this.listen = (0,utils.getStringOption)(attributes.listen, ["refOnly", "refAndDescendents"]);
    this.name = attributes.name || "";
    this.ref = attributes.ref || "";
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.extras = null;
    this.encryptData = null;
    this.execute = null;
    this.script = null;
    this.signData = null;
    this.submit = null;
  }
}
class ExData extends xfa_object.ContentObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "exData");
    this.contentType = attributes.contentType || "";
    this.href = attributes.href || "";
    this.id = attributes.id || "";
    this.maxLength = (0,utils.getInteger)({
      data: attributes.maxLength,
      defaultValue: -1,
      validate: x => x >= -1
    });
    this.name = attributes.name || "";
    this.rid = attributes.rid || "";
    this.transferEncoding = (0,utils.getStringOption)(attributes.transferEncoding, ["none", "base64", "package"]);
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
  }
  [symbol_utils.$isCDATAXml]() {
    return this.contentType === "text/html";
  }
  [symbol_utils.$onChild](child) {
    if (this.contentType === "text/html" && child[symbol_utils.$namespaceId] === namespaces.NamespaceIds.xhtml.id) {
      this[symbol_utils.$content] = child;
      return true;
    }
    if (this.contentType === "text/xml") {
      this[symbol_utils.$content] = child;
      return true;
    }
    return false;
  }
  [symbol_utils.$toHTML](availableSpace) {
    if (this.contentType !== "text/html" || !this[symbol_utils.$content]) {
      return utils.HTMLResult.EMPTY;
    }
    return this[symbol_utils.$content][symbol_utils.$toHTML](availableSpace);
  }
}
class ExObject extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "exObject", true);
    this.archive = attributes.archive || "";
    this.classId = attributes.classId || "";
    this.codeBase = attributes.codeBase || "";
    this.codeType = attributes.codeType || "";
    this.id = attributes.id || "";
    this.name = attributes.name || "";
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.extras = null;
    this.boolean = new xfa_object.XFAObjectArray();
    this.date = new xfa_object.XFAObjectArray();
    this.dateTime = new xfa_object.XFAObjectArray();
    this.decimal = new xfa_object.XFAObjectArray();
    this.exData = new xfa_object.XFAObjectArray();
    this.exObject = new xfa_object.XFAObjectArray();
    this.float = new xfa_object.XFAObjectArray();
    this.image = new xfa_object.XFAObjectArray();
    this.integer = new xfa_object.XFAObjectArray();
    this.text = new xfa_object.XFAObjectArray();
    this.time = new xfa_object.XFAObjectArray();
  }
}
class ExclGroup extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "exclGroup", true);
    this.access = (0,utils.getStringOption)(attributes.access, ["open", "nonInteractive", "protected", "readOnly"]);
    this.accessKey = attributes.accessKey || "";
    this.anchorType = (0,utils.getStringOption)(attributes.anchorType, ["topLeft", "bottomCenter", "bottomLeft", "bottomRight", "middleCenter", "middleLeft", "middleRight", "topCenter", "topRight"]);
    this.colSpan = (0,utils.getInteger)({
      data: attributes.colSpan,
      defaultValue: 1,
      validate: n => n >= 1 || n === -1
    });
    this.h = attributes.h ? (0,utils.getMeasurement)(attributes.h) : "";
    this.hAlign = (0,utils.getStringOption)(attributes.hAlign, ["left", "center", "justify", "justifyAll", "radix", "right"]);
    this.id = attributes.id || "";
    this.layout = (0,utils.getStringOption)(attributes.layout, ["position", "lr-tb", "rl-row", "rl-tb", "row", "table", "tb"]);
    this.maxH = (0,utils.getMeasurement)(attributes.maxH, "0pt");
    this.maxW = (0,utils.getMeasurement)(attributes.maxW, "0pt");
    this.minH = (0,utils.getMeasurement)(attributes.minH, "0pt");
    this.minW = (0,utils.getMeasurement)(attributes.minW, "0pt");
    this.name = attributes.name || "";
    this.presence = (0,utils.getStringOption)(attributes.presence, ["visible", "hidden", "inactive", "invisible"]);
    this.relevant = (0,utils.getRelevant)(attributes.relevant);
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.w = attributes.w ? (0,utils.getMeasurement)(attributes.w) : "";
    this.x = (0,utils.getMeasurement)(attributes.x, "0pt");
    this.y = (0,utils.getMeasurement)(attributes.y, "0pt");
    this.assist = null;
    this.bind = null;
    this.border = null;
    this.calculate = null;
    this.caption = null;
    this.desc = null;
    this.extras = null;
    this.margin = null;
    this.para = null;
    this.traversal = null;
    this.validate = null;
    this.connect = new xfa_object.XFAObjectArray();
    this.event = new xfa_object.XFAObjectArray();
    this.field = new xfa_object.XFAObjectArray();
    this.setProperty = new xfa_object.XFAObjectArray();
  }
  [symbol_utils.$isBindable]() {
    return true;
  }
  [symbol_utils.$hasSettableValue]() {
    return true;
  }
  [symbol_utils.$setValue](value) {
    for (const field of this.field.children) {
      if (!field.value) {
        const nodeValue = new Value({});
        field[symbol_utils.$appendChild](nodeValue);
        field.value = nodeValue;
      }
      field.value[symbol_utils.$setValue](value);
    }
  }
  [symbol_utils.$isThereMoreWidth]() {
    return this.layout.endsWith("-tb") && this[symbol_utils.$extra].attempt === 0 && this[symbol_utils.$extra].numberInLine > 0 || this[symbol_utils.$getParent]()[symbol_utils.$isThereMoreWidth]();
  }
  [symbol_utils.$isSplittable]() {
    const parent = this[symbol_utils.$getSubformParent]();
    if (!parent[symbol_utils.$isSplittable]()) {
      return false;
    }
    if (this[symbol_utils.$extra]._isSplittable !== undefined) {
      return this[symbol_utils.$extra]._isSplittable;
    }
    if (this.layout === "position" || this.layout.includes("row")) {
      this[symbol_utils.$extra]._isSplittable = false;
      return false;
    }
    if (parent.layout?.endsWith("-tb") && parent[symbol_utils.$extra].numberInLine !== 0) {
      return false;
    }
    this[symbol_utils.$extra]._isSplittable = true;
    return true;
  }
  [symbol_utils.$flushHTML]() {
    return flushHTML(this);
  }
  [symbol_utils.$addHTML](html, bbox) {
    addHTML(this, html, bbox);
  }
  [symbol_utils.$getAvailableSpace]() {
    return getAvailableSpace(this);
  }
  [symbol_utils.$toHTML](availableSpace) {
    setTabIndex(this);
    if (this.presence === "hidden" || this.presence === "inactive" || this.h === 0 || this.w === 0) {
      return utils.HTMLResult.EMPTY;
    }
    ;(0,html_utils.fixDimensions)(this);
    const children = [];
    const attributes = {
      id: this[symbol_utils.$uid],
      class: []
    };
    (0,html_utils.setAccess)(this, attributes.class);
    this[symbol_utils.$extra] ||= Object.create(null);
    Object.assign(this[symbol_utils.$extra], {
      children,
      attributes,
      attempt: 0,
      line: null,
      numberInLine: 0,
      availableSpace: {
        width: Math.min(this.w || Infinity, availableSpace.width),
        height: Math.min(this.h || Infinity, availableSpace.height)
      },
      width: 0,
      height: 0,
      prevHeight: 0,
      currentWidth: 0
    });
    const isSplittable = this[symbol_utils.$isSplittable]();
    if (!isSplittable) {
      setFirstUnsplittable(this);
    }
    if (!checkDimensions(this, availableSpace)) {
      return utils.HTMLResult.FAILURE;
    }
    const filter = new Set(["field"]);
    if (this.layout.includes("row")) {
      const columnWidths = this[symbol_utils.$getSubformParent]().columnWidths;
      if (Array.isArray(columnWidths) && columnWidths.length > 0) {
        this[symbol_utils.$extra].columnWidths = columnWidths;
        this[symbol_utils.$extra].currentColumn = 0;
      }
    }
    const style = (0,html_utils.toStyle)(this, "anchorType", "dimensions", "position", "presence", "border", "margin", "hAlign");
    const classNames = ["xfaExclgroup"];
    const cl = (0,html_utils.layoutClass)(this);
    if (cl) {
      classNames.push(cl);
    }
    if ((0,html_utils.isPrintOnly)(this)) {
      classNames.push("xfaPrintOnly");
    }
    attributes.style = style;
    attributes.class = classNames;
    if (this.name) {
      attributes.xfaName = this.name;
    }
    this[symbol_utils.$pushPara]();
    const isLrTb = this.layout === "lr-tb" || this.layout === "rl-tb";
    const maxRun = isLrTb ? MAX_ATTEMPTS_FOR_LRTB_LAYOUT : 1;
    for (; this[symbol_utils.$extra].attempt < maxRun; this[symbol_utils.$extra].attempt++) {
      if (isLrTb && this[symbol_utils.$extra].attempt === MAX_ATTEMPTS_FOR_LRTB_LAYOUT - 1) {
        this[symbol_utils.$extra].numberInLine = 0;
      }
      const result = this[symbol_utils.$childrenToHTML]({
        filter,
        include: true
      });
      if (result.success) {
        break;
      }
      if (result.isBreak()) {
        this[symbol_utils.$popPara]();
        return result;
      }
      if (isLrTb && this[symbol_utils.$extra].attempt === 0 && this[symbol_utils.$extra].numberInLine === 0 && !this[symbol_utils.$getTemplateRoot]()[symbol_utils.$extra].noLayoutFailure) {
        this[symbol_utils.$extra].attempt = maxRun;
        break;
      }
    }
    this[symbol_utils.$popPara]();
    if (!isSplittable) {
      unsetFirstUnsplittable(this);
    }
    if (this[symbol_utils.$extra].attempt === maxRun) {
      if (!isSplittable) {
        delete this[symbol_utils.$extra];
      }
      return utils.HTMLResult.FAILURE;
    }
    let marginH = 0;
    let marginV = 0;
    if (this.margin) {
      marginH = this.margin.leftInset + this.margin.rightInset;
      marginV = this.margin.topInset + this.margin.bottomInset;
    }
    const width = Math.max(this[symbol_utils.$extra].width + marginH, this.w || 0);
    const height = Math.max(this[symbol_utils.$extra].height + marginV, this.h || 0);
    const bbox = [this.x, this.y, width, height];
    if (this.w === "") {
      style.width = (0,html_utils.measureToString)(width);
    }
    if (this.h === "") {
      style.height = (0,html_utils.measureToString)(height);
    }
    const html = {
      name: "div",
      attributes,
      children
    };
    applyAssist(this, attributes);
    delete this[symbol_utils.$extra];
    return utils.HTMLResult.success((0,html_utils.createWrapper)(this, html), bbox);
  }
}
class Execute extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "execute");
    this.connection = attributes.connection || "";
    this.executeType = (0,utils.getStringOption)(attributes.executeType, ["import", "remerge"]);
    this.id = attributes.id || "";
    this.runAt = (0,utils.getStringOption)(attributes.runAt, ["client", "both", "server"]);
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
  }
}
class Extras extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "extras", true);
    this.id = attributes.id || "";
    this.name = attributes.name || "";
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.boolean = new xfa_object.XFAObjectArray();
    this.date = new xfa_object.XFAObjectArray();
    this.dateTime = new xfa_object.XFAObjectArray();
    this.decimal = new xfa_object.XFAObjectArray();
    this.exData = new xfa_object.XFAObjectArray();
    this.extras = new xfa_object.XFAObjectArray();
    this.float = new xfa_object.XFAObjectArray();
    this.image = new xfa_object.XFAObjectArray();
    this.integer = new xfa_object.XFAObjectArray();
    this.text = new xfa_object.XFAObjectArray();
    this.time = new xfa_object.XFAObjectArray();
  }
}
class Field extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "field", true);
    this.access = (0,utils.getStringOption)(attributes.access, ["open", "nonInteractive", "protected", "readOnly"]);
    this.accessKey = attributes.accessKey || "";
    this.anchorType = (0,utils.getStringOption)(attributes.anchorType, ["topLeft", "bottomCenter", "bottomLeft", "bottomRight", "middleCenter", "middleLeft", "middleRight", "topCenter", "topRight"]);
    this.colSpan = (0,utils.getInteger)({
      data: attributes.colSpan,
      defaultValue: 1,
      validate: n => n >= 1 || n === -1
    });
    this.h = attributes.h ? (0,utils.getMeasurement)(attributes.h) : "";
    this.hAlign = (0,utils.getStringOption)(attributes.hAlign, ["left", "center", "justify", "justifyAll", "radix", "right"]);
    this.id = attributes.id || "";
    this.locale = attributes.locale || "";
    this.maxH = (0,utils.getMeasurement)(attributes.maxH, "0pt");
    this.maxW = (0,utils.getMeasurement)(attributes.maxW, "0pt");
    this.minH = (0,utils.getMeasurement)(attributes.minH, "0pt");
    this.minW = (0,utils.getMeasurement)(attributes.minW, "0pt");
    this.name = attributes.name || "";
    this.presence = (0,utils.getStringOption)(attributes.presence, ["visible", "hidden", "inactive", "invisible"]);
    this.relevant = (0,utils.getRelevant)(attributes.relevant);
    this.rotate = (0,utils.getInteger)({
      data: attributes.rotate,
      defaultValue: 0,
      validate: x => x % 90 === 0
    });
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.w = attributes.w ? (0,utils.getMeasurement)(attributes.w) : "";
    this.x = (0,utils.getMeasurement)(attributes.x, "0pt");
    this.y = (0,utils.getMeasurement)(attributes.y, "0pt");
    this.assist = null;
    this.bind = null;
    this.border = null;
    this.calculate = null;
    this.caption = null;
    this.desc = null;
    this.extras = null;
    this.font = null;
    this.format = null;
    this.items = new xfa_object.XFAObjectArray(2);
    this.keep = null;
    this.margin = null;
    this.para = null;
    this.traversal = null;
    this.ui = null;
    this.validate = null;
    this.value = null;
    this.bindItems = new xfa_object.XFAObjectArray();
    this.connect = new xfa_object.XFAObjectArray();
    this.event = new xfa_object.XFAObjectArray();
    this.setProperty = new xfa_object.XFAObjectArray();
  }
  [symbol_utils.$isBindable]() {
    return true;
  }
  [symbol_utils.$setValue](value) {
    _setValue(this, value);
  }
  [symbol_utils.$toHTML](availableSpace) {
    setTabIndex(this);
    if (!this.ui) {
      this.ui = new Ui({});
      this.ui[symbol_utils.$globalData] = this[symbol_utils.$globalData];
      this[symbol_utils.$appendChild](this.ui);
      let node;
      switch (this.items.children.length) {
        case 0:
          node = new TextEdit({});
          this.ui.textEdit = node;
          break;
        case 1:
          node = new CheckButton({});
          this.ui.checkButton = node;
          break;
        case 2:
          node = new ChoiceList({});
          this.ui.choiceList = node;
          break;
      }
      this.ui[symbol_utils.$appendChild](node);
    }
    if (!this.ui || this.presence === "hidden" || this.presence === "inactive" || this.h === 0 || this.w === 0) {
      return utils.HTMLResult.EMPTY;
    }
    if (this.caption) {
      delete this.caption[symbol_utils.$extra];
    }
    this[symbol_utils.$pushPara]();
    const caption = this.caption ? this.caption[symbol_utils.$toHTML](availableSpace).html : null;
    const savedW = this.w;
    const savedH = this.h;
    let marginH = 0;
    let marginV = 0;
    if (this.margin) {
      marginH = this.margin.leftInset + this.margin.rightInset;
      marginV = this.margin.topInset + this.margin.bottomInset;
    }
    let borderDims = null;
    if (this.w === "" || this.h === "") {
      let width = null;
      let height = null;
      let uiW = 0;
      let uiH = 0;
      if (this.ui.checkButton) {
        uiW = uiH = this.ui.checkButton.size;
      } else {
        const {
          w,
          h
        } = (0,html_utils.layoutNode)(this, availableSpace);
        if (w !== null) {
          uiW = w;
          uiH = h;
        } else {
          uiH = (0,xfa_fonts.getMetrics)(this.font, true).lineNoGap;
        }
      }
      borderDims = getBorderDims(this.ui[symbol_utils.$getExtra]());
      uiW += borderDims.w;
      uiH += borderDims.h;
      if (this.caption) {
        const {
          w,
          h,
          isBroken
        } = this.caption[symbol_utils.$getExtra](availableSpace);
        if (isBroken && this[symbol_utils.$getSubformParent]()[symbol_utils.$isThereMoreWidth]()) {
          this[symbol_utils.$popPara]();
          return utils.HTMLResult.FAILURE;
        }
        width = w;
        height = h;
        switch (this.caption.placement) {
          case "left":
          case "right":
          case "inline":
            width += uiW;
            break;
          case "top":
          case "bottom":
            height += uiH;
            break;
        }
      } else {
        width = uiW;
        height = uiH;
      }
      if (width && this.w === "") {
        width += marginH;
        this.w = Math.min(this.maxW <= 0 ? Infinity : this.maxW, this.minW + 1 < width ? width : this.minW);
      }
      if (height && this.h === "") {
        height += marginV;
        this.h = Math.min(this.maxH <= 0 ? Infinity : this.maxH, this.minH + 1 < height ? height : this.minH);
      }
    }
    this[symbol_utils.$popPara]();
    (0,html_utils.fixDimensions)(this);
    setFirstUnsplittable(this);
    if (!checkDimensions(this, availableSpace)) {
      this.w = savedW;
      this.h = savedH;
      this[symbol_utils.$popPara]();
      return utils.HTMLResult.FAILURE;
    }
    unsetFirstUnsplittable(this);
    const style = (0,html_utils.toStyle)(this, "font", "dimensions", "position", "rotate", "anchorType", "presence", "margin", "hAlign");
    (0,html_utils.setMinMaxDimensions)(this, style);
    const classNames = ["xfaField"];
    if (this.font) {
      classNames.push("xfaFont");
    }
    if ((0,html_utils.isPrintOnly)(this)) {
      classNames.push("xfaPrintOnly");
    }
    const attributes = {
      style,
      id: this[symbol_utils.$uid],
      class: classNames
    };
    if (style.margin) {
      style.padding = style.margin;
      delete style.margin;
    }
    ;(0,html_utils.setAccess)(this, classNames);
    if (this.name) {
      attributes.xfaName = this.name;
    }
    const children = [];
    const html = {
      name: "div",
      attributes,
      children
    };
    applyAssist(this, attributes);
    const borderStyle = this.border ? this.border[symbol_utils.$toStyle]() : null;
    const bbox = (0,html_utils.computeBbox)(this, html, availableSpace);
    const ui = this.ui[symbol_utils.$toHTML]().html;
    if (!ui) {
      Object.assign(style, borderStyle);
      return utils.HTMLResult.success((0,html_utils.createWrapper)(this, html), bbox);
    }
    if (this[symbol_utils.$tabIndex]) {
      if (ui.children?.[0]) {
        ui.children[0].attributes.tabindex = this[symbol_utils.$tabIndex];
      } else {
        ui.attributes.tabindex = this[symbol_utils.$tabIndex];
      }
    }
    ui.attributes.style ||= Object.create(null);
    let aElement = null;
    if (this.ui.button) {
      if (ui.children.length === 1) {
        [aElement] = ui.children.splice(0, 1);
      }
      Object.assign(ui.attributes.style, borderStyle);
    } else {
      Object.assign(style, borderStyle);
    }
    children.push(ui);
    if (this.value) {
      if (this.ui.imageEdit) {
        ui.children.push(this.value[symbol_utils.$toHTML]().html);
      } else if (!this.ui.button) {
        let value = "";
        if (this.value.exData) {
          value = this.value.exData[symbol_utils.$text]();
        } else if (this.value.text) {
          value = this.value.text[symbol_utils.$getExtra]();
        } else {
          const htmlValue = this.value[symbol_utils.$toHTML]().html;
          if (htmlValue !== null) {
            value = htmlValue.children[0].value;
          }
        }
        if (this.ui.textEdit && this.value.text?.maxChars) {
          ui.children[0].attributes.maxLength = this.value.text.maxChars;
        }
        if (value) {
          if (this.ui.numericEdit) {
            value = parseFloat(value);
            value = isNaN(value) ? "" : value.toString();
          }
          if (ui.children[0].name === "textarea") {
            ui.children[0].attributes.textContent = value;
          } else {
            ui.children[0].attributes.value = value;
          }
        }
      }
    }
    if (!this.ui.imageEdit && ui.children?.[0] && this.h) {
      borderDims ||= getBorderDims(this.ui[symbol_utils.$getExtra]());
      if (this.caption && ["top", "bottom"].includes(this.caption.placement)) {
        let captionHeight = this.caption.reserve;
        if (captionHeight <= 0) {
          captionHeight = this.caption[symbol_utils.$getExtra](availableSpace).h;
        }
        const inputHeight = this.h - captionHeight - marginV - borderDims.h;
        ui.children[0].attributes.style.height = (0,html_utils.measureToString)(inputHeight);
      } else {
        ui.children[0].attributes.style.height = "100%";
      }
    }
    if (aElement) {
      ui.children.push(aElement);
    }
    if (!caption) {
      if (ui.attributes.class) {
        ui.attributes.class.push("xfaLeft");
      }
      this.w = savedW;
      this.h = savedH;
      return utils.HTMLResult.success((0,html_utils.createWrapper)(this, html), bbox);
    }
    if (this.ui.button) {
      if (style.padding) {
        delete style.padding;
      }
      if (caption.name === "div") {
        caption.name = "span";
      }
      ui.children.push(caption);
      return utils.HTMLResult.success(html, bbox);
    } else if (this.ui.checkButton) {
      caption.attributes.class[0] = "xfaCaptionForCheckButton";
    }
    ui.attributes.class ||= [];
    ui.children.splice(0, 0, caption);
    switch (this.caption.placement) {
      case "left":
        ui.attributes.class.push("xfaLeft");
        break;
      case "right":
        ui.attributes.class.push("xfaRight");
        break;
      case "top":
        ui.attributes.class.push("xfaTop");
        break;
      case "bottom":
        ui.attributes.class.push("xfaBottom");
        break;
      case "inline":
        ui.attributes.class.push("xfaLeft");
        break;
    }
    this.w = savedW;
    this.h = savedH;
    return utils.HTMLResult.success((0,html_utils.createWrapper)(this, html), bbox);
  }
}
class Fill extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "fill", true);
    this.id = attributes.id || "";
    this.presence = (0,utils.getStringOption)(attributes.presence, ["visible", "hidden", "inactive", "invisible"]);
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.color = null;
    this.extras = null;
    this.linear = null;
    this.pattern = null;
    this.radial = null;
    this.solid = null;
    this.stipple = null;
  }
  [symbol_utils.$toStyle]() {
    const parent = this[symbol_utils.$getParent]();
    const grandpa = parent[symbol_utils.$getParent]();
    const ggrandpa = grandpa[symbol_utils.$getParent]();
    const style = Object.create(null);
    let propName = "color";
    let altPropName = propName;
    if (parent instanceof Border) {
      propName = "background-color";
      altPropName = "background";
      if (ggrandpa instanceof Ui) {
        style.backgroundColor = "white";
      }
    }
    if (parent instanceof Rectangle || parent instanceof Arc) {
      propName = altPropName = "fill";
      style.fill = "white";
    }
    for (const name of Object.getOwnPropertyNames(this)) {
      if (name === "extras" || name === "color") {
        continue;
      }
      const obj = this[name];
      if (!(obj instanceof xfa_object.XFAObject)) {
        continue;
      }
      const color = obj[symbol_utils.$toStyle](this.color);
      if (color) {
        style[color.startsWith("#") ? propName : altPropName] = color;
      }
      return style;
    }
    if (this.color?.value) {
      const color = this.color[symbol_utils.$toStyle]();
      style[color.startsWith("#") ? propName : altPropName] = color;
    }
    return style;
  }
}
class Filter extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "filter", true);
    this.addRevocationInfo = (0,utils.getStringOption)(attributes.addRevocationInfo, ["", "required", "optional", "none"]);
    this.id = attributes.id || "";
    this.name = attributes.name || "";
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.version = (0,utils.getInteger)({
      data: this.version,
      defaultValue: 5,
      validate: x => x >= 1 && x <= 5
    });
    this.appearanceFilter = null;
    this.certificates = null;
    this.digestMethods = null;
    this.encodings = null;
    this.encryptionMethods = null;
    this.handler = null;
    this.lockDocument = null;
    this.mdp = null;
    this.reasons = null;
    this.timeStamp = null;
  }
}
class Float extends xfa_object.ContentObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "float");
    this.id = attributes.id || "";
    this.name = attributes.name || "";
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
  }
  [symbol_utils.$finalize]() {
    const number = parseFloat(this[symbol_utils.$content].trim());
    this[symbol_utils.$content] = isNaN(number) ? null : number;
  }
  [symbol_utils.$toHTML](availableSpace) {
    return valueToHtml(this[symbol_utils.$content] !== null ? this[symbol_utils.$content].toString() : "");
  }
}
class Font extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "font", true);
    this.baselineShift = (0,utils.getMeasurement)(attributes.baselineShift);
    this.fontHorizontalScale = (0,utils.getFloat)({
      data: attributes.fontHorizontalScale,
      defaultValue: 100,
      validate: x => x >= 0
    });
    this.fontVerticalScale = (0,utils.getFloat)({
      data: attributes.fontVerticalScale,
      defaultValue: 100,
      validate: x => x >= 0
    });
    this.id = attributes.id || "";
    this.kerningMode = (0,utils.getStringOption)(attributes.kerningMode, ["none", "pair"]);
    this.letterSpacing = (0,utils.getMeasurement)(attributes.letterSpacing, "0");
    this.lineThrough = (0,utils.getInteger)({
      data: attributes.lineThrough,
      defaultValue: 0,
      validate: x => x === 1 || x === 2
    });
    this.lineThroughPeriod = (0,utils.getStringOption)(attributes.lineThroughPeriod, ["all", "word"]);
    this.overline = (0,utils.getInteger)({
      data: attributes.overline,
      defaultValue: 0,
      validate: x => x === 1 || x === 2
    });
    this.overlinePeriod = (0,utils.getStringOption)(attributes.overlinePeriod, ["all", "word"]);
    this.posture = (0,utils.getStringOption)(attributes.posture, ["normal", "italic"]);
    this.size = (0,utils.getMeasurement)(attributes.size, "10pt");
    this.typeface = attributes.typeface || "Courier";
    this.underline = (0,utils.getInteger)({
      data: attributes.underline,
      defaultValue: 0,
      validate: x => x === 1 || x === 2
    });
    this.underlinePeriod = (0,utils.getStringOption)(attributes.underlinePeriod, ["all", "word"]);
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.weight = (0,utils.getStringOption)(attributes.weight, ["normal", "bold"]);
    this.extras = null;
    this.fill = null;
  }
  [symbol_utils.$clean](builder) {
    super[symbol_utils.$clean](builder);
    this[symbol_utils.$globalData].usedTypefaces.add(this.typeface);
  }
  [symbol_utils.$toStyle]() {
    const style = (0,html_utils.toStyle)(this, "fill");
    const color = style.color;
    if (color) {
      if (color === "#000000") {
        delete style.color;
      } else if (!color.startsWith("#")) {
        style.background = color;
        style.backgroundClip = "text";
        style.color = "transparent";
      }
    }
    if (this.baselineShift) {
      style.verticalAlign = (0,html_utils.measureToString)(this.baselineShift);
    }
    style.fontKerning = this.kerningMode === "none" ? "none" : "normal";
    style.letterSpacing = (0,html_utils.measureToString)(this.letterSpacing);
    if (this.lineThrough !== 0) {
      style.textDecoration = "line-through";
      if (this.lineThrough === 2) {
        style.textDecorationStyle = "double";
      }
    }
    if (this.overline !== 0) {
      style.textDecoration = "overline";
      if (this.overline === 2) {
        style.textDecorationStyle = "double";
      }
    }
    style.fontStyle = this.posture;
    style.fontSize = (0,html_utils.measureToString)(0.99 * this.size);
    (0,html_utils.setFontFamily)(this, this, this[symbol_utils.$globalData].fontFinder, style);
    if (this.underline !== 0) {
      style.textDecoration = "underline";
      if (this.underline === 2) {
        style.textDecorationStyle = "double";
      }
    }
    style.fontWeight = this.weight;
    return style;
  }
}
class Format extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "format", true);
    this.id = attributes.id || "";
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.extras = null;
    this.picture = null;
  }
}
class Handler extends xfa_object.StringObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "handler");
    this.id = attributes.id || "";
    this.type = (0,utils.getStringOption)(attributes.type, ["optional", "required"]);
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
  }
}
class Hyphenation extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "hyphenation");
    this.excludeAllCaps = (0,utils.getInteger)({
      data: attributes.excludeAllCaps,
      defaultValue: 0,
      validate: x => x === 1
    });
    this.excludeInitialCap = (0,utils.getInteger)({
      data: attributes.excludeInitialCap,
      defaultValue: 0,
      validate: x => x === 1
    });
    this.hyphenate = (0,utils.getInteger)({
      data: attributes.hyphenate,
      defaultValue: 0,
      validate: x => x === 1
    });
    this.id = attributes.id || "";
    this.pushCharacterCount = (0,utils.getInteger)({
      data: attributes.pushCharacterCount,
      defaultValue: 3,
      validate: x => x >= 0
    });
    this.remainCharacterCount = (0,utils.getInteger)({
      data: attributes.remainCharacterCount,
      defaultValue: 3,
      validate: x => x >= 0
    });
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.wordCharacterCount = (0,utils.getInteger)({
      data: attributes.wordCharacterCount,
      defaultValue: 7,
      validate: x => x >= 0
    });
  }
}
class Image extends xfa_object.StringObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "image");
    this.aspect = (0,utils.getStringOption)(attributes.aspect, ["fit", "actual", "height", "none", "width"]);
    this.contentType = attributes.contentType || "";
    this.href = attributes.href || "";
    this.id = attributes.id || "";
    this.name = attributes.name || "";
    this.transferEncoding = (0,utils.getStringOption)(attributes.transferEncoding, ["base64", "none", "package"]);
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
  }
  [symbol_utils.$toHTML]() {
    if (this.contentType && !MIMES.has(this.contentType.toLowerCase())) {
      return utils.HTMLResult.EMPTY;
    }
    let buffer = this[symbol_utils.$globalData].images?.get(this.href);
    if (!buffer && (this.href || !this[symbol_utils.$content])) {
      return utils.HTMLResult.EMPTY;
    }
    if (!buffer && this.transferEncoding === "base64") {
      buffer = Uint8Array.fromBase64(this[symbol_utils.$content]);
    }
    if (!buffer) {
      return utils.HTMLResult.EMPTY;
    }
    if (!this.contentType) {
      for (const [header, type] of IMAGES_HEADERS) {
        if (buffer.length > header.length && header.every((x, i) => x === buffer[i])) {
          this.contentType = type;
          break;
        }
      }
      if (!this.contentType) {
        return utils.HTMLResult.EMPTY;
      }
    }
    const blob = new Blob([buffer], {
      type: this.contentType
    });
    let style;
    switch (this.aspect) {
      case "fit":
      case "actual":
        break;
      case "height":
        style = {
          height: "100%",
          objectFit: "fill"
        };
        break;
      case "none":
        style = {
          width: "100%",
          height: "100%",
          objectFit: "fill"
        };
        break;
      case "width":
        style = {
          width: "100%",
          objectFit: "fill"
        };
        break;
    }
    const parent = this[symbol_utils.$getParent]();
    return utils.HTMLResult.success({
      name: "img",
      attributes: {
        class: ["xfaImage"],
        style,
        src: URL.createObjectURL(blob),
        alt: parent ? ariaLabel(parent[symbol_utils.$getParent]()) : null
      }
    });
  }
}
class ImageEdit extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "imageEdit", true);
    this.data = (0,utils.getStringOption)(attributes.data, ["link", "embed"]);
    this.id = attributes.id || "";
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.border = null;
    this.extras = null;
    this.margin = null;
  }
  [symbol_utils.$toHTML](availableSpace) {
    if (this.data === "embed") {
      return utils.HTMLResult.success({
        name: "div",
        children: [],
        attributes: {}
      });
    }
    return utils.HTMLResult.EMPTY;
  }
}
class Integer extends xfa_object.ContentObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "integer");
    this.id = attributes.id || "";
    this.name = attributes.name || "";
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
  }
  [symbol_utils.$finalize]() {
    const number = parseInt(this[symbol_utils.$content].trim(), 10);
    this[symbol_utils.$content] = isNaN(number) ? null : number;
  }
  [symbol_utils.$toHTML](availableSpace) {
    return valueToHtml(this[symbol_utils.$content] !== null ? this[symbol_utils.$content].toString() : "");
  }
}
class Issuers extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "issuers", true);
    this.id = attributes.id || "";
    this.type = (0,utils.getStringOption)(attributes.type, ["optional", "required"]);
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.certificate = new xfa_object.XFAObjectArray();
  }
}
class Items extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "items", true);
    this.id = attributes.id || "";
    this.name = attributes.name || "";
    this.presence = (0,utils.getStringOption)(attributes.presence, ["visible", "hidden", "inactive", "invisible"]);
    this.ref = attributes.ref || "";
    this.save = (0,utils.getInteger)({
      data: attributes.save,
      defaultValue: 0,
      validate: x => x === 1
    });
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.boolean = new xfa_object.XFAObjectArray();
    this.date = new xfa_object.XFAObjectArray();
    this.dateTime = new xfa_object.XFAObjectArray();
    this.decimal = new xfa_object.XFAObjectArray();
    this.exData = new xfa_object.XFAObjectArray();
    this.float = new xfa_object.XFAObjectArray();
    this.image = new xfa_object.XFAObjectArray();
    this.integer = new xfa_object.XFAObjectArray();
    this.text = new xfa_object.XFAObjectArray();
    this.time = new xfa_object.XFAObjectArray();
  }
  [symbol_utils.$toHTML]() {
    const output = [];
    for (const child of this[symbol_utils.$getChildren]()) {
      output.push(child[symbol_utils.$text]());
    }
    return utils.HTMLResult.success(output);
  }
}
class Keep extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "keep", true);
    this.id = attributes.id || "";
    const options = ["none", "contentArea", "pageArea"];
    this.intact = (0,utils.getStringOption)(attributes.intact, options);
    this.next = (0,utils.getStringOption)(attributes.next, options);
    this.previous = (0,utils.getStringOption)(attributes.previous, options);
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.extras = null;
  }
}
class KeyUsage extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "keyUsage");
    const options = ["", "yes", "no"];
    this.crlSign = (0,utils.getStringOption)(attributes.crlSign, options);
    this.dataEncipherment = (0,utils.getStringOption)(attributes.dataEncipherment, options);
    this.decipherOnly = (0,utils.getStringOption)(attributes.decipherOnly, options);
    this.digitalSignature = (0,utils.getStringOption)(attributes.digitalSignature, options);
    this.encipherOnly = (0,utils.getStringOption)(attributes.encipherOnly, options);
    this.id = attributes.id || "";
    this.keyAgreement = (0,utils.getStringOption)(attributes.keyAgreement, options);
    this.keyCertSign = (0,utils.getStringOption)(attributes.keyCertSign, options);
    this.keyEncipherment = (0,utils.getStringOption)(attributes.keyEncipherment, options);
    this.nonRepudiation = (0,utils.getStringOption)(attributes.nonRepudiation, options);
    this.type = (0,utils.getStringOption)(attributes.type, ["optional", "required"]);
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
  }
}
class Line extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "line", true);
    this.hand = (0,utils.getStringOption)(attributes.hand, ["even", "left", "right"]);
    this.id = attributes.id || "";
    this.slope = (0,utils.getStringOption)(attributes.slope, ["\\", "/"]);
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.edge = null;
  }
  [symbol_utils.$toHTML]() {
    const parent = this[symbol_utils.$getParent]()[symbol_utils.$getParent]();
    const edge = this.edge || new Edge({});
    const edgeStyle = edge[symbol_utils.$toStyle]();
    const style = Object.create(null);
    const thickness = edge.presence === "visible" ? edge.thickness : 0;
    style.strokeWidth = (0,html_utils.measureToString)(thickness);
    style.stroke = edgeStyle.color;
    let x1, y1, x2, y2;
    let width = "100%";
    let height = "100%";
    if (parent.w <= thickness) {
      [x1, y1, x2, y2] = ["50%", 0, "50%", "100%"];
      width = style.strokeWidth;
    } else if (parent.h <= thickness) {
      [x1, y1, x2, y2] = [0, "50%", "100%", "50%"];
      height = style.strokeWidth;
    } else if (this.slope === "\\") {
      [x1, y1, x2, y2] = [0, 0, "100%", "100%"];
    } else {
      [x1, y1, x2, y2] = [0, "100%", "100%", 0];
    }
    const line = {
      name: "line",
      attributes: {
        xmlns: util.SVG_NS,
        x1,
        y1,
        x2,
        y2,
        style
      }
    };
    const svg = {
      name: "svg",
      children: [line],
      attributes: {
        xmlns: util.SVG_NS,
        width,
        height,
        style: {
          overflow: "visible"
        }
      }
    };
    if (hasMargin(parent)) {
      return utils.HTMLResult.success({
        name: "div",
        attributes: {
          style: {
            display: "inline",
            width: "100%",
            height: "100%"
          }
        },
        children: [svg]
      });
    }
    svg.attributes.style.position = "absolute";
    return utils.HTMLResult.success(svg);
  }
}
class Linear extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "linear", true);
    this.id = attributes.id || "";
    this.type = (0,utils.getStringOption)(attributes.type, ["toRight", "toBottom", "toLeft", "toTop"]);
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.color = null;
    this.extras = null;
  }
  [symbol_utils.$toStyle](startColor) {
    startColor = startColor ? startColor[symbol_utils.$toStyle]() : "#FFFFFF";
    const transf = this.type.replace(/([RBLT])/, " $1").toLowerCase();
    const endColor = this.color ? this.color[symbol_utils.$toStyle]() : "#000000";
    return `linear-gradient(${transf}, ${startColor}, ${endColor})`;
  }
}
class LockDocument extends xfa_object.ContentObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "lockDocument");
    this.id = attributes.id || "";
    this.type = (0,utils.getStringOption)(attributes.type, ["optional", "required"]);
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
  }
  [symbol_utils.$finalize]() {
    this[symbol_utils.$content] = (0,utils.getStringOption)(this[symbol_utils.$content], ["auto", "0", "1"]);
  }
}
class Manifest extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "manifest", true);
    this.action = (0,utils.getStringOption)(attributes.action, ["include", "all", "exclude"]);
    this.id = attributes.id || "";
    this.name = attributes.name || "";
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.extras = null;
    this.ref = new xfa_object.XFAObjectArray();
  }
}
class Margin extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "margin", true);
    this.bottomInset = (0,utils.getMeasurement)(attributes.bottomInset, "0");
    this.id = attributes.id || "";
    this.leftInset = (0,utils.getMeasurement)(attributes.leftInset, "0");
    this.rightInset = (0,utils.getMeasurement)(attributes.rightInset, "0");
    this.topInset = (0,utils.getMeasurement)(attributes.topInset, "0");
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.extras = null;
  }
  [symbol_utils.$toStyle]() {
    return {
      margin: (0,html_utils.measureToString)(this.topInset) + " " + (0,html_utils.measureToString)(this.rightInset) + " " + (0,html_utils.measureToString)(this.bottomInset) + " " + (0,html_utils.measureToString)(this.leftInset)
    };
  }
}
class Mdp extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "mdp");
    this.id = attributes.id || "";
    this.permissions = (0,utils.getInteger)({
      data: attributes.permissions,
      defaultValue: 2,
      validate: x => x === 1 || x === 3
    });
    this.signatureType = (0,utils.getStringOption)(attributes.signatureType, ["filler", "author"]);
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
  }
}
class Medium extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "medium");
    this.id = attributes.id || "";
    this.imagingBBox = (0,utils.getBBox)(attributes.imagingBBox);
    this.long = (0,utils.getMeasurement)(attributes.long);
    this.orientation = (0,utils.getStringOption)(attributes.orientation, ["portrait", "landscape"]);
    this.short = (0,utils.getMeasurement)(attributes.short);
    this.stock = attributes.stock || "";
    this.trayIn = (0,utils.getStringOption)(attributes.trayIn, ["auto", "delegate", "pageFront"]);
    this.trayOut = (0,utils.getStringOption)(attributes.trayOut, ["auto", "delegate"]);
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
  }
}
class Message extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "message", true);
    this.id = attributes.id || "";
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.text = new xfa_object.XFAObjectArray();
  }
}
class NumericEdit extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "numericEdit", true);
    this.hScrollPolicy = (0,utils.getStringOption)(attributes.hScrollPolicy, ["auto", "off", "on"]);
    this.id = attributes.id || "";
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.border = null;
    this.comb = null;
    this.extras = null;
    this.margin = null;
  }
  [symbol_utils.$toHTML](availableSpace) {
    const style = (0,html_utils.toStyle)(this, "border", "font", "margin");
    const field = this[symbol_utils.$getParent]()[symbol_utils.$getParent]();
    const html = {
      name: "input",
      attributes: {
        type: "text",
        fieldId: field[symbol_utils.$uid],
        dataId: field[symbol_utils.$data]?.[symbol_utils.$uid] || field[symbol_utils.$uid],
        class: ["xfaTextfield"],
        style,
        "aria-label": ariaLabel(field),
        "aria-required": false
      }
    };
    if (isRequired(field)) {
      html.attributes["aria-required"] = true;
      html.attributes.required = true;
    }
    return utils.HTMLResult.success({
      name: "label",
      attributes: {
        class: ["xfaLabel"]
      },
      children: [html]
    });
  }
}
class Occur extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "occur", true);
    this.id = attributes.id || "";
    this.initial = attributes.initial !== "" ? (0,utils.getInteger)({
      data: attributes.initial,
      defaultValue: "",
      validate: x => true
    }) : "";
    this.max = attributes.max !== "" ? (0,utils.getInteger)({
      data: attributes.max,
      defaultValue: -1,
      validate: x => true
    }) : "";
    this.min = attributes.min !== "" ? (0,utils.getInteger)({
      data: attributes.min,
      defaultValue: 1,
      validate: x => true
    }) : "";
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.extras = null;
  }
  [symbol_utils.$clean]() {
    const parent = this[symbol_utils.$getParent]();
    const originalMin = this.min;
    if (this.min === "") {
      this.min = parent instanceof PageArea || parent instanceof PageSet ? 0 : 1;
    }
    if (this.max === "") {
      if (originalMin === "") {
        this.max = parent instanceof PageArea || parent instanceof PageSet ? -1 : 1;
      } else {
        this.max = this.min;
      }
    }
    if (this.max !== -1 && this.max < this.min) {
      this.max = this.min;
    }
    if (this.initial === "") {
      this.initial = parent instanceof Template ? 1 : this.min;
    }
  }
}
class Oid extends xfa_object.StringObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "oid");
    this.id = attributes.id || "";
    this.name = attributes.name || "";
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
  }
}
class Oids extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "oids", true);
    this.id = attributes.id || "";
    this.type = (0,utils.getStringOption)(attributes.type, ["optional", "required"]);
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.oid = new xfa_object.XFAObjectArray();
  }
}
class Overflow extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "overflow");
    this.id = attributes.id || "";
    this.leader = attributes.leader || "";
    this.target = attributes.target || "";
    this.trailer = attributes.trailer || "";
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
  }
  [symbol_utils.$getExtra]() {
    if (!this[symbol_utils.$extra]) {
      const parent = this[symbol_utils.$getParent]();
      const root = this[symbol_utils.$getTemplateRoot]();
      const target = root[symbol_utils.$searchNode](this.target, parent);
      const leader = root[symbol_utils.$searchNode](this.leader, parent);
      const trailer = root[symbol_utils.$searchNode](this.trailer, parent);
      this[symbol_utils.$extra] = {
        target: target?.[0] || null,
        leader: leader?.[0] || null,
        trailer: trailer?.[0] || null,
        addLeader: false,
        addTrailer: false
      };
    }
    return this[symbol_utils.$extra];
  }
}
class PageArea extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "pageArea", true);
    this.blankOrNotBlank = (0,utils.getStringOption)(attributes.blankOrNotBlank, ["any", "blank", "notBlank"]);
    this.id = attributes.id || "";
    this.initialNumber = (0,utils.getInteger)({
      data: attributes.initialNumber,
      defaultValue: 1,
      validate: x => true
    });
    this.name = attributes.name || "";
    this.numbered = (0,utils.getInteger)({
      data: attributes.numbered,
      defaultValue: 1,
      validate: x => true
    });
    this.oddOrEven = (0,utils.getStringOption)(attributes.oddOrEven, ["any", "even", "odd"]);
    this.pagePosition = (0,utils.getStringOption)(attributes.pagePosition, ["any", "first", "last", "only", "rest"]);
    this.relevant = (0,utils.getRelevant)(attributes.relevant);
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.desc = null;
    this.extras = null;
    this.medium = null;
    this.occur = null;
    this.area = new xfa_object.XFAObjectArray();
    this.contentArea = new xfa_object.XFAObjectArray();
    this.draw = new xfa_object.XFAObjectArray();
    this.exclGroup = new xfa_object.XFAObjectArray();
    this.field = new xfa_object.XFAObjectArray();
    this.subform = new xfa_object.XFAObjectArray();
  }
  [symbol_utils.$isUsable]() {
    if (!this[symbol_utils.$extra]) {
      this[symbol_utils.$extra] = {
        numberOfUse: 0
      };
      return true;
    }
    return !this.occur || this.occur.max === -1 || this[symbol_utils.$extra].numberOfUse < this.occur.max;
  }
  [symbol_utils.$cleanPage]() {
    delete this[symbol_utils.$extra];
  }
  [symbol_utils.$getNextPage]() {
    this[symbol_utils.$extra] ||= {
      numberOfUse: 0
    };
    const parent = this[symbol_utils.$getParent]();
    if (parent.relation === "orderedOccurrence") {
      if (this[symbol_utils.$isUsable]()) {
        this[symbol_utils.$extra].numberOfUse += 1;
        return this;
      }
    }
    return parent[symbol_utils.$getNextPage]();
  }
  [symbol_utils.$getAvailableSpace]() {
    return this[symbol_utils.$extra].space || {
      width: 0,
      height: 0
    };
  }
  [symbol_utils.$toHTML]() {
    this[symbol_utils.$extra] ||= {
      numberOfUse: 1
    };
    const children = [];
    this[symbol_utils.$extra].children = children;
    const style = Object.create(null);
    if (this.medium && this.medium.short && this.medium.long) {
      style.width = (0,html_utils.measureToString)(this.medium.short);
      style.height = (0,html_utils.measureToString)(this.medium.long);
      this[symbol_utils.$extra].space = {
        width: this.medium.short,
        height: this.medium.long
      };
      if (this.medium.orientation === "landscape") {
        const x = style.width;
        style.width = style.height;
        style.height = x;
        this[symbol_utils.$extra].space = {
          width: this.medium.long,
          height: this.medium.short
        };
      }
    } else {
      (0,util.warn)("XFA - No medium specified in pageArea: please file a bug.");
    }
    this[symbol_utils.$childrenToHTML]({
      filter: new Set(["area", "draw", "field", "subform"]),
      include: true
    });
    this[symbol_utils.$childrenToHTML]({
      filter: new Set(["contentArea"]),
      include: true
    });
    return utils.HTMLResult.success({
      name: "div",
      children,
      attributes: {
        class: ["xfaPage"],
        id: this[symbol_utils.$uid],
        style,
        xfaName: this.name
      }
    });
  }
}
class PageSet extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "pageSet", true);
    this.duplexImposition = (0,utils.getStringOption)(attributes.duplexImposition, ["longEdge", "shortEdge"]);
    this.id = attributes.id || "";
    this.name = attributes.name || "";
    this.relation = (0,utils.getStringOption)(attributes.relation, ["orderedOccurrence", "duplexPaginated", "simplexPaginated"]);
    this.relevant = (0,utils.getRelevant)(attributes.relevant);
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.extras = null;
    this.occur = null;
    this.pageArea = new xfa_object.XFAObjectArray();
    this.pageSet = new xfa_object.XFAObjectArray();
  }
  [symbol_utils.$cleanPage]() {
    for (const page of this.pageArea.children) {
      page[symbol_utils.$cleanPage]();
    }
    for (const page of this.pageSet.children) {
      page[symbol_utils.$cleanPage]();
    }
  }
  [symbol_utils.$isUsable]() {
    return !this.occur || this.occur.max === -1 || this[symbol_utils.$extra].numberOfUse < this.occur.max;
  }
  [symbol_utils.$getNextPage]() {
    this[symbol_utils.$extra] ||= {
      numberOfUse: 1,
      pageIndex: -1,
      pageSetIndex: -1
    };
    if (this.relation === "orderedOccurrence") {
      if (this[symbol_utils.$extra].pageIndex + 1 < this.pageArea.children.length) {
        this[symbol_utils.$extra].pageIndex += 1;
        const pageArea = this.pageArea.children[this[symbol_utils.$extra].pageIndex];
        return pageArea[symbol_utils.$getNextPage]();
      }
      if (this[symbol_utils.$extra].pageSetIndex + 1 < this.pageSet.children.length) {
        this[symbol_utils.$extra].pageSetIndex += 1;
        return this.pageSet.children[this[symbol_utils.$extra].pageSetIndex][symbol_utils.$getNextPage]();
      }
      if (this[symbol_utils.$isUsable]()) {
        this[symbol_utils.$extra].numberOfUse += 1;
        this[symbol_utils.$extra].pageIndex = -1;
        this[symbol_utils.$extra].pageSetIndex = -1;
        return this[symbol_utils.$getNextPage]();
      }
      const parent = this[symbol_utils.$getParent]();
      if (parent instanceof PageSet) {
        return parent[symbol_utils.$getNextPage]();
      }
      this[symbol_utils.$cleanPage]();
      return this[symbol_utils.$getNextPage]();
    }
    const pageNumber = this[symbol_utils.$getTemplateRoot]()[symbol_utils.$extra].pageNumber;
    const parity = pageNumber % 2 === 0 ? "even" : "odd";
    const position = pageNumber === 0 ? "first" : "rest";
    let page = this.pageArea.children.find(p => p.oddOrEven === parity && p.pagePosition === position);
    if (page) {
      return page;
    }
    page = this.pageArea.children.find(p => p.oddOrEven === "any" && p.pagePosition === position);
    if (page) {
      return page;
    }
    page = this.pageArea.children.find(p => p.oddOrEven === "any" && p.pagePosition === "any");
    return page ?? this.pageArea.children[0];
  }
}
class Para extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "para", true);
    this.hAlign = (0,utils.getStringOption)(attributes.hAlign, ["left", "center", "justify", "justifyAll", "radix", "right"]);
    this.id = attributes.id || "";
    this.lineHeight = attributes.lineHeight ? (0,utils.getMeasurement)(attributes.lineHeight, "0pt") : "";
    this.marginLeft = attributes.marginLeft ? (0,utils.getMeasurement)(attributes.marginLeft, "0pt") : "";
    this.marginRight = attributes.marginRight ? (0,utils.getMeasurement)(attributes.marginRight, "0pt") : "";
    this.orphans = (0,utils.getInteger)({
      data: attributes.orphans,
      defaultValue: 0,
      validate: x => x >= 0
    });
    this.preserve = attributes.preserve || "";
    this.radixOffset = attributes.radixOffset ? (0,utils.getMeasurement)(attributes.radixOffset, "0pt") : "";
    this.spaceAbove = attributes.spaceAbove ? (0,utils.getMeasurement)(attributes.spaceAbove, "0pt") : "";
    this.spaceBelow = attributes.spaceBelow ? (0,utils.getMeasurement)(attributes.spaceBelow, "0pt") : "";
    this.tabDefault = attributes.tabDefault ? (0,utils.getMeasurement)(this.tabDefault) : "";
    this.tabStops = (attributes.tabStops || "").trim().split(/\s+/).map((x, i) => i % 2 === 1 ? (0,utils.getMeasurement)(x) : x);
    this.textIndent = attributes.textIndent ? (0,utils.getMeasurement)(attributes.textIndent, "0pt") : "";
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.vAlign = (0,utils.getStringOption)(attributes.vAlign, ["top", "bottom", "middle"]);
    this.widows = (0,utils.getInteger)({
      data: attributes.widows,
      defaultValue: 0,
      validate: x => x >= 0
    });
    this.hyphenation = null;
  }
  [symbol_utils.$toStyle]() {
    const style = (0,html_utils.toStyle)(this, "hAlign");
    if (this.marginLeft !== "") {
      style.paddingLeft = (0,html_utils.measureToString)(this.marginLeft);
    }
    if (this.marginRight !== "") {
      style.paddingRight = (0,html_utils.measureToString)(this.marginRight);
    }
    if (this.spaceAbove !== "") {
      style.paddingTop = (0,html_utils.measureToString)(this.spaceAbove);
    }
    if (this.spaceBelow !== "") {
      style.paddingBottom = (0,html_utils.measureToString)(this.spaceBelow);
    }
    if (this.textIndent !== "") {
      style.textIndent = (0,html_utils.measureToString)(this.textIndent);
      (0,html_utils.fixTextIndent)(style);
    }
    if (this.lineHeight > 0) {
      style.lineHeight = (0,html_utils.measureToString)(this.lineHeight);
    }
    if (this.tabDefault !== "") {
      style.tabSize = (0,html_utils.measureToString)(this.tabDefault);
    }
    if (this.tabStops.length > 0) {}
    if (this.hyphenatation) {
      Object.assign(style, this.hyphenatation[symbol_utils.$toStyle]());
    }
    return style;
  }
}
class PasswordEdit extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "passwordEdit", true);
    this.hScrollPolicy = (0,utils.getStringOption)(attributes.hScrollPolicy, ["auto", "off", "on"]);
    this.id = attributes.id || "";
    this.passwordChar = attributes.passwordChar || "*";
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.border = null;
    this.extras = null;
    this.margin = null;
  }
}
class Pattern extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "pattern", true);
    this.id = attributes.id || "";
    this.type = (0,utils.getStringOption)(attributes.type, ["crossHatch", "crossDiagonal", "diagonalLeft", "diagonalRight", "horizontal", "vertical"]);
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.color = null;
    this.extras = null;
  }
  [symbol_utils.$toStyle](startColor) {
    startColor = startColor ? startColor[symbol_utils.$toStyle]() : "#FFFFFF";
    const endColor = this.color ? this.color[symbol_utils.$toStyle]() : "#000000";
    const width = 5;
    const cmd = "repeating-linear-gradient";
    const colors = `${startColor},${startColor} ${width}px,${endColor} ${width}px,${endColor} ${2 * width}px`;
    switch (this.type) {
      case "crossHatch":
        return `${cmd}(to top,${colors}) ${cmd}(to right,${colors})`;
      case "crossDiagonal":
        return `${cmd}(45deg,${colors}) ${cmd}(-45deg,${colors})`;
      case "diagonalLeft":
        return `${cmd}(45deg,${colors})`;
      case "diagonalRight":
        return `${cmd}(-45deg,${colors})`;
      case "horizontal":
        return `${cmd}(to top,${colors})`;
      case "vertical":
        return `${cmd}(to right,${colors})`;
    }
    return "";
  }
}
class Picture extends xfa_object.StringObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "picture");
    this.id = attributes.id || "";
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
  }
}
class Proto extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "proto", true);
    this.appearanceFilter = new xfa_object.XFAObjectArray();
    this.arc = new xfa_object.XFAObjectArray();
    this.area = new xfa_object.XFAObjectArray();
    this.assist = new xfa_object.XFAObjectArray();
    this.barcode = new xfa_object.XFAObjectArray();
    this.bindItems = new xfa_object.XFAObjectArray();
    this.bookend = new xfa_object.XFAObjectArray();
    this.boolean = new xfa_object.XFAObjectArray();
    this.border = new xfa_object.XFAObjectArray();
    this.break = new xfa_object.XFAObjectArray();
    this.breakAfter = new xfa_object.XFAObjectArray();
    this.breakBefore = new xfa_object.XFAObjectArray();
    this.button = new xfa_object.XFAObjectArray();
    this.calculate = new xfa_object.XFAObjectArray();
    this.caption = new xfa_object.XFAObjectArray();
    this.certificate = new xfa_object.XFAObjectArray();
    this.certificates = new xfa_object.XFAObjectArray();
    this.checkButton = new xfa_object.XFAObjectArray();
    this.choiceList = new xfa_object.XFAObjectArray();
    this.color = new xfa_object.XFAObjectArray();
    this.comb = new xfa_object.XFAObjectArray();
    this.connect = new xfa_object.XFAObjectArray();
    this.contentArea = new xfa_object.XFAObjectArray();
    this.corner = new xfa_object.XFAObjectArray();
    this.date = new xfa_object.XFAObjectArray();
    this.dateTime = new xfa_object.XFAObjectArray();
    this.dateTimeEdit = new xfa_object.XFAObjectArray();
    this.decimal = new xfa_object.XFAObjectArray();
    this.defaultUi = new xfa_object.XFAObjectArray();
    this.desc = new xfa_object.XFAObjectArray();
    this.digestMethod = new xfa_object.XFAObjectArray();
    this.digestMethods = new xfa_object.XFAObjectArray();
    this.draw = new xfa_object.XFAObjectArray();
    this.edge = new xfa_object.XFAObjectArray();
    this.encoding = new xfa_object.XFAObjectArray();
    this.encodings = new xfa_object.XFAObjectArray();
    this.encrypt = new xfa_object.XFAObjectArray();
    this.encryptData = new xfa_object.XFAObjectArray();
    this.encryption = new xfa_object.XFAObjectArray();
    this.encryptionMethod = new xfa_object.XFAObjectArray();
    this.encryptionMethods = new xfa_object.XFAObjectArray();
    this.event = new xfa_object.XFAObjectArray();
    this.exData = new xfa_object.XFAObjectArray();
    this.exObject = new xfa_object.XFAObjectArray();
    this.exclGroup = new xfa_object.XFAObjectArray();
    this.execute = new xfa_object.XFAObjectArray();
    this.extras = new xfa_object.XFAObjectArray();
    this.field = new xfa_object.XFAObjectArray();
    this.fill = new xfa_object.XFAObjectArray();
    this.filter = new xfa_object.XFAObjectArray();
    this.float = new xfa_object.XFAObjectArray();
    this.font = new xfa_object.XFAObjectArray();
    this.format = new xfa_object.XFAObjectArray();
    this.handler = new xfa_object.XFAObjectArray();
    this.hyphenation = new xfa_object.XFAObjectArray();
    this.image = new xfa_object.XFAObjectArray();
    this.imageEdit = new xfa_object.XFAObjectArray();
    this.integer = new xfa_object.XFAObjectArray();
    this.issuers = new xfa_object.XFAObjectArray();
    this.items = new xfa_object.XFAObjectArray();
    this.keep = new xfa_object.XFAObjectArray();
    this.keyUsage = new xfa_object.XFAObjectArray();
    this.line = new xfa_object.XFAObjectArray();
    this.linear = new xfa_object.XFAObjectArray();
    this.lockDocument = new xfa_object.XFAObjectArray();
    this.manifest = new xfa_object.XFAObjectArray();
    this.margin = new xfa_object.XFAObjectArray();
    this.mdp = new xfa_object.XFAObjectArray();
    this.medium = new xfa_object.XFAObjectArray();
    this.message = new xfa_object.XFAObjectArray();
    this.numericEdit = new xfa_object.XFAObjectArray();
    this.occur = new xfa_object.XFAObjectArray();
    this.oid = new xfa_object.XFAObjectArray();
    this.oids = new xfa_object.XFAObjectArray();
    this.overflow = new xfa_object.XFAObjectArray();
    this.pageArea = new xfa_object.XFAObjectArray();
    this.pageSet = new xfa_object.XFAObjectArray();
    this.para = new xfa_object.XFAObjectArray();
    this.passwordEdit = new xfa_object.XFAObjectArray();
    this.pattern = new xfa_object.XFAObjectArray();
    this.picture = new xfa_object.XFAObjectArray();
    this.radial = new xfa_object.XFAObjectArray();
    this.reason = new xfa_object.XFAObjectArray();
    this.reasons = new xfa_object.XFAObjectArray();
    this.rectangle = new xfa_object.XFAObjectArray();
    this.ref = new xfa_object.XFAObjectArray();
    this.script = new xfa_object.XFAObjectArray();
    this.setProperty = new xfa_object.XFAObjectArray();
    this.signData = new xfa_object.XFAObjectArray();
    this.signature = new xfa_object.XFAObjectArray();
    this.signing = new xfa_object.XFAObjectArray();
    this.solid = new xfa_object.XFAObjectArray();
    this.speak = new xfa_object.XFAObjectArray();
    this.stipple = new xfa_object.XFAObjectArray();
    this.subform = new xfa_object.XFAObjectArray();
    this.subformSet = new xfa_object.XFAObjectArray();
    this.subjectDN = new xfa_object.XFAObjectArray();
    this.subjectDNs = new xfa_object.XFAObjectArray();
    this.submit = new xfa_object.XFAObjectArray();
    this.text = new xfa_object.XFAObjectArray();
    this.textEdit = new xfa_object.XFAObjectArray();
    this.time = new xfa_object.XFAObjectArray();
    this.timeStamp = new xfa_object.XFAObjectArray();
    this.toolTip = new xfa_object.XFAObjectArray();
    this.traversal = new xfa_object.XFAObjectArray();
    this.traverse = new xfa_object.XFAObjectArray();
    this.ui = new xfa_object.XFAObjectArray();
    this.validate = new xfa_object.XFAObjectArray();
    this.value = new xfa_object.XFAObjectArray();
    this.variables = new xfa_object.XFAObjectArray();
  }
}
class Radial extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "radial", true);
    this.id = attributes.id || "";
    this.type = (0,utils.getStringOption)(attributes.type, ["toEdge", "toCenter"]);
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.color = null;
    this.extras = null;
  }
  [symbol_utils.$toStyle](startColor) {
    startColor = startColor ? startColor[symbol_utils.$toStyle]() : "#FFFFFF";
    const endColor = this.color ? this.color[symbol_utils.$toStyle]() : "#000000";
    const colors = this.type === "toEdge" ? `${startColor},${endColor}` : `${endColor},${startColor}`;
    return `radial-gradient(circle at center, ${colors})`;
  }
}
class Reason extends xfa_object.StringObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "reason");
    this.id = attributes.id || "";
    this.name = attributes.name || "";
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
  }
}
class Reasons extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "reasons", true);
    this.id = attributes.id || "";
    this.type = (0,utils.getStringOption)(attributes.type, ["optional", "required"]);
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.reason = new xfa_object.XFAObjectArray();
  }
}
class Rectangle extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "rectangle", true);
    this.hand = (0,utils.getStringOption)(attributes.hand, ["even", "left", "right"]);
    this.id = attributes.id || "";
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.corner = new xfa_object.XFAObjectArray(4);
    this.edge = new xfa_object.XFAObjectArray(4);
    this.fill = null;
  }
  [symbol_utils.$toHTML]() {
    const edge = this.edge.children.length ? this.edge.children[0] : new Edge({});
    const edgeStyle = edge[symbol_utils.$toStyle]();
    const style = Object.create(null);
    if (this.fill?.presence === "visible") {
      Object.assign(style, this.fill[symbol_utils.$toStyle]());
    } else {
      style.fill = "transparent";
    }
    style.strokeWidth = (0,html_utils.measureToString)(edge.presence === "visible" ? edge.thickness : 0);
    style.stroke = edgeStyle.color;
    const corner = this.corner.children.length ? this.corner.children[0] : new Corner({});
    const cornerStyle = corner[symbol_utils.$toStyle]();
    const rect = {
      name: "rect",
      attributes: {
        xmlns: util.SVG_NS,
        width: "100%",
        height: "100%",
        x: 0,
        y: 0,
        rx: cornerStyle.radius,
        ry: cornerStyle.radius,
        style
      }
    };
    const svg = {
      name: "svg",
      children: [rect],
      attributes: {
        xmlns: util.SVG_NS,
        style: {
          overflow: "visible"
        },
        width: "100%",
        height: "100%"
      }
    };
    const parent = this[symbol_utils.$getParent]()[symbol_utils.$getParent]();
    if (hasMargin(parent)) {
      return utils.HTMLResult.success({
        name: "div",
        attributes: {
          style: {
            display: "inline",
            width: "100%",
            height: "100%"
          }
        },
        children: [svg]
      });
    }
    svg.attributes.style.position = "absolute";
    return utils.HTMLResult.success(svg);
  }
}
class RefElement extends xfa_object.StringObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "ref");
    this.id = attributes.id || "";
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
  }
}
class Script extends xfa_object.StringObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "script");
    this.binding = attributes.binding || "";
    this.contentType = attributes.contentType || "";
    this.id = attributes.id || "";
    this.name = attributes.name || "";
    this.runAt = (0,utils.getStringOption)(attributes.runAt, ["client", "both", "server"]);
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
  }
}
class SetProperty extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "setProperty");
    this.connection = attributes.connection || "";
    this.ref = attributes.ref || "";
    this.target = attributes.target || "";
  }
}
class SignData extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "signData", true);
    this.id = attributes.id || "";
    this.operation = (0,utils.getStringOption)(attributes.operation, ["sign", "clear", "verify"]);
    this.ref = attributes.ref || "";
    this.target = attributes.target || "";
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.filter = null;
    this.manifest = null;
  }
}
class Signature extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "signature", true);
    this.id = attributes.id || "";
    this.type = (0,utils.getStringOption)(attributes.type, ["PDF1.3", "PDF1.6"]);
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.border = null;
    this.extras = null;
    this.filter = null;
    this.manifest = null;
    this.margin = null;
  }
}
class Signing extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "signing", true);
    this.id = attributes.id || "";
    this.type = (0,utils.getStringOption)(attributes.type, ["optional", "required"]);
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.certificate = new xfa_object.XFAObjectArray();
  }
}
class Solid extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "solid", true);
    this.id = attributes.id || "";
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.extras = null;
  }
  [symbol_utils.$toStyle](startColor) {
    return startColor ? startColor[symbol_utils.$toStyle]() : "#FFFFFF";
  }
}
class Speak extends xfa_object.StringObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "speak");
    this.disable = (0,utils.getInteger)({
      data: attributes.disable,
      defaultValue: 0,
      validate: x => x === 1
    });
    this.id = attributes.id || "";
    this.priority = (0,utils.getStringOption)(attributes.priority, ["custom", "caption", "name", "toolTip"]);
    this.rid = attributes.rid || "";
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
  }
}
class Stipple extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "stipple", true);
    this.id = attributes.id || "";
    this.rate = (0,utils.getInteger)({
      data: attributes.rate,
      defaultValue: 50,
      validate: x => x >= 0 && x <= 100
    });
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.color = null;
    this.extras = null;
  }
  [symbol_utils.$toStyle](bgColor) {
    const alpha = this.rate / 100;
    return util.Util.makeHexColor(Math.round(bgColor.value.r * (1 - alpha) + this.value.r * alpha), Math.round(bgColor.value.g * (1 - alpha) + this.value.g * alpha), Math.round(bgColor.value.b * (1 - alpha) + this.value.b * alpha));
  }
}
class Subform extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "subform", true);
    this.access = (0,utils.getStringOption)(attributes.access, ["open", "nonInteractive", "protected", "readOnly"]);
    this.allowMacro = (0,utils.getInteger)({
      data: attributes.allowMacro,
      defaultValue: 0,
      validate: x => x === 1
    });
    this.anchorType = (0,utils.getStringOption)(attributes.anchorType, ["topLeft", "bottomCenter", "bottomLeft", "bottomRight", "middleCenter", "middleLeft", "middleRight", "topCenter", "topRight"]);
    this.colSpan = (0,utils.getInteger)({
      data: attributes.colSpan,
      defaultValue: 1,
      validate: n => n >= 1 || n === -1
    });
    this.columnWidths = (attributes.columnWidths || "").trim().split(/\s+/).map(x => x === "-1" ? -1 : (0,utils.getMeasurement)(x));
    this.h = attributes.h ? (0,utils.getMeasurement)(attributes.h) : "";
    this.hAlign = (0,utils.getStringOption)(attributes.hAlign, ["left", "center", "justify", "justifyAll", "radix", "right"]);
    this.id = attributes.id || "";
    this.layout = (0,utils.getStringOption)(attributes.layout, ["position", "lr-tb", "rl-row", "rl-tb", "row", "table", "tb"]);
    this.locale = attributes.locale || "";
    this.maxH = (0,utils.getMeasurement)(attributes.maxH, "0pt");
    this.maxW = (0,utils.getMeasurement)(attributes.maxW, "0pt");
    this.mergeMode = (0,utils.getStringOption)(attributes.mergeMode, ["consumeData", "matchTemplate"]);
    this.minH = (0,utils.getMeasurement)(attributes.minH, "0pt");
    this.minW = (0,utils.getMeasurement)(attributes.minW, "0pt");
    this.name = attributes.name || "";
    this.presence = (0,utils.getStringOption)(attributes.presence, ["visible", "hidden", "inactive", "invisible"]);
    this.relevant = (0,utils.getRelevant)(attributes.relevant);
    this.restoreState = (0,utils.getStringOption)(attributes.restoreState, ["manual", "auto"]);
    this.scope = (0,utils.getStringOption)(attributes.scope, ["name", "none"]);
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.w = attributes.w ? (0,utils.getMeasurement)(attributes.w) : "";
    this.x = (0,utils.getMeasurement)(attributes.x, "0pt");
    this.y = (0,utils.getMeasurement)(attributes.y, "0pt");
    this.assist = null;
    this.bind = null;
    this.bookend = null;
    this.border = null;
    this.break = null;
    this.calculate = null;
    this.desc = null;
    this.extras = null;
    this.keep = null;
    this.margin = null;
    this.occur = null;
    this.overflow = null;
    this.pageSet = null;
    this.para = null;
    this.traversal = null;
    this.validate = null;
    this.variables = null;
    this.area = new xfa_object.XFAObjectArray();
    this.breakAfter = new xfa_object.XFAObjectArray();
    this.breakBefore = new xfa_object.XFAObjectArray();
    this.connect = new xfa_object.XFAObjectArray();
    this.draw = new xfa_object.XFAObjectArray();
    this.event = new xfa_object.XFAObjectArray();
    this.exObject = new xfa_object.XFAObjectArray();
    this.exclGroup = new xfa_object.XFAObjectArray();
    this.field = new xfa_object.XFAObjectArray();
    this.proto = new xfa_object.XFAObjectArray();
    this.setProperty = new xfa_object.XFAObjectArray();
    this.subform = new xfa_object.XFAObjectArray();
    this.subformSet = new xfa_object.XFAObjectArray();
  }
  [symbol_utils.$getSubformParent]() {
    const parent = this[symbol_utils.$getParent]();
    return parent instanceof SubformSet ? parent[symbol_utils.$getSubformParent]() : parent;
  }
  [symbol_utils.$isBindable]() {
    return true;
  }
  [symbol_utils.$isThereMoreWidth]() {
    return this.layout.endsWith("-tb") && this[symbol_utils.$extra].attempt === 0 && this[symbol_utils.$extra].numberInLine > 0 || this[symbol_utils.$getParent]()[symbol_utils.$isThereMoreWidth]();
  }
  *[symbol_utils.$getContainedChildren]() {
    yield* getContainedChildren(this);
  }
  [symbol_utils.$flushHTML]() {
    return flushHTML(this);
  }
  [symbol_utils.$addHTML](html, bbox) {
    addHTML(this, html, bbox);
  }
  [symbol_utils.$getAvailableSpace]() {
    return getAvailableSpace(this);
  }
  [symbol_utils.$isSplittable]() {
    const parent = this[symbol_utils.$getSubformParent]();
    if (!parent[symbol_utils.$isSplittable]()) {
      return false;
    }
    if (this[symbol_utils.$extra]._isSplittable !== undefined) {
      return this[symbol_utils.$extra]._isSplittable;
    }
    if (this.layout === "position" || this.layout.includes("row")) {
      this[symbol_utils.$extra]._isSplittable = false;
      return false;
    }
    if (this.keep && this.keep.intact !== "none") {
      this[symbol_utils.$extra]._isSplittable = false;
      return false;
    }
    if (parent.layout?.endsWith("-tb") && parent[symbol_utils.$extra].numberInLine !== 0) {
      return false;
    }
    this[symbol_utils.$extra]._isSplittable = true;
    return true;
  }
  [symbol_utils.$toHTML](availableSpace) {
    setTabIndex(this);
    if (this.break) {
      if (this.break.after !== "auto" || this.break.afterTarget !== "") {
        const node = new BreakAfter({
          targetType: this.break.after,
          target: this.break.afterTarget,
          startNew: this.break.startNew.toString()
        });
        node[symbol_utils.$globalData] = this[symbol_utils.$globalData];
        this[symbol_utils.$appendChild](node);
        this.breakAfter.push(node);
      }
      if (this.break.before !== "auto" || this.break.beforeTarget !== "") {
        const node = new BreakBefore({
          targetType: this.break.before,
          target: this.break.beforeTarget,
          startNew: this.break.startNew.toString()
        });
        node[symbol_utils.$globalData] = this[symbol_utils.$globalData];
        this[symbol_utils.$appendChild](node);
        this.breakBefore.push(node);
      }
      if (this.break.overflowTarget !== "") {
        const node = new Overflow({
          target: this.break.overflowTarget,
          leader: this.break.overflowLeader,
          trailer: this.break.overflowTrailer
        });
        node[symbol_utils.$globalData] = this[symbol_utils.$globalData];
        this[symbol_utils.$appendChild](node);
        this.overflow.push(node);
      }
      this[symbol_utils.$removeChild](this.break);
      this.break = null;
    }
    if (this.presence === "hidden" || this.presence === "inactive") {
      return utils.HTMLResult.EMPTY;
    }
    if (this.breakBefore.children.length > 1 || this.breakAfter.children.length > 1) {
      (0,util.warn)("XFA - Several breakBefore or breakAfter in subforms: please file a bug.");
    }
    if (this.breakBefore.children.length >= 1) {
      const breakBefore = this.breakBefore.children[0];
      if (handleBreak(breakBefore)) {
        return utils.HTMLResult.breakNode(breakBefore);
      }
    }
    if (this[symbol_utils.$extra]?.afterBreakAfter) {
      return utils.HTMLResult.EMPTY;
    }
    ;(0,html_utils.fixDimensions)(this);
    const children = [];
    const attributes = {
      id: this[symbol_utils.$uid],
      class: []
    };
    (0,html_utils.setAccess)(this, attributes.class);
    this[symbol_utils.$extra] ||= Object.create(null);
    Object.assign(this[symbol_utils.$extra], {
      children,
      line: null,
      attributes,
      attempt: 0,
      numberInLine: 0,
      availableSpace: {
        width: Math.min(this.w || Infinity, availableSpace.width),
        height: Math.min(this.h || Infinity, availableSpace.height)
      },
      width: 0,
      height: 0,
      prevHeight: 0,
      currentWidth: 0
    });
    const root = this[symbol_utils.$getTemplateRoot]();
    const savedNoLayoutFailure = root[symbol_utils.$extra].noLayoutFailure;
    const isSplittable = this[symbol_utils.$isSplittable]();
    if (!isSplittable) {
      setFirstUnsplittable(this);
    }
    if (!checkDimensions(this, availableSpace)) {
      return utils.HTMLResult.FAILURE;
    }
    const filter = new Set(["area", "draw", "exclGroup", "field", "subform", "subformSet"]);
    if (this.layout.includes("row")) {
      const columnWidths = this[symbol_utils.$getSubformParent]().columnWidths;
      if (Array.isArray(columnWidths) && columnWidths.length > 0) {
        this[symbol_utils.$extra].columnWidths = columnWidths;
        this[symbol_utils.$extra].currentColumn = 0;
      }
    }
    const style = (0,html_utils.toStyle)(this, "anchorType", "dimensions", "position", "presence", "border", "margin", "hAlign");
    const classNames = ["xfaSubform"];
    const cl = (0,html_utils.layoutClass)(this);
    if (cl) {
      classNames.push(cl);
    }
    attributes.style = style;
    attributes.class = classNames;
    if (this.name) {
      attributes.xfaName = this.name;
    }
    if (this.overflow) {
      const overflowExtra = this.overflow[symbol_utils.$getExtra]();
      if (overflowExtra.addLeader) {
        overflowExtra.addLeader = false;
        handleOverflow(this, overflowExtra.leader, availableSpace);
      }
    }
    this[symbol_utils.$pushPara]();
    const isLrTb = this.layout === "lr-tb" || this.layout === "rl-tb";
    const maxRun = isLrTb ? MAX_ATTEMPTS_FOR_LRTB_LAYOUT : 1;
    for (; this[symbol_utils.$extra].attempt < maxRun; this[symbol_utils.$extra].attempt++) {
      if (isLrTb && this[symbol_utils.$extra].attempt === MAX_ATTEMPTS_FOR_LRTB_LAYOUT - 1) {
        this[symbol_utils.$extra].numberInLine = 0;
      }
      const result = this[symbol_utils.$childrenToHTML]({
        filter,
        include: true
      });
      if (result.success) {
        break;
      }
      if (result.isBreak()) {
        this[symbol_utils.$popPara]();
        return result;
      }
      if (isLrTb && this[symbol_utils.$extra].attempt === 0 && this[symbol_utils.$extra].numberInLine === 0 && !root[symbol_utils.$extra].noLayoutFailure) {
        this[symbol_utils.$extra].attempt = maxRun;
        break;
      }
    }
    this[symbol_utils.$popPara]();
    if (!isSplittable) {
      unsetFirstUnsplittable(this);
    }
    root[symbol_utils.$extra].noLayoutFailure = savedNoLayoutFailure;
    if (this[symbol_utils.$extra].attempt === maxRun) {
      if (this.overflow) {
        this[symbol_utils.$getTemplateRoot]()[symbol_utils.$extra].overflowNode = this.overflow;
      }
      if (!isSplittable) {
        delete this[symbol_utils.$extra];
      }
      return utils.HTMLResult.FAILURE;
    }
    if (this.overflow) {
      const overflowExtra = this.overflow[symbol_utils.$getExtra]();
      if (overflowExtra.addTrailer) {
        overflowExtra.addTrailer = false;
        handleOverflow(this, overflowExtra.trailer, availableSpace);
      }
    }
    let marginH = 0;
    let marginV = 0;
    if (this.margin) {
      marginH = this.margin.leftInset + this.margin.rightInset;
      marginV = this.margin.topInset + this.margin.bottomInset;
    }
    const width = Math.max(this[symbol_utils.$extra].width + marginH, this.w || 0);
    const height = Math.max(this[symbol_utils.$extra].height + marginV, this.h || 0);
    const bbox = [this.x, this.y, width, height];
    if (this.w === "") {
      style.width = (0,html_utils.measureToString)(width);
    }
    if (this.h === "") {
      style.height = (0,html_utils.measureToString)(height);
    }
    if ((style.width === "0px" || style.height === "0px") && children.length === 0) {
      return utils.HTMLResult.EMPTY;
    }
    const html = {
      name: "div",
      attributes,
      children
    };
    applyAssist(this, attributes);
    const result = utils.HTMLResult.success((0,html_utils.createWrapper)(this, html), bbox);
    if (this.breakAfter.children.length >= 1) {
      const breakAfter = this.breakAfter.children[0];
      if (handleBreak(breakAfter)) {
        this[symbol_utils.$extra].afterBreakAfter = result;
        return utils.HTMLResult.breakNode(breakAfter);
      }
    }
    delete this[symbol_utils.$extra];
    return result;
  }
}
class SubformSet extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "subformSet", true);
    this.id = attributes.id || "";
    this.name = attributes.name || "";
    this.relation = (0,utils.getStringOption)(attributes.relation, ["ordered", "choice", "unordered"]);
    this.relevant = (0,utils.getRelevant)(attributes.relevant);
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.bookend = null;
    this.break = null;
    this.desc = null;
    this.extras = null;
    this.occur = null;
    this.overflow = null;
    this.breakAfter = new xfa_object.XFAObjectArray();
    this.breakBefore = new xfa_object.XFAObjectArray();
    this.subform = new xfa_object.XFAObjectArray();
    this.subformSet = new xfa_object.XFAObjectArray();
  }
  *[symbol_utils.$getContainedChildren]() {
    yield* getContainedChildren(this);
  }
  [symbol_utils.$getSubformParent]() {
    let parent = this[symbol_utils.$getParent]();
    while (!(parent instanceof Subform)) {
      parent = parent[symbol_utils.$getParent]();
    }
    return parent;
  }
  [symbol_utils.$isBindable]() {
    return true;
  }
}
class SubjectDN extends xfa_object.ContentObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "subjectDN");
    this.delimiter = attributes.delimiter || ",";
    this.id = attributes.id || "";
    this.name = attributes.name || "";
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
  }
  [symbol_utils.$finalize]() {
    this[symbol_utils.$content] = new Map(this[symbol_utils.$content].split(this.delimiter).map(kv => {
      kv = kv.split("=", 2);
      kv[0] = kv[0].trim();
      return kv;
    }));
  }
}
class SubjectDNs extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "subjectDNs", true);
    this.id = attributes.id || "";
    this.type = (0,utils.getStringOption)(attributes.type, ["optional", "required"]);
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.subjectDN = new xfa_object.XFAObjectArray();
  }
}
class Submit extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "submit", true);
    this.embedPDF = (0,utils.getInteger)({
      data: attributes.embedPDF,
      defaultValue: 0,
      validate: x => x === 1
    });
    this.format = (0,utils.getStringOption)(attributes.format, ["xdp", "formdata", "pdf", "urlencoded", "xfd", "xml"]);
    this.id = attributes.id || "";
    this.target = attributes.target || "";
    this.textEncoding = (0,utils.getKeyword)({
      data: attributes.textEncoding ? attributes.textEncoding.toLowerCase() : "",
      defaultValue: "",
      validate: k => ["utf-8", "big-five", "fontspecific", "gbk", "gb-18030", "gb-2312", "ksc-5601", "none", "shift-jis", "ucs-2", "utf-16"].includes(k) || k.match(/iso-8859-\d{2}/)
    });
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.xdpContent = attributes.xdpContent || "";
    this.encrypt = null;
    this.encryptData = new xfa_object.XFAObjectArray();
    this.signData = new xfa_object.XFAObjectArray();
  }
}
class Template extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "template", true);
    this.baseProfile = (0,utils.getStringOption)(attributes.baseProfile, ["full", "interactiveForms"]);
    this.extras = null;
    this.subform = new xfa_object.XFAObjectArray();
  }
  [symbol_utils.$finalize]() {
    if (this.subform.children.length === 0) {
      (0,util.warn)("XFA - No subforms in template node.");
    }
    if (this.subform.children.length >= 2) {
      (0,util.warn)("XFA - Several subforms in template node: please file a bug.");
    }
    this[symbol_utils.$tabIndex] = DEFAULT_TAB_INDEX;
  }
  [symbol_utils.$isSplittable]() {
    return true;
  }
  [symbol_utils.$searchNode](expr, container) {
    if (expr.startsWith("#")) {
      return [this[symbol_utils.$ids].get(expr.slice(1))];
    }
    return (0,som.searchNode)(this, container, expr, true, true);
  }
  *[symbol_utils.$toPages]() {
    if (!this.subform.children.length) {
      return utils.HTMLResult.success({
        name: "div",
        children: []
      });
    }
    this[symbol_utils.$extra] = {
      overflowNode: null,
      firstUnsplittable: null,
      currentContentArea: null,
      currentPageArea: null,
      noLayoutFailure: false,
      pageNumber: 1,
      pagePosition: "first",
      oddOrEven: "odd",
      blankOrNotBlank: "nonBlank",
      paraStack: []
    };
    const root = this.subform.children[0];
    root.pageSet[symbol_utils.$cleanPage]();
    const pageAreas = root.pageSet.pageArea.children;
    const mainHtml = {
      name: "div",
      children: []
    };
    let pageArea = null;
    let breakBefore = null;
    let breakBeforeTarget = null;
    if (root.breakBefore.children.length >= 1) {
      breakBefore = root.breakBefore.children[0];
      breakBeforeTarget = breakBefore.target;
    } else if (root.subform.children.length >= 1 && root.subform.children[0].breakBefore.children.length >= 1) {
      breakBefore = root.subform.children[0].breakBefore.children[0];
      breakBeforeTarget = breakBefore.target;
    } else if (root.break?.beforeTarget) {
      breakBefore = root.break;
      breakBeforeTarget = breakBefore.beforeTarget;
    } else if (root.subform.children.length >= 1 && root.subform.children[0].break?.beforeTarget) {
      breakBefore = root.subform.children[0].break;
      breakBeforeTarget = breakBefore.beforeTarget;
    }
    if (breakBefore) {
      const target = this[symbol_utils.$searchNode](breakBeforeTarget, breakBefore[symbol_utils.$getParent]());
      if (target instanceof PageArea) {
        pageArea = target;
        breakBefore[symbol_utils.$extra] = {};
      }
    }
    pageArea ||= pageAreas[0];
    pageArea[symbol_utils.$extra] = {
      numberOfUse: 1
    };
    const pageAreaParent = pageArea[symbol_utils.$getParent]();
    pageAreaParent[symbol_utils.$extra] = {
      numberOfUse: 1,
      pageIndex: pageAreaParent.pageArea.children.indexOf(pageArea),
      pageSetIndex: 0
    };
    let targetPageArea;
    let leader = null;
    let trailer = null;
    let hasSomething = true;
    let hasSomethingCounter = 0;
    let startIndex = 0;
    while (true) {
      if (!hasSomething) {
        mainHtml.children.pop();
        if (++hasSomethingCounter === MAX_EMPTY_PAGES) {
          (0,util.warn)("XFA - Something goes wrong: please file a bug.");
          return mainHtml;
        }
      } else {
        hasSomethingCounter = 0;
      }
      targetPageArea = null;
      this[symbol_utils.$extra].currentPageArea = pageArea;
      const page = pageArea[symbol_utils.$toHTML]().html;
      mainHtml.children.push(page);
      if (leader) {
        this[symbol_utils.$extra].noLayoutFailure = true;
        page.children.push(leader[symbol_utils.$toHTML](pageArea[symbol_utils.$extra].space).html);
        leader = null;
      }
      if (trailer) {
        this[symbol_utils.$extra].noLayoutFailure = true;
        page.children.push(trailer[symbol_utils.$toHTML](pageArea[symbol_utils.$extra].space).html);
        trailer = null;
      }
      const contentAreas = pageArea.contentArea.children;
      const htmlContentAreas = page.children.filter(node => node.attributes.class.includes("xfaContentarea"));
      hasSomething = false;
      this[symbol_utils.$extra].firstUnsplittable = null;
      this[symbol_utils.$extra].noLayoutFailure = false;
      const flush = index => {
        const html = root[symbol_utils.$flushHTML]();
        if (html) {
          hasSomething ||= html.children?.length > 0;
          htmlContentAreas[index].children.push(html);
        }
      };
      for (let i = startIndex, ii = contentAreas.length; i < ii; i++) {
        const contentArea = this[symbol_utils.$extra].currentContentArea = contentAreas[i];
        const space = {
          width: contentArea.w,
          height: contentArea.h
        };
        startIndex = 0;
        if (leader) {
          htmlContentAreas[i].children.push(leader[symbol_utils.$toHTML](space).html);
          leader = null;
        }
        if (trailer) {
          htmlContentAreas[i].children.push(trailer[symbol_utils.$toHTML](space).html);
          trailer = null;
        }
        const html = root[symbol_utils.$toHTML](space);
        if (html.success) {
          if (html.html) {
            hasSomething ||= html.html.children?.length > 0;
            htmlContentAreas[i].children.push(html.html);
          } else if (!hasSomething && mainHtml.children.length > 1) {
            mainHtml.children.pop();
          }
          return mainHtml;
        }
        if (html.isBreak()) {
          const node = html.breakNode;
          flush(i);
          if (node.targetType === "auto") {
            continue;
          }
          if (node.leader) {
            leader = this[symbol_utils.$searchNode](node.leader, node[symbol_utils.$getParent]());
            leader = leader ? leader[0] : null;
          }
          if (node.trailer) {
            trailer = this[symbol_utils.$searchNode](node.trailer, node[symbol_utils.$getParent]());
            trailer = trailer ? trailer[0] : null;
          }
          if (node.targetType === "pageArea") {
            targetPageArea = node[symbol_utils.$extra].target;
            i = Infinity;
          } else if (!node[symbol_utils.$extra].target) {
            i = node[symbol_utils.$extra].index;
          } else {
            targetPageArea = node[symbol_utils.$extra].target;
            startIndex = node[symbol_utils.$extra].index + 1;
            i = Infinity;
          }
          continue;
        }
        if (this[symbol_utils.$extra].overflowNode) {
          const node = this[symbol_utils.$extra].overflowNode;
          this[symbol_utils.$extra].overflowNode = null;
          const overflowExtra = node[symbol_utils.$getExtra]();
          const target = overflowExtra.target;
          overflowExtra.addLeader = overflowExtra.leader !== null;
          overflowExtra.addTrailer = overflowExtra.trailer !== null;
          flush(i);
          const currentIndex = i;
          i = Infinity;
          if (target instanceof PageArea) {
            targetPageArea = target;
          } else if (target instanceof ContentArea) {
            const index = contentAreas.indexOf(target);
            if (index !== -1) {
              if (index > currentIndex) {
                i = index - 1;
              } else {
                startIndex = index;
              }
            } else {
              targetPageArea = target[symbol_utils.$getParent]();
              startIndex = targetPageArea.contentArea.children.indexOf(target);
            }
          }
          continue;
        }
        flush(i);
      }
      this[symbol_utils.$extra].pageNumber += 1;
      if (targetPageArea) {
        if (targetPageArea[symbol_utils.$isUsable]()) {
          targetPageArea[symbol_utils.$extra].numberOfUse += 1;
        } else {
          targetPageArea = null;
        }
      }
      pageArea = targetPageArea || pageArea[symbol_utils.$getNextPage]();
      yield null;
    }
  }
}
class Text extends xfa_object.ContentObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "text");
    this.id = attributes.id || "";
    this.maxChars = (0,utils.getInteger)({
      data: attributes.maxChars,
      defaultValue: 0,
      validate: x => x >= 0
    });
    this.name = attributes.name || "";
    this.rid = attributes.rid || "";
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
  }
  [symbol_utils.$acceptWhitespace]() {
    return true;
  }
  [symbol_utils.$onChild](child) {
    if (child[symbol_utils.$namespaceId] === namespaces.NamespaceIds.xhtml.id) {
      this[symbol_utils.$content] = child;
      return true;
    }
    ;(0,util.warn)(`XFA - Invalid content in Text: ${child[symbol_utils.$nodeName]}.`);
    return false;
  }
  [symbol_utils.$onText](str) {
    if (this[symbol_utils.$content] instanceof xfa_object.XFAObject) {
      return;
    }
    super[symbol_utils.$onText](str);
  }
  [symbol_utils.$finalize]() {
    if (typeof this[symbol_utils.$content] === "string") {
      this[symbol_utils.$content] = this[symbol_utils.$content].replaceAll("\r\n", "\n");
    }
  }
  [symbol_utils.$getExtra]() {
    if (typeof this[symbol_utils.$content] === "string") {
      return this[symbol_utils.$content].split(/[\u2029\u2028\n]/).filter(Boolean).join("\n");
    }
    return this[symbol_utils.$content][symbol_utils.$text]();
  }
  [symbol_utils.$toHTML](availableSpace) {
    if (typeof this[symbol_utils.$content] === "string") {
      const html = valueToHtml(this[symbol_utils.$content]).html;
      if (this[symbol_utils.$content].includes("\u2029")) {
        html.name = "div";
        html.children = [];
        this[symbol_utils.$content].split("\u2029").map(para => para.split(/[\u2028\n]/).flatMap(line => [{
          name: "span",
          value: line
        }, {
          name: "br"
        }])).forEach(lines => {
          html.children.push({
            name: "p",
            children: lines
          });
        });
      } else if (/[\u2028\n]/.test(this[symbol_utils.$content])) {
        html.name = "div";
        html.children = [];
        this[symbol_utils.$content].split(/[\u2028\n]/).forEach(line => {
          html.children.push({
            name: "span",
            value: line
          }, {
            name: "br"
          });
        });
      }
      return utils.HTMLResult.success(html);
    }
    return this[symbol_utils.$content][symbol_utils.$toHTML](availableSpace);
  }
}
class TextEdit extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "textEdit", true);
    this.allowRichText = (0,utils.getInteger)({
      data: attributes.allowRichText,
      defaultValue: 0,
      validate: x => x === 1
    });
    this.hScrollPolicy = (0,utils.getStringOption)(attributes.hScrollPolicy, ["auto", "off", "on"]);
    this.id = attributes.id || "";
    this.multiLine = (0,utils.getInteger)({
      data: attributes.multiLine,
      defaultValue: "",
      validate: x => x === 0 || x === 1
    });
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.vScrollPolicy = (0,utils.getStringOption)(attributes.vScrollPolicy, ["auto", "off", "on"]);
    this.border = null;
    this.comb = null;
    this.extras = null;
    this.margin = null;
  }
  [symbol_utils.$toHTML](availableSpace) {
    const style = (0,html_utils.toStyle)(this, "border", "font", "margin");
    let html;
    const field = this[symbol_utils.$getParent]()[symbol_utils.$getParent]();
    if (this.multiLine === "") {
      this.multiLine = field instanceof Draw ? 1 : 0;
    }
    if (this.multiLine === 1) {
      html = {
        name: "textarea",
        attributes: {
          dataId: field[symbol_utils.$data]?.[symbol_utils.$uid] || field[symbol_utils.$uid],
          fieldId: field[symbol_utils.$uid],
          class: ["xfaTextfield"],
          style,
          "aria-label": ariaLabel(field),
          "aria-required": false
        }
      };
    } else {
      html = {
        name: "input",
        attributes: {
          type: "text",
          dataId: field[symbol_utils.$data]?.[symbol_utils.$uid] || field[symbol_utils.$uid],
          fieldId: field[symbol_utils.$uid],
          class: ["xfaTextfield"],
          style,
          "aria-label": ariaLabel(field),
          "aria-required": false
        }
      };
    }
    if (isRequired(field)) {
      html.attributes["aria-required"] = true;
      html.attributes.required = true;
    }
    return utils.HTMLResult.success({
      name: "label",
      attributes: {
        class: ["xfaLabel"]
      },
      children: [html]
    });
  }
}
class Time extends xfa_object.StringObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "time");
    this.id = attributes.id || "";
    this.name = attributes.name || "";
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
  }
  [symbol_utils.$finalize]() {
    const date = this[symbol_utils.$content].trim();
    this[symbol_utils.$content] = date ? new Date(date) : null;
  }
  [symbol_utils.$toHTML](availableSpace) {
    return valueToHtml(this[symbol_utils.$content] ? this[symbol_utils.$content].toString() : "");
  }
}
class TimeStamp extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "timeStamp");
    this.id = attributes.id || "";
    this.server = attributes.server || "";
    this.type = (0,utils.getStringOption)(attributes.type, ["optional", "required"]);
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
  }
}
class ToolTip extends xfa_object.StringObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "toolTip");
    this.id = attributes.id || "";
    this.rid = attributes.rid || "";
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
  }
}
class Traversal extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "traversal", true);
    this.id = attributes.id || "";
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.extras = null;
    this.traverse = new xfa_object.XFAObjectArray();
  }
}
class Traverse extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "traverse", true);
    this.id = attributes.id || "";
    this.operation = (0,utils.getStringOption)(attributes.operation, ["next", "back", "down", "first", "left", "right", "up"]);
    this.ref = attributes.ref || "";
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.extras = null;
    this.script = null;
  }
  get name() {
    return this.operation;
  }
  [symbol_utils.$isTransparent]() {
    return false;
  }
}
class Ui extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "ui", true);
    this.id = attributes.id || "";
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.extras = null;
    this.picture = null;
    this.barcode = null;
    this.button = null;
    this.checkButton = null;
    this.choiceList = null;
    this.dateTimeEdit = null;
    this.defaultUi = null;
    this.imageEdit = null;
    this.numericEdit = null;
    this.passwordEdit = null;
    this.signature = null;
    this.textEdit = null;
  }
  [symbol_utils.$getExtra]() {
    if (this[symbol_utils.$extra] === undefined) {
      for (const name of Object.getOwnPropertyNames(this)) {
        if (name === "extras" || name === "picture") {
          continue;
        }
        const obj = this[name];
        if (!(obj instanceof xfa_object.XFAObject)) {
          continue;
        }
        this[symbol_utils.$extra] = obj;
        return obj;
      }
      this[symbol_utils.$extra] = null;
    }
    return this[symbol_utils.$extra];
  }
  [symbol_utils.$toHTML](availableSpace) {
    const obj = this[symbol_utils.$getExtra]();
    return obj ? obj[symbol_utils.$toHTML](availableSpace) : utils.HTMLResult.EMPTY;
  }
}
class Validate extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "validate", true);
    this.formatTest = (0,utils.getStringOption)(attributes.formatTest, ["warning", "disabled", "error"]);
    this.id = attributes.id || "";
    this.nullTest = (0,utils.getStringOption)(attributes.nullTest, ["disabled", "error", "warning"]);
    this.scriptTest = (0,utils.getStringOption)(attributes.scriptTest, ["error", "disabled", "warning"]);
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.extras = null;
    this.message = null;
    this.picture = null;
    this.script = null;
  }
}
class Value extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "value", true);
    this.id = attributes.id || "";
    this.override = (0,utils.getInteger)({
      data: attributes.override,
      defaultValue: 0,
      validate: x => x === 1
    });
    this.relevant = (0,utils.getRelevant)(attributes.relevant);
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.arc = null;
    this.boolean = null;
    this.date = null;
    this.dateTime = null;
    this.decimal = null;
    this.exData = null;
    this.float = null;
    this.image = null;
    this.integer = null;
    this.line = null;
    this.rectangle = null;
    this.text = null;
    this.time = null;
  }
  [symbol_utils.$setValue](value) {
    const parent = this[symbol_utils.$getParent]();
    if (parent instanceof Field) {
      if (parent.ui?.imageEdit) {
        if (!this.image) {
          this.image = new Image({});
          this[symbol_utils.$appendChild](this.image);
        }
        this.image[symbol_utils.$content] = value[symbol_utils.$content];
        return;
      }
    }
    const valueName = value[symbol_utils.$nodeName];
    if (this[valueName] !== null) {
      this[valueName][symbol_utils.$content] = value[symbol_utils.$content];
      return;
    }
    for (const name of Object.getOwnPropertyNames(this)) {
      const obj = this[name];
      if (obj instanceof xfa_object.XFAObject) {
        this[name] = null;
        this[symbol_utils.$removeChild](obj);
      }
    }
    this[value[symbol_utils.$nodeName]] = value;
    this[symbol_utils.$appendChild](value);
  }
  [symbol_utils.$text]() {
    if (this.exData) {
      return typeof this.exData[symbol_utils.$content] === "string" ? this.exData[symbol_utils.$content].trim() : this.exData[symbol_utils.$content][symbol_utils.$text]().trim();
    }
    for (const name of Object.getOwnPropertyNames(this)) {
      if (name === "image") {
        continue;
      }
      const obj = this[name];
      if (obj instanceof xfa_object.XFAObject) {
        return (obj[symbol_utils.$content] || "").toString().trim();
      }
    }
    return null;
  }
  [symbol_utils.$toHTML](availableSpace) {
    for (const name of Object.getOwnPropertyNames(this)) {
      const obj = this[name];
      if (!(obj instanceof xfa_object.XFAObject)) {
        continue;
      }
      return obj[symbol_utils.$toHTML](availableSpace);
    }
    return utils.HTMLResult.EMPTY;
  }
}
class Variables extends xfa_object.XFAObject {
  constructor(attributes) {
    super(TEMPLATE_NS_ID, "variables", true);
    this.id = attributes.id || "";
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
    this.boolean = new xfa_object.XFAObjectArray();
    this.date = new xfa_object.XFAObjectArray();
    this.dateTime = new xfa_object.XFAObjectArray();
    this.decimal = new xfa_object.XFAObjectArray();
    this.exData = new xfa_object.XFAObjectArray();
    this.float = new xfa_object.XFAObjectArray();
    this.image = new xfa_object.XFAObjectArray();
    this.integer = new xfa_object.XFAObjectArray();
    this.manifest = new xfa_object.XFAObjectArray();
    this.script = new xfa_object.XFAObjectArray();
    this.text = new xfa_object.XFAObjectArray();
    this.time = new xfa_object.XFAObjectArray();
  }
  [symbol_utils.$isTransparent]() {
    return true;
  }
}
class TemplateNamespace {
  static [namespaces.$buildXFAObject](name, attributes) {
    if (Object.hasOwn(TemplateNamespace, name)) {
      const node = TemplateNamespace[name](attributes);
      node[symbol_utils.$setSetAttributes](attributes);
      return node;
    }
    return undefined;
  }
  static appearanceFilter(attrs) {
    return new AppearanceFilter(attrs);
  }
  static arc(attrs) {
    return new Arc(attrs);
  }
  static area(attrs) {
    return new Area(attrs);
  }
  static assist(attrs) {
    return new Assist(attrs);
  }
  static barcode(attrs) {
    return new Barcode(attrs);
  }
  static bind(attrs) {
    return new Bind(attrs);
  }
  static bindItems(attrs) {
    return new BindItems(attrs);
  }
  static bookend(attrs) {
    return new Bookend(attrs);
  }
  static boolean(attrs) {
    return new BooleanElement(attrs);
  }
  static border(attrs) {
    return new Border(attrs);
  }
  static break(attrs) {
    return new Break(attrs);
  }
  static breakAfter(attrs) {
    return new BreakAfter(attrs);
  }
  static breakBefore(attrs) {
    return new BreakBefore(attrs);
  }
  static button(attrs) {
    return new Button(attrs);
  }
  static calculate(attrs) {
    return new Calculate(attrs);
  }
  static caption(attrs) {
    return new Caption(attrs);
  }
  static certificate(attrs) {
    return new Certificate(attrs);
  }
  static certificates(attrs) {
    return new Certificates(attrs);
  }
  static checkButton(attrs) {
    return new CheckButton(attrs);
  }
  static choiceList(attrs) {
    return new ChoiceList(attrs);
  }
  static color(attrs) {
    return new Color(attrs);
  }
  static comb(attrs) {
    return new Comb(attrs);
  }
  static connect(attrs) {
    return new Connect(attrs);
  }
  static contentArea(attrs) {
    return new ContentArea(attrs);
  }
  static corner(attrs) {
    return new Corner(attrs);
  }
  static date(attrs) {
    return new DateElement(attrs);
  }
  static dateTime(attrs) {
    return new DateTime(attrs);
  }
  static dateTimeEdit(attrs) {
    return new DateTimeEdit(attrs);
  }
  static decimal(attrs) {
    return new Decimal(attrs);
  }
  static defaultUi(attrs) {
    return new DefaultUi(attrs);
  }
  static desc(attrs) {
    return new Desc(attrs);
  }
  static digestMethod(attrs) {
    return new DigestMethod(attrs);
  }
  static digestMethods(attrs) {
    return new DigestMethods(attrs);
  }
  static draw(attrs) {
    return new Draw(attrs);
  }
  static edge(attrs) {
    return new Edge(attrs);
  }
  static encoding(attrs) {
    return new Encoding(attrs);
  }
  static encodings(attrs) {
    return new Encodings(attrs);
  }
  static encrypt(attrs) {
    return new Encrypt(attrs);
  }
  static encryptData(attrs) {
    return new EncryptData(attrs);
  }
  static encryption(attrs) {
    return new Encryption(attrs);
  }
  static encryptionMethod(attrs) {
    return new EncryptionMethod(attrs);
  }
  static encryptionMethods(attrs) {
    return new EncryptionMethods(attrs);
  }
  static event(attrs) {
    return new Event(attrs);
  }
  static exData(attrs) {
    return new ExData(attrs);
  }
  static exObject(attrs) {
    return new ExObject(attrs);
  }
  static exclGroup(attrs) {
    return new ExclGroup(attrs);
  }
  static execute(attrs) {
    return new Execute(attrs);
  }
  static extras(attrs) {
    return new Extras(attrs);
  }
  static field(attrs) {
    return new Field(attrs);
  }
  static fill(attrs) {
    return new Fill(attrs);
  }
  static filter(attrs) {
    return new Filter(attrs);
  }
  static float(attrs) {
    return new Float(attrs);
  }
  static font(attrs) {
    return new Font(attrs);
  }
  static format(attrs) {
    return new Format(attrs);
  }
  static handler(attrs) {
    return new Handler(attrs);
  }
  static hyphenation(attrs) {
    return new Hyphenation(attrs);
  }
  static image(attrs) {
    return new Image(attrs);
  }
  static imageEdit(attrs) {
    return new ImageEdit(attrs);
  }
  static integer(attrs) {
    return new Integer(attrs);
  }
  static issuers(attrs) {
    return new Issuers(attrs);
  }
  static items(attrs) {
    return new Items(attrs);
  }
  static keep(attrs) {
    return new Keep(attrs);
  }
  static keyUsage(attrs) {
    return new KeyUsage(attrs);
  }
  static line(attrs) {
    return new Line(attrs);
  }
  static linear(attrs) {
    return new Linear(attrs);
  }
  static lockDocument(attrs) {
    return new LockDocument(attrs);
  }
  static manifest(attrs) {
    return new Manifest(attrs);
  }
  static margin(attrs) {
    return new Margin(attrs);
  }
  static mdp(attrs) {
    return new Mdp(attrs);
  }
  static medium(attrs) {
    return new Medium(attrs);
  }
  static message(attrs) {
    return new Message(attrs);
  }
  static numericEdit(attrs) {
    return new NumericEdit(attrs);
  }
  static occur(attrs) {
    return new Occur(attrs);
  }
  static oid(attrs) {
    return new Oid(attrs);
  }
  static oids(attrs) {
    return new Oids(attrs);
  }
  static overflow(attrs) {
    return new Overflow(attrs);
  }
  static pageArea(attrs) {
    return new PageArea(attrs);
  }
  static pageSet(attrs) {
    return new PageSet(attrs);
  }
  static para(attrs) {
    return new Para(attrs);
  }
  static passwordEdit(attrs) {
    return new PasswordEdit(attrs);
  }
  static pattern(attrs) {
    return new Pattern(attrs);
  }
  static picture(attrs) {
    return new Picture(attrs);
  }
  static proto(attrs) {
    return new Proto(attrs);
  }
  static radial(attrs) {
    return new Radial(attrs);
  }
  static reason(attrs) {
    return new Reason(attrs);
  }
  static reasons(attrs) {
    return new Reasons(attrs);
  }
  static rectangle(attrs) {
    return new Rectangle(attrs);
  }
  static ref(attrs) {
    return new RefElement(attrs);
  }
  static script(attrs) {
    return new Script(attrs);
  }
  static setProperty(attrs) {
    return new SetProperty(attrs);
  }
  static signData(attrs) {
    return new SignData(attrs);
  }
  static signature(attrs) {
    return new Signature(attrs);
  }
  static signing(attrs) {
    return new Signing(attrs);
  }
  static solid(attrs) {
    return new Solid(attrs);
  }
  static speak(attrs) {
    return new Speak(attrs);
  }
  static stipple(attrs) {
    return new Stipple(attrs);
  }
  static subform(attrs) {
    return new Subform(attrs);
  }
  static subformSet(attrs) {
    return new SubformSet(attrs);
  }
  static subjectDN(attrs) {
    return new SubjectDN(attrs);
  }
  static subjectDNs(attrs) {
    return new SubjectDNs(attrs);
  }
  static submit(attrs) {
    return new Submit(attrs);
  }
  static template(attrs) {
    return new Template(attrs);
  }
  static text(attrs) {
    return new Text(attrs);
  }
  static textEdit(attrs) {
    return new TextEdit(attrs);
  }
  static time(attrs) {
    return new Time(attrs);
  }
  static timeStamp(attrs) {
    return new TimeStamp(attrs);
  }
  static toolTip(attrs) {
    return new ToolTip(attrs);
  }
  static traversal(attrs) {
    return new Traversal(attrs);
  }
  static traverse(attrs) {
    return new Traverse(attrs);
  }
  static ui(attrs) {
    return new Ui(attrs);
  }
  static validate(attrs) {
    return new Validate(attrs);
  }
  static value(attrs) {
    return new Value(attrs);
  }
  static variables(attrs) {
    return new Variables(attrs);
  }
}

;// ./src/core/xfa/bind.js






const NS_DATASETS = namespaces.NamespaceIds.datasets.id;
function createText(content) {
  const node = new Text({});
  node[symbol_utils.$content] = content;
  return node;
}
class Binder {
  constructor(root) {
    this.root = root;
    this.datasets = root.datasets;
    this.data = root.datasets?.data || new xfa_object.XmlObject(NS_DATASETS, "data");
    this.emptyMerge = this.data[symbol_utils.$getChildren]().length === 0;
    this.root.form = this.form = root.template[symbol_utils.$clone]();
  }
  _isConsumeData() {
    return !this.emptyMerge && this._mergeMode;
  }
  _isMatchTemplate() {
    return !this._isConsumeData();
  }
  bind() {
    this._bindElement(this.form, this.data);
    return this.form;
  }
  getData() {
    return this.data;
  }
  _bindValue(formNode, data, picture) {
    formNode[symbol_utils.$data] = data;
    if (formNode[symbol_utils.$hasSettableValue]()) {
      if (data[symbol_utils.$isDataValue]()) {
        const value = data[symbol_utils.$getDataValue]();
        formNode[symbol_utils.$setValue](createText(value));
      } else if (formNode instanceof Field && formNode.ui?.choiceList?.open === "multiSelect") {
        const value = data[symbol_utils.$getChildren]().map(child => child[symbol_utils.$content].trim()).join("\n");
        formNode[symbol_utils.$setValue](createText(value));
      } else if (this._isConsumeData()) {
        (0,util.warn)(`XFA - Nodes haven't the same type.`);
      }
    } else if (!data[symbol_utils.$isDataValue]() || this._isMatchTemplate()) {
      this._bindElement(formNode, data);
    } else {
      (0,util.warn)(`XFA - Nodes haven't the same type.`);
    }
  }
  _findDataByNameToConsume(name, isValue, dataNode, global) {
    if (!name) {
      return null;
    }
    let generator, match;
    for (let i = 0; i < 3; i++) {
      generator = dataNode[symbol_utils.$getRealChildrenByNameIt](name, false, true);
      while (true) {
        match = generator.next().value;
        if (!match) {
          break;
        }
        if (isValue === match[symbol_utils.$isDataValue]()) {
          return match;
        }
      }
      if (dataNode[symbol_utils.$namespaceId] === NS_DATASETS && dataNode[symbol_utils.$nodeName] === "data") {
        break;
      }
      dataNode = dataNode[symbol_utils.$getParent]();
    }
    if (!global) {
      return null;
    }
    generator = this.data[symbol_utils.$getRealChildrenByNameIt](name, true, false);
    match = generator.next().value;
    if (match) {
      return match;
    }
    generator = this.data[symbol_utils.$getAttributeIt](name, true);
    match = generator.next().value;
    return match?.[symbol_utils.$isDataValue]() ? match : null;
  }
  _setProperties(formNode, dataNode) {
    if (!Object.hasOwn(formNode, "setProperty")) {
      return;
    }
    for (const {
      ref,
      target,
      connection
    } of formNode.setProperty.children) {
      if (connection) {
        continue;
      }
      if (!ref) {
        continue;
      }
      const nodes = (0,som.searchNode)(this.root, dataNode, ref, false, false);
      if (!nodes) {
        (0,util.warn)(`XFA - Invalid reference: ${ref}.`);
        continue;
      }
      const [node] = nodes;
      if (!node[symbol_utils.$isDescendent](this.data)) {
        (0,util.warn)(`XFA - Invalid node: must be a data node.`);
        continue;
      }
      const targetNodes = (0,som.searchNode)(this.root, formNode, target, false, false);
      if (!targetNodes) {
        (0,util.warn)(`XFA - Invalid target: ${target}.`);
        continue;
      }
      const [targetNode] = targetNodes;
      if (!targetNode[symbol_utils.$isDescendent](formNode)) {
        (0,util.warn)(`XFA - Invalid target: must be a property or subproperty.`);
        continue;
      }
      const targetParent = targetNode[symbol_utils.$getParent]();
      if (targetNode instanceof SetProperty || targetParent instanceof SetProperty) {
        (0,util.warn)(`XFA - Invalid target: cannot be a setProperty or one of its properties.`);
        continue;
      }
      if (targetNode instanceof BindItems || targetParent instanceof BindItems) {
        (0,util.warn)(`XFA - Invalid target: cannot be a bindItems or one of its properties.`);
        continue;
      }
      const content = node[symbol_utils.$text]();
      const name = targetNode[symbol_utils.$nodeName];
      if (targetNode instanceof xfa_object.XFAAttribute) {
        const attrs = Object.create(null);
        attrs[name] = content;
        const obj = Reflect.construct(Object.getPrototypeOf(targetParent).constructor, [attrs]);
        targetParent[name] = obj[name];
        continue;
      }
      if (!Object.hasOwn(targetNode, symbol_utils.$content)) {
        (0,util.warn)(`XFA - Invalid node to use in setProperty`);
        continue;
      }
      targetNode[symbol_utils.$data] = node;
      targetNode[symbol_utils.$content] = content;
      targetNode[symbol_utils.$finalize]();
    }
  }
  _bindItems(formNode, dataNode) {
    if (!Object.hasOwn(formNode, "items") || !Object.hasOwn(formNode, "bindItems") || formNode.bindItems.isEmpty()) {
      return;
    }
    for (const item of formNode.items.children) {
      formNode[symbol_utils.$removeChild](item);
    }
    formNode.items.clear();
    const labels = new Items({});
    const values = new Items({});
    formNode[symbol_utils.$appendChild](labels);
    formNode.items.push(labels);
    formNode[symbol_utils.$appendChild](values);
    formNode.items.push(values);
    for (const {
      ref,
      labelRef,
      valueRef,
      connection
    } of formNode.bindItems.children) {
      if (connection) {
        continue;
      }
      if (!ref) {
        continue;
      }
      const nodes = (0,som.searchNode)(this.root, dataNode, ref, false, false);
      if (!nodes) {
        (0,util.warn)(`XFA - Invalid reference: ${ref}.`);
        continue;
      }
      for (const node of nodes) {
        if (!node[symbol_utils.$isDescendent](this.datasets)) {
          (0,util.warn)(`XFA - Invalid ref (${ref}): must be a datasets child.`);
          continue;
        }
        const labelNodes = (0,som.searchNode)(this.root, node, labelRef, true, false);
        if (!labelNodes) {
          (0,util.warn)(`XFA - Invalid label: ${labelRef}.`);
          continue;
        }
        const [labelNode] = labelNodes;
        if (!labelNode[symbol_utils.$isDescendent](this.datasets)) {
          (0,util.warn)(`XFA - Invalid label: must be a datasets child.`);
          continue;
        }
        const valueNodes = (0,som.searchNode)(this.root, node, valueRef, true, false);
        if (!valueNodes) {
          (0,util.warn)(`XFA - Invalid value: ${valueRef}.`);
          continue;
        }
        const [valueNode] = valueNodes;
        if (!valueNode[symbol_utils.$isDescendent](this.datasets)) {
          (0,util.warn)(`XFA - Invalid value: must be a datasets child.`);
          continue;
        }
        const label = createText(labelNode[symbol_utils.$text]());
        const value = createText(valueNode[symbol_utils.$text]());
        labels[symbol_utils.$appendChild](label);
        labels.text.push(label);
        values[symbol_utils.$appendChild](value);
        values.text.push(value);
      }
    }
  }
  _bindOccurrences(formNode, matches, picture) {
    let baseClone;
    if (matches.length > 1) {
      baseClone = formNode[symbol_utils.$clone]();
      baseClone[symbol_utils.$removeChild](baseClone.occur);
      baseClone.occur = null;
    }
    this._bindValue(formNode, matches[0], picture);
    this._setProperties(formNode, matches[0]);
    this._bindItems(formNode, matches[0]);
    if (matches.length === 1) {
      return;
    }
    const parent = formNode[symbol_utils.$getParent]();
    const name = formNode[symbol_utils.$nodeName];
    const pos = parent[symbol_utils.$indexOf](formNode);
    for (let i = 1, ii = matches.length; i < ii; i++) {
      const match = matches[i];
      const clone = baseClone[symbol_utils.$clone]();
      parent[name].push(clone);
      parent[symbol_utils.$insertAt](pos + i, clone);
      this._bindValue(clone, match, picture);
      this._setProperties(clone, match);
      this._bindItems(clone, match);
    }
  }
  _createOccurrences(formNode) {
    if (!this.emptyMerge) {
      return;
    }
    const {
      occur
    } = formNode;
    if (!occur || occur.initial <= 1) {
      return;
    }
    const parent = formNode[symbol_utils.$getParent]();
    const name = formNode[symbol_utils.$nodeName];
    if (!(parent[name] instanceof xfa_object.XFAObjectArray)) {
      return;
    }
    let currentNumber;
    if (formNode.name) {
      currentNumber = parent[name].children.filter(e => e.name === formNode.name).length;
    } else {
      currentNumber = parent[name].children.length;
    }
    const pos = parent[symbol_utils.$indexOf](formNode) + 1;
    const ii = occur.initial - currentNumber;
    if (ii) {
      const nodeClone = formNode[symbol_utils.$clone]();
      nodeClone[symbol_utils.$removeChild](nodeClone.occur);
      nodeClone.occur = null;
      parent[name].push(nodeClone);
      parent[symbol_utils.$insertAt](pos, nodeClone);
      for (let i = 1; i < ii; i++) {
        const clone = nodeClone[symbol_utils.$clone]();
        parent[name].push(clone);
        parent[symbol_utils.$insertAt](pos + i, clone);
      }
    }
  }
  _getOccurInfo(formNode) {
    const {
      name,
      occur
    } = formNode;
    if (!occur || !name) {
      return [1, 1];
    }
    const max = occur.max === -1 ? Infinity : occur.max;
    return [occur.min, max];
  }
  _setAndBind(formNode, dataNode) {
    this._setProperties(formNode, dataNode);
    this._bindItems(formNode, dataNode);
    this._bindElement(formNode, dataNode);
  }
  _bindElement(formNode, dataNode) {
    const uselessNodes = [];
    this._createOccurrences(formNode);
    for (const child of formNode[symbol_utils.$getChildren]()) {
      if (child[symbol_utils.$data]) {
        continue;
      }
      if (this._mergeMode === undefined && child[symbol_utils.$nodeName] === "subform") {
        this._mergeMode = child.mergeMode === "consumeData";
        const dataChildren = dataNode[symbol_utils.$getChildren]();
        if (dataChildren.length > 0) {
          this._bindOccurrences(child, [dataChildren[0]], null);
        } else if (this.emptyMerge) {
          const nsId = dataNode[symbol_utils.$namespaceId] === NS_DATASETS ? -1 : dataNode[symbol_utils.$namespaceId];
          const dataChild = child[symbol_utils.$data] = new xfa_object.XmlObject(nsId, child.name || "root");
          dataNode[symbol_utils.$appendChild](dataChild);
          this._bindElement(child, dataChild);
        }
        continue;
      }
      if (!child[symbol_utils.$isBindable]()) {
        continue;
      }
      let global = false;
      let picture = null;
      let ref = null;
      let match = null;
      if (child.bind) {
        switch (child.bind.match) {
          case "none":
            this._setAndBind(child, dataNode);
            continue;
          case "global":
            global = true;
            break;
          case "dataRef":
            if (!child.bind.ref) {
              (0,util.warn)(`XFA - ref is empty in node ${child[symbol_utils.$nodeName]}.`);
              this._setAndBind(child, dataNode);
              continue;
            }
            ref = child.bind.ref;
            break;
          default:
            break;
        }
        if (child.bind.picture) {
          picture = child.bind.picture[symbol_utils.$content];
        }
      }
      const [min, max] = this._getOccurInfo(child);
      if (ref) {
        match = (0,som.searchNode)(this.root, dataNode, ref, true, false);
        if (match === null) {
          match = (0,som.createDataNode)(this.data, dataNode, ref);
          if (!match) {
            continue;
          }
          if (this._isConsumeData()) {
            match[symbol_utils.$consumed] = true;
          }
          this._setAndBind(child, match);
          continue;
        } else {
          if (this._isConsumeData()) {
            match = match.filter(node => !node[symbol_utils.$consumed]);
          }
          if (match.length > max) {
            match = match.slice(0, max);
          } else if (match.length === 0) {
            match = null;
          }
          if (match && this._isConsumeData()) {
            match.forEach(node => {
              node[symbol_utils.$consumed] = true;
            });
          }
        }
      } else {
        if (!child.name) {
          this._setAndBind(child, dataNode);
          continue;
        }
        if (this._isConsumeData()) {
          const matches = [];
          while (matches.length < max) {
            const found = this._findDataByNameToConsume(child.name, child[symbol_utils.$hasSettableValue](), dataNode, global);
            if (!found) {
              break;
            }
            found[symbol_utils.$consumed] = true;
            matches.push(found);
          }
          match = matches.length > 0 ? matches : null;
        } else {
          match = dataNode[symbol_utils.$getRealChildrenByNameIt](child.name, false, this.emptyMerge).next().value;
          if (!match) {
            if (min === 0) {
              uselessNodes.push(child);
              continue;
            }
            const nsId = dataNode[symbol_utils.$namespaceId] === NS_DATASETS ? -1 : dataNode[symbol_utils.$namespaceId];
            match = child[symbol_utils.$data] = new xfa_object.XmlObject(nsId, child.name);
            if (this.emptyMerge) {
              match[symbol_utils.$consumed] = true;
            }
            dataNode[symbol_utils.$appendChild](match);
            this._setAndBind(child, match);
            continue;
          }
          if (this.emptyMerge) {
            match[symbol_utils.$consumed] = true;
          }
          match = [match];
        }
      }
      if (match) {
        this._bindOccurrences(child, match, picture);
      } else if (min > 0) {
        this._setAndBind(child, dataNode);
      } else {
        uselessNodes.push(child);
      }
    }
    uselessNodes.forEach(node => node[symbol_utils.$getParent]()[symbol_utils.$removeChild](node));
  }
}

;// ./src/core/xfa/data.js

class DataHandler {
  constructor(root, data) {
    this.data = data;
    this.dataset = root.datasets || null;
  }
  serialize(storage) {
    const stack = [[-1, this.data[symbol_utils.$getChildren]()]];
    while (stack.length > 0) {
      const last = stack.at(-1);
      const [i, children] = last;
      if (i + 1 === children.length) {
        stack.pop();
        continue;
      }
      const child = children[++last[0]];
      const storageEntry = storage.get(child[symbol_utils.$uid]);
      if (storageEntry) {
        child[symbol_utils.$setValue](storageEntry);
      } else {
        const attributes = child[symbol_utils.$getAttributes]();
        for (const value of attributes.values()) {
          const entry = storage.get(value[symbol_utils.$uid]);
          if (entry) {
            value[symbol_utils.$setValue](entry);
            break;
          }
        }
      }
      const nodes = child[symbol_utils.$getChildren]();
      if (nodes.length > 0) {
        stack.push([-1, nodes]);
      }
    }
    const buf = [`<xfa:datasets xmlns:xfa="http://www.xfa.org/schema/xfa-data/1.0/">`];
    if (this.dataset) {
      for (const child of this.dataset[symbol_utils.$getChildren]()) {
        if (child[symbol_utils.$nodeName] !== "data") {
          child[symbol_utils.$toString](buf);
        }
      }
    }
    this.data[symbol_utils.$toString](buf);
    buf.push("</xfa:datasets>");
    return buf.join("");
  }
}

;// ./src/core/xfa/config.js





const CONFIG_NS_ID = namespaces.NamespaceIds.config.id;
class Acrobat extends xfa_object.XFAObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "acrobat", true);
    this.acrobat7 = null;
    this.autoSave = null;
    this.common = null;
    this.validate = null;
    this.validateApprovalSignatures = null;
    this.submitUrl = new xfa_object.XFAObjectArray();
  }
}
class Acrobat7 extends xfa_object.XFAObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "acrobat7", true);
    this.dynamicRender = null;
  }
}
class ADBE_JSConsole extends xfa_object.OptionObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "ADBE_JSConsole", ["delegate", "Enable", "Disable"]);
  }
}
class ADBE_JSDebugger extends xfa_object.OptionObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "ADBE_JSDebugger", ["delegate", "Enable", "Disable"]);
  }
}
class AddSilentPrint extends xfa_object.Option01 {
  constructor(attributes) {
    super(CONFIG_NS_ID, "addSilentPrint");
  }
}
class AddViewerPreferences extends xfa_object.Option01 {
  constructor(attributes) {
    super(CONFIG_NS_ID, "addViewerPreferences");
  }
}
class AdjustData extends xfa_object.Option10 {
  constructor(attributes) {
    super(CONFIG_NS_ID, "adjustData");
  }
}
class AdobeExtensionLevel extends xfa_object.IntegerObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "adobeExtensionLevel", 0, n => n >= 1 && n <= 8);
  }
}
class Agent extends xfa_object.XFAObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "agent", true);
    this.name = attributes.name ? attributes.name.trim() : "";
    this.common = new xfa_object.XFAObjectArray();
  }
}
class AlwaysEmbed extends xfa_object.ContentObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "alwaysEmbed");
  }
}
class Amd extends xfa_object.StringObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "amd");
  }
}
class config_Area extends xfa_object.XFAObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "area");
    this.level = (0,utils.getInteger)({
      data: attributes.level,
      defaultValue: 0,
      validate: n => n >= 1 && n <= 3
    });
    this.name = (0,utils.getStringOption)(attributes.name, ["", "barcode", "coreinit", "deviceDriver", "font", "general", "layout", "merge", "script", "signature", "sourceSet", "templateCache"]);
  }
}
class Attributes extends xfa_object.OptionObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "attributes", ["preserve", "delegate", "ignore"]);
  }
}
class AutoSave extends xfa_object.OptionObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "autoSave", ["disabled", "enabled"]);
  }
}
class Base extends xfa_object.StringObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "base");
  }
}
class BatchOutput extends xfa_object.XFAObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "batchOutput");
    this.format = (0,utils.getStringOption)(attributes.format, ["none", "concat", "zip", "zipCompress"]);
  }
}
class BehaviorOverride extends xfa_object.ContentObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "behaviorOverride");
  }
  [symbol_utils.$finalize]() {
    this[symbol_utils.$content] = new Map(this[symbol_utils.$content].trim().split(/\s+/).filter(x => x.includes(":")).map(x => x.split(":", 2)));
  }
}
class Cache extends xfa_object.XFAObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "cache", true);
    this.templateCache = null;
  }
}
class Change extends xfa_object.Option01 {
  constructor(attributes) {
    super(CONFIG_NS_ID, "change");
  }
}
class Common extends xfa_object.XFAObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "common", true);
    this.data = null;
    this.locale = null;
    this.localeSet = null;
    this.messaging = null;
    this.suppressBanner = null;
    this.template = null;
    this.validationMessaging = null;
    this.versionControl = null;
    this.log = new xfa_object.XFAObjectArray();
  }
}
class Compress extends xfa_object.XFAObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "compress");
    this.scope = (0,utils.getStringOption)(attributes.scope, ["imageOnly", "document"]);
  }
}
class CompressLogicalStructure extends xfa_object.Option01 {
  constructor(attributes) {
    super(CONFIG_NS_ID, "compressLogicalStructure");
  }
}
class CompressObjectStream extends xfa_object.Option10 {
  constructor(attributes) {
    super(CONFIG_NS_ID, "compressObjectStream");
  }
}
class Compression extends xfa_object.XFAObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "compression", true);
    this.compressLogicalStructure = null;
    this.compressObjectStream = null;
    this.level = null;
    this.type = null;
  }
}
class Config extends xfa_object.XFAObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "config", true);
    this.acrobat = null;
    this.present = null;
    this.trace = null;
    this.agent = new xfa_object.XFAObjectArray();
  }
}
class Conformance extends xfa_object.OptionObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "conformance", ["A", "B"]);
  }
}
class ContentCopy extends xfa_object.Option01 {
  constructor(attributes) {
    super(CONFIG_NS_ID, "contentCopy");
  }
}
class Copies extends xfa_object.IntegerObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "copies", 1, n => n >= 1);
  }
}
class Creator extends xfa_object.StringObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "creator");
  }
}
class CurrentPage extends xfa_object.IntegerObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "currentPage", 0, n => n >= 0);
  }
}
class Data extends xfa_object.XFAObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "data", true);
    this.adjustData = null;
    this.attributes = null;
    this.incrementalLoad = null;
    this.outputXSL = null;
    this.range = null;
    this.record = null;
    this.startNode = null;
    this.uri = null;
    this.window = null;
    this.xsl = null;
    this.excludeNS = new xfa_object.XFAObjectArray();
    this.transform = new xfa_object.XFAObjectArray();
  }
}
class Debug extends xfa_object.XFAObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "debug", true);
    this.uri = null;
  }
}
class DefaultTypeface extends xfa_object.ContentObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "defaultTypeface");
    this.writingScript = (0,utils.getStringOption)(attributes.writingScript, ["*", "Arabic", "Cyrillic", "EastEuropeanRoman", "Greek", "Hebrew", "Japanese", "Korean", "Roman", "SimplifiedChinese", "Thai", "TraditionalChinese", "Vietnamese"]);
  }
}
class Destination extends xfa_object.OptionObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "destination", ["pdf", "pcl", "ps", "webClient", "zpl"]);
  }
}
class DocumentAssembly extends xfa_object.Option01 {
  constructor(attributes) {
    super(CONFIG_NS_ID, "documentAssembly");
  }
}
class Driver extends xfa_object.XFAObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "driver", true);
    this.name = attributes.name ? attributes.name.trim() : "";
    this.fontInfo = null;
    this.xdc = null;
  }
}
class DuplexOption extends xfa_object.OptionObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "duplexOption", ["simplex", "duplexFlipLongEdge", "duplexFlipShortEdge"]);
  }
}
class DynamicRender extends xfa_object.OptionObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "dynamicRender", ["forbidden", "required"]);
  }
}
class Embed extends xfa_object.Option01 {
  constructor(attributes) {
    super(CONFIG_NS_ID, "embed");
  }
}
class config_Encrypt extends xfa_object.Option01 {
  constructor(attributes) {
    super(CONFIG_NS_ID, "encrypt");
  }
}
class config_Encryption extends xfa_object.XFAObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "encryption", true);
    this.encrypt = null;
    this.encryptionLevel = null;
    this.permissions = null;
  }
}
class EncryptionLevel extends xfa_object.OptionObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "encryptionLevel", ["40bit", "128bit"]);
  }
}
class Enforce extends xfa_object.StringObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "enforce");
  }
}
class Equate extends xfa_object.XFAObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "equate");
    this.force = (0,utils.getInteger)({
      data: attributes.force,
      defaultValue: 1,
      validate: n => n === 0
    });
    this.from = attributes.from || "";
    this.to = attributes.to || "";
  }
}
class EquateRange extends xfa_object.XFAObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "equateRange");
    this.from = attributes.from || "";
    this.to = attributes.to || "";
    this._unicodeRange = attributes.unicodeRange || "";
  }
  get unicodeRange() {
    const ranges = [];
    const unicodeRegex = /U\+([0-9a-fA-F]+)/;
    const unicodeRange = this._unicodeRange;
    for (let range of unicodeRange.split(",").map(x => x.trim()).filter(Boolean)) {
      range = range.split("-", 2).map(x => {
        const found = x.match(unicodeRegex);
        return !found ? 0 : parseInt(found[1], 16);
      });
      if (range.length === 1) {
        range.push(range[0]);
      }
      ranges.push(range);
    }
    return (0,util.shadow)(this, "unicodeRange", ranges);
  }
}
class Exclude extends xfa_object.ContentObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "exclude");
  }
  [symbol_utils.$finalize]() {
    this[symbol_utils.$content] = this[symbol_utils.$content].trim().split(/\s+/).filter(x => x && ["calculate", "close", "enter", "exit", "initialize", "ready", "validate"].includes(x));
  }
}
class ExcludeNS extends xfa_object.StringObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "excludeNS");
  }
}
class FlipLabel extends xfa_object.OptionObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "flipLabel", ["usePrinterSetting", "on", "off"]);
  }
}
class FontInfo extends xfa_object.XFAObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "fontInfo", true);
    this.embed = null;
    this.map = null;
    this.subsetBelow = null;
    this.alwaysEmbed = new xfa_object.XFAObjectArray();
    this.defaultTypeface = new xfa_object.XFAObjectArray();
    this.neverEmbed = new xfa_object.XFAObjectArray();
  }
}
class FormFieldFilling extends xfa_object.Option01 {
  constructor(attributes) {
    super(CONFIG_NS_ID, "formFieldFilling");
  }
}
class GroupParent extends xfa_object.StringObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "groupParent");
  }
}
class IfEmpty extends xfa_object.OptionObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "ifEmpty", ["dataValue", "dataGroup", "ignore", "remove"]);
  }
}
class IncludeXDPContent extends xfa_object.StringObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "includeXDPContent");
  }
}
class IncrementalLoad extends xfa_object.OptionObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "incrementalLoad", ["none", "forwardOnly"]);
  }
}
class IncrementalMerge extends xfa_object.Option01 {
  constructor(attributes) {
    super(CONFIG_NS_ID, "incrementalMerge");
  }
}
class Interactive extends xfa_object.Option01 {
  constructor(attributes) {
    super(CONFIG_NS_ID, "interactive");
  }
}
class Jog extends xfa_object.OptionObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "jog", ["usePrinterSetting", "none", "pageSet"]);
  }
}
class LabelPrinter extends xfa_object.XFAObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "labelPrinter", true);
    this.name = (0,utils.getStringOption)(attributes.name, ["zpl", "dpl", "ipl", "tcpl"]);
    this.batchOutput = null;
    this.flipLabel = null;
    this.fontInfo = null;
    this.xdc = null;
  }
}
class Layout extends xfa_object.OptionObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "layout", ["paginate", "panel"]);
  }
}
class Level extends xfa_object.IntegerObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "level", 0, n => n > 0);
  }
}
class Linearized extends xfa_object.Option01 {
  constructor(attributes) {
    super(CONFIG_NS_ID, "linearized");
  }
}
class Locale extends xfa_object.StringObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "locale");
  }
}
class LocaleSet extends xfa_object.StringObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "localeSet");
  }
}
class Log extends xfa_object.XFAObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "log", true);
    this.mode = null;
    this.threshold = null;
    this.to = null;
    this.uri = null;
  }
}
class MapElement extends xfa_object.XFAObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "map", true);
    this.equate = new xfa_object.XFAObjectArray();
    this.equateRange = new xfa_object.XFAObjectArray();
  }
}
class MediumInfo extends xfa_object.XFAObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "mediumInfo", true);
    this.map = null;
  }
}
class config_Message extends xfa_object.XFAObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "message", true);
    this.msgId = null;
    this.severity = null;
  }
}
class Messaging extends xfa_object.XFAObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "messaging", true);
    this.message = new xfa_object.XFAObjectArray();
  }
}
class Mode extends xfa_object.OptionObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "mode", ["append", "overwrite"]);
  }
}
class ModifyAnnots extends xfa_object.Option01 {
  constructor(attributes) {
    super(CONFIG_NS_ID, "modifyAnnots");
  }
}
class MsgId extends xfa_object.IntegerObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "msgId", 1, n => n >= 1);
  }
}
class NameAttr extends xfa_object.StringObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "nameAttr");
  }
}
class NeverEmbed extends xfa_object.ContentObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "neverEmbed");
  }
}
class NumberOfCopies extends xfa_object.IntegerObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "numberOfCopies", null, n => n >= 2 && n <= 5);
  }
}
class OpenAction extends xfa_object.XFAObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "openAction", true);
    this.destination = null;
  }
}
class Output extends xfa_object.XFAObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "output", true);
    this.to = null;
    this.type = null;
    this.uri = null;
  }
}
class OutputBin extends xfa_object.StringObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "outputBin");
  }
}
class OutputXSL extends xfa_object.XFAObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "outputXSL", true);
    this.uri = null;
  }
}
class Overprint extends xfa_object.OptionObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "overprint", ["none", "both", "draw", "field"]);
  }
}
class Packets extends xfa_object.StringObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "packets");
  }
  [symbol_utils.$finalize]() {
    if (this[symbol_utils.$content] === "*") {
      return;
    }
    this[symbol_utils.$content] = this[symbol_utils.$content].trim().split(/\s+/).filter(x => ["config", "datasets", "template", "xfdf", "xslt"].includes(x));
  }
}
class PageOffset extends xfa_object.XFAObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "pageOffset");
    this.x = (0,utils.getInteger)({
      data: attributes.x,
      defaultValue: "useXDCSetting",
      validate: n => true
    });
    this.y = (0,utils.getInteger)({
      data: attributes.y,
      defaultValue: "useXDCSetting",
      validate: n => true
    });
  }
}
class PageRange extends xfa_object.StringObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "pageRange");
  }
  [symbol_utils.$finalize]() {
    const numbers = this[symbol_utils.$content].trim().split(/\s+/).map(x => parseInt(x, 10));
    const ranges = [];
    for (let i = 0, ii = numbers.length; i < ii; i += 2) {
      ranges.push(numbers.slice(i, i + 2));
    }
    this[symbol_utils.$content] = ranges;
  }
}
class Pagination extends xfa_object.OptionObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "pagination", ["simplex", "duplexShortEdge", "duplexLongEdge"]);
  }
}
class PaginationOverride extends xfa_object.OptionObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "paginationOverride", ["none", "forceDuplex", "forceDuplexLongEdge", "forceDuplexShortEdge", "forceSimplex"]);
  }
}
class Part extends xfa_object.IntegerObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "part", 1, n => false);
  }
}
class Pcl extends xfa_object.XFAObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "pcl", true);
    this.name = attributes.name || "";
    this.batchOutput = null;
    this.fontInfo = null;
    this.jog = null;
    this.mediumInfo = null;
    this.outputBin = null;
    this.pageOffset = null;
    this.staple = null;
    this.xdc = null;
  }
}
class Pdf extends xfa_object.XFAObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "pdf", true);
    this.name = attributes.name || "";
    this.adobeExtensionLevel = null;
    this.batchOutput = null;
    this.compression = null;
    this.creator = null;
    this.encryption = null;
    this.fontInfo = null;
    this.interactive = null;
    this.linearized = null;
    this.openAction = null;
    this.pdfa = null;
    this.producer = null;
    this.renderPolicy = null;
    this.scriptModel = null;
    this.silentPrint = null;
    this.submitFormat = null;
    this.tagged = null;
    this.version = null;
    this.viewerPreferences = null;
    this.xdc = null;
  }
}
class Pdfa extends xfa_object.XFAObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "pdfa", true);
    this.amd = null;
    this.conformance = null;
    this.includeXDPContent = null;
    this.part = null;
  }
}
class Permissions extends xfa_object.XFAObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "permissions", true);
    this.accessibleContent = null;
    this.change = null;
    this.contentCopy = null;
    this.documentAssembly = null;
    this.formFieldFilling = null;
    this.modifyAnnots = null;
    this.plaintextMetadata = null;
    this.print = null;
    this.printHighQuality = null;
  }
}
class PickTrayByPDFSize extends xfa_object.Option01 {
  constructor(attributes) {
    super(CONFIG_NS_ID, "pickTrayByPDFSize");
  }
}
class config_Picture extends xfa_object.StringObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "picture");
  }
}
class PlaintextMetadata extends xfa_object.Option01 {
  constructor(attributes) {
    super(CONFIG_NS_ID, "plaintextMetadata");
  }
}
class Presence extends xfa_object.OptionObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "presence", ["preserve", "dissolve", "dissolveStructure", "ignore", "remove"]);
  }
}
class Present extends xfa_object.XFAObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "present", true);
    this.behaviorOverride = null;
    this.cache = null;
    this.common = null;
    this.copies = null;
    this.destination = null;
    this.incrementalMerge = null;
    this.layout = null;
    this.output = null;
    this.overprint = null;
    this.pagination = null;
    this.paginationOverride = null;
    this.script = null;
    this.validate = null;
    this.xdp = null;
    this.driver = new xfa_object.XFAObjectArray();
    this.labelPrinter = new xfa_object.XFAObjectArray();
    this.pcl = new xfa_object.XFAObjectArray();
    this.pdf = new xfa_object.XFAObjectArray();
    this.ps = new xfa_object.XFAObjectArray();
    this.submitUrl = new xfa_object.XFAObjectArray();
    this.webClient = new xfa_object.XFAObjectArray();
    this.zpl = new xfa_object.XFAObjectArray();
  }
}
class Print extends xfa_object.Option01 {
  constructor(attributes) {
    super(CONFIG_NS_ID, "print");
  }
}
class PrintHighQuality extends xfa_object.Option01 {
  constructor(attributes) {
    super(CONFIG_NS_ID, "printHighQuality");
  }
}
class PrintScaling extends xfa_object.OptionObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "printScaling", ["appdefault", "noScaling"]);
  }
}
class PrinterName extends xfa_object.StringObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "printerName");
  }
}
class Producer extends xfa_object.StringObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "producer");
  }
}
class Ps extends xfa_object.XFAObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "ps", true);
    this.name = attributes.name || "";
    this.batchOutput = null;
    this.fontInfo = null;
    this.jog = null;
    this.mediumInfo = null;
    this.outputBin = null;
    this.staple = null;
    this.xdc = null;
  }
}
class Range extends xfa_object.ContentObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "range");
  }
  [symbol_utils.$finalize]() {
    this[symbol_utils.$content] = this[symbol_utils.$content].split(",", 2).map(range => range.split("-").map(x => parseInt(x.trim(), 10))).filter(range => range.every(x => !isNaN(x))).map(range => {
      if (range.length === 1) {
        range.push(range[0]);
      }
      return range;
    });
  }
}
class Record extends xfa_object.ContentObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "record");
  }
  [symbol_utils.$finalize]() {
    this[symbol_utils.$content] = this[symbol_utils.$content].trim();
    const n = parseInt(this[symbol_utils.$content], 10);
    if (!isNaN(n) && n >= 0) {
      this[symbol_utils.$content] = n;
    }
  }
}
class Relevant extends xfa_object.ContentObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "relevant");
  }
  [symbol_utils.$finalize]() {
    this[symbol_utils.$content] = this[symbol_utils.$content].trim().split(/\s+/);
  }
}
class Rename extends xfa_object.ContentObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "rename");
  }
  [symbol_utils.$finalize]() {
    this[symbol_utils.$content] = this[symbol_utils.$content].trim();
    if (this[symbol_utils.$content].toLowerCase().startsWith("xml") || /[\p{L}_][\p{L}\d._\p{M}-]*/u.test(this[symbol_utils.$content])) {
      (0,util.warn)("XFA - Rename: invalid XFA name");
    }
  }
}
class RenderPolicy extends xfa_object.OptionObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "renderPolicy", ["server", "client"]);
  }
}
class RunScripts extends xfa_object.OptionObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "runScripts", ["both", "client", "none", "server"]);
  }
}
class config_Script extends xfa_object.XFAObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "script", true);
    this.currentPage = null;
    this.exclude = null;
    this.runScripts = null;
  }
}
class ScriptModel extends xfa_object.OptionObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "scriptModel", ["XFA", "none"]);
  }
}
class Severity extends xfa_object.OptionObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "severity", ["ignore", "error", "information", "trace", "warning"]);
  }
}
class SilentPrint extends xfa_object.XFAObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "silentPrint", true);
    this.addSilentPrint = null;
    this.printerName = null;
  }
}
class Staple extends xfa_object.XFAObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "staple");
    this.mode = (0,utils.getStringOption)(attributes.mode, ["usePrinterSetting", "on", "off"]);
  }
}
class StartNode extends xfa_object.StringObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "startNode");
  }
}
class StartPage extends xfa_object.IntegerObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "startPage", 0, n => true);
  }
}
class SubmitFormat extends xfa_object.OptionObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "submitFormat", ["html", "delegate", "fdf", "xml", "pdf"]);
  }
}
class SubmitUrl extends xfa_object.StringObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "submitUrl");
  }
}
class SubsetBelow extends xfa_object.IntegerObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "subsetBelow", 100, n => n >= 0 && n <= 100);
  }
}
class SuppressBanner extends xfa_object.Option01 {
  constructor(attributes) {
    super(CONFIG_NS_ID, "suppressBanner");
  }
}
class Tagged extends xfa_object.Option01 {
  constructor(attributes) {
    super(CONFIG_NS_ID, "tagged");
  }
}
class config_Template extends xfa_object.XFAObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "template", true);
    this.base = null;
    this.relevant = null;
    this.startPage = null;
    this.uri = null;
    this.xsl = null;
  }
}
class Threshold extends xfa_object.OptionObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "threshold", ["trace", "error", "information", "warning"]);
  }
}
class To extends xfa_object.OptionObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "to", ["null", "memory", "stderr", "stdout", "system", "uri"]);
  }
}
class TemplateCache extends xfa_object.XFAObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "templateCache");
    this.maxEntries = (0,utils.getInteger)({
      data: attributes.maxEntries,
      defaultValue: 5,
      validate: n => n >= 0
    });
  }
}
class Trace extends xfa_object.XFAObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "trace", true);
    this.area = new xfa_object.XFAObjectArray();
  }
}
class Transform extends xfa_object.XFAObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "transform", true);
    this.groupParent = null;
    this.ifEmpty = null;
    this.nameAttr = null;
    this.picture = null;
    this.presence = null;
    this.rename = null;
    this.whitespace = null;
  }
}
class Type extends xfa_object.OptionObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "type", ["none", "ascii85", "asciiHex", "ccittfax", "flate", "lzw", "runLength", "native", "xdp", "mergedXDP"]);
  }
}
class Uri extends xfa_object.StringObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "uri");
  }
}
class config_Validate extends xfa_object.OptionObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "validate", ["preSubmit", "prePrint", "preExecute", "preSave"]);
  }
}
class ValidateApprovalSignatures extends xfa_object.ContentObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "validateApprovalSignatures");
  }
  [symbol_utils.$finalize]() {
    this[symbol_utils.$content] = this[symbol_utils.$content].trim().split(/\s+/).filter(x => ["docReady", "postSign"].includes(x));
  }
}
class ValidationMessaging extends xfa_object.OptionObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "validationMessaging", ["allMessagesIndividually", "allMessagesTogether", "firstMessageOnly", "noMessages"]);
  }
}
class Version extends xfa_object.OptionObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "version", ["1.7", "1.6", "1.5", "1.4", "1.3", "1.2"]);
  }
}
class VersionControl extends xfa_object.XFAObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "VersionControl");
    this.outputBelow = (0,utils.getStringOption)(attributes.outputBelow, ["warn", "error", "update"]);
    this.sourceAbove = (0,utils.getStringOption)(attributes.sourceAbove, ["warn", "error"]);
    this.sourceBelow = (0,utils.getStringOption)(attributes.sourceBelow, ["update", "maintain"]);
  }
}
class ViewerPreferences extends xfa_object.XFAObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "viewerPreferences", true);
    this.ADBE_JSConsole = null;
    this.ADBE_JSDebugger = null;
    this.addViewerPreferences = null;
    this.duplexOption = null;
    this.enforce = null;
    this.numberOfCopies = null;
    this.pageRange = null;
    this.pickTrayByPDFSize = null;
    this.printScaling = null;
  }
}
class WebClient extends xfa_object.XFAObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "webClient", true);
    this.name = attributes.name ? attributes.name.trim() : "";
    this.fontInfo = null;
    this.xdc = null;
  }
}
class Whitespace extends xfa_object.OptionObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "whitespace", ["preserve", "ltrim", "normalize", "rtrim", "trim"]);
  }
}
class Window extends xfa_object.ContentObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "window");
  }
  [symbol_utils.$finalize]() {
    const pair = this[symbol_utils.$content].split(",", 2).map(x => parseInt(x.trim(), 10));
    if (pair.some(x => isNaN(x))) {
      this[symbol_utils.$content] = [0, 0];
      return;
    }
    if (pair.length === 1) {
      pair.push(pair[0]);
    }
    this[symbol_utils.$content] = pair;
  }
}
class Xdc extends xfa_object.XFAObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "xdc", true);
    this.uri = new xfa_object.XFAObjectArray();
    this.xsl = new xfa_object.XFAObjectArray();
  }
}
class Xdp extends xfa_object.XFAObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "xdp", true);
    this.packets = null;
  }
}
class Xsl extends xfa_object.XFAObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "xsl", true);
    this.debug = null;
    this.uri = null;
  }
}
class Zpl extends xfa_object.XFAObject {
  constructor(attributes) {
    super(CONFIG_NS_ID, "zpl", true);
    this.name = attributes.name ? attributes.name.trim() : "";
    this.batchOutput = null;
    this.flipLabel = null;
    this.fontInfo = null;
    this.xdc = null;
  }
}
class ConfigNamespace {
  static [namespaces.$buildXFAObject](name, attributes) {
    return Object.hasOwn(ConfigNamespace, name) ? ConfigNamespace[name](attributes) : undefined;
  }
  static acrobat(attrs) {
    return new Acrobat(attrs);
  }
  static acrobat7(attrs) {
    return new Acrobat7(attrs);
  }
  static ADBE_JSConsole(attrs) {
    return new ADBE_JSConsole(attrs);
  }
  static ADBE_JSDebugger(attrs) {
    return new ADBE_JSDebugger(attrs);
  }
  static addSilentPrint(attrs) {
    return new AddSilentPrint(attrs);
  }
  static addViewerPreferences(attrs) {
    return new AddViewerPreferences(attrs);
  }
  static adjustData(attrs) {
    return new AdjustData(attrs);
  }
  static adobeExtensionLevel(attrs) {
    return new AdobeExtensionLevel(attrs);
  }
  static agent(attrs) {
    return new Agent(attrs);
  }
  static alwaysEmbed(attrs) {
    return new AlwaysEmbed(attrs);
  }
  static amd(attrs) {
    return new Amd(attrs);
  }
  static area(attrs) {
    return new config_Area(attrs);
  }
  static attributes(attrs) {
    return new Attributes(attrs);
  }
  static autoSave(attrs) {
    return new AutoSave(attrs);
  }
  static base(attrs) {
    return new Base(attrs);
  }
  static batchOutput(attrs) {
    return new BatchOutput(attrs);
  }
  static behaviorOverride(attrs) {
    return new BehaviorOverride(attrs);
  }
  static cache(attrs) {
    return new Cache(attrs);
  }
  static change(attrs) {
    return new Change(attrs);
  }
  static common(attrs) {
    return new Common(attrs);
  }
  static compress(attrs) {
    return new Compress(attrs);
  }
  static compressLogicalStructure(attrs) {
    return new CompressLogicalStructure(attrs);
  }
  static compressObjectStream(attrs) {
    return new CompressObjectStream(attrs);
  }
  static compression(attrs) {
    return new Compression(attrs);
  }
  static config(attrs) {
    return new Config(attrs);
  }
  static conformance(attrs) {
    return new Conformance(attrs);
  }
  static contentCopy(attrs) {
    return new ContentCopy(attrs);
  }
  static copies(attrs) {
    return new Copies(attrs);
  }
  static creator(attrs) {
    return new Creator(attrs);
  }
  static currentPage(attrs) {
    return new CurrentPage(attrs);
  }
  static data(attrs) {
    return new Data(attrs);
  }
  static debug(attrs) {
    return new Debug(attrs);
  }
  static defaultTypeface(attrs) {
    return new DefaultTypeface(attrs);
  }
  static destination(attrs) {
    return new Destination(attrs);
  }
  static documentAssembly(attrs) {
    return new DocumentAssembly(attrs);
  }
  static driver(attrs) {
    return new Driver(attrs);
  }
  static duplexOption(attrs) {
    return new DuplexOption(attrs);
  }
  static dynamicRender(attrs) {
    return new DynamicRender(attrs);
  }
  static embed(attrs) {
    return new Embed(attrs);
  }
  static encrypt(attrs) {
    return new config_Encrypt(attrs);
  }
  static encryption(attrs) {
    return new config_Encryption(attrs);
  }
  static encryptionLevel(attrs) {
    return new EncryptionLevel(attrs);
  }
  static enforce(attrs) {
    return new Enforce(attrs);
  }
  static equate(attrs) {
    return new Equate(attrs);
  }
  static equateRange(attrs) {
    return new EquateRange(attrs);
  }
  static exclude(attrs) {
    return new Exclude(attrs);
  }
  static excludeNS(attrs) {
    return new ExcludeNS(attrs);
  }
  static flipLabel(attrs) {
    return new FlipLabel(attrs);
  }
  static fontInfo(attrs) {
    return new FontInfo(attrs);
  }
  static formFieldFilling(attrs) {
    return new FormFieldFilling(attrs);
  }
  static groupParent(attrs) {
    return new GroupParent(attrs);
  }
  static ifEmpty(attrs) {
    return new IfEmpty(attrs);
  }
  static includeXDPContent(attrs) {
    return new IncludeXDPContent(attrs);
  }
  static incrementalLoad(attrs) {
    return new IncrementalLoad(attrs);
  }
  static incrementalMerge(attrs) {
    return new IncrementalMerge(attrs);
  }
  static interactive(attrs) {
    return new Interactive(attrs);
  }
  static jog(attrs) {
    return new Jog(attrs);
  }
  static labelPrinter(attrs) {
    return new LabelPrinter(attrs);
  }
  static layout(attrs) {
    return new Layout(attrs);
  }
  static level(attrs) {
    return new Level(attrs);
  }
  static linearized(attrs) {
    return new Linearized(attrs);
  }
  static locale(attrs) {
    return new Locale(attrs);
  }
  static localeSet(attrs) {
    return new LocaleSet(attrs);
  }
  static log(attrs) {
    return new Log(attrs);
  }
  static map(attrs) {
    return new MapElement(attrs);
  }
  static mediumInfo(attrs) {
    return new MediumInfo(attrs);
  }
  static message(attrs) {
    return new config_Message(attrs);
  }
  static messaging(attrs) {
    return new Messaging(attrs);
  }
  static mode(attrs) {
    return new Mode(attrs);
  }
  static modifyAnnots(attrs) {
    return new ModifyAnnots(attrs);
  }
  static msgId(attrs) {
    return new MsgId(attrs);
  }
  static nameAttr(attrs) {
    return new NameAttr(attrs);
  }
  static neverEmbed(attrs) {
    return new NeverEmbed(attrs);
  }
  static numberOfCopies(attrs) {
    return new NumberOfCopies(attrs);
  }
  static openAction(attrs) {
    return new OpenAction(attrs);
  }
  static output(attrs) {
    return new Output(attrs);
  }
  static outputBin(attrs) {
    return new OutputBin(attrs);
  }
  static outputXSL(attrs) {
    return new OutputXSL(attrs);
  }
  static overprint(attrs) {
    return new Overprint(attrs);
  }
  static packets(attrs) {
    return new Packets(attrs);
  }
  static pageOffset(attrs) {
    return new PageOffset(attrs);
  }
  static pageRange(attrs) {
    return new PageRange(attrs);
  }
  static pagination(attrs) {
    return new Pagination(attrs);
  }
  static paginationOverride(attrs) {
    return new PaginationOverride(attrs);
  }
  static part(attrs) {
    return new Part(attrs);
  }
  static pcl(attrs) {
    return new Pcl(attrs);
  }
  static pdf(attrs) {
    return new Pdf(attrs);
  }
  static pdfa(attrs) {
    return new Pdfa(attrs);
  }
  static permissions(attrs) {
    return new Permissions(attrs);
  }
  static pickTrayByPDFSize(attrs) {
    return new PickTrayByPDFSize(attrs);
  }
  static picture(attrs) {
    return new config_Picture(attrs);
  }
  static plaintextMetadata(attrs) {
    return new PlaintextMetadata(attrs);
  }
  static presence(attrs) {
    return new Presence(attrs);
  }
  static present(attrs) {
    return new Present(attrs);
  }
  static print(attrs) {
    return new Print(attrs);
  }
  static printHighQuality(attrs) {
    return new PrintHighQuality(attrs);
  }
  static printScaling(attrs) {
    return new PrintScaling(attrs);
  }
  static printerName(attrs) {
    return new PrinterName(attrs);
  }
  static producer(attrs) {
    return new Producer(attrs);
  }
  static ps(attrs) {
    return new Ps(attrs);
  }
  static range(attrs) {
    return new Range(attrs);
  }
  static record(attrs) {
    return new Record(attrs);
  }
  static relevant(attrs) {
    return new Relevant(attrs);
  }
  static rename(attrs) {
    return new Rename(attrs);
  }
  static renderPolicy(attrs) {
    return new RenderPolicy(attrs);
  }
  static runScripts(attrs) {
    return new RunScripts(attrs);
  }
  static script(attrs) {
    return new config_Script(attrs);
  }
  static scriptModel(attrs) {
    return new ScriptModel(attrs);
  }
  static severity(attrs) {
    return new Severity(attrs);
  }
  static silentPrint(attrs) {
    return new SilentPrint(attrs);
  }
  static staple(attrs) {
    return new Staple(attrs);
  }
  static startNode(attrs) {
    return new StartNode(attrs);
  }
  static startPage(attrs) {
    return new StartPage(attrs);
  }
  static submitFormat(attrs) {
    return new SubmitFormat(attrs);
  }
  static submitUrl(attrs) {
    return new SubmitUrl(attrs);
  }
  static subsetBelow(attrs) {
    return new SubsetBelow(attrs);
  }
  static suppressBanner(attrs) {
    return new SuppressBanner(attrs);
  }
  static tagged(attrs) {
    return new Tagged(attrs);
  }
  static template(attrs) {
    return new config_Template(attrs);
  }
  static templateCache(attrs) {
    return new TemplateCache(attrs);
  }
  static threshold(attrs) {
    return new Threshold(attrs);
  }
  static to(attrs) {
    return new To(attrs);
  }
  static trace(attrs) {
    return new Trace(attrs);
  }
  static transform(attrs) {
    return new Transform(attrs);
  }
  static type(attrs) {
    return new Type(attrs);
  }
  static uri(attrs) {
    return new Uri(attrs);
  }
  static validate(attrs) {
    return new config_Validate(attrs);
  }
  static validateApprovalSignatures(attrs) {
    return new ValidateApprovalSignatures(attrs);
  }
  static validationMessaging(attrs) {
    return new ValidationMessaging(attrs);
  }
  static version(attrs) {
    return new Version(attrs);
  }
  static versionControl(attrs) {
    return new VersionControl(attrs);
  }
  static viewerPreferences(attrs) {
    return new ViewerPreferences(attrs);
  }
  static webClient(attrs) {
    return new WebClient(attrs);
  }
  static whitespace(attrs) {
    return new Whitespace(attrs);
  }
  static window(attrs) {
    return new Window(attrs);
  }
  static xdc(attrs) {
    return new Xdc(attrs);
  }
  static xdp(attrs) {
    return new Xdp(attrs);
  }
  static xsl(attrs) {
    return new Xsl(attrs);
  }
  static zpl(attrs) {
    return new Zpl(attrs);
  }
}

;// ./src/core/xfa/connection_set.js


const CONNECTION_SET_NS_ID = namespaces.NamespaceIds.connectionSet.id;
class ConnectionSet extends xfa_object.XFAObject {
  constructor(attributes) {
    super(CONNECTION_SET_NS_ID, "connectionSet", true);
    this.wsdlConnection = new xfa_object.XFAObjectArray();
    this.xmlConnection = new xfa_object.XFAObjectArray();
    this.xsdConnection = new xfa_object.XFAObjectArray();
  }
}
class EffectiveInputPolicy extends xfa_object.XFAObject {
  constructor(attributes) {
    super(CONNECTION_SET_NS_ID, "effectiveInputPolicy");
    this.id = attributes.id || "";
    this.name = attributes.name || "";
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
  }
}
class EffectiveOutputPolicy extends xfa_object.XFAObject {
  constructor(attributes) {
    super(CONNECTION_SET_NS_ID, "effectiveOutputPolicy");
    this.id = attributes.id || "";
    this.name = attributes.name || "";
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
  }
}
class Operation extends xfa_object.StringObject {
  constructor(attributes) {
    super(CONNECTION_SET_NS_ID, "operation");
    this.id = attributes.id || "";
    this.input = attributes.input || "";
    this.name = attributes.name || "";
    this.output = attributes.output || "";
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
  }
}
class RootElement extends xfa_object.StringObject {
  constructor(attributes) {
    super(CONNECTION_SET_NS_ID, "rootElement");
    this.id = attributes.id || "";
    this.name = attributes.name || "";
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
  }
}
class SoapAction extends xfa_object.StringObject {
  constructor(attributes) {
    super(CONNECTION_SET_NS_ID, "soapAction");
    this.id = attributes.id || "";
    this.name = attributes.name || "";
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
  }
}
class SoapAddress extends xfa_object.StringObject {
  constructor(attributes) {
    super(CONNECTION_SET_NS_ID, "soapAddress");
    this.id = attributes.id || "";
    this.name = attributes.name || "";
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
  }
}
class connection_set_Uri extends xfa_object.StringObject {
  constructor(attributes) {
    super(CONNECTION_SET_NS_ID, "uri");
    this.id = attributes.id || "";
    this.name = attributes.name || "";
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
  }
}
class WsdlAddress extends xfa_object.StringObject {
  constructor(attributes) {
    super(CONNECTION_SET_NS_ID, "wsdlAddress");
    this.id = attributes.id || "";
    this.name = attributes.name || "";
    this.use = attributes.use || "";
    this.usehref = attributes.usehref || "";
  }
}
class WsdlConnection extends xfa_object.XFAObject {
  constructor(attributes) {
    super(CONNECTION_SET_NS_ID, "wsdlConnection", true);
    this.dataDescription = attributes.dataDescription || "";
    this.name = attributes.name || "";
    this.effectiveInputPolicy = null;
    this.effectiveOutputPolicy = null;
    this.operation = null;
    this.soapAction = null;
    this.soapAddress = null;
    this.wsdlAddress = null;
  }
}
class XmlConnection extends xfa_object.XFAObject {
  constructor(attributes) {
    super(CONNECTION_SET_NS_ID, "xmlConnection", true);
    this.dataDescription = attributes.dataDescription || "";
    this.name = attributes.name || "";
    this.uri = null;
  }
}
class XsdConnection extends xfa_object.XFAObject {
  constructor(attributes) {
    super(CONNECTION_SET_NS_ID, "xsdConnection", true);
    this.dataDescription = attributes.dataDescription || "";
    this.name = attributes.name || "";
    this.rootElement = null;
    this.uri = null;
  }
}
class ConnectionSetNamespace {
  static [namespaces.$buildXFAObject](name, attributes) {
    return Object.hasOwn(ConnectionSetNamespace, name) ? ConnectionSetNamespace[name](attributes) : undefined;
  }
  static connectionSet(attrs) {
    return new ConnectionSet(attrs);
  }
  static effectiveInputPolicy(attrs) {
    return new EffectiveInputPolicy(attrs);
  }
  static effectiveOutputPolicy(attrs) {
    return new EffectiveOutputPolicy(attrs);
  }
  static operation(attrs) {
    return new Operation(attrs);
  }
  static rootElement(attrs) {
    return new RootElement(attrs);
  }
  static soapAction(attrs) {
    return new SoapAction(attrs);
  }
  static soapAddress(attrs) {
    return new SoapAddress(attrs);
  }
  static uri(attrs) {
    return new connection_set_Uri(attrs);
  }
  static wsdlAddress(attrs) {
    return new WsdlAddress(attrs);
  }
  static wsdlConnection(attrs) {
    return new WsdlConnection(attrs);
  }
  static xmlConnection(attrs) {
    return new XmlConnection(attrs);
  }
  static xsdConnection(attrs) {
    return new XsdConnection(attrs);
  }
}

;// ./src/core/xfa/datasets.js



const DATASETS_NS_ID = namespaces.NamespaceIds.datasets.id;
class datasets_Data extends xfa_object.XmlObject {
  constructor(attributes) {
    super(DATASETS_NS_ID, "data", attributes);
  }
  [symbol_utils.$isNsAgnostic]() {
    return true;
  }
}
class Datasets extends xfa_object.XFAObject {
  constructor(attributes) {
    super(DATASETS_NS_ID, "datasets", true);
    this.data = null;
    this.Signature = null;
  }
  [symbol_utils.$onChild](child) {
    const name = child[symbol_utils.$nodeName];
    if (name === "data" && child[symbol_utils.$namespaceId] === DATASETS_NS_ID || name === "Signature" && child[symbol_utils.$namespaceId] === namespaces.NamespaceIds.signature.id) {
      this[name] = child;
    }
    this[symbol_utils.$appendChild](child);
  }
}
class DatasetsNamespace {
  static [namespaces.$buildXFAObject](name, attributes) {
    return Object.hasOwn(DatasetsNamespace, name) ? DatasetsNamespace[name](attributes) : undefined;
  }
  static datasets(attributes) {
    return new Datasets(attributes);
  }
  static data(attributes) {
    return new datasets_Data(attributes);
  }
}

;// ./src/core/xfa/locale_set.js



const LOCALE_SET_NS_ID = namespaces.NamespaceIds.localeSet.id;
class CalendarSymbols extends xfa_object.XFAObject {
  constructor(attributes) {
    super(LOCALE_SET_NS_ID, "calendarSymbols", true);
    this.name = "gregorian";
    this.dayNames = new xfa_object.XFAObjectArray(2);
    this.eraNames = null;
    this.meridiemNames = null;
    this.monthNames = new xfa_object.XFAObjectArray(2);
  }
}
class CurrencySymbol extends xfa_object.StringObject {
  constructor(attributes) {
    super(LOCALE_SET_NS_ID, "currencySymbol");
    this.name = (0,utils.getStringOption)(attributes.name, ["symbol", "isoname", "decimal"]);
  }
}
class CurrencySymbols extends xfa_object.XFAObject {
  constructor(attributes) {
    super(LOCALE_SET_NS_ID, "currencySymbols", true);
    this.currencySymbol = new xfa_object.XFAObjectArray(3);
  }
}
class DatePattern extends xfa_object.StringObject {
  constructor(attributes) {
    super(LOCALE_SET_NS_ID, "datePattern");
    this.name = (0,utils.getStringOption)(attributes.name, ["full", "long", "med", "short"]);
  }
}
class DatePatterns extends xfa_object.XFAObject {
  constructor(attributes) {
    super(LOCALE_SET_NS_ID, "datePatterns", true);
    this.datePattern = new xfa_object.XFAObjectArray(4);
  }
}
class DateTimeSymbols extends xfa_object.ContentObject {
  constructor(attributes) {
    super(LOCALE_SET_NS_ID, "dateTimeSymbols");
  }
}
class Day extends xfa_object.StringObject {
  constructor(attributes) {
    super(LOCALE_SET_NS_ID, "day");
  }
}
class DayNames extends xfa_object.XFAObject {
  constructor(attributes) {
    super(LOCALE_SET_NS_ID, "dayNames", true);
    this.abbr = (0,utils.getInteger)({
      data: attributes.abbr,
      defaultValue: 0,
      validate: x => x === 1
    });
    this.day = new xfa_object.XFAObjectArray(7);
  }
}
class Era extends xfa_object.StringObject {
  constructor(attributes) {
    super(LOCALE_SET_NS_ID, "era");
  }
}
class EraNames extends xfa_object.XFAObject {
  constructor(attributes) {
    super(LOCALE_SET_NS_ID, "eraNames", true);
    this.era = new xfa_object.XFAObjectArray(2);
  }
}
class locale_set_Locale extends xfa_object.XFAObject {
  constructor(attributes) {
    super(LOCALE_SET_NS_ID, "locale", true);
    this.desc = attributes.desc || "";
    this.name = "isoname";
    this.calendarSymbols = null;
    this.currencySymbols = null;
    this.datePatterns = null;
    this.dateTimeSymbols = null;
    this.numberPatterns = null;
    this.numberSymbols = null;
    this.timePatterns = null;
    this.typeFaces = null;
  }
}
class locale_set_LocaleSet extends xfa_object.XFAObject {
  constructor(attributes) {
    super(LOCALE_SET_NS_ID, "localeSet", true);
    this.locale = new xfa_object.XFAObjectArray();
  }
}
class Meridiem extends xfa_object.StringObject {
  constructor(attributes) {
    super(LOCALE_SET_NS_ID, "meridiem");
  }
}
class MeridiemNames extends xfa_object.XFAObject {
  constructor(attributes) {
    super(LOCALE_SET_NS_ID, "meridiemNames", true);
    this.meridiem = new xfa_object.XFAObjectArray(2);
  }
}
class Month extends xfa_object.StringObject {
  constructor(attributes) {
    super(LOCALE_SET_NS_ID, "month");
  }
}
class MonthNames extends xfa_object.XFAObject {
  constructor(attributes) {
    super(LOCALE_SET_NS_ID, "monthNames", true);
    this.abbr = (0,utils.getInteger)({
      data: attributes.abbr,
      defaultValue: 0,
      validate: x => x === 1
    });
    this.month = new xfa_object.XFAObjectArray(12);
  }
}
class NumberPattern extends xfa_object.StringObject {
  constructor(attributes) {
    super(LOCALE_SET_NS_ID, "numberPattern");
    this.name = (0,utils.getStringOption)(attributes.name, ["full", "long", "med", "short"]);
  }
}
class NumberPatterns extends xfa_object.XFAObject {
  constructor(attributes) {
    super(LOCALE_SET_NS_ID, "numberPatterns", true);
    this.numberPattern = new xfa_object.XFAObjectArray(4);
  }
}
class NumberSymbol extends xfa_object.StringObject {
  constructor(attributes) {
    super(LOCALE_SET_NS_ID, "numberSymbol");
    this.name = (0,utils.getStringOption)(attributes.name, ["decimal", "grouping", "percent", "minus", "zero"]);
  }
}
class NumberSymbols extends xfa_object.XFAObject {
  constructor(attributes) {
    super(LOCALE_SET_NS_ID, "numberSymbols", true);
    this.numberSymbol = new xfa_object.XFAObjectArray(5);
  }
}
class TimePattern extends xfa_object.StringObject {
  constructor(attributes) {
    super(LOCALE_SET_NS_ID, "timePattern");
    this.name = (0,utils.getStringOption)(attributes.name, ["full", "long", "med", "short"]);
  }
}
class TimePatterns extends xfa_object.XFAObject {
  constructor(attributes) {
    super(LOCALE_SET_NS_ID, "timePatterns", true);
    this.timePattern = new xfa_object.XFAObjectArray(4);
  }
}
class TypeFace extends xfa_object.XFAObject {
  constructor(attributes) {
    super(LOCALE_SET_NS_ID, "typeFace", true);
    this.name = attributes.name | "";
  }
}
class TypeFaces extends xfa_object.XFAObject {
  constructor(attributes) {
    super(LOCALE_SET_NS_ID, "typeFaces", true);
    this.typeFace = new xfa_object.XFAObjectArray();
  }
}
class LocaleSetNamespace {
  static [namespaces.$buildXFAObject](name, attributes) {
    return Object.hasOwn(LocaleSetNamespace, name) ? LocaleSetNamespace[name](attributes) : undefined;
  }
  static calendarSymbols(attrs) {
    return new CalendarSymbols(attrs);
  }
  static currencySymbol(attrs) {
    return new CurrencySymbol(attrs);
  }
  static currencySymbols(attrs) {
    return new CurrencySymbols(attrs);
  }
  static datePattern(attrs) {
    return new DatePattern(attrs);
  }
  static datePatterns(attrs) {
    return new DatePatterns(attrs);
  }
  static dateTimeSymbols(attrs) {
    return new DateTimeSymbols(attrs);
  }
  static day(attrs) {
    return new Day(attrs);
  }
  static dayNames(attrs) {
    return new DayNames(attrs);
  }
  static era(attrs) {
    return new Era(attrs);
  }
  static eraNames(attrs) {
    return new EraNames(attrs);
  }
  static locale(attrs) {
    return new locale_set_Locale(attrs);
  }
  static localeSet(attrs) {
    return new locale_set_LocaleSet(attrs);
  }
  static meridiem(attrs) {
    return new Meridiem(attrs);
  }
  static meridiemNames(attrs) {
    return new MeridiemNames(attrs);
  }
  static month(attrs) {
    return new Month(attrs);
  }
  static monthNames(attrs) {
    return new MonthNames(attrs);
  }
  static numberPattern(attrs) {
    return new NumberPattern(attrs);
  }
  static numberPatterns(attrs) {
    return new NumberPatterns(attrs);
  }
  static numberSymbol(attrs) {
    return new NumberSymbol(attrs);
  }
  static numberSymbols(attrs) {
    return new NumberSymbols(attrs);
  }
  static timePattern(attrs) {
    return new TimePattern(attrs);
  }
  static timePatterns(attrs) {
    return new TimePatterns(attrs);
  }
  static typeFace(attrs) {
    return new TypeFace(attrs);
  }
  static typeFaces(attrs) {
    return new TypeFaces(attrs);
  }
}

;// ./src/core/xfa/signature.js


const SIGNATURE_NS_ID = namespaces.NamespaceIds.signature.id;
class signature_Signature extends xfa_object.XFAObject {
  constructor(attributes) {
    super(SIGNATURE_NS_ID, "signature", true);
  }
}
class SignatureNamespace {
  static [namespaces.$buildXFAObject](name, attributes) {
    return Object.hasOwn(SignatureNamespace, name) ? SignatureNamespace[name](attributes) : undefined;
  }
  static signature(attributes) {
    return new signature_Signature(attributes);
  }
}

;// ./src/core/xfa/stylesheet.js


const STYLESHEET_NS_ID = namespaces.NamespaceIds.stylesheet.id;
class Stylesheet extends xfa_object.XFAObject {
  constructor(attributes) {
    super(STYLESHEET_NS_ID, "stylesheet", true);
  }
}
class StylesheetNamespace {
  static [namespaces.$buildXFAObject](name, attributes) {
    return Object.hasOwn(StylesheetNamespace, name) ? StylesheetNamespace[name](attributes) : undefined;
  }
  static stylesheet(attributes) {
    return new Stylesheet(attributes);
  }
}

;// ./src/core/xfa/xdp.js



const XDP_NS_ID = namespaces.NamespaceIds.xdp.id;
class xdp_Xdp extends xfa_object.XFAObject {
  constructor(attributes) {
    super(XDP_NS_ID, "xdp", true);
    this.uuid = attributes.uuid || "";
    this.timeStamp = attributes.timeStamp || "";
    this.config = null;
    this.connectionSet = null;
    this.datasets = null;
    this.localeSet = null;
    this.stylesheet = new xfa_object.XFAObjectArray();
    this.template = null;
  }
  [symbol_utils.$onChildCheck](child) {
    const ns = namespaces.NamespaceIds[child[symbol_utils.$nodeName]];
    return ns && child[symbol_utils.$namespaceId] === ns.id;
  }
}
class XdpNamespace {
  static [namespaces.$buildXFAObject](name, attributes) {
    return Object.hasOwn(XdpNamespace, name) ? XdpNamespace[name](attributes) : undefined;
  }
  static xdp(attributes) {
    return new xdp_Xdp(attributes);
  }
}

// EXTERNAL MODULE: ./src/core/xfa/xhtml.js
var xhtml = __webpack_require__(407);
;// ./src/core/xfa/setup.js









const NamespaceSetUp = {
  config: ConfigNamespace,
  connection: ConnectionSetNamespace,
  datasets: DatasetsNamespace,
  localeSet: LocaleSetNamespace,
  signature: SignatureNamespace,
  stylesheet: StylesheetNamespace,
  template: TemplateNamespace,
  xdp: XdpNamespace,
  xhtml: xhtml.XhtmlNamespace
};

// EXTERNAL MODULE: ./src/core/xfa/parser_base.js + 2 modules
var parser_base = __webpack_require__(950);
;// ./src/core/xfa/parser.js



class XFAParser extends parser_base.XFAParserBase {
  constructor() {
    super({
      namespaces: NamespaceSetUp,
      templateClass: Template
    });
  }
}

;// ./src/core/xfa_font_widths.js
const XfaFontCodePoints = [32, 95, 161, 12, 174, 210, 402, 1, 506, 6, 536, 4, 710, 2, 713, 1, 728, 6, 900, 7, 908, 1, 910, 20, 931, 44, 1024, 96, 1138, 2, 1168, 2, 7808, 6, 7922, 2, 8208, 2, 8211, 3, 8215, 8, 8224, 3, 8230, 1, 8240, 1, 8242, 2, 8249, 2, 8252, 1, 8254, 1, 8260, 1, 8319, 1, 8355, 2, 8359, 1, 8364, 1, 8453, 1, 8467, 1, 8470, 1, 8482, 1, 8486, 1, 8494, 1, 8539, 4, 8592, 6, 8616, 1, 8706, 1, 8710, 1, 8719, 1, 8721, 2, 8730, 1, 8734, 2, 8745, 1, 8747, 1, 8776, 1, 8800, 2, 8804, 2, 8962, 1, 8976, 1, 8992, 2, 9472, 1, 9474, 1, 9484, 1, 9488, 1, 9492, 1, 9496, 1, 9500, 1, 9508, 1, 9516, 1, 9524, 1, 9532, 1, 9552, 29, 9600, 1, 9604, 1, 9608, 1, 9612, 1, 9616, 4, 9632, 2, 9642, 3, 9650, 1, 9658, 1, 9660, 1, 9668, 1, 9674, 2, 9679, 1, 9688, 2, 9702, 1, 9786, 3, 9792, 1, 9794, 1, 9824, 1, 9827, 1, 9829, 2, 9834, 3, 61441, 2, 61445, 1];
const ArialRegularWidths = [278, 278, 355, 556, 556, 889, 667, 191, 333, 333, 389, 584, 278, 333, 278, 278, 556, 556, 556, 556, 556, 556, 556, 556, 556, 556, 278, 278, 584, 584, 584, 556, 1015, 667, 667, 722, 722, 667, 611, 778, 722, 278, 500, 667, 556, 833, 722, 778, 667, 778, 722, 667, 611, 722, 667, 944, 667, 667, 611, 278, 278, 278, 469, 556, 333, 556, 556, 500, 556, 556, 278, 556, 556, 222, 222, 500, 222, 833, 556, 556, 556, 556, 333, 500, 278, 556, 500, 722, 500, 500, 500, 334, 260, 334, 584, 333, 556, 556, 556, 556, 260, 556, 333, 737, 370, 556, 584, 737, 552, 400, 549, 333, 333, 333, 576, 537, 278, 333, 333, 365, 556, 834, 834, 834, 611, 667, 667, 667, 667, 667, 667, 1000, 722, 667, 667, 667, 667, 278, 278, 278, 278, 722, 722, 778, 778, 778, 778, 778, 584, 778, 722, 722, 722, 722, 667, 667, 611, 556, 556, 556, 556, 556, 556, 889, 500, 556, 556, 556, 556, 278, 278, 278, 278, 556, 556, 556, 556, 556, 556, 556, 549, 611, 556, 556, 556, 556, 500, 556, 500, 667, 556, 667, 556, 667, 556, 722, 500, 722, 500, 722, 500, 722, 500, 722, 615, 722, 556, 667, 556, 667, 556, 667, 556, 667, 556, 667, 556, 778, 556, 778, 556, 778, 556, 778, 556, 722, 556, 722, 556, 278, 278, 278, 278, 278, 278, 278, 222, 278, 278, 735, 444, 500, 222, 667, 500, 500, 556, 222, 556, 222, 556, 292, 556, 334, 556, 222, 722, 556, 722, 556, 722, 556, 604, 723, 556, 778, 556, 778, 556, 778, 556, 1000, 944, 722, 333, 722, 333, 722, 333, 667, 500, 667, 500, 667, 500, 667, 500, 611, 278, 611, 375, 611, 278, 722, 556, 722, 556, 722, 556, 722, 556, 722, 556, 722, 556, 944, 722, 667, 500, 667, 611, 500, 611, 500, 611, 500, 222, 556, 667, 556, 1000, 889, 778, 611, 667, 500, 611, 278, 333, 333, 333, 333, 333, 333, 333, 333, 333, 333, 333, 667, 278, 784, 838, 384, 774, 855, 752, 222, 667, 667, 551, 668, 667, 611, 722, 778, 278, 667, 668, 833, 722, 650, 778, 722, 667, 618, 611, 667, 798, 667, 835, 748, 278, 667, 578, 446, 556, 222, 547, 578, 575, 500, 557, 446, 441, 556, 556, 222, 500, 500, 576, 500, 448, 556, 690, 569, 482, 617, 395, 547, 648, 525, 713, 781, 222, 547, 556, 547, 781, 667, 667, 865, 542, 719, 667, 278, 278, 500, 1057, 1010, 854, 583, 722, 635, 719, 667, 656, 667, 542, 677, 667, 923, 604, 719, 719, 583, 656, 833, 722, 778, 719, 667, 722, 611, 635, 760, 667, 740, 667, 917, 938, 792, 885, 656, 719, 1010, 722, 556, 573, 531, 365, 583, 556, 669, 458, 559, 559, 438, 583, 688, 552, 556, 542, 556, 500, 458, 500, 823, 500, 573, 521, 802, 823, 625, 719, 521, 510, 750, 542, 556, 556, 556, 365, 510, 500, 222, 278, 222, 906, 812, 556, 438, 559, 500, 552, 778, 556, 489, 411, 944, 722, 944, 722, 944, 722, 667, 500, 333, 333, 556, 1000, 1000, 552, 222, 222, 222, 222, 333, 333, 333, 556, 556, 350, 1000, 1000, 188, 354, 333, 333, 500, 333, 167, 365, 556, 556, 1094, 556, 885, 323, 1073, 1000, 768, 600, 834, 834, 834, 834, 1000, 500, 1000, 500, 1000, 500, 500, 494, 612, 823, 713, 584, 549, 713, 979, 719, 274, 549, 549, 583, 549, 549, 604, 584, 604, 604, 708, 625, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 729, 604, 604, 354, 354, 1000, 990, 990, 990, 990, 494, 604, 604, 604, 604, 354, 1021, 1052, 917, 750, 750, 531, 656, 594, 510, 500, 750, 750, 500, 500, 333];
const ArialBoldWidths = [278, 333, 474, 556, 556, 889, 722, 238, 333, 333, 389, 584, 278, 333, 278, 278, 556, 556, 556, 556, 556, 556, 556, 556, 556, 556, 333, 333, 584, 584, 584, 611, 975, 722, 722, 722, 722, 667, 611, 778, 722, 278, 556, 722, 611, 833, 722, 778, 667, 778, 722, 667, 611, 722, 667, 944, 667, 667, 611, 333, 278, 333, 584, 556, 333, 556, 611, 556, 611, 556, 333, 611, 611, 278, 278, 556, 278, 889, 611, 611, 611, 611, 389, 556, 333, 611, 556, 778, 556, 556, 500, 389, 280, 389, 584, 333, 556, 556, 556, 556, 280, 556, 333, 737, 370, 556, 584, 737, 552, 400, 549, 333, 333, 333, 576, 556, 278, 333, 333, 365, 556, 834, 834, 834, 611, 722, 722, 722, 722, 722, 722, 1000, 722, 667, 667, 667, 667, 278, 278, 278, 278, 722, 722, 778, 778, 778, 778, 778, 584, 778, 722, 722, 722, 722, 667, 667, 611, 556, 556, 556, 556, 556, 556, 889, 556, 556, 556, 556, 556, 278, 278, 278, 278, 611, 611, 611, 611, 611, 611, 611, 549, 611, 611, 611, 611, 611, 556, 611, 556, 722, 556, 722, 556, 722, 556, 722, 556, 722, 556, 722, 556, 722, 556, 722, 719, 722, 611, 667, 556, 667, 556, 667, 556, 667, 556, 667, 556, 778, 611, 778, 611, 778, 611, 778, 611, 722, 611, 722, 611, 278, 278, 278, 278, 278, 278, 278, 278, 278, 278, 785, 556, 556, 278, 722, 556, 556, 611, 278, 611, 278, 611, 385, 611, 479, 611, 278, 722, 611, 722, 611, 722, 611, 708, 723, 611, 778, 611, 778, 611, 778, 611, 1000, 944, 722, 389, 722, 389, 722, 389, 667, 556, 667, 556, 667, 556, 667, 556, 611, 333, 611, 479, 611, 333, 722, 611, 722, 611, 722, 611, 722, 611, 722, 611, 722, 611, 944, 778, 667, 556, 667, 611, 500, 611, 500, 611, 500, 278, 556, 722, 556, 1000, 889, 778, 611, 667, 556, 611, 333, 333, 333, 333, 333, 333, 333, 333, 333, 333, 333, 465, 722, 333, 853, 906, 474, 825, 927, 838, 278, 722, 722, 601, 719, 667, 611, 722, 778, 278, 722, 667, 833, 722, 644, 778, 722, 667, 600, 611, 667, 821, 667, 809, 802, 278, 667, 615, 451, 611, 278, 582, 615, 610, 556, 606, 475, 460, 611, 541, 278, 558, 556, 612, 556, 445, 611, 766, 619, 520, 684, 446, 582, 715, 576, 753, 845, 278, 582, 611, 582, 845, 667, 669, 885, 567, 711, 667, 278, 276, 556, 1094, 1062, 875, 610, 722, 622, 719, 722, 719, 722, 567, 712, 667, 904, 626, 719, 719, 610, 702, 833, 722, 778, 719, 667, 722, 611, 622, 854, 667, 730, 703, 1005, 1019, 870, 979, 719, 711, 1031, 719, 556, 618, 615, 417, 635, 556, 709, 497, 615, 615, 500, 635, 740, 604, 611, 604, 611, 556, 490, 556, 875, 556, 615, 581, 833, 844, 729, 854, 615, 552, 854, 583, 556, 556, 611, 417, 552, 556, 278, 281, 278, 969, 906, 611, 500, 615, 556, 604, 778, 611, 487, 447, 944, 778, 944, 778, 944, 778, 667, 556, 333, 333, 556, 1000, 1000, 552, 278, 278, 278, 278, 500, 500, 500, 556, 556, 350, 1000, 1000, 240, 479, 333, 333, 604, 333, 167, 396, 556, 556, 1094, 556, 885, 489, 1115, 1000, 768, 600, 834, 834, 834, 834, 1000, 500, 1000, 500, 1000, 500, 500, 494, 612, 823, 713, 584, 549, 713, 979, 722, 274, 549, 549, 583, 549, 549, 604, 584, 604, 604, 708, 625, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 729, 604, 604, 354, 354, 1000, 990, 990, 990, 990, 494, 604, 604, 604, 604, 354, 1021, 1052, 917, 750, 750, 531, 656, 594, 510, 500, 750, 750, 611, 611, 333];
const ArialItalicWidths = [278, 278, 355, 556, 556, 889, 667, 191, 333, 333, 389, 584, 278, 333, 278, 278, 556, 556, 556, 556, 556, 556, 556, 556, 556, 556, 278, 278, 584, 584, 584, 556, 1015, 667, 667, 722, 722, 667, 611, 778, 722, 278, 500, 667, 556, 833, 722, 778, 667, 778, 722, 667, 611, 722, 667, 944, 667, 667, 611, 278, 278, 278, 469, 556, 333, 556, 556, 500, 556, 556, 278, 556, 556, 222, 222, 500, 222, 833, 556, 556, 556, 556, 333, 500, 278, 556, 500, 722, 500, 500, 500, 334, 260, 334, 584, 333, 556, 556, 556, 556, 260, 556, 333, 737, 370, 556, 584, 737, 552, 400, 549, 333, 333, 333, 576, 537, 278, 333, 333, 365, 556, 834, 834, 834, 611, 667, 667, 667, 667, 667, 667, 1000, 722, 667, 667, 667, 667, 278, 278, 278, 278, 722, 722, 778, 778, 778, 778, 778, 584, 778, 722, 722, 722, 722, 667, 667, 611, 556, 556, 556, 556, 556, 556, 889, 500, 556, 556, 556, 556, 278, 278, 278, 278, 556, 556, 556, 556, 556, 556, 556, 549, 611, 556, 556, 556, 556, 500, 556, 500, 667, 556, 667, 556, 667, 556, 722, 500, 722, 500, 722, 500, 722, 500, 722, 625, 722, 556, 667, 556, 667, 556, 667, 556, 667, 556, 667, 556, 778, 556, 778, 556, 778, 556, 778, 556, 722, 556, 722, 556, 278, 278, 278, 278, 278, 278, 278, 222, 278, 278, 733, 444, 500, 222, 667, 500, 500, 556, 222, 556, 222, 556, 281, 556, 400, 556, 222, 722, 556, 722, 556, 722, 556, 615, 723, 556, 778, 556, 778, 556, 778, 556, 1000, 944, 722, 333, 722, 333, 722, 333, 667, 500, 667, 500, 667, 500, 667, 500, 611, 278, 611, 354, 611, 278, 722, 556, 722, 556, 722, 556, 722, 556, 722, 556, 722, 556, 944, 722, 667, 500, 667, 611, 500, 611, 500, 611, 500, 222, 556, 667, 556, 1000, 889, 778, 611, 667, 500, 611, 278, 333, 333, 333, 333, 333, 333, 333, 333, 333, 333, 333, 667, 278, 789, 846, 389, 794, 865, 775, 222, 667, 667, 570, 671, 667, 611, 722, 778, 278, 667, 667, 833, 722, 648, 778, 725, 667, 600, 611, 667, 837, 667, 831, 761, 278, 667, 570, 439, 555, 222, 550, 570, 571, 500, 556, 439, 463, 555, 542, 222, 500, 492, 548, 500, 447, 556, 670, 573, 486, 603, 374, 550, 652, 546, 728, 779, 222, 550, 556, 550, 779, 667, 667, 843, 544, 708, 667, 278, 278, 500, 1066, 982, 844, 589, 715, 639, 724, 667, 651, 667, 544, 704, 667, 917, 614, 715, 715, 589, 686, 833, 722, 778, 725, 667, 722, 611, 639, 795, 667, 727, 673, 920, 923, 805, 886, 651, 694, 1022, 682, 556, 562, 522, 493, 553, 556, 688, 465, 556, 556, 472, 564, 686, 550, 556, 556, 556, 500, 833, 500, 835, 500, 572, 518, 830, 851, 621, 736, 526, 492, 752, 534, 556, 556, 556, 378, 496, 500, 222, 222, 222, 910, 828, 556, 472, 565, 500, 556, 778, 556, 492, 339, 944, 722, 944, 722, 944, 722, 667, 500, 333, 333, 556, 1000, 1000, 552, 222, 222, 222, 222, 333, 333, 333, 556, 556, 350, 1000, 1000, 188, 354, 333, 333, 500, 333, 167, 365, 556, 556, 1094, 556, 885, 323, 1083, 1000, 768, 600, 834, 834, 834, 834, 1000, 500, 998, 500, 1000, 500, 500, 494, 612, 823, 713, 584, 549, 713, 979, 719, 274, 549, 549, 584, 549, 549, 604, 584, 604, 604, 708, 625, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 729, 604, 604, 354, 354, 1000, 990, 990, 990, 990, 494, 604, 604, 604, 604, 354, 1021, 1052, 917, 750, 750, 531, 656, 594, 510, 500, 750, 750, 500, 500, 333];
const ArialBoldItalicWidths = [278, 333, 474, 556, 556, 889, 722, 238, 333, 333, 389, 584, 278, 333, 278, 278, 556, 556, 556, 556, 556, 556, 556, 556, 556, 556, 333, 333, 584, 584, 584, 611, 975, 722, 722, 722, 722, 667, 611, 778, 722, 278, 556, 722, 611, 833, 722, 778, 667, 778, 722, 667, 611, 722, 667, 944, 667, 667, 611, 333, 278, 333, 584, 556, 333, 556, 611, 556, 611, 556, 333, 611, 611, 278, 278, 556, 278, 889, 611, 611, 611, 611, 389, 556, 333, 611, 556, 778, 556, 556, 500, 389, 280, 389, 584, 333, 556, 556, 556, 556, 280, 556, 333, 737, 370, 556, 584, 737, 552, 400, 549, 333, 333, 333, 576, 556, 278, 333, 333, 365, 556, 834, 834, 834, 611, 722, 722, 722, 722, 722, 722, 1000, 722, 667, 667, 667, 667, 278, 278, 278, 278, 722, 722, 778, 778, 778, 778, 778, 584, 778, 722, 722, 722, 722, 667, 667, 611, 556, 556, 556, 556, 556, 556, 889, 556, 556, 556, 556, 556, 278, 278, 278, 278, 611, 611, 611, 611, 611, 611, 611, 549, 611, 611, 611, 611, 611, 556, 611, 556, 722, 556, 722, 556, 722, 556, 722, 556, 722, 556, 722, 556, 722, 556, 722, 740, 722, 611, 667, 556, 667, 556, 667, 556, 667, 556, 667, 556, 778, 611, 778, 611, 778, 611, 778, 611, 722, 611, 722, 611, 278, 278, 278, 278, 278, 278, 278, 278, 278, 278, 782, 556, 556, 278, 722, 556, 556, 611, 278, 611, 278, 611, 396, 611, 479, 611, 278, 722, 611, 722, 611, 722, 611, 708, 723, 611, 778, 611, 778, 611, 778, 611, 1000, 944, 722, 389, 722, 389, 722, 389, 667, 556, 667, 556, 667, 556, 667, 556, 611, 333, 611, 479, 611, 333, 722, 611, 722, 611, 722, 611, 722, 611, 722, 611, 722, 611, 944, 778, 667, 556, 667, 611, 500, 611, 500, 611, 500, 278, 556, 722, 556, 1000, 889, 778, 611, 667, 556, 611, 333, 333, 333, 333, 333, 333, 333, 333, 333, 333, 333, 333, 722, 333, 854, 906, 473, 844, 930, 847, 278, 722, 722, 610, 671, 667, 611, 722, 778, 278, 722, 667, 833, 722, 657, 778, 718, 667, 590, 611, 667, 822, 667, 829, 781, 278, 667, 620, 479, 611, 278, 591, 620, 621, 556, 610, 479, 492, 611, 558, 278, 566, 556, 603, 556, 450, 611, 712, 605, 532, 664, 409, 591, 704, 578, 773, 834, 278, 591, 611, 591, 834, 667, 667, 886, 614, 719, 667, 278, 278, 556, 1094, 1042, 854, 622, 719, 677, 719, 722, 708, 722, 614, 722, 667, 927, 643, 719, 719, 615, 687, 833, 722, 778, 719, 667, 722, 611, 677, 781, 667, 729, 708, 979, 989, 854, 1000, 708, 719, 1042, 729, 556, 619, 604, 534, 618, 556, 736, 510, 611, 611, 507, 622, 740, 604, 611, 611, 611, 556, 889, 556, 885, 556, 646, 583, 889, 935, 707, 854, 594, 552, 865, 589, 556, 556, 611, 469, 563, 556, 278, 278, 278, 969, 906, 611, 507, 619, 556, 611, 778, 611, 575, 467, 944, 778, 944, 778, 944, 778, 667, 556, 333, 333, 556, 1000, 1000, 552, 278, 278, 278, 278, 500, 500, 500, 556, 556, 350, 1000, 1000, 240, 479, 333, 333, 604, 333, 167, 396, 556, 556, 1104, 556, 885, 516, 1146, 1000, 768, 600, 834, 834, 834, 834, 999, 500, 1000, 500, 1000, 500, 500, 494, 612, 823, 713, 584, 549, 713, 979, 722, 274, 549, 549, 583, 549, 549, 604, 584, 604, 604, 708, 625, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 729, 604, 604, 354, 354, 1000, 990, 990, 990, 990, 494, 604, 604, 604, 604, 354, 1021, 1052, 917, 750, 750, 531, 656, 594, 510, 500, 750, 750, 611, 611, 333];
const CalibriRegularWidths = [278, 325.88, 400.902, 497.909, 506.699, 714.712, 682.134, 220.798, 303.216, 303.216, 497.842, 498.059, 249.663, 333, 252.594, 386.464, 506.699, 506.699, 506.699, 506.699, 506.699, 506.699, 506.699, 506.699, 506.699, 506.699, 267.739, 267.739, 498.059, 498.059, 498.059, 463.254, 893.921, 578.623, 543.952, 533.081, 615.093, 488.284, 459.594, 630.997, 622.906, 252.104, 318.85, 519.54, 420.297, 854.975, 645.36, 662.249, 516.605, 672.993, 542.843, 459.476, 487.431, 641.454, 567.39, 889.796, 519.046, 487.31, 468.386, 306.826, 386.464, 306.826, 497.792, 497.909, 291.009, 478.872, 525.248, 422.85, 525.248, 497.42, 305.361, 470.576, 525.248, 229.319, 239.076, 454.59, 229.319, 798.822, 525.248, 527.199, 525.248, 525.248, 348.624, 391.115, 335.162, 525.248, 451.66, 714.679, 433.105, 452.635, 395.02, 314.468, 460.866, 314.468, 498.059, 325.677, 497.909, 506.699, 497.909, 506.699, 498.495, 497.909, 392.57, 834.682, 402.216, 512.065, 498.059, 506.96, 393.869, 338.948, 498.201, 335.93, 334.465, 291.984, 576, 585.819, 278, 307.123, 246.087, 422.093, 512.065, 636.242, 671.403, 675.306, 463.498, 578.623, 578.623, 578.623, 578.623, 578.623, 578.623, 763.18, 533.081, 488.284, 488.284, 488.284, 488.284, 252.104, 252.104, 252.104, 252.104, 624.364, 645.36, 662.249, 662.249, 662.249, 662.249, 662.249, 498.059, 663.72, 641.454, 641.454, 641.454, 641.454, 487.31, 516.605, 527.482, 478.872, 478.872, 478.872, 478.872, 478.872, 478.872, 772.808, 422.85, 497.42, 497.42, 497.42, 497.42, 229.631, 229.631, 229.631, 229.631, 525.248, 525.248, 527.199, 527.199, 527.199, 527.199, 527.199, 498.201, 529.438, 525.248, 525.248, 525.248, 525.248, 452.635, 525.248, 452.635, 578.623, 478.872, 578.623, 478.872, 578.623, 478.872, 533.081, 422.85, 533.081, 422.85, 533.081, 422.85, 533.081, 422.85, 615.093, 568.592, 624.364, 551.608, 488.284, 497.42, 488.284, 497.42, 488.284, 497.42, 488.284, 497.42, 488.284, 497.42, 630.997, 470.576, 630.997, 470.576, 630.997, 470.576, 778, 556, 622.906, 525.248, 656.096, 532.57, 252.104, 229.631, 252.104, 229.631, 252.104, 229.631, 252.104, 229.319, 252.104, 229.631, 571.396, 468.913, 318.85, 239.076, 667, 500, 454.59, 420.297, 229.319, 556, 222, 422.738, 264.12, 545.747, 373.552, 429.571, 247.372, 645.36, 525.248, 722, 556, 645.36, 525.248, 579.097, 628.294, 525.248, 662.249, 527.199, 662.249, 527.199, 662.249, 527.199, 866.7, 849.751, 542.843, 348.624, 722, 333, 542.843, 348.624, 459.476, 391.115, 459.476, 391.115, 459.476, 391.115, 459.476, 391.115, 611, 278, 487.431, 345.705, 487.431, 342.004, 641.454, 525.248, 641.454, 525.248, 641.454, 525.248, 641.454, 525.248, 641.454, 525.248, 641.454, 525.248, 889.796, 714.679, 487.31, 452.635, 487.31, 468.386, 395.02, 468.386, 395.02, 468.386, 395.02, 242.493, 305.094, 578.623, 478.872, 763.18, 772.808, 663.72, 529.438, 667, 500, 487.431, 335.162, 395.011, 395.011, 333, 380.852, 225.581, 320.792, 312.004, 450.186, 469.227, 316.886, 494.129, 578.196, 252.594, 488.173, 623.128, 252.092, 661.755, 487.034, 663.67, 274.206, 578.623, 543.952, 415.691, 564.48, 488.284, 468.386, 622.906, 662.249, 252.104, 519.54, 572.783, 854.975, 645.36, 491.77, 662.249, 622.906, 516.605, 458.377, 487.431, 487.31, 759.417, 519.046, 749.596, 664.456, 252.104, 487.31, 567.261, 455.772, 536.963, 274.206, 541.628, 567.261, 531.07, 446.29, 523.775, 455.772, 347.72, 536.963, 531.592, 274.206, 454.59, 462.89, 549.642, 448.73, 376.181, 527.199, 552.78, 508.925, 410.703, 531.576, 387.187, 541.628, 650.929, 426.347, 707.631, 696.504, 274.206, 541.628, 527.199, 541.628, 696.504, 488.284, 667, 865, 542, 719, 667, 278, 278, 500, 1057, 1010, 854, 583, 641.454, 635, 719, 667, 656, 667, 542, 677, 667, 923, 604, 719, 719, 583, 656, 833, 722, 778, 719, 667, 722, 611, 635, 760, 667, 740, 667, 917, 938, 792, 885, 656, 719, 1010, 722, 556, 573, 531, 365, 583, 556, 669, 458, 559, 559, 438, 583, 688, 552, 556, 542, 556, 500, 458, 500, 823, 500, 573, 521, 802, 823, 625, 719, 521, 510, 750, 542, 497.42, 556, 556, 365, 510, 500, 222, 278, 222, 906, 812, 556, 438, 540.922, 500, 552, 778, 556, 489, 411, 889.796, 714.679, 889.796, 714.679, 889.796, 714.679, 487.31, 452.635, 333, 333, 497.909, 905.27, 1000, 497.827, 249.324, 249.324, 249.324, 249.324, 418.448, 418.448, 418.448, 497.909, 497.909, 497.907, 690.43, 1038.09, 188, 354, 338.861, 338.861, 557.615, 498.035, 336.442, 362.558, 459.345, 506.699, 922.089, 506.699, 885, 323, 1073, 705.08, 768, 738.648, 665.54, 704.113, 704.113, 645.516, 905.27, 905.275, 905.27, 905.275, 1288.09, 777.345, 472.17, 532.562, 612, 799.084, 541.096, 498.059, 498.201, 853.162, 683.587, 701.902, 365.823, 498.201, 498.201, 497.625, 498.201, 498.201, 875.48, 498.059, 540.036, 540.036, 499.65, 500, 499.65, 499.65, 499.65, 499.65, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 729, 604, 604, 353.511, 353.511, 1000, 990, 990, 990, 990, 510.598, 549.803, 604, 604, 604, 353.511, 1021, 1052, 917, 750, 750, 531, 656, 594, 510, 500, 750, 750, 529.295, 529.295, 333];
const CalibriBoldWidths = [278, 325.677, 438.365, 497.909, 506.699, 728.873, 704.427, 233.602, 311.515, 311.515, 497.842, 498.059, 257.967, 333, 267.253, 429.946, 506.699, 506.699, 506.699, 506.699, 506.699, 506.699, 506.699, 506.699, 506.699, 506.699, 275.874, 275.874, 498.059, 498.059, 498.059, 463.498, 898.346, 605.816, 560.416, 529.175, 630.227, 487.797, 459.105, 637.345, 630.71, 266.763, 330.965, 546.749, 422.965, 874.017, 658.536, 676.417, 532.233, 686.18, 562.857, 472.663, 495.246, 652.681, 591.316, 906.4, 550.789, 519.54, 478.15, 324.698, 429.946, 324.698, 498.059, 497.909, 300.286, 493.517, 536.764, 418.34, 536.764, 503.28, 316.4, 474.246, 536.764, 245.755, 255.526, 479.85, 245.755, 813.328, 536.764, 537.741, 536.764, 536.764, 355.324, 398.819, 346.673, 536.764, 473.017, 745.277, 459.345, 473.501, 397.46, 343.608, 475.462, 343.608, 498.059, 325.677, 497.909, 506.699, 497.909, 506.699, 498.428, 497.909, 414.542, 834.682, 415.884, 538.425, 498.059, 506.96, 389.96, 342.368, 498.201, 337.882, 335.93, 300.776, 576, 597.494, 278, 303.216, 252.434, 434.781, 538.425, 657.726, 690.927, 701.669, 463.498, 605.816, 605.816, 605.816, 605.816, 605.816, 605.816, 775.39, 529.175, 487.797, 487.797, 487.797, 487.797, 266.763, 266.763, 266.763, 266.763, 639.013, 658.536, 676.417, 676.417, 676.417, 676.417, 676.417, 498.059, 680.812, 652.681, 652.681, 652.681, 652.681, 519.54, 532.233, 554.831, 493.517, 493.517, 493.517, 493.517, 493.517, 493.517, 774.764, 418.34, 503.28, 503.28, 503.28, 503.28, 245.755, 245.755, 245.755, 245.755, 536.764, 536.764, 537.741, 537.741, 537.741, 537.741, 537.741, 498.201, 544.089, 536.764, 536.764, 536.764, 536.764, 473.501, 536.764, 473.501, 605.816, 493.517, 605.816, 493.517, 605.816, 493.517, 529.175, 418.34, 529.175, 418.34, 529.175, 418.34, 529.175, 418.34, 630.227, 596.885, 639.013, 568.994, 487.797, 503.28, 487.797, 503.28, 487.797, 503.28, 487.797, 503.28, 487.797, 503.28, 637.345, 474.246, 637.345, 474.246, 637.345, 474.246, 778, 611, 630.71, 536.764, 657.561, 547.505, 266.763, 245.755, 266.763, 245.755, 266.763, 245.755, 266.763, 245.755, 266.763, 245.755, 598.398, 501.329, 330.965, 255.526, 722, 556, 479.85, 422.965, 245.755, 611, 278, 430.291, 305.952, 561.668, 422.358, 432.729, 263.83, 658.536, 536.764, 722, 611, 658.536, 536.764, 621.638, 640.983, 536.764, 676.417, 537.741, 676.417, 537.741, 676.417, 537.741, 874.02, 842.907, 562.857, 355.324, 722, 389, 562.857, 355.324, 472.663, 398.819, 472.663, 398.819, 472.663, 398.819, 472.663, 398.819, 611, 333, 495.246, 363.278, 495.246, 354.485, 652.681, 536.764, 652.681, 536.764, 652.681, 536.764, 652.681, 536.764, 652.681, 536.764, 652.681, 536.764, 906.4, 745.277, 519.54, 473.501, 519.54, 478.15, 397.46, 478.15, 397.46, 478.15, 397.46, 258.457, 316.32, 605.816, 493.517, 775.39, 774.764, 680.812, 544.089, 667, 556, 495.246, 346.673, 401.358, 401.358, 333, 384.758, 235.344, 327.629, 315.421, 444.325, 485.827, 316.886, 494.309, 605.816, 267.572, 487.78, 631.029, 266.81, 676.508, 519.389, 680.749, 286.307, 605.816, 560.416, 409.618, 577.839, 487.797, 478.15, 630.71, 676.417, 266.763, 546.749, 593.757, 874.017, 658.536, 492.531, 676.417, 630.71, 532.233, 469.65, 495.246, 519.54, 801.46, 550.789, 807.536, 680.866, 266.763, 519.54, 591.064, 457.346, 544.089, 286.307, 548.308, 591.064, 545.096, 467.157, 532.323, 457.895, 363.901, 544.089, 545.398, 286.307, 480.31, 486.194, 563.646, 471.06, 384.983, 537.741, 579.387, 530.57, 406.723, 544.368, 392.324, 548.308, 698.891, 450.933, 737.383, 718.005, 286.307, 548.308, 537.741, 548.308, 718.005, 487.797, 669, 885, 567, 711, 667, 278, 276, 556, 1094, 1062, 875, 610, 651.706, 622, 719, 722, 719, 722, 567, 712, 667, 904, 626, 719, 719, 610, 702, 833, 722, 778, 719, 667, 722, 611, 622, 854, 667, 730, 703, 1005, 1019, 870, 979, 719, 711, 1031, 719, 556, 618, 615, 417, 635, 556, 709, 497, 615, 615, 500, 635, 740, 604, 611, 604, 611, 556, 490, 556, 875, 556, 615, 581, 833, 844, 729, 854, 615, 552, 854, 583, 503.28, 556, 611, 417, 552, 556, 278, 281, 278, 969, 906, 611, 500, 556.87, 556, 604, 778, 611, 487, 447, 906.4, 745.277, 906.4, 745.277, 906.4, 745.277, 519.54, 473.501, 333, 333, 497.909, 905.27, 1000, 498.804, 257.967, 257.967, 257.967, 257.967, 435.06, 435.06, 435.06, 497.909, 497.909, 497.907, 711.43, 1061.52, 240, 479, 344.229, 344.229, 586.913, 498.035, 380.877, 371.587, 463.737, 506.699, 920.131, 506.699, 885, 489, 1115, 720.21, 768, 738.648, 696.298, 738.299, 738.299, 679.702, 905.27, 905.275, 905.27, 905.275, 1310.06, 768.555, 472.17, 536.958, 612, 781.998, 550.379, 498.059, 498.201, 838.017, 683.587, 701.495, 375.591, 498.201, 498.201, 497.625, 498.201, 498.201, 875.48, 498.059, 540.036, 540.036, 499.65, 500, 499.65, 499.65, 499.65, 499.65, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 729, 604, 604, 353.511, 353.511, 1000, 990, 990, 990, 990, 533.54, 549.803, 604, 604, 604, 353.511, 1021, 1052, 917, 750, 750, 531, 656, 594, 510, 500, 750, 750, 554.342, 554.342, 333];
const CalibriItalicWidths = [278, 325.88, 400.902, 497.909, 506.699, 714.712, 682.134, 220.798, 303.216, 303.216, 497.842, 498.059, 249.663, 333, 252.594, 387.93, 506.699, 506.699, 506.699, 506.699, 506.699, 506.699, 506.699, 506.699, 506.699, 506.699, 267.739, 267.739, 498.059, 498.059, 498.059, 463.254, 893.921, 578.623, 543.952, 522.338, 615.093, 488.284, 459.594, 630.997, 622.906, 252.104, 318.85, 519.54, 420.297, 854.975, 644.378, 654.438, 516.605, 664.21, 542.843, 452.153, 487.431, 641.454, 567.39, 890.277, 519.046, 487.31, 468.386, 306.826, 384.51, 306.826, 497.792, 497.909, 291.009, 514.022, 514.022, 416.015, 514.022, 477.409, 305.361, 514.022, 514.022, 229.319, 239.076, 454.59, 229.319, 791.008, 514.022, 513.043, 514.022, 514.022, 342.767, 389.16, 335.162, 514.022, 445.8, 714.679, 433.105, 447.265, 395.02, 314.468, 460.866, 314.468, 498.059, 325.677, 497.909, 506.699, 497.909, 506.699, 498.495, 497.909, 392.57, 834.682, 430.528, 512.065, 498.059, 506.96, 393.869, 338.948, 498.201, 335.93, 334.465, 291.984, 576, 585.819, 278, 307.123, 246.087, 422.093, 512.065, 636.242, 671.403, 675.306, 463.498, 578.623, 578.623, 578.623, 578.623, 578.623, 578.623, 763.18, 522.338, 488.284, 488.284, 488.284, 488.284, 252.104, 252.104, 252.104, 252.104, 624.364, 644.378, 654.438, 654.438, 654.438, 654.438, 654.438, 498.059, 657.853, 641.454, 641.454, 641.454, 641.454, 487.31, 516.605, 527.482, 514.022, 514.022, 514.022, 514.022, 514.022, 514.022, 754.254, 416.015, 477.409, 477.409, 477.409, 477.409, 229.631, 229.631, 229.631, 229.631, 525.248, 514.022, 513.043, 513.043, 513.043, 513.043, 513.043, 498.201, 529.438, 514.022, 514.022, 514.022, 514.022, 447.265, 514.022, 447.265, 578.623, 514.022, 578.623, 514.022, 578.623, 514.022, 522.338, 416.015, 522.338, 416.015, 522.338, 416.015, 522.338, 416.015, 615.093, 554.688, 624.364, 550.629, 488.284, 477.409, 488.284, 477.409, 488.284, 477.409, 488.284, 477.409, 488.284, 477.409, 630.997, 514.022, 630.997, 514.022, 630.997, 514.022, 778, 556, 622.906, 514.022, 656.096, 520.366, 252.104, 229.631, 252.104, 229.631, 252.104, 229.631, 252.104, 229.319, 252.104, 229.631, 570.978, 468.913, 318.85, 239.076, 667, 500, 454.59, 420.297, 229.319, 556, 222, 422.738, 263.438, 545.747, 373.628, 429.571, 247.372, 644.378, 514.022, 722, 556, 644.378, 514.022, 569.084, 626.827, 514.022, 654.438, 513.043, 654.438, 513.043, 654.438, 513.043, 866.7, 814.587, 542.843, 342.767, 722, 333, 542.843, 342.767, 452.153, 389.16, 452.153, 389.16, 452.153, 389.16, 452.153, 389.16, 611, 278, 487.431, 345.699, 487.431, 342.004, 641.454, 514.022, 641.454, 514.022, 641.454, 514.022, 641.454, 514.022, 641.454, 514.022, 641.454, 514.022, 890.277, 714.679, 487.31, 447.265, 487.31, 468.386, 395.02, 468.386, 395.02, 468.386, 395.02, 242.493, 305.094, 578.623, 514.022, 763.18, 754.254, 657.853, 529.438, 667, 500, 487.431, 335.162, 395.011, 395.011, 333, 380.852, 223.14, 320.792, 312.004, 450.186, 469.227, 316.886, 494.129, 578.623, 252.594, 488.549, 622.91, 252.165, 654.343, 487.177, 664.144, 274.206, 578.623, 543.952, 415.302, 564.539, 488.284, 468.386, 622.906, 656.391, 252.104, 519.54, 572.76, 854.975, 644.378, 491.365, 654.438, 623.384, 516.605, 458.796, 487.431, 487.31, 759.845, 519.046, 750.393, 664.285, 252.104, 487.31, 564.631, 455.098, 525.224, 274.206, 541.695, 564.631, 527.564, 445.8, 523.752, 455.098, 347.37, 525.224, 558.097, 274.206, 454.59, 462.716, 538.229, 448.73, 376.164, 513.043, 552.797, 509.007, 410.369, 531.713, 387.184, 541.695, 653.467, 426.35, 707.965, 696.457, 274.206, 541.695, 513.043, 541.695, 696.457, 488.284, 667, 843, 544, 708, 667, 278, 278, 500, 1066, 982, 844, 589, 641.305, 639, 724, 667, 651, 667, 544, 704, 667, 917, 614, 715, 715, 589, 686, 833, 722, 778, 725, 667, 722, 611, 639, 795, 667, 727, 673, 920, 923, 805, 886, 651, 694, 1022, 682, 556, 562, 522, 493, 553, 556, 688, 465, 556, 556, 472, 564, 686, 550, 556, 556, 556, 500, 833, 500, 835, 500, 572, 518, 830, 851, 621, 736, 526, 492, 752, 534, 477.409, 556, 556, 378, 496, 500, 222, 222, 222, 910, 828, 556, 472, 513.771, 500, 556, 778, 556, 492, 339, 890.277, 714.679, 890.277, 714.679, 890.277, 714.679, 487.31, 447.265, 333, 333, 497.909, 905.27, 1000, 497.827, 249.324, 249.324, 249.324, 249.324, 418.448, 418.448, 418.448, 497.909, 497.909, 497.907, 689.94, 1038.09, 188, 354, 338.861, 338.861, 557.615, 498.035, 336.442, 357.182, 459.345, 506.699, 912.801, 506.699, 885, 323, 1083, 705.08, 768, 738.648, 665.54, 704.113, 704.113, 645.516, 905.72, 905.275, 905.675, 905.275, 1288.09, 777.345, 472.17, 532.562, 612, 799.084, 541.096, 498.059, 498.201, 853.162, 683.587, 701.902, 365.823, 498.201, 498.201, 498.059, 498.201, 498.201, 875.48, 498.059, 540.036, 540.036, 499.65, 500, 499.65, 499.65, 499.65, 499.65, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 729, 604, 604, 353.511, 353.511, 1000, 990, 990, 990, 990, 510.598, 549.803, 604, 604, 604, 353.511, 1021, 1052, 917, 750, 750, 531, 656, 594, 510, 500, 750, 750, 529.295, 529.295, 333];
const CalibriBoldItalicWidths = [278, 325.677, 438.365, 497.909, 506.699, 728.873, 704.427, 233.602, 311.515, 311.515, 497.842, 498.059, 257.967, 333, 267.253, 434.344, 506.699, 506.699, 506.699, 506.699, 506.699, 506.699, 506.699, 506.699, 506.699, 506.699, 275.874, 275.874, 498.059, 498.059, 498.059, 463.498, 898.346, 605.816, 560.416, 518.432, 630.227, 487.797, 459.105, 637.345, 630.71, 266.763, 330.965, 546.749, 422.965, 874.017, 656.096, 668.598, 532.233, 677.389, 562.857, 465.339, 495.246, 652.681, 591.316, 906.882, 550.789, 519.54, 478.15, 324.698, 425.062, 324.698, 498.059, 497.909, 300.286, 527.688, 527.971, 411.507, 527.971, 491.076, 316.4, 527.971, 527.482, 245.755, 255.526, 479.85, 245.755, 803.567, 527.482, 527.482, 527.971, 527.971, 351.905, 393.937, 346.673, 527.482, 469.108, 745.277, 459.345, 470.087, 397.46, 343.608, 475.462, 343.608, 498.059, 325.677, 497.909, 506.699, 497.909, 506.699, 498.428, 497.909, 414.542, 834.682, 440.777, 538.425, 498.059, 506.96, 389.96, 342.368, 498.201, 337.882, 335.93, 300.776, 576, 597.494, 278, 303.216, 252.434, 434.781, 538.425, 657.726, 690.927, 701.669, 463.498, 605.816, 605.816, 605.816, 605.816, 605.816, 605.816, 775.39, 518.432, 487.797, 487.797, 487.797, 487.797, 266.763, 266.763, 266.763, 266.763, 639.013, 656.096, 668.598, 668.598, 668.598, 668.598, 668.598, 498.059, 677.389, 652.681, 652.681, 652.681, 652.681, 519.54, 532.233, 554.831, 527.688, 527.688, 527.688, 527.688, 527.688, 527.688, 763.535, 411.507, 491.076, 491.076, 491.076, 491.076, 245.755, 245.755, 245.755, 245.755, 536.764, 527.482, 527.482, 527.482, 527.482, 527.482, 527.482, 498.201, 544.089, 527.482, 527.482, 527.482, 527.482, 470.087, 527.971, 470.087, 605.816, 527.688, 605.816, 527.688, 605.816, 527.688, 518.432, 411.507, 518.432, 411.507, 518.432, 411.507, 518.432, 411.507, 630.227, 588.581, 639.013, 566.556, 487.797, 491.076, 487.797, 491.076, 487.797, 491.076, 487.797, 491.076, 487.797, 491.076, 637.345, 527.971, 637.345, 527.971, 637.345, 527.971, 778, 611, 630.71, 527.482, 657.561, 536.275, 266.763, 245.755, 266.763, 245.755, 266.763, 245.755, 266.763, 245.755, 266.763, 245.755, 597.972, 501.329, 330.965, 255.526, 722, 556, 479.85, 422.965, 245.755, 611, 278, 430.291, 306.156, 561.668, 422.358, 432.729, 263.83, 656.096, 527.482, 722, 611, 656.096, 527.482, 615.294, 637.079, 527.482, 668.598, 527.482, 668.598, 527.482, 668.598, 527.482, 874.02, 817.023, 562.857, 351.905, 722, 389, 562.857, 351.905, 465.339, 393.937, 465.339, 393.937, 465.339, 393.937, 465.339, 393.937, 611, 333, 495.246, 363.278, 495.246, 354.485, 652.681, 527.482, 652.681, 527.482, 652.681, 527.482, 652.681, 527.482, 652.681, 527.482, 652.681, 527.482, 906.882, 745.277, 519.54, 470.087, 519.54, 478.15, 397.46, 478.15, 397.46, 478.15, 397.46, 258.457, 316.32, 605.816, 527.688, 775.39, 763.535, 677.389, 544.089, 667, 556, 495.246, 346.673, 401.358, 401.358, 333, 384.758, 233.393, 327.629, 315.421, 444.325, 485.827, 316.886, 494.129, 605.816, 267.572, 487.788, 631.029, 266.521, 668.271, 519.433, 680.531, 286.307, 605.816, 560.416, 409.761, 577.302, 487.797, 478.15, 630.71, 672.511, 266.763, 546.749, 593.757, 874.017, 656.096, 492.021, 668.598, 631.057, 532.233, 469.463, 495.246, 519.54, 801.014, 550.789, 807.993, 680.868, 266.763, 519.54, 584.362, 457.981, 534.808, 286.307, 548.052, 584.362, 542.822, 467.157, 532.408, 457.981, 364.479, 534.808, 570.215, 286.307, 480.047, 486.194, 553.687, 471.06, 385.506, 527.482, 579.169, 529.865, 406.565, 544.785, 392.873, 548.052, 699.6, 450.967, 737.844, 717.365, 286.307, 548.052, 527.482, 548.052, 717.365, 487.797, 667, 886, 614, 719, 667, 278, 278, 556, 1094, 1042, 854, 622, 651.637, 677, 719, 722, 708, 722, 614, 722, 667, 927, 643, 719, 719, 615, 687, 833, 722, 778, 719, 667, 722, 611, 677, 781, 667, 729, 708, 979, 989, 854, 1000, 708, 719, 1042, 729, 556, 619, 604, 534, 618, 556, 736, 510, 611, 611, 507, 622, 740, 604, 611, 611, 611, 556, 889, 556, 885, 556, 646, 583, 889, 935, 707, 854, 594, 552, 865, 589, 491.076, 556, 611, 469, 563, 556, 278, 278, 278, 969, 906, 611, 507, 527.227, 556, 611, 778, 611, 575, 467, 906.882, 745.277, 906.882, 745.277, 906.882, 745.277, 519.54, 470.087, 333, 333, 497.909, 905.27, 1000, 498.804, 257.967, 257.967, 257.967, 249.663, 435.06, 435.06, 435.06, 497.909, 497.909, 497.907, 710.94, 1061.52, 240, 479, 344.229, 344.229, 586.913, 498.035, 380.877, 368.169, 463.737, 506.699, 919.919, 506.699, 885, 516, 1146, 720.21, 768, 738.648, 696.298, 738.299, 738.299, 679.702, 905.254, 905.275, 905.27, 905.275, 1310.7, 768.555, 472.17, 536.958, 612, 781.998, 550.379, 498.059, 498.201, 838.017, 683.587, 701.495, 375.591, 498.201, 498.201, 497.625, 498.201, 498.201, 875.48, 498.059, 540.036, 540.036, 499.65, 500, 499.65, 499.65, 499.65, 499.65, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 729, 604, 604, 353.511, 353.511, 1000, 990, 990, 990, 990, 533.54, 549.803, 604, 604, 604, 353.511, 1021, 1052, 917, 750, 750, 531, 656, 594, 510, 500, 750, 750, 554.342, 554.342, 333];
const HelveticaRegularWidths = [278.167, 278.167, 355.021, 555.85, 555.85, 888.84, 667.007, 191.082, 332.993, 332.993, 388.841, 584.018, 278.167, 332.993, 278.167, 278.167, 555.85, 555.85, 555.85, 555.85, 555.85, 555.85, 555.85, 555.85, 555.85, 555.85, 278.167, 278, 584.018, 584.018, 584.018, 555.85, 1014.868, 667.007, 667.007, 721.834, 721.834, 667.007, 611.159, 778.171, 721.834, 278.167, 500, 667.007, 555.85, 832.992, 721.834, 778.171, 667.007, 778.171, 721.834, 667.007, 611.159, 721.834, 667.007, 944.151, 667.007, 667.007, 611.159, 278.167, 278.167, 278.167, 468.761, 555.85, 332.993, 555.85, 555.85, 500, 555.85, 555.85, 278.167, 555.85, 555.85, 221.831, 221.831, 500, 221.831, 832.992, 555.85, 555.85, 555.85, 555.85, 332.993, 500, 278.167, 555.85, 500, 721.834, 500, 500, 500, 334.017, 260.234, 334.017, 584.018, 332.993, 555.85, 555.85, 555.85, 555.85, 260.234, 555.85, 332.993, 737.184, 369.882, 555.85, 584.018, 737.184, 332.85, 400.096, 584.185, 333, 333, 332.993, 576, 536.893, 278.167, 332.993, 333, 364.766, 555.85, 834.017, 834.017, 834.017, 611.159, 667.007, 667.007, 667.007, 667.007, 667.007, 667.007, 1000, 721.834, 667.007, 667.007, 667.007, 667.007, 278.167, 278.167, 278.167, 278.167, 721.834, 721.834, 778.171, 778.171, 778.171, 778.171, 778.171, 584.018, 778.171, 721.834, 721.834, 721.834, 721.834, 667.007, 667.007, 611.159, 555.85, 555.85, 555.85, 555.85, 555.85, 555.85, 888.84, 500, 555.85, 555.85, 555.85, 555.85, 278.167, 278.167, 278.167, 278.167, 555.85, 555.85, 555.85, 555.85, 555.85, 555.85, 555.85, 584.185, 611.159, 555.85, 555.85, 555.85, 555.85, 500, 555.85, 500, 667.007, 555.85, 667.007, 555.85, 667.007, 555.85, 721.834, 500, 721.834, 500, 721.834, 500, 721.834, 500, 721.834, 643.265, 721.834, 555.85, 667.007, 555.85, 667.007, 555.85, 667.007, 555.85, 667.007, 555.85, 667.007, 555.85, 778.171, 555.85, 778.171, 555.85, 778.171, 555.85, 778.171, 555.85, 721.834, 555.85, 721.834, 555.85, 278.167, 278.167, 278.167, 278.167, 278.167, 278.167, 278.167, 221.831, 278.167, 278.167, 735.14, 444.151, 500, 221.831, 667.007, 500, 500, 555.85, 221.831, 555.85, 221.831, 555.85, 299.51, 555.85, 334.017, 555.85, 221.831, 721.834, 555.85, 721.834, 555.85, 721.834, 555.85, 603.994, 722.855, 555.85, 778.171, 555.85, 778.171, 555.85, 778.171, 555.85, 1000, 944.151, 721.834, 332.993, 721.834, 332.993, 721.834, 332.993, 667.007, 500, 667.007, 500, 667.007, 500, 667.007, 500, 611.159, 278.167, 611.159, 316.999, 611.159, 278.167, 721.834, 555.85, 721.834, 555.85, 721.834, 555.85, 721.834, 555.85, 721.834, 555.85, 721.834, 555.85, 944.151, 721.834, 667.007, 500, 667.007, 611.159, 500, 611.159, 500, 611.159, 500, 221.831, 555.85, 667.007, 555.85, 1000, 888.84, 778.171, 611.159, 667.007, 500, 611.159, 278.167, 332.993, 332.993, 332.993, 332.993, 332.993, 332.993, 332.993, 332.993, 332.993, 332.993, 332.993, 666.52, 278, 783.82, 838.109, 384.211, 773.59, 854.53, 751.556, 221.831, 667.007, 667.007, 551.22, 612.028, 667.007, 611.159, 721.834, 778.171, 278.167, 667.007, 668.033, 832.992, 721.834, 650.098, 778.171, 721.834, 667.007, 617.833, 611.159, 667.007, 798.152, 667.007, 834.549, 748, 278.167, 667.007, 577.873, 446.201, 555.85, 221.831, 547.126, 577.873, 574.805, 500, 557.362, 446.201, 441.084, 555.85, 555.85, 221.831, 500, 500, 555.834, 500, 448.246, 555.85, 690.055, 569.154, 482, 616.815, 394.98, 547.126, 647.566, 525.1, 713.107, 781.242, 221.831, 547.126, 555.85, 547.126, 781.242, 667.007, 666.52, 865.251, 542.499, 719.252, 667.007, 278.167, 278.167, 500, 1056.873, 1009.748, 854, 583.478, 718.83, 634.746, 719.252, 667.007, 655.751, 667.007, 542.499, 676.756, 667.007, 922.658, 603.994, 719.252, 719.252, 583.478, 655.751, 832.992, 721.834, 778.171, 719.252, 667.007, 721.834, 611.159, 634.746, 759.749, 667.007, 740.252, 667.494, 917.495, 938.497, 792.499, 884.743, 655.751, 719.252, 1009.748, 721.834, 555.85, 573.246, 530.75, 365.256, 582.504, 555.85, 669.054, 457.505, 559.408, 559.408, 438.499, 582.504, 688.502, 551.752, 555.85, 542.499, 555.85, 500, 457.991, 500, 823.247, 500, 573.246, 521.005, 801.751, 823.247, 625, 719.252, 521.005, 509.745, 750, 542.499, 555.85, 555.85, 555.85, 365.256, 509.745, 500, 221.831, 278.167, 221.831, 905.746, 812.503, 555.85, 438.499, 559.408, 500, 551.752, 778, 556, 489.23, 410.868, 944.151, 721.834, 944.151, 721.834, 944.151, 721.834, 667.007, 500, 333, 333, 555.85, 1000, 1000, 551.752, 221.831, 221.831, 221.831, 221.831, 332.993, 332.993, 332.993, 555.85, 555.85, 349.902, 1000, 1000, 188.502, 353.996, 332.993, 332.993, 500, 332.993, 278.013, 365, 555.85, 555.85, 1094.252, 555.85, 884.743, 322.758, 1073.247, 1000, 767.931, 599.904, 834.017, 834.017, 834.017, 834.017, 1000, 500, 1000, 500, 1000, 500, 500, 475.865, 612, 995.707, 996.154, 584.018, 453.145, 713.107, 979, 719.252, 274.074, 549.17, 549.17, 582.504, 549.17, 549.17, 603.994, 584.018, 603.994, 603.994, 999.3, 1000, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 995.299, 995.299, 995.996, 603.994, 604, 353.508, 353.508, 1000, 990.257, 990.257, 990.257, 990.257, 470.866, 603.994, 603.994, 603.994, 603.994, 497.31, 1021, 1051.758, 917.495, 750, 750, 530.75, 655.751, 594.249, 509.745, 500, 750, 750, 500, 500, 333];
const HelveticaBoldWidths = [278.167, 332.993, 473.877, 555.85, 555.85, 888.84, 721.834, 238.207, 332.993, 332.993, 388.841, 584.018, 278.167, 332.993, 278.167, 278.167, 555.85, 555.85, 555.85, 555.85, 555.85, 555.85, 555.85, 555.85, 555.85, 555.85, 332.993, 333, 584.018, 584.018, 584.018, 611.159, 974.903, 721.834, 721.834, 721.834, 721.834, 667.007, 611.159, 778.171, 721.834, 278.167, 555.85, 721.834, 611.159, 832.992, 721.834, 778.171, 667.007, 778.171, 721.834, 667.007, 611.159, 721.834, 667.007, 944.151, 667.007, 667.007, 611.159, 332.993, 278.167, 332.993, 584.018, 555.85, 332.993, 555.85, 611.159, 555.85, 611.159, 555.85, 332.993, 611.159, 611.159, 278.167, 278.167, 555.85, 278.167, 888.84, 611.159, 611.159, 611.159, 611.159, 388.841, 555.85, 332.993, 611.159, 555.85, 778.171, 555.85, 555.85, 500, 388.841, 280.216, 388.841, 584.018, 332.993, 555.85, 555.85, 555.85, 555.85, 280.216, 555.85, 332.993, 737.184, 369.882, 555.85, 584.018, 737.184, 332.85, 400.096, 584.185, 333, 333, 332.993, 576, 555.85, 278.167, 332.993, 333, 364.766, 555.85, 834.017, 834.017, 834.017, 611.159, 721.834, 721.834, 721.834, 721.834, 721.834, 721.834, 1000, 721.834, 667.007, 667.007, 667.007, 667.007, 278.167, 278.167, 278.167, 278.167, 721.834, 721.834, 778.171, 778.171, 778.171, 778.171, 778.171, 584.018, 778.171, 721.834, 721.834, 721.834, 721.834, 667.007, 667.007, 611.159, 555.85, 555.85, 555.85, 555.85, 555.85, 555.85, 888.84, 555.85, 555.85, 555.85, 555.85, 555.85, 278.167, 278.167, 278.167, 278.167, 611.159, 611.159, 611.159, 611.159, 611.159, 611.159, 611.159, 584.185, 611.159, 611.159, 611.159, 611.159, 611.159, 555.85, 611.159, 555.85, 721.834, 555.85, 721.834, 555.85, 721.834, 555.85, 721.834, 555.85, 721.834, 555.85, 721.834, 555.85, 721.834, 555.85, 721.834, 743.259, 721.834, 611.159, 667.007, 555.85, 667.007, 555.85, 667.007, 555.85, 667.007, 555.85, 667.007, 555.85, 778.171, 611.159, 778.171, 611.159, 778.171, 611.159, 778.171, 611.159, 721.834, 611.159, 721.834, 611.159, 278.167, 278.167, 278.167, 278.167, 278.167, 278.167, 278.167, 278.167, 278.167, 278.167, 785.33, 555.85, 555.85, 278.167, 721.834, 555.85, 555.85, 611.159, 278.167, 611.159, 278.167, 611.159, 399.738, 611.159, 478.995, 611.159, 278.167, 721.834, 611.159, 721.834, 611.159, 721.834, 611.159, 707.504, 722.855, 611.159, 778.171, 611.159, 778.171, 611.159, 778.171, 611.159, 1000, 944.151, 721.834, 388.841, 721.834, 388.841, 721.834, 388.841, 667.007, 555.85, 667.007, 555.85, 667.007, 555.85, 667.007, 555.85, 611.159, 332.993, 611.159, 388.996, 611.159, 332.993, 721.834, 611.159, 721.834, 611.159, 721.834, 611.159, 721.834, 611.159, 721.834, 611.159, 721.834, 611.159, 944.151, 778.171, 667.007, 555.85, 667.007, 611.159, 500, 611.159, 500, 611.159, 500, 278.167, 555.85, 721.834, 555.85, 1000, 888.84, 778.171, 611.159, 667.007, 555.85, 611.159, 332.993, 332.993, 332.993, 332.993, 332.993, 332.993, 332.993, 332.993, 332.993, 332.993, 332.993, 465.158, 721.834, 333, 852.974, 906.236, 474.37, 825.297, 926.75, 838.109, 278.167, 721.834, 721.834, 600.928, 612.214, 667.007, 611.159, 721.834, 778.171, 278.167, 721.834, 667.007, 832.992, 721.834, 644.444, 778.171, 721.834, 667.007, 599.904, 611.159, 667.007, 821.197, 667.007, 808.919, 802, 278.167, 667.007, 615.252, 450.829, 611.159, 278.167, 581.971, 615.252, 609.646, 555.85, 605.558, 475.39, 459.549, 611.159, 541.471, 278.167, 558.385, 555.85, 611.186, 555.85, 444.689, 611.159, 766.375, 619.347, 520, 683.918, 445.71, 581.971, 714.671, 576.317, 752.586, 845.27, 278.167, 581.971, 611.159, 581.971, 845.27, 667.007, 669.054, 884.743, 567.108, 710.573, 667.007, 278.167, 276.121, 555.85, 1094.252, 1062.499, 875, 609.646, 718.83, 621.932, 719.252, 721.834, 719.252, 721.834, 567.108, 711.601, 667.007, 904.19, 625.537, 719.252, 719.252, 609.646, 702.337, 832.992, 721.834, 778.171, 719.252, 667.007, 721.834, 611.159, 621.932, 854.487, 667.007, 729.533, 703.366, 1005.121, 1018.959, 870.374, 979, 719.252, 710.573, 1030.753, 719.252, 555.85, 618.321, 615.252, 417.496, 635.235, 555.85, 709.014, 496.93, 615.252, 615.252, 499.51, 634.746, 740.252, 603.994, 611.159, 603.994, 611.159, 555.85, 490.255, 555.85, 875, 555.85, 615.252, 581.436, 832.5, 844.253, 728.993, 854, 615.252, 551.752, 854, 582.504, 555.85, 555.85, 611.159, 417.496, 551.752, 555.85, 278.167, 280.75, 278.167, 969.252, 905.746, 611.159, 499.51, 615.252, 555.85, 603.994, 778, 611, 487.185, 447.224, 944.151, 778.171, 944.151, 778.171, 944.151, 778.171, 667.007, 555.85, 333, 333, 555.85, 1000, 1000, 551.752, 278.167, 278.167, 278.167, 278.167, 500, 500, 500, 555.85, 555.85, 349.902, 1000, 1000, 240.254, 478.995, 332.993, 332.993, 603.994, 332.993, 278.013, 396, 555.85, 555.85, 1094.252, 555.85, 884.743, 489.23, 1115.256, 1000, 767.931, 599.904, 834.017, 834.017, 834.017, 834.017, 1000, 500, 1000, 500, 1000, 500, 500, 493.862, 612, 995.707, 996.154, 584.018, 549.17, 713.107, 979, 718.83, 274.074, 549.17, 549.17, 582.504, 549.17, 549.17, 603.994, 584.018, 603.994, 603.994, 999.3, 1000, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 995.299, 995.299, 995.996, 603.994, 604, 353.508, 353.508, 1000, 990.257, 990.257, 990.257, 990.257, 493.862, 603.994, 603.994, 603.994, 603.994, 497.31, 1021, 1051.758, 917.495, 750, 750, 530.75, 655.751, 594.249, 509.745, 500, 750, 750, 611, 611, 333];
const HelveticaItalicWidths = [278.167, 278.167, 355.021, 555.85, 555.85, 888.84, 667.007, 191.082, 332.993, 332.993, 388.841, 584.018, 278.167, 332.993, 278.167, 278.167, 555.85, 555.85, 555.85, 555.85, 555.85, 555.85, 555.85, 555.85, 555.85, 555.85, 278.167, 278, 584.018, 584.018, 584.018, 555.85, 1014.868, 667.007, 667.007, 721.834, 721.834, 667.007, 611.159, 778.171, 721.834, 278.167, 500, 667.007, 555.85, 832.992, 721.834, 778.171, 667.007, 778.171, 721.834, 667.007, 611.159, 721.834, 667.007, 944.151, 667.007, 667.007, 611.159, 278.167, 278.167, 278.167, 468.761, 555.85, 332.993, 555.85, 555.85, 500, 555.85, 555.85, 278.167, 555.85, 555.85, 221.831, 221.831, 500, 221.831, 832.992, 555.85, 555.85, 555.85, 555.85, 332.993, 500, 278.167, 555.85, 500, 721.834, 500, 500, 500, 334.017, 260.234, 334.017, 584.018, 332.993, 555.85, 555.85, 555.85, 555.85, 260.234, 555.85, 332.993, 737.184, 369.882, 555.85, 584.018, 737.184, 332.85, 400.096, 584.185, 333, 333, 332.993, 576, 536.893, 278.167, 332.993, 333, 364.766, 555.85, 834.017, 834.017, 834.017, 611.159, 667.007, 667.007, 667.007, 667.007, 667.007, 667.007, 1000, 721.834, 667.007, 667.007, 667.007, 667.007, 278.167, 278.167, 278.167, 278.167, 721.834, 721.834, 778.171, 778.171, 778.171, 778.171, 778.171, 584.018, 778.171, 721.834, 721.834, 721.834, 721.834, 667.007, 667.007, 611.159, 555.85, 555.85, 555.85, 555.85, 555.85, 555.85, 888.84, 500, 555.85, 555.85, 555.85, 555.85, 278.167, 278.167, 278.167, 278.167, 555.85, 555.85, 555.85, 555.85, 555.85, 555.85, 555.85, 584.185, 611.159, 555.85, 555.85, 555.85, 555.85, 500, 555.85, 500, 667.007, 555.85, 667.007, 555.85, 667.007, 555.85, 721.834, 500, 721.834, 500, 721.834, 500, 721.834, 500, 721.834, 643, 721.834, 555.85, 667.007, 555.85, 667.007, 555.85, 667.007, 555.85, 667.007, 555.85, 667.007, 555.85, 778.171, 555.85, 778.171, 555.85, 778.171, 555.85, 778.171, 555.85, 721.834, 555.85, 721.834, 555.85, 278.167, 278.167, 278.167, 278.167, 278.167, 278.167, 278.167, 221.831, 278.167, 278.167, 732.604, 444.151, 500, 221.831, 667.007, 500, 500, 555.85, 221.831, 555.85, 221.831, 555.85, 298.734, 555.85, 400.096, 555.85, 221.831, 721.834, 555.85, 721.834, 555.85, 721.834, 555.85, 615.252, 722.855, 555.85, 778.171, 555.85, 778.171, 555.85, 778.171, 555.85, 1000, 944.151, 721.834, 332.993, 721.834, 332.993, 721.834, 332.993, 667.007, 500, 667.007, 500, 667.007, 500, 667.007, 500, 611.159, 278.167, 611.159, 316.996, 611.159, 278.167, 721.834, 555.85, 721.834, 555.85, 721.834, 555.85, 721.834, 555.85, 721.834, 555.85, 721.834, 555.85, 944.151, 721.834, 667.007, 500, 667.007, 611.159, 500, 611.159, 500, 611.159, 500, 221.831, 555.85, 667.007, 555.85, 1000, 888.84, 778.171, 611.159, 667.007, 500, 611.159, 278.167, 332.993, 332.993, 332.993, 332.993, 332.993, 332.993, 332.993, 332.993, 332.993, 332.993, 332.993, 667.007, 278, 789.426, 845.805, 389.327, 794.056, 864.766, 775.101, 221.831, 667.007, 667.007, 569.687, 612.093, 667.007, 611.159, 721.834, 778.171, 278.167, 667.007, 667.007, 832.992, 721.834, 647.566, 778.171, 725.392, 667.007, 600.39, 611.159, 667.007, 837.084, 667.007, 831.432, 761, 278.167, 667.007, 569.687, 438.548, 554.822, 221.831, 550.198, 569.687, 570.709, 500, 556.334, 438.548, 462.62, 554.822, 541.518, 221.831, 500, 491.813, 556.149, 500, 447.224, 555.85, 670.08, 573.246, 486, 602.97, 373.978, 550.198, 651.654, 546.104, 728.459, 779.195, 221.831, 550.198, 555.85, 550.198, 779.195, 667.007, 667.007, 843.228, 544.054, 708.481, 667.007, 278.167, 278.167, 500, 1066.085, 981.578, 843.764, 588.647, 714.671, 638.84, 724.369, 667.007, 651.124, 667.007, 544.054, 703.901, 667.007, 917.009, 614.233, 714.671, 714.671, 588.647, 686.453, 832.992, 721.834, 778.171, 725.392, 667.007, 721.834, 611.159, 638.84, 795.079, 667.007, 726.949, 672.664, 919.586, 923.148, 804.823, 885.77, 651.124, 694.153, 1021.54, 682.361, 555.85, 562.5, 522.026, 492.837, 553.265, 555.85, 688.014, 465.158, 555.85, 555.85, 471.83, 564.034, 686.453, 549.709, 555.85, 549.851, 555.85, 500, 374, 500, 834.549, 500, 572.223, 518.425, 829.925, 850.923, 620.907, 736.162, 526.121, 492.3, 752.045, 533.818, 555.85, 555.85, 555.85, 378.072, 496.397, 500, 221.831, 221.831, 221.831, 909.845, 828.364, 555.85, 471.83, 555.576, 500, 555.85, 778, 556, 491.813, 338.644, 944.151, 721.834, 944.151, 721.834, 944.151, 721.834, 667.007, 500, 333, 333, 555.85, 1000, 1000, 551.752, 221.831, 221.831, 221.831, 221.831, 332.993, 332.993, 332.993, 555.85, 555.85, 349.902, 1000, 1000, 188.502, 353.996, 332.993, 332.993, 500, 332.993, 278.013, 365, 555.85, 555.85, 1094.252, 555.85, 888.744, 322.758, 1082.989, 1000, 767.931, 599.904, 834.017, 834.017, 834.017, 834.017, 1000.49, 500, 1000.445, 500, 1000, 500, 500, 475.865, 612, 995.707, 996.154, 584.018, 453.145, 713.107, 979, 719.252, 274.074, 549.17, 549.17, 584.018, 549.17, 549.17, 603.994, 584.018, 603.994, 603.994, 999.3, 1000, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 995.299, 995.299, 995.996, 603.994, 604, 353.508, 353.508, 1000, 990.257, 990.257, 990.257, 990.257, 470.866, 603.994, 603.994, 603.994, 603.994, 497.31, 1021, 1051.758, 917.495, 750, 750, 530.75, 655.751, 594.249, 509.745, 500, 750, 750, 500, 500, 333];
const HelveticaBoldItalicWidths = [278.167, 332.993, 473.877, 555.85, 555.85, 888.84, 721.834, 238.207, 332.993, 332.993, 388.841, 584.018, 278.167, 332.993, 278.167, 278.167, 555.85, 555.85, 555.85, 555.85, 555.85, 555.85, 555.85, 555.85, 555.85, 555.85, 332.993, 333, 584.018, 584.018, 584.018, 611.159, 974.903, 721.834, 721.834, 721.834, 721.834, 667.007, 611.159, 778.171, 721.834, 278.167, 555.85, 721.834, 611.159, 832.992, 721.834, 778.171, 667.007, 778.171, 721.834, 667.007, 611.159, 721.834, 667.007, 944.151, 667.007, 667.007, 611.159, 332.993, 278.167, 332.993, 584.018, 555.85, 332.993, 555.85, 611.159, 555.85, 611.159, 555.85, 332.993, 611.159, 611.159, 278.167, 278.167, 555.85, 278.167, 888.84, 611.159, 611.159, 611.159, 611.159, 388.841, 555.85, 332.993, 611.159, 555.85, 778.171, 555.85, 555.85, 500, 388.841, 280.216, 388.841, 584.018, 332.993, 555.85, 555.85, 555.85, 555.85, 280.216, 555.85, 332.993, 737.184, 369.882, 555.85, 584.018, 737.184, 332.85, 400.096, 584.185, 333, 333, 332.993, 576, 555.85, 278.167, 332.993, 333, 364.766, 555.85, 834.017, 834.017, 834.017, 611.159, 721.834, 721.834, 721.834, 721.834, 721.834, 721.834, 1000, 721.834, 667.007, 667.007, 667.007, 667.007, 278.167, 278.167, 278.167, 278.167, 721.834, 721.834, 778.171, 778.171, 778.171, 778.171, 778.171, 584.018, 778.171, 721.834, 721.834, 721.834, 721.834, 667.007, 667.007, 611.159, 555.85, 555.85, 555.85, 555.85, 555.85, 555.85, 888.84, 555.85, 555.85, 555.85, 555.85, 555.85, 278.167, 278.167, 278.167, 278.167, 611.159, 611.159, 611.159, 611.159, 611.159, 611.159, 611.159, 584.185, 611.159, 611.159, 611.159, 611.159, 611.159, 555.85, 611.159, 555.85, 721.834, 555.85, 721.834, 555.85, 721.834, 555.85, 721.834, 555.85, 721.834, 555.85, 721.834, 555.85, 721.834, 555.85, 721.834, 743.256, 721.834, 611.159, 667.007, 555.85, 667.007, 555.85, 667.007, 555.85, 667.007, 555.85, 667.007, 555.85, 778.171, 611.159, 778.171, 611.159, 778.171, 611.159, 778.171, 611.159, 721.834, 611.159, 721.834, 611.159, 278.167, 278.167, 278.167, 278.167, 278.167, 278.167, 278.167, 278.167, 278.167, 278.167, 781.773, 555.85, 555.85, 278.167, 721.834, 555.85, 555.85, 611.159, 278.167, 611.159, 278.167, 611.159, 400.004, 611.159, 478.995, 611.159, 278.167, 721.834, 611.159, 721.834, 611.159, 721.834, 611.159, 707.504, 722.855, 611.159, 778.171, 611.159, 778.171, 611.159, 778.171, 611.159, 1000, 944.151, 721.834, 388.841, 721.834, 388.841, 721.834, 388.841, 667.007, 555.85, 667.007, 555.85, 667.007, 555.85, 667.007, 555.85, 611.159, 332.993, 611.159, 388.996, 611.159, 332.993, 721.834, 611.159, 721.834, 611.159, 721.834, 611.159, 721.834, 611.159, 721.834, 611.159, 721.834, 611.159, 944.151, 778.171, 667.007, 555.85, 667.007, 611.159, 500, 611.159, 500, 611.159, 500, 278.167, 555.85, 721.834, 555.85, 1000, 888.84, 778.171, 611.159, 667.007, 555.85, 611.159, 332.993, 332.993, 332.993, 332.993, 332.993, 332.993, 332.993, 332.993, 332.993, 332.993, 332.993, 332.993, 721.834, 333, 854, 906.236, 472.853, 843.764, 929.823, 846.831, 278.167, 721.834, 721.834, 610.134, 611.65, 667.007, 611.159, 721.834, 778.171, 278.167, 721.834, 667.007, 832.992, 721.834, 656.777, 778.171, 718.23, 667.007, 589.67, 611.159, 667.007, 821.737, 667.007, 829.39, 781, 278.167, 667.007, 619.882, 479.484, 611.159, 278.167, 590.693, 619.882, 621.397, 555.85, 609.646, 479.484, 492.3, 611.159, 558.385, 278.167, 566.079, 555.85, 610.972, 555.85, 450.293, 611.159, 712.085, 604.534, 532, 664.425, 409.311, 590.693, 704.387, 578.364, 773.054, 833.525, 278.167, 590.693, 611.159, 590.693, 833.525, 667.007, 667.007, 886.257, 614.233, 719.252, 667.007, 278.167, 278.167, 555.85, 1093.759, 1042.01, 854.487, 621.932, 718.763, 676.756, 718.763, 721.834, 707.993, 721.834, 614.233, 721.834, 667.007, 926.75, 643.424, 718.763, 718.763, 615.252, 686.986, 832.992, 721.834, 778.171, 718.763, 667.007, 721.834, 611.159, 676.756, 781.242, 667.007, 728.993, 707.993, 979, 989.227, 854, 1000, 707.993, 719.252, 1042.01, 728.993, 555.85, 618.858, 603.994, 534.31, 617.833, 555.85, 735.669, 509.745, 611.159, 611.159, 507.162, 621.932, 740.252, 603.994, 611.159, 611.159, 611.159, 555.85, 408.922, 555.85, 885.23, 555.85, 646.006, 582.994, 888.84, 934.944, 706.972, 854, 594.249, 552.243, 865.251, 589.135, 555.85, 555.85, 611.159, 468.761, 563.011, 555.85, 278.167, 278.167, 278.167, 968.758, 906.236, 611.159, 507.162, 610.86, 555.85, 611.159, 778, 611, 574.805, 467.205, 944.151, 778.171, 944.151, 778.171, 944.151, 778.171, 667.007, 555.85, 333, 333, 555.85, 1000, 1000, 551.752, 278.167, 278.167, 278.167, 278.167, 500, 500, 500, 555.85, 555.85, 349.902, 1000, 1000, 240.254, 478.995, 332.993, 332.993, 603.994, 332.993, 278.013, 396, 555.85, 555.85, 1104, 555.85, 884.743, 515.886, 1146, 1000, 767.931, 599.904, 834.017, 834.017, 834.017, 834.017, 999.979, 500, 1000, 500, 1000.49, 500, 500, 493.862, 612, 995.707, 996.154, 584.018, 549.17, 713.107, 979, 718.83, 274.074, 549.17, 549.17, 582.504, 549.17, 549.17, 603.994, 584.018, 603.994, 603.994, 999.3, 1000, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 999.3, 995.299, 995.299, 995.996, 603.994, 604, 353.508, 353.508, 1000, 990.257, 990.257, 990.257, 990.257, 493.862, 603.994, 603.994, 603.994, 603.994, 497.31, 1021, 1051.758, 917.495, 750, 750, 530.75, 655.751, 594.249, 509.745, 500, 750, 750, 611, 611, 333];
const MyriadProRegularWidths = [212.128, 230.14, 337.019, 496.864, 512.86, 791.859, 605.009, 188.082, 283.992, 283.992, 414.83, 596.019, 207.124, 306.993, 207.124, 343.208, 512.86, 512.86, 512.86, 512.86, 512.86, 512.86, 512.86, 512.86, 512.86, 512.86, 207.124, 207.124, 596.019, 596.019, 596.019, 405.891, 736.9, 612.006, 542.004, 579.867, 665.843, 492.006, 487.126, 646.137, 651.85, 239.144, 370, 542.004, 471.872, 803.995, 657.85, 689.152, 532.006, 689.152, 537.876, 493.006, 497.128, 646.847, 558.006, 846.136, 571.005, 541.004, 553.144, 284.172, 341.206, 284.172, 595.696, 499.861, 299.993, 481.869, 568.844, 448, 563.845, 500.861, 292.175, 558.847, 554.849, 233.824, 242.817, 469, 235.822, 833.991, 554.849, 548.85, 568.844, 562.844, 326.993, 396, 331.201, 550.851, 481, 735.826, 463, 471, 428, 284.014, 239.216, 284.014, 596.019, 229.993, 512.86, 512.86, 512.86, 512.86, 239.216, 518.859, 299.993, 677.17, 345.891, 418.885, 596.019, 419.102, 299.868, 318.076, 596.187, 333, 333, 299.993, 576, 511.895, 207.124, 299.993, 333, 354.773, 418.885, 759.015, 759.015, 759.015, 406.107, 612.006, 612.006, 612.006, 612.006, 612.006, 612.006, 788, 579.867, 492.006, 492.006, 492.006, 492.006, 239.144, 239.144, 239.144, 239.144, 670.846, 657.85, 689.152, 689.152, 689.152, 689.152, 689.152, 596.019, 689.152, 646.847, 646.847, 646.847, 646.847, 541.004, 531.005, 548.146, 481.869, 481.869, 481.869, 481.869, 481.869, 481.869, 772.861, 448, 500.861, 500.861, 500.861, 500.861, 234.143, 234.143, 234.143, 234.143, 540.855, 554.849, 548.85, 548.85, 548.85, 548.85, 548.85, 596.187, 549.142, 550.851, 550.851, 550.851, 550.851, 471, 568.844, 471, 612.006, 481.869, 612.006, 481.869, 612.006, 481.869, 579.867, 448, 579.867, 448, 579.867, 448, 579.867, 448, 665.843, 574.238, 670.846, 563.845, 492.006, 500.861, 492.006, 500.861, 492.006, 500.861, 492.006, 500.861, 492.006, 500.861, 646.137, 558.847, 646.137, 558.847, 646.137, 558.847, 778, 556, 651.85, 554.849, 656.847, 554.849, 239.144, 234.143, 239.144, 234.143, 239.144, 234.143, 239.144, 233.824, 239.144, 234.143, 609.117, 477.162, 370, 242.817, 667, 500, 469, 471.872, 235.822, 556, 222, 471.872, 244.416, 471.872, 272.013, 475.869, 241.818, 657.85, 554.849, 722, 556, 657.85, 554.849, 554.997, 657.865, 554.849, 689.152, 548.85, 689.152, 548.85, 689.152, 548.85, 894, 863.137, 537.876, 326.993, 722, 333, 537.876, 326.993, 493.006, 396, 493.006, 396, 667, 500, 493.006, 396, 611, 278, 497.128, 339, 497.128, 331.201, 646.847, 550.851, 646.847, 550.851, 646.847, 550.851, 646.847, 550.851, 646.847, 550.851, 646.847, 550.851, 846.136, 735.826, 541.004, 471, 541.004, 553.144, 428, 553.144, 428, 553.144, 428, 222, 512.86, 612.006, 481.869, 788, 772.861, 689.152, 549.142, 667, 500, 497.128, 331.201, 299.993, 299.993, 299.993, 299.993, 299.993, 299.993, 299.993, 299.993, 299.993, 300.992, 345.99, 614.56, 217.132, 560.874, 722.096, 309.17, 727.614, 705.614, 741.562, 235.822, 612.006, 542.004, 451.181, 620.031, 492.006, 553.144, 651.85, 689.152, 239.144, 542.004, 609.029, 803.995, 657.85, 540.079, 689.152, 633.851, 532.006, 551.855, 497.128, 588.007, 718.136, 571.005, 684.633, 705.416, 239.144, 588.007, 550.88, 440.198, 554.849, 235.822, 524.119, 550.88, 559.809, 464, 546.35, 440.198, 409.076, 554.849, 533.855, 235.822, 493, 472, 552.833, 469, 433.238, 548.85, 555.05, 563.151, 482, 553.832, 412.98, 524.119, 641.565, 457.086, 655.097, 694.215, 235.822, 524.119, 548.85, 524.119, 694.215, 667, 667, 865, 542, 719, 667, 278, 278, 500, 1057, 1010, 854, 583, 722, 635, 719, 667, 656, 667, 542, 677, 667, 923, 604, 719, 719, 583, 656, 833, 722, 778, 719, 667, 722, 611, 635, 760, 667, 740, 667, 917, 938, 792, 885, 656, 719, 1010, 722, 556, 573, 531, 365, 583, 556, 669, 458, 559, 559, 438, 583, 688, 552, 556, 542, 556, 500, 458, 500, 823, 500, 573, 521, 802, 823, 625, 719, 521, 510, 750, 542, 556, 556, 556, 365, 510, 500, 222, 278, 222, 906, 812, 556, 438, 559, 500, 552, 778, 556, 489, 411, 846.136, 735.826, 846.136, 735.826, 846.136, 735.826, 541.004, 471, 306.993, 333, 499.861, 1000, 1000, 552, 206.844, 206.844, 206.844, 222, 353.992, 353.992, 355.99, 499.861, 499.861, 281.922, 1000, 1156, 188, 354, 254.995, 254.995, 500, 333, 121.005, 365, 556, 556, 1094, 512.86, 885, 323, 1073, 619, 768, 816.87, 834, 834, 834, 834, 1000, 500, 1000, 500, 1000, 500, 500, 529.85, 612, 614.822, 507.078, 596.019, 562.176, 728.108, 979, 719, 316.086, 596.187, 596.187, 583, 596.187, 596.187, 604, 584, 604, 604, 708, 625, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 729, 604, 604, 354, 354, 1000, 990, 990, 990, 990, 521.852, 604, 604, 604, 604, 354, 1021, 1052, 917, 750, 750, 531, 656, 594, 510, 500, 750, 750, 500, 500, 333];
const MyriadProBoldWidths = [202.123, 267.995, 396.899, 549.851, 554.849, 879.843, 677.842, 205.177, 313.992, 313.992, 453.811, 596.019, 260.158, 321.991, 260.158, 331.201, 554.849, 554.849, 554.849, 554.849, 554.849, 554.849, 554.849, 554.849, 554.849, 554.849, 259.993, 259.993, 596.019, 596.019, 596.019, 445.12, 769.919, 655.85, 603.859, 594.863, 695.835, 534.007, 527.14, 682.15, 688.839, 285.172, 410.89, 613.859, 511.132, 845.995, 689.842, 717.153, 581.004, 717.153, 592.863, 540.003, 548.146, 681.842, 636.005, 888.144, 613.006, 603.008, 577.151, 313.992, 330.2, 313.992, 596.019, 499.861, 299.993, 527.855, 598.157, 450.877, 596.159, 527.855, 340.992, 585.155, 586.151, 274.166, 291.177, 541.85, 275.167, 859.841, 586.151, 577.151, 598.157, 595.157, 379.843, 433.88, 366.993, 583.151, 529.857, 759.165, 518.859, 522.857, 469, 313.872, 283.217, 313.872, 596.019, 267.995, 554.849, 554.849, 554.849, 554.849, 283.217, 560.848, 299.993, 677.17, 377.881, 464.872, 596.019, 459.114, 299.868, 356.088, 596.187, 333, 333, 299.993, 576, 541.85, 260.158, 299.993, 333, 385.754, 464.872, 831.014, 831.014, 831.014, 445.12, 655.85, 655.85, 655.85, 655.85, 655.85, 655.85, 868, 594.863, 534.007, 534.007, 534.007, 534.007, 285.172, 285.172, 285.172, 285.172, 703.834, 689.842, 717.153, 717.153, 717.153, 717.153, 717.153, 596.019, 717.153, 681.842, 681.842, 681.842, 681.842, 603.008, 580.01, 600.155, 527.855, 527.855, 527.855, 527.855, 527.855, 527.855, 802.856, 450.877, 527.855, 527.855, 527.855, 527.855, 274.166, 274.166, 274.166, 274.166, 574.151, 586.151, 577.151, 577.151, 577.151, 577.151, 577.151, 596.187, 577.151, 583.151, 583.151, 583.151, 583.151, 522.857, 598.157, 522.857, 655.85, 527.855, 655.85, 527.855, 655.85, 527.855, 594.863, 450.877, 594.863, 450.877, 594.863, 450.877, 594.863, 450.877, 695.835, 606.21, 703.834, 596.159, 534.007, 527.855, 534.007, 527.855, 534.007, 527.855, 534.007, 527.855, 534.007, 527.855, 682.15, 585.155, 682.15, 585.155, 682.15, 585.155, 778, 611, 688.839, 586.151, 700.838, 586.151, 285.172, 274.166, 285.172, 274.166, 285.172, 274.166, 285.172, 274.166, 285.172, 274.166, 696.295, 564.846, 410.89, 291.177, 722, 556, 541.85, 511.132, 275.167, 611, 278, 511.132, 284.811, 511.132, 353.995, 517.138, 287.174, 689.842, 586.151, 722, 611, 689.842, 586.151, 585.587, 689.865, 586.151, 717.153, 577.151, 717.153, 577.151, 717.153, 577.151, 936, 868.14, 592.863, 379.843, 722, 389, 592.863, 379.843, 540.003, 433.88, 540.003, 433.88, 667, 556, 540.003, 433.88, 611, 333, 548.146, 370.995, 548.146, 366.993, 681.842, 583.151, 681.842, 583.151, 681.842, 583.151, 681.842, 583.151, 681.842, 583.151, 681.842, 583.151, 888.144, 759.165, 603.008, 522.857, 603.008, 577.151, 469, 577.151, 469, 577.151, 469, 278, 554.849, 655.85, 527.855, 868, 802.856, 717.153, 577.151, 667, 556, 548.146, 366.993, 299.993, 299.993, 299.993, 299.993, 299.993, 299.993, 299.993, 299.993, 299.993, 302.993, 435.147, 663.843, 276.993, 641.976, 797.208, 393.306, 792.281, 775.797, 815.106, 280.168, 655.85, 603.859, 481.942, 648.229, 534.007, 577.151, 688.839, 719.152, 285.172, 613.859, 648.011, 845.995, 689.842, 574.396, 717.153, 666.846, 581.004, 577.908, 548.146, 625.006, 785.188, 613.006, 741.926, 744.224, 285.172, 625.006, 583.241, 467.822, 586.151, 280.168, 557.969, 583.241, 586.661, 514.862, 575.579, 468.383, 440.57, 586.151, 570.501, 280.168, 548.375, 517.858, 585.176, 509.858, 471.669, 577.151, 600.291, 591.331, 520, 593.931, 451.709, 557.969, 710.667, 504.276, 725.598, 733.24, 280.168, 557.969, 577.151, 557.969, 733.24, 667, 669, 885, 567, 711, 667, 278, 276, 556, 1094, 1062, 875, 610, 722, 622, 719, 722, 719, 722, 567, 712, 667, 904, 626, 719, 719, 610, 702, 833, 722, 778, 719, 667, 722, 611, 622, 854, 667, 730, 703, 1005, 1019, 870, 979, 719, 711, 1031, 719, 556, 618, 615, 417, 635, 556, 709, 497, 615, 615, 500, 635, 740, 604, 611, 604, 611, 556, 490, 556, 875, 556, 615, 581, 833, 844, 729, 854, 615, 552, 854, 583, 556, 556, 611, 417, 552, 556, 278, 281, 278, 969, 906, 611, 500, 615, 556, 604, 778, 611, 487, 447, 888.144, 759.165, 888.144, 759.165, 888.144, 759.165, 603.008, 522.857, 321.991, 333, 499.861, 1000, 1000, 552, 260.158, 260.158, 260.158, 278, 454, 454, 459, 523.858, 523.858, 337.904, 1000, 1285, 240, 479, 269.993, 269.993, 604, 333, 125.006, 396, 556, 556, 1094, 554.849, 885, 489, 1115, 650, 768, 816.87, 834, 834, 834, 834, 1000, 500, 1000, 500, 1000, 500, 500, 578.835, 612, 662.803, 543.085, 596.019, 589.187, 752.115, 979, 722, 358.096, 596.187, 596.187, 583, 596.187, 596.187, 604, 584, 604, 604, 708, 625, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 729, 604, 604, 354, 354, 1000, 990, 990, 990, 990, 573.835, 604, 604, 604, 604, 354, 1021, 1052, 917, 750, 750, 531, 656, 594, 510, 500, 750, 750, 611, 611, 333];
const MyriadProItalicWidths = [182.109, 236.142, 304.018, 491.865, 491.865, 772.861, 589.008, 166.071, 283.992, 283.992, 413.83, 596.019, 211.127, 306.993, 211.127, 326.197, 491.865, 491.865, 491.865, 491.865, 491.865, 491.865, 491.865, 491.865, 491.865, 491.865, 211.127, 211.127, 596.019, 596.019, 596.019, 383.896, 719.899, 568.004, 519.006, 554.871, 646.847, 470.008, 465.124, 623.131, 626.855, 233.142, 364, 515.004, 441.881, 779.996, 631.851, 668.146, 507.007, 668.146, 522.88, 466.006, 474.124, 620.855, 539.009, 829.134, 521.007, 517.005, 523.138, 284.172, 329.199, 284.172, 595.696, 499.861, 382.99, 521.856, 524.858, 423, 524.858, 452.879, 283.171, 523.858, 533.855, 228.827, 226.829, 456, 228.827, 807.993, 533.855, 521.856, 524.858, 524.858, 317.992, 373, 313.189, 525.854, 455, 706.838, 441, 441, 415, 284.014, 217.196, 284.014, 596.019, 235.994, 491.865, 491.865, 491.865, 491.865, 217.196, 500.861, 382.99, 675.166, 349.891, 408.888, 596.019, 395.098, 382.829, 318.076, 596.187, 333, 333, 382.99, 576, 493.9, 211.127, 382.99, 333, 344.779, 407.887, 751.017, 751.017, 751.017, 384.099, 568.004, 568.004, 568.004, 568.004, 568.004, 568.004, 773, 554.871, 470.008, 470.008, 470.008, 470.008, 233.142, 233.142, 233.142, 233.142, 653.85, 631.851, 668.146, 668.146, 668.146, 668.146, 668.146, 596.019, 667.143, 620.855, 620.855, 620.855, 620.855, 517.005, 507.007, 538.138, 521.856, 521.856, 521.856, 521.856, 521.856, 521.856, 717.868, 423, 452.879, 452.879, 452.879, 452.879, 229.139, 229.139, 229.139, 229.139, 515.857, 533.855, 521.856, 521.856, 521.856, 521.856, 521.856, 596.187, 523.138, 525.854, 525.854, 525.854, 525.854, 441, 525.854, 441, 568.004, 521.856, 568.004, 521.856, 568.004, 521.856, 554.871, 423, 554.871, 423, 554.871, 423, 554.871, 423, 646.847, 534, 653.85, 524.858, 470.008, 452.879, 470.008, 452.879, 470.008, 452.879, 470.008, 452.879, 470.008, 452.879, 623.131, 523.858, 623.131, 523.858, 623.131, 523.858, 778, 556, 626.855, 533.855, 626.855, 533.855, 233.142, 229.139, 233.142, 229.139, 233.142, 229.139, 233.142, 228.827, 233.142, 229.139, 596.677, 456.157, 364, 226.829, 667, 500, 456, 441.881, 228.827, 556, 222, 441.881, 235.79, 441.881, 265.064, 447.875, 236.821, 631.851, 533.855, 722, 556, 631.851, 533.855, 534.22, 631.873, 533.855, 668.146, 521.856, 668.146, 521.856, 668.146, 521.856, 867, 800.125, 522.88, 317.992, 722, 333, 522.88, 317.992, 466.006, 373, 466.006, 373, 667, 500, 466.006, 373, 611, 278, 474.124, 312.996, 474.124, 313.189, 620.855, 525.854, 620.855, 525.854, 620.855, 525.854, 620.855, 525.854, 620.855, 525.854, 620.855, 525.854, 829.134, 706.838, 517.005, 441, 517.005, 523.138, 415, 523.138, 415, 523.138, 415, 222, 491.865, 568.004, 521.856, 773, 717.868, 667.143, 523.138, 667, 500, 474.124, 313.189, 382.99, 382.99, 382.99, 382.99, 382.99, 383.992, 382.99, 382.99, 382.99, 359.993, 345.99, 568.004, 214.13, 491.263, 646.852, 254.215, 683.046, 600.838, 697.089, 228.827, 568.004, 519.006, 435.759, 596.09, 470.008, 523.138, 626.855, 668.146, 233.142, 515.004, 569.004, 779.996, 631.851, 533.641, 668.146, 621.332, 507.007, 534.348, 474.124, 544.005, 691.069, 521.007, 647.341, 680.227, 233.142, 544.005, 540.702, 405.583, 533.832, 228.827, 501.177, 540.702, 532.732, 439, 522.318, 405.583, 391.679, 533.832, 515.545, 228.827, 461, 436.832, 525.143, 440, 418.209, 521.856, 528.061, 537.233, 486, 537.979, 387.976, 501.177, 612.678, 423.079, 624.398, 676.164, 228.827, 501.177, 521.856, 501.177, 676.164, 667, 667, 843, 544, 708, 667, 278, 278, 500, 1066, 982, 844, 589, 715, 639, 724, 667, 651, 667, 544, 704, 667, 917, 614, 715, 715, 589, 686, 833, 722, 778, 725, 667, 722, 611, 639, 795, 667, 727, 673, 920, 923, 805, 886, 651, 694, 1022, 682, 556, 562, 522, 493, 553, 556, 688, 465, 556, 556, 472, 564, 686, 550, 556, 556, 556, 500, 833, 500, 835, 500, 572, 518, 830, 851, 621, 736, 526, 492, 752, 534, 556, 556, 556, 378, 496, 500, 222, 222, 222, 910, 828, 556, 472, 565, 500, 556, 778, 556, 492, 339, 829.134, 706.838, 829.134, 706.838, 829.134, 706.838, 517.005, 441, 306.993, 333, 499.861, 1000, 1000, 552, 193.853, 193.853, 193.853, 222, 341.991, 341.991, 341.991, 482.869, 482.869, 276.924, 1000, 1124, 188, 354, 244.995, 244.995, 500, 333, 142.007, 365, 556, 556, 1094, 491.865, 885, 323, 1083, 669, 768, 816.87, 834, 834, 834, 834, 1000, 500, 998, 500, 1000, 500, 500, 517.85, 612, 616.822, 536.083, 596.019, 540.167, 728.108, 979, 719, 291.078, 596.187, 596.187, 584, 596.187, 596.187, 604, 584, 604, 604, 708, 625, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 729, 604, 604, 354, 354, 1000, 990, 990, 990, 990, 519.851, 604, 604, 604, 604, 354, 1021, 1052, 917, 750, 750, 531, 656, 594, 510, 500, 750, 750, 500, 500, 333];
const MyriadProBoldItalicWidths = [184.111, 268.994, 386.903, 540.855, 540.855, 868.846, 665.843, 198.173, 313.992, 313.992, 451.816, 596.019, 260.158, 321.991, 260.158, 326.197, 540.855, 540.855, 540.855, 540.855, 540.855, 540.855, 540.855, 540.855, 540.855, 540.855, 259.993, 259.993, 596.019, 596.019, 596.019, 437.116, 748.927, 617.859, 581.867, 582.863, 680.839, 517.005, 511.132, 673.149, 665.843, 274.166, 398.891, 589.86, 487.126, 820.988, 665.843, 705.156, 559.006, 705.156, 582.863, 510.008, 527.14, 654.847, 611.005, 864.138, 569.004, 570.005, 553.144, 313.992, 337.203, 313.992, 596.019, 499.861, 394.991, 553.848, 560.146, 434.881, 560.146, 500.861, 328.991, 558.149, 563.146, 265.159, 275.167, 529.857, 265.159, 830.851, 563.146, 559.144, 562.144, 560.146, 369.85, 415.888, 347.992, 560.146, 511.859, 733.156, 497.865, 496.864, 450, 313.872, 240.184, 313.872, 596.019, 268.994, 540.855, 540.855, 540.855, 540.855, 240.184, 555.85, 394.991, 673.168, 399.874, 460.874, 596.019, 409.101, 394.824, 356.088, 596.187, 333, 333, 394.991, 576, 537.852, 260.158, 394.991, 333, 382.754, 459.873, 826.019, 826.019, 826.019, 437.116, 617.859, 617.859, 617.859, 617.859, 617.859, 617.859, 845, 582.863, 517.005, 517.005, 517.005, 517.005, 274.166, 274.166, 274.166, 274.166, 692.838, 665.843, 705.156, 705.156, 705.156, 705.156, 705.156, 596.019, 702.153, 654.847, 654.847, 654.847, 654.847, 570.005, 558.006, 580.151, 553.848, 553.848, 553.848, 553.848, 553.848, 553.848, 762.86, 434.881, 500.861, 500.861, 500.861, 500.861, 265.159, 265.159, 265.159, 265.159, 558.149, 563.146, 559.144, 559.144, 559.144, 559.144, 559.144, 596.187, 560.146, 560.146, 560.146, 560.146, 560.146, 496.864, 564.149, 496.864, 617.859, 553.848, 617.859, 553.848, 617.859, 553.848, 582.863, 434.881, 582.863, 434.881, 582.863, 434.881, 582.863, 434.881, 680.839, 568.194, 692.838, 560.146, 517.005, 500.861, 517.005, 500.861, 517.005, 500.861, 517.005, 500.861, 517.005, 500.861, 673.149, 558.149, 673.149, 558.149, 673.149, 558.149, 778, 611, 665.843, 563.146, 665.843, 563.146, 274.166, 265.159, 274.166, 265.159, 274.166, 265.159, 274.166, 265.159, 274.166, 265.159, 672.802, 539.854, 398.891, 275.167, 722, 556, 529.857, 487.126, 265.159, 611, 278, 487.126, 273.002, 487.126, 355.998, 499.132, 278.167, 665.843, 563.146, 722, 611, 665.843, 563.146, 562.605, 665.869, 563.146, 705.156, 559.144, 705.156, 559.144, 705.156, 559.144, 909, 826.132, 582.863, 369.85, 722, 389, 582.863, 369.85, 510.008, 415.888, 510.008, 415.888, 667, 556, 510.008, 415.888, 611, 333, 527.14, 347.998, 527.14, 347.992, 654.847, 560.146, 654.847, 560.146, 654.847, 560.146, 654.847, 560.146, 654.847, 560.146, 654.847, 560.146, 864.138, 733.156, 570.005, 496.864, 570.005, 553.144, 450, 553.144, 450, 553.144, 450, 278, 540.855, 617.859, 553.848, 845, 762.86, 702.153, 560.146, 667, 556, 527.14, 347.992, 394.991, 394.991, 394.991, 394.991, 394.991, 394.991, 394.991, 394.991, 394.991, 335.994, 434.991, 617.859, 266.993, 571.001, 718.186, 327.898, 743.792, 673.869, 759.852, 265.159, 617.859, 581.867, 476.105, 635.632, 517.005, 553.144, 665.843, 705.156, 274.166, 589.86, 618.009, 820.988, 665.843, 560.809, 705.156, 653.208, 559.006, 557.686, 527.14, 557.005, 765.751, 569.004, 691.32, 721.222, 274.166, 557.005, 575.893, 437.442, 563.146, 265.159, 535.718, 575.893, 562.359, 482.869, 550.684, 437.442, 427.263, 563.146, 555.383, 265.159, 522.078, 474.868, 558.975, 482.869, 452.295, 559.144, 573.067, 565.56, 532, 585.376, 430.325, 535.718, 685.379, 471.295, 691.047, 716.598, 265.159, 535.718, 559.144, 535.718, 716.598, 667, 667, 886, 614, 719, 667, 278, 278, 556, 1094, 1042, 854, 622, 719, 677, 719, 722, 708, 722, 614, 722, 667, 927, 643, 719, 719, 615, 687, 833, 722, 778, 719, 667, 722, 611, 677, 781, 667, 729, 708, 979, 989, 854, 1000, 708, 719, 1042, 729, 556, 619, 604, 534, 618, 556, 736, 510, 611, 611, 507, 622, 740, 604, 611, 611, 611, 556, 889, 556, 885, 556, 646, 583, 889, 935, 707, 854, 594, 552, 865, 589, 556, 556, 611, 469, 563, 556, 278, 278, 278, 969, 906, 611, 507, 619, 556, 611, 778, 611, 575, 467, 864.138, 733.156, 864.138, 733.156, 864.138, 733.156, 570.005, 496.864, 321.991, 333, 499.861, 1000, 1000, 552, 255.154, 255.154, 255.154, 278, 448, 448, 448, 518.859, 518.859, 335.906, 1000, 1260, 240, 479, 267.995, 266.993, 604, 333, 143.007, 396, 556, 556, 1104, 540.855, 885, 516, 1146, 698, 768, 816.87, 834, 834, 834, 834, 999, 500, 1000, 500, 1000, 500, 500, 565.837, 612, 651.808, 561.088, 596.019, 568.177, 752.115, 979, 722, 337.091, 596.187, 596.187, 583, 596.187, 596.187, 604, 584, 604, 604, 708, 625, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 729, 604, 604, 354, 354, 1000, 990, 990, 990, 990, 592.83, 604, 604, 604, 604, 354, 1021, 1052, 917, 750, 750, 531, 656, 594, 510, 500, 750, 750, 611, 611, 333];
const SegoeuiRegularWidths = [274.091, 284.352, 392.112, 590.661, 538.914, 818.209, 800.3, 230.079, 301.751, 301.751, 416.821, 684.098, 216.929, 399.893, 216.929, 389.884, 538.914, 538.914, 538.914, 538.914, 538.914, 538.914, 538.914, 538.914, 538.914, 538.914, 216.929, 216.929, 684.098, 684.098, 684.098, 448.119, 954.953, 645.029, 573.246, 618.999, 701.011, 505.866, 488.409, 686.18, 709.798, 266.274, 356.935, 580.083, 470.576, 897.941, 747.876, 754.069, 560.067, 754.069, 598.004, 531.259, 524.061, 686.853, 621.104, 934.23, 589.848, 552.743, 570.46, 301.941, 379.136, 301.941, 683.736, 414.926, 268.062, 508.651, 587.731, 461.915, 588.704, 522.807, 313.178, 588.704, 565.763, 242.004, 242.004, 497.07, 242.004, 861.322, 565.763, 585.779, 587.731, 588.704, 347.649, 424.315, 339.071, 565.763, 479.005, 722.491, 458.985, 483.885, 452.15, 301.772, 239.473, 301.772, 684.098, 284.172, 538.914, 538.914, 555.511, 538.914, 239.473, 448.119, 414.052, 890.355, 391.967, 505.721, 684.098, 890.355, 414.856, 377.044, 684.296, 366.203, 366.203, 282.221, 576, 457.916, 216.929, 205.075, 351.065, 430.386, 505.721, 906.266, 930.686, 952.169, 448.358, 645.029, 645.029, 645.029, 645.029, 645.029, 645.029, 860.35, 618.999, 505.866, 505.866, 505.866, 505.866, 266.274, 266.274, 266.274, 266.274, 701.011, 747.876, 754.069, 754.069, 754.069, 754.069, 754.069, 684.098, 754.069, 686.853, 686.853, 686.853, 686.853, 552.743, 560.067, 544.089, 508.651, 508.651, 508.651, 508.651, 508.651, 508.651, 831.882, 461.915, 522.807, 522.807, 522.807, 522.807, 242.333, 242.333, 242.333, 242.333, 558.93, 565.763, 585.779, 585.779, 585.779, 585.779, 585.779, 684.296, 586.09, 565.763, 565.763, 565.763, 565.763, 483.885, 587.731, 483.885, 645.029, 508.651, 645.029, 508.651, 645.029, 508.651, 618.999, 461.915, 618.999, 461.915, 618.999, 461.915, 618.999, 461.915, 701.011, 674.594, 701.011, 588.704, 505.866, 522.807, 505.866, 522.807, 505.866, 522.807, 505.866, 522.807, 505.866, 522.807, 686.18, 588.704, 686.18, 588.704, 686.18, 588.704, 778, 556, 709.798, 565.763, 709.798, 565.763, 266.274, 242.333, 266.274, 242.333, 266.274, 242.333, 266.274, 242.004, 266.274, 242.333, 623.162, 495.286, 356.935, 242.004, 667, 500, 497.07, 470.576, 242.004, 556, 222, 470.576, 307.651, 470.576, 314.955, 470.576, 273.719, 747.876, 565.763, 722, 556, 747.876, 565.763, 605.951, 747.9, 565.763, 754.069, 585.779, 754.069, 585.779, 754.069, 585.779, 930.66, 927.886, 598.004, 347.649, 722, 333, 598.004, 347.649, 531.259, 424.315, 531.259, 424.315, 531.259, 424.315, 531.259, 424.315, 611, 278, 524.061, 439.942, 524.061, 339.071, 686.853, 565.763, 686.853, 565.763, 686.853, 565.763, 686.853, 565.763, 686.853, 565.763, 686.853, 565.763, 934.23, 722.491, 552.743, 483.885, 552.743, 570.46, 452.15, 570.46, 452.15, 570.46, 452.15, 241.028, 538.914, 645.029, 508.651, 860.35, 831.882, 754.069, 586.09, 667, 500, 524.061, 339.071, 371.085, 371.085, 310.053, 303.703, 201.169, 280.266, 185.055, 336.906, 324.212, 272.454, 272.454, 644.555, 216.929, 570.18, 774.027, 378.628, 800.355, 666.626, 825.688, 266.4, 645.029, 573.246, 471.865, 644.56, 505.866, 570.46, 709.798, 754.069, 266.274, 580.083, 628.935, 897.941, 747.876, 509.841, 754.069, 712.722, 560.067, 515.974, 524.061, 552.743, 754.046, 589.848, 775.464, 755.33, 266.274, 552.743, 614.125, 438.186, 575.037, 266.4, 552.858, 614.125, 547.665, 519.045, 584.36, 438.186, 441.975, 575.037, 586.263, 266.4, 523.925, 498.045, 576.973, 525.88, 445.07, 585.779, 627.003, 586.093, 460.512, 574.532, 487.28, 552.858, 698.745, 538.188, 750.112, 808.351, 266.4, 552.858, 585.779, 552.858, 808.351, 505.866, 667, 865, 542, 719, 667, 278, 278, 500, 1057, 1010, 854, 583, 748.851, 635, 719, 667, 656, 667, 542, 677, 667, 923, 604, 719, 719, 583, 656, 833, 722, 778, 719, 667, 722, 611, 635, 760, 667, 740, 667, 917, 938, 792, 885, 656, 719, 1010, 722, 556, 573, 531, 365, 583, 556, 669, 458, 559, 559, 438, 583, 688, 552, 556, 542, 556, 500, 458, 500, 823, 500, 573, 521, 802, 823, 625, 719, 521, 510, 750, 542, 522.807, 556, 556, 365, 510, 500, 222, 278, 222, 906, 812, 556, 438, 581.477, 500, 552, 778, 556, 489, 411, 934.23, 722.491, 934.23, 722.491, 934.23, 722.491, 552.743, 483.885, 333, 399.893, 499.861, 1000, 1000, 414.856, 228.831, 228.831, 228.831, 228.831, 376.943, 376.943, 376.943, 374.9, 374.9, 406.137, 732.91, 1209.96, 229.614, 376.95, 315.91, 315.91, 479.98, 415.028, 167, 392.364, 488.146, 538.914, 1110.607, 538.914, 885, 323, 1073, 772.95, 768, 600, 921.895, 921.895, 928.726, 892.105, 862.79, 472.17, 862.79, 472.17, 861.82, 500, 500, 535.002, 612, 753.687, 616.795, 684.098, 650.104, 817.02, 683.587, 701.902, 539.698, 685.273, 685.273, 683.498, 685.273, 685.273, 604, 584, 604, 604, 708, 625, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 729, 861.322, 604, 353.511, 353.511, 1000, 861.548, 861.548, 861.548, 861.548, 583.33, 861.322, 604, 861.322, 861.322, 353.511, 1021, 1052, 917, 750, 750, 684.247, 684.306, 684.858, 684.231, 500, 750, 750, 554.69, 554.69, 333];
const SegoeuiBoldWidths = [276.046, 327.139, 493.036, 592.123, 575.037, 867.033, 849.411, 293.223, 369.131, 369.131, 454.893, 707.049, 271.161, 404.289, 271.161, 443.627, 575.037, 575.037, 575.037, 575.037, 575.037, 575.037, 575.037, 575.037, 575.037, 575.037, 270.989, 270.989, 707.049, 707.049, 707.049, 438.105, 954.008, 702.961, 640.963, 623.88, 737.133, 532.233, 520.157, 711.092, 765.934, 317.087, 445.189, 648.775, 511.364, 957.025, 789.854, 758.464, 614.267, 758.464, 652.681, 560.553, 586.09, 722.975, 667, 1005.048, 655.281, 606.943, 607.096, 369.131, 436.299, 369.131, 707.049, 414.926, 313.959, 537.941, 620.281, 479.85, 619.303, 540.866, 383.293, 619.303, 602.208, 284.352, 284.352, 558.93, 284.352, 915.848, 605.141, 611.489, 620.281, 619.303, 397.784, 439.818, 389.15, 605.141, 541.844, 797.536, 552.097, 537.941, 479.005, 368.99, 326.421, 368.99, 707.049, 327.139, 575.037, 575.037, 556, 575.037, 326.421, 485.221, 461.904, 874.244, 410.027, 580.898, 707.049, 874.244, 414.856, 379.976, 707.255, 404.289, 404.289, 303.216, 576, 509.14, 271.161, 215.328, 394.032, 455.761, 580.898, 952.169, 965.347, 979.024, 438.105, 702.961, 702.961, 702.961, 702.961, 702.961, 702.961, 935.06, 623.88, 532.233, 532.233, 532.233, 532.233, 317.087, 317.087, 317.087, 317.087, 737.133, 789.854, 757.974, 757.974, 757.974, 757.974, 757.974, 707.049, 758.464, 722.975, 722.975, 722.975, 722.975, 606.943, 614.267, 628.096, 537.941, 537.941, 537.941, 537.941, 537.941, 537.941, 827.979, 479.85, 540.866, 540.866, 540.866, 540.866, 284.352, 284.352, 284.352, 284.352, 593.415, 605.141, 611, 611, 611, 611, 611, 707.255, 611.489, 605.141, 605.141, 605.141, 605.141, 537.941, 620.281, 537.941, 702.961, 537.941, 702.961, 537.941, 702.961, 537.941, 623.88, 479.85, 623.88, 479.85, 623.88, 479.85, 623.88, 479.85, 737.133, 740.98, 737.133, 632.489, 532.233, 540.866, 532.233, 540.866, 532.233, 540.866, 532.233, 540.866, 532.233, 540.866, 711.092, 619.303, 711.092, 619.303, 711.092, 619.303, 778, 611, 765.934, 602.208, 765.934, 616.371, 317.087, 284.352, 317.087, 284.352, 317.087, 284.352, 317.087, 284.352, 317.087, 284.352, 762.533, 604.328, 445.189, 284.352, 722, 556, 558.93, 511.364, 284.352, 611, 278, 511.364, 364.018, 511.364, 433.102, 511.364, 314.154, 789.854, 605.141, 722, 611, 789.854, 605.141, 684.579, 789.885, 605.141, 758.464, 611.489, 758.464, 611.489, 758.464, 611.489, 939.94, 924.469, 652.681, 397.784, 722, 389, 652.681, 397.784, 560.553, 439.818, 560.553, 439.818, 560.553, 439.818, 560.553, 439.818, 611, 333, 586.09, 512.693, 586.09, 389.15, 722.975, 605.141, 722.975, 605.141, 722.975, 605.141, 722.975, 605.141, 722.975, 605.141, 722.975, 605.141, 1005.048, 797.536, 606.943, 537.941, 606.943, 607.096, 479.005, 607.096, 479.005, 607.096, 479.005, 299.498, 575.037, 702.961, 537.941, 935.06, 827.979, 758.464, 611.489, 667, 556, 586.09, 389.15, 383.293, 383.293, 336.906, 346.183, 252.923, 270.989, 266.107, 383.293, 406.24, 316.886, 408.34, 730.296, 271.478, 654.763, 889.393, 479.375, 846.005, 779.579, 863.886, 328.324, 702.961, 640.963, 511.655, 703.369, 532.233, 607.096, 765.934, 758.464, 317.087, 648.775, 692.88, 957.025, 789.854, 554.091, 758.464, 764.959, 614.267, 568.758, 586.09, 606.943, 815.631, 655.281, 826.086, 765.854, 317.087, 606.943, 636.98, 462.712, 611.489, 328.324, 620.575, 636.98, 621.712, 566.742, 611.369, 463.272, 464.876, 611.489, 612.352, 328.324, 570.706, 548.677, 612.979, 553.559, 477.205, 611.489, 692.724, 618.511, 487.776, 613.206, 548.959, 620.575, 750.628, 554.504, 790.575, 831.81, 328.324, 620.575, 611.489, 620.575, 831.81, 532.233, 669, 885, 567, 711, 667, 278, 276, 556, 1094, 1062, 875, 610, 790.345, 622, 719, 722, 719, 722, 567, 712, 667, 904, 626, 719, 719, 610, 702, 833, 722, 778, 719, 667, 722, 611, 622, 854, 667, 730, 703, 1005, 1019, 870, 979, 719, 711, 1031, 719, 556, 618, 615, 417, 635, 556, 709, 497, 615, 615, 500, 635, 740, 604, 611, 604, 611, 556, 490, 556, 875, 556, 615, 581, 833, 844, 729, 854, 615, 552, 854, 583, 540.866, 556, 611, 417, 552, 556, 278, 281, 278, 969, 906, 611, 500, 627.7, 556, 604, 778, 611, 487, 447, 1005.048, 797.536, 1005.048, 797.536, 1005.048, 797.536, 606.943, 537.941, 333, 404.289, 499.861, 1000, 1000, 414.856, 290.215, 290.215, 290.215, 290.215, 493.165, 493.165, 493.165, 406.141, 406.141, 422.247, 912.11, 1256.35, 293.28, 493.159, 344.229, 344.229, 580.076, 415.028, 167, 445.318, 519.877, 575.037, 1309.376, 575.037, 885, 489, 1115, 771, 768, 600, 965.347, 965.347, 965.347, 925.306, 862.79, 472.17, 862.79, 472.17, 861.82, 500, 500, 577.471, 612, 790.78, 642.677, 707.049, 650.104, 812.627, 683.587, 701.495, 576.329, 708.232, 708.232, 706.433, 708.232, 708.232, 604, 584, 604, 604, 708, 625, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 729, 861.322, 604, 353.511, 353.511, 1000, 861.548, 861.548, 861.548, 861.548, 587.238, 861.322, 604, 861.322, 861.322, 353.511, 1021, 1052, 917, 750, 750, 684.247, 684.306, 684.858, 684.231, 500, 750, 750, 667.169, 667.169, 333];
const SegoeuiItalicWidths = [275.07, 319.041, 374.046, 590.661, 538.914, 823.09, 769.538, 226.171, 304.192, 304.192, 417.798, 685.079, 217.905, 399.893, 217.905, 396.236, 538.914, 538.914, 538.914, 538.914, 538.914, 538.914, 538.914, 538.914, 538.914, 538.914, 217.905, 217.905, 685.079, 685.079, 685.079, 430.06, 959.835, 631.842, 573.246, 639.013, 711.748, 499.029, 488.898, 688.133, 707.842, 266.274, 346.19, 599.62, 462.764, 817.864, 750.801, 754.069, 575.201, 754.069, 581.896, 497.075, 526.016, 688.802, 627.934, 929.349, 590.829, 554.204, 569.971, 304.382, 386.464, 304.382, 684.712, 414.926, 349.114, 541.844, 541.844, 436.035, 541.844, 486.683, 295.1, 541.844, 556.979, 258.594, 240.053, 490.235, 258.594, 857.898, 561.86, 535.011, 541.844, 541.844, 354.971, 395.02, 323.436, 557.952, 473.145, 720.051, 455.08, 483.885, 452.15, 304.214, 240.451, 304.214, 685.079, 318.841, 538.914, 538.914, 556, 538.914, 240.451, 448.119, 349.114, 909.406, 435.897, 514.022, 685.079, 890.355, 348.963, 377.044, 685.273, 366.203, 366.203, 349.114, 576, 457.916, 216.929, 349.114, 366.203, 446.49, 514.022, 916.032, 946.799, 960.951, 430.291, 631.842, 631.842, 631.842, 631.842, 631.842, 631.842, 854.98, 639.013, 499.029, 499.029, 499.029, 499.029, 266.274, 266.274, 266.274, 266.274, 711.748, 750.801, 754.069, 754.069, 754.069, 754.069, 754.069, 685.079, 754.069, 688.802, 688.802, 688.802, 688.802, 554.204, 575.201, 540.179, 541.844, 541.844, 541.844, 541.844, 541.844, 541.844, 826.023, 436.035, 486.683, 486.683, 486.683, 486.683, 258.946, 258.946, 258.946, 258.946, 521.828, 561.86, 535.011, 535.011, 535.011, 535.011, 535.011, 685.273, 535.297, 557.952, 557.952, 557.952, 557.952, 483.885, 541.844, 483.885, 631.842, 541.844, 631.842, 541.844, 631.842, 541.844, 639.013, 436.035, 639.013, 436.035, 639.013, 436.035, 639.013, 436.035, 711.748, 596.194, 711.748, 541.844, 499.029, 486.683, 499.029, 486.683, 499.029, 486.683, 499.029, 486.683, 499.029, 486.683, 688.133, 541.844, 688.133, 541.844, 688.133, 541.844, 778, 556, 707.842, 556.979, 707.842, 556.979, 266.274, 258.946, 266.274, 258.946, 266.274, 258.946, 266.274, 258.594, 266.274, 258.946, 618.806, 500.659, 346.19, 240.053, 667, 500, 490.235, 462.764, 258.594, 556, 222, 471.06, 294.659, 471.06, 315.02, 462.764, 274.694, 750.801, 561.86, 722, 556, 750.801, 561.86, 606.206, 750.828, 561.86, 754.069, 535.011, 754.069, 535.011, 754.069, 535.011, 923.83, 851.214, 581.896, 354.971, 722, 333, 581.896, 354.971, 497.075, 395.02, 497.075, 395.02, 497.075, 395.02, 497.075, 395.02, 611, 278, 526.016, 363.767, 524.061, 323.436, 688.802, 557.952, 688.802, 557.952, 688.802, 557.952, 688.802, 557.952, 688.802, 557.952, 688.802, 557.952, 929.349, 720.051, 554.204, 483.885, 554.204, 569.971, 452.15, 569.971, 452.15, 569.971, 452.15, 292.747, 538.914, 631.842, 541.844, 854.98, 826.023, 754.069, 535.297, 667, 500, 526.016, 323.436, 349.114, 349.114, 349.114, 349.114, 349.114, 349.114, 349.114, 349.114, 349.114, 272.943, 272.943, 631.842, 216.929, 560.364, 768.87, 327.425, 793.023, 671.205, 819.439, 266.4, 631.842, 573.246, 471.909, 644.63, 499.029, 569.971, 707.842, 754.069, 266.274, 599.62, 655.768, 817.864, 750.801, 509.419, 754.069, 707.905, 575.201, 516.45, 526.016, 554.204, 753.986, 590.829, 808.53, 770.771, 266.274, 554.204, 625.632, 456.56, 573.548, 266.4, 587.125, 625.632, 560.751, 519.045, 584.339, 456.56, 441.535, 573.548, 571.273, 266.4, 510.74, 497.855, 573.888, 506.35, 459.212, 535.011, 600.655, 560.297, 454.77, 571.753, 488.739, 587.125, 729.106, 545.023, 768.55, 804.395, 266.4, 587.125, 535.011, 587.125, 804.395, 499.029, 667, 843, 544, 708, 667, 278, 278, 500, 1066, 982, 844, 589, 741.841, 639, 724, 667, 651, 667, 544, 704, 667, 917, 614, 715, 715, 589, 686, 833, 722, 778, 725, 667, 722, 611, 639, 795, 667, 727, 673, 920, 923, 805, 886, 651, 694, 1022, 682, 556, 562, 522, 493, 553, 556, 688, 465, 556, 556, 472, 564, 686, 550, 556, 556, 556, 500, 833, 500, 835, 500, 572, 518, 830, 851, 621, 736, 526, 492, 752, 534, 486.683, 556, 556, 378, 496, 500, 222, 222, 222, 910, 828, 556, 472, 557.683, 500, 556, 778, 556, 492, 339, 929.349, 720.051, 929.349, 720.051, 929.349, 720.051, 554.204, 483.885, 333, 399.893, 499.861, 1000, 1000, 414.856, 210.778, 210.778, 210.778, 210.778, 374.016, 374.016, 374.016, 375.873, 375.873, 404.674, 735.84, 1211.91, 229.614, 376.95, 315.91, 315.91, 479.98, 415.028, 167, 392.364, 487.662, 538.914, 1110.115, 538.914, 885, 323, 1083, 772.95, 768, 600, 916.032, 916.032, 916.032, 846.693, 863.21, 472.17, 863.17, 472.17, 861.82, 500, 500, 535.002, 612, 753.687, 616.303, 685.079, 650.104, 817.02, 683.587, 701.902, 539.207, 685.273, 685.273, 685.079, 685.273, 685.273, 604, 584, 604, 604, 708, 625, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 729, 861.322, 604, 353.511, 353.511, 1000, 861.548, 861.548, 861.548, 861.548, 582.841, 861.322, 604, 861.322, 861.322, 353.511, 1021, 1052, 917, 750, 750, 684.247, 684.306, 684.858, 684.231, 500, 750, 750, 553.71, 553.71, 333];
const SegoeuiBoldItalicWidths = [275.07, 346.183, 493.036, 571.618, 576.016, 868.011, 801.081, 293.223, 370.11, 370.11, 454.893, 708.024, 271.161, 403.799, 271.161, 444.116, 576.016, 576.016, 576.016, 576.016, 576.016, 576.016, 576.016, 576.016, 576.016, 576.016, 270.989, 270.989, 708.024, 708.024, 708.024, 449.336, 954.008, 702.961, 647.8, 634.616, 752.757, 529.791, 521.134, 709.139, 763.984, 317.087, 442.748, 650.724, 512.342, 869.619, 798.64, 758.955, 629.888, 758.955, 639.988, 537.115, 587.067, 722.975, 667, 1007.975, 654.794, 607.917, 607.096, 370.11, 437.275, 370.11, 708.024, 414.926, 438.954, 592.613, 592.927, 461.786, 592.927, 519.877, 374.991, 592.927, 598.303, 317.576, 262.855, 561.86, 317.576, 910.967, 598.303, 575.348, 592.927, 592.927, 399.736, 438.845, 374.991, 598.303, 533.059, 795.093, 547.699, 537.941, 485.84, 369.966, 326.421, 369.966, 708.024, 346.183, 576.016, 576.016, 556, 576.016, 326.421, 485.705, 438.954, 875.217, 469.09, 585.779, 708.024, 874.244, 438.768, 379.976, 708.232, 413.077, 413.077, 438.954, 576, 508.651, 271.161, 438.954, 377.922, 455.761, 585.779, 950.21, 963.887, 979.024, 449.336, 702.961, 702.961, 702.961, 702.961, 702.961, 702.961, 943.85, 634.616, 529.791, 529.791, 529.791, 529.791, 317.087, 317.087, 317.087, 317.087, 752.757, 798.64, 758.955, 758.955, 758.955, 758.955, 758.955, 708.024, 758.955, 722.975, 722.975, 722.975, 722.975, 607.917, 629.888, 603.185, 592.613, 592.613, 592.613, 592.613, 592.613, 592.613, 859.707, 461.786, 519.877, 519.877, 519.877, 519.877, 317.576, 317.576, 317.576, 317.576, 568.994, 598.303, 575.348, 575.348, 575.348, 575.348, 575.348, 708.232, 575.348, 598.303, 598.303, 598.303, 598.303, 537.941, 592.927, 537.941, 702.961, 592.613, 702.961, 592.613, 702.961, 592.613, 634.616, 461.786, 634.616, 461.786, 634.616, 461.786, 634.616, 461.786, 752.757, 742.442, 752.757, 592.927, 529.791, 519.877, 529.791, 519.877, 529.791, 519.877, 529.791, 519.877, 529.791, 519.877, 709.139, 592.927, 709.139, 592.927, 709.139, 592.927, 778, 611, 763.984, 598.303, 763.984, 598.303, 317.087, 317.576, 317.087, 317.576, 317.087, 317.576, 317.087, 317.576, 317.087, 317.576, 761.989, 579.919, 442.748, 282.398, 722, 556, 561.86, 512.342, 317.576, 611, 278, 512.342, 432.135, 512.342, 433.102, 512.342, 332.232, 798.64, 598.303, 722, 611, 798.64, 598.303, 715.321, 798.669, 598.303, 758.955, 575.348, 758.955, 575.348, 758.955, 575.348, 916.02, 868.301, 639.988, 399.736, 722, 389, 639.988, 399.736, 537.115, 438.845, 537.115, 438.845, 537.115, 438.845, 537.115, 438.845, 611, 333, 587.067, 504.88, 586.09, 389.15, 722.975, 598.303, 722.975, 598.303, 722.975, 598.303, 722.975, 598.303, 722.975, 598.303, 722.975, 598.303, 1007.975, 795.093, 607.917, 537.941, 607.917, 607.096, 485.84, 607.096, 485.84, 607.096, 485.84, 342.493, 576.016, 702.961, 592.613, 943.85, 859.707, 758.955, 575.348, 667, 556, 587.067, 374.991, 438.954, 438.954, 438.954, 438.954, 438.954, 438.954, 438.954, 438.954, 438.954, 316.886, 423.33, 727.855, 277.336, 663.575, 898.181, 450.547, 873.287, 801.121, 908.026, 317.576, 702.961, 647.8, 529.901, 736.879, 529.791, 607.096, 763.984, 758.955, 317.087, 650.724, 711.436, 869.619, 798.64, 557.911, 758.955, 751.703, 629.888, 578.289, 587.067, 607.917, 824.926, 654.794, 845.124, 773.182, 317.087, 607.917, 650.758, 474.114, 614.904, 317.576, 635.408, 650.758, 618.559, 572.107, 614.88, 474.114, 468.059, 614.904, 620.055, 317.576, 568.932, 544.774, 616.182, 556, 488.601, 575.348, 707.116, 595.241, 494.909, 620.023, 552.661, 635.408, 779.236, 540.378, 805.713, 836.927, 317.576, 635.408, 575.348, 635.408, 836.927, 529.791, 667, 886, 614, 719, 667, 278, 278, 556, 1094, 1042, 854, 622, 784.407, 677, 719, 722, 708, 722, 614, 722, 667, 927, 643, 719, 719, 615, 687, 833, 722, 778, 719, 667, 722, 611, 677, 781, 667, 729, 708, 979, 989, 854, 1000, 708, 719, 1042, 729, 556, 619, 604, 534, 618, 556, 736, 510, 611, 611, 507, 622, 740, 604, 611, 611, 611, 556, 889, 556, 885, 556, 646, 583, 889, 935, 707, 854, 594, 552, 865, 589, 519.877, 556, 611, 469, 563, 556, 278, 278, 278, 969, 906, 611, 507, 598.01, 556, 611, 778, 611, 575, 467, 1007.975, 795.093, 1007.975, 795.093, 1007.975, 795.093, 607.917, 537.941, 333, 403.799, 499.861, 1000, 1000, 414.856, 291.191, 291.191, 291.191, 290.215, 493.165, 493.165, 493.165, 405.652, 405.652, 421.757, 914.06, 1265.14, 293.28, 493.159, 344.229, 344.229, 580.076, 415.028, 167, 432.135, 518.898, 574.548, 1286.613, 576.016, 885, 516, 1146, 771, 768, 600, 963.887, 963.887, 963.887, 963.887, 862.776, 472.17, 862.79, 472.17, 862.24, 500, 500, 576.982, 612, 790.78, 642.185, 708.024, 650.104, 812.136, 683.587, 701.495, 576.329, 708.232, 708.232, 707.406, 708.232, 708.232, 604, 584, 604, 604, 708, 625, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 708, 729, 861.322, 604, 353.511, 353.511, 1000, 861.548, 861.548, 861.548, 861.548, 586.749, 861.322, 604, 861.322, 861.322, 353.511, 1021, 1052, 917, 750, 750, 684.247, 684.306, 684.858, 684.231, 500, 750, 750, 692.074, 692.074, 333];

// EXTERNAL MODULE: ./src/core/primitives.js
var primitives = __webpack_require__(5632);
// EXTERNAL MODULE: ./src/core/fonts_utils.js
var fonts_utils = __webpack_require__(720);
;// ./src/core/xfa_fonts.js




const DEFAULT_METRICS = {
  lineHeight: 1.2,
  lineGap: 0.2
};
const HELVETICA_ITALIC_METRICS = {
  lineHeight: 1.35,
  lineGap: 0.2
};
const getXFAFontMap = (0,core_utils.getLookupTableFactory)(function (t) {
  const styles = [["Regular", "Helvetica"], ["Bold", "Helvetica-Bold"], ["Italic", "Helvetica-Oblique"], ["BoldItalic", "Helvetica-BoldOblique"]];
  for (const [family, widths, metrics] of [["Arial", [ArialRegularWidths, ArialBoldWidths, ArialItalicWidths, ArialBoldItalicWidths], null], ["Calibri", [CalibriRegularWidths, CalibriBoldWidths, CalibriItalicWidths, CalibriBoldItalicWidths], {
    lineHeight: 1.2207,
    lineGap: 0.2207
  }], ["Helvetica", [HelveticaRegularWidths, HelveticaBoldWidths, HelveticaItalicWidths, HelveticaBoldItalicWidths], DEFAULT_METRICS], ["MyriadPro", [MyriadProRegularWidths, MyriadProBoldWidths, MyriadProItalicWidths, MyriadProBoldItalicWidths], DEFAULT_METRICS], ["Segoeui", [SegoeuiRegularWidths, SegoeuiBoldWidths, SegoeuiItalicWidths, SegoeuiBoldItalicWidths], {
    lineHeight: 1.33008,
    lineGap: 0
  }]]) {
    for (const [i, [style, name]] of styles.entries()) {
      const font = {
        name,
        widths: widths[i]
      };
      if (metrics) {
        font.metrics = family === "Helvetica" && i >= 2 ? HELVETICA_ITALIC_METRICS : metrics;
      }
      t[`${family}-${style}`] = font;
    }
  }
  for (const [style] of styles) {
    t[`PdfJS-Fallback-${style}`] = t[`MyriadPro-${style}`];
  }
  t["MyriadPro-It"] = t["MyriadPro-Italic"];
  t["MyriadPro-BoldIt"] = t["MyriadPro-BoldItalic"];
  t.ArialMT = t.Arial = t["Arial-Regular"];
  t["Arial-BoldMT"] = t["Arial-Bold"];
  t["Arial-ItalicMT"] = t["Arial-Italic"];
  t["Arial-BoldItalicMT"] = t["Arial-BoldItalic"];
  t.Helvetica = t["Helvetica-Regular"];
});
function getXfaFontName(name) {
  return getXFAFontMap()[(0,fonts_utils.normalizeFontName)(name)];
}
function getXfaFontDict(name) {
  const {
    widths
  } = getXfaFontName(name);
  const newWidths = [];
  for (let i = 0, j = 0, ii = XfaFontCodePoints.length; i < ii; i += 2) {
    const count = XfaFontCodePoints[i + 1];
    newWidths.push(XfaFontCodePoints[i], widths.slice(j, j + count));
    j += count;
  }
  const dict = new primitives.Dict(null);
  dict.set("BaseFont", primitives.Name.get(name));
  dict.set("Type", primitives.Name.get("Font"));
  dict.set("Subtype", primitives.Name.get("CIDFontType2"));
  dict.set("Encoding", primitives.Name.get("Identity-H"));
  dict.set("CIDToGIDMap", primitives.Name.get("Identity"));
  dict.set("W", newWidths);
  dict.set("FirstChar", newWidths[0]);
  dict.set("LastChar", newWidths.at(-2) + newWidths.at(-1).length - 1);
  const descriptor = new primitives.Dict(null);
  dict.set("FontDescriptor", descriptor);
  const systemInfo = new primitives.Dict(null);
  systemInfo.set("Ordering", "Identity");
  systemInfo.set("Registry", "Adobe");
  systemInfo.set("Supplement", 0);
  dict.set("CIDSystemInfo", systemInfo);
  return dict;
}

;// ./src/core/xfa/factory.js







class XFAFactory {
  constructor(data) {
    try {
      this.root = new XFAParser().parse(XFAFactory._createDocument(data));
      const binder = new Binder(this.root);
      this.form = binder.bind();
      this.dataHandler = new DataHandler(this.root, binder.getData());
      this.form[symbol_utils.$globalData].template = this.form;
    } catch (e) {
      ;(0,util.warn)(`XFA - an error occurred during parsing and binding: ${e}`);
    }
  }
  isValid() {
    return !!(this.root && this.form);
  }
  _createPagesHelper() {
    const iterator = this.form[symbol_utils.$toPages]();
    return new Promise((resolve, reject) => {
      const nextIteration = () => {
        try {
          const value = iterator.next();
          if (value.done) {
            resolve(value.value);
          } else {
            setTimeout(nextIteration, 0);
          }
        } catch (e) {
          reject(e);
        }
      };
      setTimeout(nextIteration, 0);
    });
  }
  async _createPages() {
    try {
      this.pages = await this._createPagesHelper();
      this.dims = this.pages.children.map(c => {
        const {
          width,
          height
        } = c.attributes.style;
        return [0, 0, parseInt(width, 10), parseInt(height, 10)];
      });
    } catch (e) {
      ;(0,util.warn)(`XFA - an error occurred during layout: ${e}`);
    }
  }
  getBoundingBox(pageIndex) {
    return this.dims[pageIndex];
  }
  async getNumPages() {
    if (!this.pages) {
      await this._createPages();
    }
    return this.dims.length;
  }
  setImages(images) {
    this.form[symbol_utils.$globalData].images = images;
  }
  setFonts(fonts) {
    this.form[symbol_utils.$globalData].fontFinder = new xfa_fonts.FontFinder(fonts);
    const missingFonts = [];
    for (let typeface of this.form[symbol_utils.$globalData].usedTypefaces) {
      typeface = (0,utils.stripQuotes)(typeface);
      const font = this.form[symbol_utils.$globalData].fontFinder.find(typeface);
      if (!font) {
        missingFonts.push(typeface);
      }
    }
    return missingFonts.length > 0 ? missingFonts : null;
  }
  appendFonts(fonts, reallyMissingFonts) {
    this.form[symbol_utils.$globalData].fontFinder.add(fonts, reallyMissingFonts);
  }
  async getPages() {
    if (!this.pages) {
      await this._createPages();
    }
    const pages = this.pages;
    this.pages = null;
    return pages;
  }
  serializeData(storage) {
    return this.dataHandler.serialize(storage);
  }
  static _createDocument(data) {
    return !data.get("/xdp:xdp") ? data.get("xdp:xdp") : data.values().join("");
  }
}



/***/ },

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
