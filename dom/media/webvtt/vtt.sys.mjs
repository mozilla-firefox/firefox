/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Code below is vtt.js the JS WebVTT implementation.
 * Current source code can be found at http://github.com/mozilla/vtt.js
 *
 * Code taken from commit b89bfd06cd788a68c67e03f44561afe833db0849
 */
/**
 * Copyright 2013 vtt.js Contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

XPCOMUtils.defineLazyPreferenceGetter(lazy, "DEBUG_LOG",
                                      "media.webvtt.debug.logging", false);

function LOG(message) {
  if (lazy.DEBUG_LOG) {
    dump("[vtt] " + message + "\n");
  }
}

var _objCreate = Object.create || (function() {
  function F() {}
  return function(o) {
    if (arguments.length !== 1) {
      throw new Error('Object.create shim only accepts one parameter.');
    }
    F.prototype = o;
    return new F();
  };
})();

// Creates a new ParserError object from an errorData object. The errorData
// object should have default code and message properties. The default message
// property can be overriden by passing in a message parameter.
// See ParsingError.Errors below for acceptable errors.
function ParsingError(errorData, message) {
  this.name = "ParsingError";
  this.code = errorData.code;
  this.message = message || errorData.message;
}
ParsingError.prototype = _objCreate(Error.prototype);
ParsingError.prototype.constructor = ParsingError;

// ParsingError metadata for acceptable ParsingErrors.
ParsingError.Errors = {
  BadSignature: {
    code: 0,
    message: "Malformed WebVTT signature."
  },
  BadTimeStamp: {
    code: 1,
    message: "Malformed time stamp."
  }
};

// See spec, https://w3c.github.io/webvtt/#collect-a-webvtt-timestamp.
function collectTimeStamp(input) {
  function computeSeconds(h, m, s, f) {
    if (m > 59 || s > 59) {
      return null;
    }
    // The attribute of the milli-seconds can only be three digits.
    if (f.length !== 3) {
      return null;
    }
    return (h | 0) * 3600 + (m | 0) * 60 + (s | 0) + (f | 0) / 1000;
  }

  let timestamp = input.match(/^(\d+:)?(\d{2}):(\d{2})\.(\d+)/);
  if (!timestamp || timestamp.length !== 5) {
    return null;
  }

  let hours = timestamp[1]? timestamp[1].replace(":", "") : 0;
  let minutes = timestamp[2];
  let seconds = timestamp[3];
  let milliSeconds = timestamp[4];

  return computeSeconds(hours, minutes, seconds, milliSeconds);
}

// A settings object holds key/value pairs and will ignore anything but the first
// assignment to a specific key.
function Settings() {
  this.values = _objCreate(null);
}

Settings.prototype = {
  set: function(k, v) {
    if (v !== "") {
      this.values[k] = v;
    }
  },
  // Return the value for a key, or a default value.
  // If 'defaultKey' is passed then 'dflt' is assumed to be an object with
  // a number of possible default values as properties where 'defaultKey' is
  // the key of the property that will be chosen; otherwise it's assumed to be
  // a single value.
  get: function(k, dflt, defaultKey) {
    if (defaultKey) {
      return this.has(k) ? this.values[k] : dflt[defaultKey];
    }
    return this.has(k) ? this.values[k] : dflt;
  },
  // Check whether we have a value for a key.
  has: function(k) {
    return k in this.values;
  },
  // Accept a setting if its one of the given alternatives.
  alt: function(k, v, a) {
    for (let n = 0; n < a.length; ++n) {
      if (v === a[n]) {
        this.set(k, v);
        return true;
      }
    }
    return false;
  },
  // Accept a setting if its a valid digits value (int or float)
  digitsValue: function(k, v) {
    if (/^-0+(\.[0]*)?$/.test(v)) { // special case for -0.0
      this.set(k, 0.0);
    } else if (/^-?\d+(\.[\d]*)?$/.test(v)) {
      this.set(k, parseFloat(v));
    }
  },
  // Accept a setting if its a valid percentage.
  percent: function(k, v) {
    let m;
    if ((m = v.match(/^([\d]{1,3})(\.[\d]*)?%$/))) {
      v = parseFloat(v);
      if (v >= 0 && v <= 100) {
        this.set(k, v);
        return true;
      }
    }
    return false;
  },
  // Delete a setting
  del: function (k) {
    if (this.has(k)) {
      delete this.values[k];
    }
  },
};

// Helper function to parse input into groups separated by 'groupDelim', and
// interprete each group as a key/value pair separated by 'keyValueDelim'.
function parseOptions(input, callback, keyValueDelim, groupDelim) {
  let groups = groupDelim ? input.split(groupDelim) : [input];
  for (let i in groups) {
    if (typeof groups[i] !== "string") {
      continue;
    }
    let kv = groups[i].split(keyValueDelim);
    if (kv.length !== 2) {
      continue;
    }
    let k = kv[0];
    let v = kv[1];
    callback(k, v);
  }
}

function parseCue(input, cue, regionList) {
  // Remember the original input if we need to throw an error.
  let oInput = input;
  // 4.1 WebVTT timestamp
  function consumeTimeStamp() {
    let ts = collectTimeStamp(input);
    if (ts === null) {
      throw new ParsingError(ParsingError.Errors.BadTimeStamp,
                            "Malformed timestamp: " + oInput);
    }
    // Remove time stamp from input.
    input = input.replace(/^[^\s\uFFFDa-zA-Z-]+/, "");
    return ts;
  }

  // 4.4.2 WebVTT cue settings
  function consumeCueSettings(input, cue) {
    let settings = new Settings();
    parseOptions(input, function (k, v) {
      switch (k) {
      case "region":
        // Find the last region we parsed with the same region id.
        for (let i = regionList.length - 1; i >= 0; i--) {
          if (regionList[i].id === v) {
            settings.set(k, regionList[i].region);
            break;
          }
        }
        break;
      case "vertical":
        settings.alt(k, v, ["rl", "lr"]);
        break;
      case "line": {
        let vals = v.split(",");
        let vals0 = vals[0];
        settings.digitsValue(k, vals0);
        settings.percent(k, vals0) ? settings.set("snapToLines", false) : null;
        settings.alt(k, vals0, ["auto"]);
        if (vals.length === 2) {
          settings.alt("lineAlign", vals[1], ["start", "center", "end"]);
        }
        break;
      }
      case "position": {
        let vals = v.split(",");
        if (settings.percent(k, vals[0])) {
          if (vals.length === 2) {
            if (!settings.alt("positionAlign", vals[1], ["line-left", "center", "line-right"])) {
              // Remove the "position" value because the "positionAlign" is not expected value.
              // It will be set to default value below.
              settings.del(k);
            }
          }
        }
        break;
      }
      case "size":
        settings.percent(k, v);
        break;
      case "align":
        settings.alt(k, v, ["start", "center", "end", "left", "right"]);
        break;
      }
    }, /:/, /\t|\n|\f|\r| /); // groupDelim is ASCII whitespace

    // Apply default values for any missing fields.
    // https://w3c.github.io/webvtt/#collect-a-webvtt-block step 11.4.1.3
    cue.region = settings.get("region", null);
    cue.vertical = settings.get("vertical", "");
    cue.line = settings.get("line", "auto");
    cue.lineAlign = settings.get("lineAlign", "start");
    cue.snapToLines = settings.get("snapToLines", true);
    cue.size = settings.get("size", 100);
    cue.align = settings.get("align", "center");
    cue.position = settings.get("position", "auto");
    cue.positionAlign = settings.get("positionAlign", "auto");
  }

  function skipWhitespace() {
    input = input.replace(/^[ \f\n\r\t]+/, "");
  }

  // 4.1 WebVTT cue timings.
  skipWhitespace();
  cue.startTime = consumeTimeStamp();   // (1) collect cue start time
  skipWhitespace();
  if (input.substr(0, 3) !== "-->") {     // (3) next characters must match "-->"
    throw new ParsingError(ParsingError.Errors.BadTimeStamp,
                            "Malformed time stamp (time stamps must be separated by '-->'): " +
                            oInput);
  }
  input = input.substr(3);
  skipWhitespace();
  cue.endTime = consumeTimeStamp();     // (5) collect cue end time

  // 4.1 WebVTT cue settings list.
  skipWhitespace();
  consumeCueSettings(input, cue);
}

function emptyOrOnlyContainsWhiteSpaces(input) {
  return input == "" || /^[ \f\n\r\t]+$/.test(input);
}

function containsTimeDirectionSymbol(input) {
  return input.includes("-->");
}

function maybeIsTimeStampFormat(input) {
  return /^\s*(\d+:)?(\d{2}):(\d{2})\.(\d+)\s*-->\s*(\d+:)?(\d{2}):(\d{2})\.(\d+)\s*/.test(input);
}

