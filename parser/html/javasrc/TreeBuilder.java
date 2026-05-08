/*
 * Copyright (c) 2007 Henri Sivonen
 * Copyright (c) 2007-2017 Mozilla Foundation
 * Portions of comments Copyright 2004-2008 Apple Computer, Inc., Mozilla
 * Foundation, and Opera Software ASA.
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
 * The comments following this one that use the same comment syntax as this
 * comment are quotes from the WHATWG HTML 5 spec as of 27 June 2007
 * amended as of June 28 2007.
 * That document came with this statement:
 * "© Copyright 2004-2007 Apple Computer, Inc., Mozilla Foundation, and
 * Opera Software ASA. You are granted a license to use, reproduce and
 * create derivative works of this document."
 */

package nu.validator.htmlparser.impl;

import java.util.Arrays;
import java.util.HashMap;
import java.util.Map;

import org.xml.sax.ErrorHandler;
import org.xml.sax.Locator;
import org.xml.sax.SAXException;
import org.xml.sax.SAXParseException;

import nu.validator.htmlparser.annotation.Auto;
import nu.validator.htmlparser.annotation.Const;
import nu.validator.htmlparser.annotation.IdType;
import nu.validator.htmlparser.annotation.Inline;
import nu.validator.htmlparser.annotation.Literal;
import nu.validator.htmlparser.annotation.Local;
import nu.validator.htmlparser.annotation.NoLength;
import nu.validator.htmlparser.annotation.NsUri;
import nu.validator.htmlparser.common.DocumentMode;
import nu.validator.htmlparser.common.DocumentModeHandler;
import nu.validator.htmlparser.common.Interner;
import nu.validator.htmlparser.common.TokenHandler;
import nu.validator.htmlparser.common.XmlViolationPolicy;

