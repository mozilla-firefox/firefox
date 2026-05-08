/*
 * Copyright (c) 2008-2017 Mozilla Foundation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/*
 * THIS IS A GENERATED FILE. PLEASE DO NOT EDIT.
 * Please edit AttributeName.java instead and regenerate.
 */

#ifndef nsHtml5AttributeName_h
#define nsHtml5AttributeName_h

#include "nsAtom.h"
#include "nsHtml5AtomTable.h"
#include "nsHtml5String.h"
#include "nsNameSpaceManager.h"
#include "nsIContent.h"
#include "nsTraceRefcnt.h"
#include "jArray.h"
#include "nsHtml5ArrayCopy.h"
#include "nsAHtml5TreeBuilderState.h"
#include "nsGkAtoms.h"
#include "nsHtml5ByteReadable.h"
#include "nsHtml5Macros.h"
#include "nsIContentHandle.h"
#include "nsHtml5Portability.h"
#include "nsHtml5ContentCreatorFunction.h"

class nsHtml5StreamParser;

class nsHtml5ElementName;
class nsHtml5Tokenizer;
class nsHtml5TreeBuilder;
class nsHtml5UTF16Buffer;
class nsHtml5StateSnapshot;
class nsHtml5Portability;

class nsHtml5AttributeName {
 public:
  static int32_t* ALL_NO_NS;

 private:
  static int32_t* XMLNS_NS;
  static int32_t* XML_NS;
  static int32_t* XLINK_NS;

 public:
  static nsStaticAtom** ALL_NO_PREFIX;

 private:
  static nsStaticAtom** XMLNS_PREFIX;
  static nsStaticAtom** XLINK_PREFIX;
  static nsStaticAtom** XML_PREFIX;

 public:
  inline static int32_t levelOrderBinarySearch(jArray<int32_t, int32_t> data,
                                               int32_t key) {
    int32_t n = data.length;
    int32_t i = 0;
    while (i < n) {
      int32_t val = data[i];
      if (val < key) {
        i = 2 * i + 2;
      } else if (val > key) {
        i = 2 * i + 1;
      } else {
        return i;
      }
    }
    return -1;
  }

  inline static nsHtml5AttributeName* nameByBuffer(char16_t* buf,
                                                   int32_t length,
                                                   nsHtml5AtomTable* interner) {
    uint32_t hash = nsHtml5AttributeName::bufToHash(buf, length);
    jArray<int32_t, int32_t> hashes;
    hashes = nsHtml5AttributeName::ATTRIBUTE_HASHES;
    int32_t index = levelOrderBinarySearch(hashes, hash);
    if (index < 0) {
      return nullptr;
    }
    nsHtml5AttributeName* attributeName =
        nsHtml5AttributeName::ATTRIBUTE_NAMES[index];
    nsAtom* name = attributeName->getLocal(0);
    if (!nsHtml5Portability::localEqualsBuffer(name, buf, length)) {
      return nullptr;
    }
    return attributeName;
  }

 private:
  inline static uint32_t bufToHash(char16_t* buf, int32_t length) {
    uint32_t len = length;
    uint32_t first = buf[0];
    first <<= 19;
    uint32_t second = 1 << 23;
    uint32_t third = 0;
    uint32_t fourth = 0;
    uint32_t fifth = 0;
    uint32_t sixth = 0;
    if (length >= 4) {
      second = buf[length - 4];
      second <<= 4;
      third = buf[1];
      third <<= 9;
      fourth = buf[length - 2];
      fourth <<= 14;
      fifth = buf[3];
      fifth <<= 24;
      sixth = buf[length - 1];
      sixth <<= 11;
    } else if (length == 3) {
      second = buf[1];
      second <<= 4;
      third = buf[2];
      third <<= 9;
    } else if (length == 2) {
      second = buf[1];
      second <<= 24;
    }
    return len + first + second + third + fourth + fifth + sixth;
  }

 public:
  static const int32_t HTML = 0;

  static const int32_t MATHML = 1;

  static const int32_t SVG = 2;

 private:
  jInlineArray<int32_t, 3> uri;
  jInlineArray<nsAtom*, 3> local;
  jInlineArray<nsStaticAtom*, 3> prefix;
  bool custom;
  nsHtml5AttributeName(int32_t* uri, nsStaticAtom* html, nsStaticAtom* mathml,
                       nsStaticAtom* svg, nsStaticAtom** prefix);

 public:
  nsHtml5AttributeName();
  inline bool isInterned() { return !custom; }

  inline void setNameForNonInterned(nsAtom* name) {
    MOZ_ASSERT(custom);
    NS_IF_ADDREF(name);
    NS_IF_RELEASE(local[0]);
    local[0] = name;
    local[1] = name;
    local[2] = name;
  }