var ESCAPE = {
  "&amp;": "&",
  "&lt;": "<",
  "&gt;": ">",
  "&lrm;": "\u200e",
  "&rlm;": "\u200f",
  "&nbsp;": "\u00a0"
};

var TAG_NAME = {
  c: "span",
  i: "i",
  b: "b",
  u: "u",
  ruby: "ruby",
  rt: "rt",
  v: "span",
  lang: "span"
};

var TAG_ANNOTATION = {
  v: "title",
  lang: "lang"
};

var NEEDS_PARENT = {
  rt: "ruby"
};

const PARSE_CONTENT_MODE = {
  NORMAL_CUE: "normal_cue",
  DOCUMENT_FRAGMENT: "document_fragment",
  REGION_CUE: "region_cue",
}
// Parse content into a document fragment.
function parseContent(window, input, mode) {
  function nextToken() {
    // Check for end-of-string.
    if (!input) {
      return null;
    }

    // Consume 'n' characters from the input.
    function consume(result) {
      input = input.substr(result.length);
      return result;
    }

    let m = input.match(/^([^<]*)(<[^>]+>?)?/);
    // The input doesn't contain a complete tag.
    if (!m[0]) {
      return null;
    }
    // If there is some text before the next tag, return it, otherwise return
    // the tag.
    return consume(m[1] ? m[1] : m[2]);
  }

  const unescapeHelper = window.document.createElement("div");
  function unescapeEntities(s) {
    let match;

    // Decimal numeric character reference
    s = s.replace(/&#(\d+);?/g, (candidate, number) => {
      try {
        const codepoint = parseInt(number);
        return String.fromCodePoint(codepoint);
      } catch (_) {
        return candidate;
      }
    });

    // Hexadecimal numeric character reference
    s = s.replace(/&#x([\dA-Fa-f]+);?/g, (candidate, number) => {
      try {
        const codepoint = parseInt(number, 16);
        return String.fromCodePoint(codepoint);
      } catch (_) {
        return candidate;
      }
    });

    // Named character references
    s = s.replace(/&\w[\w\d]*;?/g, candidate => {
      // The list of entities is huge, so we use setHTML instead.
      // Ideally we would be able to use a faster/simpler variant of setHTML (bug 1731215).
      unescapeHelper.setHTML(candidate);
      const unescaped = unescapeHelper.innerText;
      if (unescaped == candidate) { // not a valid entity
        return candidate;
      }
      return unescaped;
    });
    unescapeHelper.setHTML("");

    return s;
  }

  function shouldAdd(current, element) {
    return !NEEDS_PARENT[element.localName] ||
            NEEDS_PARENT[element.localName] === current.localName;
  }

  // Create an element for this tag.
  function createElement(type, annotation) {
    let tagName = TAG_NAME[type];
    if (!tagName) {
      return null;
    }
    let element = window.document.createElement(tagName);
    let name = TAG_ANNOTATION[type];
    if (name) {
      element[name] = annotation ? annotation.trim() : "";
    }
    return element;
  }

  // https://w3c.github.io/webvtt/#webvtt-timestamp-object
  // Return hhhhh:mm:ss.fff
  function normalizedTimeStamp(secondsWithFrag) {
    let totalsec = parseInt(secondsWithFrag, 10);
    let hours = Math.floor(totalsec / 3600);
    let minutes = Math.floor(totalsec % 3600 / 60);
    let seconds = Math.floor(totalsec % 60);
    if (hours < 10) {
      hours = "0" + hours;
    }
    if (minutes < 10) {
      minutes = "0" + minutes;
    }
    if (seconds < 10) {
      seconds = "0" + seconds;
    }
    let f = secondsWithFrag.toString().split(".");
    if (f[1]) {
      f = f[1].slice(0, 3).padEnd(3, "0");
    } else {
      f = "000";
    }
    return hours + ':' + minutes + ':' + seconds + '.' + f;
  }

  let root;
  switch (mode) {
    case PARSE_CONTENT_MODE.NORMAL_CUE:
      root = window.document.createElement("span", {pseudo: "::cue"});
      break;
    case PARSE_CONTENT_MODE.REGION_CUE:
      root = window.document.createElement("span");
      break;
    case PARSE_CONTENT_MODE.DOCUMENT_FRAGMENT:
      root = window.document.createDocumentFragment();
      break;
  }

  if (!input) {
    root.appendChild(window.document.createTextNode(""));
    return root;
  }

  let current = root,
      t,
      tagStack = [];

  while ((t = nextToken()) !== null) {
    if (t[0] === '<') {
      if (t[1] === "/") {
        const endTag = t.slice(2, -1);
        const stackEnd = tagStack.at(-1);

        // If the closing tag matches, move back up to the parent node.
        if (stackEnd == endTag) {
          tagStack.pop();
          current = current.parentNode;

        // If the closing tag is <ruby> and we're at an <rt>, move back up to
        // the <ruby>'s parent node.
        } else if (endTag == "ruby" && current.nodeName == "RT") {
          tagStack.pop();
          current = current.parentNode.parentNode;
        }

        // Otherwise just ignore the end tag.
        continue;
      }
      let ts = collectTimeStamp(t.substr(1, t.length - 1));
      let node;
      if (ts) {
        // Timestamps are lead nodes as well.
        node = window.document.createProcessingInstruction("timestamp", normalizedTimeStamp(ts));
        current.appendChild(node);
        continue;
      }
      let m = t.match(/^<([^.\s/0-9>]+)(\.[^\s\\>]+)?([^>\\]+)?(\\?)>?$/);
      // If we can't parse the tag, skip to the next tag.
      if (!m) {
        continue;
      }
      // Try to construct an element, and ignore the tag if we couldn't.
      node = createElement(m[1], m[3]);
      if (!node) {
        continue;
      }
      // Determine if the tag should be added based on the context of where it
      // is placed in the cuetext.
      if (!shouldAdd(current, node)) {
        continue;
      }
      // Set the class list (as a list of classes, separated by space).
      if (m[2]) {
        node.className = m[2].substr(1).replace('.', ' ');
      }
      // Append the node to the current node, and enter the scope of the new
      // node.
      tagStack.push(m[1]);
      current.appendChild(node);
      current = node;
      continue;
    }

    // Text nodes are leaf nodes.
    current.appendChild(window.document.createTextNode(unescapeEntities(t)));
  }

  return root;
}