public abstract class TreeBuilder<T> implements TokenHandler,
        TreeBuilderState<T> {

    /**
     * Array version of U+FFFD.
     */
    private static final @NoLength char[] REPLACEMENT_CHARACTER = { '\uFFFD' };

    // Start dispatch groups

    final static int OTHER = 0;

    final static int A = 1;

    final static int BASE = 2;

    final static int BODY = 3;

    final static int BR = 4;

    final static int BUTTON = 5;

    final static int CAPTION = 6;

    final static int COL = 7;

    final static int COLGROUP = 8;

    final static int FORM = 9;

    final static int FRAME = 10;

    final static int FRAMESET = 11;

    final static int IMAGE = 12;

    final static int INPUT = 13;

    final static int RT_OR_RP = 14;

    final static int LI = 15;

    final static int LINK_OR_BASEFONT_OR_BGSOUND = 16;

    final static int MATH = 17;

    final static int META = 18;

    final static int SVG = 19;

    final static int HEAD = 20;

    final static int HR = 22;

    final static int HTML = 23;

    final static int NOBR = 24;

    final static int NOFRAMES = 25;

    final static int NOSCRIPT = 26;

    final static int OPTGROUP = 27;

    final static int OPTION = 28;

    final static int P = 29;

    final static int PLAINTEXT = 30;

    final static int SCRIPT = 31;

    final static int SELECT = 32;

    final static int STYLE = 33;

    final static int TABLE = 34;

    final static int TEXTAREA = 35;

    final static int TITLE = 36;

    final static int TR = 37;

    final static int XMP = 38;

    final static int TBODY_OR_THEAD_OR_TFOOT = 39;

    final static int TD_OR_TH = 40;

    final static int DD_OR_DT = 41;

    final static int H1_OR_H2_OR_H3_OR_H4_OR_H5_OR_H6 = 42;

    final static int MARQUEE_OR_APPLET = 43;

    final static int PRE_OR_LISTING = 44;

    final static int B_OR_BIG_OR_CODE_OR_EM_OR_I_OR_S_OR_SMALL_OR_STRIKE_OR_STRONG_OR_TT_OR_U = 45;

    final static int UL_OR_OL_OR_DL = 46;

    final static int IFRAME = 47;

    final static int EMBED = 48;

    final static int AREA_OR_WBR = 49;

    final static int DIV_OR_BLOCKQUOTE_OR_CENTER_OR_MENU = 50;

    final static int ADDRESS_OR_ARTICLE_OR_ASIDE_OR_DETAILS_OR_DIALOG_OR_DIR_OR_FIGCAPTION_OR_FIGURE_OR_FOOTER_OR_HEADER_OR_HGROUP_OR_MAIN_OR_NAV_OR_SEARCH_OR_SECTION_OR_SUMMARY = 51;

    final static int RUBY_OR_SPAN_OR_SUB_OR_SUP_OR_VAR = 52;

    final static int RB_OR_RTC = 53;

    final static int PARAM_OR_SOURCE_OR_TRACK = 55;

    final static int MGLYPH_OR_MALIGNMARK = 56;

    final static int MI_MO_MN_MS_MTEXT = 57;

    final static int ANNOTATION_XML = 58;

    final static int FOREIGNOBJECT_OR_DESC = 59;

    final static int NOEMBED = 60;

    final static int FIELDSET = 61;

    final static int OUTPUT = 62;

    final static int OBJECT = 63;

    final static int FONT = 64;

    final static int KEYGEN = 65;

    final static int TEMPLATE = 66;

    final static int IMG = 67;

    // start insertion modes

    private static final int IN_ROW = 0;

    private static final int IN_TABLE_BODY = 1;

    private static final int IN_TABLE = 2;

    private static final int IN_CAPTION = 3;

    private static final int IN_CELL = 4;

    private static final int FRAMESET_OK = 5;

    private static final int IN_BODY = 6;

    private static final int IN_HEAD = 7;

    private static final int IN_HEAD_NOSCRIPT = 8;

    // no fall-through

    private static final int IN_COLUMN_GROUP = 9;

    // no fall-through

    private static final int IN_SELECT_IN_TABLE = 10;

    private static final int IN_SELECT = 11;

    // no fall-through

    private static final int AFTER_BODY = 12;

    // no fall-through

    private static final int IN_FRAMESET = 13;

    private static final int AFTER_FRAMESET = 14;

    // no fall-through

    private static final int INITIAL = 15;

    // could add fall-through

    private static final int BEFORE_HTML = 16;

    // could add fall-through

    private static final int BEFORE_HEAD = 17;

    // no fall-through

    private static final int AFTER_HEAD = 18;

    // no fall-through

    private static final int AFTER_AFTER_BODY = 19;

    // no fall-through

    private static final int AFTER_AFTER_FRAMESET = 20;

    // no fall-through

    private static final int TEXT = 21;

    private static final int IN_TEMPLATE = 22;

    // start charset states

    private static final int CHARSET_INITIAL = 0;

    private static final int CHARSET_C = 1;

    private static final int CHARSET_H = 2;

    private static final int CHARSET_A = 3;

    private static final int CHARSET_R = 4;

    private static final int CHARSET_S = 5;

    private static final int CHARSET_E = 6;

    private static final int CHARSET_T = 7;

    private static final int CHARSET_EQUALS = 8;

    private static final int CHARSET_SINGLE_QUOTED = 9;

    private static final int CHARSET_DOUBLE_QUOTED = 10;

    private static final int CHARSET_UNQUOTED = 11;

    // end pseudo enums

    @Literal private final static String[] QUIRKY_PUBLIC_IDS = {
            "+//silmaril//dtd html pro v0r11 19970101//",
            "-//advasoft ltd//dtd html 3.0 aswedit + extensions//",
            "-//as//dtd html 3.0 aswedit + extensions//",
            "-//ietf//dtd html 2.0 level 1//",
            "-//ietf//dtd html 2.0 level 2//",
            "-//ietf//dtd html 2.0 strict level 1//",
            "-//ietf//dtd html 2.0 strict level 2//",
            "-//ietf//dtd html 2.0 strict//",
            "-//ietf//dtd html 2.0//",
            "-//ietf//dtd html 2.1e//",
            "-//ietf//dtd html 3.0//",
            "-//ietf//dtd html 3.2 final//",
            "-//ietf//dtd html 3.2//",
            "-//ietf//dtd html 3//",
            "-//ietf//dtd html level 0//",
            "-//ietf//dtd html level 1//",
            "-//ietf//dtd html level 2//",
            "-//ietf//dtd html level 3//",
            "-//ietf//dtd html strict level 0//",
            "-//ietf//dtd html strict level 1//",
            "-//ietf//dtd html strict level 2//",
            "-//ietf//dtd html strict level 3//",
            "-//ietf//dtd html strict//",
            "-//ietf//dtd html//",
            "-//metrius//dtd metrius presentational//",
            "-//microsoft//dtd internet explorer 2.0 html strict//",
            "-//microsoft//dtd internet explorer 2.0 html//",
            "-//microsoft//dtd internet explorer 2.0 tables//",
            "-//microsoft//dtd internet explorer 3.0 html strict//",
            "-//microsoft//dtd internet explorer 3.0 html//",
            "-//microsoft//dtd internet explorer 3.0 tables//",
            "-//netscape comm. corp.//dtd html//",
            "-//netscape comm. corp.//dtd strict html//",
            "-//o'reilly and associates//dtd html 2.0//",
            "-//o'reilly and associates//dtd html extended 1.0//",
            "-//o'reilly and associates//dtd html extended relaxed 1.0//",
            "-//softquad software//dtd hotmetal pro 6.0::19990601::extensions to html 4.0//",
            "-//softquad//dtd hotmetal pro 4.0::19971010::extensions to html 4.0//",
            "-//spyglass//dtd html 2.0 extended//",
            "-//sq//dtd html 2.0 hotmetal + extensions//",
            "-//sun microsystems corp.//dtd hotjava html//",
            "-//sun microsystems corp.//dtd hotjava strict html//",
            "-//w3c//dtd html 3 1995-03-24//", "-//w3c//dtd html 3.2 draft//",
            "-//w3c//dtd html 3.2 final//", "-//w3c//dtd html 3.2//",
            "-//w3c//dtd html 3.2s draft//", "-//w3c//dtd html 4.0 frameset//",
            "-//w3c//dtd html 4.0 transitional//",
            "-//w3c//dtd html experimental 19960712//",
            "-//w3c//dtd html experimental 970421//", "-//w3c//dtd w3 html//",
            "-//w3o//dtd w3 html 3.0//", "-//webtechs//dtd mozilla html 2.0//",
            "-//webtechs//dtd mozilla html//" };

    private static final int NOT_FOUND_ON_STACK = Integer.MAX_VALUE;

    // [NOCPP[

    private static final @Local String HTML_LOCAL = "html";

    // ]NOCPP]

    private int mode = INITIAL;

    private int originalMode = INITIAL;

    /**
     * Used only when moving back to IN_BODY.
     */
    private boolean framesetOk = true;

    protected Tokenizer tokenizer;

    // [NOCPP[

    protected ErrorHandler errorHandler;

    private DocumentModeHandler documentModeHandler;

    // ]NOCPP]

    private boolean scriptingEnabled = false;

    private boolean needToDropLF;

    // [NOCPP[

    private boolean wantingComments;

    // ]NOCPP]

    private boolean fragment;

    private @Local String contextName;

    private @NsUri String contextNamespace;

    private T contextNode;

    /**
     * Stack of template insertion modes
     */
    private @Auto int[] templateModeStack;

    /**
     * Current template mode stack pointer.
     */
    private int templateModePtr = -1;

    private @Auto StackNode<T>[] stackNodes;

    /**
     * Index of the earliest possible unused or empty element in stackNodes.
     */
    private int stackNodesIdx = -1;

    private int numStackNodes = 0;

    private @Auto StackNode<T>[] stack;

    private int currentPtr = -1;

    private @Auto StackNode<T>[] listOfActiveFormattingElements;

    private int listPtr = -1;

    private T formPointer;

    private T headPointer;

    protected @Auto char[] charBuffer;

    protected int charBufferLen = 0;

    private boolean quirks = false;

    private boolean forceNoQuirks = false;

    private boolean allowDeclarativeShadowRoots = false;

    private boolean noInSelectMode = false;

    private boolean keepBuffer = false;

    // [NOCPP[

    private boolean reportingDoctype = true;

    private XmlViolationPolicy namePolicy = XmlViolationPolicy.ALTER_INFOSET;

    private final Map<String, LocatorImpl> idLocations = new HashMap<String, LocatorImpl>();

    // ]NOCPP]

    protected TreeBuilder() {
        fragment = false;
    }

    /**
     * Reports an condition that would make the infoset incompatible with XML
     * 1.0 as fatal.
     *
     * @throws SAXException
     * @throws SAXParseException
     */
    protected void fatal() throws SAXException {
    }

    // CPPONLY: @Inline private @Creator Object htmlCreator(@HtmlCreator Object htmlCreator) {
    // CPPONLY:     @Creator Object creator;
    // CPPONLY:     creator.html = htmlCreator;
    // CPPONLY:     return creator;
    // CPPONLY: }
    // CPPONLY:
    // CPPONLY: @Inline private @Creator Object svgCreator(@SvgCreator Object svgCreator) {
    // CPPONLY:     @Creator Object creator;
    // CPPONLY:     creator.svg = svgCreator;
    // CPPONLY:     return creator;
    // CPPONLY: }

    // [NOCPP[

    protected final void fatal(Exception e) throws SAXException {
        SAXParseException spe = new SAXParseException(e.getMessage(),
                tokenizer, e);
        if (errorHandler != null) {
            errorHandler.fatalError(spe);
        }
        throw spe;
    }

    final void fatal(String s) throws SAXException {
        SAXParseException spe = new SAXParseException(s, tokenizer);
        if (errorHandler != null) {
            errorHandler.fatalError(spe);
        }
        throw spe;
    }

    /**
     * Reports a Parse Error.
     *
     * @param message
     *            the message
     * @throws SAXException
     */
    final void err(String message) throws SAXException {
        if (errorHandler == null) {
            return;
        }
        errNoCheck(message);
    }

    /**
     * Reports a Parse Error without checking if an error handler is present.
     *
     * @param message
     *            the message
     * @throws SAXException
     */
    final void errNoCheck(String message) throws SAXException {
        SAXParseException spe = new SAXParseException(message, tokenizer);
        errorHandler.error(spe);
    }

    private void errListUnclosedStartTags(int eltPos) throws SAXException {
        if (currentPtr != -1) {
            for (int i = currentPtr; i > eltPos; i--) {
                reportUnclosedElementNameAndLocation(i);
            }
        }
    }

    /**
     * Reports the name and location of an unclosed element.
     *
     * @throws SAXException
     */
    private final void reportUnclosedElementNameAndLocation(int pos) throws SAXException {
        StackNode<T> node = stack[pos];
        if (node.isOptionalEndTag()) {
            return;
        }
        TaintableLocatorImpl locator = node.getLocator();
        if (locator.isTainted()) {
            return;
        }
        locator.markTainted();
        SAXParseException spe = new SAXParseException(
                "Unclosed element \u201C" + node.popName + "\u201D.", locator);
        errorHandler.error(spe);
    }

    /**
     * Reports a warning
     *
     * @param message
     *            the message
     * @throws SAXException
     */
    final void warn(String message) throws SAXException {
        if (errorHandler == null) {
            return;
        }
        SAXParseException spe = new SAXParseException(message, tokenizer);
        errorHandler.warning(spe);
    }

    /**
     * Reports a warning with an explicit locator
     *
     * @param message
     *            the message
     * @throws SAXException
     */
    final void warn(String message, Locator locator) throws SAXException {
        if (errorHandler == null) {
            return;
        }
        SAXParseException spe = new SAXParseException(message, locator);
        errorHandler.warning(spe);
    }

    // ]NOCPP]

    public void setKeepBuffer(boolean keepBuffer) {
        this.keepBuffer = keepBuffer;
    }

    public boolean dropBufferIfLongerThan(int length) {
        if (charBuffer.length > length) {
            charBuffer = null;
            return true;
        }
        return false;
    }

    @SuppressWarnings("unchecked") public final void startTokenization(Tokenizer self) throws SAXException {
        tokenizer = self;
        stackNodes = new StackNode[64];
        stack = new StackNode[64];
        templateModeStack = new int[64];
        listOfActiveFormattingElements = new StackNode[64];
        needToDropLF = false;
        mode = INITIAL;
        originalMode = INITIAL;
        templateModePtr = -1;
        stackNodesIdx = 0;
        numStackNodes = 0;
        currentPtr = -1;
        listPtr = -1;
        formPointer = null;
        headPointer = null;
        // [NOCPP[
        idLocations.clear();
        wantingComments = wantsComments();
        // ]NOCPP]
        start(fragment);
        charBufferLen = 0;
        if (!keepBuffer) {
            charBuffer = null;
        }
        framesetOk = true;
        if (fragment) {
            T elt;
            if (contextNode != null) {
                elt = contextNode;
            } else {
                elt = createHtmlElementSetAsRoot(tokenizer.emptyAttributes());
            }
            // When the context node is not in the HTML namespace, contrary
            // to the spec, the first node on the stack is not set to "html"
            // in the HTML namespace. Instead, it is set to a node that has
            // the characteristics of the appropriate "adjusted current node".
            // This way, there is no need to perform "adjusted current node"
            // checks during tree construction. Instead, it's sufficient to
            // just look at the current node. However, this also means that it
            // is not safe to treat "html" in the HTML namespace as a sentinel
            // that ends stack popping. Instead, stack popping loops that are
            // meant not to pop the first element on the stack need to check
            // for currentPos becoming zero.
            if (contextNamespace == "http://www.w3.org/2000/svg") {
                ElementName elementName = ElementName.SVG;
                if ("title" == contextName || "desc" == contextName
                        || "foreignObject" == contextName) {
                    // These elements are all alike and we don't care about
                    // the exact name.
                    elementName = ElementName.FOREIGNOBJECT;
                }
                // This is the SVG variant of the StackNode constructor.
                StackNode<T> node = createStackNode(elementName,
                        elementName.getCamelCaseName(), elt
                        // [NOCPP[
                        , errorHandler == null ? null
                                : new TaintableLocatorImpl(tokenizer)
                // ]NOCPP]
                );
                currentPtr++;
                stack[currentPtr] = node;
                tokenizer.setState(Tokenizer.DATA);
                // The frameset-ok flag is set even though <frameset> never
                // ends up being allowed as HTML frameset in the fragment case.
                mode = FRAMESET_OK;
            } else if (contextNamespace == "http://www.w3.org/1998/Math/MathML") {
                ElementName elementName = ElementName.MATH;
                if ("mi" == contextName || "mo" == contextName
                        || "mn" == contextName || "ms" == contextName
                        || "mtext" == contextName) {
                    // These elements are all alike and we don't care about
                    // the exact name.
                    elementName = ElementName.MTEXT;
                } else if ("annotation-xml" == contextName) {
                    elementName = ElementName.ANNOTATION_XML;
                    // Blink does not check the encoding attribute of the
                    // annotation-xml element innerHTML is being set on.
                    // Let's do the same at least until
                    // https://www.w3.org/Bugs/Public/show_bug.cgi?id=26783
                    // is resolved.
                }
                // This is the MathML variant of the StackNode constructor.
                StackNode<T> node = createStackNode(elementName, elt,
                        elementName.getName(), false
                        // [NOCPP[
                        , errorHandler == null ? null
                                : new TaintableLocatorImpl(tokenizer)
                // ]NOCPP]
                );
                currentPtr++;
                stack[currentPtr] = node;
                tokenizer.setState(Tokenizer.DATA);
                // The frameset-ok flag is set even though <frameset> never
                // ends up being allowed as HTML frameset in the fragment case.
                mode = FRAMESET_OK;
            } else { // html
                StackNode<T> node = createStackNode(ElementName.HTML, elt
                // [NOCPP[
                        , errorHandler == null ? null
                                : new TaintableLocatorImpl(tokenizer)
                // ]NOCPP]
                );
                currentPtr++;
                stack[currentPtr] = node;
                if ("template" == contextName) {
                    pushTemplateMode(IN_TEMPLATE);
                }
                resetTheInsertionMode();
                formPointer = getFormPointerForContext(contextNode);
                if ("title" == contextName || "textarea" == contextName) {
                    tokenizer.setState(Tokenizer.RCDATA);
                } else if ("style" == contextName || "xmp" == contextName
                        || "iframe" == contextName || "noembed" == contextName
                        || "noframes" == contextName
                        || (scriptingEnabled && "noscript" == contextName)) {
                    tokenizer.setState(Tokenizer.RAWTEXT);
                } else if ("plaintext" == contextName) {
                    tokenizer.setState(Tokenizer.PLAINTEXT);
                } else if ("script" == contextName) {
                    tokenizer.setState(Tokenizer.SCRIPT_DATA);
                } else {
                    tokenizer.setState(Tokenizer.DATA);
                }
            }
        } else {
            mode = INITIAL;
            // If we are viewing XML source, put a foreign element permanently
            // on the stack so that cdataSectionAllowed() returns true.
            // CPPONLY: if (tokenizer.isViewingXmlSource()) {
            // CPPONLY: T elt = createElement("http://www.w3.org/2000/svg",
            // CPPONLY: "svg",
            // CPPONLY: tokenizer.emptyAttributes(), null,
            // CPPONLY: svgCreator(NS_NewSVGSVGElement));
            // CPPONLY: StackNode<T> node = createStackNode(ElementName.SVG,
            // CPPONLY: "svg",
            // CPPONLY: elt);
            // CPPONLY: currentPtr++;
            // CPPONLY: stack[currentPtr] = node;
            // CPPONLY: }
        }
    }

    public final void doctype(@Local String name, String publicIdentifier,
            String systemIdentifier, boolean forceQuirks) throws SAXException {
        needToDropLF = false;
        if (!isInForeign() && mode == INITIAL) {
            // [NOCPP[
            if (reportingDoctype) {
                // ]NOCPP]
                String emptyString = Portability.newEmptyString();
                appendDoctypeToDocument(name == null ? "" : name,
                        publicIdentifier == null ? emptyString
                                : publicIdentifier,
                        systemIdentifier == null ? emptyString
                                : systemIdentifier);
                Portability.releaseString(emptyString);
                // [NOCPP[
            }
            // ]NOCPP]
            if (isQuirky(name, publicIdentifier, systemIdentifier,
                    forceQuirks)) {
                errQuirkyDoctype();
                documentModeInternal(DocumentMode.QUIRKS_MODE,
                        publicIdentifier, systemIdentifier);
            } else if (isAlmostStandards(publicIdentifier,
                    systemIdentifier)) {
                errAlmostStandardsDoctype();
                documentModeInternal(
                        DocumentMode.ALMOST_STANDARDS_MODE,
                        publicIdentifier, systemIdentifier);
            } else {
                // [NOCPP[
                if ((Portability.literalEqualsString(
                        "-//W3C//DTD HTML 4.0//EN", publicIdentifier) && (systemIdentifier == null || Portability.literalEqualsString(
                        "http://www.w3.org/TR/REC-html40/strict.dtd",
                        systemIdentifier)))
                        || (Portability.literalEqualsString(
                                "-//W3C//DTD HTML 4.01//EN",
                                publicIdentifier) && (systemIdentifier == null || Portability.literalEqualsString(
                                "http://www.w3.org/TR/html4/strict.dtd",
                                systemIdentifier)))
                        || (Portability.literalEqualsString(
                                "-//W3C//DTD XHTML 1.0 Strict//EN",
                                publicIdentifier) && Portability.literalEqualsString(
                                "http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd",
                                systemIdentifier))
                        || (Portability.literalEqualsString(
                                "-//W3C//DTD XHTML 1.1//EN",
                                publicIdentifier) && Portability.literalEqualsString(
                                "http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd",
                                systemIdentifier))

                ) {
                    err("Obsolete doctype. Expected \u201C<!DOCTYPE html>\u201D.");
                } else if (!((systemIdentifier == null || Portability.literalEqualsString(
                        "about:legacy-compat", systemIdentifier)) && publicIdentifier == null)) {
                    err("Legacy doctype. Expected \u201C<!DOCTYPE html>\u201D.");
                }
                // ]NOCPP]
                documentModeInternal(DocumentMode.STANDARDS_MODE,
                        publicIdentifier, systemIdentifier);
            }

            /*
             *
             * Then, switch to the root element mode of the tree construction
             * stage.
             */
            mode = BEFORE_HTML;
            return;
        }
        /*
         * A DOCTYPE token Parse error.
         */
        errStrayDoctype();
        /*
         * Ignore the token.
         */
        return;
    }

    public final void comment(@NoLength char[] buf, int start, int length)
            throws SAXException {
        needToDropLF = false;
        // [NOCPP[
        if (!wantingComments) {
            return;
        }
        // ]NOCPP]
        if (!isInForeign()) {
            switch (mode) {
                case INITIAL:
                case BEFORE_HTML:
                case AFTER_AFTER_BODY:
                case AFTER_AFTER_FRAMESET:
                    /*
                     * A comment token Append a Comment node to the Document
                     * object with the data attribute set to the data given in
                     * the comment token.
                     */
                    appendCommentToDocument(buf, start, length);
                    return;
                case AFTER_BODY:
                    /*
                     * A comment token Append a Comment node to the first
                     * element in the stack of open elements (the html element),
                     * with the data attribute set to the data given in the
                     * comment token.
                     */
                    flushCharacters();
                    appendComment(stack[0].node, buf, start, length);
                    return;
                default:
                    break;
            }
        }
        /*
         * A comment token Append a Comment node to the current node with the
         * data attribute set to the data given in the comment token.
         */
        flushCharacters();
        appendComment(stack[currentPtr].node, buf, start, length);
        return;
    }

    /**
     * @see nu.validator.htmlparser.common.TokenHandler#characters(char[], int,
     *      int)
     */
    public final void characters(@Const @NoLength char[] buf, int start, int length)
            throws SAXException {
        // Note: Can't attach error messages to EOF in C++ yet

        // CPPONLY: if (tokenizer.isViewingXmlSource()) {
        // CPPONLY: return;
        // CPPONLY: }
        if (needToDropLF) {
            needToDropLF = false;
            if (buf[start] == '\n') {
                start++;
                length--;
                if (length == 0) {
                    return;
                }
            }
        }

        // optimize the most common case
        switch (mode) {
            case IN_BODY:
            case IN_CELL:
            case IN_CAPTION:
                if (!isInForeignButNotHtmlOrMathTextIntegrationPoint()) {
                    reconstructTheActiveFormattingElements();
                }
                // CPPONLY: MOZ_FALLTHROUGH;
            case TEXT:
                accumulateCharacters(buf, start, length);
                return;
            case IN_TABLE:
            case IN_TABLE_BODY:
            case IN_ROW:
                accumulateCharactersForced(buf, start, length);
                return;
            default:
                int end = start + length;
                charactersloop: for (int i = start; i < end; i++) {
                    switch (buf[i]) {
                        case ' ':
                        case '\t':
                        case '\n':
                        case '\r':
                        case '\u000C':
                            /*
                             * A character token that is one of one of U+0009
                             * CHARACTER TABULATION, U+000A LINE FEED (LF),
                             * U+000C FORM FEED (FF), or U+0020 SPACE
                             */
                            switch (mode) {
                                case INITIAL:
                                case BEFORE_HTML:
                                case BEFORE_HEAD:
                                    /*
                                     * Ignore the token.
                                     */
                                    start = i + 1;
                                    continue;
                                case IN_HEAD:
                                case IN_HEAD_NOSCRIPT:
                                case AFTER_HEAD:
                                case IN_COLUMN_GROUP:
                                case IN_FRAMESET:
                                case AFTER_FRAMESET:
                                    /*
                                     * Append the character to the current node.
                                     */
                                    continue;
                                case FRAMESET_OK:
                                case IN_TEMPLATE:
                                case IN_BODY:
                                case IN_CELL:
                                case IN_CAPTION:
                                    if (start < i) {
                                        accumulateCharacters(buf, start, i
                                                - start);
                                        start = i;
                                    }

                                    /*
                                     * Reconstruct the active formatting
                                     * elements, if any.
                                     */
                                    if (!isInForeignButNotHtmlOrMathTextIntegrationPoint()) {
                                        flushCharacters();
                                        reconstructTheActiveFormattingElements();
                                    }
                                    /*
                                     * Append the token's character to the
                                     * current node.
                                     */
                                    break charactersloop;
                                case IN_SELECT:
                                case IN_SELECT_IN_TABLE:
                                    // CPPONLY: MOZ_ASSERT(!noInSelectMode);
                                    break charactersloop;
                                case IN_TABLE:
                                case IN_TABLE_BODY:
                                case IN_ROW:
                                    accumulateCharactersForced(buf, i, 1);
                                    start = i + 1;
                                    continue;
                                case AFTER_BODY:
                                case AFTER_AFTER_BODY:
                                case AFTER_AFTER_FRAMESET:
                                    if (start < i) {
                                        accumulateCharacters(buf, start, i
                                                - start);
                                        start = i;
                                    }
                                    /*
                                     * Reconstruct the active formatting
                                     * elements, if any.
                                     */
                                    flushCharacters();
                                    reconstructTheActiveFormattingElements();
                                    /*
                                     * Append the token's character to the
                                     * current node.
                                     */
                                    continue;
                            }
                            // CPPONLY: MOZ_FALLTHROUGH_ASSERT();
                        default:
                            /*
                             * A character token that is not one of one of
                             * U+0009 CHARACTER TABULATION, U+000A LINE FEED
                             * (LF), U+000C FORM FEED (FF), or U+0020 SPACE
                             */
                            switch (mode) {
                                case INITIAL:
                                    /*
                                     * Parse error.
                                     */
                                    // [NOCPP[
                                    // XXX figure out a way to report this in the Gecko View Source case
                                    err("Non-space characters found without seeing a doctype first. Expected \u201C<!DOCTYPE html>\u201D.");
                                    // ]NOCPP]
                                    /*
                                     *
                                     * Set the document to quirks mode.
                                     */
                                    documentModeInternal(
                                            DocumentMode.QUIRKS_MODE, null,
                                            null);
                                    /*
                                     * Then, switch to the root element mode of
                                     * the tree construction stage
                                     */
                                    mode = BEFORE_HTML;
                                    /*
                                     * and reprocess the current token.
                                     */
                                    i--;
                                    continue;
                                case BEFORE_HTML:
                                    /*
                                     * Create an HTMLElement node with the tag
                                     * name html, in the HTML namespace. Append
                                     * it to the Document object.
                                     */
                                    // No need to flush characters here,
                                    // because there's nothing to flush.
                                    appendHtmlElementToDocumentAndPush();
                                    /* Switch to the main mode */
                                    mode = BEFORE_HEAD;
                                    /*
                                     * reprocess the current token.
                                     */
                                    i--;
                                    continue;
                                case BEFORE_HEAD:
                                    if (start < i) {
                                        accumulateCharacters(buf, start, i
                                                - start);
                                        start = i;
                                    }
                                    /*
                                     * /Act as if a start tag token with the tag
                                     * name "head" and no attributes had been
                                     * seen,
                                     */
                                    flushCharacters();
                                    appendToCurrentNodeAndPushHeadElement(HtmlAttributes.EMPTY_ATTRIBUTES);
                                    mode = IN_HEAD;
                                    /*
                                     * then reprocess the current token.
                                     *
                                     * This will result in an empty head element
                                     * being generated, with the current token
                                     * being reprocessed in the "after head"
                                     * insertion mode.
                                     */
                                    i--;
                                    continue;
                                case IN_HEAD:
                                    if (start < i) {
                                        accumulateCharacters(buf, start, i
                                                - start);
                                        start = i;
                                    }
                                    /*
                                     * Act as if an end tag token with the tag
                                     * name "head" had been seen,
                                     */
                                    flushCharacters();
                                    pop();
                                    mode = AFTER_HEAD;
                                    /*
                                     * and reprocess the current token.
                                     */
                                    i--;
                                    continue;
                                case IN_HEAD_NOSCRIPT:
                                    if (start < i) {
                                        accumulateCharacters(buf, start, i
                                                - start);
                                        start = i;
                                    }
                                    /*
                                     * Parse error. Act as if an end tag with
                                     * the tag name "noscript" had been seen
                                     */
                                    errNonSpaceInNoscriptInHead();
                                    flushCharacters();
                                    pop();
                                    mode = IN_HEAD;
                                    /*
                                     * and reprocess the current token.
                                     */
                                    i--;
                                    continue;
                                case AFTER_HEAD:
                                    if (start < i) {
                                        accumulateCharacters(buf, start, i
                                                - start);
                                        start = i;
                                    }
                                    /*
                                     * Act as if a start tag token with the tag
                                     * name "body" and no attributes had been
                                     * seen,
                                     */
                                    flushCharacters();
                                    appendToCurrentNodeAndPushBodyElement();
                                    mode = FRAMESET_OK;
                                    /*
                                     * and then reprocess the current token.
                                     */
                                    i--;
                                    continue;
                                case FRAMESET_OK:
                                    framesetOk = false;
                                    mode = IN_BODY;
                                    i--;
                                    continue;
                                case IN_TEMPLATE:
                                case IN_BODY:
                                case IN_CELL:
                                case IN_CAPTION:
                                    if (start < i) {
                                        accumulateCharacters(buf, start, i
                                                - start);
                                        start = i;
                                    }
                                    /*
                                     * Reconstruct the active formatting
                                     * elements, if any.
                                     */
                                    if (!isInForeignButNotHtmlOrMathTextIntegrationPoint()) {
                                        flushCharacters();
                                        reconstructTheActiveFormattingElements();
                                    }
                                    /*
                                     * Append the token's character to the
                                     * current node.
                                     */
                                    break charactersloop;
                                case IN_TABLE:
                                case IN_TABLE_BODY:
                                case IN_ROW:
                                    accumulateCharactersForced(buf, i, 1);
                                    start = i + 1;
                                    continue;
                                case IN_COLUMN_GROUP:
                                    if (start < i) {
                                        accumulateCharacters(buf, start, i
                                                - start);
                                        start = i;
                                    }
                                    /*
                                     * Act as if an end tag with the tag name
                                     * "colgroup" had been seen, and then, if
                                     * that token wasn't ignored, reprocess the
                                     * current token.
                                     */
                                    if (currentPtr == 0 || stack[currentPtr].getGroup() ==
                                            TreeBuilder.TEMPLATE) {
                                        errNonSpaceInColgroupInFragment();
                                        start = i + 1;
                                        continue;
                                    }
                                    flushCharacters();
                                    pop();
                                    mode = IN_TABLE;
                                    i--;
                                    continue;
                                case IN_SELECT:
                                case IN_SELECT_IN_TABLE:
                                    // CPPONLY: MOZ_ASSERT(!noInSelectMode);
                                    break charactersloop;
                                case AFTER_BODY:
                                    errNonSpaceAfterBody();
                                    fatal();
                                    mode = framesetOk ? FRAMESET_OK : IN_BODY;
                                    i--;
                                    continue;
                                case IN_FRAMESET:
                                    if (start < i) {
                                        accumulateCharacters(buf, start, i
                                                - start);
                                        // start index is adjusted below.
                                    }
                                    /*
                                     * Parse error.
                                     */
                                    errNonSpaceInFrameset();
                                    /*
                                     * Ignore the token.
                                     */
                                    start = i + 1;
                                    continue;
                                case AFTER_FRAMESET:
                                    if (start < i) {
                                        accumulateCharacters(buf, start, i
                                                - start);
                                        // start index is adjusted below.
                                    }
                                    /*
                                     * Parse error.
                                     */
                                    errNonSpaceAfterFrameset();
                                    /*
                                     * Ignore the token.
                                     */
                                    start = i + 1;
                                    continue;
                                case AFTER_AFTER_BODY:
                                    /*
                                     * Parse error.
                                     */
                                    errNonSpaceInTrailer();
                                    /*
                                     * Switch back to the main mode and
                                     * reprocess the token.
                                     */
                                    mode = framesetOk ? FRAMESET_OK : IN_BODY;
                                    i--;
                                    continue;
                                case AFTER_AFTER_FRAMESET:
                                    if (start < i) {
                                        accumulateCharacters(buf, start, i
                                                - start);
                                        // start index is adjusted below.
                                    }
                                    /*
                                     * Parse error.
                                     */
                                    errNonSpaceInTrailer();
                                    /*
                                     * Ignore the token.
                                     */
                                    start = i + 1;
                                    continue;
                            }
                    }
                }
                if (start < end) {
                    accumulateCharacters(buf, start, end - start);
                }
        }
    }

    /**
     * @see nu.validator.htmlparser.common.TokenHandler#zeroOriginatingReplacementCharacter()
     */
    public void zeroOriginatingReplacementCharacter() throws SAXException {
        if (mode == TEXT) {
            accumulateCharacters(REPLACEMENT_CHARACTER, 0, 1);
            return;
        }
        if (currentPtr >= 0) {
            if (isSpecialParentInForeign(stack[currentPtr])) {
                return;
            }
            accumulateCharacters(REPLACEMENT_CHARACTER, 0, 1);
        }
    }

    /**
     * @see nu.validator.htmlparser.common.TokenHandler#zeroOrReplacementCharacter()
     */
    public void zeroOrReplacementCharacter() throws SAXException {
        zeroOriginatingReplacementCharacter();
    }

    public final void eof() throws SAXException {
        flushCharacters();
        // Note: Can't attach error messages to EOF in C++ yet
        eofloop: for (;;) {
            switch (mode) {
                case INITIAL:
                    /*
                     * Parse error.
                     */
                    // [NOCPP[
                    err("End of file seen without seeing a doctype first. Expected \u201C<!DOCTYPE html>\u201D.");
                    // ]NOCPP]
                    /*
                     *
                     * Set the document to quirks mode.
                     */
                    documentModeInternal(DocumentMode.QUIRKS_MODE, null, null);
                    /*
                     * Then, switch to the root element mode of the tree
                     * construction stage
                     */
                    mode = BEFORE_HTML;
                    /*
                     * and reprocess the current token.
                     */
                    continue;
                case BEFORE_HTML:
                    /*
                     * Create an HTMLElement node with the tag name html, in the
                     * HTML namespace. Append it to the Document object.
                     */
                    appendHtmlElementToDocumentAndPush();
                    // XXX application cache manifest
                    /* Switch to the main mode */
                    mode = BEFORE_HEAD;
                    /*
                     * reprocess the current token.
                     */
                    continue;
                case BEFORE_HEAD:
                    appendToCurrentNodeAndPushHeadElement(HtmlAttributes.EMPTY_ATTRIBUTES);
                    mode = IN_HEAD;
                    continue;
                case IN_HEAD:
                    // [NOCPP[
                    if (errorHandler != null && currentPtr > 1) {
                        errEofWithUnclosedElements();
                    }
                    // ]NOCPP]
                    while (currentPtr > 0) {
                        popOnEof();
                    }
                    mode = AFTER_HEAD;
                    continue;
                case IN_HEAD_NOSCRIPT:
                    // [NOCPP[
                    errEofWithUnclosedElements();
                    // ]NOCPP]
                    while (currentPtr > 1) {
                        popOnEof();
                    }
                    mode = IN_HEAD;
                    continue;
                case AFTER_HEAD:
                    appendToCurrentNodeAndPushBodyElement();
                    mode = IN_BODY;
                    continue;
                case IN_TABLE_BODY:
                case IN_ROW:
                case IN_TABLE:
                case IN_SELECT_IN_TABLE:
                case IN_SELECT:
                case IN_COLUMN_GROUP:
                case FRAMESET_OK:
                case IN_CAPTION:
                case IN_CELL:
                case IN_BODY:
                    // [NOCPP[
                    // i > 0 to stop in time in the foreign fragment case.
                    openelementloop: for (int i = currentPtr; i > 0; i--) {
                        int group = stack[i].getGroup();
                        switch (group) {
                            case DD_OR_DT:
                            case LI:
                            case P:
                            case TBODY_OR_THEAD_OR_TFOOT:
                            case TD_OR_TH:
                            case BODY:
                            case HTML:
                                break;
                            default:
                                errEofWithUnclosedElements();
                                break openelementloop;
                        }
                    }
                    // ]NOCPP]

                    if (isTemplateModeStackEmpty()) {
                        break eofloop;
                    }

                    // fall through to IN_TEMPLATE
                    // CPPONLY: MOZ_FALLTHROUGH;
                case IN_TEMPLATE:
                    int eltPos = findLast("template");
                    if (eltPos == TreeBuilder.NOT_FOUND_ON_STACK) {
                        assert fragment;
                        break eofloop;
                    }
                    if (errorHandler != null) {
                        errListUnclosedStartTags(0);
                    }
                    while (currentPtr >= eltPos) {
                        pop();
                    }
                    clearTheListOfActiveFormattingElementsUpToTheLastMarker();
                    popTemplateMode();
                    resetTheInsertionMode();

                    // Reprocess token.
                    continue;
                case TEXT:
                    // [NOCPP[
                    if (errorHandler != null) {
                        errNoCheck("End of file seen when expecting text or an end tag.");
                        errListUnclosedStartTags(0);
                    }
                    // ]NOCPP]
                    // XXX mark script as already executed
                    if (originalMode == AFTER_HEAD) {
                        popOnEof();
                    }
                    popOnEof();
                    mode = originalMode;
                    continue;
                case IN_FRAMESET:
                    // [NOCPP[
                    if (errorHandler != null && currentPtr > 0) {
                        errEofWithUnclosedElements();
                    }
                    // ]NOCPP]
                    break eofloop;
                case AFTER_BODY:
                case AFTER_FRAMESET:
                case AFTER_AFTER_BODY:
                case AFTER_AFTER_FRAMESET:
                default:
                    // [NOCPP[
                    if (currentPtr == 0) { // This silliness is here to poison
                        // buggy compiler optimizations in
                        // GWT
                        System.currentTimeMillis();
                    }
                    // ]NOCPP]
                    break eofloop;
            }
        }
        while (currentPtr > 0) {
            popOnEof();
        }
        if (!fragment) {
            popOnEof();
        }
        /* Stop parsing. */
    }

    /**
     * @see nu.validator.htmlparser.common.TokenHandler#endTokenization()
     */
    public final void endTokenization() throws SAXException {
        formPointer = null;
        headPointer = null;
        contextName = null;
        contextNode = null;
        templateModeStack = null;
        if (stack != null) {
            while (currentPtr > -1) {
                stack[currentPtr].release(this);
                currentPtr--;
            }
            stack = null;
        }
        if (listOfActiveFormattingElements != null) {
            while (listPtr > -1) {
                if (listOfActiveFormattingElements[listPtr] != null) {
                    listOfActiveFormattingElements[listPtr].release(this);
                }
                listPtr--;
            }
            listOfActiveFormattingElements = null;
        }
        if (stackNodes != null) {
            for (int i = 0; i < numStackNodes; i++) {
                assert stackNodes[i].isUnused();
                Portability.delete(stackNodes[i]);
            }
            numStackNodes = 0;
            stackNodesIdx = 0;
            stackNodes = null;
        }
        // [NOCPP[
        idLocations.clear();
        // ]NOCPP]

        if (!keepBuffer) {
            charBuffer = null;
        }
        end();
    }

    public final void startTag(ElementName elementName,
            HtmlAttributes attributes, boolean selfClosing) throws SAXException {
        flushCharacters();

        // [NOCPP[
        boolean wasSelfClosing = selfClosing;
        boolean voidElement = false;
        if (errorHandler != null) {
            // ID uniqueness
            @IdType String id = attributes.getId();
            if (id != null && !isTemplateContents()) {
                LocatorImpl oldLoc = idLocations.get(id);
                if (oldLoc != null) {
                    err("Duplicate ID \u201C" + id + "\u201D.");
                    errorHandler.warning(new SAXParseException(
                            "The first occurrence of ID \u201C" + id
                            + "\u201D was here.", oldLoc));
                } else {
                    idLocations.put(id, new LocatorImpl(tokenizer));
                }
            }
        }
        // ]NOCPP]

        int eltPos;
        needToDropLF = false;
        starttagloop: for (;;) {
            int group = elementName.getGroup();
            @Local String name = elementName.getName();
            if (isInForeign()) {
                StackNode<T> currentNode = stack[currentPtr];
                @NsUri String currNs = currentNode.ns;
                if (!(currentNode.isHtmlIntegrationPoint() || (currNs == "http://www.w3.org/1998/Math/MathML" && ((currentNode.getGroup() == MI_MO_MN_MS_MTEXT && group != MGLYPH_OR_MALIGNMARK) || (currentNode.getGroup() == ANNOTATION_XML && group == SVG))))) {
                    switch (group) {
                        case B_OR_BIG_OR_CODE_OR_EM_OR_I_OR_S_OR_SMALL_OR_STRIKE_OR_STRONG_OR_TT_OR_U:
                        case DIV_OR_BLOCKQUOTE_OR_CENTER_OR_MENU:
                        case BODY:
                        case BR:
                        case RUBY_OR_SPAN_OR_SUB_OR_SUP_OR_VAR:
                        case DD_OR_DT:
                        case UL_OR_OL_OR_DL:
                        case EMBED:
                        case IMG:
                        case H1_OR_H2_OR_H3_OR_H4_OR_H5_OR_H6:
                        case HEAD:
                        case HR:
                        case LI:
                        case META:
                        case NOBR:
                        case P:
                        case PRE_OR_LISTING:
                        case TABLE:
                        case FONT:
                            // re-check FONT to deal with the special case
                            if (!(group == FONT && !(attributes.contains(AttributeName.COLOR)
                                    || attributes.contains(AttributeName.FACE) || attributes.contains(AttributeName.SIZE)))) {
                                errHtmlStartTagInForeignContext(name);
                                if (!fragment) {
                                    while (!isSpecialParentInForeign(stack[currentPtr])) {
                                        popForeign(-1, -1);
                                    }
                                    continue starttagloop;
                                } // else fall thru
                            }
                            // CPPONLY: MOZ_FALLTHROUGH;
                        default:
                            if ("http://www.w3.org/2000/svg" == currNs) {
                                attributes.adjustForSvg();
                                if (selfClosing) {
                                    appendVoidElementToCurrentMayFosterSVG(
                                            elementName, attributes);
                                    selfClosing = false;
                                } else {
                                    appendToCurrentNodeAndPushElementMayFosterSVG(
                                            elementName, attributes);
                                }
                                attributes = null; // CPP
                                break starttagloop;
                            } else {
                                attributes.adjustForMath();
                                if (selfClosing) {
                                    appendVoidElementToCurrentMayFosterMathML(
                                            elementName, attributes);
                                    selfClosing = false;
                                } else {
                                    appendToCurrentNodeAndPushElementMayFosterMathML(
                                            elementName, attributes);
                                }
                                attributes = null; // CPP
                                break starttagloop;
                            }
                    } // switch
                } // foreignObject / annotation-xml
            }
            switch (mode) {
                case IN_TEMPLATE:
                    switch (group) {
                        case COL:
                            popTemplateMode();
                            pushTemplateMode(IN_COLUMN_GROUP);
                            mode = IN_COLUMN_GROUP;
                            // Reprocess token.
                            continue;
                        case CAPTION:
                        case COLGROUP:
                        case TBODY_OR_THEAD_OR_TFOOT:
                            popTemplateMode();
                            pushTemplateMode(IN_TABLE);
                            mode = IN_TABLE;
                            // Reprocess token.
                            continue;
                        case TR:
                            popTemplateMode();
                            pushTemplateMode(IN_TABLE_BODY);
                            mode = IN_TABLE_BODY;
                            // Reprocess token.
                            continue;
                        case TD_OR_TH:
                            popTemplateMode();
                            pushTemplateMode(IN_ROW);
                            mode = IN_ROW;
                            // Reprocess token.
                            continue;
                        case META:
                            checkMetaCharset(attributes);
                            appendVoidElementToCurrentMayFoster(
                                    elementName,
                                    attributes);
                            selfClosing = false;
                            // [NOCPP[
                            voidElement = true;
                            // ]NOCPP]
                            attributes = null; // CPP
                            break starttagloop;
                        case TITLE:
                            startTagTitleInHead(elementName, attributes);
                            attributes = null; // CPP
                            break starttagloop;
                        case BASE:
                        case LINK_OR_BASEFONT_OR_BGSOUND:
                            appendVoidElementToCurrentMayFoster(
                                    elementName,
                                    attributes);
                            selfClosing = false;
                            // [NOCPP[
                            voidElement = true;
                            // ]NOCPP]
                            attributes = null; // CPP
                            break starttagloop;
                        case SCRIPT:
                            startTagScriptInHead(elementName, attributes);
                            attributes = null; // CPP
                            break starttagloop;
                        case NOFRAMES:
                        case STYLE:
                            startTagGenericRawText(elementName, attributes);
                            attributes = null; // CPP
                            break starttagloop;
                        case TEMPLATE:
                            startTagTemplateInHead(elementName, attributes);
                            attributes = null; // CPP
                            break starttagloop;
                        default:
                            popTemplateMode();
                            pushTemplateMode(IN_BODY);
                            mode = IN_BODY;
                            // Reprocess token.
                            continue;
                    }
                case IN_ROW:
                    switch (group) {
                        case TD_OR_TH:
                            clearStackBackTo(findLastOrRoot(TreeBuilder.TR));
                            appendToCurrentNodeAndPushElement(
                                    elementName,
                                    attributes);
                            mode = IN_CELL;
                            insertMarker();
                            attributes = null; // CPP
                            break starttagloop;
                        case CAPTION:
                        case COL:
                        case COLGROUP:
                        case TBODY_OR_THEAD_OR_TFOOT:
                        case TR:
                            eltPos = findLastOrRoot(TreeBuilder.TR);
                            if (eltPos == 0) {
                                assert fragment || isTemplateContents();
                                errNoTableRowToClose();
                                break starttagloop;
                            }
                            clearStackBackTo(eltPos);
                            pop();
                            mode = IN_TABLE_BODY;
                            continue;
                        default:
                            // fall through to IN_TABLE
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case IN_TABLE_BODY:
                    switch (group) {
                        case TR:
                            clearStackBackTo(findLastInTableScopeOrRootTemplateTbodyTheadTfoot());
                            appendToCurrentNodeAndPushElement(
                                    elementName,
                                    attributes);
                            mode = IN_ROW;
                            attributes = null; // CPP
                            break starttagloop;
                        case TD_OR_TH:
                            errStartTagInTableBody(name);
                            clearStackBackTo(findLastInTableScopeOrRootTemplateTbodyTheadTfoot());
                            appendToCurrentNodeAndPushElement(
                                    ElementName.TR,
                                    HtmlAttributes.EMPTY_ATTRIBUTES);
                            mode = IN_ROW;
                            continue;
                        case CAPTION:
                        case COL:
                        case COLGROUP:
                        case TBODY_OR_THEAD_OR_TFOOT:
                            eltPos = findLastInTableScopeOrRootTemplateTbodyTheadTfoot();
                            if (eltPos == 0 || stack[eltPos].getGroup() == TEMPLATE) {
                                assert fragment || isTemplateContents();
                                errStrayStartTag(name);
                                break starttagloop;
                            } else {
                                clearStackBackTo(eltPos);
                                pop();
                                mode = IN_TABLE;
                                continue;
                            }
                        default:
                            // fall through to IN_TABLE
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case IN_TABLE:
                    intableloop: for (;;) {
                        switch (group) {
                            case CAPTION:
                                clearStackBackTo(findLastOrRoot(TreeBuilder.TABLE));
                                insertMarker();
                                appendToCurrentNodeAndPushElement(
                                        elementName,
                                        attributes);
                                mode = IN_CAPTION;
                                attributes = null; // CPP
                                break starttagloop;
                            case COLGROUP:
                                clearStackBackTo(findLastOrRoot(TreeBuilder.TABLE));
                                appendToCurrentNodeAndPushElement(
                                        elementName,
                                        attributes);
                                mode = IN_COLUMN_GROUP;
                                attributes = null; // CPP
                                break starttagloop;
                            case COL:
                                clearStackBackTo(findLastOrRoot(TreeBuilder.TABLE));
                                appendToCurrentNodeAndPushElement(
                                        ElementName.COLGROUP,
                                        HtmlAttributes.EMPTY_ATTRIBUTES);
                                mode = IN_COLUMN_GROUP;
                                continue starttagloop;
                            case TBODY_OR_THEAD_OR_TFOOT:
                                clearStackBackTo(findLastOrRoot(TreeBuilder.TABLE));
                                appendToCurrentNodeAndPushElement(
                                        elementName,
                                        attributes);
                                mode = IN_TABLE_BODY;
                                attributes = null; // CPP
                                break starttagloop;
                            case TR:
                            case TD_OR_TH:
                                clearStackBackTo(findLastOrRoot(TreeBuilder.TABLE));
                                appendToCurrentNodeAndPushElement(
                                        ElementName.TBODY,
                                        HtmlAttributes.EMPTY_ATTRIBUTES);
                                mode = IN_TABLE_BODY;
                                continue starttagloop;
                            case TEMPLATE:
                                // fall through to IN_HEAD
                                break intableloop;
                            case TABLE:
                                errTableSeenWhileTableOpen();
                                eltPos = findLastInTableScope(name);
                                if (eltPos == TreeBuilder.NOT_FOUND_ON_STACK) {
                                    assert fragment || isTemplateContents();
                                    break starttagloop;
                                }
                                generateImpliedEndTags();
                                if (errorHandler != null && !isCurrent("table")) {
                                    errNoCheckUnclosedElementsOnStack();
                                }
                                while (currentPtr >= eltPos) {
                                    pop();
                                }
                                resetTheInsertionMode();
                                continue starttagloop;
                            case SCRIPT:
                                // XXX need to manage much more stuff
                                // here if
                                // supporting
                                // document.write()
                                appendToCurrentNodeAndPushElement(
                                        elementName,
                                        attributes);
                                originalMode = mode;
                                mode = TEXT;
                                tokenizer.setStateAndEndTagExpectation(
                                        Tokenizer.SCRIPT_DATA, elementName);
                                attributes = null; // CPP
                                break starttagloop;
                            case STYLE:
                                appendToCurrentNodeAndPushElement(
                                        elementName,
                                        attributes);
                                originalMode = mode;
                                mode = TEXT;
                                tokenizer.setStateAndEndTagExpectation(
                                        Tokenizer.RAWTEXT, elementName);
                                attributes = null; // CPP
                                break starttagloop;
                            case INPUT:
                                errStartTagInTable(name);
                                if (!Portability.lowerCaseLiteralEqualsIgnoreAsciiCaseString(
                                        "hidden",
                                        attributes.getValue(AttributeName.TYPE))) {
                                    break intableloop;
                                }
                                appendVoidInputToCurrent(
                                        attributes,
                                        formPointer);
                                selfClosing = false;
                                // [NOCPP[
                                voidElement = true;
                                // ]NOCPP]
                                attributes = null; // CPP
                                break starttagloop;
                            case FORM:
                                if (formPointer != null || isTemplateContents()) {
                                    errFormWhenFormOpen();
                                    break starttagloop;
                                } else {
                                    errStartTagInTable(name);
                                    appendVoidFormToCurrent(attributes);
                                    attributes = null; // CPP
                                    break starttagloop;
                                }
                            default:
                                errStartTagInTable(name);
                                // fall through to IN_BODY
                                break intableloop;
                        }
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case IN_CAPTION:
                    switch (group) {
                        case CAPTION:
                        case COL:
                        case COLGROUP:
                        case TBODY_OR_THEAD_OR_TFOOT:
                        case TR:
                        case TD_OR_TH:
                            eltPos = findLastInTableScope("caption");
                            if (eltPos == TreeBuilder.NOT_FOUND_ON_STACK) {
                                assert fragment || isTemplateContents();
                                errStrayStartTag(name);
                                break starttagloop;
                            }
                            generateImpliedEndTags();
                            if (errorHandler != null && currentPtr != eltPos) {
                                errNoCheckUnclosedElementsOnStack();
                            }
                            while (currentPtr >= eltPos) {
                                pop();
                            }
                            clearTheListOfActiveFormattingElementsUpToTheLastMarker();
                            mode = IN_TABLE;
                            continue;
                        default:
                            // fall through to IN_BODY
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case IN_CELL:
                    switch (group) {
                        case CAPTION:
                        case COL:
                        case COLGROUP:
                        case TBODY_OR_THEAD_OR_TFOOT:
                        case TR:
                        case TD_OR_TH:
                            eltPos = findLastInTableScopeTdTh();
                            if (eltPos == TreeBuilder.NOT_FOUND_ON_STACK) {
                                errNoCellToClose();
                                break starttagloop;
                            } else {
                                closeTheCell(eltPos);
                                continue;
                            }
                        default:
                            // fall through to IN_BODY
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case FRAMESET_OK:
                    switch (group) {
                        case FRAMESET:
                            if (mode == FRAMESET_OK) {
                                if (currentPtr == 0 || stack[1].getGroup() != BODY) {
                                    assert fragment || isTemplateContents();
                                    errStrayStartTag(name);
                                    break starttagloop;
                                } else {
                                    errFramesetStart();
                                    detachFromParent(stack[1].node);
                                    while (currentPtr > 0) {
                                        pop();
                                    }
                                    appendToCurrentNodeAndPushElement(
                                            elementName,
                                            attributes);
                                    mode = IN_FRAMESET;
                                    attributes = null; // CPP
                                    break starttagloop;
                                }
                            } else {
                                errStrayStartTag(name);
                                break starttagloop;
                            }
                            // NOT falling through!
                        case PRE_OR_LISTING:
                        case LI:
                        case DD_OR_DT:
                        case BUTTON:
                        case MARQUEE_OR_APPLET:
                        case OBJECT:
                        case TABLE:
                        case AREA_OR_WBR:
                        case KEYGEN:
                        case BR:
                        case EMBED:
                        case IMG:
                        case INPUT:
                        case HR:
                        case TEXTAREA:
                        case XMP:
                        case IFRAME:
                        case SELECT:
                            if (mode == FRAMESET_OK
                                    && !(group == INPUT && Portability.lowerCaseLiteralEqualsIgnoreAsciiCaseString(
                                            "hidden",
                                            attributes.getValue(AttributeName.TYPE)))) {
                                framesetOk = false;
                                mode = IN_BODY;
                            }
                            // CPPONLY: MOZ_FALLTHROUGH;
                        default:
                            // fall through to IN_BODY
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case IN_BODY:
                    inbodyloop: for (;;) {
                        switch (group) {
                            case HTML:
                                errStrayStartTag(name);
                                if (!fragment && !isTemplateContents()) {
                                    addAttributesToHtml(attributes);
                                    attributes = null; // CPP
                                }
                                break starttagloop;
                            case BASE:
                            case LINK_OR_BASEFONT_OR_BGSOUND:
                            case META:
                            case STYLE:
                            case SCRIPT:
                            case TITLE:
                            case TEMPLATE:
                                // Fall through to IN_HEAD
                                break inbodyloop;
                            case BODY:
                                if (currentPtr == 0 || stack[1].getGroup() != BODY || isTemplateContents()) {
                                    assert fragment || isTemplateContents();
                                    errStrayStartTag(name);
                                    break starttagloop;
                                }
                                errFooSeenWhenFooOpen(name);
                                framesetOk = false;
                                if (mode == FRAMESET_OK) {
                                    mode = IN_BODY;
                                }
                                if (addAttributesToBody(attributes)) {
                                    attributes = null; // CPP
                                }
                                break starttagloop;
                            case P:
                            case DIV_OR_BLOCKQUOTE_OR_CENTER_OR_MENU:
                            case UL_OR_OL_OR_DL:
                            case ADDRESS_OR_ARTICLE_OR_ASIDE_OR_DETAILS_OR_DIALOG_OR_DIR_OR_FIGCAPTION_OR_FIGURE_OR_FOOTER_OR_HEADER_OR_HGROUP_OR_MAIN_OR_NAV_OR_SEARCH_OR_SECTION_OR_SUMMARY:
                                implicitlyCloseP();
                                appendToCurrentNodeAndPushElementMayFoster(
                                        elementName,
                                        attributes);
                                attributes = null; // CPP
                                break starttagloop;
                            case H1_OR_H2_OR_H3_OR_H4_OR_H5_OR_H6:
                                implicitlyCloseP();
                                if (stack[currentPtr].getGroup() == H1_OR_H2_OR_H3_OR_H4_OR_H5_OR_H6) {
                                    errHeadingWhenHeadingOpen();
                                    pop();
                                }
                                appendToCurrentNodeAndPushElementMayFoster(
                                        elementName,
                                        attributes);
                                attributes = null; // CPP
                                break starttagloop;
                            case FIELDSET:
                                implicitlyCloseP();
                                appendToCurrentNodeAndPushElementMayFoster(
                                        elementName,
                                        attributes, formPointer);
                                attributes = null; // CPP
                                break starttagloop;
                            case PRE_OR_LISTING:
                                implicitlyCloseP();
                                appendToCurrentNodeAndPushElementMayFoster(
                                        elementName,
                                        attributes);
                                needToDropLF = true;
                                attributes = null; // CPP
                                break starttagloop;
                            case FORM:
                                if (formPointer != null && !isTemplateContents()) {
                                    errFormWhenFormOpen();
                                    break starttagloop;
                                } else {
                                    implicitlyCloseP();
                                    appendToCurrentNodeAndPushFormElementMayFoster(attributes);
                                    attributes = null; // CPP
                                    break starttagloop;
                                }
                            case LI:
                            case DD_OR_DT:
                                eltPos = currentPtr;
                                for (;;) {
                                    StackNode<T> node = stack[eltPos]; // weak
                                    // ref
                                    if (node.getGroup() == group) { // LI or
                                        // DD_OR_DT
                                        generateImpliedEndTagsExceptFor(node.name);
                                        if (errorHandler != null
                                                && eltPos != currentPtr) {
                                            errUnclosedElementsImplied(eltPos, name);
                                        }
                                        while (currentPtr >= eltPos) {
                                            pop();
                                        }
                                        break;
                                    } else if (eltPos == 0 || (node.isSpecial()
                                            && (node.ns != "http://www.w3.org/1999/xhtml"
                                                    || (node.name != "p"
                                                            && node.name != "address"
                                                            && node.name != "div")))) {
                                        break;
                                    }
                                    eltPos--;
                                }
                                implicitlyCloseP();
                                appendToCurrentNodeAndPushElementMayFoster(
                                        elementName,
                                        attributes);
                                attributes = null; // CPP
                                break starttagloop;
                            case PLAINTEXT:
                                implicitlyCloseP();
                                appendToCurrentNodeAndPushElementMayFoster(
                                        elementName,
                                        attributes);
                                tokenizer.setStateAndEndTagExpectation(
                                        Tokenizer.PLAINTEXT, elementName);
                                attributes = null; // CPP
                                break starttagloop;
                            case A:
                                int activeAPos = findInListOfActiveFormattingElementsContainsBetweenEndAndLastMarker("a");
                                if (activeAPos != -1) {
                                    errFooSeenWhenFooOpen(name);
                                    StackNode<T> activeA = listOfActiveFormattingElements[activeAPos];
                                    activeA.retain();
                                    adoptionAgencyEndTag("a");
                                    removeFromStack(activeA);
                                    activeAPos = findInListOfActiveFormattingElements(activeA);
                                    if (activeAPos != -1) {
                                        removeFromListOfActiveFormattingElements(activeAPos);
                                    }
                                    activeA.release(this);
                                }
                                reconstructTheActiveFormattingElements();
                                appendToCurrentNodeAndPushFormattingElementMayFoster(
                                        elementName,
                                        attributes);
                                attributes = null; // CPP
                                break starttagloop;
                            case B_OR_BIG_OR_CODE_OR_EM_OR_I_OR_S_OR_SMALL_OR_STRIKE_OR_STRONG_OR_TT_OR_U:
                            case FONT:
                                reconstructTheActiveFormattingElements();
                                maybeForgetEarlierDuplicateFormattingElement(elementName.getName(), attributes);
                                appendToCurrentNodeAndPushFormattingElementMayFoster(
                                        elementName,
                                        attributes);
                                attributes = null; // CPP
                                break starttagloop;
                            case NOBR:
                                reconstructTheActiveFormattingElements();
                                if (TreeBuilder.NOT_FOUND_ON_STACK != findLastInScope("nobr")) {
                                    errFooSeenWhenFooOpen(name);
                                    adoptionAgencyEndTag("nobr");
                                    reconstructTheActiveFormattingElements();
                                }
                                appendToCurrentNodeAndPushFormattingElementMayFoster(
                                        elementName,
                                        attributes);
                                attributes = null; // CPP
                                break starttagloop;
                            case BUTTON:
                                eltPos = findLastInScope(name);
                                if (eltPos != TreeBuilder.NOT_FOUND_ON_STACK) {
                                    errFooSeenWhenFooOpen(name);
                                    generateImpliedEndTags();
                                    if (errorHandler != null
                                            && !isCurrent(name)) {
                                        errUnclosedElementsImplied(eltPos, name);
                                    }
                                    while (currentPtr >= eltPos) {
                                        pop();
                                    }
                                    continue starttagloop;
                                } else {
                                    reconstructTheActiveFormattingElements();
                                    appendToCurrentNodeAndPushElementMayFoster(
                                            elementName,
                                            attributes, formPointer);
                                    attributes = null; // CPP
                                    break starttagloop;
                                }
                            case OBJECT:
                                reconstructTheActiveFormattingElements();
                                appendToCurrentNodeAndPushElementMayFoster(
                                        elementName,
                                        attributes, formPointer);
                                insertMarker();
                                attributes = null; // CPP
                                break starttagloop;
                            case MARQUEE_OR_APPLET:
                                reconstructTheActiveFormattingElements();
                                appendToCurrentNodeAndPushElementMayFoster(
                                        elementName,
                                        attributes);
                                insertMarker();
                                attributes = null; // CPP
                                break starttagloop;
                            case TABLE:
                                // The only quirk. Blame Hixie and
                                // Acid2.
                                if (!quirks) {
                                    implicitlyCloseP();
                                }
                                appendToCurrentNodeAndPushElementMayFoster(
                                        elementName,
                                        attributes);
                                mode = IN_TABLE;
                                attributes = null; // CPP
                                break starttagloop;
                            case BR:
                            case EMBED:
                            case AREA_OR_WBR:
                            case KEYGEN:
                                reconstructTheActiveFormattingElements();
                                // FALL THROUGH to PARAM_OR_SOURCE_OR_TRACK
                                // CPPONLY: MOZ_FALLTHROUGH;
                            case PARAM_OR_SOURCE_OR_TRACK:
                                appendVoidElementToCurrentMayFoster(
                                        elementName,
                                        attributes);
                                selfClosing = false;
                                // [NOCPP[
                                voidElement = true;
                                // ]NOCPP]
                                attributes = null; // CPP
                                break starttagloop;
                            case HR:
                                implicitlyCloseP();
                                if (noInSelectMode
                                        && findLastInScope("select") != TreeBuilder.NOT_FOUND_ON_STACK) {
                                    generateImpliedEndTags();
                                    if (errorHandler != null
                                            && (findLastInScope("option") != TreeBuilder.NOT_FOUND_ON_STACK
                                            || findLastInScope("optgroup") != TreeBuilder.NOT_FOUND_ON_STACK)) {
                                        errUnclosedElements(currentPtr, name);
                                    }
                                }
                                appendVoidElementToCurrentMayFoster(
                                        elementName,
                                        attributes);
                                selfClosing = false;
                                // [NOCPP[
                                voidElement = true;
                                // ]NOCPP]
                                attributes = null; // CPP
                                break starttagloop;
                            case IMAGE:
                                errImage();
                                elementName = ElementName.IMG;
                                continue starttagloop;
                            case INPUT:
                                if (noInSelectMode) {
                                    if (fragment && "select" == contextName) {
                                        errStartTagWithSelectOpen(name);
                                        break starttagloop;
                                    }
                                    eltPos = findLastInScope("select");
                                    if (eltPos != TreeBuilder.NOT_FOUND_ON_STACK) {
                                        errStartTagWithSelectOpen(name);
                                        while (currentPtr >= eltPos) {
                                            pop();
                                        }
                                        resetTheInsertionMode();
                                        continue starttagloop;
                                    }
                                }
                                // CPPONLY: MOZ_FALLTHROUGH;
                            case IMG:
                                reconstructTheActiveFormattingElements();
                                appendVoidElementToCurrentMayFoster(
                                        elementName, attributes,
                                        formPointer);
                                selfClosing = false;
                                // [NOCPP[
                                voidElement = true;
                                // ]NOCPP]
                                attributes = null; // CPP
                                break starttagloop;
                            case TEXTAREA:
                                appendToCurrentNodeAndPushElementMayFoster(
                                        elementName,
                                        attributes, formPointer);
                                tokenizer.setStateAndEndTagExpectation(
                                        Tokenizer.RCDATA, elementName);
                                originalMode = mode;
                                mode = TEXT;
                                needToDropLF = true;
                                attributes = null; // CPP
                                break starttagloop;
                            case XMP:
                                implicitlyCloseP();
                                reconstructTheActiveFormattingElements();
                                appendToCurrentNodeAndPushElementMayFoster(
                                        elementName,
                                        attributes);
                                originalMode = mode;
                                mode = TEXT;
                                tokenizer.setStateAndEndTagExpectation(
                                        Tokenizer.RAWTEXT, elementName);
                                attributes = null; // CPP
                                break starttagloop;
                            case NOSCRIPT:
                                if (!scriptingEnabled) {
                                    reconstructTheActiveFormattingElements();
                                    appendToCurrentNodeAndPushElementMayFoster(
                                            elementName,
                                            attributes);
                                    attributes = null; // CPP
                                    break starttagloop;
                                }
                                // CPPONLY: MOZ_FALLTHROUGH;
                            case NOFRAMES:
                            case IFRAME:
                            case NOEMBED:
                                startTagGenericRawText(elementName, attributes);
                                attributes = null; // CPP
                                break starttagloop;
                            case SELECT:
                                if (noInSelectMode) {
                                    if (fragment && "select" == contextName) {
                                        errStartSelectWhereEndSelectExpected();
                                        break starttagloop;
                                    }
                                    eltPos = findLastInScope("select");
                                    if (eltPos != TreeBuilder.NOT_FOUND_ON_STACK) {
                                        errStartSelectWhereEndSelectExpected();
                                        while (currentPtr >= eltPos) {
                                            pop();
                                        }
                                        break starttagloop;
                                    }
                                    reconstructTheActiveFormattingElements();
                                    appendToCurrentNodeAndPushElementMayFoster(
                                            elementName,
                                            attributes, formPointer);
                                    framesetOk = false;
                                    attributes = null; // CPP
                                    break starttagloop;
                                }
                                reconstructTheActiveFormattingElements();
                                appendToCurrentNodeAndPushElementMayFoster(
                                        elementName,
                                        attributes, formPointer);
                                if (!noInSelectMode) {
                                    switch (mode) {
                                        case IN_TABLE:
                                        case IN_CAPTION:
                                        case IN_COLUMN_GROUP:
                                        case IN_TABLE_BODY:
                                        case IN_ROW:
                                        case IN_CELL:
                                            mode = IN_SELECT_IN_TABLE;
                                            break;
                                        default:
                                            mode = IN_SELECT;
                                            break;
                                    }
                                }
                                attributes = null; // CPP
                                break starttagloop;
                            case OPTION:
                                if (noInSelectMode) {
                                    if (findLastInScope("select") != TreeBuilder.NOT_FOUND_ON_STACK) {
                                        generateImpliedEndTagsExceptFor("optgroup");
                                        if (errorHandler != null
                                            && findLastInScope("option") != TreeBuilder.NOT_FOUND_ON_STACK) {
                                            errUnclosedElements(findLastInScope("option"), name);
                                        }
                                    } else {
                                        if (isCurrent("option")) {
                                            pop();
                                        }
                                    }
                                    reconstructTheActiveFormattingElements();
                                    appendToCurrentNodeAndPushElement(
                                            elementName,
                                            attributes);
                                    attributes = null; // CPP
                                    break starttagloop;
                                }
                                // CPPONLY: MOZ_FALLTHROUGH;
                            case OPTGROUP:
                                if (noInSelectMode) {
                                    if (findLastInScope("select") != TreeBuilder.NOT_FOUND_ON_STACK) {
                                        generateImpliedEndTags();
                                        if (errorHandler != null
                                            && (findLastInScope("option") != TreeBuilder.NOT_FOUND_ON_STACK
                                                || findLastInScope("optgroup") != TreeBuilder.NOT_FOUND_ON_STACK)) {
                                            errUnclosedElements(currentPtr, name);
                                        }
                                    } else if (isCurrent("option")) {
                                        pop();
                                    }
                                    reconstructTheActiveFormattingElements();
                                    appendToCurrentNodeAndPushElement(
                                            elementName,
                                            attributes);
                                    attributes = null; // CPP
                                    break starttagloop;
                                }
                                if (isCurrent("option")) {
                                    pop();
                                }
                                reconstructTheActiveFormattingElements();
                                appendToCurrentNodeAndPushElementMayFoster(
                                        elementName,
                                        attributes);
                                attributes = null; // CPP
                                break starttagloop;
                            case RB_OR_RTC:
                                eltPos = findLastInScope("ruby");
                                if (eltPos != NOT_FOUND_ON_STACK) {
                                    generateImpliedEndTags();
                                }
                                if (eltPos != currentPtr) {
                                    if (eltPos == NOT_FOUND_ON_STACK) {
                                        errStartTagSeenWithoutRuby(name);
                                    } else {
                                        errUnclosedChildrenInRuby();
                                    }
                                }
                                appendToCurrentNodeAndPushElementMayFoster(
                                        elementName,
                                        attributes);
                                attributes = null; // CPP
                                break starttagloop;
                            case RT_OR_RP:
                                eltPos = findLastInScope("ruby");
                                if (eltPos != NOT_FOUND_ON_STACK) {
                                    generateImpliedEndTagsExceptFor("rtc");
                                }
                                if (eltPos != currentPtr) {
                                    if (!isCurrent("rtc")) {
                                        if (eltPos == NOT_FOUND_ON_STACK) {
                                            errStartTagSeenWithoutRuby(name);
                                        } else {
                                            errUnclosedChildrenInRuby();
                                        }
                                    }
                                }
                                appendToCurrentNodeAndPushElementMayFoster(
                                        elementName,
                                        attributes);
                                attributes = null; // CPP
                                break starttagloop;
                            case MATH:
                                reconstructTheActiveFormattingElements();
                                attributes.adjustForMath();
                                if (selfClosing) {
                                    appendVoidElementToCurrentMayFosterMathML(
                                            elementName, attributes);
                                    selfClosing = false;
                                } else {
                                    appendToCurrentNodeAndPushElementMayFosterMathML(
                                            elementName, attributes);
                                }
                                attributes = null; // CPP
                                break starttagloop;
                            case SVG:
                                reconstructTheActiveFormattingElements();
                                attributes.adjustForSvg();
                                if (selfClosing) {
                                    appendVoidElementToCurrentMayFosterSVG(
                                            elementName,
                                            attributes);
                                    selfClosing = false;
                                } else {
                                    appendToCurrentNodeAndPushElementMayFosterSVG(
                                            elementName, attributes);
                                }
                                attributes = null; // CPP
                                break starttagloop;
                            case CAPTION:
                            case COL:
                            case COLGROUP:
                            case TBODY_OR_THEAD_OR_TFOOT:
                            case TR:
                            case TD_OR_TH:
                            case FRAME:
                            case FRAMESET:
                            case HEAD:
                                errStrayStartTag(name);
                                break starttagloop;
                            case OUTPUT:
                                reconstructTheActiveFormattingElements();
                                appendToCurrentNodeAndPushElementMayFoster(
                                        elementName,
                                        attributes, formPointer);
                                attributes = null; // CPP
                                break starttagloop;
                            default:
                                reconstructTheActiveFormattingElements();
                                appendToCurrentNodeAndPushElementMayFoster(
                                        elementName,
                                        attributes);
                                attributes = null; // CPP
                                break starttagloop;
                        }
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case IN_HEAD:
                    inheadloop: for (;;) {
                        switch (group) {
                            case HTML:
                                errStrayStartTag(name);
                                if (!fragment && !isTemplateContents()) {
                                    addAttributesToHtml(attributes);
                                    attributes = null; // CPP
                                }
                                break starttagloop;
                            case BASE:
                            case LINK_OR_BASEFONT_OR_BGSOUND:
                                appendVoidElementToCurrentMayFoster(
                                        elementName,
                                        attributes);
                                selfClosing = false;
                                // [NOCPP[
                                voidElement = true;
                                // ]NOCPP]
                                attributes = null; // CPP
                                break starttagloop;
                            case META:
                                // Fall through to IN_HEAD_NOSCRIPT
                                break inheadloop;
                            case TITLE:
                                startTagTitleInHead(elementName, attributes);
                                attributes = null; // CPP
                                break starttagloop;
                            case NOSCRIPT:
                                if (scriptingEnabled) {
                                    appendToCurrentNodeAndPushElement(
                                            elementName,
                                            attributes);
                                    originalMode = mode;
                                    mode = TEXT;
                                    tokenizer.setStateAndEndTagExpectation(
                                            Tokenizer.RAWTEXT, elementName);
                                } else {
                                    appendToCurrentNodeAndPushElementMayFoster(
                                            elementName,
                                            attributes);
                                    mode = IN_HEAD_NOSCRIPT;
                                }
                                attributes = null; // CPP
                                break starttagloop;
                            case SCRIPT:
                                startTagScriptInHead(elementName, attributes);
                                attributes = null; // CPP
                                break starttagloop;
                            case STYLE:
                            case NOFRAMES:
                                startTagGenericRawText(elementName, attributes);
                                attributes = null; // CPP
                                break starttagloop;
                            case HEAD:
                                /* Parse error. */
                                errFooSeenWhenFooOpen(name);
                                /* Ignore the token. */
                                break starttagloop;
                            case TEMPLATE:
                                startTagTemplateInHead(elementName, attributes);
                                attributes = null; // CPP
                                break starttagloop;
                            default:
                                pop();
                                mode = AFTER_HEAD;
                                continue starttagloop;
                        }
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case IN_HEAD_NOSCRIPT:
                    switch (group) {
                        case HTML:
                            // XXX did Hixie really mean to omit "base"
                            // here?
                            errStrayStartTag(name);
                            if (!fragment && !isTemplateContents()) {
                                addAttributesToHtml(attributes);
                                attributes = null; // CPP
                            }
                            break starttagloop;
                        case LINK_OR_BASEFONT_OR_BGSOUND:
                            appendVoidElementToCurrentMayFoster(
                                    elementName,
                                    attributes);
                            selfClosing = false;
                            // [NOCPP[
                            voidElement = true;
                            // ]NOCPP]
                            attributes = null; // CPP
                            break starttagloop;
                        case META:
                            checkMetaCharset(attributes);
                            appendVoidElementToCurrentMayFoster(
                                    elementName,
                                    attributes);
                            selfClosing = false;
                            // [NOCPP[
                            voidElement = true;
                            // ]NOCPP]
                            attributes = null; // CPP
                            break starttagloop;
                        case STYLE:
                        case NOFRAMES:
                            appendToCurrentNodeAndPushElement(
                                    elementName,
                                    attributes);
                            originalMode = mode;
                            mode = TEXT;
                            tokenizer.setStateAndEndTagExpectation(
                                    Tokenizer.RAWTEXT, elementName);
                            attributes = null; // CPP
                            break starttagloop;
                        case HEAD:
                            errFooSeenWhenFooOpen(name);
                            break starttagloop;
                        case NOSCRIPT:
                            errFooSeenWhenFooOpen(name);
                            break starttagloop;
                        default:
                            errBadStartTagInNoscriptInHead(name);
                            pop();
                            mode = IN_HEAD;
                            continue;
                    }
                case IN_COLUMN_GROUP:
                    switch (group) {
                        case HTML:
                            errStrayStartTag(name);
                            if (!fragment && !isTemplateContents()) {
                                addAttributesToHtml(attributes);
                                attributes = null; // CPP
                            }
                            break starttagloop;
                        case COL:
                            appendVoidElementToCurrentMayFoster(
                                    elementName,
                                    attributes);
                            selfClosing = false;
                            // [NOCPP[
                            voidElement = true;
                            // ]NOCPP]
                            attributes = null; // CPP
                            break starttagloop;
                        case TEMPLATE:
                            startTagTemplateInHead(elementName, attributes);
                            attributes = null; // CPP
                            break starttagloop;
                        default:
                            if (currentPtr == 0 || stack[currentPtr].getGroup() == TEMPLATE) {
                                assert fragment || isTemplateContents();
                                errGarbageInColgroup();
                                break starttagloop;
                            }
                            pop();
                            mode = IN_TABLE;
                            continue;
                    }
                case IN_SELECT_IN_TABLE:
                    // CPPONLY: MOZ_ASSERT(!noInSelectMode);
                    switch (group) {
                        case CAPTION:
                        case TBODY_OR_THEAD_OR_TFOOT:
                        case TR:
                        case TD_OR_TH:
                        case TABLE:
                            errStartTagWithSelectOpen(name);
                            eltPos = findLastInTableScope("select");
                            if (eltPos == TreeBuilder.NOT_FOUND_ON_STACK) {
                                assert fragment;
                                break starttagloop; // http://www.w3.org/Bugs/Public/show_bug.cgi?id=8375
                            }
                            while (currentPtr >= eltPos) {
                                pop();
                            }
                            resetTheInsertionMode();
                            continue;
                        default:
                            // fall through to IN_SELECT
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case IN_SELECT:
                    // CPPONLY: MOZ_ASSERT(!noInSelectMode);
                    switch (group) {
                        case HTML:
                            errStrayStartTag(name);
                            if (!fragment) {
                                addAttributesToHtml(attributes);
                                attributes = null; // CPP
                            }
                            break starttagloop;
                        case OPTION:
                            if (isCurrent("option")) {
                                pop();
                            }
                            appendToCurrentNodeAndPushElement(
                                    elementName,
                                    attributes);
                            attributes = null; // CPP
                            break starttagloop;
                        case OPTGROUP:
                            if (isCurrent("option")) {
                                pop();
                            }
                            if (isCurrent("optgroup")) {
                                pop();
                            }
                            appendToCurrentNodeAndPushElement(
                                    elementName,
                                    attributes);
                            attributes = null; // CPP
                            break starttagloop;
                        case SELECT:
                            errStartSelectWhereEndSelectExpected();
                            eltPos = findLastInTableScope(name);
                            if (eltPos == TreeBuilder.NOT_FOUND_ON_STACK) {
                                assert fragment;
                                errNoSelectInTableScope();
                                break starttagloop;
                            } else {
                                while (currentPtr >= eltPos) {
                                    pop();
                                }
                                resetTheInsertionMode();
                                break starttagloop;
                            }
                        case INPUT:
                        case TEXTAREA:
                            errStartTagWithSelectOpen(name);
                            eltPos = findLastInTableScope("select");
                            if (eltPos == TreeBuilder.NOT_FOUND_ON_STACK) {
                                assert fragment;
                                break starttagloop;
                            }
                            while (currentPtr >= eltPos) {
                                pop();
                            }
                            resetTheInsertionMode();
                            continue;
                        case SCRIPT:
                            startTagScriptInHead(elementName, attributes);
                            attributes = null; // CPP
                            break starttagloop;
                        case TEMPLATE:
                            startTagTemplateInHead(elementName, attributes);
                            attributes = null; // CPP
                            break starttagloop;
                        case HR:
                            if (isCurrent("option")) {
                                pop();
                            }
                            if (isCurrent("optgroup")) {
                                pop();
                            }
                            appendVoidElementToCurrent(elementName, attributes);
                            selfClosing = false;
                            // [NOCPP[
                            voidElement = true;
                            // ]NOCPP]
                            attributes = null; // CPP
                            break starttagloop;
                        default:
                            errStrayStartTag(name);
                            break starttagloop;
                    }
                case AFTER_BODY:
                    switch (group) {
                        case HTML:
                            errStrayStartTag(name);
                            if (!fragment && !isTemplateContents()) {
                                addAttributesToHtml(attributes);
                                attributes = null; // CPP
                            }
                            break starttagloop;
                        default:
                            errStrayStartTag(name);
                            mode = framesetOk ? FRAMESET_OK : IN_BODY;
                            continue;
                    }
                case IN_FRAMESET:
                    switch (group) {
                        case FRAMESET:
                            appendToCurrentNodeAndPushElement(
                                    elementName,
                                    attributes);
                            attributes = null; // CPP
                            break starttagloop;
                        case FRAME:
                            appendVoidElementToCurrentMayFoster(
                                    elementName,
                                    attributes);
                            selfClosing = false;
                            // [NOCPP[
                            voidElement = true;
                            // ]NOCPP]
                            attributes = null; // CPP
                            break starttagloop;
                        default:
                            // fall through to AFTER_FRAMESET
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case AFTER_FRAMESET:
                    switch (group) {
                        case HTML:
                            errStrayStartTag(name);
                            if (!fragment && !isTemplateContents()) {
                                addAttributesToHtml(attributes);
                                attributes = null; // CPP
                            }
                            break starttagloop;
                        case NOFRAMES:
                            appendToCurrentNodeAndPushElement(
                                    elementName,
                                    attributes);
                            originalMode = mode;
                            mode = TEXT;
                            tokenizer.setStateAndEndTagExpectation(
                                    Tokenizer.RAWTEXT, elementName);
                            attributes = null; // CPP
                            break starttagloop;
                        default:
                            errStrayStartTag(name);
                            break starttagloop;
                    }
                case INITIAL:
                    /*
                     * Parse error.
                     */
                    errStartTagWithoutDoctype();
                    /*
                     *
                     * Set the document to quirks mode.
                     */
                    documentModeInternal(DocumentMode.QUIRKS_MODE, null, null);
                    /*
                     * Then, switch to the root element mode of the tree
                     * construction stage
                     */
                    mode = BEFORE_HTML;
                    /*
                     * and reprocess the current token.
                     */
                    continue;
                case BEFORE_HTML:
                    switch (group) {
                        case HTML:
                            // optimize error check and streaming SAX by
                            // hoisting
                            // "html" handling here.
                            if (attributes == HtmlAttributes.EMPTY_ATTRIBUTES) {
                                // This has the right magic side effect
                                // that
                                // it
                                // makes attributes in SAX Tree mutable.
                                appendHtmlElementToDocumentAndPush();
                            } else {
                                appendHtmlElementToDocumentAndPush(attributes);
                            }
                            // XXX application cache should fire here
                            mode = BEFORE_HEAD;
                            attributes = null; // CPP
                            break starttagloop;
                        default:
                            /*
                             * Create an HTMLElement node with the tag name
                             * html, in the HTML namespace. Append it to the
                             * Document object.
                             */
                            appendHtmlElementToDocumentAndPush();
                            /* Switch to the main mode */
                            mode = BEFORE_HEAD;
                            /*
                             * reprocess the current token.
                             */
                            continue;
                    }
                case BEFORE_HEAD:
                    switch (group) {
                        case HTML:
                            errStrayStartTag(name);
                            if (!fragment && !isTemplateContents()) {
                                addAttributesToHtml(attributes);
                                attributes = null; // CPP
                            }
                            break starttagloop;
                        case HEAD:
                            /*
                             * A start tag whose tag name is "head"
                             *
                             * Create an element for the token.
                             *
                             * Set the head element pointer to this new element
                             * node.
                             *
                             * Append the new element to the current node and
                             * push it onto the stack of open elements.
                             */
                            appendToCurrentNodeAndPushHeadElement(attributes);
                            /*
                             * Change the insertion mode to "in head".
                             */
                            mode = IN_HEAD;
                            attributes = null; // CPP
                            break starttagloop;
                        default:
                            /*
                             * Any other start tag token
                             *
                             * Act as if a start tag token with the tag name
                             * "head" and no attributes had been seen,
                             */
                            appendToCurrentNodeAndPushHeadElement(HtmlAttributes.EMPTY_ATTRIBUTES);
                            mode = IN_HEAD;
                            /*
                             * then reprocess the current token.
                             *
                             * This will result in an empty head element being
                             * generated, with the current token being
                             * reprocessed in the "after head" insertion mode.
                             */
                            continue;
                    }
                case AFTER_HEAD:
                    switch (group) {
                        case HTML:
                            errStrayStartTag(name);
                            if (!fragment && !isTemplateContents()) {
                                addAttributesToHtml(attributes);
                                attributes = null; // CPP
                            }
                            break starttagloop;
                        case BODY:
                            if (attributes.getLength() == 0) {
                                // This has the right magic side effect
                                // that
                                // it
                                // makes attributes in SAX Tree mutable.
                                appendToCurrentNodeAndPushBodyElement();
                            } else {
                                appendToCurrentNodeAndPushBodyElement(attributes);
                            }
                            framesetOk = false;
                            mode = IN_BODY;
                            attributes = null; // CPP
                            break starttagloop;
                        case FRAMESET:
                            appendToCurrentNodeAndPushElement(
                                    elementName,
                                    attributes);
                            mode = IN_FRAMESET;
                            attributes = null; // CPP
                            break starttagloop;
                        case TEMPLATE:
                            errFooBetweenHeadAndBody(name);
                            pushHeadPointerOntoStack();
                            StackNode<T> headOnStack = stack[currentPtr];
                            startTagTemplateInHead(elementName, attributes);
                            removeFromStack(headOnStack);
                            attributes = null; // CPP
                            break starttagloop;
                        case BASE:
                        case LINK_OR_BASEFONT_OR_BGSOUND:
                            errFooBetweenHeadAndBody(name);
                            pushHeadPointerOntoStack();
                            appendVoidElementToCurrentMayFoster(
                                    elementName,
                                    attributes);
                            selfClosing = false;
                            // [NOCPP[
                            voidElement = true;
                            // ]NOCPP]
                            pop(); // head
                            attributes = null; // CPP
                            break starttagloop;
                        case META:
                            errFooBetweenHeadAndBody(name);
                            checkMetaCharset(attributes);
                            pushHeadPointerOntoStack();
                            appendVoidElementToCurrentMayFoster(
                                    elementName,
                                    attributes);
                            selfClosing = false;
                            // [NOCPP[
                            voidElement = true;
                            // ]NOCPP]
                            pop(); // head
                            attributes = null; // CPP
                            break starttagloop;
                        case SCRIPT:
                            errFooBetweenHeadAndBody(name);
                            pushHeadPointerOntoStack();
                            appendToCurrentNodeAndPushElement(
                                    elementName,
                                    attributes);
                            originalMode = mode;
                            mode = TEXT;
                            tokenizer.setStateAndEndTagExpectation(
                                    Tokenizer.SCRIPT_DATA, elementName);
                            attributes = null; // CPP
                            break starttagloop;
                        case STYLE:
                        case NOFRAMES:
                            errFooBetweenHeadAndBody(name);
                            pushHeadPointerOntoStack();
                            appendToCurrentNodeAndPushElement(
                                    elementName,
                                    attributes);
                            originalMode = mode;
                            mode = TEXT;
                            tokenizer.setStateAndEndTagExpectation(
                                    Tokenizer.RAWTEXT, elementName);
                            attributes = null; // CPP
                            break starttagloop;
                        case TITLE:
                            errFooBetweenHeadAndBody(name);
                            pushHeadPointerOntoStack();
                            appendToCurrentNodeAndPushElement(
                                    elementName,
                                    attributes);
                            originalMode = mode;
                            mode = TEXT;
                            tokenizer.setStateAndEndTagExpectation(
                                    Tokenizer.RCDATA, elementName);
                            attributes = null; // CPP
                            break starttagloop;
                        case HEAD:
                            errStrayStartTag(name);
                            break starttagloop;
                        default:
                            appendToCurrentNodeAndPushBodyElement();
                            mode = FRAMESET_OK;
                            continue;
                    }
                case AFTER_AFTER_BODY:
                    switch (group) {
                        case HTML:
                            errStrayStartTag(name);
                            if (!fragment && !isTemplateContents()) {
                                addAttributesToHtml(attributes);
                                attributes = null; // CPP
                            }
                            break starttagloop;
                        default:
                            errStrayStartTag(name);
                            fatal();
                            mode = framesetOk ? FRAMESET_OK : IN_BODY;
                            continue;
                    }
                case AFTER_AFTER_FRAMESET:
                    switch (group) {
                        case HTML:
                            errStrayStartTag(name);
                            if (!fragment && !isTemplateContents()) {
                                addAttributesToHtml(attributes);
                                attributes = null; // CPP
                            }
                            break starttagloop;
                        case NOFRAMES:
                            startTagGenericRawText(elementName, attributes);
                            attributes = null; // CPP
                            break starttagloop;
                        default:
                            errStrayStartTag(name);
                            break starttagloop;
                    }
                case TEXT:
                    assert false;
                    break starttagloop; // Avoid infinite loop if the assertion
                                        // fails
            }
        }
        if (selfClosing) {
            errSelfClosing();
        // [NOCPP[
        } else if (wasSelfClosing && voidElement
                && tokenizer.getErrorProfile() != null
                && tokenizer.getErrorProfile().get("html-strict") != null) {
            warn("Trailing slash on void elements has no effect and interacts"
                    + " badly with unquoted attribute values.");
        // ]NOCPP]
        }
        // CPPONLY: if (mBuilder == null && attributes != HtmlAttributes.EMPTY_ATTRIBUTES) {
        // CPPONLY:    Portability.delete(attributes);
        // CPPONLY: }
    }

    private void startTagTitleInHead(ElementName elementName, HtmlAttributes attributes) throws SAXException {
        appendToCurrentNodeAndPushElementMayFoster(elementName, attributes);
        originalMode = mode;
        mode = TEXT;
        tokenizer.setStateAndEndTagExpectation(Tokenizer.RCDATA, elementName);
    }

    private void startTagGenericRawText(ElementName elementName, HtmlAttributes attributes) throws SAXException {
        appendToCurrentNodeAndPushElementMayFoster(elementName, attributes);
        originalMode = mode;
        mode = TEXT;
        tokenizer.setStateAndEndTagExpectation(Tokenizer.RAWTEXT, elementName);
    }

    private void startTagScriptInHead(ElementName elementName, HtmlAttributes attributes) throws SAXException {
        // XXX need to manage much more stuff here if supporting document.write()
        appendToCurrentNodeAndPushElementMayFoster(elementName, attributes);
        originalMode = mode;
        mode = TEXT;
        tokenizer.setStateAndEndTagExpectation(Tokenizer.SCRIPT_DATA, elementName);
    }

    private void startTagTemplateInHead(ElementName elementName, HtmlAttributes attributes) throws SAXException {
        appendToCurrentNodeAndPushElement(elementName, attributes);
        insertMarker();
        framesetOk = false;
        originalMode = mode;
        mode = IN_TEMPLATE;
        pushTemplateMode(IN_TEMPLATE);
    }

    private boolean isTemplateContents() {
        return TreeBuilder.NOT_FOUND_ON_STACK != findLast("template");
    }

    private boolean isTemplateModeStackEmpty() {
        return templateModePtr == -1;
    }

    private boolean isSpecialParentInForeign(StackNode<T> stackNode) {
        @NsUri String ns = stackNode.ns;
        return ("http://www.w3.org/1999/xhtml" == ns)
                || (stackNode.isHtmlIntegrationPoint())
                || (("http://www.w3.org/1998/Math/MathML" == ns) && (stackNode.getGroup() == MI_MO_MN_MS_MTEXT));
    }

    private T getDeclarativeShadowRoot(T currentNode, T templateNode, HtmlAttributes attributes) {
        if (!isAllowDeclarativeShadowRoots()) {
            return null;
        }

        String shadowRootMode = attributes.getValue(AttributeName.SHADOWROOTMODE);
        if (shadowRootMode == null) {
            return null;
        }

        boolean shadowRootIsClonable = attributes.contains(AttributeName.SHADOWROOTCLONABLE);
        boolean shadowRootIsSerializable = attributes.contains(AttributeName.SHADOWROOTSERIALIZABLE);
        boolean shadowRootDelegatesFocus = attributes.contains(AttributeName.SHADOWROOTDELEGATESFOCUS);
        boolean shadowRootCustomElementRegistry = attributes.contains(AttributeName.SHADOWROOTCUSTOMELEMENTREGISTRY);
        String shadowRootReferenceTarget = attributes.getValue(AttributeName.SHADOWROOTREFERENCETARGET);
        String shadowRootSlotAssignment = attributes.getValue(AttributeName.SHADOWROOTSLOTASSIGNMENT);

        return getShadowRootFromHost(currentNode, templateNode, shadowRootMode, shadowRootIsClonable, shadowRootIsSerializable, shadowRootDelegatesFocus, shadowRootCustomElementRegistry, shadowRootSlotAssignment, shadowRootReferenceTarget);
    }

    /**
     *
     * <p>
     * C++ memory note: The return value must be released.
     *
     * @return
     * @throws SAXException
     * @throws StopSniffingException
     */
    public static String extractCharsetFromContent(String attributeValue
        // CPPONLY: , TreeBuilder tb
    ) {
        // This is a bit ugly. Converting the string to char array in order to
        // make the portability layer smaller.
        int charsetState = CHARSET_INITIAL;
        int start = -1;
        int end = -1;
        @Auto char[] buffer = Portability.newCharArrayFromString(attributeValue);

        charsetloop: for (int i = 0; i < buffer.length; i++) {
            char c = buffer[i];
            switch (charsetState) {
                case CHARSET_INITIAL:
                    switch (c) {
                        case 'c':
                        case 'C':
                            charsetState = CHARSET_C;
                            continue;
                        default:
                            continue;
                    }
                case CHARSET_C:
                    switch (c) {
                        case 'h':
                        case 'H':
                            charsetState = CHARSET_H;
                            continue;
                        default:
                            charsetState = CHARSET_INITIAL;
                            continue;
                    }
                case CHARSET_H:
                    switch (c) {
                        case 'a':
                        case 'A':
                            charsetState = CHARSET_A;
                            continue;
                        default:
                            charsetState = CHARSET_INITIAL;
                            continue;
                    }
                case CHARSET_A:
                    switch (c) {
                        case 'r':
                        case 'R':
                            charsetState = CHARSET_R;
                            continue;
                        default:
                            charsetState = CHARSET_INITIAL;
                            continue;
                    }
                case CHARSET_R:
                    switch (c) {
                        case 's':
                        case 'S':
                            charsetState = CHARSET_S;
                            continue;
                        default:
                            charsetState = CHARSET_INITIAL;
                            continue;
                    }
                case CHARSET_S:
                    switch (c) {
                        case 'e':
                        case 'E':
                            charsetState = CHARSET_E;
                            continue;
                        default:
                            charsetState = CHARSET_INITIAL;
                            continue;
                    }
                case CHARSET_E:
                    switch (c) {
                        case 't':
                        case 'T':
                            charsetState = CHARSET_T;
                            continue;
                        default:
                            charsetState = CHARSET_INITIAL;
                            continue;
                    }
                case CHARSET_T:
                    switch (c) {
                        case '\t':
                        case '\n':
                        case '\u000C':
                        case '\r':
                        case ' ':
                            continue;
                        case '=':
                            charsetState = CHARSET_EQUALS;
                            continue;
                        default:
                            return null;
                    }
                case CHARSET_EQUALS:
                    switch (c) {
                        case '\t':
                        case '\n':
                        case '\u000C':
                        case '\r':
                        case ' ':
                            continue;
                        case '\'':
                            start = i + 1;
                            charsetState = CHARSET_SINGLE_QUOTED;
                            continue;
                        case '\"':
                            start = i + 1;
                            charsetState = CHARSET_DOUBLE_QUOTED;
                            continue;
                        default:
                            start = i;
                            charsetState = CHARSET_UNQUOTED;
                            continue;
                    }
                case CHARSET_SINGLE_QUOTED:
                    switch (c) {
                        case '\'':
                            end = i;
                            break charsetloop;
                        default:
                            continue;
                    }
                case CHARSET_DOUBLE_QUOTED:
                    switch (c) {
                        case '\"':
                            end = i;
                            break charsetloop;
                        default:
                            continue;
                    }
                case CHARSET_UNQUOTED:
                    switch (c) {
                        case '\t':
                        case '\n':
                        case '\u000C':
                        case '\r':
                        case ' ':
                        case ';':
                            end = i;
                            break charsetloop;
                        default:
                            continue;
                    }
            }
        }
        if (start != -1) {
            if (end == -1) {
                if (charsetState == CHARSET_UNQUOTED) {
                    end = buffer.length;
                } else {
                    return null;
                }
            }
            return Portability.newStringFromBuffer(buffer, start, end
                    - start
                // CPPONLY: , tb, false
            );
        }
        return null;
    }

    private void checkMetaCharset(HtmlAttributes attributes)
            throws SAXException {
        String charset = attributes.getValue(AttributeName.CHARSET);
        if (charset != null) {
            if (tokenizer.internalEncodingDeclaration(charset)) {
                requestSuspension();
                return;
            }
            return;
        }
        if (!Portability.lowerCaseLiteralEqualsIgnoreAsciiCaseString(
                "content-type",
                attributes.getValue(AttributeName.HTTP_EQUIV))) {
            return;
        }
        String content = attributes.getValue(AttributeName.CONTENT);
        if (content != null) {
            String extract = TreeBuilder.extractCharsetFromContent(content
                // CPPONLY: , this
            );
            // remember not to return early without releasing the string
            if (extract != null) {
                if (tokenizer.internalEncodingDeclaration(extract)) {
                    requestSuspension();
                }
            }
            Portability.releaseString(extract);
        }
    }

    public final void endTag(ElementName elementName) throws SAXException {
        flushCharacters();
        needToDropLF = false;
        int eltPos;
        int group = elementName.getGroup();
        @Local String name = elementName.getName();
        endtagloop: for (;;) {
            if (isInForeign()) {
                if (stack[currentPtr].name != name) {
                    if (currentPtr == 0) {
                        errStrayEndTag(name);
                    } else {
                        errEndTagDidNotMatchCurrentOpenElement(name, stack[currentPtr].popName);
                    }
                }
                eltPos = currentPtr;
                int origPos = currentPtr;
                for (;;) {
                    if (eltPos == 0) {
                        assert fragment: "We can get this close to the root of the stack in foreign content only in the fragment case.";
                        break endtagloop;
                    }
                    if (stack[eltPos].name == name) {
                        while (currentPtr >= eltPos) {
                            popForeign(origPos, eltPos);
                        }
                        break endtagloop;
                    }
                    if (stack[--eltPos].ns == "http://www.w3.org/1999/xhtml") {
                        break;
                    }
                }
            }
            switch (mode) {
                case IN_TEMPLATE:
                    switch (group) {
                        case TEMPLATE:
                            // fall through to IN_HEAD
                            break;
                        default:
                            errStrayEndTag(name);
                            break endtagloop;
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case IN_ROW:
                    switch (group) {
                        case TR:
                            eltPos = findLastOrRoot(TreeBuilder.TR);
                            if (eltPos == 0) {
                                assert fragment || isTemplateContents();
                                errNoTableRowToClose();
                                break endtagloop;
                            }
                            clearStackBackTo(eltPos);
                            pop();
                            mode = IN_TABLE_BODY;
                            break endtagloop;
                        case TABLE:
                            eltPos = findLastOrRoot(TreeBuilder.TR);
                            if (eltPos == 0) {
                                assert fragment || isTemplateContents();
                                errNoTableRowToClose();
                                break endtagloop;
                            }
                            clearStackBackTo(eltPos);
                            pop();
                            mode = IN_TABLE_BODY;
                            continue;
                        case TBODY_OR_THEAD_OR_TFOOT:
                            if (findLastInTableScope(name) == TreeBuilder.NOT_FOUND_ON_STACK) {
                                errStrayEndTag(name);
                                break endtagloop;
                            }
                            eltPos = findLastOrRoot(TreeBuilder.TR);
                            if (eltPos == 0) {
                                assert fragment || isTemplateContents();
                                errNoTableRowToClose();
                                break endtagloop;
                            }
                            clearStackBackTo(eltPos);
                            pop();
                            mode = IN_TABLE_BODY;
                            continue;
                        case BODY:
                        case CAPTION:
                        case COL:
                        case COLGROUP:
                        case HTML:
                        case TD_OR_TH:
                            errStrayEndTag(name);
                            break endtagloop;
                        default:
                            // fall through to IN_TABLE
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case IN_TABLE_BODY:
                    switch (group) {
                        case TBODY_OR_THEAD_OR_TFOOT:
                            eltPos = findLastOrRoot(name);
                            if (eltPos == 0) {
                                errStrayEndTag(name);
                                break endtagloop;
                            }
                            clearStackBackTo(eltPos);
                            pop();
                            mode = IN_TABLE;
                            break endtagloop;
                        case TABLE:
                            eltPos = findLastInTableScopeOrRootTemplateTbodyTheadTfoot();
                            if (eltPos == 0 || stack[eltPos].getGroup() == TEMPLATE) {
                                assert fragment || isTemplateContents();
                                errStrayEndTag(name);
                                break endtagloop;
                            }
                            clearStackBackTo(eltPos);
                            pop();
                            mode = IN_TABLE;
                            continue;
                        case BODY:
                        case CAPTION:
                        case COL:
                        case COLGROUP:
                        case HTML:
                        case TD_OR_TH:
                        case TR:
                            errStrayEndTag(name);
                            break endtagloop;
                        default:
                            // fall through to IN_TABLE
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case IN_TABLE:
                    switch (group) {
                        case TABLE:
                            eltPos = findLast("table");
                            if (eltPos == TreeBuilder.NOT_FOUND_ON_STACK) {
                                assert fragment || isTemplateContents();
                                errStrayEndTag(name);
                                break endtagloop;
                            }
                            while (currentPtr >= eltPos) {
                                pop();
                            }
                            resetTheInsertionMode();
                            break endtagloop;
                        case BODY:
                        case CAPTION:
                        case COL:
                        case COLGROUP:
                        case HTML:
                        case TBODY_OR_THEAD_OR_TFOOT:
                        case TD_OR_TH:
                        case TR:
                            errStrayEndTag(name);
                            break endtagloop;
                        case TEMPLATE:
                            // fall through to IN_HEAD
                            break;
                        default:
                            errStrayEndTag(name);
                            // fall through to IN_BODY
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case IN_CAPTION:
                    switch (group) {
                        case CAPTION:
                            eltPos = findLastInTableScope("caption");
                            if (eltPos == TreeBuilder.NOT_FOUND_ON_STACK) {
                                break endtagloop;
                            }
                            generateImpliedEndTags();
                            if (errorHandler != null && currentPtr != eltPos) {
                                errUnclosedElements(eltPos, name);
                            }
                            while (currentPtr >= eltPos) {
                                pop();
                            }
                            clearTheListOfActiveFormattingElementsUpToTheLastMarker();
                            mode = IN_TABLE;
                            break endtagloop;
                        case TABLE:
                            eltPos = findLastInTableScope("caption");

                            if (eltPos == TreeBuilder.NOT_FOUND_ON_STACK) {
                                assert fragment || isTemplateContents();
                                errStrayEndTag(name);
                                break endtagloop;
                            }
                            generateImpliedEndTags();
                            if (errorHandler != null && currentPtr != eltPos) {
                                errUnclosedElements(eltPos, name);
                            }
                            while (currentPtr >= eltPos) {
                                pop();
                            }
                            clearTheListOfActiveFormattingElementsUpToTheLastMarker();
                            mode = IN_TABLE;
                            continue;
                        case BODY:
                        case COL:
                        case COLGROUP:
                        case HTML:
                        case TBODY_OR_THEAD_OR_TFOOT:
                        case TD_OR_TH:
                        case TR:
                            errStrayEndTag(name);
                            break endtagloop;
                        default:
                            // fall through to IN_BODY
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case IN_CELL:
                    switch (group) {
                        case TD_OR_TH:
                            eltPos = findLastInTableScope(name);
                            if (eltPos == TreeBuilder.NOT_FOUND_ON_STACK) {
                                errStrayEndTag(name);
                                break endtagloop;
                            }
                            generateImpliedEndTags();
                            if (errorHandler != null && !isCurrent(name)) {
                                errUnclosedElements(eltPos, name);
                            }
                            while (currentPtr >= eltPos) {
                                pop();
                            }
                            clearTheListOfActiveFormattingElementsUpToTheLastMarker();
                            mode = IN_ROW;
                            break endtagloop;
                        case TABLE:
                        case TBODY_OR_THEAD_OR_TFOOT:
                        case TR:
                            if (findLastInTableScope(name) == TreeBuilder.NOT_FOUND_ON_STACK) {
                                assert name == "tbody" || name == "tfoot" || name == "thead" || fragment || isTemplateContents();
                                errStrayEndTag(name);
                                break endtagloop;
                            }
                            closeTheCell(findLastInTableScopeTdTh());
                            continue;
                        case BODY:
                        case CAPTION:
                        case COL:
                        case COLGROUP:
                        case HTML:
                            errStrayEndTag(name);
                            break endtagloop;
                        default:
                            // fall through to IN_BODY
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case FRAMESET_OK:
                case IN_BODY:
                    switch (group) {
                        case BODY:
                            if (!isSecondOnStackBody()) {
                                assert fragment || isTemplateContents();
                                errStrayEndTag(name);
                                break endtagloop;
                            }
                            assert currentPtr >= 1;
                            if (errorHandler != null) {
                                uncloseloop1: for (int i = 2; i <= currentPtr; i++) {
                                    switch (stack[i].getGroup()) {
                                        case DD_OR_DT:
                                        case LI:
                                        case OPTGROUP:
                                        case OPTION: // is this possible?
                                        case P:
                                        case RB_OR_RTC:
                                        case RT_OR_RP:
                                        case TD_OR_TH:
                                        case TBODY_OR_THEAD_OR_TFOOT:
                                            break;
                                        default:
                                            errEndWithUnclosedElements(name);
                                            break uncloseloop1;
                                    }
                                }
                            }
                            mode = AFTER_BODY;
                            break endtagloop;
                        case HTML:
                            if (!isSecondOnStackBody()) {
                                assert fragment || isTemplateContents();
                                errStrayEndTag(name);
                                break endtagloop;
                            }
                            if (errorHandler != null) {
                                uncloseloop2: for (int i = 0; i <= currentPtr; i++) {
                                    switch (stack[i].getGroup()) {
                                        case DD_OR_DT:
                                        case LI:
                                        case P:
                                        case RB_OR_RTC:
                                        case RT_OR_RP:
                                        case TBODY_OR_THEAD_OR_TFOOT:
                                        case TD_OR_TH:
                                        case BODY:
                                        case HTML:
                                            break;
                                        default:
                                            errEndWithUnclosedElements(name);
                                            break uncloseloop2;
                                    }
                                }
                            }
                            mode = AFTER_BODY;
                            continue;
                        case DIV_OR_BLOCKQUOTE_OR_CENTER_OR_MENU:
                        case UL_OR_OL_OR_DL:
                        case PRE_OR_LISTING:
                        case FIELDSET:
                        case BUTTON:
                        case ADDRESS_OR_ARTICLE_OR_ASIDE_OR_DETAILS_OR_DIALOG_OR_DIR_OR_FIGCAPTION_OR_FIGURE_OR_FOOTER_OR_HEADER_OR_HGROUP_OR_MAIN_OR_NAV_OR_SEARCH_OR_SECTION_OR_SUMMARY:
                            eltPos = findLastInScope(name);
                            if (eltPos == TreeBuilder.NOT_FOUND_ON_STACK) {
                                errStrayEndTag(name);
                            } else {
                                generateImpliedEndTags();
                                if (errorHandler != null && !isCurrent(name)) {
                                    errUnclosedElements(eltPos, name);
                                }
                                while (currentPtr >= eltPos) {
                                    pop();
                                }
                            }
                            break endtagloop;
                        case FORM:
                            if (!isTemplateContents()) {
                                if (formPointer == null) {
                                    errStrayEndTag(name);
                                    break endtagloop;
                                }
                                formPointer = null;
                                eltPos = findLastInScope(name);
                                if (eltPos == TreeBuilder.NOT_FOUND_ON_STACK) {
                                    errStrayEndTag(name);
                                    break endtagloop;
                                }
                                generateImpliedEndTags();
                                if (errorHandler != null && !isCurrent(name)) {
                                    errUnclosedElements(eltPos, name);
                                }
                                removeFromStack(eltPos);
                                break endtagloop;
                            } else {
                                eltPos = findLastInScope(name);
                                if (eltPos == TreeBuilder.NOT_FOUND_ON_STACK) {
                                    errStrayEndTag(name);
                                    break endtagloop;
                                }
                                generateImpliedEndTags();
                                if (errorHandler != null && !isCurrent(name)) {
                                    errUnclosedElements(eltPos, name);
                                }
                                while (currentPtr >= eltPos) {
                                    pop();
                                }
                                break endtagloop;
                            }
                        case P:
                            eltPos = findLastInButtonScope("p");
                            if (eltPos == TreeBuilder.NOT_FOUND_ON_STACK) {
                                errNoElementToCloseButEndTagSeen("p");
                                // XXX Can the 'in foreign' case happen anymore?
                                if (isInForeign()) {
                                    errHtmlStartTagInForeignContext(name);
                                    // Check for currentPtr for the fragment
                                    // case.
                                    while (currentPtr >= 0 && stack[currentPtr].ns != "http://www.w3.org/1999/xhtml") {
                                        pop();
                                    }
                                }
                                appendVoidElementToCurrentMayFoster(
                                        elementName,
                                        HtmlAttributes.EMPTY_ATTRIBUTES);
                                break endtagloop;
                            }
                            generateImpliedEndTagsExceptFor("p");
                            assert eltPos != TreeBuilder.NOT_FOUND_ON_STACK;
                            if (errorHandler != null && eltPos != currentPtr) {
                                errUnclosedElements(eltPos, name);
                            }
                            while (currentPtr >= eltPos) {
                                pop();
                            }
                            break endtagloop;
                        case LI:
                            eltPos = findLastInListScope(name);
                            if (eltPos == TreeBuilder.NOT_FOUND_ON_STACK) {
                                errNoElementToCloseButEndTagSeen(name);
                            } else {
                                generateImpliedEndTagsExceptFor(name);
                                if (errorHandler != null
                                        && eltPos != currentPtr) {
                                    errUnclosedElements(eltPos, name);
                                }
                                while (currentPtr >= eltPos) {
                                    pop();
                                }
                            }
                            break endtagloop;
                        case DD_OR_DT:
                            eltPos = findLastInScope(name);
                            if (eltPos == TreeBuilder.NOT_FOUND_ON_STACK) {
                                errNoElementToCloseButEndTagSeen(name);
                            } else {
                                generateImpliedEndTagsExceptFor(name);
                                if (errorHandler != null
                                        && eltPos != currentPtr) {
                                    errUnclosedElements(eltPos, name);
                                }
                                while (currentPtr >= eltPos) {
                                    pop();
                                }
                            }
                            break endtagloop;
                        case H1_OR_H2_OR_H3_OR_H4_OR_H5_OR_H6:
                            eltPos = findLastInScopeHn();
                            if (eltPos == TreeBuilder.NOT_FOUND_ON_STACK) {
                                errStrayEndTag(name);
                            } else {
                                generateImpliedEndTags();
                                if (errorHandler != null && !isCurrent(name)) {
                                    errUnclosedElements(eltPos, name);
                                }
                                while (currentPtr >= eltPos) {
                                    pop();
                                }
                            }
                            break endtagloop;
                        case OBJECT:
                        case MARQUEE_OR_APPLET:
                            eltPos = findLastInScope(name);
                            if (eltPos == TreeBuilder.NOT_FOUND_ON_STACK) {
                                errStrayEndTag(name);
                            } else {
                                generateImpliedEndTags();
                                if (errorHandler != null && !isCurrent(name)) {
                                    errUnclosedElements(eltPos, name);
                                }
                                while (currentPtr >= eltPos) {
                                    pop();
                                }
                                clearTheListOfActiveFormattingElementsUpToTheLastMarker();
                            }
                            break endtagloop;
                        case BR:
                            errEndTagBr();
                            if (isInForeign()) {
                                // XXX can this happen anymore?
                                errHtmlStartTagInForeignContext(name);
                                // Check for currentPtr for the fragment
                                // case.
                                while (currentPtr >= 0 && stack[currentPtr].ns != "http://www.w3.org/1999/xhtml") {
                                    pop();
                                }
                            }
                            reconstructTheActiveFormattingElements();
                            appendVoidElementToCurrentMayFoster(
                                    elementName,
                                    HtmlAttributes.EMPTY_ATTRIBUTES);
                            break endtagloop;
                        case TEMPLATE:
                            // fall through to IN_HEAD;
                            break;
                        case SELECT:
                            // TODO: once noInSelectMode goes away, case SELECT: should move together with BUTTON and ADDRESS_OR_ARTICLE_OR_ASIDE_OR_DETAILS_OR_DIALOG_OR_DIR_OR_FIGCAPTION_OR_FIGURE_OR_FOOTER_OR_HEADER_OR_HGROUP_OR_MAIN_OR_NAV_OR_SEARCH_OR_SECTION_OR_SUMMARY.
                            if (noInSelectMode) {
                                eltPos = findLastInScope(name);
                                if (eltPos == TreeBuilder.NOT_FOUND_ON_STACK) {
                                    errStrayEndTag(name);
                                } else {
                                    generateImpliedEndTags();
                                    if (errorHandler != null && !isCurrent(name)) {
                                        errUnclosedElements(eltPos, name);
                                    }
                                    while (currentPtr >= eltPos) {
                                        pop();
                                    }
                                }
                                break endtagloop;
                            }
                            // CPPONLY: MOZ_FALLTHROUGH;
                        case AREA_OR_WBR:
                        case KEYGEN: // XXX??
                        case PARAM_OR_SOURCE_OR_TRACK:
                        case EMBED:
                        case IMG:
                        case IMAGE:
                        case INPUT:
                        case HR:
                        case IFRAME:
                        case NOEMBED: // XXX???
                        case NOFRAMES: // XXX??
                        case TABLE:
                        case TEXTAREA: // XXX??
                            errStrayEndTag(name);
                            break endtagloop;
                        case NOSCRIPT:
                            if (scriptingEnabled) {
                                errStrayEndTag(name);
                                break endtagloop;
                            }
                            // CPPONLY: MOZ_FALLTHROUGH;
                        case A:
                        case B_OR_BIG_OR_CODE_OR_EM_OR_I_OR_S_OR_SMALL_OR_STRIKE_OR_STRONG_OR_TT_OR_U:
                        case FONT:
                        case NOBR:
                            if (adoptionAgencyEndTag(name)) {
                                break endtagloop;
                            }
                            // else handle like any other tag
                            // CPPONLY: MOZ_FALLTHROUGH;
                        default:
                            if (isCurrent(name)) {
                                pop();
                                break endtagloop;
                            }

                            eltPos = currentPtr;
                            for (;;) {
                                StackNode<T> node = stack[eltPos];
                                if (node.ns == "http://www.w3.org/1999/xhtml" && node.name == name) {
                                    generateImpliedEndTags();
                                    if (errorHandler != null
                                            && !isCurrent(name)) {
                                        errUnclosedElements(eltPos, name);
                                    }
                                    while (currentPtr >= eltPos) {
                                        pop();
                                    }
                                    break endtagloop;
                                } else if (eltPos == 0 || node.isSpecial()) {
                                    errStrayEndTag(name);
                                    break endtagloop;
                                }
                                eltPos--;
                            }
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case IN_HEAD:
                    switch (group) {
                        case HEAD:
                            pop();
                            mode = AFTER_HEAD;
                            break endtagloop;
                        case BR:
                        case HTML:
                        case BODY:
                            pop();
                            mode = AFTER_HEAD;
                            continue;
                        case TEMPLATE:
                            endTagTemplateInHead();
                            break endtagloop;
                        default:
                            errStrayEndTag(name);
                            break endtagloop;
                    }
                case IN_HEAD_NOSCRIPT:
                    switch (group) {
                        case NOSCRIPT:
                            pop();
                            mode = IN_HEAD;
                            break endtagloop;
                        case BR:
                            errStrayEndTag(name);
                            pop();
                            mode = IN_HEAD;
                            continue;
                        default:
                            errStrayEndTag(name);
                            break endtagloop;
                    }
                case IN_COLUMN_GROUP:
                    switch (group) {
                        case COLGROUP:
                            if (currentPtr == 0 || stack[currentPtr].getGroup() ==
                                    TreeBuilder.TEMPLATE) {
                                assert fragment || isTemplateContents();
                                errGarbageInColgroup();
                                break endtagloop;
                            }
                            pop();
                            mode = IN_TABLE;
                            break endtagloop;
                        case COL:
                            errStrayEndTag(name);
                            break endtagloop;
                        case TEMPLATE:
                            endTagTemplateInHead();
                            break endtagloop;
                        default:
                            if (currentPtr == 0 || stack[currentPtr].getGroup() ==
                                    TreeBuilder.TEMPLATE) {
                                assert fragment || isTemplateContents();
                                errGarbageInColgroup();
                                break endtagloop;
                            }
                            pop();
                            mode = IN_TABLE;
                            continue;
                    }
                case IN_SELECT_IN_TABLE:
                    // CPPONLY: MOZ_ASSERT(!noInSelectMode);
                    switch (group) {
                        case CAPTION:
                        case TABLE:
                        case TBODY_OR_THEAD_OR_TFOOT:
                        case TR:
                        case TD_OR_TH:
                            errEndTagSeenWithSelectOpen(name);
                            if (findLastInTableScope(name) != TreeBuilder.NOT_FOUND_ON_STACK) {
                                eltPos = findLastInTableScope("select");
                                if (eltPos == TreeBuilder.NOT_FOUND_ON_STACK) {
                                    assert fragment;
                                    break endtagloop; // http://www.w3.org/Bugs/Public/show_bug.cgi?id=8375
                                }
                                while (currentPtr >= eltPos) {
                                    pop();
                                }
                                resetTheInsertionMode();
                                continue;
                            } else {
                                break endtagloop;
                            }
                        default:
                            // fall through to IN_SELECT
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case IN_SELECT:
                    // CPPONLY: MOZ_ASSERT(!noInSelectMode);
                    switch (group) {
                        case OPTION:
                            if (isCurrent("option")) {
                                pop();
                                break endtagloop;
                            } else {
                                errStrayEndTag(name);
                                break endtagloop;
                            }
                        case OPTGROUP:
                            if (isCurrent("option")
                                    && "optgroup" == stack[currentPtr - 1].name) {
                                pop();
                            }
                            if (isCurrent("optgroup")) {
                                pop();
                            } else {
                                errStrayEndTag(name);
                            }
                            break endtagloop;
                        case SELECT:
                            eltPos = findLastInTableScope("select");
                            if (eltPos == TreeBuilder.NOT_FOUND_ON_STACK) {
                                assert fragment;
                                errStrayEndTag(name);
                                break endtagloop;
                            }
                            while (currentPtr >= eltPos) {
                                pop();
                            }
                            resetTheInsertionMode();
                            break endtagloop;
                        case TEMPLATE:
                            endTagTemplateInHead();
                            break endtagloop;
                        default:
                            errStrayEndTag(name);
                            break endtagloop;
                    }
                case AFTER_BODY:
                    switch (group) {
                        case HTML:
                            if (fragment) {
                                errStrayEndTag(name);
                                break endtagloop;
                            } else {
                                mode = AFTER_AFTER_BODY;
                                break endtagloop;
                            }
                        default:
                            errEndTagAfterBody();
                            mode = framesetOk ? FRAMESET_OK : IN_BODY;
                            continue;
                    }
                case IN_FRAMESET:
                    switch (group) {
                        case FRAMESET:
                            if (currentPtr == 0) {
                                assert fragment;
                                errStrayEndTag(name);
                                break endtagloop;
                            }
                            pop();
                            if ((!fragment) && !isCurrent("frameset")) {
                                mode = AFTER_FRAMESET;
                            }
                            break endtagloop;
                        default:
                            errStrayEndTag(name);
                            break endtagloop;
                    }
                case AFTER_FRAMESET:
                    switch (group) {
                        case HTML:
                            mode = AFTER_AFTER_FRAMESET;
                            break endtagloop;
                        default:
                            errStrayEndTag(name);
                            break endtagloop;
                    }
                case INITIAL:
                    /*
                     * Parse error.
                     */
                    errEndTagSeenWithoutDoctype();
                    /*
                     *
                     * Set the document to quirks mode.
                     */
                    documentModeInternal(DocumentMode.QUIRKS_MODE, null, null);
                    /*
                     * Then, switch to the root element mode of the tree
                     * construction stage
                     */
                    mode = BEFORE_HTML;
                    /*
                     * and reprocess the current token.
                     */
                    continue;
                case BEFORE_HTML:
                    switch (group) {
                        case HEAD:
                        case BR:
                        case HTML:
                        case BODY:
                            /*
                             * Create an HTMLElement node with the tag name
                             * html, in the HTML namespace. Append it to the
                             * Document object.
                             */
                            appendHtmlElementToDocumentAndPush();
                            /* Switch to the main mode */
                            mode = BEFORE_HEAD;
                            /*
                             * reprocess the current token.
                             */
                            continue;
                        default:
                            errStrayEndTag(name);
                            break endtagloop;
                    }
                case BEFORE_HEAD:
                    switch (group) {
                        case HEAD:
                        case BR:
                        case HTML:
                        case BODY:
                            appendToCurrentNodeAndPushHeadElement(HtmlAttributes.EMPTY_ATTRIBUTES);
                            mode = IN_HEAD;
                            continue;
                        default:
                            errStrayEndTag(name);
                            break endtagloop;
                    }
                case AFTER_HEAD:
                    switch (group) {
                        case TEMPLATE:
                            endTagTemplateInHead();
                            break endtagloop;
                        case HTML:
                        case BODY:
                        case BR:
                            appendToCurrentNodeAndPushBodyElement();
                            mode = FRAMESET_OK;
                            continue;
                        default:
                            errStrayEndTag(name);
                            break endtagloop;
                    }
                case AFTER_AFTER_BODY:
                    errStrayEndTag(name);
                    mode = framesetOk ? FRAMESET_OK : IN_BODY;
                    continue;
                case AFTER_AFTER_FRAMESET:
                    errStrayEndTag(name);
                    break endtagloop;
                case TEXT:
                    // XXX need to manage insertion point here
                    pop();
                    if (originalMode == AFTER_HEAD) {
                        silentPop();
                    }
                    mode = originalMode;
                    break endtagloop;
            }
        } // endtagloop
    }

    private void endTagTemplateInHead() throws SAXException {
        int eltPos = findLast("template");
        if (eltPos == TreeBuilder.NOT_FOUND_ON_STACK) {
            errStrayEndTag("template");
            return;
        }
        generateImpliedEndTagsThoroughly();
        if (errorHandler != null && !isCurrent("template")) {
            errUnclosedElements(eltPos, "template");
        }
        while (currentPtr >= eltPos) {
            pop();
        }
        clearTheListOfActiveFormattingElementsUpToTheLastMarker();
        popTemplateMode();
        resetTheInsertionMode();
    }

    private int findLastInTableScopeOrRootTemplateTbodyTheadTfoot() {
        for (int i = currentPtr; i > 0; i--) {
            if (stack[i].ns == "http://www.w3.org/1999/xhtml"
                    && (stack[i].getGroup() == TreeBuilder.TBODY_OR_THEAD_OR_TFOOT
                            || stack[i].getGroup() == TreeBuilder.TEMPLATE)) {
                return i;
            }
        }
        return 0;
    }

    private int findLast(@Local String name) {
        for (int i = currentPtr; i > 0; i--) {
            if (stack[i].ns == "http://www.w3.org/1999/xhtml" && stack[i].name == name) {
                return i;
            }
        }
        return TreeBuilder.NOT_FOUND_ON_STACK;
    }

    private int findLastInTableScope(@Local String name) {
        for (int i = currentPtr; i > 0; i--) {
            if (stack[i].ns == "http://www.w3.org/1999/xhtml") {
                if (stack[i].name == name) {
                    return i;
                } else if (stack[i].name == "table" || stack[i].name == "template") {
                    return TreeBuilder.NOT_FOUND_ON_STACK;
                }
            }
        }
        return TreeBuilder.NOT_FOUND_ON_STACK;
    }

    private int findLastInButtonScope(@Local String name) {
        for (int i = currentPtr; i > 0; i--) {
            if (stack[i].ns == "http://www.w3.org/1999/xhtml") {
                if (stack[i].name == name) {
                    return i;
                } else if (stack[i].name == "button") {
                    return TreeBuilder.NOT_FOUND_ON_STACK;
                }
            }

            if (stack[i].isScoping()) {
                return TreeBuilder.NOT_FOUND_ON_STACK;
            }
        }
        return TreeBuilder.NOT_FOUND_ON_STACK;
    }

    private int findLastInScope(@Local String name) {
        for (int i = currentPtr; i > 0; i--) {
            if (stack[i].ns == "http://www.w3.org/1999/xhtml" && stack[i].name == name) {
                return i;
            } else if (stack[i].isScoping()) {
                return TreeBuilder.NOT_FOUND_ON_STACK;
            }
        }
        return TreeBuilder.NOT_FOUND_ON_STACK;
    }

    private int findLastInListScope(@Local String name) {
        for (int i = currentPtr; i > 0; i--) {
            if (stack[i].ns == "http://www.w3.org/1999/xhtml") {
                if (stack[i].name == name) {
                    return i;
                } else if (stack[i].name == "ul" || stack[i].name == "ol") {
                    return TreeBuilder.NOT_FOUND_ON_STACK;
                }
            }

            if (stack[i].isScoping()) {
                return TreeBuilder.NOT_FOUND_ON_STACK;
            }
        }
        return TreeBuilder.NOT_FOUND_ON_STACK;
    }

    private int findLastInScopeHn() {
        for (int i = currentPtr; i > 0; i--) {
            if (stack[i].getGroup() == TreeBuilder.H1_OR_H2_OR_H3_OR_H4_OR_H5_OR_H6) {
                return i;
            } else if (stack[i].isScoping()) {
                return TreeBuilder.NOT_FOUND_ON_STACK;
            }
        }
        return TreeBuilder.NOT_FOUND_ON_STACK;
    }

    private void generateImpliedEndTagsExceptFor(@Local String name)
            throws SAXException {
        for (;;) {
            StackNode<T> node = stack[currentPtr];
            switch (node.getGroup()) {
                case P:
                case LI:
                case DD_OR_DT:
                case OPTION:
                case OPTGROUP:
                case RB_OR_RTC:
                case RT_OR_RP:
                    if (node.ns == "http://www.w3.org/1999/xhtml" && node.name == name) {
                        return;
                    }
                    pop();
                    continue;
                default:
                    return;
            }
        }
    }

    private void generateImpliedEndTags() throws SAXException {
        for (;;) {
            switch (stack[currentPtr].getGroup()) {
                case P:
                case LI:
                case DD_OR_DT:
                case OPTION:
                case OPTGROUP:
                case RB_OR_RTC:
                case RT_OR_RP:
                    pop();
                    continue;
                default:
                    return;
            }
        }
    }

    private void generateImpliedEndTagsThoroughly() throws SAXException {
        for (;;) {
            switch (stack[currentPtr].getGroup()) {
                case CAPTION:
                case COLGROUP:
                case DD_OR_DT:
                case LI:
                case OPTGROUP:
                case OPTION:
                case P:
                case RB_OR_RTC:
                case RT_OR_RP:
                case TBODY_OR_THEAD_OR_TFOOT:
                case TD_OR_TH:
                case TR:
                    pop();
                    continue;
                default:
                    return;
            }
        }
    }

    private boolean isSecondOnStackBody() {
        return currentPtr >= 1 && stack[1].getGroup() == TreeBuilder.BODY;
    }

    private void documentModeInternal(DocumentMode m, String publicIdentifier,
            String systemIdentifier)
            throws SAXException {

        if (forceNoQuirks) {
            // Srcdoc documents are always rendered in standards mode.
            quirks = false;
            if (documentModeHandler != null) {
                documentModeHandler.documentMode(
                        DocumentMode.STANDARDS_MODE
                        // [NOCPP[
                        , null, null
                // ]NOCPP]
                );
            }
            return;
        }

        quirks = (m == DocumentMode.QUIRKS_MODE);
        if (documentModeHandler != null) {
            documentModeHandler.documentMode(
                    m
                    // [NOCPP[
                    , publicIdentifier, systemIdentifier
            // ]NOCPP]
            );
        }
        // [NOCPP[
        documentMode(m, publicIdentifier, systemIdentifier);
        // ]NOCPP]
    }

    private boolean isAlmostStandards(String publicIdentifier,
            String systemIdentifier) {
        if (Portability.lowerCaseLiteralIsPrefixOfIgnoreAsciiCaseString(
                "-//w3c//dtd xhtml 1.0 transitional//", publicIdentifier)) {
            return true;
        }
        if (Portability.lowerCaseLiteralIsPrefixOfIgnoreAsciiCaseString(
                "-//w3c//dtd xhtml 1.0 frameset//", publicIdentifier)) {
            return true;
        }
        if (systemIdentifier != null) {
            if (Portability.lowerCaseLiteralIsPrefixOfIgnoreAsciiCaseString(
                    "-//w3c//dtd html 4.01 transitional//", publicIdentifier)) {
                return true;
            }
            if (Portability.lowerCaseLiteralIsPrefixOfIgnoreAsciiCaseString(
                    "-//w3c//dtd html 4.01 frameset//", publicIdentifier)) {
                return true;
            }
        }
        return false;
    }

    private boolean isQuirky(@Local String name, String publicIdentifier,
            String systemIdentifier, boolean forceQuirks) {
        if (forceQuirks) {
            return true;
        }
        if (name != HTML_LOCAL) {
            return true;
        }
        if (publicIdentifier != null) {
            for (int i = 0; i < TreeBuilder.QUIRKY_PUBLIC_IDS.length; i++) {
                if (Portability.lowerCaseLiteralIsPrefixOfIgnoreAsciiCaseString(
                        TreeBuilder.QUIRKY_PUBLIC_IDS[i], publicIdentifier)) {
                    return true;
                }
            }
            if (Portability.lowerCaseLiteralEqualsIgnoreAsciiCaseString(
                    "-//w3o//dtd w3 html strict 3.0//en//", publicIdentifier)
                    || Portability.lowerCaseLiteralEqualsIgnoreAsciiCaseString(
                            "-/w3c/dtd html 4.0 transitional/en",
                            publicIdentifier)
                    || Portability.lowerCaseLiteralEqualsIgnoreAsciiCaseString(
                            "html", publicIdentifier)) {
                return true;
            }
        }
        if (systemIdentifier == null) {
            if (Portability.lowerCaseLiteralIsPrefixOfIgnoreAsciiCaseString(
                    "-//w3c//dtd html 4.01 transitional//", publicIdentifier)) {
                return true;
            } else if (Portability.lowerCaseLiteralIsPrefixOfIgnoreAsciiCaseString(
                    "-//w3c//dtd html 4.01 frameset//", publicIdentifier)) {
                return true;
            }
        } else if (Portability.lowerCaseLiteralEqualsIgnoreAsciiCaseString(
                "http://www.ibm.com/data/dtd/v11/ibmxhtml1-transitional.dtd",
                systemIdentifier)) {
            return true;
        }
        return false;
    }

    private void closeTheCell(int eltPos) throws SAXException {
        generateImpliedEndTags();
        if (errorHandler != null && eltPos != currentPtr) {
            errUnclosedElementsCell(eltPos);
        }
        while (currentPtr >= eltPos) {
            pop();
        }
        clearTheListOfActiveFormattingElementsUpToTheLastMarker();
        mode = IN_ROW;
        return;
    }

    private int findLastInTableScopeTdTh() {
        for (int i = currentPtr; i > 0; i--) {
            @Local String name = stack[i].name;
            if (stack[i].ns == "http://www.w3.org/1999/xhtml") {
                if ("td" == name || "th" == name) {
                    return i;
                } else if (name == "table" || name == "template") {
                    return TreeBuilder.NOT_FOUND_ON_STACK;
                }
            }
        }
        return TreeBuilder.NOT_FOUND_ON_STACK;
    }

    private void clearStackBackTo(int eltPos) throws SAXException {
        int eltGroup = stack[eltPos].getGroup();
        while (currentPtr > eltPos) { // > not >= intentional
            if (stack[currentPtr].ns == "http://www.w3.org/1999/xhtml"
                    && stack[currentPtr].getGroup() == TEMPLATE
                    && (eltGroup == TABLE || eltGroup == TBODY_OR_THEAD_OR_TFOOT|| eltGroup == TR || eltPos == 0)) {
                return;
            }
            pop();
        }
    }

    private void resetTheInsertionMode() {
        StackNode<T> node;
        @Local String name;
        @NsUri String ns;
        for (int i = currentPtr; i >= 0; i--) {
            node = stack[i];
            name = node.name;
            ns = node.ns;
            if (i == 0) {
                if (!(contextNamespace == "http://www.w3.org/1999/xhtml" && (contextName == "td" || contextName == "th"))) {
                    if (fragment) {
                        // Make sure we are parsing a fragment otherwise the context element doesn't make sense.
                        name = contextName;
                        ns = contextNamespace;
                    }
                } else {
                    mode = framesetOk ? FRAMESET_OK : IN_BODY; // XXX from Hixie's email
                    return;
                }
            }
            if ("select" == name) {
                if (!noInSelectMode) {
                    int ancestorIndex = i;
                    while (ancestorIndex > 0) {
                        StackNode<T> ancestor = stack[ancestorIndex--];
                        if ("http://www.w3.org/1999/xhtml" == ancestor.ns) {
                            if ("template" == ancestor.name) {
                                break;
                            }
                            if ("table" == ancestor.name) {
                                mode = IN_SELECT_IN_TABLE;
                                return;
                            }
                        }
                    }
                    mode = IN_SELECT;
                    return;
                }
                if (i == 0) {
                    mode = framesetOk ? FRAMESET_OK : IN_BODY;
                    return;
                }
                continue;
            } else if ("td" == name || "th" == name) {
                mode = IN_CELL;
                return;
            } else if ("tr" == name) {
                mode = IN_ROW;
                return;
            } else if ("tbody" == name || "thead" == name || "tfoot" == name) {
                mode = IN_TABLE_BODY;
                return;
            } else if ("caption" == name) {
                mode = IN_CAPTION;
                return;
            } else if ("colgroup" == name) {
                mode = IN_COLUMN_GROUP;
                return;
            } else if ("table" == name) {
                mode = IN_TABLE;
                return;
            } else if ("http://www.w3.org/1999/xhtml" != ns) {
                mode = framesetOk ? FRAMESET_OK : IN_BODY;
                return;
            } else if ("template" == name) {
                assert templateModePtr >= 0;
                mode = templateModeStack[templateModePtr];
                return;
            }  else if ("head" == name) {
                if (name == contextName) {
                    mode = framesetOk ? FRAMESET_OK : IN_BODY; // really
                } else {
                    mode = IN_HEAD;
                }
                return;
            } else if ("body" == name) {
                mode = framesetOk ? FRAMESET_OK : IN_BODY;
                return;
            } else if ("frameset" == name) {
                // TODO: Fragment case. Add error reporting.
                mode = IN_FRAMESET;
                return;
            } else if ("html" == name) {
                if (headPointer == null) {
                    // TODO: Fragment case. Add error reporting.
                    mode = BEFORE_HEAD;
                } else {
                    mode = AFTER_HEAD;
                }
                return;
            } else if (i == 0) {
                mode = framesetOk ? FRAMESET_OK : IN_BODY;
                return;
            }
        }
    }

    /**
     * @throws SAXException
     *
     */
    private void implicitlyCloseP() throws SAXException {
        int eltPos = findLastInButtonScope("p");
        if (eltPos == TreeBuilder.NOT_FOUND_ON_STACK) {
            return;
        }
        generateImpliedEndTagsExceptFor("p");
        if (errorHandler != null && eltPos != currentPtr) {
            errUnclosedElementsImplied(eltPos, "p");
        }
        while (currentPtr >= eltPos) {
            pop();
        }
    }

    private boolean debugOnlyClearLastStackSlot() {
        stack[currentPtr] = null;
        return true;
    }

    private boolean debugOnlyClearLastListSlot() {
        listOfActiveFormattingElements[listPtr] = null;
        return true;
    }

    private void pushTemplateMode(int mode) {
        templateModePtr++;
        if (templateModePtr == templateModeStack.length) {
            int[] newStack = new int[templateModeStack.length + 64];
            System.arraycopy(templateModeStack, 0, newStack, 0, templateModeStack.length);
            templateModeStack = newStack;
        }
        templateModeStack[templateModePtr] = mode;
    }

    @SuppressWarnings("unchecked") private void push(StackNode<T> node) throws SAXException {
        currentPtr++;
        if (currentPtr == stack.length) {
            StackNode<T>[] newStack = new StackNode[stack.length + 64];
            System.arraycopy(stack, 0, newStack, 0, stack.length);
            stack = newStack;
        }
        stack[currentPtr] = node;
        elementPushed(node.ns, node.popName, node.node);
    }

    @SuppressWarnings("unchecked") private void silentPush(StackNode<T> node) throws SAXException {
        currentPtr++;
        if (currentPtr == stack.length) {
            StackNode<T>[] newStack = new StackNode[stack.length + 64];
            System.arraycopy(stack, 0, newStack, 0, stack.length);
            stack = newStack;
        }
        stack[currentPtr] = node;
    }

    @SuppressWarnings("unchecked") private void append(StackNode<T> node) {
        listPtr++;
        if (listPtr == listOfActiveFormattingElements.length) {
            StackNode<T>[] newList = new StackNode[listOfActiveFormattingElements.length + 64];
            System.arraycopy(listOfActiveFormattingElements, 0, newList, 0,
                    listOfActiveFormattingElements.length);
            listOfActiveFormattingElements = newList;
        }
        listOfActiveFormattingElements[listPtr] = node;
    }

    @Inline private void insertMarker() {
        append(null);
    }

    private void clearTheListOfActiveFormattingElementsUpToTheLastMarker() {
        while (listPtr > -1) {
            if (listOfActiveFormattingElements[listPtr] == null) {
                --listPtr;
                return;
            }
            listOfActiveFormattingElements[listPtr].release(this);
            --listPtr;
        }
    }

    @Inline private boolean isCurrent(@Local String name) {
        return stack[currentPtr].ns == "http://www.w3.org/1999/xhtml" &&
                name == stack[currentPtr].name;
    }

    private void removeFromStack(int pos) throws SAXException {
        if (currentPtr == pos) {
            pop();
        } else {
            fatal();
            stack[pos].release(this);
            System.arraycopy(stack, pos + 1, stack, pos, currentPtr - pos);
            assert debugOnlyClearLastStackSlot();
            currentPtr--;
        }
    }

    private void removeFromStack(StackNode<T> node) throws SAXException {
        if (stack[currentPtr] == node) {
            pop();
        } else {
            int pos = currentPtr - 1;
            while (pos >= 0 && stack[pos] != node) {
                pos--;
            }
            if (pos == -1) {
                // dead code?
                return;
            }
            fatal();
            node.release(this);
            System.arraycopy(stack, pos + 1, stack, pos, currentPtr - pos);
            currentPtr--;
        }
    }

    private void removeFromListOfActiveFormattingElements(int pos) {
        assert listOfActiveFormattingElements[pos] != null;
        listOfActiveFormattingElements[pos].release(this);
        if (pos == listPtr) {
            assert debugOnlyClearLastListSlot();
            listPtr--;
            return;
        }
        assert pos < listPtr;
        System.arraycopy(listOfActiveFormattingElements, pos + 1,
                listOfActiveFormattingElements, pos, listPtr - pos);
        assert debugOnlyClearLastListSlot();
        listPtr--;
    }

    /**
     * Adoption agency algorithm.
     *
     * @param name subject as described in the specified algorithm.
     * @return Returns true if the algorithm has completed and there is nothing remaining to
     * be done. Returns false if the algorithm needs to "act as described in the 'any other
     * end tag' entry" as described in the specified algorithm.
     * @throws SAXException
     */
    private boolean adoptionAgencyEndTag(@Local String name) throws SAXException {
        // This check intends to ensure that for properly nested tags, closing tags will match
        // against the stack instead of the listOfActiveFormattingElements.
        if (stack[currentPtr].ns == "http://www.w3.org/1999/xhtml" &&
                stack[currentPtr].name == name &&
                findInListOfActiveFormattingElements(stack[currentPtr]) == -1) {
            // If the current element matches the name but isn't on the list of active
            // formatting elements, then it is possible that the list was mangled by the Noah's Ark
            // clause. In this case, we want to match the end tag against the stack instead of
            // proceeding with the AAA algorithm that may match against the list of
            // active formatting elements (and possibly mangle the tree in unexpected ways).
            pop();
            return true;
        }

        // If you crash around here, perhaps some stack node variable claimed to
        // be a weak ref isn't.
        for (int i = 0; i < 8; ++i) {
            int formattingEltListPos = listPtr;
            while (formattingEltListPos > -1) {
                StackNode<T> listNode = listOfActiveFormattingElements[formattingEltListPos]; // weak ref
                if (listNode == null) {
                    formattingEltListPos = -1;
                    break;
                } else if (listNode.name == name) {
                    break;
                }
                formattingEltListPos--;
            }
            if (formattingEltListPos == -1) {
                return false;
            }
            // this *looks* like a weak ref to the list of formatting elements
            StackNode<T> formattingElt = listOfActiveFormattingElements[formattingEltListPos];
            int formattingEltStackPos = currentPtr;
            boolean inScope = true;
            while (formattingEltStackPos > -1) {
                StackNode<T> node = stack[formattingEltStackPos]; // weak ref
                if (node == formattingElt) {
                    break;
                } else if (node.isScoping()) {
                    inScope = false;
                }
                formattingEltStackPos--;
            }
            if (formattingEltStackPos == -1) {
                errNoElementToCloseButEndTagSeen(name);
                removeFromListOfActiveFormattingElements(formattingEltListPos);
                return true;
            }
            if (!inScope) {
                errNoElementToCloseButEndTagSeen(name);
                return true;
            }
            // stackPos now points to the formatting element and it is in scope
            if (formattingEltStackPos != currentPtr) {
                errEndTagViolatesNestingRules(name);
            }
            int furthestBlockPos = formattingEltStackPos + 1;
            while (furthestBlockPos <= currentPtr) {
                StackNode<T> node = stack[furthestBlockPos]; // weak ref
                assert furthestBlockPos > 0: "How is formattingEltStackPos + 1 not > 0?";
                if (node.isSpecial()) {
                    break;
                }
                furthestBlockPos++;
            }
            if (furthestBlockPos > currentPtr) {
                // no furthest block
                while (currentPtr >= formattingEltStackPos) {
                    pop();
                }
                removeFromListOfActiveFormattingElements(formattingEltListPos);
                return true;
            }
            // commonAncestor is used for running the algorithm and
            // insertionCommonAncestor is used for the actual insertions to
            // keep them depth-limited.
            StackNode<T> commonAncestor = stack[formattingEltStackPos - 1]; // weak ref
            T insertionCommonAncestor = nodeFromStackWithBlinkCompat(formattingEltStackPos - 1); // weak ref
            StackNode<T> furthestBlock = stack[furthestBlockPos]; // weak ref
            // detachFromParent(furthestBlock.node); XXX AAA CHANGE
            int bookmark = formattingEltListPos;
            int nodePos = furthestBlockPos;
            StackNode<T> lastNode = furthestBlock; // weak ref
            int j = 0;
            for (;;) {
                ++j;
                nodePos--;
                if (nodePos == formattingEltStackPos) {
                    break;
                }
                StackNode<T> node = stack[nodePos]; // weak ref
                int nodeListPos = findInListOfActiveFormattingElements(node);

                if (j > 3 && nodeListPos != -1) {
                    removeFromListOfActiveFormattingElements(nodeListPos);

                    // Adjust the indices into the list to account
                    // for the removal of nodeListPos.
                    if (nodeListPos <= formattingEltListPos) {
                        formattingEltListPos--;
                    }
                    if (nodeListPos <= bookmark) {
                        bookmark--;
                    }

                    // Update position to reflect removal from list.
                    nodeListPos = -1;
                }

                if (nodeListPos == -1) {
                    assert formattingEltStackPos < nodePos;
                    assert bookmark < nodePos;
                    assert furthestBlockPos > nodePos;
                    removeFromStack(nodePos); // node is now a bad pointer in C++
                    furthestBlockPos--;
                    continue;
                }
                // now node is both on stack and in the list
                if (nodePos == furthestBlockPos) {
                    bookmark = nodeListPos + 1;
                }
                // if (hasChildren(node.node)) { XXX AAA CHANGE
                assert node == listOfActiveFormattingElements[nodeListPos];
                assert node == stack[nodePos];
                T clone = createElement("http://www.w3.org/1999/xhtml",
                        node.name, node.attributes.cloneAttributes(), insertionCommonAncestor
                        // CPPONLY: , htmlCreator(node.getHtmlCreator())
                        );
                StackNode<T> newNode = createStackNode(node.getFlags(), node.ns,
                        node.name, clone, node.popName, node.attributes
                        // CPPONLY: , node.getHtmlCreator()
                        // [NOCPP[
                        , node.getLocator()
                        // ]NOCPP]
                ); // creation ownership goes to stack
                node.dropAttributes(); // adopt ownership to newNode
                stack[nodePos] = newNode;
                newNode.retain(); // retain for list
                listOfActiveFormattingElements[nodeListPos] = newNode;
                node.release(this); // release from stack
                node.release(this); // release from list
                node = newNode;
                // } XXX AAA CHANGE
                detachFromParent(lastNode.node);
                appendElement(lastNode.node, nodeFromStackWithBlinkCompat(nodePos));
                lastNode = node;
            }
            // If we insert into a foster parent, for simplicity, we insert
            // accoding to the spec without Blink's depth limit.
            if (commonAncestor.isFosterParenting()) {
                fatal();
                detachFromParent(lastNode.node);
                insertIntoFosterParent(lastNode.node);
            } else {
                detachFromParent(lastNode.node);
                appendElement(lastNode.node, insertionCommonAncestor);
            }
            T clone = createElement("http://www.w3.org/1999/xhtml",
                    formattingElt.name,
                    formattingElt.attributes.cloneAttributes(), furthestBlock.node
                    // CPPONLY: , htmlCreator(formattingElt.getHtmlCreator())
                    );
            StackNode<T> formattingClone = createStackNode(
                    formattingElt.getFlags(), formattingElt.ns,
                    formattingElt.name, clone, formattingElt.popName,
                    formattingElt.attributes
                    // CPPONLY: , formattingElt.getHtmlCreator()
                    // [NOCPP[
                    , errorHandler == null ? null : new TaintableLocatorImpl(tokenizer)
                    // ]NOCPP]
            ); // Ownership transfers to stack below
            formattingElt.dropAttributes(); // transfer ownership to
                                            // formattingClone
            appendChildrenToNewParent(furthestBlock.node, clone);
            appendElement(clone, furthestBlock.node);
            removeFromListOfActiveFormattingElements(formattingEltListPos);
            insertIntoListOfActiveFormattingElements(formattingClone, bookmark);
            assert formattingEltStackPos < furthestBlockPos;
            removeFromStack(formattingEltStackPos);
            // furthestBlockPos is now off by one and points to the slot after
            // it
            insertIntoStack(formattingClone, furthestBlockPos);
        }
        return true;
    }

    private void insertIntoStack(StackNode<T> node, int position)
            throws SAXException {
        assert currentPtr + 1 < stack.length;
        assert position <= currentPtr + 1;
        if (position == currentPtr + 1) {
            push(node);
        } else {
            System.arraycopy(stack, position, stack, position + 1,
                    (currentPtr - position) + 1);
            currentPtr++;
            stack[position] = node;
        }
    }

    private void insertIntoListOfActiveFormattingElements(
            StackNode<T> formattingClone, int bookmark) {
        formattingClone.retain();
        assert listPtr + 1 < listOfActiveFormattingElements.length;
        if (bookmark <= listPtr) {
            System.arraycopy(listOfActiveFormattingElements, bookmark,
                    listOfActiveFormattingElements, bookmark + 1,
                    (listPtr - bookmark) + 1);
        }
        listPtr++;
        listOfActiveFormattingElements[bookmark] = formattingClone;
    }

    private int findInListOfActiveFormattingElements(StackNode<T> node) {
        for (int i = listPtr; i >= 0; i--) {
            if (node == listOfActiveFormattingElements[i]) {
                return i;
            }
        }
        return -1;
    }

    private int findInListOfActiveFormattingElementsContainsBetweenEndAndLastMarker(
            @Local String name) {
        for (int i = listPtr; i >= 0; i--) {
            StackNode<T> node = listOfActiveFormattingElements[i];
            if (node == null) {
                return -1;
            } else if (node.name == name) {
                return i;
            }
        }
        return -1;
    }


    private void maybeForgetEarlierDuplicateFormattingElement(
            @Local String name, HtmlAttributes attributes) throws SAXException {
        int candidate = -1;
        int count = 0;
        for (int i = listPtr; i >= 0; i--) {
            StackNode<T> node = listOfActiveFormattingElements[i];
            if (node == null) {
                break;
            }
            if (node.name == name && node.attributes.equalsAnother(attributes)) {
                candidate = i;
                ++count;
            }
        }
        if (count >= 3) {
            removeFromListOfActiveFormattingElements(candidate);
        }
    }

    private int findLastOrRoot(@Local String name) {
        for (int i = currentPtr; i > 0; i--) {
            if (stack[i].ns == "http://www.w3.org/1999/xhtml" && stack[i].name == name) {
                return i;
            }
        }
        return 0;
    }

    private int findLastOrRoot(int group) {
        for (int i = currentPtr; i > 0; i--) {
            if (stack[i].ns == "http://www.w3.org/1999/xhtml" && stack[i].getGroup() == group) {
                return i;
            }
        }
        return 0;
    }

    /**
     * Attempt to add attribute to the body element.
     * @param attributes the attributes
     * @return <code>true</code> iff the attributes were added
     * @throws SAXException
     */
    private boolean addAttributesToBody(HtmlAttributes attributes)
            throws SAXException {
        // [NOCPP[
        checkAttributes(attributes, "http://www.w3.org/1999/xhtml");
        // ]NOCPP]
        if (currentPtr >= 1) {
            StackNode<T> body = stack[1];
            if (body.getGroup() == TreeBuilder.BODY) {
                addAttributesToElement(body.node, attributes);
                return true;
            }
        }
        return false;
    }

    private void addAttributesToHtml(HtmlAttributes attributes)
            throws SAXException {
        // [NOCPP[
        checkAttributes(attributes, "http://www.w3.org/1999/xhtml");
        // ]NOCPP]
        addAttributesToElement(stack[0].node, attributes);
    }

    private void pushHeadPointerOntoStack() throws SAXException {
        assert headPointer != null;
        assert mode == AFTER_HEAD;
        fatal();
        silentPush(createStackNode(ElementName.HEAD, headPointer
        // [NOCPP[
                , errorHandler == null ? null : new TaintableLocatorImpl(tokenizer)
        // ]NOCPP]
        ));
    }

    /**
     * @throws SAXException
     *
     */
    private void reconstructTheActiveFormattingElements() throws SAXException {
        if (listPtr == -1) {
            return;
        }
        StackNode<T> mostRecent = listOfActiveFormattingElements[listPtr];
        if (mostRecent == null || isInStack(mostRecent)) {
            return;
        }
        int entryPos = listPtr;
        for (;;) {
            entryPos--;
            if (entryPos == -1) {
                break;
            }
            if (listOfActiveFormattingElements[entryPos] == null) {
                break;
            }
            if (isInStack(listOfActiveFormattingElements[entryPos])) {
                break;
            }
        }
        while (entryPos < listPtr) {
            entryPos++;
            StackNode<T> entry = listOfActiveFormattingElements[entryPos];
            StackNode<T> current = stack[currentPtr];

            T clone;
            if (current.isFosterParenting()) {
                clone = createAndInsertFosterParentedElement("http://www.w3.org/1999/xhtml", entry.name,
                        entry.attributes.cloneAttributes()
                        // CPPONLY: , htmlCreator(entry.getHtmlCreator())
                        );
            } else {
                T currentNode = nodeFromStackWithBlinkCompat(currentPtr);
                clone = createElement("http://www.w3.org/1999/xhtml", entry.name,
                        entry.attributes.cloneAttributes(), currentNode
                        // CPPONLY: , htmlCreator(entry.getHtmlCreator())
                        );
                appendElement(clone, currentNode);
            }

            StackNode<T> entryClone = createStackNode(entry.getFlags(),
                    entry.ns, entry.name, clone, entry.popName,
                    entry.attributes
                    // CPPONLY: , entry.getHtmlCreator()
                    // [NOCPP[
                    , entry.getLocator()
                    // ]NOCPP]
            );

            entry.dropAttributes(); // transfer ownership to entryClone

            push(entryClone);
            // stack takes ownership of the local variable
            listOfActiveFormattingElements[entryPos] = entryClone;
            // overwriting the old entry on the list, so release & retain
            entry.release(this);
            entryClone.retain();
        }
    }

    void notifyUnusedStackNode(int idxInStackNodes) {
        // stackNodesIdx is the earliest possible index of a stack node that might be unused,
        // so update the index if necessary.
        if (idxInStackNodes < stackNodesIdx) {
            stackNodesIdx = idxInStackNodes;
        }
    }

    @SuppressWarnings("unchecked") private StackNode<T> getUnusedStackNode() {
        // Search for an unused stack node.
        while (stackNodesIdx < numStackNodes) {
            if (stackNodes[stackNodesIdx].isUnused()) {
                return stackNodes[stackNodesIdx++];
            }
            stackNodesIdx++;
        }

        if (stackNodesIdx < stackNodes.length) {
            // No unused stack nodes, but there is still space in the storage array.
            stackNodes[stackNodesIdx] = new StackNode<T>(stackNodesIdx);
            numStackNodes++;
            return stackNodes[stackNodesIdx++];
        }

        // Could not find an unused stack node and storage array is full.
        StackNode<T>[] newStack = new StackNode[stackNodes.length + 64];
        System.arraycopy(stackNodes, 0, newStack, 0, stackNodes.length);
        stackNodes = newStack;

        // Create a new stack node and return it.
        stackNodes[stackNodesIdx] = new StackNode<T>(stackNodesIdx);
        numStackNodes++;
        return stackNodes[stackNodesIdx++];
    }

    private StackNode<T> createStackNode(int flags, @NsUri String ns, @Local String name, T node,
            @Local String popName, HtmlAttributes attributes
            // CPPONLY: , @HtmlCreator Object htmlCreator
            // [NOCPP[
            , TaintableLocatorImpl locator
            // ]NOCPP]
    ) {
        StackNode<T> instance = getUnusedStackNode();
        instance.setValues(flags, ns, name, node, popName, attributes
                // CPPONLY: , htmlCreator
                // [NOCPP[
                , locator
                // ]NOCPP]
        );
        return instance;
    }

    private StackNode<T> createStackNode(ElementName elementName, T node
            // [NOCPP[
            , TaintableLocatorImpl locator
            // ]NOCPP]
    ) {
        StackNode<T> instance = getUnusedStackNode();
        instance.setValues(elementName, node
                // [NOCPP[
                , locator
                // ]NOCPP]
        );
        return instance;
    }

    private StackNode<T> createStackNode(ElementName elementName, T node, HtmlAttributes attributes
            // [NOCPP[
            , TaintableLocatorImpl locator
            // ]NOCPP]
    ) {
        StackNode<T> instance = getUnusedStackNode();
        instance.setValues(elementName, node, attributes
                // [NOCPP[
                , locator
                // ]NOCPP]
        );
        return instance;
    }

    private StackNode<T> createStackNode(ElementName elementName, T node, @Local String popName
            // [NOCPP[
            , TaintableLocatorImpl locator
            // ]NOCPP]
    ) {
        StackNode<T> instance = getUnusedStackNode();
        instance.setValues(elementName, node, popName
                // [NOCPP[
                , locator
                // ]NOCPP]
        );
        return instance;
    }

    private StackNode<T> createStackNode(ElementName elementName, @Local String popName, T node
            // [NOCPP[
            , TaintableLocatorImpl locator
            // ]NOCPP]
    ) {
        StackNode<T> instance = getUnusedStackNode();
        instance.setValues(elementName, popName, node
                // [NOCPP[
                , locator
                // ]NOCPP]
        );
        return instance;
    }

    private StackNode<T> createStackNode(ElementName elementName, T node, @Local String popName,
            boolean markAsIntegrationPoint
            // [NOCPP[
            , TaintableLocatorImpl locator
            // ]NOCPP]
    ) {
        StackNode<T> instance = getUnusedStackNode();
        instance.setValues(elementName, node, popName, markAsIntegrationPoint
                // [NOCPP[
                , locator
                // ]NOCPP]
        );
        return instance;
    }

    private void insertIntoFosterParent(T child) throws SAXException {
        int tablePos = findLastOrRoot(TreeBuilder.TABLE);
        int templatePos = findLastOrRoot(TreeBuilder.TEMPLATE);

        if (templatePos >= tablePos) {
            appendElement(child, stack[templatePos].node);
            return;
        }

        StackNode<T> node = stack[tablePos];
        insertFosterParentedChild(child, node.node, stack[tablePos - 1].node);
    }

    private T createAndInsertFosterParentedElement(@NsUri String ns, @Local String name,
            HtmlAttributes attributes
            // CPPONLY: , @Creator Object creator
            ) throws SAXException {
        return createAndInsertFosterParentedElement(ns, name, attributes, null
                // CPPONLY: , creator
                );
    }

    private T createAndInsertFosterParentedElement(@NsUri String ns, @Local String name,
            HtmlAttributes attributes, T form
            // CPPONLY: , @Creator Object creator
            ) throws SAXException {
        int tablePos = findLastOrRoot(TreeBuilder.TABLE);
        int templatePos = findLastOrRoot(TreeBuilder.TEMPLATE);

        if (templatePos >= tablePos) {
            T child = createElement(ns, name, attributes, form, stack[templatePos].node
                    // CPPONLY: , creator
                    );
            appendElement(child, stack[templatePos].node);
            return child;
        }

        StackNode<T> node = stack[tablePos];
        return createAndInsertFosterParentedElement(ns, name, attributes, form, node.node, stack[tablePos - 1].node
                // CPPONLY: , creator
                );
    }

    private boolean isInStack(StackNode<T> node) {
        for (int i = currentPtr; i >= 0; i--) {
            if (stack[i] == node) {
                return true;
            }
        }
        return false;
    }

    private void popTemplateMode() {
        templateModePtr--;
    }

    private void pop() throws SAXException {
        StackNode<T> node = stack[currentPtr];
        assert debugOnlyClearLastStackSlot();
        if (node.getGroup() == OPTION) {
            optionElementPopped(node.node);
        }
        currentPtr--;
        elementPopped(node.ns, node.popName, node.node);
        node.release(this);
    }

    private void popForeign(int origPos, int eltPos) throws SAXException {
        StackNode<T> node = stack[currentPtr];
        if (origPos != currentPtr || eltPos != currentPtr) {
            markMalformedIfScript(node.node);
        }
        assert debugOnlyClearLastStackSlot();
        if (node.getGroup() == OPTION) {
            optionElementPopped(node.node);
        }
        currentPtr--;
        elementPopped(node.ns, node.popName, node.node);
        node.release(this);
    }

    private void silentPop() throws SAXException {
        StackNode<T> node = stack[currentPtr];
        assert debugOnlyClearLastStackSlot();
        assert node.getGroup() != OPTION;
        currentPtr--;
        node.release(this);
    }

    private void popOnEof() throws SAXException {
        StackNode<T> node = stack[currentPtr];
        assert debugOnlyClearLastStackSlot();
        if (node.getGroup() == OPTION) {
            optionElementPopped(node.node);
        }
        currentPtr--;
        markMalformedIfScript(node.node);
        elementPopped(node.ns, node.popName, node.node);
        node.release(this);
    }

    // [NOCPP[
    private void checkAttributes(HtmlAttributes attributes, @NsUri String ns)
            throws SAXException {
        if (errorHandler != null) {
            int len = attributes.getXmlnsLength();
            for (int i = 0; i < len; i++) {
                AttributeName name = attributes.getXmlnsAttributeName(i);
                if (name == AttributeName.XMLNS) {
                    String xmlns = attributes.getXmlnsValue(i);
                    if (!ns.equals(xmlns)) {
                        err("Bad value \u201C"
                                + xmlns
                                + "\u201D for the attribute \u201Cxmlns\u201D (only \u201C"
                                + ns + "\u201D permitted here).");
                        switch (namePolicy) {
                            case ALTER_INFOSET:
                                // fall through
                            case ALLOW:
                                warn("Attribute \u201Cxmlns\u201D is not serializable as XML 1.0.");
                                break;
                            case FATAL:
                                fatal("Attribute \u201Cxmlns\u201D is not serializable as XML 1.0.");
                                break;
                        }
                    }
                } else if (ns != "http://www.w3.org/1999/xhtml"
                        && name == AttributeName.XMLNS_XLINK) {
                    String xmlns = attributes.getXmlnsValue(i);
                    if (!"http://www.w3.org/1999/xlink".equals(xmlns)) {
                        err("Bad value \u201C"
                                + xmlns
                                + "\u201D for the attribute \u201Cxmlns:link\u201D (only \u201Chttp://www.w3.org/1999/xlink\u201D permitted here).");
                        switch (namePolicy) {
                            case ALTER_INFOSET:
                                // fall through
                            case ALLOW:
                                warn("Attribute \u201Cxmlns:xlink\u201D with a value other than \u201Chttp://www.w3.org/1999/xlink\u201D is not serializable as XML 1.0 without changing document semantics.");
                                break;
                            case FATAL:
                                fatal("Attribute \u201Cxmlns:xlink\u201D with a value other than \u201Chttp://www.w3.org/1999/xlink\u201D is not serializable as XML 1.0 without changing document semantics.");
                                break;
                        }
                    }
                } else {
                    err("Attribute \u201C" + attributes.getXmlnsLocalName(i)
                            + "\u201D not allowed here.");
                    switch (namePolicy) {
                        case ALTER_INFOSET:
                            // fall through
                        case ALLOW:
                            warn("Attribute with the local name \u201C"
                                    + attributes.getXmlnsLocalName(i)
                                    + "\u201D is not serializable as XML 1.0.");
                            break;
                        case FATAL:
                            fatal("Attribute with the local name \u201C"
                                    + attributes.getXmlnsLocalName(i)
                                    + "\u201D is not serializable as XML 1.0.");
                            break;
                    }
                }
            }
        }
        attributes.processNonNcNames(this, namePolicy);
    }

    private String checkPopName(@Local String name) throws SAXException {
        if (NCName.isNCName(name)) {
            return name;
        } else {
            switch (namePolicy) {
                case ALLOW:
                    warn("Element name \u201C" + name
                            + "\u201D cannot be represented as XML 1.0.");
                    return name;
                case ALTER_INFOSET:
                    warn("Element name \u201C" + name
                            + "\u201D cannot be represented as XML 1.0.");
                    return NCName.escapeName(name);
                case FATAL:
                    fatal("Element name \u201C" + name
                            + "\u201D cannot be represented as XML 1.0.");
            }
        }
        return null; // keep compiler happy
    }

    // ]NOCPP]

    private void appendHtmlElementToDocumentAndPush(HtmlAttributes attributes)
            throws SAXException {
        // [NOCPP[
        checkAttributes(attributes, "http://www.w3.org/1999/xhtml");
        // ]NOCPP]
        T elt = createHtmlElementSetAsRoot(attributes);
        StackNode<T> node = createStackNode(ElementName.HTML,
                elt
                // [NOCPP[
                , errorHandler == null ? null : new TaintableLocatorImpl(tokenizer)
        // ]NOCPP]
        );
        push(node);
    }

    private void appendHtmlElementToDocumentAndPush() throws SAXException {
        appendHtmlElementToDocumentAndPush(tokenizer.emptyAttributes());
    }

    private void appendToCurrentNodeAndPushHeadElement(HtmlAttributes attributes)
            throws SAXException {
        // [NOCPP[
        checkAttributes(attributes, "http://www.w3.org/1999/xhtml");
        // ]NOCPP]
        T currentNode = nodeFromStackWithBlinkCompat(currentPtr);
        T elt = createElement("http://www.w3.org/1999/xhtml", "head", attributes, currentNode
                /*
                 * head uses NS_NewHTMLSharedElement creator
                 */
                // CPPONLY: , htmlCreator(NS_NewHTMLSharedElement)
                );
        appendElement(elt, currentNode);
        headPointer = elt;
        StackNode<T> node = createStackNode(ElementName.HEAD,
                elt
                // [NOCPP[
                , errorHandler == null ? null : new TaintableLocatorImpl(tokenizer)
        // ]NOCPP]
        );
        push(node);
    }

    private void appendToCurrentNodeAndPushBodyElement(HtmlAttributes attributes)
            throws SAXException {
        appendToCurrentNodeAndPushElement(ElementName.BODY,
                attributes);
    }

    private void appendToCurrentNodeAndPushBodyElement() throws SAXException {
        appendToCurrentNodeAndPushBodyElement(tokenizer.emptyAttributes());
    }

    private void appendToCurrentNodeAndPushFormElementMayFoster(
            HtmlAttributes attributes) throws SAXException {
        // [NOCPP[
        checkAttributes(attributes, "http://www.w3.org/1999/xhtml");
        // ]NOCPP]

        T elt;
        StackNode<T> current = stack[currentPtr];
        if (current.isFosterParenting()) {
            fatal();
            elt = createAndInsertFosterParentedElement("http://www.w3.org/1999/xhtml", "form", attributes
                    // CPPONLY: , htmlCreator(NS_NewHTMLFormElement)
                    );
        } else {
            T currentNode = nodeFromStackWithBlinkCompat(currentPtr);
            elt = createElement("http://www.w3.org/1999/xhtml", "form", attributes, currentNode
                    // CPPONLY: , htmlCreator(NS_NewHTMLFormElement)
                    );
            appendElement(elt, currentNode);
        }

        if (!isTemplateContents()) {
            formPointer = elt;
        }

        StackNode<T> node = createStackNode(ElementName.FORM,
                elt
                // [NOCPP[
                , errorHandler == null ? null : new TaintableLocatorImpl(tokenizer)
                // ]NOCPP]
        );
        push(node);
    }

    private void appendToCurrentNodeAndPushFormattingElementMayFoster(
            ElementName elementName, HtmlAttributes attributes)
            throws SAXException {
        // [NOCPP[
        checkAttributes(attributes, "http://www.w3.org/1999/xhtml");
        // ]NOCPP]
        // This method can't be called for custom elements
        HtmlAttributes clone = attributes.cloneAttributes();
        // Attributes must not be read after calling createElement, because
        // createElement may delete attributes in C++.
        T elt;
        StackNode<T> current = stack[currentPtr];
        if (current.isFosterParenting()) {
            fatal();
            elt = createAndInsertFosterParentedElement("http://www.w3.org/1999/xhtml", elementName.getName(), attributes
                    // CPPONLY: , htmlCreator(elementName.getHtmlCreator())
                    );
        } else {
            T currentNode = nodeFromStackWithBlinkCompat(currentPtr);
            elt = createElement("http://www.w3.org/1999/xhtml", elementName.getName(), attributes, currentNode
                    // CPPONLY: , htmlCreator(elementName.getHtmlCreator())
                    );
            appendElement(elt, currentNode);
        }
        StackNode<T> node = createStackNode(elementName, elt, clone
                // [NOCPP[
                , errorHandler == null ? null : new TaintableLocatorImpl(tokenizer)
        // ]NOCPP]
        );
        push(node);
        append(node);
        node.retain(); // append doesn't retain itself
    }

    private void appendToCurrentNodeAndPushElement(ElementName elementName,
            HtmlAttributes attributes)
            throws SAXException {
        // [NOCPP[
        checkAttributes(attributes, "http://www.w3.org/1999/xhtml");
        // ]NOCPP]
        // This method can't be called for custom elements
        T currentNode = nodeFromStackWithBlinkCompat(currentPtr);
        T elt = createElement("http://www.w3.org/1999/xhtml", elementName.getName(), attributes, currentNode
                // CPPONLY: , htmlCreator(elementName.getHtmlCreator())
                );
        if (ElementName.TEMPLATE == elementName) {
            T root = getDeclarativeShadowRoot(currentNode, elt, attributes);
            if (root != null) {
                setDocumentFragmentForTemplate(elt, root);
                elt = root;
            } else {
                appendElement(elt, currentNode);
                elt = getDocumentFragmentForTemplate(elt);
            }
        } else {
            appendElement(elt, currentNode);
        }
        StackNode<T> node = createStackNode(elementName, elt
                // [NOCPP[
                , errorHandler == null ? null : new TaintableLocatorImpl(tokenizer)
        // ]NOCPP]
        );
        push(node);
    }

    private void appendToCurrentNodeAndPushElementMayFoster(ElementName elementName,
            HtmlAttributes attributes)
            throws SAXException {
        @Local String popName = elementName.getName();
        // [NOCPP[
        checkAttributes(attributes, "http://www.w3.org/1999/xhtml");
        if (!elementName.isInterned()) {
            popName = checkPopName(popName);
        }
        // ]NOCPP]
        T elt;
        StackNode<T> current = stack[currentPtr];
        if (current.isFosterParenting()) {
            fatal();
            elt = createAndInsertFosterParentedElement("http://www.w3.org/1999/xhtml", popName, attributes
                    // CPPONLY: , htmlCreator(elementName.getHtmlCreator())
                    );
        } else {
            T currentNode = nodeFromStackWithBlinkCompat(currentPtr);
            elt = createElement("http://www.w3.org/1999/xhtml", popName, attributes, currentNode
                    // CPPONLY: , htmlCreator(elementName.getHtmlCreator())
                    );
            appendElement(elt, currentNode);
        }
        StackNode<T> node = createStackNode(elementName, elt, popName
                // [NOCPP[
                , errorHandler == null ? null : new TaintableLocatorImpl(tokenizer)
        // ]NOCPP]
        );
        push(node);
    }

    private void appendToCurrentNodeAndPushElementMayFosterMathML(
            ElementName elementName, HtmlAttributes attributes)
            throws SAXException {
        @Local String popName = elementName.getName();
        // [NOCPP[
        checkAttributes(attributes, "http://www.w3.org/1998/Math/MathML");
        if (!elementName.isInterned()) {
            popName = checkPopName(popName);
        }
        // ]NOCPP]
        boolean markAsHtmlIntegrationPoint = false;
        if (ElementName.ANNOTATION_XML == elementName
                && annotationXmlEncodingPermitsHtml(attributes)) {
            markAsHtmlIntegrationPoint = true;
        }
        // Attributes must not be read after calling createElement(), since
        // createElement may delete the object in C++.
        T elt;
        StackNode<T> current = stack[currentPtr];
        if (current.isFosterParenting()) {
            fatal();
            elt = createAndInsertFosterParentedElement("http://www.w3.org/1998/Math/MathML", popName, attributes
                    // CPPONLY: , htmlCreator(null)
                    );
        } else {
            T currentNode = nodeFromStackWithBlinkCompat(currentPtr);
            elt  = createElement("http://www.w3.org/1998/Math/MathML", popName, attributes, currentNode
                    // CPPONLY: , htmlCreator(null)
                    );
            appendElement(elt, currentNode);
        }
        StackNode<T> node = createStackNode(elementName, elt, popName,
                markAsHtmlIntegrationPoint
                // [NOCPP[
                , errorHandler == null ? null : new TaintableLocatorImpl(tokenizer)
        // ]NOCPP]
        );
        push(node);
    }

    // [NOCPP[
    T getDocumentFragmentForTemplate(T template) {
        return template;
    }

    void setDocumentFragmentForTemplate(T template, T fragment) {
    }

    T getShadowRootFromHost(T host, T template, String shadowRootMode,
            boolean shadowRootIsClonable, boolean shadowRootIsSerializable, boolean shadowRootDelegatesFocus,
            boolean shadowRootCustomElementRegistry, String shadowRootSlotAssignment,
            String shadowRootReferenceTarget) {
        return null;
    }

    T getFormPointerForContext(T context) {
        return null;
    }
    // ]NOCPP]

    private boolean annotationXmlEncodingPermitsHtml(HtmlAttributes attributes) {
        String encoding = attributes.getValue(AttributeName.ENCODING);
        if (encoding == null) {
            return false;
        }
        return Portability.lowerCaseLiteralEqualsIgnoreAsciiCaseString(
                "application/xhtml+xml", encoding)
                || Portability.lowerCaseLiteralEqualsIgnoreAsciiCaseString(
                        "text/html", encoding);
    }

    private void appendToCurrentNodeAndPushElementMayFosterSVG(
            ElementName elementName, HtmlAttributes attributes)
            throws SAXException {
        @Local String popName = elementName.getCamelCaseName();
        // [NOCPP[
        checkAttributes(attributes, "http://www.w3.org/2000/svg");
        if (!elementName.isInterned()) {
            popName = checkPopName(popName);
        }
        // ]NOCPP]
        T elt;
        StackNode<T> current = stack[currentPtr];
        if (current.isFosterParenting()) {
            fatal();
            elt = createAndInsertFosterParentedElement("http://www.w3.org/2000/svg", popName, attributes
                    // CPPONLY: , svgCreator(elementName.getSvgCreator())
                    );
        } else {
            T currentNode = nodeFromStackWithBlinkCompat(currentPtr);
            elt = createElement("http://www.w3.org/2000/svg", popName, attributes, currentNode
                    // CPPONLY: , svgCreator(elementName.getSvgCreator())
                    );
            appendElement(elt, currentNode);
        }
        StackNode<T> node = createStackNode(elementName, popName, elt
                // [NOCPP[
                , errorHandler == null ? null : new TaintableLocatorImpl(tokenizer)
        // ]NOCPP]
        );
        push(node);
    }

    private void appendToCurrentNodeAndPushElementMayFoster(ElementName elementName,
            HtmlAttributes attributes, T form)
            throws SAXException {
        // [NOCPP[
        checkAttributes(attributes, "http://www.w3.org/1999/xhtml");
        // ]NOCPP]
        // Can't be called for custom elements
        T elt;
        T formOwner = form == null || fragment || isTemplateContents() ? null : form;
        StackNode<T> current = stack[currentPtr];
        if (current.isFosterParenting()) {
            fatal();
            elt = createAndInsertFosterParentedElement("http://www.w3.org/1999/xhtml", elementName.getName(),
                    attributes, formOwner
                    // CPPONLY: , htmlCreator(elementName.getHtmlCreator())
                    );
        } else {
            T currentNode = nodeFromStackWithBlinkCompat(currentPtr);
            elt = createElement("http://www.w3.org/1999/xhtml", elementName.getName(),
                    attributes, formOwner, currentNode
                    // CPPONLY: , htmlCreator(elementName.getHtmlCreator())
                    );
            appendElement(elt, currentNode);
        }
        StackNode<T> node = createStackNode(elementName, elt
                // [NOCPP[
                , errorHandler == null ? null : new TaintableLocatorImpl(tokenizer)
        // ]NOCPP]
        );
        push(node);
    }

    private void appendVoidElementToCurrent(
            ElementName elementName, HtmlAttributes attributes)
            throws SAXException {
        @Local String popName = elementName.getName();
        // [NOCPP[
        checkAttributes(attributes, "http://www.w3.org/1999/xhtml");
        if (!elementName.isInterned()) {
            popName = checkPopName(popName);
        }
        // ]NOCPP]
        T currentNode = nodeFromStackWithBlinkCompat(currentPtr);
        T elt = createElement("http://www.w3.org/1999/xhtml", popName, attributes, currentNode
                // CPPONLY: , htmlCreator(elementName.getHtmlCreator())
                );
        appendElement(elt, currentNode);
        elementPushed("http://www.w3.org/1999/xhtml", popName, elt);
        elementPopped("http://www.w3.org/1999/xhtml", popName, elt);
    }

    private void appendVoidElementToCurrentMayFoster(
            ElementName elementName, HtmlAttributes attributes, T form) throws SAXException {
        @Local String name = elementName.getName();
        // [NOCPP[
        checkAttributes(attributes, "http://www.w3.org/1999/xhtml");
        // ]NOCPP]
        // Can't be called for custom elements
        T elt;
        T formOwner = form == null || fragment || isTemplateContents() ? null : form;
        StackNode<T> current = stack[currentPtr];
        if (current.isFosterParenting()) {
            fatal();
            elt = createAndInsertFosterParentedElement("http://www.w3.org/1999/xhtml", name,
                    attributes, formOwner
                    // CPPONLY: , htmlCreator(elementName.getHtmlCreator())
                    );
        } else {
            T currentNode = nodeFromStackWithBlinkCompat(currentPtr);
            elt = createElement("http://www.w3.org/1999/xhtml", name,

                    attributes, formOwner, currentNode
                    // CPPONLY: , htmlCreator(elementName.getHtmlCreator())
                    );
            appendElement(elt, currentNode);
        }
        elementPushed("http://www.w3.org/1999/xhtml", name, elt);
        elementPopped("http://www.w3.org/1999/xhtml", name, elt);
    }

    private void appendVoidElementToCurrentMayFoster(
            ElementName elementName, HtmlAttributes attributes)
            throws SAXException {
        @Local String popName = elementName.getName();
        // [NOCPP[
        checkAttributes(attributes, "http://www.w3.org/1999/xhtml");
        if (!elementName.isInterned()) {
            popName = checkPopName(popName);
        }
        // ]NOCPP]
        T elt;
        StackNode<T> current = stack[currentPtr];
        if (current.isFosterParenting()) {
            fatal();
            elt = createAndInsertFosterParentedElement("http://www.w3.org/1999/xhtml", popName, attributes
                    // CPPONLY: , htmlCreator(elementName.getHtmlCreator())
                    );
        } else {
            T currentNode = nodeFromStackWithBlinkCompat(currentPtr);
            elt = createElement("http://www.w3.org/1999/xhtml", popName, attributes, currentNode
                    // CPPONLY: , htmlCreator(elementName.getHtmlCreator())
                    );
            appendElement(elt, currentNode);
        }
        elementPushed("http://www.w3.org/1999/xhtml", popName, elt);
        elementPopped("http://www.w3.org/1999/xhtml", popName, elt);
    }

    private void appendVoidElementToCurrentMayFosterSVG(
            ElementName elementName, HtmlAttributes attributes)
            throws SAXException {
        @Local String popName = elementName.getCamelCaseName();
        // [NOCPP[
        checkAttributes(attributes, "http://www.w3.org/2000/svg");
        if (!elementName.isInterned()) {
            popName = checkPopName(popName);
        }
        // ]NOCPP]
        T elt;
        StackNode<T> current = stack[currentPtr];
        if (current.isFosterParenting()) {
            fatal();
            elt = createAndInsertFosterParentedElement("http://www.w3.org/2000/svg", popName, attributes
                    // CPPONLY: , svgCreator(elementName.getSvgCreator())
                    );
        } else {
            T currentNode = nodeFromStackWithBlinkCompat(currentPtr);
            elt = createElement("http://www.w3.org/2000/svg", popName, attributes, currentNode
                    // CPPONLY: , svgCreator(elementName.getSvgCreator())
                    );
            appendElement(elt, currentNode);
        }
        elementPushed("http://www.w3.org/2000/svg", popName, elt);
        elementPopped("http://www.w3.org/2000/svg", popName, elt);
    }

    private void appendVoidElementToCurrentMayFosterMathML(
            ElementName elementName, HtmlAttributes attributes)
            throws SAXException {
        @Local String popName = elementName.getName();
        // [NOCPP[
        checkAttributes(attributes, "http://www.w3.org/1998/Math/MathML");
        if (!elementName.isInterned()) {
            popName = checkPopName(popName);
        }
        // ]NOCPP]
        T elt;
        StackNode<T> current = stack[currentPtr];
        if (current.isFosterParenting()) {
            fatal();
            elt = createAndInsertFosterParentedElement("http://www.w3.org/1998/Math/MathML", popName, attributes
                    // CPPONLY: , htmlCreator(null)
                    );
        } else {
            T currentNode = nodeFromStackWithBlinkCompat(currentPtr);
            elt = createElement("http://www.w3.org/1998/Math/MathML", popName, attributes, currentNode
                    // CPPONLY: , htmlCreator(null)
                    );
            appendElement(elt, currentNode);
        }
        elementPushed("http://www.w3.org/1998/Math/MathML", popName, elt);
        elementPopped("http://www.w3.org/1998/Math/MathML", popName, elt);
    }

    private void appendVoidInputToCurrent(HtmlAttributes attributes, T form) throws SAXException {
        // [NOCPP[
        checkAttributes(attributes, "http://www.w3.org/1999/xhtml");
        // ]NOCPP]
        // Can't be called for custom elements
        T currentNode = nodeFromStackWithBlinkCompat(currentPtr);
        T elt = createElement("http://www.w3.org/1999/xhtml", "input", attributes,
                form == null || fragment || isTemplateContents() ? null : form, currentNode
                        // CPPONLY: , htmlCreator(NS_NewHTMLInputElement)
                        );
        appendElement(elt, currentNode);
        elementPushed("http://www.w3.org/1999/xhtml", "input", elt);
        elementPopped("http://www.w3.org/1999/xhtml", "input", elt);
    }

    private void appendVoidFormToCurrent(HtmlAttributes attributes) throws SAXException {
        // [NOCPP[
        checkAttributes(attributes, "http://www.w3.org/1999/xhtml");
        // ]NOCPP]
        T currentNode = nodeFromStackWithBlinkCompat(currentPtr);
        T elt = createElement("http://www.w3.org/1999/xhtml", "form",
                attributes, currentNode
                // CPPONLY: , htmlCreator(NS_NewHTMLFormElement)
                );
        formPointer = elt;
        // ownership transferred to form pointer
        appendElement(elt, currentNode);
        elementPushed("http://www.w3.org/1999/xhtml", "form", elt);
        elementPopped("http://www.w3.org/1999/xhtml", "form", elt);
    }

    // [NOCPP[

    private final void accumulateCharactersForced(@Const @NoLength char[] buf,
            int start, int length) throws SAXException {
        System.arraycopy(buf, start, charBuffer, charBufferLen, length);
        charBufferLen += length;
    }

    @Override public void ensureBufferSpace(int inputLength)
            throws SAXException {
        // TODO: Unify Tokenizer.strBuf and TreeBuilder.charBuffer so that
        // this method becomes unnecessary.
        int worstCase = charBufferLen + inputLength;
        if (charBuffer == null) {
            // Add an arbitrary small value to avoid immediate reallocation
            // once there are a few characters in the buffer.
            charBuffer = new char[worstCase + 128];
        } else if (worstCase > charBuffer.length) {
            // HotSpot reportedly allocates memory with 8-byte accuracy, so
            // there's no point in trying to do math here to avoid slop.
            // Maybe we should add some small constant to worstCase here
            // but not doing that without profiling. In C++ with jemalloc,
            // the corresponding method should do math to round up here
            // to avoid slop.
            char[] newBuf = new char[worstCase];
            System.arraycopy(charBuffer, 0, newBuf, 0, charBufferLen);
            charBuffer = newBuf;
        }
    }

    // ]NOCPP]

    protected void accumulateCharacters(@Const @NoLength char[] buf, int start,
            int length) throws SAXException {
        appendCharacters(stack[currentPtr].node, buf, start, length);
    }

    // ------------------------------- //

    protected final void requestSuspension() {
        tokenizer.requestSuspension();
    }

    protected abstract T createElement(@NsUri String ns, @Local String name,
            HtmlAttributes attributes, T intendedParent
            // CPPONLY: , @Creator Object creator
            ) throws SAXException;

    protected T createElement(@NsUri String ns, @Local String name,
            HtmlAttributes attributes, T form, T intendedParent
            // CPPONLY: , @Creator Object creator
            ) throws SAXException {
        return createElement("http://www.w3.org/1999/xhtml", name, attributes, intendedParent
                // CPPONLY: , creator
                );
    }

    protected abstract T createHtmlElementSetAsRoot(HtmlAttributes attributes)
            throws SAXException;

    protected abstract void detachFromParent(T element) throws SAXException;

    protected void optionElementPopped(T option) throws SAXException {
    }

    protected abstract boolean hasChildren(T element) throws SAXException;

    protected abstract void appendElement(T child, T newParent)
            throws SAXException;

    protected abstract void appendChildrenToNewParent(T oldParent, T newParent)
            throws SAXException;

    protected abstract void insertFosterParentedChild(T child, T table,
            T stackParent) throws SAXException;

    // We don't generate CPP code for this method because it is not used in generated CPP
    // code. Instead, the form owner version of this method is called with a null form owner.
    // [NOCPP[

    protected abstract T createAndInsertFosterParentedElement(@NsUri String ns, @Local String name,
            HtmlAttributes attributes, T table, T stackParent) throws SAXException;

    // ]NOCPP]

    protected T createAndInsertFosterParentedElement(@NsUri String ns, @Local String name,
            HtmlAttributes attributes, T form, T table, T stackParent
            // CPPONLY: , @Creator Object creator
            ) throws SAXException {
        return createAndInsertFosterParentedElement(ns, name, attributes, table, stackParent);
    };

    protected abstract void insertFosterParentedCharacters(
            @NoLength char[] buf, int start, int length, T table, T stackParent)
            throws SAXException;

    protected abstract void appendCharacters(T parent, @NoLength char[] buf,
            int start, int length) throws SAXException;

    protected abstract void appendComment(T parent, @NoLength char[] buf,
            int start, int length) throws SAXException;

    protected abstract void appendCommentToDocument(@NoLength char[] buf,
            int start, int length) throws SAXException;

    protected abstract void addAttributesToElement(T element,
            HtmlAttributes attributes) throws SAXException;

    protected void markMalformedIfScript(T elt) throws SAXException {

    }

    protected void start(boolean fragmentMode) throws SAXException {

    }

    protected void end() throws SAXException {

    }

    protected void appendDoctypeToDocument(@Local String name,
            String publicIdentifier, String systemIdentifier)
            throws SAXException {

    }

    protected void elementPushed(@NsUri String ns, @Local String name, T node)
            throws SAXException {

    }

    protected void elementPopped(@NsUri String ns, @Local String name, T node)
            throws SAXException {

    }

    // [NOCPP[

    protected void documentMode(DocumentMode m, String publicIdentifier,
            String systemIdentifier)
            throws SAXException {

    }

    /**
     * @see nu.validator.htmlparser.common.TokenHandler#wantsComments()
     */
    public boolean wantsComments() {
        return wantingComments;
    }

    public void setIgnoringComments(boolean ignoreComments) {
        wantingComments = !ignoreComments;
    }

    /**
     * Sets the errorHandler.
     *
     * @param errorHandler
     *            the errorHandler to set
     */
    public final void setErrorHandler(ErrorHandler errorHandler) {
        this.errorHandler = errorHandler;
    }

    /**
     * Returns the errorHandler.
     *
     * @return the errorHandler
     */
    public ErrorHandler getErrorHandler() {
        return errorHandler;
    }

    /**
     * The argument MUST be an interned string or <code>null</code>.
     *
     * @param context
     */
    public final void setFragmentContext(@Local String context) {
        this.contextName = context;
        this.contextNamespace = "http://www.w3.org/1999/xhtml";
        this.contextNode = null;
        this.fragment = (contextName != null);
        this.quirks = false;
    }

    // ]NOCPP]

    /**
     * @see nu.validator.htmlparser.common.TokenHandler#cdataSectionAllowed()
     */
    @Inline public boolean cdataSectionAllowed() throws SAXException {
        return isInForeign();
    }

    private boolean isInForeign() {
        return currentPtr >= 0
                && stack[currentPtr].ns != "http://www.w3.org/1999/xhtml";
    }

    private boolean isInForeignButNotHtmlOrMathTextIntegrationPoint() {
        if (currentPtr < 0) {
            return false;
        }
        return !isSpecialParentInForeign(stack[currentPtr]);
    }

    /**
     * The argument MUST be an interned string or <code>null</code>.
     *
     * @param context
     */
    public final void setFragmentContext(@Local String context,
            @NsUri String ns, T node, boolean quirks) {
        // [NOCPP[
        if (!((context == null && ns == null)
                || "http://www.w3.org/1999/xhtml" == ns
                || "http://www.w3.org/2000/svg" == ns || "http://www.w3.org/1998/Math/MathML" == ns)) {
            throw new IllegalArgumentException(
                    "The namespace must be the HTML, SVG or MathML namespace (or null when the local name is null). Got: "
                            + ns);
        }
        // ]NOCPP]
        this.contextName = context;
        this.contextNamespace = ns;
        this.contextNode = node;
        this.fragment = (contextName != null);
        this.quirks = quirks;
    }

    protected final T currentNode() {
        return stack[currentPtr].node;
    }

    /**
     * Returns the scriptingEnabled.
     *
     * @return the scriptingEnabled
     */
    public boolean isScriptingEnabled() {
        return scriptingEnabled;
    }

    /**
     * Sets the scriptingEnabled.
     *
     * @param scriptingEnabled
     *            the scriptingEnabled to set
     */
    public void setScriptingEnabled(boolean scriptingEnabled) {
        this.scriptingEnabled = scriptingEnabled;
    }

    public void setForceNoQuirks(boolean forceNoQuirks) {
        this.forceNoQuirks = forceNoQuirks;
    }

    // Redundant method retained because previously public.
    public void setIsSrcdocDocument(boolean isSrcdocDocument) {
        this.setForceNoQuirks(isSrcdocDocument);
    }

    public boolean isAllowDeclarativeShadowRoots() {
        return allowDeclarativeShadowRoots;
    }

    public void setAllowDeclarativeShadowRoots(boolean allow) {
        allowDeclarativeShadowRoots = allow;
    }

    public boolean isNoInSelectMode() {
        return noInSelectMode;
    }

    public void setNoInSelectMode(boolean mode) {
        noInSelectMode = mode;
    }

    // [NOCPP[

    public void setNamePolicy(XmlViolationPolicy namePolicy) {
        this.namePolicy = namePolicy;
    }

    /**
     * Sets the documentModeHandler.
     *
     * @param documentModeHandler
     *            the documentModeHandler to set
     */
    public void setDocumentModeHandler(DocumentModeHandler documentModeHandler) {
        this.documentModeHandler = documentModeHandler;
    }

    /**
     * Sets the reportingDoctype.
     *
     * @param reportingDoctype
     *            the reportingDoctype to set
     */
    public void setReportingDoctype(boolean reportingDoctype) {
        this.reportingDoctype = reportingDoctype;
    }

    // ]NOCPP]

    /**
     * Flushes the pending characters. Public for document.write use cases only.
     * @throws SAXException
     */
    public final void flushCharacters() throws SAXException {
        if (charBufferLen > 0) {
            if ((mode == IN_TABLE || mode == IN_TABLE_BODY || mode == IN_ROW)
                    && charBufferContainsNonWhitespace()) {
                errNonSpaceInTable();
                reconstructTheActiveFormattingElements();
                if (!stack[currentPtr].isFosterParenting()) {
                    // reconstructing gave us a new current node
                    appendCharacters(currentNode(), charBuffer, 0,
                            charBufferLen);
                    charBufferLen = 0;
                    return;
                }

                int tablePos = findLastOrRoot(TreeBuilder.TABLE);
                int templatePos = findLastOrRoot(TreeBuilder.TEMPLATE);

                if (templatePos >= tablePos) {
                    appendCharacters(stack[templatePos].node, charBuffer, 0, charBufferLen);
                    charBufferLen = 0;
                    return;
                }

                StackNode<T> tableElt = stack[tablePos];
                insertFosterParentedCharacters(charBuffer, 0, charBufferLen,
                        tableElt.node, stack[tablePos - 1].node);
                charBufferLen = 0;
                return;
            }
            appendCharacters(currentNode(), charBuffer, 0, charBufferLen);
            charBufferLen = 0;
        }
    }

    private boolean charBufferContainsNonWhitespace() {
        for (int i = 0; i < charBufferLen; i++) {
            switch (charBuffer[i]) {
                case ' ':
                case '\t':
                case '\n':
                case '\r':
                case '\u000C':
                    continue;
                default:
                    return true;
            }
        }
        return false;
    }

    /**
     * Creates a comparable snapshot of the tree builder state. Snapshot
     * creation is only supported immediately after a script end tag has been
     * processed. In C++ the caller is responsible for calling
     * <code>delete</code> on the returned object.
     *
     * @return a snapshot.
     * @throws SAXException
     */
    @SuppressWarnings("unchecked") public TreeBuilderState<T> newSnapshot()
            throws SAXException {
        StackNode<T>[] listCopy = new StackNode[listPtr + 1];
        for (int i = 0; i < listCopy.length; i++) {
            StackNode<T> node = listOfActiveFormattingElements[i];
            if (node != null) {
                StackNode<T> newNode = new StackNode<T>(-1);
                newNode.setValues(node.getFlags(), node.ns,
                        node.name, node.node, node.popName,
                        node.attributes.cloneAttributes()
                        // CPPONLY: , node.getHtmlCreator()
                        // [NOCPP[
                        , node.getLocator()
                        // ]NOCPP]
                );
                listCopy[i] = newNode;
            } else {
                listCopy[i] = null;
            }
        }
        StackNode<T>[] stackCopy = new StackNode[currentPtr + 1];
        for (int i = 0; i < stackCopy.length; i++) {
            StackNode<T> node = stack[i];
            int listIndex = findInListOfActiveFormattingElements(node);
            if (listIndex == -1) {
                StackNode<T> newNode = new StackNode<T>(-1);
                newNode.setValues(node.getFlags(), node.ns,
                        node.name, node.node, node.popName,
                        null
                        // CPPONLY: , node.getHtmlCreator()
                        // [NOCPP[
                        , node.getLocator()
                        // ]NOCPP]
                );
                stackCopy[i] = newNode;
            } else {
                stackCopy[i] = listCopy[listIndex];
                stackCopy[i].retain();
            }
        }
        int[] templateModeStackCopy = new int[templateModePtr + 1];
        System.arraycopy(templateModeStack, 0, templateModeStackCopy, 0,
                templateModeStackCopy.length);
        return new StateSnapshot<T>(stackCopy, listCopy, templateModeStackCopy, formPointer,
                headPointer, mode, originalMode, framesetOk,
                needToDropLF, quirks);
    }

    public boolean snapshotMatches(TreeBuilderState<T> snapshot) {
        StackNode<T>[] stackCopy = snapshot.getStack();
        int stackLen = snapshot.getStackLength();
        StackNode<T>[] listCopy = snapshot.getListOfActiveFormattingElements();
        int listLen = snapshot.getListOfActiveFormattingElementsLength();
        int[] templateModeStackCopy = snapshot.getTemplateModeStack();
        int templateModeStackLen = snapshot.getTemplateModeStackLength();

        if (stackLen != currentPtr + 1
                || listLen != listPtr + 1
                || templateModeStackLen != templateModePtr + 1
                || formPointer != snapshot.getFormPointer()
                || headPointer != snapshot.getHeadPointer()
                || mode != snapshot.getMode()
                || originalMode != snapshot.getOriginalMode()
                || framesetOk != snapshot.isFramesetOk()
                || needToDropLF != snapshot.isNeedToDropLF()
                || quirks != snapshot.isQuirks()) { // maybe just assert quirks
            return false;
        }
        for (int i = listLen - 1; i >= 0; i--) {
            if (listCopy[i] == null
                    && listOfActiveFormattingElements[i] == null) {
                continue;
            } else if (listCopy[i] == null
                    || listOfActiveFormattingElements[i] == null) {
                return false;
            }
            if (listCopy[i].node != listOfActiveFormattingElements[i].node) {
                return false; // it's possible that this condition is overly
                              // strict
            }
        }
        for (int i = stackLen - 1; i >= 0; i--) {
            if (stackCopy[i].node != stack[i].node) {
                return false;
            }
        }
        for (int i = templateModeStackLen - 1; i >=0; i--) {
            if (templateModeStackCopy[i] != templateModeStack[i]) {
                return false;
            }
        }
        return true;
    }

    @SuppressWarnings("unchecked") public void loadState(
            TreeBuilderState<T> snapshot)
            throws SAXException {
        // CPPONLY: mCurrentHtmlScriptCannotDocumentWriteOrBlock = false;
        StackNode<T>[] stackCopy = snapshot.getStack();
        int stackLen = snapshot.getStackLength();
        StackNode<T>[] listCopy = snapshot.getListOfActiveFormattingElements();
        int listLen = snapshot.getListOfActiveFormattingElementsLength();
        int[] templateModeStackCopy = snapshot.getTemplateModeStack();
        int templateModeStackLen = snapshot.getTemplateModeStackLength();

        for (int i = 0; i <= listPtr; i++) {
            if (listOfActiveFormattingElements[i] != null) {
                listOfActiveFormattingElements[i].release(this);
            }
        }
        if (listOfActiveFormattingElements.length < listLen) {
            listOfActiveFormattingElements = new StackNode[listLen];
        }
        listPtr = listLen - 1;

        for (int i = 0; i <= currentPtr; i++) {
            stack[i].release(this);
        }
        if (stack.length < stackLen) {
            stack = new StackNode[stackLen];
        }
        currentPtr = stackLen - 1;

        if (templateModeStack.length < templateModeStackLen) {
            templateModeStack = new int[templateModeStackLen];
        }
        templateModePtr = templateModeStackLen - 1;

        for (int i = 0; i < listLen; i++) {
            StackNode<T> node = listCopy[i];
            if (node != null) {
                StackNode<T> newNode = createStackNode(node.getFlags(), node.ns,
                        node.name, node.node,
                        node.popName,
                        node.attributes.cloneAttributes()
                        // CPPONLY: , node.getHtmlCreator()
                        // [NOCPP[
                        , node.getLocator()
                // ]NOCPP]
                );
                listOfActiveFormattingElements[i] = newNode;
            } else {
                listOfActiveFormattingElements[i] = null;
            }
        }
        for (int i = 0; i < stackLen; i++) {
            StackNode<T> node = stackCopy[i];
            int listIndex = findInArray(node, listCopy);
            if (listIndex == -1) {
                StackNode<T> newNode = createStackNode(node.getFlags(), node.ns,
                        node.name, node.node,
                        node.popName,
                        null
                        // CPPONLY: , node.getHtmlCreator()
                        // [NOCPP[
                        , node.getLocator()
                // ]NOCPP]
                );
                stack[i] = newNode;
            } else {
                stack[i] = listOfActiveFormattingElements[listIndex];
                stack[i].retain();
            }
        }
        System.arraycopy(templateModeStackCopy, 0, templateModeStack, 0, templateModeStackLen);
        formPointer = snapshot.getFormPointer();
        headPointer = snapshot.getHeadPointer();
        mode = snapshot.getMode();
        originalMode = snapshot.getOriginalMode();
        framesetOk = snapshot.isFramesetOk();
        needToDropLF = snapshot.isNeedToDropLF();
        quirks = snapshot.isQuirks();
    }

    private int findInArray(StackNode<T> node, StackNode<T>[] arr) {
        for (int i = listPtr; i >= 0; i--) {
            if (node == arr[i]) {
                return i;
            }
        }
        return -1;
    }

    /**
     * Returns <code>stack[stackPos].node</code> if <code>stackPos</code> is
     * smaller than Blink's magic limit or the node at Blink's magic limit
     * otherwise.
     *
     * In order to get Blink-compatible handling of excessive deeply-nested
     * markup, this method must be used to obtain the node that is used as the
     * parent node of an insertion.
     *
     * Blink's magic number is 512, but our counting is off by one compared to
     * Blink's way of counting, so in order to get the same
     * externally-observable outcome, we use 511 as our magic number.
     *
     * @param stackPos the stack position to attempt to read
     * @return node at the position capped to Blink's magic number
     * @throws SAXException
     */
    private T nodeFromStackWithBlinkCompat(int stackPos) throws SAXException {
        // Magic number if off by one relative to Blink's magic number, but the
        // outcome is the same, because the counting is different by one.
        if (stackPos > 511) {
            errDeepTree();
            return stack[511].node;
        }
        return stack[stackPos].node;
    }
    /**
     * @see nu.validator.htmlparser.impl.TreeBuilderState#getFormPointer()
     */
    @Override
    public T getFormPointer() {
        return formPointer;
    }

    /**
     * Returns the headPointer.
     *
     * @return the headPointer
     */
    @Override
    public T getHeadPointer() {
        return headPointer;
    }

    /**
     * @see nu.validator.htmlparser.impl.TreeBuilderState#getListOfActiveFormattingElements()
     */
    @Override
    public StackNode<T>[] getListOfActiveFormattingElements() {
        return listOfActiveFormattingElements;
    }

    /**
     * @see nu.validator.htmlparser.impl.TreeBuilderState#getStack()
     */
    @Override
    public StackNode<T>[] getStack() {
        return stack;
    }

    /**
     * @see nu.validator.htmlparser.impl.TreeBuilderState#getTemplateModeStack()
     */
    @Override
    public int[] getTemplateModeStack() {
        return templateModeStack;
    }

    /**
     * Returns the mode.
     *
     * @return the mode
     */
    @Override
    public int getMode() {
        return mode;
    }

    /**
     * Returns the originalMode.
     *
     * @return the originalMode
     */
    @Override
    public int getOriginalMode() {
        return originalMode;
    }

    /**
     * Returns the framesetOk.
     *
     * @return the framesetOk
     */
    @Override
    public boolean isFramesetOk() {
        return framesetOk;
    }

    /**
     * Returns the needToDropLF.
     *
     * @return the needToDropLF
     */
    @Override
    public boolean isNeedToDropLF() {
        return needToDropLF;
    }

    /**
     * Returns the quirks.
     *
     * @return the quirks
     */
    @Override
    public boolean isQuirks() {
        return quirks;
    }

    /**
     * @see nu.validator.htmlparser.impl.TreeBuilderState#getListOfActiveFormattingElementsLength()
     */
    @Override
    public int getListOfActiveFormattingElementsLength() {
        return listPtr + 1;
    }

    /**
     * @see nu.validator.htmlparser.impl.TreeBuilderState#getStackLength()
     */
    @Override
    public int getStackLength() {
        return currentPtr + 1;
    }

    /**
     * @see nu.validator.htmlparser.impl.TreeBuilderState#getTemplateModeStackLength()
     */
    @Override
    public int getTemplateModeStackLength() {
        return templateModePtr + 1;
    }

    /**
     * Complains about an over-deep tree. Theoretically this should just be
     * a warning, but in practice authors should take this as an error.
     *
     * @throws SAXException
     */
    private void errDeepTree() throws SAXException {
        err("The document tree is more than 513 elements deep, which causes Firefox and Chrome to flatten the tree.");
    }

    /**
     * Reports a stray start tag.
     * @param name the name of the stray tag
     *
     * @throws SAXException
     */
    private void errStrayStartTag(@Local String name) throws SAXException {
        err("Stray start tag \u201C" + name + "\u201D.");
    }

    /**
     * Reports a stray end tag.
     * @param name the name of the stray tag
     *
     * @throws SAXException
     */
    private void errStrayEndTag(@Local String name) throws SAXException {
        err("Stray end tag \u201C" + name + "\u201D.");
    }

    /**
     * Reports a state when elements expected to be closed were not.
     *
     * @param eltPos the position of the start tag on the stack of the element
     * being closed.
     * @param name the name of the end tag
     *
     * @throws SAXException
     */
    private void errUnclosedElements(int eltPos, @Local String name) throws SAXException {
        errNoCheck("End tag \u201C" + name + "\u201D seen, but there were open elements.");
        errListUnclosedStartTags(eltPos);
    }

    /**
     * Reports a state when elements expected to be closed ahead of an implied
     * end tag but were not.
     *
     * @param eltPos the position of the start tag on the stack of the element
     * being closed.
     * @param name the name of the end tag
     *
     * @throws SAXException
     */
    private void errUnclosedElementsImplied(int eltPos, String name) throws SAXException {
        errNoCheck("End tag \u201C" + name + "\u201D implied, but there were open elements.");
        errListUnclosedStartTags(eltPos);
    }

    /**
     * Reports a state when elements expected to be closed ahead of an implied
     * table cell close.
     *
     * @param eltPos the position of the start tag on the stack of the element
     * being closed.
     * @throws SAXException
     */
    private void errUnclosedElementsCell(int eltPos) throws SAXException {
        errNoCheck("A table cell was implicitly closed, but there were open elements.");
        errListUnclosedStartTags(eltPos);
    }

    private void errStrayDoctype() throws SAXException {
        err("Stray doctype.");
    }

    private void errAlmostStandardsDoctype() throws SAXException {
        if (!forceNoQuirks) {
            err("Almost standards mode doctype. Expected \u201C<!DOCTYPE html>\u201D.");
        }
    }

    private void errQuirkyDoctype() throws SAXException {
        if (!forceNoQuirks) {
            err("Quirky doctype. Expected \u201C<!DOCTYPE html>\u201D.");
        }
    }

    private void errNonSpaceInTrailer() throws SAXException {
        err("Non-space character in page trailer.");
    }

    private void errNonSpaceAfterFrameset() throws SAXException {
        err("Non-space after \u201Cframeset\u201D.");
    }

    private void errNonSpaceInFrameset() throws SAXException {
        err("Non-space in \u201Cframeset\u201D.");
    }

    private void errNonSpaceAfterBody() throws SAXException {
        err("Non-space character after body.");
    }

    private void errNonSpaceInColgroupInFragment() throws SAXException {
        err("Non-space in \u201Ccolgroup\u201D when parsing fragment.");
    }

    private void errNonSpaceInNoscriptInHead() throws SAXException {
        err("Non-space character inside \u201Cnoscript\u201D inside \u201Chead\u201D.");
    }

    private void errFooBetweenHeadAndBody(@Local String name) throws SAXException {
        if (errorHandler == null) {
            return;
        }
        errNoCheck("\u201C" + name + "\u201D element between \u201Chead\u201D and \u201Cbody\u201D.");
    }

    private void errStartTagWithoutDoctype() throws SAXException {
        if (!forceNoQuirks) {
            err("Start tag seen without seeing a doctype first. Expected \u201C<!DOCTYPE html>\u201D.");
        }
    }

    private void errNoSelectInTableScope() throws SAXException {
        err("No \u201Cselect\u201D in table scope.");
    }

    private void errStartSelectWhereEndSelectExpected() throws SAXException {
        err("\u201Cselect\u201D start tag where end tag expected.");
    }

    private void errStartTagWithSelectOpen(@Local String name)
            throws SAXException {
        if (errorHandler == null) {
            return;
        }
        errNoCheck("\u201C" + name
                + "\u201D start tag with \u201Cselect\u201D open.");
    }

    private void errBadStartTagInNoscriptInHead(@Local String name) throws SAXException {
        if (errorHandler == null) {
            return;
        }
        errNoCheck("Bad start tag in \u201C" + name
                + "\u201D in \u201Cnoscript\u201D in \u201Chead\u201D.");
    }

    private void errImage() throws SAXException {
        err("Saw a start tag \u201Cimage\u201D.");
    }

    private void errFooSeenWhenFooOpen(@Local String name) throws SAXException {
        if (errorHandler == null) {
            return;
        }
        errNoCheck("Start tag \u201C" + name + "\u201D seen but an element of the same type was already open.");
    }

    private void errHeadingWhenHeadingOpen() throws SAXException {
        err("Heading cannot be a child of another heading.");
    }

    private void errFramesetStart() throws SAXException {
        err("\u201Cframeset\u201D start tag seen.");
    }

    private void errNoCellToClose() throws SAXException {
        err("No cell to close.");
    }

    private void errStartTagInTable(@Local String name) throws SAXException {
        if (errorHandler == null) {
            return;
        }
        errNoCheck("Start tag \u201C" + name
                + "\u201D seen in \u201Ctable\u201D.");
    }

    private void errFormWhenFormOpen() throws SAXException {
        err("Saw a \u201Cform\u201D start tag, but there was already an active \u201Cform\u201D element. Nested forms are not allowed. Ignoring the tag.");
    }

    private void errTableSeenWhileTableOpen() throws SAXException {
        err("Start tag for \u201Ctable\u201D seen but the previous \u201Ctable\u201D is still open.");
    }

    private void errStartTagInTableBody(@Local String name) throws SAXException {
        if (errorHandler == null) {
            return;
        }
        errNoCheck("\u201C" + name + "\u201D start tag in table body.");
    }

    private void errEndTagSeenWithoutDoctype() throws SAXException {
        if (!forceNoQuirks) {
            err("End tag seen without seeing a doctype first. Expected \u201C<!DOCTYPE html>\u201D.");
        }
    }

    private void errEndTagAfterBody() throws SAXException {
        err("Saw an end tag after \u201Cbody\u201D had been closed.");
    }

    private void errEndTagSeenWithSelectOpen(@Local String name) throws SAXException {
        if (errorHandler == null) {
            return;
        }
        errNoCheck("\u201C" + name
                + "\u201D end tag with \u201Cselect\u201D open.");
    }

    private void errGarbageInColgroup() throws SAXException {
        err("Garbage in \u201Ccolgroup\u201D fragment.");
    }

    private void errEndTagBr() throws SAXException {
        err("End tag \u201Cbr\u201D.");
    }

    private void errNoElementToCloseButEndTagSeen(@Local String name)
            throws SAXException {
        if (errorHandler == null) {
            return;
        }
        errNoCheck("No \u201C" + name + "\u201D element in scope but a \u201C"
                + name + "\u201D end tag seen.");
    }

    private void errHtmlStartTagInForeignContext(@Local String name)
            throws SAXException {
        if (errorHandler == null) {
            return;
        }
        errNoCheck("HTML start tag \u201C" + name
                + "\u201D in a foreign namespace context.");
    }

    private void errNoTableRowToClose() throws SAXException {
        err("No table row to close.");
    }

    private void errNonSpaceInTable() throws SAXException {
        err("Misplaced non-space characters inside a table.");
    }

    private void errUnclosedChildrenInRuby() throws SAXException {
        if (errorHandler == null) {
            return;
        }
        errNoCheck("Unclosed children in \u201Cruby\u201D.");
    }

    private void errStartTagSeenWithoutRuby(@Local String name) throws SAXException {
        if (errorHandler == null) {
            return;
        }
        errNoCheck("Start tag \u201C"
                + name
                + "\u201D seen without a \u201Cruby\u201D element being open.");
    }

    private void errSelfClosing() throws SAXException {
        if (errorHandler == null) {
            return;
        }
        errNoCheck("Self-closing syntax (\u201C/>\u201D) used on a non-void HTML element. Ignoring the slash and treating as a start tag.");
    }

    private void errNoCheckUnclosedElementsOnStack() throws SAXException {
        errNoCheck("Unclosed elements on stack.");
    }

    private void errEndTagDidNotMatchCurrentOpenElement(@Local String name,
            @Local String currOpenName) throws SAXException {
        if (errorHandler == null) {
            return;
        }
        errNoCheck("End tag \u201C"
                + name
                + "\u201D did not match the name of the current open element (\u201C"
                + currOpenName + "\u201D).");
    }

    private void errEndTagViolatesNestingRules(@Local String name) throws SAXException {
        if (errorHandler == null) {
            return;
        }
        errNoCheck("End tag \u201C" + name + "\u201D violates nesting rules.");
    }

    private void errEofWithUnclosedElements() throws SAXException {
        if (errorHandler == null) {
            return;
        }
        errNoCheck("End of file seen and there were open elements.");
        // just report all remaining unclosed elements
        errListUnclosedStartTags(0);
    }

    /**
     * Reports arriving at/near end of document with unclosed elements remaining.
     *
     * @param message
     *            the message
     * @throws SAXException
     */
    private void errEndWithUnclosedElements(@Local String name) throws SAXException {
        if (errorHandler == null) {
            return;
        }
        errNoCheck("End tag for  \u201C"
                + name
                + "\u201D seen, but there were unclosed elements.");
        // just report all remaining unclosed elements
        errListUnclosedStartTags(0);
    }
}