  ~nsHtml5AttributeName();
  int32_t getUri(int32_t mode);
  nsAtom* getLocal(int32_t mode);
  nsStaticAtom* getPrefix(int32_t mode);
  bool equalsAnother(nsHtml5AttributeName* another);
  static nsHtml5AttributeName* ATTR_ALT;
  static nsHtml5AttributeName* ATTR_DIR;
  static nsHtml5AttributeName* ATTR_DUR;
  static nsHtml5AttributeName* ATTR_END;
  static nsHtml5AttributeName* ATTR_FOR;
  static nsHtml5AttributeName* ATTR_IN2;
  static nsHtml5AttributeName* ATTR_LOW;
  static nsHtml5AttributeName* ATTR_MIN;
  static nsHtml5AttributeName* ATTR_MAX;
  static nsHtml5AttributeName* ATTR_REL;
  static nsHtml5AttributeName* ATTR_REV;
  static nsHtml5AttributeName* ATTR_SRC;
  static nsHtml5AttributeName* ATTR_D;
  static nsHtml5AttributeName* ATTR_R;
  static nsHtml5AttributeName* ATTR_X;
  static nsHtml5AttributeName* ATTR_Y;
  static nsHtml5AttributeName* ATTR_Z;
  static nsHtml5AttributeName* ATTR_K1;
  static nsHtml5AttributeName* ATTR_X1;
  static nsHtml5AttributeName* ATTR_Y1;
  static nsHtml5AttributeName* ATTR_K2;
  static nsHtml5AttributeName* ATTR_X2;
  static nsHtml5AttributeName* ATTR_Y2;
  static nsHtml5AttributeName* ATTR_K3;
  static nsHtml5AttributeName* ATTR_K4;
  static nsHtml5AttributeName* ATTR_XML_SPACE;
  static nsHtml5AttributeName* ATTR_XML_LANG;
  static nsHtml5AttributeName* ATTR_ARIA_GRAB;
  static nsHtml5AttributeName* ATTR_ARIA_VALUEMAX;
  static nsHtml5AttributeName* ATTR_ARIA_LABELLEDBY;
  static nsHtml5AttributeName* ATTR_ARIA_DESCRIBEDBY;
  static nsHtml5AttributeName* ATTR_ARIA_DISABLED;
  static nsHtml5AttributeName* ATTR_ARIA_CHECKED;
  static nsHtml5AttributeName* ATTR_ARIA_SELECTED;
  static nsHtml5AttributeName* ATTR_ARIA_DROPEFFECT;
  static nsHtml5AttributeName* ATTR_ARIA_REQUIRED;
  static nsHtml5AttributeName* ATTR_ARIA_EXPANDED;
  static nsHtml5AttributeName* ATTR_ARIA_PRESSED;
  static nsHtml5AttributeName* ATTR_ARIA_LEVEL;
  static nsHtml5AttributeName* ATTR_ARIA_CHANNEL;
  static nsHtml5AttributeName* ATTR_ARIA_HIDDEN;
  static nsHtml5AttributeName* ATTR_ARIA_SECRET;
  static nsHtml5AttributeName* ATTR_ARIA_POSINSET;
  static nsHtml5AttributeName* ATTR_ARIA_ATOMIC;
  static nsHtml5AttributeName* ATTR_ARIA_INVALID;
  static nsHtml5AttributeName* ATTR_ARIA_TEMPLATEID;
  static nsHtml5AttributeName* ATTR_ARIA_VALUEMIN;
  static nsHtml5AttributeName* ATTR_ARIA_MULTISELECTABLE;
  static nsHtml5AttributeName* ATTR_ARIA_CONTROLS;
  static nsHtml5AttributeName* ATTR_ARIA_MULTILINE;
  static nsHtml5AttributeName* ATTR_ARIA_READONLY;
  static nsHtml5AttributeName* ATTR_ARIA_OWNS;
  static nsHtml5AttributeName* ATTR_ARIA_ACTIVEDESCENDANT;
  static nsHtml5AttributeName* ATTR_ARIA_RELEVANT;
  static nsHtml5AttributeName* ATTR_ARIA_DATATYPE;
  static nsHtml5AttributeName* ATTR_ARIA_VALUENOW;
  static nsHtml5AttributeName* ATTR_ARIA_SORT;
  static nsHtml5AttributeName* ATTR_ARIA_AUTOCOMPLETE;
  static nsHtml5AttributeName* ATTR_ARIA_FLOWTO;
  static nsHtml5AttributeName* ATTR_ARIA_BUSY;
  static nsHtml5AttributeName* ATTR_ARIA_LIVE;
  static nsHtml5AttributeName* ATTR_ARIA_HASPOPUP;
  static nsHtml5AttributeName* ATTR_ARIA_SETSIZE;
  static nsHtml5AttributeName* ATTR_CLEAR;
  static nsHtml5AttributeName* ATTR_DISABLED;
  static nsHtml5AttributeName* ATTR_DEFAULT;
  static nsHtml5AttributeName* ATTR_DATA;
  static nsHtml5AttributeName* ATTR_EQUALCOLUMNS;
  static nsHtml5AttributeName* ATTR_EQUALROWS;
  static nsHtml5AttributeName* ATTR_HSPACE;
  static nsHtml5AttributeName* ATTR_ISMAP;
  static nsHtml5AttributeName* ATTR_LOCAL;
  static nsHtml5AttributeName* ATTR_LSPACE;
  static nsHtml5AttributeName* ATTR_MOVABLELIMITS;
  static nsHtml5AttributeName* ATTR_NOTATION;
  static nsHtml5AttributeName* ATTR_ONDATAAVAILABLE;
  static nsHtml5AttributeName* ATTR_ONPASTE;
  static nsHtml5AttributeName* ATTR_RSPACE;
  static nsHtml5AttributeName* ATTR_ROWALIGN;
  static nsHtml5AttributeName* ATTR_ROTATE;
  static nsHtml5AttributeName* ATTR_SEPARATOR;
  static nsHtml5AttributeName* ATTR_SEPARATORS;
  static nsHtml5AttributeName* ATTR_VSPACE;
  static nsHtml5AttributeName* ATTR_XCHANNELSELECTOR;
  static nsHtml5AttributeName* ATTR_YCHANNELSELECTOR;
  static nsHtml5AttributeName* ATTR_ENABLE_BACKGROUND;
  static nsHtml5AttributeName* ATTR_ONDBLCLICK;
  static nsHtml5AttributeName* ATTR_ONABORT;
  static nsHtml5AttributeName* ATTR_CALCMODE;
  static nsHtml5AttributeName* ATTR_CHECKED;
  static nsHtml5AttributeName* ATTR_FENCE;
  static nsHtml5AttributeName* ATTR_FETCHPRIORITY;
  static nsHtml5AttributeName* ATTR_NONCE;
  static nsHtml5AttributeName* ATTR_ONSCROLL;
  static nsHtml5AttributeName* ATTR_ONACTIVATE;
  static nsHtml5AttributeName* ATTR_OPACITY;
  static nsHtml5AttributeName* ATTR_SPACING;
  static nsHtml5AttributeName* ATTR_SPECULAREXPONENT;
  static nsHtml5AttributeName* ATTR_SPECULARCONSTANT;
  static nsHtml5AttributeName* ATTR_BORDER;
  static nsHtml5AttributeName* ATTR_ID;
  static nsHtml5AttributeName* ATTR_GRADIENTTRANSFORM;
  static nsHtml5AttributeName* ATTR_GRADIENTUNITS;
  static nsHtml5AttributeName* ATTR_HIDDEN;
  static nsHtml5AttributeName* ATTR_HEADERS;
  static nsHtml5AttributeName* ATTR_LOADING;
  static nsHtml5AttributeName* ATTR_READONLY;
  static nsHtml5AttributeName* ATTR_RENDERING_INTENT;
  static nsHtml5AttributeName* ATTR_SHADOWROOTMODE;
  static nsHtml5AttributeName* ATTR_SEED;
  static nsHtml5AttributeName* ATTR_SHADOWROOTREFERENCETARGET;
  static nsHtml5AttributeName* ATTR_SHADOWROOTCLONABLE;
  static nsHtml5AttributeName* ATTR_SHADOWROOTSERIALIZABLE;
  static nsHtml5AttributeName* ATTR_SRCDOC;
  static nsHtml5AttributeName* ATTR_STDDEVIATION;
  static nsHtml5AttributeName* ATTR_SANDBOX;
  static nsHtml5AttributeName* ATTR_SHADOWROOTCUSTOMELEMENTREGISTRY;
  static nsHtml5AttributeName* ATTR_SHADOWROOTDELEGATESFOCUS;
  static nsHtml5AttributeName* ATTR_SHADOWROOTSLOTASSIGNMENT;
  static nsHtml5AttributeName* ATTR_WORD_SPACING;
  static nsHtml5AttributeName* ATTR_ACCENTUNDER;
  static nsHtml5AttributeName* ATTR_ACCEPT_CHARSET;
  static nsHtml5AttributeName* ATTR_ACCESSKEY;
  static nsHtml5AttributeName* ATTR_ACCENT;
  static nsHtml5AttributeName* ATTR_ACCEPT;
  static nsHtml5AttributeName* ATTR_BEVELLED;
  static nsHtml5AttributeName* ATTR_BASEFREQUENCY;
  static nsHtml5AttributeName* ATTR_BASELINE_SHIFT;
  static nsHtml5AttributeName* ATTR_BASEPROFILE;
  static nsHtml5AttributeName* ATTR_BASELINE;
  static nsHtml5AttributeName* ATTR_BASE;
  static nsHtml5AttributeName* ATTR_CODE;
  static nsHtml5AttributeName* ATTR_CODETYPE;
  static nsHtml5AttributeName* ATTR_CODEBASE;
  static nsHtml5AttributeName* ATTR_CITE;
  static nsHtml5AttributeName* ATTR_DEFER;
  static nsHtml5AttributeName* ATTR_DATETIME;
  static nsHtml5AttributeName* ATTR_DIRECTION;
  static nsHtml5AttributeName* ATTR_EDGEMODE;
  static nsHtml5AttributeName* ATTR_EDGE;
  static nsHtml5AttributeName* ATTR_ENTERKEYHINT;
  static nsHtml5AttributeName* ATTR_FACE;
  static nsHtml5AttributeName* ATTR_INDEX;
  static nsHtml5AttributeName* ATTR_INTERCEPT;
  static nsHtml5AttributeName* ATTR_INTEGRITY;
  static nsHtml5AttributeName* ATTR_LINEBREAK;
  static nsHtml5AttributeName* ATTR_LABEL;
  static nsHtml5AttributeName* ATTR_LINETHICKNESS;
  static nsHtml5AttributeName* ATTR_MODE;
  static nsHtml5AttributeName* ATTR_NAME;
  static nsHtml5AttributeName* ATTR_NORESIZE;
  static nsHtml5AttributeName* ATTR_ONBEFOREUNLOAD;
  static nsHtml5AttributeName* ATTR_ONREPEAT;
  static nsHtml5AttributeName* ATTR_OBJECT;
  static nsHtml5AttributeName* ATTR_ONSELECT;
  static nsHtml5AttributeName* ATTR_ORDER;
  static nsHtml5AttributeName* ATTR_OTHER;
  static nsHtml5AttributeName* ATTR_ONRESET;
  static nsHtml5AttributeName* ATTR_ONREADYSTATECHANGE;
  static nsHtml5AttributeName* ATTR_ONMESSAGE;
  static nsHtml5AttributeName* ATTR_ONBEGIN;
  static nsHtml5AttributeName* ATTR_ONBEFOREPRINT;
  static nsHtml5AttributeName* ATTR_ORIENT;
  static nsHtml5AttributeName* ATTR_ORIENTATION;
  static nsHtml5AttributeName* ATTR_ONBEFORECOPY;
  static nsHtml5AttributeName* ATTR_ONSELECTSTART;
  static nsHtml5AttributeName* ATTR_ONBEFOREPASTE;
  static nsHtml5AttributeName* ATTR_ONKEYPRESS;
  static nsHtml5AttributeName* ATTR_ONKEYUP;
  static nsHtml5AttributeName* ATTR_ONBEFORECUT;
  static nsHtml5AttributeName* ATTR_ONKEYDOWN;
  static nsHtml5AttributeName* ATTR_ONRESIZE;
  static nsHtml5AttributeName* ATTR_REPEAT;
  static nsHtml5AttributeName* ATTR_REFERRERPOLICY;
  static nsHtml5AttributeName* ATTR_RULES;
  static nsHtml5AttributeName* ATTR_ROLE;
  static nsHtml5AttributeName* ATTR_REPEATCOUNT;
  static nsHtml5AttributeName* ATTR_REPEATDUR;
  static nsHtml5AttributeName* ATTR_SELECTED;
  static nsHtml5AttributeName* ATTR_SIZES;
  static nsHtml5AttributeName* ATTR_SUPERSCRIPTSHIFT;
  static nsHtml5AttributeName* ATTR_STRETCHY;
  static nsHtml5AttributeName* ATTR_SCHEME;
  static nsHtml5AttributeName* ATTR_SPREADMETHOD;
  static nsHtml5AttributeName* ATTR_SELECTION;
  static nsHtml5AttributeName* ATTR_SIZE;
  static nsHtml5AttributeName* ATTR_TYPE;
  static nsHtml5AttributeName* ATTR_DIFFUSECONSTANT;
  static nsHtml5AttributeName* ATTR_HREF;
  static nsHtml5AttributeName* ATTR_HREFLANG;
  static nsHtml5AttributeName* ATTR_ONAFTERPRINT;
  static nsHtml5AttributeName* ATTR_PROFILE;
  static nsHtml5AttributeName* ATTR_SURFACESCALE;
  static nsHtml5AttributeName* ATTR_XREF;
  static nsHtml5AttributeName* ATTR_ALIGN;
  static nsHtml5AttributeName* ATTR_ALIGNMENT_BASELINE;
  static nsHtml5AttributeName* ATTR_ALIGNMENTSCOPE;
  static nsHtml5AttributeName* ATTR_DRAGGABLE;
  static nsHtml5AttributeName* ATTR_HEIGHT;
  static nsHtml5AttributeName* ATTR_IMAGESIZES;
  static nsHtml5AttributeName* ATTR_IMAGESRCSET;
  static nsHtml5AttributeName* ATTR_IMAGE_RENDERING;
  static nsHtml5AttributeName* ATTR_LANGUAGE;
  static nsHtml5AttributeName* ATTR_LANG;
  static nsHtml5AttributeName* ATTR_LARGEOP;
  static nsHtml5AttributeName* ATTR_LONGDESC;
  static nsHtml5AttributeName* ATTR_LENGTHADJUST;
  static nsHtml5AttributeName* ATTR_MARGINHEIGHT;
  static nsHtml5AttributeName* ATTR_MARGINWIDTH;
  static nsHtml5AttributeName* ATTR_ORIGIN;
  static nsHtml5AttributeName* ATTR_PING;
  static nsHtml5AttributeName* ATTR_TARGET;
  static nsHtml5AttributeName* ATTR_TARGETX;
  static nsHtml5AttributeName* ATTR_TARGETY;
  static nsHtml5AttributeName* ATTR_ARCHIVE;
  static nsHtml5AttributeName* ATTR_HIGH;
  static nsHtml5AttributeName* ATTR_LIGHTING_COLOR;
  static nsHtml5AttributeName* ATTR_MATHBACKGROUND;
  static nsHtml5AttributeName* ATTR_METHOD;
  static nsHtml5AttributeName* ATTR_MATHVARIANT;
  static nsHtml5AttributeName* ATTR_MATHCOLOR;
  static nsHtml5AttributeName* ATTR_MATHSIZE;
  static nsHtml5AttributeName* ATTR_NOSHADE;
  static nsHtml5AttributeName* ATTR_ONCHANGE;
  static nsHtml5AttributeName* ATTR_PATHLENGTH;
  static nsHtml5AttributeName* ATTR_PATH;
  static nsHtml5AttributeName* ATTR_ALTIMG;
  static nsHtml5AttributeName* ATTR_ACTIONTYPE;
  static nsHtml5AttributeName* ATTR_ACTION;
  static nsHtml5AttributeName* ATTR_ACTIVE;
  static nsHtml5AttributeName* ATTR_ADDITIVE;
  static nsHtml5AttributeName* ATTR_BEGIN;
  static nsHtml5AttributeName* ATTR_DOMINANT_BASELINE;
  static nsHtml5AttributeName* ATTR_DIVISOR;
  static nsHtml5AttributeName* ATTR_DEFINITIONURL;
  static nsHtml5AttributeName* ATTR_LIMITINGCONEANGLE;
  static nsHtml5AttributeName* ATTR_MEDIA;
  static nsHtml5AttributeName* ATTR_MANIFEST;
  static nsHtml5AttributeName* ATTR_ONFINISH;
  static nsHtml5AttributeName* ATTR_OPTIMUM;
  static nsHtml5AttributeName* ATTR_RADIOGROUP;
  static nsHtml5AttributeName* ATTR_RADIUS;
  static nsHtml5AttributeName* ATTR_SCRIPTLEVEL;
  static nsHtml5AttributeName* ATTR_SCRIPTSIZEMULTIPLIER;
  static nsHtml5AttributeName* ATTR_SCRIPTMINSIZE;
  static nsHtml5AttributeName* ATTR_TABINDEX;
  static nsHtml5AttributeName* ATTR_VALIGN;
  static nsHtml5AttributeName* ATTR_VISIBILITY;
  static nsHtml5AttributeName* ATTR_BACKGROUND;
  static nsHtml5AttributeName* ATTR_LINK;
  static nsHtml5AttributeName* ATTR_MARKER_MID;
  static nsHtml5AttributeName* ATTR_MARKERHEIGHT;
  static nsHtml5AttributeName* ATTR_MARKER_END;
  static nsHtml5AttributeName* ATTR_MASK;
  static nsHtml5AttributeName* ATTR_MARKER_START;
  static nsHtml5AttributeName* ATTR_MARKERWIDTH;
  static nsHtml5AttributeName* ATTR_MASKUNITS;
  static nsHtml5AttributeName* ATTR_MARKERUNITS;
  static nsHtml5AttributeName* ATTR_MASKCONTENTUNITS;
  static nsHtml5AttributeName* ATTR_AMPLITUDE;
  static nsHtml5AttributeName* ATTR_CELLSPACING;
  static nsHtml5AttributeName* ATTR_CELLPADDING;
  static nsHtml5AttributeName* ATTR_DECLARE;
  static nsHtml5AttributeName* ATTR_FILL_RULE;
  static nsHtml5AttributeName* ATTR_FILL;
  static nsHtml5AttributeName* ATTR_FILL_OPACITY;
  static nsHtml5AttributeName* ATTR_MAXLENGTH;
  static nsHtml5AttributeName* ATTR_ONCLICK;
  static nsHtml5AttributeName* ATTR_ONBLUR;
  static nsHtml5AttributeName* ATTR_REPLACE;
  static nsHtml5AttributeName* ATTR_ROWLINES;
  static nsHtml5AttributeName* ATTR_SCALE;
  static nsHtml5AttributeName* ATTR_STYLE;
  static nsHtml5AttributeName* ATTR_TABLEVALUES;
  static nsHtml5AttributeName* ATTR_TITLE;
  static nsHtml5AttributeName* ATTR_AZIMUTH;
  static nsHtml5AttributeName* ATTR_FORMAT;
  static nsHtml5AttributeName* ATTR_FRAMEBORDER;
  static nsHtml5AttributeName* ATTR_FRAME;
  static nsHtml5AttributeName* ATTR_FRAMESPACING;
  static nsHtml5AttributeName* ATTR_FROM;
  static nsHtml5AttributeName* ATTR_FORM;
  static nsHtml5AttributeName* ATTR_PROMPT;
  static nsHtml5AttributeName* ATTR_PRIMITIVEUNITS;
  static nsHtml5AttributeName* ATTR_SYMMETRIC;
  static nsHtml5AttributeName* ATTR_SUMMARY;
  static nsHtml5AttributeName* ATTR_USEMAP;
  static nsHtml5AttributeName* ATTR_ZOOMANDPAN;
  static nsHtml5AttributeName* ATTR_ASYNC;
  static nsHtml5AttributeName* ATTR_ALINK;
  static nsHtml5AttributeName* ATTR_IN;
  static nsHtml5AttributeName* ATTR_EVENT;
  static nsHtml5AttributeName* ATTR_ICON;
  static nsHtml5AttributeName* ATTR_KERNELMATRIX;
  static nsHtml5AttributeName* ATTR_KERNING;
  static nsHtml5AttributeName* ATTR_KERNELUNITLENGTH;
  static nsHtml5AttributeName* ATTR_ONUNLOAD;
  static nsHtml5AttributeName* ATTR_OPEN;
  static nsHtml5AttributeName* ATTR_ONINVALID;
  static nsHtml5AttributeName* ATTR_ONEND;
  static nsHtml5AttributeName* ATTR_ONINPUT;
  static nsHtml5AttributeName* ATTR_POINTER_EVENTS;
  static nsHtml5AttributeName* ATTR_POINTS;
  static nsHtml5AttributeName* ATTR_POINTSATX;
  static nsHtml5AttributeName* ATTR_POINTSATY;
  static nsHtml5AttributeName* ATTR_POINTSATZ;
  static nsHtml5AttributeName* ATTR_SPAN;
  static nsHtml5AttributeName* ATTR_STANDBY;
  static nsHtml5AttributeName* ATTR_TRANSFORM_ORIGIN;
  static nsHtml5AttributeName* ATTR_TRANSFORM;
  static nsHtml5AttributeName* ATTR_VLINK;
  static nsHtml5AttributeName* ATTR_WHEN;
  static nsHtml5AttributeName* ATTR_XLINK_HREF;
  static nsHtml5AttributeName* ATTR_XLINK_TITLE;
  static nsHtml5AttributeName* ATTR_XLINK_ROLE;
  static nsHtml5AttributeName* ATTR_XLINK_ARCROLE;
  static nsHtml5AttributeName* ATTR_XMLNS_XLINK;
  static nsHtml5AttributeName* ATTR_XMLNS;
  static nsHtml5AttributeName* ATTR_XLINK_TYPE;
  static nsHtml5AttributeName* ATTR_XLINK_SHOW;
  static nsHtml5AttributeName* ATTR_XLINK_ACTUATE;
  static nsHtml5AttributeName* ATTR_AUTOPLAY;
  static nsHtml5AttributeName* ATTR_AUTOCORRECT;
  static nsHtml5AttributeName* ATTR_AUTOCOMPLETE;
  static nsHtml5AttributeName* ATTR_AUTOFOCUS;
  static nsHtml5AttributeName* ATTR_AUTOCAPITALIZE;
  static nsHtml5AttributeName* ATTR_BGCOLOR;
  static nsHtml5AttributeName* ATTR_COLOR_PROFILE;
  static nsHtml5AttributeName* ATTR_COLOR_RENDERING;
  static nsHtml5AttributeName* ATTR_COLOR_INTERPOLATION;
  static nsHtml5AttributeName* ATTR_COLOR;
  static nsHtml5AttributeName* ATTR_COLOR_INTERPOLATION_FILTERS;
  static nsHtml5AttributeName* ATTR_ENCODING;
  static nsHtml5AttributeName* ATTR_EXPONENT;
  static nsHtml5AttributeName* ATTR_FLOOD_COLOR;
  static nsHtml5AttributeName* ATTR_FLOOD_OPACITY;
  static nsHtml5AttributeName* ATTR_LQUOTE;
  static nsHtml5AttributeName* ATTR_NUMOCTAVES;
  static nsHtml5AttributeName* ATTR_NOMODULE;
  static nsHtml5AttributeName* ATTR_ONLOAD;
  static nsHtml5AttributeName* ATTR_ONMOUSEWHEEL;
  static nsHtml5AttributeName* ATTR_ONMOUSEENTER;
  static nsHtml5AttributeName* ATTR_ONMOUSEOVER;
  static nsHtml5AttributeName* ATTR_ONFOCUSIN;
  static nsHtml5AttributeName* ATTR_ONCONTEXTMENU;
  static nsHtml5AttributeName* ATTR_ONZOOM;
  static nsHtml5AttributeName* ATTR_ONCOPY;
  static nsHtml5AttributeName* ATTR_ONMOUSELEAVE;
  static nsHtml5AttributeName* ATTR_ONMOUSEMOVE;
  static nsHtml5AttributeName* ATTR_ONMOUSEUP;
  static nsHtml5AttributeName* ATTR_ONFOCUS;
  static nsHtml5AttributeName* ATTR_ONMOUSEOUT;
  static nsHtml5AttributeName* ATTR_ONFOCUSOUT;
  static nsHtml5AttributeName* ATTR_ONMOUSEDOWN;
  static nsHtml5AttributeName* ATTR_TO;
  static nsHtml5AttributeName* ATTR_RQUOTE;
  static nsHtml5AttributeName* ATTR_STROKE_LINECAP;
  static nsHtml5AttributeName* ATTR_STROKE_DASHARRAY;
  static nsHtml5AttributeName* ATTR_STROKE_DASHOFFSET;
  static nsHtml5AttributeName* ATTR_STROKE_LINEJOIN;
  static nsHtml5AttributeName* ATTR_STROKE_MITERLIMIT;
  static nsHtml5AttributeName* ATTR_STROKE;
  static nsHtml5AttributeName* ATTR_SCROLLING;
  static nsHtml5AttributeName* ATTR_STROKE_WIDTH;
  static nsHtml5AttributeName* ATTR_STROKE_OPACITY;
  static nsHtml5AttributeName* ATTR_COMPACT;
  static nsHtml5AttributeName* ATTR_CLIP;
  static nsHtml5AttributeName* ATTR_CLIP_RULE;
  static nsHtml5AttributeName* ATTR_CLIP_PATH;
  static nsHtml5AttributeName* ATTR_CLIPPATHUNITS;
  static nsHtml5AttributeName* ATTR_DISPLAY;
  static nsHtml5AttributeName* ATTR_DISPLAYSTYLE;
  static nsHtml5AttributeName* ATTR_GLYPH_ORIENTATION_VERTICAL;
  static nsHtml5AttributeName* ATTR_GLYPH_ORIENTATION_HORIZONTAL;
  static nsHtml5AttributeName* ATTR_GLYPHREF;
  static nsHtml5AttributeName* ATTR_HTTP_EQUIV;
  static nsHtml5AttributeName* ATTR_KEYPOINTS;
  static nsHtml5AttributeName* ATTR_LOOP;
  static nsHtml5AttributeName* ATTR_PROPERTY;
  static nsHtml5AttributeName* ATTR_SCOPED;
  static nsHtml5AttributeName* ATTR_STEP;
  static nsHtml5AttributeName* ATTR_SHAPE_RENDERING;
  static nsHtml5AttributeName* ATTR_SCOPE;
  static nsHtml5AttributeName* ATTR_SHAPE;
  static nsHtml5AttributeName* ATTR_SLOPE;
  static nsHtml5AttributeName* ATTR_STOP_COLOR;
  static nsHtml5AttributeName* ATTR_STOP_OPACITY;
  static nsHtml5AttributeName* ATTR_TEMPLATE;
  static nsHtml5AttributeName* ATTR_WRAP;
  static nsHtml5AttributeName* ATTR_ABBR;
  static nsHtml5AttributeName* ATTR_ATTRIBUTENAME;
  static nsHtml5AttributeName* ATTR_ATTRIBUTETYPE;
  static nsHtml5AttributeName* ATTR_CHAR;
  static nsHtml5AttributeName* ATTR_COORDS;
  static nsHtml5AttributeName* ATTR_CHAROFF;
  static nsHtml5AttributeName* ATTR_CHARSET;
  static nsHtml5AttributeName* ATTR_NOWRAP;
  static nsHtml5AttributeName* ATTR_NOHREF;
  static nsHtml5AttributeName* ATTR_ONDRAG;
  static nsHtml5AttributeName* ATTR_ONDRAGENTER;
  static nsHtml5AttributeName* ATTR_ONDRAGOVER;
  static nsHtml5AttributeName* ATTR_ONDRAGEND;
  static nsHtml5AttributeName* ATTR_ONDROP;
  static nsHtml5AttributeName* ATTR_ONDRAGDROP;
  static nsHtml5AttributeName* ATTR_ONERROR;
  static nsHtml5AttributeName* ATTR_OPERATOR;
  static nsHtml5AttributeName* ATTR_OVERFLOW;
  static nsHtml5AttributeName* ATTR_ONDRAGSTART;
  static nsHtml5AttributeName* ATTR_ONDRAGLEAVE;
  static nsHtml5AttributeName* ATTR_STARTOFFSET;
  static nsHtml5AttributeName* ATTR_START;
  static nsHtml5AttributeName* ATTR_AS;
  static nsHtml5AttributeName* ATTR_AXIS;
  static nsHtml5AttributeName* ATTR_BIAS;
  static nsHtml5AttributeName* ATTR_COLSPAN;
  static nsHtml5AttributeName* ATTR_CLASSID;
  static nsHtml5AttributeName* ATTR_CROSSORIGIN;
  static nsHtml5AttributeName* ATTR_COLS;
  static nsHtml5AttributeName* ATTR_CURSOR;
  static nsHtml5AttributeName* ATTR_CLOSURE;
  static nsHtml5AttributeName* ATTR_CLOSE;
  static nsHtml5AttributeName* ATTR_CLASS;
  static nsHtml5AttributeName* ATTR_IS;
  static nsHtml5AttributeName* ATTR_KEYSYSTEM;
  static nsHtml5AttributeName* ATTR_KEYSPLINES;
  static nsHtml5AttributeName* ATTR_LOWSRC;
  static nsHtml5AttributeName* ATTR_MAXSIZE;
  static nsHtml5AttributeName* ATTR_MINSIZE;
  static nsHtml5AttributeName* ATTR_OFFSET;
  static nsHtml5AttributeName* ATTR_PRESERVEALPHA;
  static nsHtml5AttributeName* ATTR_PRESERVEASPECTRATIO;
  static nsHtml5AttributeName* ATTR_ROWSPAN;
  static nsHtml5AttributeName* ATTR_ROWSPACING;
  static nsHtml5AttributeName* ATTR_ROWS;
  static nsHtml5AttributeName* ATTR_SRCSET;
  static nsHtml5AttributeName* ATTR_SUBSCRIPTSHIFT;
  static nsHtml5AttributeName* ATTR_VERSION;
  static nsHtml5AttributeName* ATTR_ALTTEXT;
  static nsHtml5AttributeName* ATTR_CONTENTEDITABLE;
  static nsHtml5AttributeName* ATTR_CONTROLS;
  static nsHtml5AttributeName* ATTR_CONTENT;
  static nsHtml5AttributeName* ATTR_CONTEXTMENU;
  static nsHtml5AttributeName* ATTR_DEPTH;
  static nsHtml5AttributeName* ATTR_ENCTYPE;
  static nsHtml5AttributeName* ATTR_FONT_STRETCH;
  static nsHtml5AttributeName* ATTR_FILTER;
  static nsHtml5AttributeName* ATTR_FONTWEIGHT;
  static nsHtml5AttributeName* ATTR_FONT_WEIGHT;
  static nsHtml5AttributeName* ATTR_FONTSTYLE;
  static nsHtml5AttributeName* ATTR_FONT_STYLE;
  static nsHtml5AttributeName* ATTR_FONTFAMILY;
  static nsHtml5AttributeName* ATTR_FONT_FAMILY;
  static nsHtml5AttributeName* ATTR_FONT_VARIANT;
  static nsHtml5AttributeName* ATTR_FONT_SIZE_ADJUST;
  static nsHtml5AttributeName* ATTR_FILTERUNITS;
  static nsHtml5AttributeName* ATTR_FONTSIZE;
  static nsHtml5AttributeName* ATTR_FONT_SIZE;
  static nsHtml5AttributeName* ATTR_KEYTIMES;
  static nsHtml5AttributeName* ATTR_LETTER_SPACING;
  static nsHtml5AttributeName* ATTR_LIST;
  static nsHtml5AttributeName* ATTR_MULTIPLE;
  static nsHtml5AttributeName* ATTR_RT;
  static nsHtml5AttributeName* ATTR_ONSTOP;
  static nsHtml5AttributeName* ATTR_ONSTART;
  static nsHtml5AttributeName* ATTR_POSTER;
  static nsHtml5AttributeName* ATTR_PATTERNTRANSFORM;
  static nsHtml5AttributeName* ATTR_PATTERN;
  static nsHtml5AttributeName* ATTR_PATTERNUNITS;
  static nsHtml5AttributeName* ATTR_PATTERNCONTENTUNITS;
  static nsHtml5AttributeName* ATTR_RESTART;
  static nsHtml5AttributeName* ATTR_STITCHTILES;
  static nsHtml5AttributeName* ATTR_SYSTEMLANGUAGE;
  static nsHtml5AttributeName* ATTR_TEXT_RENDERING;
  static nsHtml5AttributeName* ATTR_TEXT_DECORATION;
  static nsHtml5AttributeName* ATTR_TEXT_ANCHOR;
  static nsHtml5AttributeName* ATTR_TEXTLENGTH;
  static nsHtml5AttributeName* ATTR_TEXT;
  static nsHtml5AttributeName* ATTR_WRITING_MODE;
  static nsHtml5AttributeName* ATTR_WIDTH;
  static nsHtml5AttributeName* ATTR_ACCUMULATE;
  static nsHtml5AttributeName* ATTR_COLUMNSPAN;
  static nsHtml5AttributeName* ATTR_COLUMNLINES;
  static nsHtml5AttributeName* ATTR_COLUMNALIGN;
  static nsHtml5AttributeName* ATTR_COLUMNSPACING;
  static nsHtml5AttributeName* ATTR_COLUMNWIDTH;
  static nsHtml5AttributeName* ATTR_GROUPALIGN;
  static nsHtml5AttributeName* ATTR_INPUTMODE;
  static nsHtml5AttributeName* ATTR_ONSUBMIT;
  static nsHtml5AttributeName* ATTR_ONCUT;
  static nsHtml5AttributeName* ATTR_REQUIRED;
  static nsHtml5AttributeName* ATTR_REQUIREDFEATURES;
  static nsHtml5AttributeName* ATTR_RESULT;
  static nsHtml5AttributeName* ATTR_REQUIREDEXTENSIONS;
  static nsHtml5AttributeName* ATTR_VALUES;
  static nsHtml5AttributeName* ATTR_VALUETYPE;
  static nsHtml5AttributeName* ATTR_VALUE;
  static nsHtml5AttributeName* ATTR_ELEVATION;
  static nsHtml5AttributeName* ATTR_VIEWTARGET;
  static nsHtml5AttributeName* ATTR_VIEWBOX;
  static nsHtml5AttributeName* ATTR_CX;
  static nsHtml5AttributeName* ATTR_DX;
  static nsHtml5AttributeName* ATTR_FX;
  static nsHtml5AttributeName* ATTR_RX;
  static nsHtml5AttributeName* ATTR_REFX;
  static nsHtml5AttributeName* ATTR_BY;
  static nsHtml5AttributeName* ATTR_CY;
  static nsHtml5AttributeName* ATTR_DY;
  static nsHtml5AttributeName* ATTR_FY;
  static nsHtml5AttributeName* ATTR_RY;
  static nsHtml5AttributeName* ATTR_REFY;

 private:
  static nsHtml5AttributeName** ATTRIBUTE_NAMES;
  static staticJArray<int32_t, int32_t> ATTRIBUTE_HASHES;

 public:
  static void initializeStatics();
  static void releaseStatics();
};

#endif