function StyleBox() {
}

// Apply styles to a div. If there is no div passed then it defaults to the
// div on 'this'.
StyleBox.prototype.applyStyles = function(styles, div) {
  div = div || this.div;
  for (let prop in styles) {
    if (styles.hasOwnProperty(prop)) {
      div.style[prop] = styles[prop];
    }
  }
};

StyleBox.prototype.formatStyle = function(val, unit) {
  return val === 0 ? 0 : val + unit;
};

// TODO(alwu): remove StyleBox and change other style box to class-based.
class StyleBoxBase {
  applyStyles(styles, div) {
    div = div || this.div;
    Object.assign(div.style, styles);
  }

  formatStyle(val, unit) {
    return val === 0 ? 0 : val + unit;
  }
}

// Constructs the computed display state of the cue (a div). Places the div
// into the overlay which should be a block level element (usually a div).
class CueStyleBox extends StyleBoxBase {
  constructor(window, cue, containerBox) {
    super();
    this.cue = cue;
    this.div = window.document.createElement("div");
    this.cueDiv = parseContent(window, cue.text, PARSE_CONTENT_MODE.NORMAL_CUE);
    this.div.appendChild(this.cueDiv);

    this.containerHeight = containerBox.height;
    this.containerWidth = containerBox.width;
    this.fontSize = this._getFontSize(containerBox);
    this.isCueStyleBox = true;

    // As pseudo element won't inherit the parent div's style, so we have to
    // set the font size explicitly.
    this._applyDefaultStylesOnBackgroundNode();
    this._applyDefaultStylesOnRootNode();
  }

  getCueBoxPositionAndSize() {
    // As `top`, `left`, `width` and `height` are all represented by the
    // percentage of the container, we need to convert them to the actual
    // number according to the container's size.
    const isWritingDirectionHorizontal = this.cue.vertical == "";
    let top =
          this.containerHeight * this._tranferPercentageToFloat(this.div.style.top),
        left =
          this.containerWidth * this._tranferPercentageToFloat(this.div.style.left),
        width = isWritingDirectionHorizontal ?
          this.containerWidth * this._tranferPercentageToFloat(this.div.style.width) :
          this.div.clientWidthDouble,
        height = isWritingDirectionHorizontal ?
          this.div.clientHeightDouble :
          this.containerHeight * this._tranferPercentageToFloat(this.div.style.height);
    return { top, left, width, height };
  }

  getFirstLineBoxSize() {
    // This size would be automatically adjusted by writing direction. When
    // direction is horizontal, it represents box's height. When direction is
    // vertical, it represents box's width.
    return this.div.firstLineBoxBSize;
  }

  setBidiRule() {
    // This function is a workaround which is used to force the reflow in order
    // to use the correct alignment for bidi text. Now this function would be
    // called after calculating the final position of the cue box to ensure the
    // rendering result is correct. See bug1557882 comment3 for more details.
    // TODO : remove this function and set `unicode-bidi` when initiailizing
    // the CueStyleBox, after fixing bug1558431.
    this.applyStyles({ "unicode-bidi": "plaintext" });
  }

  /**
   * Following methods are private functions, should not use them outside this
   * class.
   */
  _tranferPercentageToFloat(input) {
    return input.replace("%", "") / 100.0;
  }

  _getFontSize(containerBox) {
    // In https://www.w3.org/TR/webvtt1/#applying-css-properties, the spec
    // said the font size is '5vh', which means 5% of the viewport height.
    // However, if we use 'vh' as a basic unit, it would eventually become
    // 5% of screen height, instead of video's viewport height. Therefore, we
    // have to use 'px' here to make sure we have the correct font size.
    //
    // Note Chromium uses min(width, height) instead of just height, to not
    // make the font unexpectedly large on portrait videos. This matches that
    // behavior.
    // TODO: Update this when the spec has settled
    //       https://github.com/w3c/webvtt/issues/529.
    return Math.min(containerBox.width, containerBox.height) * 0.05 + "px";
  }

  _applyDefaultStylesOnBackgroundNode() {
    // most of the properties have been defined in `::cue` in `html.css`, but
    // there are some css properties we have to set them dynamically.
    // FIXME(emilio): These are observable by content. Ideally the style
    // attribute will work like for ::part() and we wouldn't need this.
    this.cueDiv.style.setProperty("--cue-font-size", this.fontSize, "important");
    this.cueDiv.style.setProperty("--cue-writing-mode", this._getCueWritingMode(), "important");
  }

  // spec https://www.w3.org/TR/webvtt1/#applying-css-properties
  _applyDefaultStylesOnRootNode() {
    // The variables writing-mode, top, left, width, and height are calculated
    // in the spec 7.2, https://www.w3.org/TR/webvtt1/#processing-cue-settings
    // spec 7.2.1, calculate 'writing-mode'.
    const writingMode = this._getCueWritingMode();

    // spec 7.2.2 ~ 7.2.7, calculate 'width', 'height', 'left' and 'top'.
    const {width, height, left, top} = this._getCueSizeAndPosition();

    // TODO: https://github.com/w3c/webvtt/issues/530 for potentially making
    // the cue container's font-size 0.
    //
    // The inline cue element cannot make the size of the parent element, this
    // container, smaller. If the inline cue element is larger, the line height
    // grows. If it's smaller than the container, it's stuck with the
    // container's height. This becomes a problem when the container's font-size
    // is large and a site wants to style the ::cue pseudo element significantly
    // smaller. This is less of a problem when using the equivalent of 5vmin
    // instead of 5vh of course, but it's still a problem. It would be most
    // visible in large videos with 1:1 aspect ratio that a site tries to scale
    // down.
    //
    // All WebVTT use videos with min(width, height) 180, 5% of which is 9px.
    // 9px font-size keeps tests passing.
    const fontSize = "9px";

    this.applyStyles({
      "position": "absolute",
      // "unicode-bidi": "plaintext", (uncomment this line after fixing bug1558431)
      "writing-mode": writingMode,
      "top": top,
      "left": left,
      "width": width,
      "height": height,
      "overflow-wrap": "break-word",
      // "text-wrap": "balance", (we haven't supported this CSS attribute yet)
      "white-space": "pre-line",
      "font": `${fontSize} sans-serif`,
      "color": "rgba(255, 255, 255, 1)",
      "white-space": "pre-line",
      "text-align": this.cue.align,
    });
  }

  _getCueWritingMode() {
    const cue = this.cue;
    if (cue.vertical == "") {
      return "horizontal-tb";
    }
    return cue.vertical == "lr" ? "vertical-lr" : "vertical-rl";
  }

  _getCueSizeAndPosition() {
    const cue = this.cue;
    // spec 7.2.2, determine the value of maximum size for cue as per the
    // appropriate rules from the following list.
    let maximumSize;
    let computedPosition = cue.computedPosition;
    switch (cue.computedPositionAlign) {
      case "line-left":
        maximumSize = 100 - computedPosition;
        break;
      case "line-right":
        maximumSize = computedPosition;
        break;
      case "center":
        maximumSize = computedPosition <= 50 ?
          computedPosition * 2 : (100 - computedPosition) * 2;
        break;
    }
    const size = Math.min(cue.size, maximumSize);

    // spec 7.2.5, determine the value of x-position or y-position for cue as
    // per the appropriate rules from the following list.
    let xPosition = 0.0, yPosition = 0.0;
    const isWritingDirectionHorizontal = cue.vertical == "";
    switch (cue.computedPositionAlign) {
      case "line-left":
        if (isWritingDirectionHorizontal) {
          xPosition = cue.computedPosition;
        } else {
          yPosition = cue.computedPosition;
        }
        break;
      case "center":
        if (isWritingDirectionHorizontal) {
          xPosition = cue.computedPosition - (size / 2);
        } else {
          yPosition = cue.computedPosition - (size / 2);
        }
        break;
      case "line-right":
        if (isWritingDirectionHorizontal) {
          xPosition = cue.computedPosition - size;
        } else {
          yPosition = cue.computedPosition - size;
        }
        break;
    }

    // spec 7.2.6, determine the value of whichever of x-position or
    // y-position is not yet calculated for cue as per the appropriate rules
    // from the following list.
    if (!cue.snapToLines) {
      if (isWritingDirectionHorizontal) {
        yPosition = cue.computedLine;
      } else {
        xPosition = cue.computedLine;
      }
    } else {
      if (isWritingDirectionHorizontal) {
        yPosition = 0;
      } else {
        xPosition = 0;
      }
    }
    return {
      left: xPosition + "%",
      top: yPosition + "%",
      width: isWritingDirectionHorizontal ? size + "%" : "auto",
      height: isWritingDirectionHorizontal ? "auto" : size + "%",
    };
  }
}

function RegionNodeBox(window, region, container) {
  StyleBox.call(this);

  // TODO: Update this when the spec has settled
  //       https://github.com/w3c/webvtt/issues/529.
  let boxLineHeight = Math.min(container.width, container.height) * 0.0533 // 0.0533vh ? 5.33vh
  let boxHeight = boxLineHeight * region.lines;
  let boxWidth = container.width * region.width / 100; // convert percentage to px

  let regionNodeStyles = {
    position: "absolute",
    height: boxHeight + "px",
    width: boxWidth + "px",
    top: (region.viewportAnchorY * container.height / 100) - (region.regionAnchorY * boxHeight / 100) + "px",
    left: (region.viewportAnchorX * container.width / 100) - (region.regionAnchorX * boxWidth / 100) + "px",
    lineHeight: boxLineHeight + "px",
    writingMode: "horizontal-tb",
    backgroundColor: "rgba(0, 0, 0, 0.8)",
    wordWrap: "break-word",
    overflowWrap: "break-word",
    font: (boxLineHeight/1.3) + "px sans-serif",
    color: "rgba(255, 255, 255, 1)",
    overflow: "hidden",
    minHeight: "0px",
    maxHeight: boxHeight + "px",
    display: "inline-flex",
    flexFlow: "column",
    justifyContent: "flex-end",
  };

  this.div = window.document.createElement("div");
  this.div.id = region.id; // useless?
  this.applyStyles(regionNodeStyles);
}
RegionNodeBox.prototype = _objCreate(StyleBox.prototype);
RegionNodeBox.prototype.constructor = RegionNodeBox;

function RegionCueStyleBox(window, cue) {
  StyleBox.call(this);
  this.cueDiv = parseContent(window, cue.text, PARSE_CONTENT_MODE.REGION_CUE);

  let regionCueStyles = {
    position: "relative",
    writingMode: "horizontal-tb",
    unicodeBidi: "plaintext",
    width: "auto",
    height: "auto",
    textAlign: cue.align,
  };
  // TODO: fix me, LTR and RTL ? using margin replace the "left/right"
  // 6.1.14.3.3
  let offset = cue.computedPosition * cue.region.width / 100;
  // 6.1.14.3.4
  switch (cue.align) {
    case "start":
    case "left":
      regionCueStyles.left = offset + "%";
      regionCueStyles.right = "auto";
      break;
    case "end":
    case "right":
      regionCueStyles.left = "auto";
      regionCueStyles.right = offset + "%";
      break;
    case "middle":
      break;
  }

  this.div = window.document.createElement("div");
  this.applyStyles(regionCueStyles);
  this.div.appendChild(this.cueDiv);
}
RegionCueStyleBox.prototype = _objCreate(StyleBox.prototype);
RegionCueStyleBox.prototype.constructor = RegionCueStyleBox;

// Represents the co-ordinates of an Element in a way that we can easily
// compute things with such as if it overlaps or intersects with other boxes.
class BoxPosition {
  constructor(obj) {
    // Get dimensions by calling getCueBoxPositionAndSize on a CueStyleBox, by
    // getting offset properties from an HTMLElement (from the object or its
    // `div` property), otherwise look at the regular box properties on the
    // object.
    const isHTMLElement = !obj.isCueStyleBox && (obj.div || obj.tagName);
    obj = obj.isCueStyleBox ? obj.getCueBoxPositionAndSize() : obj.div || obj;
    this.top = isHTMLElement ? obj.offsetTop : obj.top;
    this.left = isHTMLElement ? obj.offsetLeft : obj.left;
    this.width = isHTMLElement ? obj.offsetWidth : obj.width;
    this.height = isHTMLElement ? obj.offsetHeight : obj.height;
    // This value is smaller than 1 app unit (~= 0.0166 px).
    this.fuzz = 0.01;
  }

  get bottom() {
    return this.top + this.height;
  }

  get right() {
    return this.left + this.width;
  }

  // This function is used for debugging, it will return the box's information.
  getBoxInfoInChars() {
    return `top=${this.top}, bottom=${this.bottom}, left=${this.left}, ` +
            `right=${this.right}, width=${this.width}, height=${this.height}`;
  }

  // Move the box along a particular axis. Optionally pass in an amount to move
  // the box. If no amount is passed then the default is the line height of the
  // box.
  move(axis, toMove) {
    switch (axis) {
    case "+x":
      LOG(`box's left moved from ${this.left} to ${this.left + toMove}`);
      this.left += toMove;
      break;
    case "-x":
      LOG(`box's left moved from ${this.left} to ${this.left - toMove}`);
      this.left -= toMove;
      break;
    case "+y":
      LOG(`box's top moved from ${this.top} to ${this.top + toMove}`);
      this.top += toMove;
      break;
    case "-y":
      LOG(`box's top moved from ${this.top} to ${this.top - toMove}`);
      this.top -= toMove;
      break;
    }
  }

  // Check if this box overlaps another box, b2.
  overlaps(b2) {
    return (this.left < b2.right - this.fuzz) &&
            (this.right > b2.left + this.fuzz) &&
            (this.top < b2.bottom - this.fuzz) &&
            (this.bottom > b2.top + this.fuzz);
  }

  // Check if this box overlaps any other boxes in boxes.
  overlapsAny(boxes) {
    for (let i = 0; i < boxes.length; i++) {
      if (this.overlaps(boxes[i])) {
        return true;
      }
    }
    return false;
  }

  // Check if this box is within another box.
  within(container) {
    return (this.top >= container.top - this.fuzz) &&
            (this.bottom <= container.bottom + this.fuzz) &&
            (this.left >= container.left - this.fuzz) &&
            (this.right <= container.right + this.fuzz);
  }

  // Check whether this box is passed over the specfic axis boundary. The axis
  // is based on the canvas coordinates, the `+x` is rightward and `+y` is
  // downward.
  isOutsideTheAxisBoundary(container, axis) {
    switch (axis) {
    case "+x":
      return this.right > container.right + this.fuzz;
    case "-x":
      return this.left < container.left - this.fuzz;
    case "+y":
      return this.bottom > container.bottom + this.fuzz;
    case "-y":
      return this.top < container.top - this.fuzz;
    }
  }

  // Find the percentage of the area that this box is overlapping with another
  // box.
  intersectPercentage(b2) {
    let x = Math.max(0, Math.min(this.right, b2.right) - Math.max(this.left, b2.left)),
        y = Math.max(0, Math.min(this.bottom, b2.bottom) - Math.max(this.top, b2.top)),
        intersectArea = x * y;
    return intersectArea / (this.height * this.width);
  }
}

BoxPosition.prototype.clone = function(){
  return new BoxPosition(this);
};

function adjustBoxPosition(styleBox, containerBox, controlBarBox, outputBoxes) {
  const cue = styleBox.cue;
  const isWritingDirectionHorizontal = cue.vertical == "";
  let box = new BoxPosition(styleBox);
  if (!box.width || !box.height) {
    LOG(`No way to adjust a box with zero width or height.`);
    return;
  }

  // Spec 7.2.10, adjust the positions of boxes according to the appropriate
  // steps from the following list. Also, we use offsetHeight/offsetWidth here
  // in order to prevent the incorrect positioning caused by CSS transform
  // scale.
  const fullDimension = isWritingDirectionHorizontal ?
    containerBox.height : containerBox.width;
  if (cue.snapToLines) {
    LOG(`Adjust position when 'snap-to-lines' is true.`);
    // The step is the height or width of the line box. We should use font
    // size directly, instead of using text box's width or height, because the
    // width or height of the box would be changed when the text is wrapped to
    // different line. Ex. if text is wrapped to two line, the height or width
    // of the box would become 2 times of font size.
    let step = styleBox.getFirstLineBoxSize();
    if (step == 0) {
      return;
    }

    // spec 7.2.10.4 ~ 7.2.10.6
    let line = Math.floor(cue.computedLine + 0.5);
    if (cue.vertical == "rl") {
      line = -1 * (line + 1);
    }

    // spec 7.2.10.7 ~ 7.2.10.8
    let position = step * line;
    if (cue.vertical == "rl") {
      position = position - box.width + step;
    }

    // spec 7.2.10.9
    if (line < 0) {
      position += fullDimension;
      step = -1 * step;
    }

    // spec 7.2.10.10, move the box to the specific position along the direction.
    const movingDirection = isWritingDirectionHorizontal ? "+y" : "+x";
    box.move(movingDirection, position);

    // spec 7.2.10.11, remember the position as specified position.
    let specifiedPosition = box.clone();

    // spec 7.2.10.12, let title area be a box that covers all of the video’s
    // rendering area.
    const titleAreaBox = containerBox.clone();
    if (controlBarBox) {
      titleAreaBox.height -= controlBarBox.height;
    }

    function isBoxOutsideTheRenderingArea() {
      if (isWritingDirectionHorizontal) {
        // the top side of the box is above the rendering area, or the bottom
        // side of the box is below the rendering area.
        return step < 0 && box.top < 0 ||
                step > 0 && box.bottom > fullDimension;
      }
      // the left side of the box is outside the left side of the rendering
      // area, or the right side of the box is outside the right side of the
      // rendering area.
      return step < 0 && box.left < 0 ||
              step > 0 && box.right > fullDimension;
    }

    // spec 7.2.10.13, if none of the boxes in boxes would overlap any of the
    // boxes in output, and all of the boxes in boxes are entirely within the
    // title area box.
    let switched = false;
    while (!box.within(titleAreaBox) || box.overlapsAny(outputBoxes)) {
      // spec 7.2.10.14, check if we need to switch the direction.
      if (isBoxOutsideTheRenderingArea()) {
        // spec 7.2.10.17, if `switched` is true, remove all the boxes in
        // `boxes`, which means we shouldn't apply any CSS boxes for this cue.
        // Therefore, returns null box.
        if (switched) {
          return null;
        }
        // spec 7.2.10.18 ~ 7.2.10.20
        switched = true;
        box = specifiedPosition.clone();
        step = -1 * step;
      }
      // spec 7.2.10.15, moving box along the specific direction.
      box.move(movingDirection, step);
    }

    if (isWritingDirectionHorizontal) {
      styleBox.applyStyles({
        top: getPercentagePosition(box.top, fullDimension),
      });
    } else {
      styleBox.applyStyles({
        left: getPercentagePosition(box.left, fullDimension),
      });
    }
  } else {
    LOG(`Adjust position when 'snap-to-lines' is false.`);
    // (snap-to-lines if false) spec 7.2.10.1 ~ 7.2.10.2
    if (cue.lineAlign != "start") {
      const isCenterAlign = cue.lineAlign == "center";
      const movingDirection = isWritingDirectionHorizontal ? "-y" : "-x";
      if (isWritingDirectionHorizontal) {
        box.move(movingDirection, isCenterAlign ? box.height : box.height / 2);
      } else {
        box.move(movingDirection, isCenterAlign ? box.width : box.width / 2);
      }
    }

    // spec 7.2.10.3
    let bestPosition = {},
        specifiedPosition = box.clone(),
        outsideAreaPercentage = 1; // Highest possible so the first thing we get is better.
    let hasFoundBestPosition = false;

    // For the different writing directions, we should have different priority
    // for the moving direction. For example, if the writing direction is
    // horizontal, which means the cues will grow from the top to the bottom,
    // then moving cues along the `y` axis should be more important than moving
    // cues along the `x` axis, and vice versa for those cues growing from the
    // left to right, or from the right to the left. We don't follow the exact
    // way which the spec requires, see the reason in bug1575460.
    function getAxis(writingDirection) {
      if (writingDirection == "") {
        return ["+y", "-y", "+x", "-x"];
      }
      // Growing from left to right.
      if (writingDirection == "lr") {
        return ["+x", "-x", "+y", "-y"];
      }
      // Growing from right to left.
      return ["-x", "+x", "+y", "-y"];
    }
    const axis = getAxis(cue.vertical);

    // This factor effects the granularity of the moving unit, when using the
    // factor=1 often moves too much and results in too many redudant spaces
    // between boxes. So we can increase the factor to slightly reduce the
    // move we do every time, but still can preverse the reasonable spaces
    // between boxes.
    const factor = 4;
    const toMove = styleBox.getFirstLineBoxSize() / factor;
    for (let i = 0; i < axis.length && !hasFoundBestPosition; i++) {
      while (!box.isOutsideTheAxisBoundary(containerBox, axis[i]) &&
              (!box.within(containerBox) || box.overlapsAny(outputBoxes))) {
        box.move(axis[i], toMove);
      }
      // We found a spot where we aren't overlapping anything. This is our
      // best position.
      if (box.within(containerBox)) {
        bestPosition = box.clone();
        hasFoundBestPosition = true;
        break;
      }
      let p = box.intersectPercentage(containerBox);
      // If we're outside the container box less then we were on our last try
      // then remember this position as the best position.
      if (outsideAreaPercentage > p) {
        bestPosition = box.clone();
        outsideAreaPercentage = p;
      }
      // Reset the box position to the specified position.
      box = specifiedPosition.clone();
    }

    // Can not find a place to place this box inside the rendering area.
    if (!box.within(containerBox)) {
      return null;
    }

    styleBox.applyStyles({
      top: getPercentagePosition(box.top, containerBox.height),
      left: getPercentagePosition(box.left, containerBox.width),
    });
  }

  // In order to not be affected by CSS scale, so we use '%' to make sure the
  // cue can stick in the right position.
  function getPercentagePosition(position, fullDimension) {
    return (position / fullDimension) * 100 + "%";
  }

  return box;
}

export function WebVTT() {
  this.isProcessingCues = false;
  // Nothing
}

// Helper to allow strings to be decoded instead of the default binary utf8 data.
WebVTT.StringDecoder = function() {
  return {
    decode: function(data) {
      if (!data) {
        return "";
      }
      if (typeof data !== "string") {
        throw new Error("Error - expected string data.");
      }
      return decodeURIComponent(encodeURIComponent(data));
    }
  };
};

WebVTT.convertCueToDOMTree = function(window, cuetext) {
  if (!window) {
    return null;
  }
  return parseContent(window, cuetext, PARSE_CONTENT_MODE.DOCUMENT_FRAGMENT);
};

function clearAllCuesDiv(overlay) {
  while (overlay.firstChild) {
    overlay.firstChild.remove();
  }
}

// It's used to record how many cues we process in the last `processCues` run.
var lastDisplayedCueNums = 0;

const DIV_COMPUTING_STATE = {
  REUSE : 0,
  REUSE_AND_CLEAR : 1,
  COMPUTE_AND_CLEAR : 2
};

// Runs the processing model over the cues and regions passed to it.
// Spec https://www.w3.org/TR/webvtt1/#processing-model
// @parem window : JS window
// @param cues : the VTT cues are going to be displayed.
// @param overlay : A block level element (usually a div) that the computed cues
//                and regions will be placed into.
// @param controls : A Control bar element. Cues' position will be
//                 affected and repositioned according to it.
function processCuesInternal(window, cues, overlay, controls) {
  LOG(`=== processCues ===`);
  if (!cues) {
    LOG(`clear display and abort processing because of no cue.`);
    clearAllCuesDiv(overlay);
    lastDisplayedCueNums = 0;
    return;
  }

  let controlBar, controlBarShown;
  if (controls) {
    // controls is a <div> that is the children of the UA Widget Shadow Root.
    controlBar = controls.parentNode.getElementById("controlBar");
    controlBarShown = controlBar ? !controlBar.hidden : false;
  } else {
    // There is no controls element. This only happen to UA Widget because
    // it is created lazily.
    controlBarShown = false;
  }

  /**
   * This function is used to tell us if we have to recompute or reuse current
   * cue's display state. Display state is a DIV element with corresponding
   * CSS style to display cue on the screen. When the cue is being displayed
   * first time, we will compute its display state. After that, we could reuse
   * its state until following conditions happen.
   * (1) control changes : it means the rendering area changes so we should
   * recompute cues' position.
   * (2) cue's `hasBeenReset` flag is true : it means cues' line or position
   * property has been modified, we also need to recompute cues' position.
   * (3) the amount of showing cues changes : it means some cue would disappear
   * but other cues should stay at the same place without recomputing, so we
   * can resume their display state.
   */
  function getDIVComputingState(cues) {
    if (overlay.lastControlBarShownStatus != controlBarShown) {
      return DIV_COMPUTING_STATE.COMPUTE_AND_CLEAR;
    }

    for (let i = 0; i < cues.length; i++) {
      if (cues[i].hasBeenReset || !cues[i].displayState) {
        return DIV_COMPUTING_STATE.COMPUTE_AND_CLEAR;
      }
    }

    if (lastDisplayedCueNums != cues.length) {
      return DIV_COMPUTING_STATE.REUSE_AND_CLEAR;
    }
    return DIV_COMPUTING_STATE.REUSE;
  }

  const divState = getDIVComputingState(cues);
  overlay.lastControlBarShownStatus = controlBarShown;

  if (divState == DIV_COMPUTING_STATE.REUSE) {
    LOG(`reuse current cue's display state and abort processing`);
    return;
  }

  clearAllCuesDiv(overlay);
  let rootOfCues = window.document.createElement("div");
  rootOfCues.style.position = "absolute";
  rootOfCues.style.left = "0";
  rootOfCues.style.right = "0";
  rootOfCues.style.top = "0";
  rootOfCues.style.bottom = "0";
  overlay.appendChild(rootOfCues);

  if (divState == DIV_COMPUTING_STATE.REUSE_AND_CLEAR) {
    LOG(`clear display but reuse cues' display state.`);
    for (let cue of cues) {
      rootOfCues.appendChild(cue.displayState);
    }
  } else if (divState == DIV_COMPUTING_STATE.COMPUTE_AND_CLEAR) {
    LOG(`clear display and recompute cues' display state.`);
    let boxPositions = [],
      containerBox = new BoxPosition(rootOfCues);

    let styleBox, cue, controlBarBox;
    if (controlBarShown) {
      controlBarBox = new BoxPosition(controlBar);
      // Add an empty output box that cover the same region as video control bar.
      boxPositions.push(controlBarBox);
    }

    // https://w3c.github.io/webvtt/#processing-model 6.1.12.1
    // Create regionNode
    let regionNodeBoxes = {};
    let regionNodeBox;

    LOG(`lastDisplayedCueNums=${lastDisplayedCueNums}, currentCueNums=${cues.length}`);
    lastDisplayedCueNums = cues.length;
    for (let i = 0; i < cues.length; i++) {
      cue = cues[i];
      if (cue.region != null) {
        // 6.1.14.1
        styleBox = new RegionCueStyleBox(window, cue);

        if (!regionNodeBoxes[cue.region.id]) {
          // create regionNode
          // Adjust the container hieght to exclude the controlBar
          let adjustContainerBox = new BoxPosition(rootOfCues);
          if (controlBarShown) {
            adjustContainerBox.height -= controlBarBox.height;
            adjustContainerBox.bottom += controlBarBox.height;
          }
          regionNodeBox = new RegionNodeBox(window, cue.region, adjustContainerBox);
          regionNodeBoxes[cue.region.id] = regionNodeBox;
        }
        // 6.1.14.3
        let currentRegionBox = regionNodeBoxes[cue.region.id];
        let currentRegionNodeDiv = currentRegionBox.div;
        // 6.1.14.3.2
        // TODO: fix me, it looks like the we need to set/change "top" attribute at the styleBox.div
        // to do the "scroll up", however, we do not implement it yet?
        if (cue.region.scroll == "up" && currentRegionNodeDiv.childElementCount > 0) {
          styleBox.div.style.transitionProperty = "top";
          styleBox.div.style.transitionDuration = "0.433s";
        }

        currentRegionNodeDiv.appendChild(styleBox.div);
        rootOfCues.appendChild(currentRegionNodeDiv);
        cue.displayState = styleBox.div;
        boxPositions.push(new BoxPosition(currentRegionBox));
      } else {
        // Compute the intial position and styles of the cue div.
        styleBox = new CueStyleBox(window, cue, containerBox);
        rootOfCues.appendChild(styleBox.div);

        // Move the cue to correct position, we might get the null box if the
        // result of algorithm doesn't want us to show the cue when we don't
        // have any room for this cue.
        let cueBox = adjustBoxPosition(styleBox, containerBox, controlBarBox, boxPositions);
        if (cueBox) {
          styleBox.setBidiRule();
          // Remember the computed div so that we don't have to recompute it later
          // if we don't have too.
          cue.displayState = styleBox.div;
          boxPositions.push(cueBox);
          LOG(`cue ${i}, ` + cueBox.getBoxInfoInChars());
        } else {
          LOG(`can not find a proper position to place cue ${i}`);
          // Clear the display state and clear the reset flag in the cue as well,
          // which controls whether the task for updating the cue display is
          // dispatched.
          cue.displayState = null;
          rootOfCues.removeChild(styleBox.div);
        }
      }
    }
  } else {
    LOG(`[ERROR] unknown div computing state`);
  }
};

WebVTT.processCues = function(window, cues, overlay, controls) {
  // When accessing `offsetXXX` attributes of element, it would trigger reflow
  // and might result in a re-entry of this function. In order to avoid doing
  // redundant computation, we would only do one processing at a time.
  if (this.isProcessingCues) {
    return;
  }
  this.isProcessingCues = true;
  try {
    processCuesInternal(window, cues, overlay, controls);
  } finally {
    this.isProcessingCues = false;
  }
};

WebVTT.Parser = function(window, decoder) {
  this.window = window;
  this.state = "INITIAL";
  this.substate = "";
  this.substatebuffer = "";
  this.buffer = "";
  this.decoder = decoder || new TextDecoder("utf8");
  this.regionList = [];
  this.isPrevLineBlank = false;
};

WebVTT.Parser.prototype = {
  // If the error is a ParsingError then report it to the consumer if
  // possible. If it's not a ParsingError then throw it like normal.
  reportOrThrowError: function(e) {
    if (e instanceof ParsingError) {
      this.onparsingerror && this.onparsingerror(e);
    } else {
      throw e;
    }
  },
  parse: function (data) {
    // If there is no data then we won't decode it, but will just try to parse
    // whatever is in buffer already. This may occur in circumstances, for
    // example when flush() is called.
    if (data) {
      // Try to decode the data that we received.
      this.buffer += this.decoder.decode(data, {stream: true});
    }

    // This parser is line-based. Let's see if we have a line to parse.
    while (/\r\n|\n|\r/.test(this.buffer)) {
      let buffer = this.buffer;
      let pos = 0;
      while (buffer[pos] !== '\r' && buffer[pos] !== '\n') {
        ++pos;
      }
      let line = buffer.substr(0, pos);
      // Advance the buffer early in case we fail below.
      if (buffer[pos] === '\r') {
        ++pos;
      }
      if (buffer[pos] === '\n') {
        ++pos;
      }
      this.buffer = buffer.substr(pos);

      // Spec defined replacement.
      line = line.replace(/[\u0000]/g, "\uFFFD");

      // Detect the comment. We parse line on the fly, so we only check if the
      // comment block is preceded by a blank line and won't check if it's
      // followed by another blank line.
      // https://www.w3.org/TR/webvtt1/#introduction-comments
      // TODO (1703895): according to the spec, the comment represents as a
      // comment block, so we need to refactor the parser in order to better
      // handle the comment block.
      if (this.isPrevLineBlank && /^NOTE($|[ \t])/.test(line)) {
        LOG("Ignore comment that starts with 'NOTE'");
      } else {
        this.parseLine(line);
      }
      this.isPrevLineBlank = emptyOrOnlyContainsWhiteSpaces(line);
    }

    return this;
  },
  parseLine: function(line) {
    let self = this;

    function createCueIfNeeded() {
      if (!self.cue) {
        self.cue = new self.window.VTTCue(0, 0, "");
      }
    }

    // Parsing cue identifier and the identifier should be unique.
    // Return true if the input is a cue identifier.
    function parseCueIdentifier(input) {
      if (maybeIsTimeStampFormat(input)) {
        self.state = "CUE";
        return false;
      }

      createCueIfNeeded();
      // TODO : ensure the cue identifier is unique among all cue identifiers.
      self.cue.id = containsTimeDirectionSymbol(input) ? "" : input;
      self.state = "CUE";
      return true;
    }

    // Parsing the timestamp and cue settings.
    // See spec, https://w3c.github.io/webvtt/#collect-webvtt-cue-timings-and-settings
    function parseCueMayThrow(input) {
      try {
        createCueIfNeeded();
        parseCue(input, self.cue, self.regionList);
        self.state = "CUETEXT";
      } catch (e) {
        self.reportOrThrowError(e);
        // In case of an error ignore rest of the cue.
        self.cue = null;
        self.state = "BADCUE";
      }
    }

    // 3.4 WebVTT region and WebVTT region settings syntax
    function parseRegion(input) {
      let settings = new Settings();
      parseOptions(input, function (k, v) {
        switch (k) {
        case "id":
          settings.set(k, v);
          break;
        case "width":
          settings.percent(k, v);
          break;
        case "lines":
          settings.digitsValue(k, v);
          break;
        case "regionanchor":
        case "viewportanchor": {
          let xy = v.split(',');
          if (xy.length !== 2) {
            break;
          }
          // We have to make sure both x and y parse, so use a temporary
          // settings object here.
          let anchor = new Settings();
          anchor.percent("x", xy[0]);
          anchor.percent("y", xy[1]);
          if (!anchor.has("x") || !anchor.has("y")) {
            break;
          }
          settings.set(k + "X", anchor.get("x"));
          settings.set(k + "Y", anchor.get("y"));
          break;
        }
        case "scroll":
          settings.alt(k, v, ["up"]);
          break;
        }
      }, /:/, /\t|\n|\f|\r| /); // groupDelim is ASCII whitespace
      // https://infra.spec.whatwg.org/#ascii-whitespace, U+0009 TAB, U+000A LF, U+000C FF, U+000D CR, U+0020 SPACE

      // Create the region, using default values for any values that were not
      // specified.
      if (settings.has("id")) {
        try {
          let region = new self.window.VTTRegion();
          region.id = settings.get("id", "");
          region.width = settings.get("width", 100);
          region.lines = settings.get("lines", 3);
          region.regionAnchorX = settings.get("regionanchorX", 0);
          region.regionAnchorY = settings.get("regionanchorY", 100);
          region.viewportAnchorX = settings.get("viewportanchorX", 0);
          region.viewportAnchorY = settings.get("viewportanchorY", 100);
          region.scroll = settings.get("scroll", "");
          // Register the region.
          self.onregion && self.onregion(region);
          // Remember the VTTRegion for later in case we parse any VTTCues that
          // reference it.
          self.regionList.push({
            id: settings.get("id"),
            region: region
          });
        } catch(e) {
          dump("VTTRegion Error " + e + "\n");
        }
      }
    }

    // Parsing the WebVTT signature, it contains parsing algo step1 to step9.
    // See spec, https://w3c.github.io/webvtt/#file-parsing
    function parseSignatureMayThrow(signature) {
      if (!/^WEBVTT([ \t].*)?$/.test(signature)) {
        throw new ParsingError(ParsingError.Errors.BadSignature);
      } else {
        self.state = "HEADER";
      }
    }

    function parseRegionOrStyle(input) {
      switch (self.substate) {
        case "REGION":
          parseRegion(input);
        break;
        case "STYLE":
          // TODO : not supported yet.
        break;
      }
    }
    // Parsing the region and style information.
    // See spec, https://w3c.github.io/webvtt/#collect-a-webvtt-block
    //
    // There are sereval things would appear in header,
    //   1. Region or Style setting
    //   2. Garbage (meaningless string)
    //   3. Empty line
    //   4. Cue's timestamp
    // The case 4 happens when there is no line interval between the header
    // and the cue blocks. In this case, we should preserve the line for the
    // next phase parsing, returning "true".
    function parseHeader(line) {
      if (!self.substate && /^REGION|^STYLE/.test(line)) {
        self.substate = /^REGION/.test(line) ? "REGION" : "STYLE";
        return false;
      }

      if (self.substate === "REGION" || self.substate === "STYLE") {
        if (maybeIsTimeStampFormat(line) ||
            emptyOrOnlyContainsWhiteSpaces(line) ||
            containsTimeDirectionSymbol(line)) {
          parseRegionOrStyle(self.substatebuffer);
          self.substatebuffer = "";
          self.substate = null;

          // This is the end of the region or style state.
          return parseHeader(line);
        }

        if (/^REGION|^STYLE/.test(line)) {
          // The line is another REGION/STYLE, parse and reset substatebuffer.
          // Don't break the while loop to parse the next REGION/STYLE.
          parseRegionOrStyle(self.substatebuffer);
          self.substatebuffer = "";
          self.substate = /^REGION/.test(line) ? "REGION" : "STYLE";
          return false;
        }

        // We weren't able to parse the line as a header. Accumulate and
        // return.
        self.substatebuffer += " " + line;
        return false;
      }

      if (emptyOrOnlyContainsWhiteSpaces(line)) {
        // empty line, whitespaces, nothing to do.
        return false;
      }

      if (maybeIsTimeStampFormat(line)) {
        self.state = "CUE";
        // We want to process the same line again.
        return true;
      }

      // string contains "-->" or an ID
      self.state = "ID";
      return true;
    }

    try {
      LOG(`state=${self.state}, line=${line}`)
      // 5.1 WebVTT file parsing.
      if (self.state === "INITIAL") {
        parseSignatureMayThrow(line);
        return;
      }

      if (self.state === "HEADER") {
        // parseHeader returns false if the same line doesn't need to be
        // parsed again.
        if (!parseHeader(line)) {
          return;
        }
      }

      if (self.state === "ID") {
        // If there is no cue identifier, read the next line.
        if (line == "") {
          return;
        }

        // If there is no cue identifier, parse the line again.
        if (!parseCueIdentifier(line)) {
          return self.parseLine(line);
        }
        return;
      }

      if (self.state === "CUE") {
        parseCueMayThrow(line);
        return;
      }

      if (self.state === "CUETEXT") {
        // Report the cue when (1) get an empty line (2) get the "-->""
        if (emptyOrOnlyContainsWhiteSpaces(line) ||
            containsTimeDirectionSymbol(line)) {
          // We are done parsing self cue.
          self.oncue && self.oncue(self.cue);
          self.cue = null;
          self.state = "ID";

          if (emptyOrOnlyContainsWhiteSpaces(line)) {
            return;
          }

          // Reuse the same line.
          return self.parseLine(line);
        }
        if (self.cue.text) {
          self.cue.text += "\n";
        }
        self.cue.text += line;
        return;
      }

      if (self.state === "BADCUE") {
        // 54-62 - Collect and discard the remaining cue.
        self.state = "ID";
        return self.parseLine(line);
      }
    } catch (e) {
      self.reportOrThrowError(e);

      // If we are currently parsing a cue, report what we have.
      if (self.state === "CUETEXT" && self.cue && self.oncue) {
        self.oncue(self.cue);
      }
      self.cue = null;
      // Enter BADWEBVTT state if header was not parsed correctly otherwise
      // another exception occurred so enter BADCUE state.
      self.state = self.state === "INITIAL" ? "BADWEBVTT" : "BADCUE";
    }
    return this;
  },
  flush: function () {
    let self = this;
    try {
      // Finish decoding the stream.
      self.buffer += self.decoder.decode();
      self.buffer += "\n\n";
      self.parse();
    } catch(e) {
      self.reportOrThrowError(e);
    }
    self.isPrevLineBlank = false;
    self.onflush && self.onflush();
    return this;
  }
};
