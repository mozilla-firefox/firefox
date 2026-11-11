/*
 * Copyright (c) 2005-2007 Henri Sivonen
 * Copyright (c) 2007-2017 Mozilla Foundation
 * Portions of comments Copyright 2004-2010 Apple Computer, Inc., Mozilla
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
 * comment are quotes from the WHATWG HTML 5 spec as of 2 June 2007
 * amended as of June 18 2008 and May 31 2010.
 * That document came with this statement:
 * "© Copyright 2004-2010 Apple Computer, Inc., Mozilla Foundation, and
 * Opera Software ASA. You are granted a license to use, reproduce and
 * create derivative works of this document."
 */

package nu.validator.htmlparser.impl;

import java.util.HashMap;

import org.xml.sax.ErrorHandler;
import org.xml.sax.Locator;
import org.xml.sax.ext.Locator2;
import org.xml.sax.SAXException;
import org.xml.sax.SAXParseException;

import nu.validator.htmlparser.annotation.Auto;
import nu.validator.htmlparser.annotation.CharacterName;
import nu.validator.htmlparser.annotation.Const;
import nu.validator.htmlparser.annotation.Inline;
import nu.validator.htmlparser.annotation.Local;
import nu.validator.htmlparser.annotation.NoLength;
import nu.validator.htmlparser.common.EncodingDeclarationHandler;
import nu.validator.htmlparser.common.Interner;
import nu.validator.htmlparser.common.TokenHandler;
import nu.validator.htmlparser.common.XmlViolationPolicy;

/**
 * An implementation of
 * https://html.spec.whatwg.org/multipage/syntax.html#tokenization
 *
 * This class implements the <code>Locator</code> interface. This is not an
 * incidental implementation detail: Users of this class are encouraged to make
 * use of the <code>Locator</code> nature.
 *
 * By default, the tokenizer may report data that XML 1.0 bans. The tokenizer
 * can be configured to treat these conditions as fatal or to coerce the infoset
 * to something that XML 1.0 allows.
 *
 * @version $Id$
 * @author hsivonen
 */
public class Tokenizer implements Locator, Locator2 {

    private static final int DATA_AND_RCDATA_MASK = ~1;

    public static final int DATA = 0;

    public static final int RCDATA = 1;

    public static final int SCRIPT_DATA = 2;

    public static final int RAWTEXT = 3;

    public static final int SCRIPT_DATA_ESCAPED = 4;

    public static final int ATTRIBUTE_VALUE_DOUBLE_QUOTED = 5;

    public static final int ATTRIBUTE_VALUE_SINGLE_QUOTED = 6;

    public static final int ATTRIBUTE_VALUE_UNQUOTED = 7;

    public static final int PLAINTEXT = 8;

    public static final int TAG_OPEN = 9;

    public static final int CLOSE_TAG_OPEN = 10;

    public static final int TAG_NAME = 11;

    public static final int BEFORE_ATTRIBUTE_NAME = 12;

    public static final int ATTRIBUTE_NAME = 13;

    public static final int AFTER_ATTRIBUTE_NAME = 14;

    public static final int BEFORE_ATTRIBUTE_VALUE = 15;

    public static final int AFTER_ATTRIBUTE_VALUE_QUOTED = 16;

    public static final int BOGUS_COMMENT = 17;

    public static final int MARKUP_DECLARATION_OPEN = 18;

    public static final int DOCTYPE = 19;

    public static final int BEFORE_DOCTYPE_NAME = 20;

    public static final int DOCTYPE_NAME = 21;

    public static final int AFTER_DOCTYPE_NAME = 22;

    public static final int BEFORE_DOCTYPE_PUBLIC_IDENTIFIER = 23;

    public static final int DOCTYPE_PUBLIC_IDENTIFIER_DOUBLE_QUOTED = 24;

    public static final int DOCTYPE_PUBLIC_IDENTIFIER_SINGLE_QUOTED = 25;

    public static final int AFTER_DOCTYPE_PUBLIC_IDENTIFIER = 26;

    public static final int BEFORE_DOCTYPE_SYSTEM_IDENTIFIER = 27;

    public static final int DOCTYPE_SYSTEM_IDENTIFIER_DOUBLE_QUOTED = 28;

    public static final int DOCTYPE_SYSTEM_IDENTIFIER_SINGLE_QUOTED = 29;

    public static final int AFTER_DOCTYPE_SYSTEM_IDENTIFIER = 30;

    public static final int BOGUS_DOCTYPE = 31;

    public static final int COMMENT_START = 32;

    public static final int COMMENT_START_DASH = 33;

    public static final int COMMENT = 34;

    public static final int COMMENT_END_DASH = 35;

    public static final int COMMENT_END = 36;

    public static final int COMMENT_END_BANG = 37;

    public static final int NON_DATA_END_TAG_NAME = 38;

    public static final int MARKUP_DECLARATION_HYPHEN = 39;

    public static final int MARKUP_DECLARATION_OCTYPE = 40;

    public static final int DOCTYPE_UBLIC = 41;

    public static final int DOCTYPE_YSTEM = 42;

    public static final int AFTER_DOCTYPE_PUBLIC_KEYWORD = 43;

    public static final int BETWEEN_DOCTYPE_PUBLIC_AND_SYSTEM_IDENTIFIERS = 44;

    public static final int AFTER_DOCTYPE_SYSTEM_KEYWORD = 45;

    public static final int CONSUME_CHARACTER_REFERENCE = 46;

    public static final int CONSUME_NCR = 47;

    public static final int CHARACTER_REFERENCE_TAIL = 48;

    public static final int HEX_NCR_LOOP = 49;

    public static final int DECIMAL_NRC_LOOP = 50;

    public static final int HANDLE_NCR_VALUE = 51;

    public static final int HANDLE_NCR_VALUE_RECONSUME = 52;

    public static final int CHARACTER_REFERENCE_HILO_LOOKUP = 53;

    public static final int SELF_CLOSING_START_TAG = 54;

    public static final int CDATA_START = 55;

    public static final int CDATA_SECTION = 56;

    public static final int CDATA_RSQB = 57;

    public static final int CDATA_RSQB_RSQB = 58;

    public static final int SCRIPT_DATA_LESS_THAN_SIGN = 59;

    public static final int SCRIPT_DATA_ESCAPE_START = 60;

    public static final int SCRIPT_DATA_ESCAPE_START_DASH = 61;

    public static final int SCRIPT_DATA_ESCAPED_DASH = 62;

    public static final int SCRIPT_DATA_ESCAPED_DASH_DASH = 63;

    public static final int BOGUS_COMMENT_HYPHEN = 64;

    public static final int RAWTEXT_RCDATA_LESS_THAN_SIGN = 65;

    public static final int SCRIPT_DATA_ESCAPED_LESS_THAN_SIGN = 66;

    public static final int SCRIPT_DATA_DOUBLE_ESCAPE_START = 67;

    public static final int SCRIPT_DATA_DOUBLE_ESCAPED = 68;

    public static final int SCRIPT_DATA_DOUBLE_ESCAPED_LESS_THAN_SIGN = 69;

    public static final int SCRIPT_DATA_DOUBLE_ESCAPED_DASH = 70;

    public static final int SCRIPT_DATA_DOUBLE_ESCAPED_DASH_DASH = 71;

    public static final int SCRIPT_DATA_DOUBLE_ESCAPE_END = 72;

    public static final int PROCESSING_INSTRUCTION = 73;

    public static final int PROCESSING_INSTRUCTION_QUESTION_MARK = 74;

    public static final int COMMENT_LESSTHAN = 76;

    public static final int COMMENT_LESSTHAN_BANG = 77;

    public static final int COMMENT_LESSTHAN_BANG_DASH = 78;

    public static final int COMMENT_LESSTHAN_BANG_DASH_DASH = 79;

    public static final int PROCESSING_INSTRUCTION_OPEN = 80;

    public static final int PROCESSING_INSTRUCTION_TARGET = 81;

    public static final int AFTER_PROCESSING_INSTRUCTION_TARGET = 82;

    public static final int PROCESSING_INSTRUCTION_DATA = 83;

    public static final int PROCESSING_INSTRUCTION_QUESTIONABLE = 84;

    /**
     * Magic value for UTF-16 operations.
     */
    private static final int LEAD_OFFSET = (0xD800 - (0x10000 >> 10));

    /**
     * UTF-16 code unit array containing less than and greater than for emitting
     * those characters on certain parse errors.
     */
    private static final @NoLength char[] LT_GT = { '<', '>' };

    private static final @NoLength char[] XML_STYLESHEET = { 'x', 'm', 'l',
            '-', 's', 't', 'y', 'l', 'e', 's', 'h', 'e', 'e', 't' };

    /**
     * UTF-16 code unit array containing less than and solidus for emitting
     * those characters on certain parse errors.
     */
    private static final @NoLength char[] LT_SOLIDUS = { '<', '/' };

    /**
     * UTF-16 code unit array containing ]] for emitting those characters on
     * state transitions.
     */
    private static final @NoLength char[] RSQB_RSQB = { ']', ']' };

    /**
     * Array version of U+FFFD.
     */
    private static final @NoLength char[] REPLACEMENT_CHARACTER = { '\uFFFD' };

    // [NOCPP[

    /**
     * Array version of space.
     */
    private static final @NoLength char[] SPACE = { ' ' };

    // ]NOCPP]

    /**
     * Array version of line feed.
     */
    private static final @NoLength char[] LF = { '\n' };

    /**
     * "CDATA[" as <code>char[]</code>
     */
    private static final @NoLength char[] CDATA_LSQB = { 'C', 'D', 'A', 'T',
            'A', '[' };

    /**
     * "octype" as <code>char[]</code>
     */
    private static final @NoLength char[] OCTYPE = { 'o', 'c', 't', 'y', 'p',
            'e' };

    /**
     * "ublic" as <code>char[]</code>
     */
    private static final @NoLength char[] UBLIC = { 'u', 'b', 'l', 'i', 'c' };

    /**
     * "ystem" as <code>char[]</code>
     */
    private static final @NoLength char[] YSTEM = { 'y', 's', 't', 'e', 'm' };

    private static final char[] TITLE_ARR = { 't', 'i', 't', 'l', 'e' };

    private static final char[] SCRIPT_ARR = { 's', 'c', 'r', 'i', 'p', 't' };

    private static final char[] STYLE_ARR = { 's', 't', 'y', 'l', 'e' };

    private static final char[] PLAINTEXT_ARR = { 'p', 'l', 'a', 'i', 'n', 't',
            'e', 'x', 't' };

    private static final char[] XMP_ARR = { 'x', 'm', 'p' };

    private static final char[] TEXTAREA_ARR = { 't', 'e', 'x', 't', 'a', 'r',
            'e', 'a' };

    private static final char[] IFRAME_ARR = { 'i', 'f', 'r', 'a', 'm', 'e' };

    private static final char[] NOEMBED_ARR = { 'n', 'o', 'e', 'm', 'b', 'e',
            'd' };

    private static final char[] NOSCRIPT_ARR = { 'n', 'o', 's', 'c', 'r', 'i',
            'p', 't' };

    private static final char[] NOFRAMES_ARR = { 'n', 'o', 'f', 'r', 'a', 'm',
            'e', 's' };

    /**
     * The token handler.
     */
    protected final TokenHandler tokenHandler;

    protected EncodingDeclarationHandler encodingDeclarationHandler;

    // [NOCPP[

    /**
     * The error handler.
     */
    protected ErrorHandler errorHandler;

    // ]NOCPP]

    /**
     * Whether the previous char read was CR.
     */
    protected boolean lastCR;

    protected int stateSave;

    private int returnStateSave;

    protected int index;

    private boolean forceQuirks;

    private char additional;

    private int entCol;

    private int firstCharKey;

    private int lo;

    private int hi;

    private int candidate;

    private int charRefBufMark;

    protected int value;

    private boolean seenDigits;

    private boolean suspendAfterCurrentNonTextToken;

    protected int cstart;

    /**
     * The SAX public id for the resource being tokenized. (Only passed to back
     * as part of locator data.)
     */
    private String publicId;

    /**
     * The SAX system id for the resource being tokenized. (Only passed to back
     * as part of locator data.)
     */
    private String systemId;

    /**
     * Buffer for bufferable things other than those that fit the description
     * of <code>charRefBuf</code>.
     */
    private @Auto char[] strBuf;

    /**
     * Number of significant <code>char</code>s in <code>strBuf</code>.
     */
    private int strBufLen;

    /**
     * Buffer for characters that might form a character reference but may
     * end up not forming one.
     */
    private final @Auto char[] charRefBuf;

    /**
     * Number of significant <code>char</code>s in <code>charRefBuf</code>.
     */
    private int charRefBufLen;

    /**
     * Buffer for expanding NCRs falling into the Basic Multilingual Plane.
     */
    private final @Auto char[] bmpChar;

    /**
     * Buffer for expanding astral NCRs.
     */
    private final @Auto char[] astralChar;

    /**
     * The element whose end tag closes the current CDATA or RCDATA element.
     */
    protected ElementName endTagExpectation = null;

    private char[] endTagExpectationAsArray; // not @Auto!

    /**
     * <code>true</code> if tokenizing an end tag
     */
    protected boolean endTag;

    /**
     * <code>true</code> iff the current element/attribute name contains
     * a hyphen.
     */
    private boolean containsHyphen;

    /**
     * The current tag token name. One of
     * 1) null,
     * 2) non-owning reference to nonInternedTagName
     * 3) non-owning reference to a pre-interned ElementName
     */
    private ElementName tagName = null;

    /**
     * The recycled ElementName instance for the non-pre-interned cases.
     */
    private ElementName nonInternedTagName = null;

    /**
     * The current attribute name.
     */
    protected AttributeName attributeName = null;

    // CPPONLY: private AttributeName nonInternedAttributeName = null;

    // [NOCPP[

    /**
     * Whether comment tokens are emitted.
     */
    private boolean wantsComments = false;

    /**
     * Whether the stream is past the first 1024 bytes.
     */
    private boolean metaBoundaryPassed;

    // ]NOCPP]

    /**
     * The name of the current doctype token.
     */
    private @Local String doctypeName;

    /**
     * The public id of the current doctype token.
     */
    private String publicIdentifier;

    /**
     * The system id of the current doctype token.
     */
    private String systemIdentifier;

    /**
     * The target of the current processing instruction token.
     */
    private String piTarget;

    /**
     * Whether <code>&lt;?</code> starts a processing instruction instead of
     * a bogus comment.
     */
    private boolean processingInstructionsEnabled;

    /**
     * The attribute holder.
     */
    private HtmlAttributes attributes;

    // [NOCPP[

    /**
     * The policy for vertical tab and form feed.
     */
    private XmlViolationPolicy contentSpacePolicy = XmlViolationPolicy.ALTER_INFOSET;

    /**
     * The policy for comments.
     */
    private XmlViolationPolicy commentPolicy = XmlViolationPolicy.ALTER_INFOSET;

    private XmlViolationPolicy xmlnsPolicy = XmlViolationPolicy.ALTER_INFOSET;

    private XmlViolationPolicy namePolicy = XmlViolationPolicy.ALTER_INFOSET;

    private int mappingLangToXmlLang;

    // ]NOCPP]

    private final boolean newAttributesEachTime;

    private boolean shouldSuspend;

    private boolean keepBuffer;

    protected boolean confident;

    private int line;

    /*
     * The line number of the current attribute. First set to the line of the
     * attribute name and if there is a value, set to the line the value
     * started on.
     */
    // CPPONLY: private int attributeLine;

    private Interner interner;

    // CPPONLY: private boolean viewingXmlSource;

    // [NOCPP[

    protected LocatorImpl ampersandLocation;

    public Tokenizer(TokenHandler tokenHandler, boolean newAttributesEachTime) {
        this.tokenHandler = tokenHandler;
        this.encodingDeclarationHandler = null;
        this.lastCR = false;
        this.stateSave = 0;
        this.returnStateSave = 0;
        this.index = 0;
        this.forceQuirks = false;
        this.additional = '\u0000';
        this.entCol = 0;
        this.firstCharKey = 0;
        this.lo = 0;
        this.hi = 0;
        this.candidate = 0;
        this.charRefBufMark = 0;
        this.value = 0;
        this.seenDigits = false;
        this.suspendAfterCurrentNonTextToken = false;
        this.cstart = 0;
        this.strBufLen = 0;
        this.newAttributesEachTime = newAttributesEachTime;
        // &CounterClockwiseContourIntegral; is the longest valid char ref and
        // the semicolon never gets appended to the buffer.
        this.charRefBuf = new char[32];
        this.charRefBufLen = 0;
        this.bmpChar = new char[1];
        this.astralChar = new char[2];
        this.endTagExpectation = null;
        this.endTagExpectationAsArray = null;
        this.endTag = false;
        this.containsHyphen = false;
        this.tagName = null;
        this.nonInternedTagName = new ElementName();
        this.attributeName = null;
        // CPPONLY: this.nonInternedAttributeName = new AttributeName();
        this.doctypeName = null;
        this.publicIdentifier = null;
        this.systemIdentifier = null;
        this.piTarget = null;
        this.processingInstructionsEnabled = false;
        this.attributes = null;
        this.shouldSuspend = false;
        this.keepBuffer = false;
        this.confident = false;
        this.line = 0;
        // CPPONLY: this.attributeLine = 0;
        this.interner = null;
    }

    // ]NOCPP]

    /**
     * The constructor.
     *
     * @param tokenHandler
     *            the handler for receiving tokens
     */
    public Tokenizer(TokenHandler tokenHandler
    // CPPONLY: , boolean viewingXmlSource
    ) {
        this.tokenHandler = tokenHandler;
        this.encodingDeclarationHandler = null;
        // [NOCPP[
        this.newAttributesEachTime = false;
        // ]NOCPP]
        this.lastCR = false;
        this.stateSave = 0;
        this.returnStateSave = 0;
        this.index = 0;
        this.forceQuirks = false;
        this.additional = '\u0000';
        this.entCol = 0;
        this.firstCharKey = 0;
        this.lo = 0;
        this.hi = 0;
        this.candidate = 0;
        this.charRefBufMark = 0;
        this.value = 0;
        this.seenDigits = false;
        this.suspendAfterCurrentNonTextToken = false;
        this.cstart = 0;
        this.strBufLen = 0;
        // &CounterClockwiseContourIntegral; is the longest valid char ref and
        // the semicolon never gets appended to the buffer.
        this.charRefBuf = new char[32];
        this.charRefBufLen = 0;
        this.bmpChar = new char[1];
        this.astralChar = new char[2];
        this.endTagExpectation = null;
        this.endTagExpectationAsArray = null;
        this.endTag = false;
        this.containsHyphen = false;
        this.tagName = null;
        this.nonInternedTagName = new ElementName();
        this.attributeName = null;
        // CPPONLY: this.nonInternedAttributeName = new AttributeName();
        this.doctypeName = null;
        this.publicIdentifier = null;
        this.systemIdentifier = null;
        this.piTarget = null;
        this.processingInstructionsEnabled = false;
        // [NOCPP[
        this.attributes = null;
        // ]NOCPP]
        // CPPONLY: this.attributes = tokenHandler.HasBuilder() ? new HtmlAttributes(mappingLangToXmlLang) : null;
        // CPPONLY: this.newAttributesEachTime = !tokenHandler.HasBuilder();
        this.shouldSuspend = false;
        this.keepBuffer = false;
        this.confident = false;
        this.line = 0;
        // CPPONLY: this.attributeLine = 0;
        this.interner = null;
        // CPPONLY: this.viewingXmlSource = viewingXmlSource;
    }

    public void setInterner(Interner interner) {
        this.interner = interner;
    }

    public void setProcessingInstructionsEnabled(
            boolean processingInstructionsEnabled) {
        this.processingInstructionsEnabled = processingInstructionsEnabled;
    }

    public void initLocation(String newPublicId, String newSystemId) {
        this.systemId = newSystemId;
        this.publicId = newPublicId;

    }

    // CPPONLY: boolean isViewingXmlSource() {
    // CPPONLY: return viewingXmlSource;
    // CPPONLY: }

    public void setKeepBuffer(boolean keepBuffer) {
        this.keepBuffer = keepBuffer;
    }

    public boolean dropBufferIfLongerThan(int length) {
        if (strBuf.length > length) {
            strBuf = null;
            return true;
        }
        return false;
    }

    // [NOCPP[

    /**
     * Returns the mappingLangToXmlLang.
     *
     * @return the mappingLangToXmlLang
     */
    public boolean isMappingLangToXmlLang() {
        return mappingLangToXmlLang == AttributeName.HTML_LANG;
    }

    /**
     * Sets the mappingLangToXmlLang.
     *
     * @param mappingLangToXmlLang
     *            the mappingLangToXmlLang to set
     */
    public void setMappingLangToXmlLang(boolean mappingLangToXmlLang) {
        this.mappingLangToXmlLang = mappingLangToXmlLang ? AttributeName.HTML_LANG
                : AttributeName.HTML;
    }

    /**
     * Sets the error handler.
     *
     * @see org.xml.sax.XMLReader#setErrorHandler(org.xml.sax.ErrorHandler)
     */
    public void setErrorHandler(ErrorHandler eh) {
        this.errorHandler = eh;
    }

    public ErrorHandler getErrorHandler() {
        return this.errorHandler;
    }

    /**
     * Gets the errorProfile.
     *
     * @param errorProfile
     */
    public HashMap getErrorProfile() {
        return null;
    }

    /**
     * Sets the commentPolicy.
     *
     * @param commentPolicy
     *            the commentPolicy to set
     */
    public void setCommentPolicy(XmlViolationPolicy commentPolicy) {
        this.commentPolicy = commentPolicy;
    }

    /**
     * Sets the contentNonXmlCharPolicy.
     *
     * @param contentNonXmlCharPolicy
     *            the contentNonXmlCharPolicy to set
     */
    public void setContentNonXmlCharPolicy(
            XmlViolationPolicy contentNonXmlCharPolicy) {
        if (contentNonXmlCharPolicy != XmlViolationPolicy.ALLOW) {
            throw new IllegalArgumentException(
                    "Must use ErrorReportingTokenizer to set contentNonXmlCharPolicy to non-ALLOW.");
        }
    }

    /**
     * Sets the contentSpacePolicy.
     *
     * @param contentSpacePolicy
     *            the contentSpacePolicy to set
     */
    public void setContentSpacePolicy(XmlViolationPolicy contentSpacePolicy) {
        this.contentSpacePolicy = contentSpacePolicy;
    }

    /**
     * Sets the xmlnsPolicy.
     *
     * @param xmlnsPolicy
     *            the xmlnsPolicy to set
     */
    public void setXmlnsPolicy(XmlViolationPolicy xmlnsPolicy) {
        if (xmlnsPolicy == XmlViolationPolicy.FATAL) {
            throw new IllegalArgumentException("Can't use FATAL here.");
        }
        this.xmlnsPolicy = xmlnsPolicy;
    }

    public void setNamePolicy(XmlViolationPolicy namePolicy) {
        this.namePolicy = namePolicy;
    }

    // ]NOCPP]

    // For the token handler to call

    /**
     * Sets the tokenizer state and the associated element name. This should
     * only ever used to put the tokenizer into one of the states that have
     * a special end tag expectation.
     *
     * @param specialTokenizerState
     *            the tokenizer state to set
     */
    public void setState(int specialTokenizerState) {
        this.stateSave = specialTokenizerState;
        this.endTagExpectation = null;
        this.endTagExpectationAsArray = null;
    }

    // [NOCPP[

    /**
     * Sets the tokenizer state and the associated element name. This should
     * only ever used to put the tokenizer into one of the states that have
     * a special end tag expectation. For use from the tokenizer test harness.
     *
     * @param specialTokenizerState
     *            the tokenizer state to set
     * @param endTagExpectation
     *            the expected end tag for transitioning back to normal
     */
    public void setStateAndEndTagExpectation(int specialTokenizerState,
            @Local String endTagExpectation) {
        this.stateSave = specialTokenizerState;
        if (specialTokenizerState == Tokenizer.DATA) {
            return;
        }
        @Auto char[] asArray = Portability.newCharArrayFromLocal(endTagExpectation);
        this.endTagExpectation = ElementName.elementNameByBuffer(asArray,
                asArray.length);
        assert this.endTagExpectation != null;
        endTagExpectationToArray();
    }

    // ]NOCPP]

    /**
     * Sets the tokenizer state and the associated element name. This should
     * only ever used to put the tokenizer into one of the states that have
     * a special end tag expectation.
     *
     * @param specialTokenizerState
     *            the tokenizer state to set
     * @param endTagExpectation
     *            the expected end tag for transitioning back to normal
     */
    public void setStateAndEndTagExpectation(int specialTokenizerState,
            ElementName endTagExpectation) {
        this.stateSave = specialTokenizerState;
        this.endTagExpectation = endTagExpectation;
        endTagExpectationToArray();
    }

    private void endTagExpectationToArray() {
        switch (endTagExpectation.getGroup()) {
            case TreeBuilder.TITLE:
                endTagExpectationAsArray = TITLE_ARR;
                return;
            case TreeBuilder.SCRIPT:
                endTagExpectationAsArray = SCRIPT_ARR;
                return;
            case TreeBuilder.STYLE:
                endTagExpectationAsArray = STYLE_ARR;
                return;
            case TreeBuilder.PLAINTEXT:
                endTagExpectationAsArray = PLAINTEXT_ARR;
                return;
            case TreeBuilder.XMP:
                endTagExpectationAsArray = XMP_ARR;
                return;
            case TreeBuilder.TEXTAREA:
                endTagExpectationAsArray = TEXTAREA_ARR;
                return;
            case TreeBuilder.IFRAME:
                endTagExpectationAsArray = IFRAME_ARR;
                return;
            case TreeBuilder.NOEMBED:
                endTagExpectationAsArray = NOEMBED_ARR;
                return;
            case TreeBuilder.NOSCRIPT:
                endTagExpectationAsArray = NOSCRIPT_ARR;
                return;
            case TreeBuilder.NOFRAMES:
                endTagExpectationAsArray = NOFRAMES_ARR;
                return;
            default:
                assert false: "Bad end tag expectation.";
                return;
        }
    }

    /**
     * For C++ use only.
     */
    public void setLineNumber(int line) {
        // CPPONLY: this.attributeLine = line; // XXX is this needed?
        this.line = line;
    }

    // start Locator impl

    /**
     * @see org.xml.sax.Locator#getLineNumber()
     */
    @Inline public int getLineNumber() {
        return line;
    }

    // [NOCPP[

    /**
     * @see org.xml.sax.Locator#getColumnNumber()
     */
    @Inline public int getColumnNumber() {
        return -1;
    }

    /**
     * @see org.xml.sax.Locator#getPublicId()
     */
    public String getPublicId() {
        return publicId;
    }

    /**
     * @see org.xml.sax.Locator#getSystemId()
     */
    public String getSystemId() {
        return systemId;
    }

    /**
     * @see org.xml.sax.ext.Locator2#getXMLVersion()
     */
    public String getXMLVersion() {
        return "1.0";
    }

    /**
     * @see org.xml.sax.ext.Locator2#getXMLVersion()
     */
    public String getEncoding() {
        try {
            return encodingDeclarationHandler == null ? null : encodingDeclarationHandler.getCharacterEncoding();
        } catch (SAXException e) {
            return null;
        }
    }

    // end Locator impl

    // end public API

    public void notifyAboutMetaBoundary() {
        metaBoundaryPassed = true;
    }

    // ]NOCPP]

    @Inline HtmlAttributes emptyAttributes() {
        // [NOCPP[
        if (newAttributesEachTime) {
            return new HtmlAttributes(mappingLangToXmlLang);
        } else {
            // ]NOCPP]
            return HtmlAttributes.EMPTY_ATTRIBUTES;
            // [NOCPP[
        }
        // ]NOCPP]
    }

    private void appendCharRefBuf(char c) {
        // CPPONLY: assert charRefBufLen < charRefBuf.length:
        // CPPONLY:     "RELEASE: Attempted to overrun charRefBuf!";
        charRefBuf[charRefBufLen++] = c;
    }

    private void emitOrAppendCharRefBuf(int returnState) throws SAXException {
        if ((returnState & DATA_AND_RCDATA_MASK) != 0) {
            appendCharRefBufToStrBuf();
        } else {
            if (charRefBufLen > 0) {
                tokenHandler.characters(charRefBuf, 0, charRefBufLen);
                charRefBufLen = 0;
            }
        }
    }

    @Inline private void clearStrBufAfterUse() {
        strBufLen = 0;
    }

    @Inline private void clearStrBufBeforeUse() {
        assert strBufLen == 0: "strBufLen not reset after previous use!";
        strBufLen = 0; // no-op in the absence of bugs
    }

    @Inline private void clearStrBufAfterOneHyphen() {
        assert strBufLen == 1: "strBufLen length not one!";
        assert strBuf[0] == '-': "strBuf does not start with a hyphen!";
        strBufLen = 0;
    }

    /**
     * Appends to the buffer.
     *
     * @param c
     *            the UTF-16 code unit to append
     */
    @Inline private void appendStrBuf(char c) {
        // CPPONLY: if (strBufLen == strBuf.length) {
        // CPPONLY:     EnsureBufferSpaceShouldNeverHappen(1);
        // CPPONLY: }
        strBuf[strBufLen++] = c;
    }

    @Inline protected String strBufToString() {
        String str = Portability.newStringFromBuffer(strBuf, 0, strBufLen
            // CPPONLY: , tokenHandler, null
        );
        clearStrBufAfterUse();
        return str;
    }

    @Inline protected String strBufToAttributeValueString() {
        // CPPONLY: String digitAtom = TryAtomizeForSingleDigit();
        // CPPONLY: if (digitAtom) {
        // CPPONLY:   return digitAtom;
        // CPPONLY: }
        // CPPONLY:
        // CPPONLY: String str = AtomizeAttributeValueOrNewString();
        // [NOCPP[
        String str = Portability.newStringFromBuffer(strBuf, 0, strBufLen);
        // ]NOCPP]
        clearStrBufAfterUse();
        return str;
    }

    /**
     * Returns the buffer as a local name. The return value is released in
     * emitDoctypeToken().
     *
     * @return the buffer as local name
     */
    @Inline private void strBufToDoctypeName() {
        doctypeName = Portability.newLocalNameFromBuffer(strBuf, strBufLen, interner);
        clearStrBufAfterUse();
    }

    /**
     * Emits the buffer as character tokens.
     *
     * @throws SAXException
     *             if the token handler threw
     */
    @Inline private void emitStrBuf() throws SAXException {
        if (strBufLen > 0) {
            tokenHandler.characters(strBuf, 0, strBufLen);
            clearStrBufAfterUse();
        }
    }

    @Inline private void appendSecondHyphenToBogusComment() throws SAXException {
        // [NOCPP[
        switch (commentPolicy) {
            case ALTER_INFOSET:
                appendStrBuf(' ');
                // CPPONLY: MOZ_FALLTHROUGH;
            case ALLOW:
                warn("The document is not mappable to XML 1.0 due to two consecutive hyphens in a comment.");
                // ]NOCPP]
                appendStrBuf('-');
                // [NOCPP[
                break;
            case FATAL:
                fatal("The document is not mappable to XML 1.0 due to two consecutive hyphens in a comment.");
                break;
        }
        // ]NOCPP]
    }

    // [NOCPP[
    private void maybeAppendSpaceToBogusComment() throws SAXException {
        switch (commentPolicy) {
            case ALTER_INFOSET:
                appendStrBuf(' ');
                // CPPONLY: MOZ_FALLTHROUGH;
            case ALLOW:
                warn("The document is not mappable to XML 1.0 due to a trailing hyphen in a comment.");
                break;
            case FATAL:
                fatal("The document is not mappable to XML 1.0 due to a trailing hyphen in a comment.");
                break;
        }
    }

    // ]NOCPP]

    @Inline private void adjustDoubleHyphenAndAppendToStrBufAndErr(char c, boolean reportedConsecutiveHyphens)
            throws SAXException {
        // [NOCPP[
        switch (commentPolicy) {
            case ALTER_INFOSET:
                strBufLen--;
                // WARNING!!! This expands the worst case of the buffer length
                // given the length of input!
                appendStrBuf(' ');
                appendStrBuf('-');
                // CPPONLY: MOZ_FALLTHROUGH;
            case ALLOW:
                if (!reportedConsecutiveHyphens) {
                    warn("The document is not mappable to XML 1.0 due to two consecutive hyphens in a comment.");
                }
                // ]NOCPP]
                appendStrBuf(c);
                // [NOCPP[
                break;
            case FATAL:
                fatal("The document is not mappable to XML 1.0 due to two consecutive hyphens in a comment.");
                break;
        }
        // ]NOCPP]
    }

    @Inline private void appendStrBuf(@NoLength char[] buffer, int offset, int length) throws SAXException {
        // Years of crash stats have shown that the this addition doesn't overflow, as it logically
        // shouldn't.
        int newLen = strBufLen + length;
        // CPPONLY: if (strBuf.length < newLen) {
        // CPPONLY:     EnsureBufferSpaceShouldNeverHappen(length);
        // CPPONLY: }
        System.arraycopy(buffer, offset, strBuf, strBufLen, length);
        strBufLen = newLen;
    }

    /**
     * Append the contents of the char reference buffer to the main one.
     */
    @Inline private void appendCharRefBufToStrBuf() throws SAXException {
        appendStrBuf(charRefBuf, 0, charRefBufLen);
        charRefBufLen = 0;
    }

    /**
     * Emits the current comment token.
     *
     * NOTE: The method may set <code>shouldSuspend</code>, so the caller
     * must have this pattern after the state's <code>transition</code> call:
     *
     * <pre>
     * if (shouldSuspend) {
     *     break stateloop;
     * }
     * continue stateloop;
     * </pre>
     *
     * @param pos
     *            TODO
     *
     * @throws SAXException
     */
    private void emitComment(int provisionalHyphens, int pos)
            throws SAXException {
        // CPPONLY: RememberGt(pos);
        // [NOCPP[
        if (wantsComments) {
            // ]NOCPP]
            tokenHandler.comment(strBuf, 0, strBufLen
                    - provisionalHyphens);
            // [NOCPP[
        }
        // ]NOCPP]
        clearStrBufAfterUse();
        cstart = pos + 1;
        suspendIfRequestedAfterCurrentNonTextToken();
    }

    /**
     * Checks whether the processing instruction target accumulated after the
     * initial U+003F in the buffer is an ASCII case-insensitive match for
     * "xml" or "xml-stylesheet".
     */
    private boolean isDisallowedPiTarget() {
        int targetLength = strBufLen - 1;
        if (targetLength != 3 && targetLength != 14) {
            return false;
        }
        for (int i = 0; i < targetLength; i++) {
            char folded = strBuf[i + 1];
            if (folded >= 'A' && folded <= 'Z') {
                folded += 0x20;
            }
            if (folded != XML_STYLESHEET[i]) {
                return false;
            }
        }
        return true;
    }

    /**
     * Takes the target out of the buffer, leaving the buffer ready for
     * accumulating the processing instruction data. The initial U+003F stays
     * in the buffer only while the bogus comment fallback is still possible,
     * so it is excluded here.
     */
    private void finishPiTarget() {
        piTarget = Portability.newStringFromBuffer(strBuf, 1, strBufLen - 1
            // CPPONLY: , tokenHandler, null
        );
        clearStrBufAfterUse();
    }

    private void emitProcessingInstruction(int pos) throws SAXException {
        // CPPONLY: RememberGt(pos);
        tokenHandler.processingInstruction(piTarget, strBuf, 0, strBufLen);
        clearStrBufAfterUse();
        Portability.releaseString(piTarget);
        piTarget = null;
        cstart = pos + 1;
        suspendIfRequestedAfterCurrentNonTextToken();
    }

    /**
     * Flushes coalesced character tokens.
     *
     * @param buf
     *            TODO
     * @param pos
     *            TODO
     *
     * @throws SAXException
     */
    protected void flushChars(@NoLength char[] buf, int pos)
            throws SAXException {
        if (pos > cstart) {
            tokenHandler.characters(buf, cstart, pos - cstart);
        }
        cstart = Integer.MAX_VALUE;
    }

    /**
     * Reports an condition that would make the infoset incompatible with XML
     * 1.0 as fatal.
     *
     * @param message
     *            the message
     * @throws SAXException
     * @throws SAXParseException
     */
    public void fatal(String message) throws SAXException {
        SAXParseException spe = new SAXParseException(message, this);
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
    public void err(String message) throws SAXException {
        if (errorHandler == null) {
            return;
        }
        SAXParseException spe = new SAXParseException(message, this);
        errorHandler.error(spe);
    }

    public void errTreeBuilder(String message) throws SAXException {
        ErrorHandler eh = null;
        if (tokenHandler instanceof TreeBuilder<?>) {
            TreeBuilder<?> treeBuilder = (TreeBuilder<?>) tokenHandler;
            eh = treeBuilder.getErrorHandler();
        }
        if (eh == null) {
            eh = errorHandler;
        }
        if (eh == null) {
            return;
        }
        SAXParseException spe = new SAXParseException(message, this);
        eh.error(spe);
    }

    /**
     * Reports a warning
     *
     * @param message
     *            the message
     * @throws SAXException
     */
    public void warn(String message) throws SAXException {
        if (errorHandler == null) {
            return;
        }
        SAXParseException spe = new SAXParseException(message, this);
        errorHandler.warning(spe);
    }

    private void strBufToElementNameString() {
        if (containsHyphen) {
            // We've got a custom element or annotation-xml.
            @Local String annotationName = ElementName.ANNOTATION_XML.getName();
            if (Portability.localEqualsBuffer(annotationName, strBuf, strBufLen)) {
                tagName = ElementName.ANNOTATION_XML;
            } else {
                nonInternedTagName.setNameForNonInterned(Portability.newLocalNameFromBuffer(strBuf, strBufLen,
                        interner)
                        // CPPONLY: , true
                        );
                tagName = nonInternedTagName;
            }
        } else {
            tagName = ElementName.elementNameByBuffer(strBuf, strBufLen);
            if (tagName == null) {
                nonInternedTagName.setNameForNonInterned(Portability.newLocalNameFromBuffer(strBuf, strBufLen,
                    interner)
                        // CPPONLY: , false
                        );
                tagName = nonInternedTagName;
            }
        }
        containsHyphen = false;
        clearStrBufAfterUse();
    }

    /**
     * Emits a tag token.
     *
     * NOTE: The method may set <code>shouldSuspend</code>, so the caller
     * must have this pattern after the state's <code>transition</code> call:
     * <pre>
     * if (shouldSuspend) {
     *     break stateloop;
     * }
     * continue stateloop;
     * </pre>
     *
     * @param selfClosing
     * @param pos
     * @return
     * @throws SAXException
     */
    private int emitCurrentTagToken(boolean selfClosing, int pos)
            throws SAXException {
        // CPPONLY: RememberGt(pos);
        cstart = pos + 1;
        maybeErrSlashInEndTag(selfClosing);
        stateSave = Tokenizer.DATA;
        HtmlAttributes attrs = (attributes == null ? HtmlAttributes.EMPTY_ATTRIBUTES
                : attributes);
        if (endTag) {
            /*
             * When an end tag token is emitted, the content model flag must be
             * switched to the PCDATA state.
             */
            maybeErrAttributesOnEndTag(attrs);
            // CPPONLY: if (!viewingXmlSource) {
            tokenHandler.endTag(tagName);
            // CPPONLY: }
            // CPPONLY: if (newAttributesEachTime) {
            // CPPONLY:   Portability.delete(attributes);
            // CPPONLY:   attributes = null;
            // CPPONLY: }
        } else {
            // CPPONLY: if (viewingXmlSource) {
            // CPPONLY:   assert newAttributesEachTime;
            // CPPONLY:   Portability.delete(attributes);
            // CPPONLY:   attributes = null;
            // CPPONLY: } else {
            tokenHandler.startTag(tagName, attrs, selfClosing);
            // CPPONLY: }
        }
        tagName = null;
        if (newAttributesEachTime) {
            attributes = null;
        } else {
            attributes.clear(mappingLangToXmlLang);
        }
        /*
         * The token handler may have called setStateAndEndTagExpectation
         * and changed stateSave since the start of this method.
         */
        suspendIfRequestedAfterCurrentNonTextToken();
        return stateSave;
    }

    private void attributeNameComplete() throws SAXException {
        attributeName = AttributeName.nameByBuffer(strBuf, strBufLen, interner);
        if (attributeName == null) {
            // [NOCPP[
            attributeName = AttributeName.createAttributeName(
                    Portability.newLocalNameFromBuffer(strBuf, strBufLen,
                            interner),
                    namePolicy != XmlViolationPolicy.ALLOW);
            // ]NOCPP]
            // CPPONLY:     nonInternedAttributeName.setNameForNonInterned(Portability.newLocalNameFromBuffer(strBuf, strBufLen, interner));
            // CPPONLY:     attributeName = nonInternedAttributeName;
        }
        clearStrBufAfterUse();

        if (attributes == null) {
            attributes = new HtmlAttributes(mappingLangToXmlLang);
        }

        /*
         * When the user agent leaves the attribute name state (and before
         * emitting the tag token, if appropriate), the complete attribute's
         * name must be compared to the other attributes on the same token; if
         * there is already an attribute on the token with the exact same name,
         * then this is a parse error and the new attribute must be dropped,
         * along with the value that gets associated with it (if any).
         */
        if (attributes.contains(attributeName)) {
            errDuplicateAttribute();
            attributeName = null;
        }
    }

    private void addAttributeWithoutValue() throws SAXException {
        noteAttributeWithoutValue();

        // [NOCPP[
        if (metaBoundaryPassed && AttributeName.CHARSET == attributeName
                && ElementName.META == tagName) {
            err("A \u201Ccharset\u201D attribute on a \u201Cmeta\u201D element found after the first 1024 bytes.");
        }
        // ]NOCPP]
        if (attributeName != null) {
            // [NOCPP[
            if (AttributeName.SRC == attributeName
                    || AttributeName.HREF == attributeName) {
                warn("Attribute \u201C"
                        + attributeName.getLocal(AttributeName.HTML)
                        + "\u201D without an explicit value seen. The attribute may be dropped by IE7.");
            }
            // ]NOCPP]
            attributes.addAttribute(attributeName,
                    Portability.newEmptyString()
                    // [NOCPP[
                    , xmlnsPolicy
            // ]NOCPP]
            // CPPONLY: , attributeLine
            );
            attributeName = null;
        } else {
            clearStrBufAfterUse();
        }
    }

    private void addAttributeWithValue() throws SAXException {
        // [NOCPP[
        if (metaBoundaryPassed && ElementName.META == tagName
                && AttributeName.CHARSET == attributeName) {
            err("A \u201Ccharset\u201D attribute on a \u201Cmeta\u201D element found after the first 1024 bytes.");
        }
        // ]NOCPP]
        if (attributeName != null) {
            String val = strBufToAttributeValueString(); // Ownership transferred to
            // HtmlAttributes
            // CPPONLY: if (mViewSource) {
            // CPPONLY:   mViewSource.MaybeLinkifyAttributeValue(attributeName, val);
            // CPPONLY: }
            attributes.addAttribute(attributeName, val
            // [NOCPP[
                    , xmlnsPolicy
            // ]NOCPP]
            // CPPONLY: , attributeLine
            );
            attributeName = null;
        } else {
            // We have a duplicate attribute. Explicitly discard its value.
            clearStrBufAfterUse();
        }
    }

    // [NOCPP[

    protected void startErrorReporting() throws SAXException {

    }

    // ]NOCPP]

    public void start() throws SAXException {
        initializeWithoutStarting();
        tokenHandler.startTokenization(this);
        // CPPONLY: if (mViewSource) {
        // CPPONLY:   line = 1;
        // CPPONLY:   col = -1;
        // CPPONLY:   nextCharOnNewLine = false;
        // CPPONLY: } else if (tokenHandler.WantsLineAndColumn()) {
        // CPPONLY:   line = 0;
        // CPPONLY:   col = 1;
        // CPPONLY:   nextCharOnNewLine = true;
        // CPPONLY: } else {
        // CPPONLY:   line = -1;
        // CPPONLY:   col = -1;
        // CPPONLY:   nextCharOnNewLine = false;
        // CPPONLY: }
        // [NOCPP[
        startErrorReporting();
        // ]NOCPP]
    }

    public boolean tokenizeBuffer(UTF16Buffer buffer) throws SAXException {
        int state = stateSave;
        int returnState = returnStateSave;
        char c = '\u0000';
        shouldSuspend = false;
        lastCR = false;

        int start = buffer.getStart();
        int end = buffer.getEnd();

        // In C++, the caller of tokenizeBuffer needs to do this explicitly.
        // [NOCPP[
        ensureBufferSpace(end - start);
        // ]NOCPP]

        /**
         * The index of the last <code>char</code> read from <code>buf</code>.
         */
        int pos = start - 1;

        switch (state) {
            case DATA:
            case RCDATA:
            case SCRIPT_DATA:
            case PLAINTEXT:
            case RAWTEXT:
            case CDATA_SECTION:
            case SCRIPT_DATA_ESCAPED:
            case SCRIPT_DATA_ESCAPE_START:
            case SCRIPT_DATA_ESCAPE_START_DASH:
            case SCRIPT_DATA_ESCAPED_DASH:
            case SCRIPT_DATA_ESCAPED_DASH_DASH:
            case SCRIPT_DATA_DOUBLE_ESCAPE_START:
            case SCRIPT_DATA_DOUBLE_ESCAPED:
            case SCRIPT_DATA_DOUBLE_ESCAPED_LESS_THAN_SIGN:
            case SCRIPT_DATA_DOUBLE_ESCAPED_DASH:
            case SCRIPT_DATA_DOUBLE_ESCAPED_DASH_DASH:
            case SCRIPT_DATA_DOUBLE_ESCAPE_END:
                cstart = start;
                break;
            default:
                cstart = Integer.MAX_VALUE;
                break;
        }

        // CPPONLY: if (mViewSource) {
        // CPPONLY:   mViewSource.SetBuffer(buffer);
        // CPPONLY:   if (htmlaccelEnabled()) {
        // CPPONLY:     pos = StateLoopViewSourceSIMD(state, c, pos, buffer.getBuffer(), false, returnState, buffer.getEnd());
        // CPPONLY:   } else {
        // CPPONLY:     pos = StateLoopViewSourceALU(state, c, pos, buffer.getBuffer(), false, returnState, buffer.getEnd());
        // CPPONLY:   }
        // CPPONLY:   mViewSource.DropBuffer((pos == buffer.getEnd()) ? pos : pos + 1);
        // CPPONLY: } else if (tokenHandler.WantsLineAndColumn()) {
        // CPPONLY:   if (htmlaccelEnabled()) {
        // CPPONLY:     pos = StateLoopLineColSIMD(state, c, pos, buffer.getBuffer(), false, returnState, buffer.getEnd());
        // CPPONLY:   } else {
        // CPPONLY:     pos = StateLoopLineColALU(state, c, pos, buffer.getBuffer(), false, returnState, buffer.getEnd());
        // CPPONLY:   }
        // CPPONLY: } else if (htmlaccelEnabled()) {
        // CPPONLY:   pos = StateLoopFastestSIMD(state, c, pos, buffer.getBuffer(), false, returnState, buffer.getEnd());
        // CPPONLY: } else {
        // CPPONLY:   pos = StateLoopFastestALU(state, c, pos, buffer.getBuffer(), false, returnState, buffer.getEnd());
        // CPPONLY: }
        // [NOCPP[
        pos = stateLoop(state, c, pos, buffer.getBuffer(), false, returnState,
                end);
        // ]NOCPP]
        if (pos == end) {
            // exiting due to end of buffer
            buffer.setStart(pos);
        } else {
            buffer.setStart(pos + 1);
        }
        return lastCR;
    }

    // [NOCPP[
    private void ensureBufferSpace(int inputLength) throws SAXException {
        // Add 2 to account for emissions of LT_GT, LT_SOLIDUS and RSQB_RSQB.
        // Adding to the general worst case instead of only the
        // TreeBuilder-exposed worst case to avoid re-introducing a bug when
        // unifying the tokenizer and tree builder buffers in the future.
        int worstCase = strBufLen + inputLength + charRefBufLen + 2;
        tokenHandler.ensureBufferSpace(worstCase);
        if (commentPolicy == XmlViolationPolicy.ALTER_INFOSET) {
            // When altering infoset, if the comment contents are consecutive
            // hyphens, each hyphen generates a space, too. These buffer
            // contents never get emitted as characters() to the tokenHandler,
            // which is why this calculation happens after the call to
            // ensureBufferSpace on tokenHandler.
            worstCase *= 2;
        }
        if (strBuf == null) {
            // Add an arbitrary small value to avoid immediate reallocation
            // once there are a few characters in the buffer.
            strBuf = new char[worstCase + 128];
        } else if (worstCase > strBuf.length) {
            // HotSpot reportedly allocates memory with 8-byte accuracy, so
            // there's no point in trying to do math here to avoid slop.
            // Maybe we should add some small constant to worstCase here
            // but not doing that without profiling. In C++ with jemalloc,
            // the corresponding method should do math to round up here
            // to avoid slop.
            char[] newBuf = new char[Math.max(worstCase, (strBuf.length*5)/4)];
            System.arraycopy(strBuf, 0, newBuf, 0, strBufLen);
            strBuf = newBuf;
        }
    }
    // ]NOCPP]

    @SuppressWarnings("unused") @Inline private int stateLoop(int state, char c,
            int pos, @NoLength char[] buf, boolean reconsume, int returnState,
            int endPos) throws SAXException {
        boolean reportedConsecutiveHyphens = false;
        /*
         * Idioms used in this code:
         *
         *
         * Consuming the next input character
         *
         * To consume the next input character, the code does this: if (++pos ==
         * endPos) { break stateloop; } c = checkChar(buf, pos);
         *
         *
         * Staying in a state
         *
         * When there's a state that the tokenizer may stay in over multiple
         * input characters, the state has a wrapper |for(;;)| loop and staying
         * in the state continues the loop.
         *
         *
         * Switching to another state
         *
         * To switch to another state, the code sets the state variable to the
         * magic number of the new state. Then it either continues stateloop or
         * breaks out of the state's own wrapper loop if the target state is
         * right after the current state in source order. (This is a partial
         * workaround for Java's lack of goto.)
         *
         *
         * Reconsume support
         *
         * The spec sometimes says that an input character is reconsumed in
         * another state. If a state can ever be entered so that an input
         * character can be reconsumed in it, the state's code starts with an
         * |if (reconsume)| that sets reconsume to false and skips over the
         * normal code for consuming a new character.
         *
         * To reconsume the current character in another state, the code sets
         * |reconsume| to true and then switches to the other state.
         *
         *
         * Emitting character tokens
         *
         * This method emits character tokens lazily. Whenever a new range of
         * character tokens starts, the field cstart must be set to the start
         * index of the range. The flushChars() method must be called at the end
         * of a range to flush it.
         *
         *
         * U+0000 handling
         *
         * The various states have to handle the replacement of U+0000 with
         * U+FFFD. However, if U+0000 would be reconsumed in another state, the
         * replacement doesn't need to happen, because it's handled by the
         * reconsuming state.
         *
         *
         * LF handling
         *
         * Every state needs to increment the line number upon LF unless the LF
         * gets reconsumed by another state which increments the line number.
         *
         *
         * CR handling
         *
         * Every state needs to handle CR unless the CR gets reconsumed and is
         * handled by the reconsuming state. The CR needs to be handled as if it
         * were and LF, the lastCR field must be set to true and then this
         * method must return. The IO driver will then swallow the next
         * character if it is an LF to coalesce CRLF.
         */
        stateloop: for (;;) {
            switch (state) {
                case DATA:
                    dataloop: for (;;) {
                        if (reconsume) {
                            reconsume = false;
                        } else {
                            ++pos;
                            // Perhaps at some point, it will be appropriate to do SIMD in Java, but not today.
                            // The line below advances pos by some number of code units that this state is indifferent to.
                            // CPPONLY: pos += accelerateAdvancementData(buf, pos, endPos);
                            if (pos == endPos) {
                                break stateloop;
                            }
                            c = checkChar(buf, pos);
                        }
                        switch (c) {
                            case '&':
                                /*
                                 * U+0026 AMPERSAND (&) Switch to the character
                                 * reference in data state.
                                 */
                                flushChars(buf, pos);
                                assert charRefBufLen == 0: "charRefBufLen not reset after previous use!";
                                appendCharRefBuf(c);
                                setAdditionalAndRememberAmpersandLocation('\u0000');
                                returnState = state;
                                state = transition(state, Tokenizer.CONSUME_CHARACTER_REFERENCE, reconsume, pos);
                                continue stateloop;
                            case '<':
                                /*
                                 * U+003C LESS-THAN SIGN (<) Switch to the tag
                                 * open state.
                                 */
                                flushChars(buf, pos);

                                state = transition(state, Tokenizer.TAG_OPEN, reconsume, pos);
                                // `break` optimizes; `continue stateloop;` would be valid
                                break dataloop;
                            case '\u0000':
                                maybeEmitReplacementCharacter(buf, pos);
                                continue;
                            case '\r':
                                emitCarriageReturn(buf, pos);
                                break stateloop;
                            case '\n':
                                silentLineFeed();
                                // CPPONLY: MOZ_FALLTHROUGH;
                            default:
                                /*
                                 * Anything else Emit the input character as a
                                 * character token.
                                 *
                                 * Stay in the data state.
                                 */
                                continue;
                        }
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case TAG_OPEN:
                    tagopenloop: for (;;) {
                        /*
                         * The behavior of this state depends on the content
                         * model flag.
                         */
                        if (++pos == endPos) {
                            break stateloop;
                        }
                        c = checkChar(buf, pos);
                        /*
                         * If the content model flag is set to the PCDATA state
                         * Consume the next input character:
                         */
                        if (c >= 'A' && c <= 'Z') {
                            /*
                             * U+0041 LATIN CAPITAL LETTER A through to U+005A
                             * LATIN CAPITAL LETTER Z Create a new start tag
                             * token,
                             */
                            endTag = false;
                            /*
                             * set its tag name to the lowercase version of the
                             * input character (add 0x0020 to the character's
                             * code point),
                             */
                            clearStrBufBeforeUse();
                            appendStrBuf((char) (c + 0x20));
                            containsHyphen = false;
                            /* then switch to the tag name state. */
                            state = transition(state, Tokenizer.TAG_NAME, reconsume, pos);
                            /*
                             * (Don't emit the token yet; further details will
                             * be filled in before it is emitted.)
                             */
                            // `break` optimizes; `continue stateloop;` would be valid
                            break tagopenloop;
                        } else if (c >= 'a' && c <= 'z') {
                            /*
                             * U+0061 LATIN SMALL LETTER A through to U+007A
                             * LATIN SMALL LETTER Z Create a new start tag
                             * token,
                             */
                            endTag = false;
                            /*
                             * set its tag name to the input character,
                             */
                            clearStrBufBeforeUse();
                            appendStrBuf(c);
                            containsHyphen = false;
                            /* then switch to the tag name state. */
                            state = transition(state, Tokenizer.TAG_NAME, reconsume, pos);
                            /*
                             * (Don't emit the token yet; further details will
                             * be filled in before it is emitted.)
                             */
                            // `break` optimizes; `continue stateloop;` would be valid
                            break tagopenloop;
                        }
                        switch (c) {
                            case '!':
                                /*
                                 * U+0021 EXCLAMATION MARK (!) Switch to the
                                 * markup declaration open state.
                                 */
                                state = transition(state, Tokenizer.MARKUP_DECLARATION_OPEN, reconsume, pos);
                                continue stateloop;
                            case '/':
                                /*
                                 * U+002F SOLIDUS (/) Switch to the close tag
                                 * open state.
                                 */
                                state = transition(state, Tokenizer.CLOSE_TAG_OPEN, reconsume, pos);
                                continue stateloop;
                            case '?':
                                // CPPONLY: if (viewingXmlSource) {
                                // CPPONLY: state = transition(state,
                                // CPPONLY: Tokenizer.PROCESSING_INSTRUCTION,
                                // CPPONLY: reconsume,
                                // CPPONLY: pos);
                                // CPPONLY: continue stateloop;
                                // CPPONLY: }
                                if (processingInstructionsEnabled) {
                                    /*
                                     * U+003F QUESTION MARK (?) Set the
                                     * temporary buffer to the empty string.
                                     * Switch to the processing instruction
                                     * open state.
                                     *
                                     * The U+003F is kept at the start of the
                                     * buffer so that converting the buffer to
                                     * a comment needs no further work.
                                     */
                                    clearStrBufBeforeUse();
                                    appendStrBuf(c);
                                    state = transition(state, Tokenizer.PROCESSING_INSTRUCTION_OPEN, reconsume, pos);
                                    continue stateloop;
                                }
                                /*
                                 * U+003F QUESTION MARK (?) Parse error.
                                 */
                                errProcessingInstruction();
                                /*
                                 * Switch to the bogus comment state.
                                 */
                                clearStrBufBeforeUse();
                                appendStrBuf(c);
                                state = transition(state, Tokenizer.BOGUS_COMMENT, reconsume, pos);
                                continue stateloop;
                            case '>':
                                /*
                                 * U+003E GREATER-THAN SIGN (>) Parse error.
                                 */
                                errLtGt();
                                /*
                                 * Emit a U+003C LESS-THAN SIGN character token
                                 * and a U+003E GREATER-THAN SIGN character
                                 * token.
                                 */
                                tokenHandler.characters(Tokenizer.LT_GT, 0, 2);
                                /* Switch to the data state. */
                                cstart = pos + 1;
                                state = transition(state, Tokenizer.DATA, reconsume, pos);
                                continue stateloop;
                            default:
                                /*
                                 * Anything else Parse error.
                                 */
                                errBadCharAfterLt(c);
                                /*
                                 * Emit a U+003C LESS-THAN SIGN character token
                                 */
                                tokenHandler.characters(Tokenizer.LT_GT, 0, 1);
                                /*
                                 * and reconsume the current input character in
                                 * the data state.
                                 */
                                cstart = pos;
                                reconsume = true;
                                state = transition(state, Tokenizer.DATA, reconsume, pos);
                                continue stateloop;
                        }
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case TAG_NAME:
                    tagnameloop: for (;;) {
                        if (++pos == endPos) {
                            break stateloop;
                        }
                        c = checkChar(buf, pos);
                        /*
                         * Consume the next input character:
                         */
                        switch (c) {
                            case '\r':
                                silentCarriageReturn();
                                strBufToElementNameString();
                                state = transition(state, Tokenizer.BEFORE_ATTRIBUTE_NAME, reconsume, pos);
                                break stateloop;
                            case '\n':
                                silentLineFeed();
                                // CPPONLY: MOZ_FALLTHROUGH;
                            case ' ':
                            case '\t':
                            case '\u000C':
                                /*
                                 * U+0009 CHARACTER TABULATION U+000A LINE FEED
                                 * (LF) U+000C FORM FEED (FF) U+0020 SPACE
                                 * Switch to the before attribute name state.
                                 */
                                strBufToElementNameString();
                                state = transition(state, Tokenizer.BEFORE_ATTRIBUTE_NAME, reconsume, pos);
                                // `break` optimizes; `continue stateloop;` would be valid
                                break tagnameloop;
                            case '/':
                                /*
                                 * U+002F SOLIDUS (/) Switch to the self-closing
                                 * start tag state.
                                 */
                                strBufToElementNameString();
                                state = transition(state, Tokenizer.SELF_CLOSING_START_TAG, reconsume, pos);
                                continue stateloop;
                            case '>':
                                /*
                                 * U+003E GREATER-THAN SIGN (>) Emit the current
                                 * tag token.
                                 */
                                strBufToElementNameString();
                                state = transition(state, emitCurrentTagToken(false, pos), reconsume, pos);
                                if (shouldSuspend) {
                                    break stateloop;
                                }
                                /*
                                 * Switch to the data state.
                                 */
                                continue stateloop;
                            case '\u0000':
                                c = '\uFFFD';
                                // CPPONLY: MOZ_FALLTHROUGH;
                            default:
                                if (c >= 'A' && c <= 'Z') {
                                    /*
                                     * U+0041 LATIN CAPITAL LETTER A through to
                                     * U+005A LATIN CAPITAL LETTER Z Append the
                                     * lowercase version of the current input
                                     * character (add 0x0020 to the character's
                                     * code point) to the current tag token's
                                     * tag name.
                                     */
                                    c += 0x20;
                                } else if (c == '-') {
                                    containsHyphen = true;
                                }
                                /*
                                 * Anything else Append the current input
                                 * character to the current tag token's tag
                                 * name.
                                 */
                                appendStrBuf(c);
                                /*
                                 * Stay in the tag name state.
                                 */
                                continue;
                        }
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case BEFORE_ATTRIBUTE_NAME:
                    beforeattributenameloop: for (;;) {
                        if (reconsume) {
                            reconsume = false;
                        } else {
                            if (++pos == endPos) {
                                break stateloop;
                            }
                            c = checkChar(buf, pos);
                        }
                        /*
                         * Consume the next input character:
                         */
                        switch (c) {
                            case '\r':
                                silentCarriageReturn();
                                break stateloop;
                            case '\n':
                                silentLineFeed();
                                // CPPONLY: MOZ_FALLTHROUGH;
                            case ' ':
                            case '\t':
                            case '\u000C':
                                /*
                                 * U+0009 CHARACTER TABULATION U+000A LINE FEED
                                 * (LF) U+000C FORM FEED (FF) U+0020 SPACE Stay
                                 * in the before attribute name state.
                                 */
                                continue;
                            case '/':
                                /*
                                 * U+002F SOLIDUS (/) Switch to the self-closing
                                 * start tag state.
                                 */
                                state = transition(state, Tokenizer.SELF_CLOSING_START_TAG, reconsume, pos);
                                continue stateloop;
                            case '>':
                                /*
                                 * U+003E GREATER-THAN SIGN (>) Emit the current
                                 * tag token.
                                 */
                                state = transition(state, emitCurrentTagToken(false, pos), reconsume, pos);
                                if (shouldSuspend) {
                                    break stateloop;
                                }
                                /*
                                 * Switch to the data state.
                                 */
                                continue stateloop;
                            case '\u0000':
                                c = '\uFFFD';
                                // CPPONLY: MOZ_FALLTHROUGH;
                            case '\"':
                            case '\'':
                            case '<':
                            case '=':
                                /*
                                 * U+0022 QUOTATION MARK (") U+0027 APOSTROPHE
                                 * (') U+003C LESS-THAN SIGN (<) U+003D EQUALS
                                 * SIGN (=) Parse error.
                                 */
                                errBadCharBeforeAttributeNameOrNull(c);
                                /*
                                 * Treat it as per the "anything else" entry
                                 * below.
                                 */
                                // CPPONLY: MOZ_FALLTHROUGH;
                            default:
                                /*
                                 * Anything else Start a new attribute in the
                                 * current tag token.
                                 */
                                if (c >= 'A' && c <= 'Z') {
                                    /*
                                     * U+0041 LATIN CAPITAL LETTER A through to
                                     * U+005A LATIN CAPITAL LETTER Z Set that
                                     * attribute's name to the lowercase version
                                     * of the current input character (add
                                     * 0x0020 to the character's code point)
                                     */
                                    c += 0x20;
                                }
                                // CPPONLY: attributeLine = line;
                                /*
                                 * Set that attribute's name to the current
                                 * input character,
                                 */
                                clearStrBufBeforeUse();
                                appendStrBuf(c);
                                /*
                                 * and its value to the empty string.
                                 */
                                // Will do later.
                                /*
                                 * Switch to the attribute name state.
                                 */
                                state = transition(state, Tokenizer.ATTRIBUTE_NAME, reconsume, pos);
                                // `break` optimizes; `continue stateloop;` would be valid
                                break beforeattributenameloop;
                        }
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case ATTRIBUTE_NAME:
                    attributenameloop: for (;;) {
                        if (++pos == endPos) {
                            break stateloop;
                        }
                        c = checkChar(buf, pos);
                        /*
                         * Consume the next input character:
                         */
                        switch (c) {
                            case '\r':
                                silentCarriageReturn();
                                attributeNameComplete();
                                state = transition(state, Tokenizer.AFTER_ATTRIBUTE_NAME, reconsume, pos);
                                break stateloop;
                            case '\n':
                                silentLineFeed();
                                // CPPONLY: MOZ_FALLTHROUGH;
                            case ' ':
                            case '\t':
                            case '\u000C':
                                /*
                                 * U+0009 CHARACTER TABULATION U+000A LINE FEED
                                 * (LF) U+000C FORM FEED (FF) U+0020 SPACE
                                 * Switch to the after attribute name state.
                                 */
                                attributeNameComplete();
                                state = transition(state, Tokenizer.AFTER_ATTRIBUTE_NAME, reconsume, pos);
                                continue stateloop;
                            case '/':
                                /*
                                 * U+002F SOLIDUS (/) Switch to the self-closing
                                 * start tag state.
                                 */
                                attributeNameComplete();
                                addAttributeWithoutValue();
                                state = transition(state, Tokenizer.SELF_CLOSING_START_TAG, reconsume, pos);
                                continue stateloop;
                            case '=':
                                /*
                                 * U+003D EQUALS SIGN (=) Switch to the before
                                 * attribute value state.
                                 */
                                attributeNameComplete();
                                state = transition(state, Tokenizer.BEFORE_ATTRIBUTE_VALUE, reconsume, pos);
                                // `break` optimizes; `continue stateloop;` would be valid
                                break attributenameloop;
                            case '>':
                                /*
                                 * U+003E GREATER-THAN SIGN (>) Emit the current
                                 * tag token.
                                 */
                                attributeNameComplete();
                                addAttributeWithoutValue();
                                state = transition(state, emitCurrentTagToken(false, pos), reconsume, pos);
                                if (shouldSuspend) {
                                    break stateloop;
                                }
                                /*
                                 * Switch to the data state.
                                 */
                                continue stateloop;
                            case '\u0000':
                                c = '\uFFFD';
                                // CPPONLY: MOZ_FALLTHROUGH;
                            case '\"':
                            case '\'':
                            case '<':
                                /*
                                 * U+0022 QUOTATION MARK (") U+0027 APOSTROPHE
                                 * (') U+003C LESS-THAN SIGN (<) Parse error.
                                 */
                                errQuoteOrLtInAttributeNameOrNull(c);
                                /*
                                 * Treat it as per the "anything else" entry
                                 * below.
                                 */
                                // CPPONLY: MOZ_FALLTHROUGH;
                            default:
                                if (c >= 'A' && c <= 'Z') {
                                    /*
                                     * U+0041 LATIN CAPITAL LETTER A through to
                                     * U+005A LATIN CAPITAL LETTER Z Append the
                                     * lowercase version of the current input
                                     * character (add 0x0020 to the character's
                                     * code point) to the current attribute's
                                     * name.
                                     */
                                    c += 0x20;
                                }
                                /*
                                 * Anything else Append the current input
                                 * character to the current attribute's name.
                                 */
                                appendStrBuf(c);
                                /*
                                 * Stay in the attribute name state.
                                 */
                                continue;
                        }
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case BEFORE_ATTRIBUTE_VALUE:
                    beforeattributevalueloop: for (;;) {
                        if (++pos == endPos) {
                            break stateloop;
                        }
                        c = checkChar(buf, pos);
                        /*
                         * Consume the next input character:
                         */
                        switch (c) {
                            case '\r':
                                silentCarriageReturn();
                                break stateloop;
                            case '\n':
                                silentLineFeed();
                                // CPPONLY: MOZ_FALLTHROUGH;
                            case ' ':
                            case '\t':
                            case '\u000C':
                                /*
                                 * U+0009 CHARACTER TABULATION U+000A LINE FEED
                                 * (LF) U+000C FORM FEED (FF) U+0020 SPACE Stay
                                 * in the before attribute value state.
                                 */
                                continue;
                            case '"':
                                /*
                                 * U+0022 QUOTATION MARK (") Switch to the
                                 * attribute value (double-quoted) state.
                                 */
                                // CPPONLY: attributeLine = line;
                                clearStrBufBeforeUse();
                                state = transition(state, Tokenizer.ATTRIBUTE_VALUE_DOUBLE_QUOTED, reconsume, pos);
                                // `break` optimizes; `continue stateloop;` would be valid
                                break beforeattributevalueloop;
                            case '&':
                                /*
                                 * U+0026 AMPERSAND (&) Switch to the attribute
                                 * value (unquoted) state and reconsume this
                                 * input character.
                                 */
                                // CPPONLY: attributeLine = line;
                                clearStrBufBeforeUse();
                                reconsume = true;
                                state = transition(state, Tokenizer.ATTRIBUTE_VALUE_UNQUOTED, reconsume, pos);
                                noteUnquotedAttributeValue();
                                continue stateloop;
                            case '\'':
                                /*
                                 * U+0027 APOSTROPHE (') Switch to the attribute
                                 * value (single-quoted) state.
                                 */
                                // CPPONLY: attributeLine = line;
                                clearStrBufBeforeUse();
                                state = transition(state, Tokenizer.ATTRIBUTE_VALUE_SINGLE_QUOTED, reconsume, pos);
                                continue stateloop;
                            case '>':
                                /*
                                 * U+003E GREATER-THAN SIGN (>) Parse error.
                                 */
                                errAttributeValueMissing();
                                /*
                                 * Emit the current tag token.
                                 */
                                addAttributeWithoutValue();
                                state = transition(state, emitCurrentTagToken(false, pos), reconsume, pos);
                                if (shouldSuspend) {
                                    break stateloop;
                                }
                                /*
                                 * Switch to the data state.
                                 */
                                continue stateloop;
                            case '\u0000':
                                c = '\uFFFD';
                                // CPPONLY: MOZ_FALLTHROUGH;
                            case '<':
                            case '=':
                            case '`':
                                /*
                                 * U+003C LESS-THAN SIGN (<) U+003D EQUALS SIGN
                                 * (=) U+0060 GRAVE ACCENT (`)
                                 */
                                errLtOrEqualsOrGraveInUnquotedAttributeOrNull(c);
                                /*
                                 * Treat it as per the "anything else" entry
                                 * below.
                                 */
                                // CPPONLY: MOZ_FALLTHROUGH;
                            default:
                                /*
                                 * Anything else Append the current input
                                 * character to the current attribute's value.
                                 */
                                // CPPONLY: attributeLine = line;
                                clearStrBufBeforeUse();
                                appendStrBuf(c);
                                /*
                                 * Switch to the attribute value (unquoted)
                                 * state.
                                 */

                                state = transition(state, Tokenizer.ATTRIBUTE_VALUE_UNQUOTED, reconsume, pos);
                                noteUnquotedAttributeValue();
                                continue stateloop;
                        }
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case ATTRIBUTE_VALUE_DOUBLE_QUOTED:
                    attributevaluedoublequotedloop: for (;;) {
                        if (reconsume) {
                            reconsume = false;
                        } else {
                            ++pos;
                            // Perhaps at some point, it will be appropriate to do SIMD in Java, but not today.
                            // The line below advances pos by some number of code units that this state is indifferent to.
                            // CPPONLY: pos += accelerateAdvancementAttributeValueDoubleQuoted(buf, pos, endPos);
                            if (pos == endPos) {
                                break stateloop;
                            }
                            c = checkChar(buf, pos);
                        }
                        /*
                         * Consume the next input character:
                         */
                        switch (c) {
                            case '"':
                                /*
                                 * U+0022 QUOTATION MARK (") Switch to the after
                                 * attribute value (quoted) state.
                                 */
                                addAttributeWithValue();

                                state = transition(state, Tokenizer.AFTER_ATTRIBUTE_VALUE_QUOTED, reconsume, pos);
                                // `break` optimizes; `continue stateloop;` would be valid
                                break attributevaluedoublequotedloop;
                            case '&':
                                /*
                                 * U+0026 AMPERSAND (&) Switch to the character
                                 * reference in attribute value state, with the
                                 * additional allowed character being U+0022
                                 * QUOTATION MARK (").
                                 */
                                assert charRefBufLen == 0: "charRefBufLen not reset after previous use!";
                                appendCharRefBuf(c);
                                setAdditionalAndRememberAmpersandLocation('\"');
                                returnState = state;
                                state = transition(state, Tokenizer.CONSUME_CHARACTER_REFERENCE, reconsume, pos);
                                continue stateloop;
                            case '\r':
                                appendStrBufCarriageReturn();
                                break stateloop;
                            case '\n':
                                appendStrBufLineFeed();
                                continue;
                            case '\u0000':
                                c = '\uFFFD';
                                // CPPONLY: MOZ_FALLTHROUGH;
                            default:
                                /*
                                 * Anything else Append the current input
                                 * character to the current attribute's value.
                                 */
                                appendStrBuf(c);
                                /*
                                 * Stay in the attribute value (double-quoted)
                                 * state.
                                 */
                                continue;
                        }
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case AFTER_ATTRIBUTE_VALUE_QUOTED:
                    afterattributevaluequotedloop: for (;;) {
                        if (++pos == endPos) {
                            break stateloop;
                        }
                        c = checkChar(buf, pos);
                        /*
                         * Consume the next input character:
                         */
                        switch (c) {
                            case '\r':
                                silentCarriageReturn();
                                state = transition(state, Tokenizer.BEFORE_ATTRIBUTE_NAME, reconsume, pos);
                                break stateloop;
                            case '\n':
                                silentLineFeed();
                                // CPPONLY: MOZ_FALLTHROUGH;
                            case ' ':
                            case '\t':
                            case '\u000C':
                                /*
                                 * U+0009 CHARACTER TABULATION U+000A LINE FEED
                                 * (LF) U+000C FORM FEED (FF) U+0020 SPACE
                                 * Switch to the before attribute name state.
                                 */
                                state = transition(state, Tokenizer.BEFORE_ATTRIBUTE_NAME, reconsume, pos);
                                continue stateloop;
                            case '/':
                                /*
                                 * U+002F SOLIDUS (/) Switch to the self-closing
                                 * start tag state.
                                 */
                                state = transition(state, Tokenizer.SELF_CLOSING_START_TAG, reconsume, pos);
                                // `break` optimizes; `continue stateloop;` would be valid
                                break afterattributevaluequotedloop;
                            case '>':
                                /*
                                 * U+003E GREATER-THAN SIGN (>) Emit the current
                                 * tag token.
                                 */
                                state = transition(state, emitCurrentTagToken(false, pos), reconsume, pos);
                                if (shouldSuspend) {
                                    break stateloop;
                                }
                                /*
                                 * Switch to the data state.
                                 */
                                continue stateloop;
                            default:
                                /*
                                 * Anything else Parse error.
                                 */
                                errNoSpaceBetweenAttributes();
                                /*
                                 * Reconsume the character in the before
                                 * attribute name state.
                                 */
                                reconsume = true;
                                state = transition(state, Tokenizer.BEFORE_ATTRIBUTE_NAME, reconsume, pos);
                                continue stateloop;
                        }
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case SELF_CLOSING_START_TAG:
                    if (++pos == endPos) {
                        break stateloop;
                    }
                    c = checkChar(buf, pos);
                    /*
                     * Consume the next input character:
                     */
                    switch (c) {
                        case '>':
                            /*
                             * U+003E GREATER-THAN SIGN (>) Set the self-closing
                             * flag of the current tag token. Emit the current
                             * tag token.
                             */
                            state = transition(state, emitCurrentTagToken(true, pos), reconsume, pos);
                            if (shouldSuspend) {
                                break stateloop;
                            }
                            /*
                             * Switch to the data state.
                             */
                            continue stateloop;
                        default:
                            /* Anything else Parse error. */
                            errSlashNotFollowedByGt();
                            /*
                             * Reconsume the character in the before attribute
                             * name state.
                             */
                            reconsume = true;
                            state = transition(state, Tokenizer.BEFORE_ATTRIBUTE_NAME, reconsume, pos);
                            continue stateloop;
                    }
                    // no fallthrough, reordering opportunity
                case ATTRIBUTE_VALUE_UNQUOTED:
                    for (;;) {
                        if (reconsume) {
                            reconsume = false;
                        } else {
                            if (++pos == endPos) {
                                break stateloop;
                            }
                            c = checkChar(buf, pos);
                        }
                        /*
                         * Consume the next input character:
                         */
                        switch (c) {
                            case '\r':
                                silentCarriageReturn();
                                addAttributeWithValue();
                                state = transition(state, Tokenizer.BEFORE_ATTRIBUTE_NAME, reconsume, pos);
                                break stateloop;
                            case '\n':
                                silentLineFeed();
                                // CPPONLY: MOZ_FALLTHROUGH;
                            case ' ':
                            case '\t':
                            case '\u000C':
                                /*
                                 * U+0009 CHARACTER TABULATION U+000A LINE FEED
                                 * (LF) U+000C FORM FEED (FF) U+0020 SPACE
                                 * Switch to the before attribute name state.
                                 */
                                addAttributeWithValue();
                                state = transition(state, Tokenizer.BEFORE_ATTRIBUTE_NAME, reconsume, pos);
                                continue stateloop;
                            case '&':
                                /*
                                 * U+0026 AMPERSAND (&) Switch to the character
                                 * reference in attribute value state, with the
                                 * additional allowed character being U+003E
                                 * GREATER-THAN SIGN (>)
                                 */
                                assert charRefBufLen == 0: "charRefBufLen not reset after previous use!";
                                appendCharRefBuf(c);
                                setAdditionalAndRememberAmpersandLocation('>');
                                returnState = state;
                                state = transition(state, Tokenizer.CONSUME_CHARACTER_REFERENCE, reconsume, pos);
                                continue stateloop;
                            case '>':
                                /*
                                 * U+003E GREATER-THAN SIGN (>) Emit the current
                                 * tag token.
                                 */
                                addAttributeWithValue();
                                state = transition(state, emitCurrentTagToken(false, pos), reconsume, pos);
                                if (shouldSuspend) {
                                    break stateloop;
                                }
                                /*
                                 * Switch to the data state.
                                 */
                                continue stateloop;
                            case '\u0000':
                                c = '\uFFFD';
                                // CPPONLY: MOZ_FALLTHROUGH;
                            case '<':
                            case '\"':
                            case '\'':
                            case '=':
                            case '`':
                                /*
                                 * U+0022 QUOTATION MARK (") U+0027 APOSTROPHE
                                 * (') U+003C LESS-THAN SIGN (<) U+003D EQUALS
                                 * SIGN (=) U+0060 GRAVE ACCENT (`) Parse error.
                                 */
                                errUnquotedAttributeValOrNull(c);
                                /*
                                 * Treat it as per the "anything else" entry
                                 * below.
                                 */
                                // CPPONLY: MOZ_FALLTHROUGH;
                            default:
                                /*
                                 * Anything else Append the current input
                                 * character to the current attribute's value.
                                 */
                                appendStrBuf(c);
                                /*
                                 * Stay in the attribute value (unquoted) state.
                                 */
                                continue;
                        }
                    }
                    // no fallthrough, reordering opportunity
               case AFTER_ATTRIBUTE_NAME:
                    for (;;) {
                        if (++pos == endPos) {
                            break stateloop;
                        }
                        c = checkChar(buf, pos);
                        /*
                         * Consume the next input character:
                         */
                        switch (c) {
                            case '\r':
                                silentCarriageReturn();
                                break stateloop;
                            case '\n':
                                silentLineFeed();
                                // CPPONLY: MOZ_FALLTHROUGH;
                            case ' ':
                            case '\t':
                            case '\u000C':
                                /*
                                 * U+0009 CHARACTER TABULATION U+000A LINE FEED
                                 * (LF) U+000C FORM FEED (FF) U+0020 SPACE Stay
                                 * in the after attribute name state.
                                 */
                                continue;
                            case '/':
                                /*
                                 * U+002F SOLIDUS (/) Switch to the self-closing
                                 * start tag state.
                                 */
                                addAttributeWithoutValue();
                                state = transition(state, Tokenizer.SELF_CLOSING_START_TAG, reconsume, pos);
                                continue stateloop;
                            case '=':
                                /*
                                 * U+003D EQUALS SIGN (=) Switch to the before
                                 * attribute value state.
                                 */
                                state = transition(state, Tokenizer.BEFORE_ATTRIBUTE_VALUE, reconsume, pos);
                                continue stateloop;
                            case '>':
                                /*
                                 * U+003E GREATER-THAN SIGN (>) Emit the current
                                 * tag token.
                                 */
                                addAttributeWithoutValue();
                                state = transition(state, emitCurrentTagToken(false, pos), reconsume, pos);
                                if (shouldSuspend) {
                                    break stateloop;
                                }
                                /*
                                 * Switch to the data state.
                                 */
                                continue stateloop;
                            case '\u0000':
                                c = '\uFFFD';
                                // CPPONLY: MOZ_FALLTHROUGH;
                            case '\"':
                            case '\'':
                            case '<':
                                errQuoteOrLtInAttributeNameOrNull(c);
                                /*
                                 * Treat it as per the "anything else" entry
                                 * below.
                                 */
                                // CPPONLY: MOZ_FALLTHROUGH;
                            default:
                                addAttributeWithoutValue();
                                /*
                                 * Anything else Start a new attribute in the
                                 * current tag token.
                                 */
                                if (c >= 'A' && c <= 'Z') {
                                    /*
                                     * U+0041 LATIN CAPITAL LETTER A through to
                                     * U+005A LATIN CAPITAL LETTER Z Set that
                                     * attribute's name to the lowercase version
                                     * of the current input character (add
                                     * 0x0020 to the character's code point)
                                     */
                                    c += 0x20;
                                }
                                /*
                                 * Set that attribute's name to the current
                                 * input character,
                                 */
                                clearStrBufBeforeUse();
                                appendStrBuf(c);
                                /*
                                 * and its value to the empty string.
                                 */
                                // Will do later.
                                /*
                                 * Switch to the attribute name state.
                                 */
                                state = transition(state, Tokenizer.ATTRIBUTE_NAME, reconsume, pos);
                                continue stateloop;
                        }
                    }
                    // no fallthrough, reordering opportunity
                case MARKUP_DECLARATION_OPEN:
                    markupdeclarationopenloop: for (;;) {
                        if (++pos == endPos) {
                            break stateloop;
                        }
                        c = checkChar(buf, pos);
                        /*
                         * If the next two characters are both U+002D
                         * HYPHEN-MINUS characters (-), consume those two
                         * characters, create a comment token whose data is the
                         * empty string, and switch to the comment start state.
                         *
                         * Otherwise, if the next seven characters are an ASCII
                         * case-insensitive match for the word "DOCTYPE", then
                         * consume those characters and switch to the DOCTYPE
                         * state.
                         *
                         * Otherwise, if the insertion mode is
                         * "in foreign content" and the current node is not an
                         * element in the HTML namespace and the next seven
                         * characters are an case-sensitive match for the string
                         * "[CDATA[" (the five uppercase letters "CDATA" with a
                         * U+005B LEFT SQUARE BRACKET character before and
                         * after), then consume those characters and switch to
                         * the CDATA section state.
                         *
                         * Otherwise, is is a parse error. Switch to the bogus
                         * comment state. The next character that is consumed,
                         * if any, is the first character that will be in the
                         * comment.
                         */
                        switch (c) {
                            case '-':
                                clearStrBufBeforeUse();
                                appendStrBuf(c);
                                state = transition(state, Tokenizer.MARKUP_DECLARATION_HYPHEN, reconsume, pos);
                                // `break` optimizes; `continue stateloop;` would be valid
                                break markupdeclarationopenloop;
                            case 'd':
                            case 'D':
                                clearStrBufBeforeUse();
                                appendStrBuf(c);
                                index = 0;
                                state = transition(state, Tokenizer.MARKUP_DECLARATION_OCTYPE, reconsume, pos);
                                continue stateloop;
                            case '[':
                                if (tokenHandler.cdataSectionAllowed()) {
                                    clearStrBufBeforeUse();
                                    appendStrBuf(c);
                                    index = 0;
                                    state = transition(state, Tokenizer.CDATA_START, reconsume, pos);
                                    continue stateloop;
                                }
                                // CPPONLY: MOZ_FALLTHROUGH;
                            default:
                                errBogusComment();
                                clearStrBufBeforeUse();
                                reconsume = true;
                                state = transition(state, Tokenizer.BOGUS_COMMENT, reconsume, pos);
                                continue stateloop;
                        }
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case MARKUP_DECLARATION_HYPHEN:
                    markupdeclarationhyphenloop: for (;;) {
                        if (++pos == endPos) {
                            break stateloop;
                        }
                        c = checkChar(buf, pos);
                        switch (c) {
                            case '-':
                                clearStrBufAfterOneHyphen();
                                state = transition(state, Tokenizer.COMMENT_START, reconsume, pos);
                                // `break` optimizes; `continue stateloop;` would be valid
                                break markupdeclarationhyphenloop;
                            default:
                                errBogusComment();
                                reconsume = true;
                                state = transition(state, Tokenizer.BOGUS_COMMENT, reconsume, pos);
                                continue stateloop;
                        }
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case COMMENT_START:
                    reportedConsecutiveHyphens = false;
                    commentstartloop: for (;;) {
                        if (++pos == endPos) {
                            break stateloop;
                        }
                        c = checkChar(buf, pos);
                        /*
                         * Comment start state
                         *
                         *
                         * Consume the next input character:
                         */
                        switch (c) {
                            case '-':
                                /*
                                 * U+002D HYPHEN-MINUS (-) Switch to the comment
                                 * start dash state.
                                 */
                                appendStrBuf(c);
                                state = transition(state, Tokenizer.COMMENT_START_DASH, reconsume, pos);
                                continue stateloop;
                            case '>':
                                /*
                                 * U+003E GREATER-THAN SIGN (>) Parse error.
                                 */
                                errPrematureEndOfComment();
                                /* Emit the comment token. */
                                emitComment(0, pos);
                                /*
                                 * Switch to the data state.
                                 */
                                state = transition(state, Tokenizer.DATA, reconsume, pos);
                                if (shouldSuspend) {
                                    break stateloop;
                                }
                                continue stateloop;
                            case '<':
                                appendStrBuf(c);
                                state = transition(state, Tokenizer.COMMENT_LESSTHAN, reconsume, pos);
                                continue stateloop;
                            case '\r':
                                appendStrBufCarriageReturn();
                                state = transition(state, Tokenizer.COMMENT, reconsume, pos);
                                break stateloop;
                            case '\n':
                                appendStrBufLineFeed();
                                state = transition(state, Tokenizer.COMMENT, reconsume, pos);
                                break commentstartloop;
                            case '\u0000':
                                c = '\uFFFD';
                                // CPPONLY: MOZ_FALLTHROUGH;
                            default:
                                /*
                                 * Anything else Append the input character to
                                 * the comment token's data.
                                 */
                                appendStrBuf(c);
                                /*
                                 * Switch to the comment state.
                                 */
                                state = transition(state, Tokenizer.COMMENT, reconsume, pos);
                                // `break` optimizes; `continue stateloop;` would be valid
                                break commentstartloop;
                        }
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case COMMENT:
                    commentloop: for (;;) {
                        ++pos;
                        // Perhaps at some point, it will be appropriate to do SIMD in Java, but not today.
                        // The line below advances pos by some number of code units that this state is indifferent to.
                        // CPPONLY: pos += accelerateAdvancementComment(buf, pos, endPos);
                        if (pos == endPos) {
                            break stateloop;
                        }
                        c = checkChar(buf, pos);
                        /*
                         * Comment state Consume the next input character:
                         */
                        switch (c) {
                            case '-':
                                /*
                                 * U+002D HYPHEN-MINUS (-) Switch to the comment
                                 * end dash state
                                 */
                                appendStrBuf(c);
                                state = transition(state, Tokenizer.COMMENT_END_DASH, reconsume, pos);
                                // `break` optimizes; `continue stateloop;` would be valid
                                break commentloop;
                            case '<':
                                appendStrBuf(c);
                                state = transition(state, Tokenizer.COMMENT_LESSTHAN, reconsume, pos);
                                continue stateloop;
                            case '\r':
                                appendStrBufCarriageReturn();
                                break stateloop;
                            case '\n':
                                appendStrBufLineFeed();
                                continue;
                            case '\u0000':
                                c = '\uFFFD';
                                // CPPONLY: MOZ_FALLTHROUGH;
                            default:
                                /*
                                 * Anything else Append the input character to
                                 * the comment token's data.
                                 */
                                appendStrBuf(c);
                                /*
                                 * Stay in the comment state.
                                 */
                                continue;
                        }
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case COMMENT_END_DASH:
                    commentenddashloop: for (;;) {
                        if (++pos == endPos) {
                            break stateloop;
                        }
                        c = checkChar(buf, pos);
                        /*
                         * Comment end dash state Consume the next input
                         * character:
                         */
                        switch (c) {
                            case '-':
                                /*
                                 * U+002D HYPHEN-MINUS (-) Switch to the comment
                                 * end state
                                 */
                                appendStrBuf(c);
                                state = transition(state, Tokenizer.COMMENT_END, reconsume, pos);
                                // `break` optimizes; `continue stateloop;` would be valid
                                break commentenddashloop;
                            case '<':
                                appendStrBuf(c);
                                state = transition(state, Tokenizer.COMMENT_LESSTHAN, reconsume, pos);
                                continue stateloop;
                            case '\r':
                                appendStrBufCarriageReturn();
                                state = transition(state, Tokenizer.COMMENT, reconsume, pos);
                                break stateloop;
                            case '\n':
                                appendStrBufLineFeed();
                                state = transition(state, Tokenizer.COMMENT, reconsume, pos);
                                continue stateloop;
                            case '\u0000':
                                c = '\uFFFD';
                                // CPPONLY: MOZ_FALLTHROUGH;
                            default:
                                /*
                                 * Anything else Append a U+002D HYPHEN-MINUS
                                 * (-) character and the input character to the
                                 * comment token's data.
                                 */
                                appendStrBuf(c);
                                /*
                                 * Switch to the comment state.
                                 */
                                state = transition(state, Tokenizer.COMMENT, reconsume, pos);
                                continue stateloop;
                        }
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case COMMENT_END:
                    commentendloop: for (;;) {
                        if (++pos == endPos) {
                            break stateloop;
                        }
                        c = checkChar(buf, pos);
                        /*
                         * Comment end dash state Consume the next input
                         * character:
                         */
                        switch (c) {
                            case '>':
                                /*
                                 * U+003E GREATER-THAN SIGN (>) Emit the comment
                                 * token.
                                 */
                                emitComment(2, pos);
                                /*
                                 * Switch to the data state.
                                 */
                                state = transition(state, Tokenizer.DATA, reconsume, pos);
                                if (shouldSuspend) {
                                    break stateloop;
                                }
                                continue stateloop;
                            case '-':
                                /* U+002D HYPHEN-MINUS (-) Parse error. */
                                /*
                                 * Append a U+002D HYPHEN-MINUS (-) character to
                                 * the comment token's data.
                                 */
                                adjustDoubleHyphenAndAppendToStrBufAndErr(c, reportedConsecutiveHyphens);
                                reportedConsecutiveHyphens = true;
                                /*
                                 * Stay in the comment end state.
                                 */
                                continue;
                            case '<':
                                appendStrBuf(c);
                                state = transition(state, Tokenizer.COMMENT_LESSTHAN, reconsume, pos);
                                continue stateloop;
                            case '\r':
                                adjustDoubleHyphenAndAppendToStrBufCarriageReturn();
                                state = transition(state, Tokenizer.COMMENT, reconsume, pos);
                                break stateloop;
                            case '\n':
                                adjustDoubleHyphenAndAppendToStrBufLineFeed();
                                state = transition(state, Tokenizer.COMMENT, reconsume, pos);
                                continue stateloop;
                            case '!':
                                appendStrBuf(c);
                                state = transition(state, Tokenizer.COMMENT_END_BANG, reconsume, pos);
                                // `break` optimizes; `continue stateloop;` would be valid
                                break commentendloop;
                            case '\u0000':
                                c = '\uFFFD';
                                // CPPONLY: MOZ_FALLTHROUGH;
                            default:
                                /*
                                 * Append two U+002D HYPHEN-MINUS (-) characters
                                 * and the input character to the comment
                                 * token's data.
                                 */
                                adjustDoubleHyphenAndAppendToStrBufAndErr(c, reportedConsecutiveHyphens);
                                reportedConsecutiveHyphens = true;
                                /*
                                 * Switch to the comment state.
                                 */
                                state = transition(state, Tokenizer.COMMENT, reconsume, pos);
                                continue stateloop;
                        }
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case COMMENT_END_BANG:
                    for (;;) {
                        if (++pos == endPos) {
                            break stateloop;
                        }
                        c = checkChar(buf, pos);
                        /*
                         * Comment end bang state
                         *
                         * Consume the next input character:
                         */
                        switch (c) {
                            case '>':
                                /*
                                 * U+003E GREATER-THAN SIGN (>) Emit the comment
                                 * token.
                                 */
                                emitComment(3, pos);
                                /*
                                 * Switch to the data state.
                                 */
                                state = transition(state, Tokenizer.DATA, reconsume, pos);
                                if (shouldSuspend) {
                                    break stateloop;
                                }
                                continue stateloop;
                            case '-':
                                /*
                                 * Append two U+002D HYPHEN-MINUS (-) characters
                                 * and a U+0021 EXCLAMATION MARK (!) character
                                 * to the comment token's data.
                                 */
                                appendStrBuf(c);
                                /*
                                 * Switch to the comment end dash state.
                                 */
                                state = transition(state, Tokenizer.COMMENT_END_DASH, reconsume, pos);
                                continue stateloop;
                            case '\r':
                                appendStrBufCarriageReturn();
                                state = transition(state, Tokenizer.COMMENT, reconsume, pos);
                                break stateloop;
                            case '\n':
                                appendStrBufLineFeed();
                                state = transition(state, Tokenizer.COMMENT, reconsume, pos);
                                continue stateloop;
                            case '\u0000':
                                c = '\uFFFD';
                                // CPPONLY: MOZ_FALLTHROUGH;
                            default:
                                /*
                                 * Anything else Append two U+002D HYPHEN-MINUS
                                 * (-) characters, a U+0021 EXCLAMATION MARK (!)
                                 * character, and the input character to the
                                 * comment token's data. Switch to the comment
                                 * state.
                                 */
                                appendStrBuf(c);
                                /*
                                 * Switch to the comment state.
                                 */
                                state = transition(state, Tokenizer.COMMENT, reconsume, pos);
                                continue stateloop;
                        }
                    }
                    // no fallthrough, reordering opportunity
                case COMMENT_LESSTHAN:
                    commentlessthanloop: for (;;) {
                        if (++pos == endPos) {
                            break stateloop;
                        }
                        c = checkChar(buf, pos);
                        switch (c) {
                            case '!':
                                appendStrBuf(c);
                                state = transition(state, Tokenizer.COMMENT_LESSTHAN_BANG, reconsume, pos);
                                // `break` optimizes; `continue stateloop;` would be valid
                                break commentlessthanloop;
                            case '<':
                                appendStrBuf(c);
                                continue;
                            case '-':
                                appendStrBuf(c);
                                state = transition(state, Tokenizer.COMMENT_END_DASH, reconsume, pos);
                                continue stateloop;
                            case '\r':
                                appendStrBufCarriageReturn();
                                state = transition(state, Tokenizer.COMMENT, reconsume, pos);
                                break stateloop;
                            case '\n':
                                appendStrBufLineFeed();
                                state = transition(state, Tokenizer.COMMENT, reconsume, pos);
                                continue stateloop;
                            case '\u0000':
                                c = '\uFFFD';
                                // CPPONLY: MOZ_FALLTHROUGH;
                            default:
                                appendStrBuf(c);
                                state = transition(state, Tokenizer.COMMENT, reconsume, pos);
                                continue stateloop;
                        }
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case COMMENT_LESSTHAN_BANG:
                    commentlessthanbangloop: for (;;) {
                        if (++pos == endPos) {
                            break stateloop;
                        }
                        c = checkChar(buf, pos);
                        switch (c) {
                            case '-':
                                appendStrBuf(c);
                                state = transition(state, Tokenizer.COMMENT_LESSTHAN_BANG_DASH, reconsume, pos);
                                // `break` optimizes; `continue stateloop;` would be valid
                                break commentlessthanbangloop;
                            case '<':
                                appendStrBuf(c);
                                state = transition(state, Tokenizer.COMMENT_LESSTHAN, reconsume, pos);
                                continue stateloop;
                            case '\r':
                                appendStrBufCarriageReturn();
                                state = transition(state, Tokenizer.COMMENT, reconsume, pos);
                                break stateloop;
                            case '\n':
                                appendStrBufLineFeed();
                                state = transition(state, Tokenizer.COMMENT, reconsume, pos);
                                continue stateloop;
                            case '\u0000':
                                c = '\uFFFD';
                                // CPPONLY: MOZ_FALLTHROUGH;
                            default:
                                appendStrBuf(c);
                                state = transition(state, Tokenizer.COMMENT, reconsume, pos);
                                continue stateloop;
                        }
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case COMMENT_LESSTHAN_BANG_DASH:
                    if (++pos == endPos) {
                        break stateloop;
                    }
                    c = checkChar(buf, pos);
                    switch (c) {
                        case '-':
                            appendStrBuf(c);
                            state = transition(state,
                                    Tokenizer.COMMENT_LESSTHAN_BANG_DASH_DASH,
                                    reconsume, pos);
                            // `break` optimizes; `continue stateloop;` would be valid
                            break;
                        case '<':
                            appendStrBuf(c);
                            state = transition(state,
                                    Tokenizer.COMMENT_LESSTHAN, reconsume, pos);
                            continue stateloop;
                        case '\r':
                            appendStrBufCarriageReturn();
                            state = transition(state, Tokenizer.COMMENT,
                                    reconsume, pos);
                            break stateloop;
                        case '\n':
                            appendStrBufLineFeed();
                            state = transition(state, Tokenizer.COMMENT,
                                    reconsume, pos);
                            continue stateloop;
                        case '\u0000':
                            c = '\uFFFD';
                            // CPPONLY: MOZ_FALLTHROUGH;
                        default:
                            appendStrBuf(c);
                            state = transition(state, Tokenizer.COMMENT,
                                    reconsume, pos);
                            continue stateloop;
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case COMMENT_LESSTHAN_BANG_DASH_DASH:
                    if (++pos == endPos) {
                        break stateloop;
                    }
                    c = checkChar(buf, pos);
                    switch (c) {
                        case '>':
                            appendStrBuf(c);
                            emitComment(3, pos);
                            state = transition(state, Tokenizer.DATA, reconsume,
                                    pos);
                            if (shouldSuspend) {
                                break stateloop;
                            }
                            continue stateloop;
                        case '-':
                            errNestedComment();
                            adjustDoubleHyphenAndAppendToStrBufAndErr(c,
                                    reportedConsecutiveHyphens);
                            reportedConsecutiveHyphens = true;
                            state = transition(state, Tokenizer.COMMENT_END,
                                    reconsume, pos);
                            continue stateloop;
                        case '\r':
                            c = '\n';
                            silentCarriageReturn();
                            errNestedComment();
                            adjustDoubleHyphenAndAppendToStrBufAndErr(c,
                                    reportedConsecutiveHyphens);
                            reportedConsecutiveHyphens = true;
                            state = transition(state, Tokenizer.COMMENT,
                                    reconsume, pos);
                            break stateloop;
                        case '\n':
                            silentLineFeed();
                            errNestedComment();
                            adjustDoubleHyphenAndAppendToStrBufAndErr(c,
                                    reportedConsecutiveHyphens);
                            reportedConsecutiveHyphens = true;
                            state = transition(state, Tokenizer.COMMENT,
                                    reconsume, pos);
                            continue stateloop;
                        case '!':
                            errNestedComment();
                            adjustDoubleHyphenAndAppendToStrBufAndErr(c,
                                    reportedConsecutiveHyphens);
                            reportedConsecutiveHyphens = true;
                            state = transition(state,
                                    Tokenizer.COMMENT_END_BANG, reconsume, pos);
                            continue stateloop;
                        case '\u0000':
                            c = '\uFFFD';
                            // CPPONLY: MOZ_FALLTHROUGH;
                        default:
                            errNestedComment();
                            adjustDoubleHyphenAndAppendToStrBufAndErr(c,
                                    reportedConsecutiveHyphens);
                            reportedConsecutiveHyphens = true;
                            state = transition(state, Tokenizer.COMMENT,
                                    reconsume, pos);
                            continue stateloop;
                    }
                    // no fallthrough, reordering opportunity
                case COMMENT_START_DASH:
                    if (++pos == endPos) {
                        break stateloop;
                    }
                    c = checkChar(buf, pos);
                    /*
                     * Comment start dash state
                     *
                     * Consume the next input character:
                     */
                    switch (c) {
                        case '-':
                            /*
                             * U+002D HYPHEN-MINUS (-) Switch to the comment end
                             * state
                             */
                            appendStrBuf(c);
                            state = transition(state, Tokenizer.COMMENT_END, reconsume, pos);
                            continue stateloop;
                        case '>':
                            errPrematureEndOfComment();
                            /* Emit the comment token. */
                            emitComment(1, pos);
                            /*
                             * Switch to the data state.
                             */
                            state = transition(state, Tokenizer.DATA, reconsume, pos);
                            if (shouldSuspend) {
                                break stateloop;
                            }
                            continue stateloop;
                        case '<':
                            appendStrBuf(c);
                            state = transition(state, Tokenizer.COMMENT_LESSTHAN, reconsume, pos);
                            continue stateloop;
                        case '\r':
                            appendStrBufCarriageReturn();
                            state = transition(state, Tokenizer.COMMENT, reconsume, pos);
                            break stateloop;
                        case '\n':
                            appendStrBufLineFeed();
                            state = transition(state, Tokenizer.COMMENT, reconsume, pos);
                            continue stateloop;
                        case '\u0000':
                            c = '\uFFFD';
                            // CPPONLY: MOZ_FALLTHROUGH;
                        default:
                            /*
                             * Append a U+002D HYPHEN-MINUS character (-) and
                             * the current input character to the comment
                             * token's data.
                             */
                            appendStrBuf(c);
                            /*
                             * Switch to the comment state.
                             */
                            state = transition(state, Tokenizer.COMMENT, reconsume, pos);
                            continue stateloop;
                    }
                    // no fallthrough, reordering opportunity
                case CDATA_START:
                    for (;;) {
                        if (++pos == endPos) {
                            break stateloop;
                        }
                        c = checkChar(buf, pos);
                        if (index < 6) { // CDATA_LSQB.length
                            if (c == Tokenizer.CDATA_LSQB[index]) {
                                appendStrBuf(c);
                            } else {
                                errBogusComment();
                                reconsume = true;
                                state = transition(state, Tokenizer.BOGUS_COMMENT, reconsume, pos);
                                continue stateloop;
                            }
                            index++;
                            continue;
                        } else {
                            clearStrBufAfterUse();
                            cstart = pos; // start coalescing
                            reconsume = true;
                            state = transition(state, Tokenizer.CDATA_SECTION, reconsume, pos);
                            // `break` optimizes; `continue stateloop;` would be valid
                            break;
                        }
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case CDATA_SECTION:
                    cdatasectionloop: for (;;) {
                        if (reconsume) {
                            reconsume = false;
                        } else {
                            ++pos;
                            // Perhaps at some point, it will be appropriate to do SIMD in Java, but not today.
                            // The line below advances pos by some number of code units that this state is indifferent to.
                            // CPPONLY: pos += accelerateAdvancementCdataSection(buf, pos, endPos);
                            if (pos == endPos) {
                                break stateloop;
                            }
                            c = checkChar(buf, pos);
                        }
                        switch (c) {
                            case ']':
                                flushChars(buf, pos);
                                state = transition(state, Tokenizer.CDATA_RSQB, reconsume, pos);
                                // `break` optimizes; `continue stateloop;` would be valid
                                break cdatasectionloop;
                            case '\u0000':
                                maybeEmitReplacementCharacter(buf, pos);
                                continue;
                            case '\r':
                                emitCarriageReturn(buf, pos);
                                break stateloop;
                            case '\n':
                                silentLineFeed();
                                // CPPONLY: MOZ_FALLTHROUGH;
                            default:
                                continue;
                        }
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case CDATA_RSQB:
                    if (++pos == endPos) {
                        break stateloop;
                    }
                    c = checkChar(buf, pos);
                    switch (c) {
                        case ']':
                            state = transition(state, Tokenizer.CDATA_RSQB_RSQB,
                                    reconsume, pos);
                            // `break` optimizes; `continue stateloop;` would be valid
                            break;
                        default:
                            tokenHandler.characters(Tokenizer.RSQB_RSQB, 0, 1);
                            cstart = pos;
                            reconsume = true;
                            state = transition(state, Tokenizer.CDATA_SECTION,
                                    reconsume, pos);
                            continue stateloop;
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case CDATA_RSQB_RSQB:
                    cdatarsqbrsqb: for (;;) {
                        if (++pos == endPos) {
                            break stateloop;
                        }
                        c = checkChar(buf, pos);
                        switch (c) {
                            case ']':
                                // Saw a third ]. Emit one ] (logically the
                                // first one) and stay in this state to
                                // remember that the last two characters seen
                                // have been ]].
                                tokenHandler.characters(Tokenizer.RSQB_RSQB, 0, 1);
                                continue;
                            case '>':
                                cstart = pos + 1;
                                state = transition(state, Tokenizer.DATA, reconsume, pos);
                                // Since a CDATA section starts with a less-than sign, it
                                // participates in the suspension-after-current-token
                                // behavior. (The suspension can be requested when the
                                // less-than sign has been seen but we don't yet know the
                                // resulting token type.) Therefore, we need to deal with
                                // a potential request here.
                                suspendIfRequestedAfterCurrentNonTextToken();
                                if (shouldSuspend) {
                                    break stateloop;
                                }
                                continue stateloop;
                            default:
                                tokenHandler.characters(Tokenizer.RSQB_RSQB, 0, 2);
                                cstart = pos;
                                reconsume = true;
                                state = transition(state, Tokenizer.CDATA_SECTION, reconsume, pos);
                                continue stateloop;
                        }
                    }
                    // no fallthrough, reordering opportunity
                case ATTRIBUTE_VALUE_SINGLE_QUOTED:
                    attributevaluesinglequotedloop: for (;;) {
                        if (reconsume) {
                            reconsume = false;
                        } else {
                            ++pos;
                            // Perhaps at some point, it will be appropriate to do SIMD in Java, but not today.
                            // The line below advances pos by some number of code units that this state is indifferent to.
                            // CPPONLY: pos += accelerateAdvancementAttributeValueSingleQuoted(buf, pos, endPos);
                            if (pos == endPos) {
                                break stateloop;
                            }
                            c = checkChar(buf, pos);
                        }
                        /*
                         * Consume the next input character:
                         */
                        switch (c) {
                            case '\'':
                                /*
                                 * U+0027 APOSTROPHE (') Switch to the after
                                 * attribute value (quoted) state.
                                 */
                                addAttributeWithValue();

                                state = transition(state, Tokenizer.AFTER_ATTRIBUTE_VALUE_QUOTED, reconsume, pos);
                                continue stateloop;
                            case '&':
                                /*
                                 * U+0026 AMPERSAND (&) Switch to the character
                                 * reference in attribute value state, with the
                                 * + additional allowed character being U+0027
                                 * APOSTROPHE (').
                                 */
                                assert charRefBufLen == 0: "charRefBufLen not reset after previous use!";
                                appendCharRefBuf(c);
                                setAdditionalAndRememberAmpersandLocation('\'');
                                returnState = state;
                                state = transition(state, Tokenizer.CONSUME_CHARACTER_REFERENCE, reconsume, pos);
                                // `break` optimizes; `continue stateloop;` would be valid
                                break attributevaluesinglequotedloop;
                            case '\r':
                                appendStrBufCarriageReturn();
                                break stateloop;
                            case '\n':
                                appendStrBufLineFeed();
                                continue;
                            case '\u0000':
                                c = '\uFFFD';
                                // CPPONLY: MOZ_FALLTHROUGH;
                            default:
                                /*
                                 * Anything else Append the current input
                                 * character to the current attribute's value.
                                 */
                                appendStrBuf(c);
                                /*
                                 * Stay in the attribute value (double-quoted)
                                 * state.
                                 */
                                continue;
                        }
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case CONSUME_CHARACTER_REFERENCE:
                    if (++pos == endPos) {
                        break stateloop;
                    }
                    c = checkChar(buf, pos);
                    /*
                     * Unlike the definition is the spec, this state does not
                     * return a value and never requires the caller to
                     * backtrack. This state takes care of emitting characters
                     * or appending to the current attribute value. It also
                     * takes care of that in the case when consuming the
                     * character reference fails.
                     */
                    /*
                     * This section defines how to consume a character
                     * reference. This definition is used when parsing character
                     * references in text and in attributes.
                     *
                     * The behavior depends on the identity of the next
                     * character (the one immediately after the U+0026 AMPERSAND
                     * character):
                     */
                    switch (c) {
                        case ' ':
                        case '\t':
                        case '\n':
                        case '\r': // we'll reconsume!
                        case '\u000C':
                        case '<':
                        case '&':
                        case '\u0000':
                        case ';':
                            emitOrAppendCharRefBuf(returnState);
                            if ((returnState & DATA_AND_RCDATA_MASK) == 0) {
                                cstart = pos;
                            }
                            reconsume = true;
                            state = transition(state, returnState, reconsume, pos);
                            continue stateloop;
                        case '#':
                            /*
                             * U+0023 NUMBER SIGN (#) Consume the U+0023 NUMBER
                             * SIGN.
                             */
                            appendCharRefBuf('#');
                            state = transition(state, Tokenizer.CONSUME_NCR, reconsume, pos);
                            continue stateloop;
                        default:
                            if (c == additional) {
                                emitOrAppendCharRefBuf(returnState);
                                reconsume = true;
                                state = transition(state, returnState, reconsume, pos);
                                continue stateloop;
                            }
                            if (c >= 'a' && c <= 'z') {
                                firstCharKey = c - 'a' + 26;
                            } else if (c >= 'A' && c <= 'Z') {
                                firstCharKey = c - 'A';
                            } else {
                                // No match
                                if (c == ';') {
                                    errNoNamedCharacterMatch();
                                }
                                emitOrAppendCharRefBuf(returnState);
                                if ((returnState & DATA_AND_RCDATA_MASK) == 0) {
                                    cstart = pos;
                                }
                                reconsume = true;
                                state = transition(state, returnState, reconsume, pos);
                                continue stateloop;
                            }
                            // Didn't fail yet
                            appendCharRefBuf(c);
                            state = transition(state, Tokenizer.CHARACTER_REFERENCE_HILO_LOOKUP, reconsume, pos);
                            // `break` optimizes; `continue stateloop;` would be valid
                            break;
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case CHARACTER_REFERENCE_HILO_LOOKUP:
                    {
                        if (++pos == endPos) {
                            break stateloop;
                        }
                        c = checkChar(buf, pos);
                        /*
                         * The data structure is as follows:
                         *
                         * HILO_ACCEL is a two-dimensional int array whose major
                         * index corresponds to the second character of the
                         * character reference (code point as index) and the
                         * minor index corresponds to the first character of the
                         * character reference (packed so that A-Z runs from 0
                         * to 25 and a-z runs from 26 to 51). This layout makes
                         * it easier to use the sparseness of the data structure
                         * to omit parts of it: The second dimension of the
                         * table is null when no character reference starts with
                         * the character corresponding to that row.
                         *
                         * The int value HILO_ACCEL (by these indeces) is zero
                         * if there exists no character reference starting with
                         * that two-letter prefix. Otherwise, the value is an
                         * int that packs two shorts so that the higher short is
                         * the index of the highest character reference name
                         * with that prefix in NAMES and the lower short
                         * corresponds to the index of the lowest character
                         * reference name with that prefix. (It happens that the
                         * first two character reference names share their
                         * prefix so the packed int cannot be 0 by packing the
                         * two shorts.)
                         *
                         * NAMES is an array of byte arrays where each byte
                         * array encodes the name of a character references as
                         * ASCII. The names omit the first two letters of the
                         * name. (Since storing the first two letters would be
                         * redundant with the data contained in HILO_ACCEL.) The
                         * entries are lexically sorted.
                         *
                         * For a given index in NAMES, the same index in VALUES
                         * contains the corresponding expansion as an array of
                         * two UTF-16 code units (either the character and
                         * U+0000 or a suggogate pair).
                         */
                        int hilo = 0;
                        if (c <= 'z') {
                            @Const @NoLength int[] row = NamedCharactersAccel.HILO_ACCEL[c];
                            if (row != null) {
                                hilo = row[firstCharKey];
                            }
                        }
                        if (hilo == 0) {
                            if (c == ';') {
                                errNoNamedCharacterMatch();
                            }
                            emitOrAppendCharRefBuf(returnState);
                            if ((returnState & DATA_AND_RCDATA_MASK) == 0) {
                                cstart = pos;
                            }
                            reconsume = true;
                            state = transition(state, returnState, reconsume, pos);
                            continue stateloop;
                        }
                        // Didn't fail yet
                        appendCharRefBuf(c);
                        lo = hilo & 0xFFFF;
                        hi = hilo >> 16;
                        entCol = -1;
                        candidate = -1;
                        charRefBufMark = 0;
                        state = transition(state, Tokenizer.CHARACTER_REFERENCE_TAIL, reconsume, pos);
                        // fallthrough optimizes; `continue stateloop;` would also be valid
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case CHARACTER_REFERENCE_TAIL:
                    outer: for (;;) {
                        if (++pos == endPos) {
                            break stateloop;
                        }
                        c = checkChar(buf, pos);
                        entCol++;
                        /*
                         * Consume the maximum number of characters possible,
                         * with the consumed characters matching one of the
                         * identifiers in the first column of the named
                         * character references table (in a case-sensitive
                         * manner).
                         */
                        loloop: for (;;) {
                            if (hi < lo) {
                                break outer;
                            }
                            if (entCol == NamedCharacters.NAMES[lo].length()) {
                                candidate = lo;
                                charRefBufMark = charRefBufLen;
                                lo++;
                            } else if (entCol > NamedCharacters.NAMES[lo].length()) {
                                break outer;
                            } else if (c > NamedCharacters.NAMES[lo].charAt(entCol)) {
                                lo++;
                            } else {
                                break loloop;
                            }
                        }

                        hiloop: for (;;) {
                            if (hi < lo) {
                                break outer;
                            }
                            if (entCol == NamedCharacters.NAMES[hi].length()) {
                                break hiloop;
                            }
                            if (entCol > NamedCharacters.NAMES[hi].length()) {
                                break outer;
                            } else if (c < NamedCharacters.NAMES[hi].charAt(entCol)) {
                                hi--;
                            } else {
                                break hiloop;
                            }
                        }

                        if (c == ';') {
                            // If we see a semicolon, there cannot be a
                            // longer match. Break the loop. However, before
                            // breaking, take the longest match so far as the
                            // candidate, if we are just about to complete a
                            // match.
                            if (entCol + 1 == NamedCharacters.NAMES[lo].length()) {
                                candidate = lo;
                                charRefBufMark = charRefBufLen;
                            }
                            break outer;
                        }

                        if (hi < lo) {
                            break outer;
                        }
                        appendCharRefBuf(c);
                        continue;
                    }

                    if (candidate == -1) {
                        // reconsume deals with CR, LF or nul
                        if (c == ';') {
                            errNoNamedCharacterMatch();
                        }
                        emitOrAppendCharRefBuf(returnState);
                        if ((returnState & DATA_AND_RCDATA_MASK) == 0) {
                            cstart = pos;
                        }
                        reconsume = true;
                        state = transition(state, returnState, reconsume, pos);
                        continue stateloop;
                    } else {
                        // c can't be CR, LF or nul if we got here
                        @Const @CharacterName String candidateName = NamedCharacters.NAMES[candidate];
                        if (candidateName.length() == 0
                                || candidateName.charAt(candidateName.length() - 1) != ';') {
                            /*
                             * If the last character matched is not a U+003B
                             * SEMICOLON (;), there is a parse error.
                             */
                            if ((returnState & DATA_AND_RCDATA_MASK) != 0) {
                                /*
                                 * If the entity is being consumed as part of an
                                 * attribute, and the last character matched is
                                 * not a U+003B SEMICOLON (;),
                                 */
                                char ch;
                                if (charRefBufMark == charRefBufLen) {
                                    ch = c;
                                } else {
                                    ch = charRefBuf[charRefBufMark];
                                }
                                if (ch == '=' || (ch >= '0' && ch <= '9')
                                        || (ch >= 'A' && ch <= 'Z')
                                        || (ch >= 'a' && ch <= 'z')) {
                                    /*
                                     * and the next character is either a U+003D
                                     * EQUALS SIGN character (=) or in the range
                                     * U+0030 DIGIT ZERO to U+0039 DIGIT NINE,
                                     * U+0041 LATIN CAPITAL LETTER A to U+005A
                                     * LATIN CAPITAL LETTER Z, or U+0061 LATIN
                                     * SMALL LETTER A to U+007A LATIN SMALL
                                     * LETTER Z, then, for historical reasons,
                                     * all the characters that were matched
                                     * after the U+0026 AMPERSAND (&) must be
                                     * unconsumed, and nothing is returned.
                                     */
                                    if (c == ';') {
                                        errNoNamedCharacterMatch();
                                    }
                                    appendCharRefBufToStrBuf();
                                    reconsume = true;
                                    state = transition(state, returnState, reconsume, pos);
                                    continue stateloop;
                                }
                            }
                            if ((returnState & DATA_AND_RCDATA_MASK) != 0) {
                                errUnescapedAmpersandInterpretedAsCharacterReference();
                            } else {
                                errNotSemicolonTerminated();
                            }
                        }

                        /*
                         * Otherwise, return a character token for the character
                         * corresponding to the entity name (as given by the
                         * second column of the named character references
                         * table).
                         */
                        // CPPONLY: completedNamedCharacterReference();
                        @Const @NoLength char[] val = NamedCharacters.VALUES[candidate];
                        if (
                        // [NOCPP[
                        val.length == 1
                        // ]NOCPP]
                        // CPPONLY: val[1] == 0
                        ) {
                            emitOrAppendOne(val, returnState);
                        } else {
                            emitOrAppendTwo(val, returnState);
                        }
                        // this is so complicated!
                        if (charRefBufMark < charRefBufLen) {
                            if ((returnState & DATA_AND_RCDATA_MASK) != 0) {
                                appendStrBuf(charRefBuf, charRefBufMark,
                                        charRefBufLen - charRefBufMark);
                            } else {
                                tokenHandler.characters(charRefBuf, charRefBufMark,
                                        charRefBufLen - charRefBufMark);
                            }
                        }
                        // charRefBufLen will be zeroed below!

                        // Check if we broke out early with c being the last
                        // character that matched as opposed to being the
                        // first one that didn't match. In the case of an
                        // early break, the next run on text should start
                        // *after* the current character and the current
                        // character shouldn't be reconsumed.
                        boolean earlyBreak = (c == ';' && charRefBufMark == charRefBufLen);
                        charRefBufLen = 0;
                        if ((returnState & DATA_AND_RCDATA_MASK) == 0) {
                            cstart = earlyBreak ? pos + 1 : pos;
                        }
                        reconsume = !earlyBreak;
                        state = transition(state, returnState, reconsume, pos);
                        continue stateloop;
                        /*
                         * If the markup contains I'm &notit; I tell you, the
                         * entity is parsed as "not", as in, I'm ¬it; I tell
                         * you. But if the markup was I'm &notin; I tell you,
                         * the entity would be parsed as "notin;", resulting in
                         * I'm ∉ I tell you.
                         */
                    }
                    // no fallthrough, reordering opportunity
                case CONSUME_NCR:
                    if (++pos == endPos) {
                        break stateloop;
                    }
                    c = checkChar(buf, pos);
                    value = 0;
                    seenDigits = false;
                    /*
                     * The behavior further depends on the character after the
                     * U+0023 NUMBER SIGN:
                     */
                    switch (c) {
                        case 'x':
                        case 'X':

                            /*
                             * U+0078 LATIN SMALL LETTER X U+0058 LATIN CAPITAL
                             * LETTER X Consume the X.
                             *
                             * Follow the steps below, but using the range of
                             * characters U+0030 DIGIT ZERO through to U+0039
                             * DIGIT NINE, U+0061 LATIN SMALL LETTER A through
                             * to U+0066 LATIN SMALL LETTER F, and U+0041 LATIN
                             * CAPITAL LETTER A, through to U+0046 LATIN CAPITAL
                             * LETTER F (in other words, 0-9, A-F, a-f).
                             *
                             * When it comes to interpreting the number,
                             * interpret it as a hexadecimal number.
                             */
                            appendCharRefBuf(c);
                            state = transition(state, Tokenizer.HEX_NCR_LOOP, reconsume, pos);
                            continue stateloop;
                        default:
                            /*
                             * Anything else Follow the steps below, but using
                             * the range of characters U+0030 DIGIT ZERO through
                             * to U+0039 DIGIT NINE (i.e. just 0-9).
                             *
                             * When it comes to interpreting the number,
                             * interpret it as a decimal number.
                             */
                            reconsume = true;
                            state = transition(state, Tokenizer.DECIMAL_NRC_LOOP, reconsume, pos);
                            // `break` optimizes; `continue stateloop;` would be valid
                            break;
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case DECIMAL_NRC_LOOP:
                    decimalloop: for (;;) {
                        if (reconsume) {
                            reconsume = false;
                        } else {
                            if (++pos == endPos) {
                                break stateloop;
                            }
                            c = checkChar(buf, pos);
                        }
                        /*
                         * Consume as many characters as match the range of
                         * characters given above.
                         */
                        assert value >= 0: "value must not become negative.";
                        if (c >= '0' && c <= '9') {
                            seenDigits = true;
                            // Avoid overflow
                            if (value <= 0x10FFFF) {
                                value *= 10;
                                value += c - '0';
                            }
                            continue;
                        } else if (c == ';') {
                            if (seenDigits) {
                                if ((returnState & DATA_AND_RCDATA_MASK) == 0) {
                                    cstart = pos + 1;
                                }
                                state = transition(state, Tokenizer.HANDLE_NCR_VALUE, reconsume, pos);
                                // `break` optimizes; `continue stateloop;` would be valid
                                break decimalloop;
                            } else {
                                errNoDigitsInNCR();
                                appendCharRefBuf(';');
                                emitOrAppendCharRefBuf(returnState);
                                if ((returnState & DATA_AND_RCDATA_MASK) == 0) {
                                    cstart = pos + 1;
                                }
                                state = transition(state, returnState, reconsume, pos);
                                continue stateloop;
                            }
                        } else {
                            /*
                             * If no characters match the range, then don't
                             * consume any characters (and unconsume the U+0023
                             * NUMBER SIGN character and, if appropriate, the X
                             * character). This is a parse error; nothing is
                             * returned.
                             *
                             * Otherwise, if the next character is a U+003B
                             * SEMICOLON, consume that too. If it isn't, there
                             * is a parse error.
                             */
                            if (!seenDigits) {
                                errNoDigitsInNCR();
                                emitOrAppendCharRefBuf(returnState);
                                if ((returnState & DATA_AND_RCDATA_MASK) == 0) {
                                    cstart = pos;
                                }
                                reconsume = true;
                                state = transition(state, returnState, reconsume, pos);
                                continue stateloop;
                            } else {
                                errCharRefLacksSemicolon();
                                if ((returnState & DATA_AND_RCDATA_MASK) == 0) {
                                    cstart = pos;
                                }
                                reconsume = true;
                                state = transition(state, Tokenizer.HANDLE_NCR_VALUE, reconsume, pos);
                                // `break` optimizes; `continue stateloop;` would be valid
                                break decimalloop;
                            }
                        }
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case HANDLE_NCR_VALUE:
                    // WARNING previous state sets reconsume
                    // We are not going to emit the contents of charRefBuf.
                    charRefBufLen = 0;
                    // XXX inline this case if the method size can take it
                    handleNcrValue(returnState);
                    state = transition(state, returnState, reconsume, pos);
                    continue stateloop;
                    // no fallthrough, reordering opportunity
                case HEX_NCR_LOOP:
                    for (;;) {
                        if (++pos == endPos) {
                            break stateloop;
                        }
                        c = checkChar(buf, pos);
                        /*
                         * Consume as many characters as match the range of
                         * characters given above.
                         */
                        assert value >= 0: "value must not become negative.";
                        if (c >= '0' && c <= '9') {
                            seenDigits = true;
                            // Avoid overflow
                            if (value <= 0x10FFFF) {
                                value *= 16;
                                value += c - '0';
                            }
                            continue;
                        } else if (c >= 'A' && c <= 'F') {
                            seenDigits = true;
                            // Avoid overflow
                            if (value <= 0x10FFFF) {
                                value *= 16;
                                value += c - 'A' + 10;
                            }
                            continue;
                        } else if (c >= 'a' && c <= 'f') {
                            seenDigits = true;
                            // Avoid overflow
                            if (value <= 0x10FFFF) {
                                value *= 16;
                                value += c - 'a' + 10;
                            }
                            continue;
                        } else if (c == ';') {
                            if (seenDigits) {
                                if ((returnState & DATA_AND_RCDATA_MASK) == 0) {
                                    cstart = pos + 1;
                                }
                                state = transition(state, Tokenizer.HANDLE_NCR_VALUE, reconsume, pos);
                                continue stateloop;
                            } else {
                                errNoDigitsInNCR();
                                appendCharRefBuf(';');
                                emitOrAppendCharRefBuf(returnState);
                                if ((returnState & DATA_AND_RCDATA_MASK) == 0) {
                                    cstart = pos + 1;
                                }
                                state = transition(state, returnState, reconsume, pos);
                                continue stateloop;
                            }
                        } else {
                            /*
                             * If no characters match the range, then don't
                             * consume any characters (and unconsume the U+0023
                             * NUMBER SIGN character and, if appropriate, the X
                             * character). This is a parse error; nothing is
                             * returned.
                             *
                             * Otherwise, if the next character is a U+003B
                             * SEMICOLON, consume that too. If it isn't, there
                             * is a parse error.
                             */
                            if (!seenDigits) {
                                errNoDigitsInNCR();
                                emitOrAppendCharRefBuf(returnState);
                                if ((returnState & DATA_AND_RCDATA_MASK) == 0) {
                                    cstart = pos;
                                }
                                reconsume = true;
                                state = transition(state, returnState, reconsume, pos);
                                continue stateloop;
                            } else {
                                errCharRefLacksSemicolon();
                                if ((returnState & DATA_AND_RCDATA_MASK) == 0) {
                                    cstart = pos;
                                }
                                reconsume = true;
                                state = transition(state, Tokenizer.HANDLE_NCR_VALUE, reconsume, pos);
                                continue stateloop;
                            }
                        }
                    }
                    // no fallthrough, reordering opportunity
                case PLAINTEXT:
                    plaintextloop: for (;;) {
                        if (reconsume) {
                            reconsume = false;
                        } else {
                            ++pos;
                            // Perhaps at some point, it will be appropriate to do SIMD in Java, but not today.
                            // The line below advances pos by some number of code units that this state is indifferent to.
                            // CPPONLY: pos += accelerateAdvancementPlaintext(buf, pos, endPos);
                            if (pos == endPos) {
                                break stateloop;
                            }
                            c = checkChar(buf, pos);
                        }
                        switch (c) {
                            case '\u0000':
                                emitPlaintextReplacementCharacter(buf, pos);
                                continue;
                            case '\r':
                                emitCarriageReturn(buf, pos);
                                break stateloop;
                            case '\n':
                                silentLineFeed();
                                // CPPONLY: MOZ_FALLTHROUGH;
                            default:
                                /*
                                 * Anything else Emit the current input
                                 * character as a character token. Stay in the
                                 * RAWTEXT state.
                                 */
                                continue;
                        }
                    }
                    // no fallthrough, reordering opportunity
                case CLOSE_TAG_OPEN:
                    if (++pos == endPos) {
                        break stateloop;
                    }
                    c = checkChar(buf, pos);
                    /*
                     * Otherwise, if the content model flag is set to the PCDATA
                     * state, or if the next few characters do match that tag
                     * name, consume the next input character:
                     */
                    switch (c) {
                        case '>':
                            /* U+003E GREATER-THAN SIGN (>) Parse error. */
                            errLtSlashGt();
                            /*
                             * Switch to the data state.
                             */
                            cstart = pos + 1;
                            state = transition(state, Tokenizer.DATA, reconsume, pos);
                            continue stateloop;
                        case '\r':
                            silentCarriageReturn();
                            /* Anything else Parse error. */
                            errGarbageAfterLtSlash();
                            /*
                             * Switch to the bogus comment state.
                             */
                            clearStrBufBeforeUse();
                            appendStrBuf('\n');
                            state = transition(state, Tokenizer.BOGUS_COMMENT, reconsume, pos);
                            break stateloop;
                        case '\n':
                            silentLineFeed();
                            /* Anything else Parse error. */
                            errGarbageAfterLtSlash();
                            /*
                             * Switch to the bogus comment state.
                             */
                            clearStrBufBeforeUse();
                            appendStrBuf(c);
                            state = transition(state, Tokenizer.BOGUS_COMMENT, reconsume, pos);
                            continue stateloop;
                        case '\u0000':
                            c = '\uFFFD';
                            // CPPONLY: MOZ_FALLTHROUGH;
                        default:
                            if (c >= 'A' && c <= 'Z') {
                                c += 0x20;
                            }
                            if (c >= 'a' && c <= 'z') {
                                /*
                                 * U+0061 LATIN SMALL LETTER A through to U+007A
                                 * LATIN SMALL LETTER Z Create a new end tag
                                 * token,
                                 */
                                endTag = true;
                                /*
                                 * set its tag name to the input character,
                                 */
                                clearStrBufBeforeUse();
                                appendStrBuf(c);
                                containsHyphen = false;
                                /*
                                 * then switch to the tag name state. (Don't
                                 * emit the token yet; further details will be
                                 * filled in before it is emitted.)
                                 */
                                state = transition(state, Tokenizer.TAG_NAME, reconsume, pos);
                                continue stateloop;
                            } else {
                                /* Anything else Parse error. */
                                errGarbageAfterLtSlash();
                                /*
                                 * Switch to the bogus comment state.
                                 */
                                clearStrBufBeforeUse();
                                appendStrBuf(c);
                                state = transition(state, Tokenizer.BOGUS_COMMENT, reconsume, pos);
                                continue stateloop;
                            }
                    }
                    // no fallthrough, reordering opportunity
                case RCDATA:
                    rcdataloop: for (;;) {
                        if (reconsume) {
                            reconsume = false;
                        } else {
                            ++pos;
                            // Perhaps at some point, it will be appropriate to do SIMD in Java, but not today.
                            // The line below advances pos by some number of code units that this state is indifferent to.
                            // RCDATA and DATA have the same set of characters that they are indifferent to, hence accelerateData.
                            // CPPONLY: pos += accelerateAdvancementData(buf, pos, endPos);
                            if (pos == endPos) {
                                break stateloop;
                            }
                            c = checkChar(buf, pos);
                        }
                        switch (c) {
                            case '&':
                                /*
                                 * U+0026 AMPERSAND (&) Switch to the character
                                 * reference in RCDATA state.
                                 */
                                flushChars(buf, pos);
                                assert charRefBufLen == 0: "charRefBufLen not reset after previous use!";
                                appendCharRefBuf(c);
                                setAdditionalAndRememberAmpersandLocation('\u0000');
                                returnState = state;
                                state = transition(state, Tokenizer.CONSUME_CHARACTER_REFERENCE, reconsume, pos);
                                continue stateloop;
                            case '<':
                                /*
                                 * U+003C LESS-THAN SIGN (<) Switch to the
                                 * RCDATA less-than sign state.
                                 */
                                flushChars(buf, pos);

                                returnState = state;
                                state = transition(state, Tokenizer.RAWTEXT_RCDATA_LESS_THAN_SIGN, reconsume, pos);
                                continue stateloop;
                            case '\u0000':
                                emitReplacementCharacter(buf, pos);
                                continue;
                            case '\r':
                                emitCarriageReturn(buf, pos);
                                break stateloop;
                            case '\n':
                                silentLineFeed();
                                // CPPONLY: MOZ_FALLTHROUGH;
                            default:
                                /*
                                 * Emit the current input character as a
                                 * character token. Stay in the RCDATA state.
                                 */
                                continue;
                        }
                    }
                    // no fallthrough, reordering opportunity
                case RAWTEXT:
                    rawtextloop: for (;;) {
                        if (reconsume) {
                            reconsume = false;
                        } else {
                            ++pos;
                            // Perhaps at some point, it will be appropriate to do SIMD in Java, but not today.
                            // The line below advances pos by some number of code units that this state is indifferent to.
                            // CPPONLY: pos += accelerateAdvancementRawtext(buf, pos, endPos);
                            if (pos == endPos) {
                                break stateloop;
                            }
                            c = checkChar(buf, pos);
                        }
                        switch (c) {
                            case '<':
                                /*
                                 * U+003C LESS-THAN SIGN (<) Switch to the
                                 * RAWTEXT less-than sign state.
                                 */
                                flushChars(buf, pos);

                                returnState = state;
                                state = transition(state, Tokenizer.RAWTEXT_RCDATA_LESS_THAN_SIGN, reconsume, pos);
                                // `break` optimizes; `continue stateloop;` would be valid
                                break rawtextloop;
                            case '\u0000':
                                emitReplacementCharacter(buf, pos);
                                continue;
                            case '\r':
                                emitCarriageReturn(buf, pos);
                                break stateloop;
                            case '\n':
                                silentLineFeed();
                                // CPPONLY: MOZ_FALLTHROUGH;
                            default:
                                /*
                                 * Emit the current input character as a
                                 * character token. Stay in the RAWTEXT state.
                                 */
                                continue;
                        }
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case RAWTEXT_RCDATA_LESS_THAN_SIGN:
                    rawtextrcdatalessthansignloop: for (;;) {
                        if (++pos == endPos) {
                            break stateloop;
                        }
                        c = checkChar(buf, pos);
                        switch (c) {
                            case '/':
                                /*
                                 * U+002F SOLIDUS (/) Set the temporary buffer
                                 * to the empty string. Switch to the script
                                 * data end tag open state.
                                 */
                                index = 0;
                                clearStrBufBeforeUse();
                                state = transition(state, Tokenizer.NON_DATA_END_TAG_NAME, reconsume, pos);
                                // `break` optimizes; `continue stateloop;` would be valid
                                break rawtextrcdatalessthansignloop;
                            default:
                                /*
                                 * Otherwise, emit a U+003C LESS-THAN SIGN
                                 * character token
                                 */
                                tokenHandler.characters(Tokenizer.LT_GT, 0, 1);
                                /*
                                 * and reconsume the current input character in
                                 * the data state.
                                 */
                                cstart = pos;
                                reconsume = true;
                                state = transition(state, returnState, reconsume, pos);
                                continue stateloop;
                        }
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case NON_DATA_END_TAG_NAME:
                    for (;;) {
                        if (++pos == endPos) {
                            break stateloop;
                        }
                        c = checkChar(buf, pos);
                        /*
                         * ASSERT! when entering this state, set index to 0 and
                         * call clearStrBufBeforeUse(); Let's implement the above
                         * without lookahead. strBuf is the 'temporary buffer'.
                         */
                        if (endTagExpectationAsArray == null) {
                            tokenHandler.characters(Tokenizer.LT_SOLIDUS,
                                    0, 2);
                            cstart = pos;
                            reconsume = true;
                            state = transition(state, returnState, reconsume, pos);
                            continue stateloop;
                        } else if (index < endTagExpectationAsArray.length) {
                            char e = endTagExpectationAsArray[index];
                            char folded = c;
                            if (c >= 'A' && c <= 'Z') {
                                folded += 0x20;
                            }
                            if (folded != e) {
                                // [NOCPP[
                                errHtml4LtSlashInRcdata(folded);
                                // ]NOCPP]
                                tokenHandler.characters(Tokenizer.LT_SOLIDUS,
                                        0, 2);
                                emitStrBuf();
                                cstart = pos;
                                reconsume = true;
                                state = transition(state, returnState, reconsume, pos);
                                continue stateloop;
                            }
                            appendStrBuf(c);
                            index++;
                            continue;
                        } else {
                            endTag = true;
                            // XXX replace contentModelElement with different
                            // type
                            tagName = endTagExpectation;
                            switch (c) {
                                case '\r':
                                    silentCarriageReturn();
                                    clearStrBufAfterUse(); // strBuf not used
                                    state = transition(state, Tokenizer.BEFORE_ATTRIBUTE_NAME, reconsume, pos);
                                    break stateloop;
                                case '\n':
                                    silentLineFeed();
                                    // CPPONLY: MOZ_FALLTHROUGH;
                                case ' ':
                                case '\t':
                                case '\u000C':
                                    /*
                                     * U+0009 CHARACTER TABULATION U+000A LINE
                                     * FEED (LF) U+000C FORM FEED (FF) U+0020
                                     * SPACE If the current end tag token is an
                                     * appropriate end tag token, then switch to
                                     * the before attribute name state.
                                     */
                                    clearStrBufAfterUse(); // strBuf not used
                                    state = transition(state, Tokenizer.BEFORE_ATTRIBUTE_NAME, reconsume, pos);
                                    continue stateloop;
                                case '/':
                                    /*
                                     * U+002F SOLIDUS (/) If the current end tag
                                     * token is an appropriate end tag token,
                                     * then switch to the self-closing start tag
                                     * state.
                                     */
                                    clearStrBufAfterUse(); // strBuf not used
                                    state = transition(state, Tokenizer.SELF_CLOSING_START_TAG, reconsume, pos);
                                    continue stateloop;
                                case '>':
                                    /*
                                     * U+003E GREATER-THAN SIGN (>) If the
                                     * current end tag token is an appropriate
                                     * end tag token, then emit the current tag
                                     * token and switch to the data state.
                                     */
                                    clearStrBufAfterUse(); // strBuf not used
                                    state = transition(state, emitCurrentTagToken(false, pos), reconsume, pos);
                                    if (shouldSuspend) {
                                        break stateloop;
                                    }
                                    continue stateloop;
                                default:
                                    /*
                                     * Emit a U+003C LESS-THAN SIGN character
                                     * token, a U+002F SOLIDUS character token,
                                     * a character token for each of the
                                     * characters in the temporary buffer (in
                                     * the order they were added to the buffer),
                                     * and reconsume the current input character
                                     * in the RAWTEXT state.
                                     */
                                    // [NOCPP[
                                    errWarnLtSlashInRcdata();
                                    // ]NOCPP]
                                    tokenHandler.characters(
                                            Tokenizer.LT_SOLIDUS, 0, 2);
                                    emitStrBuf();
                                    cstart = pos; // don't drop the
                                                  // character
                                    reconsume = true;
                                    state = transition(state, returnState, reconsume, pos);
                                    continue stateloop;
                            }
                        }
                    }
                    // no fallthrough, reordering opportunity
                    // BEGIN HOTSPOT WORKAROUND
                case BOGUS_COMMENT:
                    boguscommentloop: for (;;) {
                        if (reconsume) {
                            reconsume = false;
                        } else {
                            if (++pos == endPos) {
                                break stateloop;
                            }
                            c = checkChar(buf, pos);
                        }
                        /*
                         * Consume every character up to and including the first
                         * U+003E GREATER-THAN SIGN character (>) or the end of
                         * the file (EOF), whichever comes first. Emit a comment
                         * token whose data is the concatenation of all the
                         * characters starting from and including the character
                         * that caused the state machine to switch into the
                         * bogus comment state, up to and including the
                         * character immediately before the last consumed
                         * character (i.e. up to the character just before the
                         * U+003E or EOF character). (If the comment was started
                         * by the end of the file (EOF), the token is empty.)
                         *
                         * Switch to the data state.
                         *
                         * If the end of the file was reached, reconsume the EOF
                         * character.
                         */
                        switch (c) {
                            case '>':
                                emitComment(0, pos);
                                state = transition(state, Tokenizer.DATA, reconsume, pos);
                                if (shouldSuspend) {
                                    break stateloop;
                                }
                                continue stateloop;
                            case '-':
                                appendStrBuf(c);
                                state = transition(state, Tokenizer.BOGUS_COMMENT_HYPHEN, reconsume, pos);
                                // `break` optimizes; `continue stateloop;` would be valid
                                break boguscommentloop;
                            case '\r':
                                appendStrBufCarriageReturn();
                                break stateloop;
                            case '\n':
                                appendStrBufLineFeed();
                                continue;
                            case '\u0000':
                                c = '\uFFFD';
                                // CPPONLY: MOZ_FALLTHROUGH;
                            default:
                                appendStrBuf(c);
                                continue;
                        }
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case BOGUS_COMMENT_HYPHEN:
                    boguscommenthyphenloop: for (;;) {
                        if (++pos == endPos) {
                            break stateloop;
                        }
                        c = checkChar(buf, pos);
                        switch (c) {
                            case '>':
                                // [NOCPP[
                                maybeAppendSpaceToBogusComment();
                                // ]NOCPP]
                                emitComment(0, pos);
                                state = transition(state, Tokenizer.DATA, reconsume, pos);
                                if (shouldSuspend) {
                                    break stateloop;
                                }
                                continue stateloop;
                            case '-':
                                appendSecondHyphenToBogusComment();
                                continue boguscommenthyphenloop;
                            case '\r':
                                appendStrBufCarriageReturn();
                                state = transition(state, Tokenizer.BOGUS_COMMENT, reconsume, pos);
                                break stateloop;
                            case '\n':
                                appendStrBufLineFeed();
                                state = transition(state, Tokenizer.BOGUS_COMMENT, reconsume, pos);
                                continue stateloop;
                            case '\u0000':
                                c = '\uFFFD';
                                // CPPONLY: MOZ_FALLTHROUGH;
                            default:
                                appendStrBuf(c);
                                state = transition(state, Tokenizer.BOGUS_COMMENT, reconsume, pos);
                                continue stateloop;
                        }
                    }
                    // no fallthrough, reordering opportunity
                case SCRIPT_DATA:
                    scriptdataloop: for (;;) {
                        if (reconsume) {
                            reconsume = false;
                        } else {
                            ++pos;
                            // Perhaps at some point, it will be appropriate to do SIMD in Java, but not today.
                            // The line below advances pos by some number of code units that this state is indifferent to.
                            // Using `accelerateAdvancementRawtext`, because this states has the same characters of interest as RAWTEXT.
                            // CPPONLY: pos += accelerateAdvancementRawtext(buf, pos, endPos);
                            if (pos == endPos) {
                                break stateloop;
                            }
                            c = checkChar(buf, pos);
                        }
                        switch (c) {
                            case '<':
                                /*
                                 * U+003C LESS-THAN SIGN (<) Switch to the
                                 * script data less-than sign state.
                                 */
                                flushChars(buf, pos);
                                returnState = state;
                                state = transition(state, Tokenizer.SCRIPT_DATA_LESS_THAN_SIGN, reconsume, pos);
                                // `break` optimizes; `continue stateloop;` would be valid
                                break scriptdataloop;
                            case '\u0000':
                                emitReplacementCharacter(buf, pos);
                                continue;
                            case '\r':
                                emitCarriageReturn(buf, pos);
                                break stateloop;
                            case '\n':
                                silentLineFeed();
                                // CPPONLY: MOZ_FALLTHROUGH;
                            default:
                                /*
                                 * Anything else Emit the current input
                                 * character as a character token. Stay in the
                                 * script data state.
                                 */
                                continue;
                        }
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case SCRIPT_DATA_LESS_THAN_SIGN:
                    scriptdatalessthansignloop: for (;;) {
                        if (++pos == endPos) {
                            break stateloop;
                        }
                        c = checkChar(buf, pos);
                        switch (c) {
                            case '/':
                                /*
                                 * U+002F SOLIDUS (/) Set the temporary buffer
                                 * to the empty string. Switch to the script
                                 * data end tag open state.
                                 */
                                index = 0;
                                clearStrBufBeforeUse();
                                state = transition(state, Tokenizer.NON_DATA_END_TAG_NAME, reconsume, pos);
                                continue stateloop;
                            case '!':
                                tokenHandler.characters(Tokenizer.LT_GT, 0, 1);
                                cstart = pos;
                                state = transition(state, Tokenizer.SCRIPT_DATA_ESCAPE_START, reconsume, pos);
                                // `break` optimizes; `continue stateloop;` would be valid
                                break scriptdatalessthansignloop;
                            default:
                                /*
                                 * Otherwise, emit a U+003C LESS-THAN SIGN
                                 * character token
                                 */
                                tokenHandler.characters(Tokenizer.LT_GT, 0, 1);
                                /*
                                 * and reconsume the current input character in
                                 * the data state.
                                 */
                                cstart = pos;
                                reconsume = true;
                                state = transition(state, Tokenizer.SCRIPT_DATA, reconsume, pos);
                                continue stateloop;
                        }
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case SCRIPT_DATA_ESCAPE_START:
                    scriptdataescapestartloop: for (;;) {
                        if (++pos == endPos) {
                            break stateloop;
                        }
                        c = checkChar(buf, pos);
                        /*
                         * Consume the next input character:
                         */
                        switch (c) {
                            case '-':
                                /*
                                 * U+002D HYPHEN-MINUS (-) Emit a U+002D
                                 * HYPHEN-MINUS character token. Switch to the
                                 * script data escape start dash state.
                                 */
                                state = transition(state, Tokenizer.SCRIPT_DATA_ESCAPE_START_DASH, reconsume, pos);
                                // `break` optimizes; `continue stateloop;` would be valid
                                break scriptdataescapestartloop;
                            default:
                                /*
                                 * Anything else Reconsume the current input
                                 * character in the script data state.
                                 */
                                reconsume = true;
                                state = transition(state, Tokenizer.SCRIPT_DATA, reconsume, pos);
                                continue stateloop;
                        }
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case SCRIPT_DATA_ESCAPE_START_DASH:
                    scriptdataescapestartdashloop: for (;;) {
                        if (++pos == endPos) {
                            break stateloop;
                        }
                        c = checkChar(buf, pos);
                        /*
                         * Consume the next input character:
                         */
                        switch (c) {
                            case '-':
                                /*
                                 * U+002D HYPHEN-MINUS (-) Emit a U+002D
                                 * HYPHEN-MINUS character token. Switch to the
                                 * script data escaped dash dash state.
                                 */
                                state = transition(state, Tokenizer.SCRIPT_DATA_ESCAPED_DASH_DASH, reconsume, pos);
                                // `break` optimizes; `continue stateloop;` would be valid
                                break scriptdataescapestartdashloop;
                            default:
                                /*
                                 * Anything else Reconsume the current input
                                 * character in the script data state.
                                 */
                                reconsume = true;
                                state = transition(state, Tokenizer.SCRIPT_DATA, reconsume, pos);
                                continue stateloop;
                        }
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case SCRIPT_DATA_ESCAPED_DASH_DASH:
                    scriptdataescapeddashdashloop: for (;;) {
                        if (++pos == endPos) {
                            break stateloop;
                        }
                        c = checkChar(buf, pos);
                        /*
                         * Consume the next input character:
                         */
                        switch (c) {
                            case '-':
                                /*
                                 * U+002D HYPHEN-MINUS (-) Emit a U+002D
                                 * HYPHEN-MINUS character token. Stay in the
                                 * script data escaped dash dash state.
                                 */
                                continue;
                            case '<':
                                /*
                                 * U+003C LESS-THAN SIGN (<) Switch to the
                                 * script data escaped less-than sign state.
                                 */
                                flushChars(buf, pos);
                                state = transition(state, Tokenizer.SCRIPT_DATA_ESCAPED_LESS_THAN_SIGN, reconsume, pos);
                                continue stateloop;
                            case '>':
                                /*
                                 * U+003E GREATER-THAN SIGN (>) Emit a U+003E
                                 * GREATER-THAN SIGN character token. Switch to
                                 * the script data state.
                                 */
                                state = transition(state, Tokenizer.SCRIPT_DATA, reconsume, pos);
                                continue stateloop;
                            case '\u0000':
                                emitReplacementCharacter(buf, pos);
                                state = transition(state, Tokenizer.SCRIPT_DATA_ESCAPED, reconsume, pos);
                                break scriptdataescapeddashdashloop;
                            case '\r':
                                emitCarriageReturn(buf, pos);
                                state = transition(state, Tokenizer.SCRIPT_DATA_ESCAPED, reconsume, pos);
                                break stateloop;
                            case '\n':
                                silentLineFeed();
                                // CPPONLY: MOZ_FALLTHROUGH;
                            default:
                                /*
                                 * Anything else Emit the current input
                                 * character as a character token. Switch to the
                                 * script data escaped state.
                                 */
                                state = transition(state, Tokenizer.SCRIPT_DATA_ESCAPED, reconsume, pos);
                                // `break` optimizes; `continue stateloop;` would be valid
                                break scriptdataescapeddashdashloop;
                        }
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case SCRIPT_DATA_ESCAPED:
                    scriptdataescapedloop: for (;;) {
                        if (reconsume) {
                            reconsume = false;
                        } else {
                            ++pos;
                            // Perhaps at some point, it will be appropriate to do SIMD in Java, but not today.
                            // The line below advances pos by some number of code units that this state is indifferent to.
                            // CPPONLY: pos += accelerateAdvancementScriptDataEscaped(buf, pos, endPos);
                            if (pos == endPos) {
                                break stateloop;
                            }
                            c = checkChar(buf, pos);
                        }
                        /*
                         * Consume the next input character:
                         */
                        switch (c) {
                            case '-':
                                /*
                                 * U+002D HYPHEN-MINUS (-) Emit a U+002D
                                 * HYPHEN-MINUS character token. Switch to the
                                 * script data escaped dash state.
                                 */
                                state = transition(state, Tokenizer.SCRIPT_DATA_ESCAPED_DASH, reconsume, pos);
                                // `break` optimizes; `continue stateloop;` would be valid
                                break scriptdataescapedloop;
                            case '<':
                                /*
                                 * U+003C LESS-THAN SIGN (<) Switch to the
                                 * script data escaped less-than sign state.
                                 */
                                flushChars(buf, pos);
                                state = transition(state, Tokenizer.SCRIPT_DATA_ESCAPED_LESS_THAN_SIGN, reconsume, pos);
                                continue stateloop;
                            case '\u0000':
                                emitReplacementCharacter(buf, pos);
                                continue;
                            case '\r':
                                emitCarriageReturn(buf, pos);
                                break stateloop;
                            case '\n':
                                silentLineFeed();
                                // CPPONLY: MOZ_FALLTHROUGH;
                            default:
                                /*
                                 * Anything else Emit the current input
                                 * character as a character token. Stay in the
                                 * script data escaped state.
                                 */
                                continue;
                        }
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case SCRIPT_DATA_ESCAPED_DASH:
                    scriptdataescapeddashloop: for (;;) {
                        if (++pos == endPos) {
                            break stateloop;
                        }
                        c = checkChar(buf, pos);
                        /*
                         * Consume the next input character:
                         */
                        switch (c) {
                            case '-':
                                /*
                                 * U+002D HYPHEN-MINUS (-) Emit a U+002D
                                 * HYPHEN-MINUS character token. Switch to the
                                 * script data escaped dash dash state.
                                 */
                                state = transition(state, Tokenizer.SCRIPT_DATA_ESCAPED_DASH_DASH, reconsume, pos);
                                continue stateloop;
                            case '<':
                                /*
                                 * U+003C LESS-THAN SIGN (<) Switch to the
                                 * script data escaped less-than sign state.
                                 */
                                flushChars(buf, pos);
                                state = transition(state, Tokenizer.SCRIPT_DATA_ESCAPED_LESS_THAN_SIGN, reconsume, pos);
                                // `break` optimizes; `continue stateloop;` would be valid
                                break scriptdataescapeddashloop;
                            case '\u0000':
                                emitReplacementCharacter(buf, pos);
                                state = transition(state, Tokenizer.SCRIPT_DATA_ESCAPED, reconsume, pos);
                                continue stateloop;
                            case '\r':
                                emitCarriageReturn(buf, pos);
                                state = transition(state, Tokenizer.SCRIPT_DATA_ESCAPED, reconsume, pos);
                                break stateloop;
                            case '\n':
                                silentLineFeed();
                                // CPPONLY: MOZ_FALLTHROUGH;
                            default:
                                /*
                                 * Anything else Emit the current input
                                 * character as a character token. Switch to the
                                 * script data escaped state.
                                 */
                                state = transition(state, Tokenizer.SCRIPT_DATA_ESCAPED, reconsume, pos);
                                continue stateloop;
                        }
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case SCRIPT_DATA_ESCAPED_LESS_THAN_SIGN:
                    scriptdataescapedlessthanloop: for (;;) {
                        if (++pos == endPos) {
                            break stateloop;
                        }
                        c = checkChar(buf, pos);
                        /*
                         * Consume the next input character:
                         */
                        switch (c) {
                            case '/':
                                /*
                                 * U+002F SOLIDUS (/) Set the temporary buffer
                                 * to the empty string. Switch to the script
                                 * data escaped end tag open state.
                                 */
                                index = 0;
                                clearStrBufBeforeUse();
                                returnState = Tokenizer.SCRIPT_DATA_ESCAPED;
                                state = transition(state, Tokenizer.NON_DATA_END_TAG_NAME, reconsume, pos);
                                continue stateloop;
                            case 'S':
                            case 's':
                                /*
                                 * U+0041 LATIN CAPITAL LETTER A through to
                                 * U+005A LATIN CAPITAL LETTER Z Emit a U+003C
                                 * LESS-THAN SIGN character token and the
                                 * current input character as a character token.
                                 */
                                tokenHandler.characters(Tokenizer.LT_GT, 0, 1);
                                cstart = pos;
                                index = 1;
                                /*
                                 * Set the temporary buffer to the empty string.
                                 * Append the lowercase version of the current
                                 * input character (add 0x0020 to the
                                 * character's code point) to the temporary
                                 * buffer. Switch to the script data double
                                 * escape start state.
                                 */
                                state = transition(state, Tokenizer.SCRIPT_DATA_DOUBLE_ESCAPE_START, reconsume, pos);
                                // `break` optimizes; `continue stateloop;` would be valid
                                break scriptdataescapedlessthanloop;
                            default:
                                /*
                                 * Anything else Emit a U+003C LESS-THAN SIGN
                                 * character token and reconsume the current
                                 * input character in the script data escaped
                                 * state.
                                 */
                                tokenHandler.characters(Tokenizer.LT_GT, 0, 1);
                                cstart = pos;
                                reconsume = true;
                                state = transition(state, Tokenizer.SCRIPT_DATA_ESCAPED, reconsume, pos);
                                continue stateloop;
                        }
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case SCRIPT_DATA_DOUBLE_ESCAPE_START:
                    scriptdatadoubleescapestartloop: for (;;) {
                        if (++pos == endPos) {
                            break stateloop;
                        }
                        c = checkChar(buf, pos);
                        assert index > 0;
                        if (index < 6) { // SCRIPT_ARR.length
                            char folded = c;
                            if (c >= 'A' && c <= 'Z') {
                                folded += 0x20;
                            }
                            if (folded != Tokenizer.SCRIPT_ARR[index]) {
                                reconsume = true;
                                state = transition(state, Tokenizer.SCRIPT_DATA_ESCAPED, reconsume, pos);
                                continue stateloop;
                            }
                            index++;
                            continue;
                        }
                        switch (c) {
                            case '\r':
                                emitCarriageReturn(buf, pos);
                                state = transition(state, Tokenizer.SCRIPT_DATA_DOUBLE_ESCAPED, reconsume, pos);
                                break stateloop;
                            case '\n':
                                silentLineFeed();
                                // CPPONLY: MOZ_FALLTHROUGH;
                            case ' ':
                            case '\t':
                            case '\u000C':
                            case '/':
                            case '>':
                                /*
                                 * U+0009 CHARACTER TABULATION U+000A LINE FEED
                                 * (LF) U+000C FORM FEED (FF) U+0020 SPACE
                                 * U+002F SOLIDUS (/) U+003E GREATER-THAN SIGN
                                 * (>) Emit the current input character as a
                                 * character token. If the temporary buffer is
                                 * the string "script", then switch to the
                                 * script data double escaped state.
                                 */
                                state = transition(state, Tokenizer.SCRIPT_DATA_DOUBLE_ESCAPED, reconsume, pos);
                                // `break` optimizes; `continue stateloop;` would be valid
                                break scriptdatadoubleescapestartloop;
                            default:
                                /*
                                 * Anything else Reconsume the current input
                                 * character in the script data escaped state.
                                 */
                                reconsume = true;
                                state = transition(state, Tokenizer.SCRIPT_DATA_ESCAPED, reconsume, pos);
                                continue stateloop;
                        }
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case SCRIPT_DATA_DOUBLE_ESCAPED:
                    scriptdatadoubleescapedloop: for (;;) {
                        if (reconsume) {
                            reconsume = false;
                        } else {
                            if (++pos == endPos) {
                                break stateloop;
                            }
                            c = checkChar(buf, pos);
                        }
                        /*
                         * Consume the next input character:
                         */
                        switch (c) {
                            case '-':
                                /*
                                 * U+002D HYPHEN-MINUS (-) Emit a U+002D
                                 * HYPHEN-MINUS character token. Switch to the
                                 * script data double escaped dash state.
                                 */
                                state = transition(state, Tokenizer.SCRIPT_DATA_DOUBLE_ESCAPED_DASH, reconsume, pos);
                                // `break` optimizes; `continue stateloop;` would be valid
                                break scriptdatadoubleescapedloop;
                            case '<':
                                /*
                                 * U+003C LESS-THAN SIGN (<) Emit a U+003C
                                 * LESS-THAN SIGN character token. Switch to the
                                 * script data double escaped less-than sign
                                 * state.
                                 */
                                state = transition(state, Tokenizer.SCRIPT_DATA_DOUBLE_ESCAPED_LESS_THAN_SIGN, reconsume, pos);
                                continue stateloop;
                            case '\u0000':
                                emitReplacementCharacter(buf, pos);
                                continue;
                            case '\r':
                                emitCarriageReturn(buf, pos);
                                break stateloop;
                            case '\n':
                                silentLineFeed();
                                // CPPONLY: MOZ_FALLTHROUGH;
                            default:
                                /*
                                 * Anything else Emit the current input
                                 * character as a character token. Stay in the
                                 * script data double escaped state.
                                 */
                                continue;
                        }
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case SCRIPT_DATA_DOUBLE_ESCAPED_DASH:
                    scriptdatadoubleescapeddashloop: for (;;) {
                        if (++pos == endPos) {
                            break stateloop;
                        }
                        c = checkChar(buf, pos);
                        /*
                         * Consume the next input character:
                         */
                        switch (c) {
                            case '-':
                                /*
                                 * U+002D HYPHEN-MINUS (-) Emit a U+002D
                                 * HYPHEN-MINUS character token. Switch to the
                                 * script data double escaped dash dash state.
                                 */
                                state = transition(state, Tokenizer.SCRIPT_DATA_DOUBLE_ESCAPED_DASH_DASH, reconsume, pos);
                                // `break` optimizes; `continue stateloop;` would be valid
                                break scriptdatadoubleescapeddashloop;
                            case '<':
                                /*
                                 * U+003C LESS-THAN SIGN (<) Emit a U+003C
                                 * LESS-THAN SIGN character token. Switch to the
                                 * script data double escaped less-than sign
                                 * state.
                                 */
                                state = transition(state, Tokenizer.SCRIPT_DATA_DOUBLE_ESCAPED_LESS_THAN_SIGN, reconsume, pos);
                                continue stateloop;
                            case '\u0000':
                                emitReplacementCharacter(buf, pos);
                                state = transition(state, Tokenizer.SCRIPT_DATA_DOUBLE_ESCAPED, reconsume, pos);
                                continue stateloop;
                            case '\r':
                                emitCarriageReturn(buf, pos);
                                state = transition(state, Tokenizer.SCRIPT_DATA_DOUBLE_ESCAPED, reconsume, pos);
                                break stateloop;
                            case '\n':
                                silentLineFeed();
                                // CPPONLY: MOZ_FALLTHROUGH;
                            default:
                                /*
                                 * Anything else Emit the current input
                                 * character as a character token. Switch to the
                                 * script data double escaped state.
                                 */
                                state = transition(state, Tokenizer.SCRIPT_DATA_DOUBLE_ESCAPED, reconsume, pos);
                                continue stateloop;
                        }
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case SCRIPT_DATA_DOUBLE_ESCAPED_DASH_DASH:
                    scriptdatadoubleescapeddashdashloop: for (;;) {
                        if (++pos == endPos) {
                            break stateloop;
                        }
                        c = checkChar(buf, pos);
                        /*
                         * Consume the next input character:
                         */
                        switch (c) {
                            case '-':
                                /*
                                 * U+002D HYPHEN-MINUS (-) Emit a U+002D
                                 * HYPHEN-MINUS character token. Stay in the
                                 * script data double escaped dash dash state.
                                 */
                                continue;
                            case '<':
                                /*
                                 * U+003C LESS-THAN SIGN (<) Emit a U+003C
                                 * LESS-THAN SIGN character token. Switch to the
                                 * script data double escaped less-than sign
                                 * state.
                                 */
                                state = transition(state, Tokenizer.SCRIPT_DATA_DOUBLE_ESCAPED_LESS_THAN_SIGN, reconsume, pos);
                                // `break` optimizes; `continue stateloop;` would be valid
                                break scriptdatadoubleescapeddashdashloop;
                            case '>':
                                /*
                                 * U+003E GREATER-THAN SIGN (>) Emit a U+003E
                                 * GREATER-THAN SIGN character token. Switch to
                                 * the script data state.
                                 */
                                state = transition(state, Tokenizer.SCRIPT_DATA, reconsume, pos);
                                continue stateloop;
                            case '\u0000':
                                emitReplacementCharacter(buf, pos);
                                state = transition(state, Tokenizer.SCRIPT_DATA_DOUBLE_ESCAPED, reconsume, pos);
                                continue stateloop;
                            case '\r':
                                emitCarriageReturn(buf, pos);
                                state = transition(state, Tokenizer.SCRIPT_DATA_DOUBLE_ESCAPED, reconsume, pos);
                                break stateloop;
                            case '\n':
                                silentLineFeed();
                                // CPPONLY: MOZ_FALLTHROUGH;
                            default:
                                /*
                                 * Anything else Emit the current input
                                 * character as a character token. Switch to the
                                 * script data double escaped state.
                                 */
                                state = transition(state, Tokenizer.SCRIPT_DATA_DOUBLE_ESCAPED, reconsume, pos);
                                continue stateloop;
                        }
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case SCRIPT_DATA_DOUBLE_ESCAPED_LESS_THAN_SIGN:
                    scriptdatadoubleescapedlessthanloop: for (;;) {
                        if (++pos == endPos) {
                            break stateloop;
                        }
                        c = checkChar(buf, pos);
                        /*
                         * Consume the next input character:
                         */
                        switch (c) {
                            case '/':
                                /*
                                 * U+002F SOLIDUS (/) Emit a U+002F SOLIDUS
                                 * character token. Set the temporary buffer to
                                 * the empty string. Switch to the script data
                                 * double escape end state.
                                 */
                                index = 0;
                                state = transition(state, Tokenizer.SCRIPT_DATA_DOUBLE_ESCAPE_END, reconsume, pos);
                                // `break` optimizes; `continue stateloop;` would be valid
                                break scriptdatadoubleescapedlessthanloop;
                            default:
                                /*
                                 * Anything else Reconsume the current input
                                 * character in the script data double escaped
                                 * state.
                                 */
                                reconsume = true;
                                state = transition(state, Tokenizer.SCRIPT_DATA_DOUBLE_ESCAPED, reconsume, pos);
                                continue stateloop;
                        }
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case SCRIPT_DATA_DOUBLE_ESCAPE_END:
                    scriptdatadoubleescapeendloop: for (;;) {
                        if (++pos == endPos) {
                            break stateloop;
                        }
                        c = checkChar(buf, pos);
                        if (index < 6) { // SCRIPT_ARR.length
                            char folded = c;
                            if (c >= 'A' && c <= 'Z') {
                                folded += 0x20;
                            }
                            if (folded != Tokenizer.SCRIPT_ARR[index]) {
                                reconsume = true;
                                state = transition(state, Tokenizer.SCRIPT_DATA_DOUBLE_ESCAPED, reconsume, pos);
                                continue stateloop;
                            }
                            index++;
                            continue;
                        }
                        switch (c) {
                            case '\r':
                                emitCarriageReturn(buf, pos);
                                state = transition(state, Tokenizer.SCRIPT_DATA_ESCAPED, reconsume, pos);
                                break stateloop;
                            case '\n':
                                silentLineFeed();
                                // CPPONLY: MOZ_FALLTHROUGH;
                            case ' ':
                            case '\t':
                            case '\u000C':
                            case '/':
                            case '>':
                                /*
                                 * U+0009 CHARACTER TABULATION U+000A LINE FEED
                                 * (LF) U+000C FORM FEED (FF) U+0020 SPACE
                                 * U+002F SOLIDUS (/) U+003E GREATER-THAN SIGN
                                 * (>) Emit the current input character as a
                                 * character token. If the temporary buffer is
                                 * the string "script", then switch to the
                                 * script data escaped state.
                                 */
                                state = transition(state, Tokenizer.SCRIPT_DATA_ESCAPED, reconsume, pos);
                                continue stateloop;
                            default:
                                /*
                                 * Reconsume the current input character in the
                                 * script data double escaped state.
                                 */
                                reconsume = true;
                                state = transition(state, Tokenizer.SCRIPT_DATA_DOUBLE_ESCAPED, reconsume, pos);
                                continue stateloop;
                        }
                    }
                    // no fallthrough, reordering opportunity
                case MARKUP_DECLARATION_OCTYPE:
                    markupdeclarationdoctypeloop: for (;;) {
                        if (++pos == endPos) {
                            break stateloop;
                        }
                        c = checkChar(buf, pos);
                        if (index < 6) { // OCTYPE.length
                            char folded = c;
                            if (c >= 'A' && c <= 'Z') {
                                folded += 0x20;
                            }
                            if (folded == Tokenizer.OCTYPE[index]) {
                                appendStrBuf(c);
                            } else {
                                errBogusComment();
                                reconsume = true;
                                state = transition(state, Tokenizer.BOGUS_COMMENT, reconsume, pos);
                                continue stateloop;
                            }
                            index++;
                            continue;
                        } else {
                            reconsume = true;
                            state = transition(state, Tokenizer.DOCTYPE, reconsume, pos);
                            // `break` optimizes; `continue stateloop;` would be valid
                            break markupdeclarationdoctypeloop;
                        }
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case DOCTYPE:
                    doctypeloop: for (;;) {
                        if (reconsume) {
                            reconsume = false;
                        } else {
                            if (++pos == endPos) {
                                break stateloop;
                            }
                            c = checkChar(buf, pos);
                        }
                        initDoctypeFields();
                        /*
                         * Consume the next input character:
                         */
                        switch (c) {
                            case '\r':
                                silentCarriageReturn();
                                state = transition(state, Tokenizer.BEFORE_DOCTYPE_NAME, reconsume, pos);
                                break stateloop;
                            case '\n':
                                silentLineFeed();
                                // CPPONLY: MOZ_FALLTHROUGH;
                            case ' ':
                            case '\t':
                            case '\u000C':
                                /*
                                 * U+0009 CHARACTER TABULATION U+000A LINE FEED
                                 * (LF) U+000C FORM FEED (FF) U+0020 SPACE
                                 * Switch to the before DOCTYPE name state.
                                 */
                                state = transition(state, Tokenizer.BEFORE_DOCTYPE_NAME, reconsume, pos);
                                // `break` optimizes; `continue stateloop;` would be valid
                                break doctypeloop;
                            default:
                                /*
                                 * Anything else Parse error.
                                 */
                                errMissingSpaceBeforeDoctypeName();
                                /*
                                 * Reconsume the current character in the before
                                 * DOCTYPE name state.
                                 */
                                reconsume = true;
                                state = transition(state, Tokenizer.BEFORE_DOCTYPE_NAME, reconsume, pos);
                                // `break` optimizes; `continue stateloop;` would be valid
                                break doctypeloop;
                        }
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case BEFORE_DOCTYPE_NAME:
                    beforedoctypenameloop: for (;;) {
                        if (reconsume) {
                            reconsume = false;
                        } else {
                            if (++pos == endPos) {
                                break stateloop;
                            }
                            c = checkChar(buf, pos);
                        }
                        /*
                         * Consume the next input character:
                         */
                        switch (c) {
                            case '\r':
                                silentCarriageReturn();
                                break stateloop;
                            case '\n':
                                silentLineFeed();
                                // CPPONLY: MOZ_FALLTHROUGH;
                            case ' ':
                            case '\t':
                            case '\u000C':
                                /*
                                 * U+0009 CHARACTER TABULATION U+000A LINE FEED
                                 * (LF) U+000C FORM FEED (FF) U+0020 SPACE Stay
                                 * in the before DOCTYPE name state.
                                 */
                                continue;
                            case '>':
                                /*
                                 * U+003E GREATER-THAN SIGN (>) Parse error.
                                 */
                                errNamelessDoctype();
                                /*
                                 * Create a new DOCTYPE token. Set its
                                 * force-quirks flag to on.
                                 */
                                forceQuirks = true;
                                /*
                                 * Emit the token.
                                 */
                                emitDoctypeToken(pos);
                                /*
                                 * Switch to the data state.
                                 */
                                state = transition(state, Tokenizer.DATA, reconsume, pos);
                                if (shouldSuspend) {
                                    break stateloop;
                                }
                                continue stateloop;
                            case '\u0000':
                                c = '\uFFFD';
                                // CPPONLY: MOZ_FALLTHROUGH;
                            default:
                                if (c >= 'A' && c <= 'Z') {
                                    /*
                                     * U+0041 LATIN CAPITAL LETTER A through to
                                     * U+005A LATIN CAPITAL LETTER Z Create a
                                     * new DOCTYPE token. Set the token's name
                                     * to the lowercase version of the input
                                     * character (add 0x0020 to the character's
                                     * code point).
                                     */
                                    c += 0x20;
                                }
                                /* Anything else Create a new DOCTYPE token. */
                                /*
                                 * Set the token's name name to the current
                                 * input character.
                                 */
                                clearStrBufBeforeUse();
                                appendStrBuf(c);
                                /*
                                 * Switch to the DOCTYPE name state.
                                 */
                                state = transition(state, Tokenizer.DOCTYPE_NAME, reconsume, pos);
                                // `break` optimizes; `continue stateloop;` would be valid
                                break beforedoctypenameloop;
                        }
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case DOCTYPE_NAME:
                    doctypenameloop: for (;;) {
                        if (++pos == endPos) {
                            break stateloop;
                        }
                        c = checkChar(buf, pos);
                        /*
                         * Consume the next input character:
                         */
                        switch (c) {
                            case '\r':
                                silentCarriageReturn();
                                strBufToDoctypeName();
                                state = transition(state, Tokenizer.AFTER_DOCTYPE_NAME, reconsume, pos);
                                break stateloop;
                            case '\n':
                                silentLineFeed();
                                // CPPONLY: MOZ_FALLTHROUGH;
                            case ' ':
                            case '\t':
                            case '\u000C':
                                /*
                                 * U+0009 CHARACTER TABULATION U+000A LINE FEED
                                 * (LF) U+000C FORM FEED (FF) U+0020 SPACE
                                 * Switch to the after DOCTYPE name state.
                                 */
                                strBufToDoctypeName();
                                state = transition(state, Tokenizer.AFTER_DOCTYPE_NAME, reconsume, pos);
                                // `break` optimizes; `continue stateloop;` would be valid
                                break doctypenameloop;
                            case '>':
                                /*
                                 * U+003E GREATER-THAN SIGN (>) Emit the current
                                 * DOCTYPE token.
                                 */
                                strBufToDoctypeName();
                                emitDoctypeToken(pos);
                                /*
                                 * Switch to the data state.
                                 */
                                state = transition(state, Tokenizer.DATA, reconsume, pos);
                                if (shouldSuspend) {
                                    break stateloop;
                                }
                                continue stateloop;
                            case '\u0000':
                                c = '\uFFFD';
                                // CPPONLY: MOZ_FALLTHROUGH;
                            default:
                                /*
                                 * U+0041 LATIN CAPITAL LETTER A through to
                                 * U+005A LATIN CAPITAL LETTER Z Append the
                                 * lowercase version of the input character (add
                                 * 0x0020 to the character's code point) to the
                                 * current DOCTYPE token's name.
                                 */
                                if (c >= 'A' && c <= 'Z') {
                                    c += 0x0020;
                                }
                                /*
                                 * Anything else Append the current input
                                 * character to the current DOCTYPE token's
                                 * name.
                                 */
                                appendStrBuf(c);
                                /*
                                 * Stay in the DOCTYPE name state.
                                 */
                                continue;
                        }
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case AFTER_DOCTYPE_NAME:
                    afterdoctypenameloop: for (;;) {
                        if (++pos == endPos) {
                            break stateloop;
                        }
                        c = checkChar(buf, pos);
                        /*
                         * Consume the next input character:
                         */
                        switch (c) {
                            case '\r':
                                silentCarriageReturn();
                                break stateloop;
                            case '\n':
                                silentLineFeed();
                                // CPPONLY: MOZ_FALLTHROUGH;
                            case ' ':
                            case '\t':
                            case '\u000C':
                                /*
                                 * U+0009 CHARACTER TABULATION U+000A LINE FEED
                                 * (LF) U+000C FORM FEED (FF) U+0020 SPACE Stay
                                 * in the after DOCTYPE name state.
                                 */
                                continue;
                            case '>':
                                /*
                                 * U+003E GREATER-THAN SIGN (>) Emit the current
                                 * DOCTYPE token.
                                 */
                                emitDoctypeToken(pos);
                                /*
                                 * Switch to the data state.
                                 */
                                state = transition(state, Tokenizer.DATA, reconsume, pos);
                                if (shouldSuspend) {
                                    break stateloop;
                                }
                                continue stateloop;
                            case 'p':
                            case 'P':
                                index = 0;
                                state = transition(state, Tokenizer.DOCTYPE_UBLIC, reconsume, pos);
                                // `break` optimizes; `continue stateloop;` would be valid
                                break afterdoctypenameloop;
                            case 's':
                            case 'S':
                                index = 0;
                                state = transition(state, Tokenizer.DOCTYPE_YSTEM, reconsume, pos);
                                continue stateloop;
                            default:
                                /*
                                 * Otherwise, this is the parse error.
                                 */
                                bogusDoctype();

                                /*
                                 * Set the DOCTYPE token's force-quirks flag to
                                 * on.
                                 */
                                // done by bogusDoctype();
                                /*
                                 * Switch to the bogus DOCTYPE state.
                                 */
                                state = transition(state, Tokenizer.BOGUS_DOCTYPE, reconsume, pos);
                                continue stateloop;
                        }
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case DOCTYPE_UBLIC:
                    doctypeublicloop: for (;;) {
                        if (++pos == endPos) {
                            break stateloop;
                        }
                        c = checkChar(buf, pos);
                        /*
                         * If the six characters starting from the current input
                         * character are an ASCII case-insensitive match for the
                         * word "PUBLIC", then consume those characters and
                         * switch to the before DOCTYPE public identifier state.
                         */
                        if (index < 5) { // UBLIC.length
                            char folded = c;
                            if (c >= 'A' && c <= 'Z') {
                                folded += 0x20;
                            }
                            if (folded != Tokenizer.UBLIC[index]) {
                                bogusDoctype();
                                // forceQuirks = true;
                                reconsume = true;
                                state = transition(state, Tokenizer.BOGUS_DOCTYPE, reconsume, pos);
                                continue stateloop;
                            }
                            index++;
                            continue;
                        } else {
                            reconsume = true;
                            state = transition(state, Tokenizer.AFTER_DOCTYPE_PUBLIC_KEYWORD, reconsume, pos);
                            // `break` optimizes; `continue stateloop;` would be valid
                            break doctypeublicloop;
                        }
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case AFTER_DOCTYPE_PUBLIC_KEYWORD:
                    afterdoctypepublickeywordloop: for (;;) {
                        if (reconsume) {
                            reconsume = false;
                        } else {
                            if (++pos == endPos) {
                                break stateloop;
                            }
                            c = checkChar(buf, pos);
                        }
                        /*
                         * Consume the next input character:
                         */
                        switch (c) {
                            case '\r':
                                silentCarriageReturn();
                                state = transition(state, Tokenizer.BEFORE_DOCTYPE_PUBLIC_IDENTIFIER, reconsume, pos);
                                break stateloop;
                            case '\n':
                                silentLineFeed();
                                // CPPONLY: MOZ_FALLTHROUGH;
                            case ' ':
                            case '\t':
                            case '\u000C':
                                /*
                                 * U+0009 CHARACTER TABULATION U+000A LINE FEED
                                 * (LF) U+000C FORM FEED (FF) U+0020 SPACE
                                 * Switch to the before DOCTYPE public
                                 * identifier state.
                                 */
                                state = transition(state, Tokenizer.BEFORE_DOCTYPE_PUBLIC_IDENTIFIER, reconsume, pos);
                                // `break` optimizes; `continue stateloop;` would be valid
                                break afterdoctypepublickeywordloop;
                            case '"':
                                /*
                                 * U+0022 QUOTATION MARK (") Parse Error.
                                 */
                                errNoSpaceBetweenDoctypePublicKeywordAndQuote();
                                /*
                                 * Set the DOCTYPE token's public identifier to
                                 * the empty string (not missing),
                                 */
                                clearStrBufBeforeUse();
                                /*
                                 * then switch to the DOCTYPE public identifier
                                 * (double-quoted) state.
                                 */
                                state = transition(state, Tokenizer.DOCTYPE_PUBLIC_IDENTIFIER_DOUBLE_QUOTED, reconsume, pos);
                                continue stateloop;
                            case '\'':
                                /*
                                 * U+0027 APOSTROPHE (') Parse Error.
                                 */
                                errNoSpaceBetweenDoctypePublicKeywordAndQuote();
                                /*
                                 * Set the DOCTYPE token's public identifier to
                                 * the empty string (not missing),
                                 */
                                clearStrBufBeforeUse();
                                /*
                                 * then switch to the DOCTYPE public identifier
                                 * (single-quoted) state.
                                 */
                                state = transition(state, Tokenizer.DOCTYPE_PUBLIC_IDENTIFIER_SINGLE_QUOTED, reconsume, pos);
                                continue stateloop;
                            case '>':
                                /* U+003E GREATER-THAN SIGN (>) Parse error. */
                                errExpectedPublicId();
                                /*
                                 * Set the DOCTYPE token's force-quirks flag to
                                 * on.
                                 */
                                forceQuirks = true;
                                /*
                                 * Emit that DOCTYPE token.
                                 */
                                emitDoctypeToken(pos);
                                /*
                                 * Switch to the data state.
                                 */
                                state = transition(state, Tokenizer.DATA, reconsume, pos);
                                if (shouldSuspend) {
                                    break stateloop;
                                }
                                continue stateloop;
                            default:
                                bogusDoctype();
                                /*
                                 * Set the DOCTYPE token's force-quirks flag to
                                 * on.
                                 */
                                // done by bogusDoctype();
                                /*
                                 * Switch to the bogus DOCTYPE state.
                                 */
                                state = transition(state, Tokenizer.BOGUS_DOCTYPE, reconsume, pos);
                                continue stateloop;
                        }
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case BEFORE_DOCTYPE_PUBLIC_IDENTIFIER:
                    beforedoctypepublicidentifierloop: for (;;) {
                        if (++pos == endPos) {
                            break stateloop;
                        }
                        c = checkChar(buf, pos);
                        /*
                         * Consume the next input character:
                         */
                        switch (c) {
                            case '\r':
                                silentCarriageReturn();
                                break stateloop;
                            case '\n':
                                silentLineFeed();
                                // CPPONLY: MOZ_FALLTHROUGH;
                            case ' ':
                            case '\t':
                            case '\u000C':
                                /*
                                 * U+0009 CHARACTER TABULATION U+000A LINE FEED
                                 * (LF) U+000C FORM FEED (FF) U+0020 SPACE Stay
                                 * in the before DOCTYPE public identifier
                                 * state.
                                 */
                                continue;
                            case '"':
                                /*
                                 * U+0022 QUOTATION MARK (") Set the DOCTYPE
                                 * token's public identifier to the empty string
                                 * (not missing),
                                 */
                                clearStrBufBeforeUse();
                                /*
                                 * then switch to the DOCTYPE public identifier
                                 * (double-quoted) state.
                                 */
                                state = transition(state, Tokenizer.DOCTYPE_PUBLIC_IDENTIFIER_DOUBLE_QUOTED, reconsume, pos);
                                // `break` optimizes; `continue stateloop;` would be valid
                                break beforedoctypepublicidentifierloop;
                            case '\'':
                                /*
                                 * U+0027 APOSTROPHE (') Set the DOCTYPE token's
                                 * public identifier to the empty string (not
                                 * missing),
                                 */
                                clearStrBufBeforeUse();
                                /*
                                 * then switch to the DOCTYPE public identifier
                                 * (single-quoted) state.
                                 */
                                state = transition(state, Tokenizer.DOCTYPE_PUBLIC_IDENTIFIER_SINGLE_QUOTED, reconsume, pos);
                                continue stateloop;
                            case '>':
                                /* U+003E GREATER-THAN SIGN (>) Parse error. */
                                errExpectedPublicId();
                                /*
                                 * Set the DOCTYPE token's force-quirks flag to
                                 * on.
                                 */
                                forceQuirks = true;
                                /*
                                 * Emit that DOCTYPE token.
                                 */
                                emitDoctypeToken(pos);
                                /*
                                 * Switch to the data state.
                                 */
                                state = transition(state, Tokenizer.DATA, reconsume, pos);
                                if (shouldSuspend) {
                                    break stateloop;
                                }
                                continue stateloop;
                            default:
                                bogusDoctype();
                                /*
                                 * Set the DOCTYPE token's force-quirks flag to
                                 * on.
                                 */
                                // done by bogusDoctype();
                                /*
                                 * Switch to the bogus DOCTYPE state.
                                 */
                                state = transition(state, Tokenizer.BOGUS_DOCTYPE, reconsume, pos);
                                continue stateloop;
                        }
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case DOCTYPE_PUBLIC_IDENTIFIER_DOUBLE_QUOTED:
                    doctypepublicidentifierdoublequotedloop: for (;;) {
                        if (++pos == endPos) {
                            break stateloop;
                        }
                        c = checkChar(buf, pos);
                        /*
                         * Consume the next input character:
                         */
                        switch (c) {
                            case '"':
                                /*
                                 * U+0022 QUOTATION MARK (") Switch to the after
                                 * DOCTYPE public identifier state.
                                 */
                                publicIdentifier = strBufToString();
                                state = transition(state, Tokenizer.AFTER_DOCTYPE_PUBLIC_IDENTIFIER, reconsume, pos);
                                // `break` optimizes; `continue stateloop;` would be valid
                                break doctypepublicidentifierdoublequotedloop;
                            case '>':
                                /*
                                 * U+003E GREATER-THAN SIGN (>) Parse error.
                                 */
                                errGtInPublicId();
                                /*
                                 * Set the DOCTYPE token's force-quirks flag to
                                 * on.
                                 */
                                forceQuirks = true;
                                /*
                                 * Emit that DOCTYPE token.
                                 */
                                publicIdentifier = strBufToString();
                                emitDoctypeToken(pos);
                                /*
                                 * Switch to the data state.
                                 */
                                state = transition(state, Tokenizer.DATA, reconsume, pos);
                                if (shouldSuspend) {
                                    break stateloop;
                                }
                                continue stateloop;
                            case '\r':
                                appendStrBufCarriageReturn();
                                break stateloop;
                            case '\n':
                                appendStrBufLineFeed();
                                continue;
                            case '\u0000':
                                c = '\uFFFD';
                                // CPPONLY: MOZ_FALLTHROUGH;
                            default:
                                /*
                                 * Anything else Append the current input
                                 * character to the current DOCTYPE token's
                                 * public identifier.
                                 */
                                appendStrBuf(c);
                                /*
                                 * Stay in the DOCTYPE public identifier
                                 * (double-quoted) state.
                                 */
                                continue;
                        }
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case AFTER_DOCTYPE_PUBLIC_IDENTIFIER:
                    afterdoctypepublicidentifierloop: for (;;) {
                        if (++pos == endPos) {
                            break stateloop;
                        }
                        c = checkChar(buf, pos);
                        /*
                         * Consume the next input character:
                         */
                        switch (c) {
                            case '\r':
                                silentCarriageReturn();
                                state = transition(state, Tokenizer.BETWEEN_DOCTYPE_PUBLIC_AND_SYSTEM_IDENTIFIERS, reconsume, pos);
                                break stateloop;
                            case '\n':
                                silentLineFeed();
                                // CPPONLY: MOZ_FALLTHROUGH;
                            case ' ':
                            case '\t':
                            case '\u000C':
                                /*
                                 * U+0009 CHARACTER TABULATION U+000A LINE FEED
                                 * (LF) U+000C FORM FEED (FF) U+0020 SPACE
                                 * Switch to the between DOCTYPE public and
                                 * system identifiers state.
                                 */
                                state = transition(state, Tokenizer.BETWEEN_DOCTYPE_PUBLIC_AND_SYSTEM_IDENTIFIERS, reconsume, pos);
                                // `break` optimizes; `continue stateloop;` would be valid
                                break afterdoctypepublicidentifierloop;
                            case '>':
                                /*
                                 * U+003E GREATER-THAN SIGN (>) Emit the current
                                 * DOCTYPE token.
                                 */
                                emitDoctypeToken(pos);
                                /*
                                 * Switch to the data state.
                                 */
                                state = transition(state, Tokenizer.DATA, reconsume, pos);
                                if (shouldSuspend) {
                                    break stateloop;
                                }
                                continue stateloop;
                            case '"':
                                /*
                                 * U+0022 QUOTATION MARK (") Parse error.
                                 */
                                errNoSpaceBetweenPublicAndSystemIds();
                                /*
                                 * Set the DOCTYPE token's system identifier to
                                 * the empty string (not missing),
                                 */
                                clearStrBufBeforeUse();
                                /*
                                 * then switch to the DOCTYPE system identifier
                                 * (double-quoted) state.
                                 */
                                state = transition(state, Tokenizer.DOCTYPE_SYSTEM_IDENTIFIER_DOUBLE_QUOTED, reconsume, pos);
                                continue stateloop;
                            case '\'':
                                /*
                                 * U+0027 APOSTROPHE (') Parse error.
                                 */
                                errNoSpaceBetweenPublicAndSystemIds();
                                /*
                                 * Set the DOCTYPE token's system identifier to
                                 * the empty string (not missing),
                                 */
                                clearStrBufBeforeUse();
                                /*
                                 * then switch to the DOCTYPE system identifier
                                 * (single-quoted) state.
                                 */
                                state = transition(state, Tokenizer.DOCTYPE_SYSTEM_IDENTIFIER_SINGLE_QUOTED, reconsume, pos);
                                continue stateloop;
                            default:
                                bogusDoctype();
                                /*
                                 * Set the DOCTYPE token's force-quirks flag to
                                 * on.
                                 */
                                // done by bogusDoctype();
                                /*
                                 * Switch to the bogus DOCTYPE state.
                                 */
                                state = transition(state, Tokenizer.BOGUS_DOCTYPE, reconsume, pos);
                                continue stateloop;
                        }
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case BETWEEN_DOCTYPE_PUBLIC_AND_SYSTEM_IDENTIFIERS:
                    betweendoctypepublicandsystemidentifiersloop: for (;;) {
                        if (++pos == endPos) {
                            break stateloop;
                        }
                        c = checkChar(buf, pos);
                        /*
                         * Consume the next input character:
                         */
                        switch (c) {
                            case '\r':
                                silentCarriageReturn();
                                break stateloop;
                            case '\n':
                                silentLineFeed();
                                // CPPONLY: MOZ_FALLTHROUGH;
                            case ' ':
                            case '\t':
                            case '\u000C':
                                /*
                                 * U+0009 CHARACTER TABULATION U+000A LINE FEED
                                 * (LF) U+000C FORM FEED (FF) U+0020 SPACE Stay
                                 * in the between DOCTYPE public and system
                                 * identifiers state.
                                 */
                                continue;
                            case '>':
                                /*
                                 * U+003E GREATER-THAN SIGN (>) Emit the current
                                 * DOCTYPE token.
                                 */
                                emitDoctypeToken(pos);
                                /*
                                 * Switch to the data state.
                                 */
                                state = transition(state, Tokenizer.DATA, reconsume, pos);
                                if (shouldSuspend) {
                                    break stateloop;
                                }
                                continue stateloop;
                            case '"':
                                /*
                                 * U+0022 QUOTATION MARK (") Set the DOCTYPE
                                 * token's system identifier to the empty string
                                 * (not missing),
                                 */
                                clearStrBufBeforeUse();
                                /*
                                 * then switch to the DOCTYPE system identifier
                                 * (double-quoted) state.
                                 */
                                state = transition(state, Tokenizer.DOCTYPE_SYSTEM_IDENTIFIER_DOUBLE_QUOTED, reconsume, pos);
                                // `break` optimizes; `continue stateloop;` would be valid
                                break betweendoctypepublicandsystemidentifiersloop;
                            case '\'':
                                /*
                                 * U+0027 APOSTROPHE (') Set the DOCTYPE token's
                                 * system identifier to the empty string (not
                                 * missing),
                                 */
                                clearStrBufBeforeUse();
                                /*
                                 * then switch to the DOCTYPE system identifier
                                 * (single-quoted) state.
                                 */
                                state = transition(state, Tokenizer.DOCTYPE_SYSTEM_IDENTIFIER_SINGLE_QUOTED, reconsume, pos);
                                continue stateloop;
                            default:
                                bogusDoctype();
                                /*
                                 * Set the DOCTYPE token's force-quirks flag to
                                 * on.
                                 */
                                // done by bogusDoctype();
                                /*
                                 * Switch to the bogus DOCTYPE state.
                                 */
                                state = transition(state, Tokenizer.BOGUS_DOCTYPE, reconsume, pos);
                                continue stateloop;
                        }
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case DOCTYPE_SYSTEM_IDENTIFIER_DOUBLE_QUOTED:
                    doctypesystemidentifierdoublequotedloop: for (;;) {
                        if (++pos == endPos) {
                            break stateloop;
                        }
                        c = checkChar(buf, pos);
                        /*
                         * Consume the next input character:
                         */
                        switch (c) {
                            case '"':
                                /*
                                 * U+0022 QUOTATION MARK (") Switch to the after
                                 * DOCTYPE system identifier state.
                                 */
                                systemIdentifier = strBufToString();
                                state = transition(state, Tokenizer.AFTER_DOCTYPE_SYSTEM_IDENTIFIER, reconsume, pos);
                                // `break` optimizes; `continue stateloop;` would be valid
                                break doctypesystemidentifierdoublequotedloop;
                            case '>':
                                /*
                                 * U+003E GREATER-THAN SIGN (>) Parse error.
                                 */
                                errGtInSystemId();
                                /*
                                 * Set the DOCTYPE token's force-quirks flag to
                                 * on.
                                 */
                                forceQuirks = true;
                                /*
                                 * Emit that DOCTYPE token.
                                 */
                                systemIdentifier = strBufToString();
                                emitDoctypeToken(pos);
                                /*
                                 * Switch to the data state.
                                 */
                                state = transition(state, Tokenizer.DATA, reconsume, pos);
                                if (shouldSuspend) {
                                    break stateloop;
                                }
                                continue stateloop;
                            case '\r':
                                appendStrBufCarriageReturn();
                                break stateloop;
                            case '\n':
                                appendStrBufLineFeed();
                                continue;
                            case '\u0000':
                                c = '\uFFFD';
                                // CPPONLY: MOZ_FALLTHROUGH;
                            default:
                                /*
                                 * Anything else Append the current input
                                 * character to the current DOCTYPE token's
                                 * system identifier.
                                 */
                                appendStrBuf(c);
                                /*
                                 * Stay in the DOCTYPE system identifier
                                 * (double-quoted) state.
                                 */
                                continue;
                        }
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case AFTER_DOCTYPE_SYSTEM_IDENTIFIER:
                    afterdoctypesystemidentifierloop: for (;;) {
                        if (++pos == endPos) {
                            break stateloop;
                        }
                        c = checkChar(buf, pos);
                        /*
                         * Consume the next input character:
                         */
                        switch (c) {
                            case '\r':
                                silentCarriageReturn();
                                break stateloop;
                            case '\n':
                                silentLineFeed();
                                // CPPONLY: MOZ_FALLTHROUGH;
                            case ' ':
                            case '\t':
                            case '\u000C':
                                /*
                                 * U+0009 CHARACTER TABULATION U+000A LINE FEED
                                 * (LF) U+000C FORM FEED (FF) U+0020 SPACE Stay
                                 * in the after DOCTYPE system identifier state.
                                 */
                                continue;
                            case '>':
                                /*
                                 * U+003E GREATER-THAN SIGN (>) Emit the current
                                 * DOCTYPE token.
                                 */
                                emitDoctypeToken(pos);
                                /*
                                 * Switch to the data state.
                                 */
                                state = transition(state, Tokenizer.DATA, reconsume, pos);
                                if (shouldSuspend) {
                                    break stateloop;
                                }
                                continue stateloop;
                            default:
                                /*
                                 * Switch to the bogus DOCTYPE state. (This does
                                 * not set the DOCTYPE token's force-quirks flag
                                 * to on.)
                                 */
                                bogusDoctypeWithoutQuirks();
                                state = transition(state, Tokenizer.BOGUS_DOCTYPE, reconsume, pos);
                                // `break` optimizes; `continue stateloop;` would be valid
                                break afterdoctypesystemidentifierloop;
                        }
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case BOGUS_DOCTYPE:
                    for (;;) {
                        if (reconsume) {
                            reconsume = false;
                        } else {
                            if (++pos == endPos) {
                                break stateloop;
                            }
                            c = checkChar(buf, pos);
                        }
                        /*
                         * Consume the next input character:
                         */
                        switch (c) {
                            case '>':
                                /*
                                 * U+003E GREATER-THAN SIGN (>) Emit that
                                 * DOCTYPE token.
                                 */
                                emitDoctypeToken(pos);
                                /*
                                 * Switch to the data state.
                                 */
                                state = transition(state, Tokenizer.DATA, reconsume, pos);
                                if (shouldSuspend) {
                                    break stateloop;
                                }
                                continue stateloop;
                            case '\r':
                                silentCarriageReturn();
                                break stateloop;
                            case '\n':
                                silentLineFeed();
                                // CPPONLY: MOZ_FALLTHROUGH;
                            default:
                                /*
                                 * Anything else Stay in the bogus DOCTYPE
                                 * state.
                                 */
                                continue;
                        }
                    }
                    // no fallthrough, reordering opportunity
                case DOCTYPE_YSTEM:
                    doctypeystemloop: for (;;) {
                        if (++pos == endPos) {
                            break stateloop;
                        }
                        c = checkChar(buf, pos);
                        /*
                         * Otherwise, if the six characters starting from the
                         * current input character are an ASCII case-insensitive
                         * match for the word "SYSTEM", then consume those
                         * characters and switch to the before DOCTYPE system
                         * identifier state.
                         */
                        if (index < 5) { // YSTEM.length
                            char folded = c;
                            if (c >= 'A' && c <= 'Z') {
                                folded += 0x20;
                            }
                            if (folded != Tokenizer.YSTEM[index]) {
                                bogusDoctype();
                                reconsume = true;
                                state = transition(state, Tokenizer.BOGUS_DOCTYPE, reconsume, pos);
                                continue stateloop;
                            }
                            index++;
                            continue stateloop;
                        } else {
                            reconsume = true;
                            state = transition(state, Tokenizer.AFTER_DOCTYPE_SYSTEM_KEYWORD, reconsume, pos);
                            // `break` optimizes; `continue stateloop;` would be valid
                            break doctypeystemloop;
                        }
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case AFTER_DOCTYPE_SYSTEM_KEYWORD:
                    afterdoctypesystemkeywordloop: for (;;) {
                        if (reconsume) {
                            reconsume = false;
                        } else {
                            if (++pos == endPos) {
                                break stateloop;
                            }
                            c = checkChar(buf, pos);
                        }
                        /*
                         * Consume the next input character:
                         */
                        switch (c) {
                            case '\r':
                                silentCarriageReturn();
                                state = transition(state, Tokenizer.BEFORE_DOCTYPE_SYSTEM_IDENTIFIER, reconsume, pos);
                                break stateloop;
                            case '\n':
                                silentLineFeed();
                                // CPPONLY: MOZ_FALLTHROUGH;
                            case ' ':
                            case '\t':
                            case '\u000C':
                                /*
                                 * U+0009 CHARACTER TABULATION U+000A LINE FEED
                                 * (LF) U+000C FORM FEED (FF) U+0020 SPACE
                                 * Switch to the before DOCTYPE public
                                 * identifier state.
                                 */
                                state = transition(state, Tokenizer.BEFORE_DOCTYPE_SYSTEM_IDENTIFIER, reconsume, pos);
                                // `break` optimizes; `continue stateloop;` would be valid
                                break afterdoctypesystemkeywordloop;
                            case '"':
                                /*
                                 * U+0022 QUOTATION MARK (") Parse Error.
                                 */
                                errNoSpaceBetweenDoctypeSystemKeywordAndQuote();
                                /*
                                 * Set the DOCTYPE token's system identifier to
                                 * the empty string (not missing),
                                 */
                                clearStrBufBeforeUse();
                                /*
                                 * then switch to the DOCTYPE public identifier
                                 * (double-quoted) state.
                                 */
                                state = transition(state, Tokenizer.DOCTYPE_SYSTEM_IDENTIFIER_DOUBLE_QUOTED, reconsume, pos);
                                continue stateloop;
                            case '\'':
                                /*
                                 * U+0027 APOSTROPHE (') Parse Error.
                                 */
                                errNoSpaceBetweenDoctypeSystemKeywordAndQuote();
                                /*
                                 * Set the DOCTYPE token's public identifier to
                                 * the empty string (not missing),
                                 */
                                clearStrBufBeforeUse();
                                /*
                                 * then switch to the DOCTYPE public identifier
                                 * (single-quoted) state.
                                 */
                                state = transition(state, Tokenizer.DOCTYPE_SYSTEM_IDENTIFIER_SINGLE_QUOTED, reconsume, pos);
                                continue stateloop;
                            case '>':
                                /* U+003E GREATER-THAN SIGN (>) Parse error. */
                                errExpectedPublicId();
                                /*
                                 * Set the DOCTYPE token's force-quirks flag to
                                 * on.
                                 */
                                forceQuirks = true;
                                /*
                                 * Emit that DOCTYPE token.
                                 */
                                emitDoctypeToken(pos);
                                /*
                                 * Switch to the data state.
                                 */
                                state = transition(state, Tokenizer.DATA, reconsume, pos);
                                if (shouldSuspend) {
                                    break stateloop;
                                }
                                continue stateloop;
                            default:
                                bogusDoctype();
                                /*
                                 * Set the DOCTYPE token's force-quirks flag to
                                 * on.
                                 */
                                // done by bogusDoctype();
                                /*
                                 * Switch to the bogus DOCTYPE state.
                                 */
                                state = transition(state, Tokenizer.BOGUS_DOCTYPE, reconsume, pos);
                                continue stateloop;
                        }
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case BEFORE_DOCTYPE_SYSTEM_IDENTIFIER:
                    beforedoctypesystemidentifierloop: for (;;) {
                        if (++pos == endPos) {
                            break stateloop;
                        }
                        c = checkChar(buf, pos);
                        /*
                         * Consume the next input character:
                         */
                        switch (c) {
                            case '\r':
                                silentCarriageReturn();
                                break stateloop;
                            case '\n':
                                silentLineFeed();
                                // CPPONLY: MOZ_FALLTHROUGH;
                            case ' ':
                            case '\t':
                            case '\u000C':
                                /*
                                 * U+0009 CHARACTER TABULATION U+000A LINE FEED
                                 * (LF) U+000C FORM FEED (FF) U+0020 SPACE Stay
                                 * in the before DOCTYPE system identifier
                                 * state.
                                 */
                                continue;
                            case '"':
                                /*
                                 * U+0022 QUOTATION MARK (") Set the DOCTYPE
                                 * token's system identifier to the empty string
                                 * (not missing),
                                 */
                                clearStrBufBeforeUse();
                                /*
                                 * then switch to the DOCTYPE system identifier
                                 * (double-quoted) state.
                                 */
                                state = transition(state, Tokenizer.DOCTYPE_SYSTEM_IDENTIFIER_DOUBLE_QUOTED, reconsume, pos);
                                continue stateloop;
                            case '\'':
                                /*
                                 * U+0027 APOSTROPHE (') Set the DOCTYPE token's
                                 * system identifier to the empty string (not
                                 * missing),
                                 */
                                clearStrBufBeforeUse();
                                /*
                                 * then switch to the DOCTYPE system identifier
                                 * (single-quoted) state.
                                 */
                                state = transition(state, Tokenizer.DOCTYPE_SYSTEM_IDENTIFIER_SINGLE_QUOTED, reconsume, pos);
                                // `break` optimizes; `continue stateloop;` would be valid
                                break beforedoctypesystemidentifierloop;
                            case '>':
                                /* U+003E GREATER-THAN SIGN (>) Parse error. */
                                errExpectedSystemId();
                                /*
                                 * Set the DOCTYPE token's force-quirks flag to
                                 * on.
                                 */
                                forceQuirks = true;
                                /*
                                 * Emit that DOCTYPE token.
                                 */
                                emitDoctypeToken(pos);
                                /*
                                 * Switch to the data state.
                                 */
                                state = transition(state, Tokenizer.DATA, reconsume, pos);
                                if (shouldSuspend) {
                                    break stateloop;
                                }
                                continue stateloop;
                            default:
                                bogusDoctype();
                                /*
                                 * Set the DOCTYPE token's force-quirks flag to
                                 * on.
                                 */
                                // done by bogusDoctype();
                                /*
                                 * Switch to the bogus DOCTYPE state.
                                 */
                                state = transition(state, Tokenizer.BOGUS_DOCTYPE, reconsume, pos);
                                continue stateloop;
                        }
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case DOCTYPE_SYSTEM_IDENTIFIER_SINGLE_QUOTED:
                    for (;;) {
                        if (++pos == endPos) {
                            break stateloop;
                        }
                        c = checkChar(buf, pos);
                        /*
                         * Consume the next input character:
                         */
                        switch (c) {
                            case '\'':
                                /*
                                 * U+0027 APOSTROPHE (') Switch to the after
                                 * DOCTYPE system identifier state.
                                 */
                                systemIdentifier = strBufToString();
                                state = transition(state, Tokenizer.AFTER_DOCTYPE_SYSTEM_IDENTIFIER, reconsume, pos);
                                continue stateloop;
                            case '>':
                                errGtInSystemId();
                                /*
                                 * Set the DOCTYPE token's force-quirks flag to
                                 * on.
                                 */
                                forceQuirks = true;
                                /*
                                 * Emit that DOCTYPE token.
                                 */
                                systemIdentifier = strBufToString();
                                emitDoctypeToken(pos);
                                /*
                                 * Switch to the data state.
                                 */
                                state = transition(state, Tokenizer.DATA, reconsume, pos);
                                if (shouldSuspend) {
                                    break stateloop;
                                }
                                continue stateloop;
                            case '\r':
                                appendStrBufCarriageReturn();
                                break stateloop;
                            case '\n':
                                appendStrBufLineFeed();
                                continue;
                            case '\u0000':
                                c = '\uFFFD';
                                // CPPONLY: MOZ_FALLTHROUGH;
                            default:
                                /*
                                 * Anything else Append the current input
                                 * character to the current DOCTYPE token's
                                 * system identifier.
                                 */
                                appendStrBuf(c);
                                /*
                                 * Stay in the DOCTYPE system identifier
                                 * (double-quoted) state.
                                 */
                                continue;
                        }
                    }
                    // no fallthrough, reordering opportunity
                case DOCTYPE_PUBLIC_IDENTIFIER_SINGLE_QUOTED:
                    for (;;) {
                        if (++pos == endPos) {
                            break stateloop;
                        }
                        c = checkChar(buf, pos);
                        /*
                         * Consume the next input character:
                         */
                        switch (c) {
                            case '\'':
                                /*
                                 * U+0027 APOSTROPHE (') Switch to the after
                                 * DOCTYPE public identifier state.
                                 */
                                publicIdentifier = strBufToString();
                                state = transition(state, Tokenizer.AFTER_DOCTYPE_PUBLIC_IDENTIFIER, reconsume, pos);
                                continue stateloop;
                            case '>':
                                errGtInPublicId();
                                /*
                                 * Set the DOCTYPE token's force-quirks flag to
                                 * on.
                                 */
                                forceQuirks = true;
                                /*
                                 * Emit that DOCTYPE token.
                                 */
                                publicIdentifier = strBufToString();
                                emitDoctypeToken(pos);
                                /*
                                 * Switch to the data state.
                                 */
                                state = transition(state, Tokenizer.DATA, reconsume, pos);
                                if (shouldSuspend) {
                                    break stateloop;
                                }
                                continue stateloop;
                            case '\r':
                                appendStrBufCarriageReturn();
                                break stateloop;
                            case '\n':
                                appendStrBufLineFeed();
                                continue;
                            case '\u0000':
                                c = '\uFFFD';
                                // CPPONLY: MOZ_FALLTHROUGH;
                            default:
                                /*
                                 * Anything else Append the current input
                                 * character to the current DOCTYPE token's
                                 * public identifier.
                                 */
                                appendStrBuf(c);
                                /*
                                 * Stay in the DOCTYPE public identifier
                                 * (single-quoted) state.
                                 */
                                continue;
                        }
                    }
                    // no fallthrough, reordering opportunity
                case PROCESSING_INSTRUCTION_OPEN:
                    if (++pos == endPos) {
                        break stateloop;
                    }
                    c = checkChar(buf, pos);
                    /*
                     * Consume the next input character:
                     */
                    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                            || c == '_') {
                        /*
                         * ASCII alpha, U+005F LOW LINE (_) Reconsume in the
                         * processing instruction target state.
                         */
                        appendStrBuf(c);
                        state = transition(state, Tokenizer.PROCESSING_INSTRUCTION_TARGET, reconsume, pos);
                        // Reconsuming the appended character is equivalent
                        // to falling through having consumed it.
                    } else {
                        /*
                         * Anything else This is an
                         * invalid-first-character-of-processing-instruction-target
                         * parse error. Convert the temporary buffer to a
                         * comment. Reconsume in the bogus comment state.
                         */
                        errInvalidFirstCharacterOfProcessingInstructionTarget();
                        reconsume = true;
                        state = transition(state, Tokenizer.BOGUS_COMMENT, reconsume, pos);
                        continue stateloop;
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case PROCESSING_INSTRUCTION_TARGET:
                    pitargetloop: for (;;) {
                        if (++pos == endPos) {
                            break stateloop;
                        }
                        c = checkChar(buf, pos);
                        /*
                         * Consume the next input character:
                         */
                        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                                || (c >= '0' && c <= '9') || c == '-'
                                || c == '_') {
                            /*
                             * ASCII alphanumeric, U+002D HYPHEN-MINUS (-),
                             * U+005F LOW LINE (_) Append the current input
                             * character to the temporary buffer.
                             */
                            appendStrBuf(c);
                            continue;
                        }
                        switch (c) {
                            case '\r':
                            case '\n':
                            case '\u000C':
                            case ' ':
                            case '\t':
                            case '?':
                            case '>':
                                /*
                                 * Let target be the concatenation of the code
                                 * points in the temporary buffer.
                                 */
                                if (isDisallowedPiTarget()) {
                                    /*
                                     * If target is an ASCII case-insensitive
                                     * match for "xml" or "xml-stylesheet",
                                     * this is a
                                     * disallowed-processing-instruction-target
                                     * parse error. Convert the temporary
                                     * buffer to a comment. Reconsume in the
                                     * bogus comment state.
                                     */
                                    errDisallowedProcessingInstructionTarget();
                                    reconsume = true;
                                    state = transition(state, Tokenizer.BOGUS_COMMENT, reconsume, pos);
                                    continue stateloop;
                                }
                                /*
                                 * Otherwise, create a processing instruction
                                 * token whose target is target and data is
                                 * the empty string. Reconsume in the after
                                 * processing instruction target state.
                                 */
                                finishPiTarget();
                                reconsume = true;
                                state = transition(state, Tokenizer.AFTER_PROCESSING_INSTRUCTION_TARGET, reconsume, pos);
                                // `break` optimizes; `continue stateloop;` would be valid
                                break pitargetloop;
                            default:
                                /*
                                 * Anything else This is an
                                 * invalid-processing-instruction-target parse
                                 * error. Convert the temporary buffer to a
                                 * comment. Reconsume in the bogus comment
                                 * state.
                                 */
                                errInvalidProcessingInstructionTarget();
                                if (c == '\u0000') {
                                    c = '\uFFFD';
                                }
                                reconsume = true;
                                state = transition(state, Tokenizer.BOGUS_COMMENT, reconsume, pos);
                                continue stateloop;
                        }
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case AFTER_PROCESSING_INSTRUCTION_TARGET:
                    afterpitargetloop: for (;;) {
                        if (reconsume) {
                            reconsume = false;
                        } else {
                            if (++pos == endPos) {
                                break stateloop;
                            }
                            c = checkChar(buf, pos);
                        }
                        /*
                         * Consume the next input character:
                         */
                        switch (c) {
                            case '\r':
                                silentCarriageReturn();
                                break stateloop;
                            case '\n':
                                silentLineFeed();
                                // CPPONLY: MOZ_FALLTHROUGH;
                            case ' ':
                            case '\t':
                            case '\u000C':
                                /*
                                 * U+0009 CHARACTER TABULATION (tab), U+000A
                                 * LINE FEED (LF), U+000C FORM FEED (FF),
                                 * U+0020 SPACE Ignore the character.
                                 */
                                continue;
                            default:
                                /*
                                 * Anything else Reconsume in the processing
                                 * instruction data state.
                                 */
                                reconsume = true;
                                state = transition(state, Tokenizer.PROCESSING_INSTRUCTION_DATA, reconsume, pos);
                                break afterpitargetloop;
                        }
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case PROCESSING_INSTRUCTION_DATA:
                    pidataloop: for (;;) {
                        if (reconsume) {
                            reconsume = false;
                        } else {
                            if (++pos == endPos) {
                                break stateloop;
                            }
                            c = checkChar(buf, pos);
                        }
                        /*
                         * Consume the next input character:
                         */
                        switch (c) {
                            case '?':
                                /*
                                 * U+003F QUESTION MARK (?) Switch to the
                                 * processing instruction questionable state.
                                 */
                                state = transition(state, Tokenizer.PROCESSING_INSTRUCTION_QUESTIONABLE, reconsume, pos);
                                break pidataloop;
                            case '>':
                                /*
                                 * U+003E GREATER-THAN SIGN (>) Switch to the
                                 * data state. Emit the current processing
                                 * instruction token.
                                 */
                                emitProcessingInstruction(pos);
                                state = transition(state, Tokenizer.DATA, reconsume, pos);
                                if (shouldSuspend) {
                                    break stateloop;
                                }
                                continue stateloop;
                            case '\r':
                                appendStrBufCarriageReturn();
                                break stateloop;
                            case '\n':
                                appendStrBufLineFeed();
                                continue;
                            case '\u0000':
                                /*
                                 * The spec appends U+0000 as-is; replacing it
                                 * with U+FFFD matches other engines and the
                                 * fix proposed in
                                 * https://github.com/whatwg/html/issues/13036
                                 */
                                c = '\uFFFD';
                                // CPPONLY: MOZ_FALLTHROUGH;
                            default:
                                /*
                                 * Anything else Append the current input
                                 * character to the current processing
                                 * instruction token's data.
                                 */
                                appendStrBuf(c);
                                continue;
                        }
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case PROCESSING_INSTRUCTION_QUESTIONABLE:
                    for (;;) {
                        if (++pos == endPos) {
                            break stateloop;
                        }
                        c = checkChar(buf, pos);
                        /*
                         * Consume the next input character:
                         */
                        switch (c) {
                            case '>':
                                /*
                                 * U+003E GREATER-THAN SIGN (>) Switch to the
                                 * data state. Emit the current processing
                                 * instruction token.
                                 */
                                emitProcessingInstruction(pos);
                                state = transition(state, Tokenizer.DATA, reconsume, pos);
                                if (shouldSuspend) {
                                    break stateloop;
                                }
                                continue stateloop;
                            default:
                                /*
                                 * Anything else Append U+003F (?) to the
                                 * current processing instruction token's
                                 * data. Reconsume in the processing
                                 * instruction data state.
                                 */
                                appendStrBuf('?');
                                reconsume = true;
                                state = transition(state, Tokenizer.PROCESSING_INSTRUCTION_DATA, reconsume, pos);
                                continue stateloop;
                        }
                    }
                    // no fallthrough, reordering opportunity
                case PROCESSING_INSTRUCTION:
                    processinginstructionloop: for (;;) {
                        if (++pos == endPos) {
                            break stateloop;
                        }
                        c = checkChar(buf, pos);
                        switch (c) {
                            case '?':
                                state = transition(
                                        state,
                                        Tokenizer.PROCESSING_INSTRUCTION_QUESTION_MARK,
                                        reconsume, pos);
                                // `break` optimizes; `continue stateloop;` would be valid
                                break processinginstructionloop;
                            default:
                                continue;
                        }
                    }
                    // CPPONLY: MOZ_FALLTHROUGH;
                case PROCESSING_INSTRUCTION_QUESTION_MARK:
                    if (++pos == endPos) {
                        break stateloop;
                    }
                    c = checkChar(buf, pos);
                    switch (c) {
                        case '>':
                            state = transition(state, Tokenizer.DATA,
                                    reconsume, pos);
                            // Processing instruction syntax goes through these
                            // states only in Gecko's XML View Source--not in HTML
                            // parsing in Java or in Gecko.
                            // Since XML View Source doesn't use the
                            // suspension-after-current-token facility, its extension
                            // to processing-instruction states is strictly unnecessary
                            // at the moment. However, if these states ever were to be
                            // used together with the suspension-after-current-token
                            // facility, these states would need to participate, since
                            // suspension could be requested when only less-than has been
                            // seen and we don't yet know if we end up here. Handling
                            // the currently-unnecessary case in order to avoid leaving
                            // a trap for future modification.
                            suspendIfRequestedAfterCurrentNonTextToken();
                            if (shouldSuspend) {
                                break stateloop;
                            }
                            continue stateloop;
                        default:
                            state = transition(state,
                                    Tokenizer.PROCESSING_INSTRUCTION,
                                    reconsume, pos);
                            continue stateloop;
                    }
                    // END HOTSPOT WORKAROUND
            }
        }
        flushChars(buf, pos);
        /*
         * if (prevCR && pos != endPos) { // why is this needed? pos--; col--; }
         */
        // Save locals
        stateSave = state;
        returnStateSave = returnState;
        return pos;
    }

    // HOTSPOT WORKAROUND INSERTION POINT

    // [NOCPP[

    protected int transition(int from, int to, boolean reconsume, int pos) throws SAXException {
        return to;
    }

    // ]NOCPP]

    private void initDoctypeFields() {
        // Discard the characters "DOCTYPE" accumulated as a potential bogus
        // comment into strBuf.
        clearStrBufAfterUse();
        doctypeName = null;
        if (systemIdentifier != null) {
            Portability.releaseString(systemIdentifier);
            systemIdentifier = null;
        }
        if (publicIdentifier != null) {
            Portability.releaseString(publicIdentifier);
            publicIdentifier = null;
        }
        forceQuirks = false;
    }

    @Inline private void adjustDoubleHyphenAndAppendToStrBufCarriageReturn()
            throws SAXException {
        silentCarriageReturn();
        adjustDoubleHyphenAndAppendToStrBufAndErr('\n', false);
    }

    @Inline private void adjustDoubleHyphenAndAppendToStrBufLineFeed()
            throws SAXException {
        silentLineFeed();
        adjustDoubleHyphenAndAppendToStrBufAndErr('\n', false);
    }

    @Inline private void appendStrBufLineFeed() {
        silentLineFeed();
        appendStrBuf('\n');
    }

    @Inline private void appendStrBufCarriageReturn() {
        silentCarriageReturn();
        appendStrBuf('\n');
    }

    // [NOCPP[

    @Inline protected void silentCarriageReturn() {
        ++line;
        lastCR = true;
    }

    @Inline protected void silentLineFeed() {
        ++line;
    }

    // ]NOCPP]

    @Inline private void emitCarriageReturn(@NoLength char[] buf, int pos)
            throws SAXException {
        silentCarriageReturn();
        flushChars(buf, pos);
        tokenHandler.characters(Tokenizer.LF, 0, 1);
        cstart = Integer.MAX_VALUE;
    }

    private void emitReplacementCharacter(@NoLength char[] buf, int pos)
            throws SAXException {
        flushChars(buf, pos);
        tokenHandler.zeroOriginatingReplacementCharacter();
        cstart = pos + 1;
    }

    private void maybeEmitReplacementCharacter(@NoLength char[] buf, int pos)
            throws SAXException {
        flushChars(buf, pos);
        tokenHandler.zeroOrReplacementCharacter();
        cstart = pos + 1;
    }

    private void emitPlaintextReplacementCharacter(@NoLength char[] buf, int pos)
            throws SAXException {
        flushChars(buf, pos);
        tokenHandler.characters(REPLACEMENT_CHARACTER, 0, 1);
        cstart = pos + 1;
    }

    @Inline private void setAdditionalAndRememberAmpersandLocation(char add) {
        additional = add;
        // [NOCPP[
        ampersandLocation = new LocatorImpl(this);
        // ]NOCPP]
    }

    private void bogusDoctype() throws SAXException {
        errBogusDoctype();
        forceQuirks = true;
    }

    private void bogusDoctypeWithoutQuirks() throws SAXException {
        errBogusDoctype();
        forceQuirks = false;
    }

    private void handleNcrValue(int returnState) throws SAXException {
        /*
         * If one or more characters match the range, then take them all and
         * interpret the string of characters as a number (either hexadecimal or
         * decimal as appropriate).
         */
        if (value <= 0xFFFF) {
            if (value >= 0x80 && value <= 0x9f) {
                /*
                 * If that number is one of the numbers in the first column of
                 * the following table, then this is a parse error.
                 */
                errNcrInC1Range();
                /*
                 * Find the row with that number in the first column, and return
                 * a character token for the Unicode character given in the
                 * second column of that row.
                 */
                @NoLength char[] val = NamedCharacters.WINDOWS_1252[value - 0x80];
                emitOrAppendOne(val, returnState);
                // [NOCPP[
            } else if (value == 0xC
                    && contentSpacePolicy != XmlViolationPolicy.ALLOW) {
                if (contentSpacePolicy == XmlViolationPolicy.ALTER_INFOSET) {
                    emitOrAppendOne(Tokenizer.SPACE, returnState);
                } else if (contentSpacePolicy == XmlViolationPolicy.FATAL) {
                    fatal("A character reference expanded to a form feed which is not legal XML 1.0 white space.");
                }
                // ]NOCPP]
            } else if (value == 0x0) {
                errNcrZero();
                emitOrAppendOne(Tokenizer.REPLACEMENT_CHARACTER, returnState);
            } else if ((value & 0xF800) == 0xD800) {
                errNcrSurrogate();
                emitOrAppendOne(Tokenizer.REPLACEMENT_CHARACTER, returnState);
            } else {
                /*
                 * Otherwise, return a character token for the Unicode character
                 * whose code point is that number.
                 */
                char ch = (char) value;
                // [NOCPP[
                if (value == 0x0D) {
                    errNcrCr();
                } else if ((value <= 0x0008) || (value == 0x000B)
                        || (value >= 0x000E && value <= 0x001F)) {
                    ch = errNcrControlChar(ch);
                } else if (value >= 0xFDD0 && value <= 0xFDEF) {
                    errNcrUnassigned();
                } else if ((value & 0xFFFE) == 0xFFFE) {
                    ch = errNcrNonCharacter(ch);
                } else if (value >= 0x007F && value <= 0x009F) {
                    errNcrControlChar();
                } else {
                    maybeWarnPrivateUse(ch);
                }
                // ]NOCPP]
                bmpChar[0] = ch;
                emitOrAppendOne(bmpChar, returnState);
            }
        } else if (value <= 0x10FFFF) {
            // [NOCPP[
            maybeWarnPrivateUseAstral();
            if ((value & 0xFFFE) == 0xFFFE) {
                errAstralNonCharacter(value);
            }
            // ]NOCPP]
            astralChar[0] = (char) (Tokenizer.LEAD_OFFSET + (value >> 10));
            astralChar[1] = (char) (0xDC00 + (value & 0x3FF));
            emitOrAppendTwo(astralChar, returnState);
        } else {
            errNcrOutOfRange();
            emitOrAppendOne(Tokenizer.REPLACEMENT_CHARACTER, returnState);
        }
    }

    public void eof() throws SAXException {
        int state = stateSave;
        int returnState = returnStateSave;

        eofloop: for (;;) {
            switch (state) {
                case SCRIPT_DATA_LESS_THAN_SIGN:
                case SCRIPT_DATA_ESCAPED_LESS_THAN_SIGN:
                    /*
                     * Otherwise, emit a U+003C LESS-THAN SIGN character token
                     */
                    tokenHandler.characters(Tokenizer.LT_GT, 0, 1);
                    /*
                     * and reconsume the current input character in the data
                     * state.
                     */
                    break eofloop;
                case TAG_OPEN:
                    /*
                     * The behavior of this state depends on the content model
                     * flag.
                     */
                    /*
                     * Anything else Parse error.
                     */
                    errEofAfterLt();
                    /*
                     * Emit a U+003C LESS-THAN SIGN character token
                     */
                    tokenHandler.characters(Tokenizer.LT_GT, 0, 1);
                    /*
                     * and reconsume the current input character in the data
                     * state.
                     */
                    break eofloop;
                case RAWTEXT_RCDATA_LESS_THAN_SIGN:
                    /*
                     * Emit a U+003C LESS-THAN SIGN character token
                     */
                    tokenHandler.characters(Tokenizer.LT_GT, 0, 1);
                    /*
                     * and reconsume the current input character in the RCDATA
                     * state.
                     */
                    break eofloop;
                case NON_DATA_END_TAG_NAME:
                    /*
                     * Emit a U+003C LESS-THAN SIGN character token, a U+002F
                     * SOLIDUS character token,
                     */
                    tokenHandler.characters(Tokenizer.LT_SOLIDUS, 0, 2);
                    /*
                     * a character token for each of the characters in the
                     * temporary buffer (in the order they were added to the
                     * buffer),
                     */
                    emitStrBuf();
                    /*
                     * and reconsume the current input character in the RCDATA
                     * state.
                     */
                    break eofloop;
                case CLOSE_TAG_OPEN:
                    /* EOF Parse error. */
                    errEofAfterLt();
                    /*
                     * Emit a U+003C LESS-THAN SIGN character token and a U+002F
                     * SOLIDUS character token.
                     */
                    tokenHandler.characters(Tokenizer.LT_SOLIDUS, 0, 2);
                    /*
                     * Reconsume the EOF character in the data state.
                     */
                    break eofloop;
                case TAG_NAME:
                    /*
                     * EOF Parse error.
                     */
                    errEofInTagName();
                    /*
                     * Reconsume the EOF character in the data state.
                     */
                    break eofloop;
                case BEFORE_ATTRIBUTE_NAME:
                case AFTER_ATTRIBUTE_VALUE_QUOTED:
                case SELF_CLOSING_START_TAG:
                    /* EOF Parse error. */
                    errEofWithoutGt();
                    /*
                     * Reconsume the EOF character in the data state.
                     */
                    break eofloop;
                case ATTRIBUTE_NAME:
                    /*
                     * EOF Parse error.
                     */
                    errEofInAttributeName();
                    /*
                     * Reconsume the EOF character in the data state.
                     */
                    break eofloop;
                case AFTER_ATTRIBUTE_NAME:
                case BEFORE_ATTRIBUTE_VALUE:
                    /* EOF Parse error. */
                    errEofWithoutGt();
                    /*
                     * Reconsume the EOF character in the data state.
                     */
                    break eofloop;
                case ATTRIBUTE_VALUE_DOUBLE_QUOTED:
                case ATTRIBUTE_VALUE_SINGLE_QUOTED:
                case ATTRIBUTE_VALUE_UNQUOTED:
                    /* EOF Parse error. */
                    errEofInAttributeValue();
                    /*
                     * Reconsume the EOF character in the data state.
                     */
                    break eofloop;
                case PROCESSING_INSTRUCTION_OPEN:
                case PROCESSING_INSTRUCTION_TARGET:
                case AFTER_PROCESSING_INSTRUCTION_TARGET:
                case PROCESSING_INSTRUCTION_DATA:
                case PROCESSING_INSTRUCTION_QUESTIONABLE:
                    /*
                     * EOF This is an eof-in-processing-instruction parse
                     * error. Emit an end-of-file token.
                     */
                    errEofInProcessingInstruction();
                    if (piTarget != null) {
                        Portability.releaseString(piTarget);
                        piTarget = null;
                    }
                    break eofloop;
                case BOGUS_COMMENT:
                    emitComment(0, 0);
                    break eofloop;
                case BOGUS_COMMENT_HYPHEN:
                    // [NOCPP[
                    maybeAppendSpaceToBogusComment();
                    // ]NOCPP]
                    emitComment(0, 0);
                    break eofloop;
                case MARKUP_DECLARATION_OPEN:
                    errBogusComment();
                    emitComment(0, 0);
                    break eofloop;
                case MARKUP_DECLARATION_HYPHEN:
                    errBogusComment();
                    emitComment(0, 0);
                    break eofloop;
                case MARKUP_DECLARATION_OCTYPE:
                    if (index < 6) {
                        errBogusComment();
                        emitComment(0, 0);
                    } else {
                        /* EOF Parse error. */
                        errEofInDoctype();
                        /*
                         * Create a new DOCTYPE token. Set its force-quirks flag
                         * to on.
                         */
                        doctypeName = null;
                        if (systemIdentifier != null) {
                            Portability.releaseString(systemIdentifier);
                            systemIdentifier = null;
                        }
                        if (publicIdentifier != null) {
                            Portability.releaseString(publicIdentifier);
                            publicIdentifier = null;
                        }
                        forceQuirks = true;
                        /*
                         * Emit the token.
                         */
                        emitDoctypeToken(0);
                        /*
                         * Reconsume the EOF character in the data state.
                         */
                        break eofloop;
                    }
                    break eofloop;
                case COMMENT_START:
                case COMMENT:
                case COMMENT_LESSTHAN:
                case COMMENT_LESSTHAN_BANG:
                    /*
                     * EOF Parse error.
                     */
                    errEofInComment();
                    /* Emit the comment token. */
                    emitComment(0, 0);
                    /*
                     * Reconsume the EOF character in the data state.
                     */
                    break eofloop;
                case COMMENT_END:
                case COMMENT_LESSTHAN_BANG_DASH_DASH:
                    errEofInComment();
                    /* Emit the comment token. */
                    emitComment(2, 0);
                    /*
                     * Reconsume the EOF character in the data state.
                     */
                    break eofloop;
                case COMMENT_END_DASH:
                case COMMENT_START_DASH:
                case COMMENT_LESSTHAN_BANG_DASH:
                    errEofInComment();
                    /* Emit the comment token. */
                    emitComment(1, 0);
                    /*
                     * Reconsume the EOF character in the data state.
                     */
                    break eofloop;
                case COMMENT_END_BANG:
                    errEofInComment();
                    /* Emit the comment token. */
                    emitComment(3, 0);
                    /*
                     * Reconsume the EOF character in the data state.
                     */
                    break eofloop;
                case DOCTYPE:
                case BEFORE_DOCTYPE_NAME:
                    errEofInDoctype();
                    /*
                     * Create a new DOCTYPE token. Set its force-quirks flag to
                     * on.
                     */
                    forceQuirks = true;
                    /*
                     * Emit the token.
                     */
                    emitDoctypeToken(0);
                    /*
                     * Reconsume the EOF character in the data state.
                     */
                    break eofloop;
                case DOCTYPE_NAME:
                    errEofInDoctype();
                    strBufToDoctypeName();
                    /*
                     * Set the DOCTYPE token's force-quirks flag to on.
                     */
                    forceQuirks = true;
                    /*
                     * Emit that DOCTYPE token.
                     */
                    emitDoctypeToken(0);
                    /*
                     * Reconsume the EOF character in the data state.
                     */
                    break eofloop;
                case DOCTYPE_UBLIC:
                case DOCTYPE_YSTEM:
                case AFTER_DOCTYPE_NAME:
                case AFTER_DOCTYPE_PUBLIC_KEYWORD:
                case AFTER_DOCTYPE_SYSTEM_KEYWORD:
                case BEFORE_DOCTYPE_PUBLIC_IDENTIFIER:
                    errEofInDoctype();
                    /*
                     * Set the DOCTYPE token's force-quirks flag to on.
                     */
                    forceQuirks = true;
                    /*
                     * Emit that DOCTYPE token.
                     */
                    emitDoctypeToken(0);
                    /*
                     * Reconsume the EOF character in the data state.
                     */
                    break eofloop;
                case DOCTYPE_PUBLIC_IDENTIFIER_DOUBLE_QUOTED:
                case DOCTYPE_PUBLIC_IDENTIFIER_SINGLE_QUOTED:
                    /* EOF Parse error. */
                    errEofInPublicId();
                    /*
                     * Set the DOCTYPE token's force-quirks flag to on.
                     */
                    forceQuirks = true;
                    /*
                     * Emit that DOCTYPE token.
                     */
                    publicIdentifier = strBufToString();
                    emitDoctypeToken(0);
                    /*
                     * Reconsume the EOF character in the data state.
                     */
                    break eofloop;
                case AFTER_DOCTYPE_PUBLIC_IDENTIFIER:
                case BEFORE_DOCTYPE_SYSTEM_IDENTIFIER:
                case BETWEEN_DOCTYPE_PUBLIC_AND_SYSTEM_IDENTIFIERS:
                    errEofInDoctype();
                    /*
                     * Set the DOCTYPE token's force-quirks flag to on.
                     */
                    forceQuirks = true;
                    /*
                     * Emit that DOCTYPE token.
                     */
                    emitDoctypeToken(0);
                    /*
                     * Reconsume the EOF character in the data state.
                     */
                    break eofloop;
                case DOCTYPE_SYSTEM_IDENTIFIER_DOUBLE_QUOTED:
                case DOCTYPE_SYSTEM_IDENTIFIER_SINGLE_QUOTED:
                    /* EOF Parse error. */
                    errEofInSystemId();
                    /*
                     * Set the DOCTYPE token's force-quirks flag to on.
                     */
                    forceQuirks = true;
                    /*
                     * Emit that DOCTYPE token.
                     */
                    systemIdentifier = strBufToString();
                    emitDoctypeToken(0);
                    /*
                     * Reconsume the EOF character in the data state.
                     */
                    break eofloop;
                case AFTER_DOCTYPE_SYSTEM_IDENTIFIER:
                    errEofInDoctype();
                    /*
                     * Set the DOCTYPE token's force-quirks flag to on.
                     */
                    forceQuirks = true;
                    /*
                     * Emit that DOCTYPE token.
                     */
                    emitDoctypeToken(0);
                    /*
                     * Reconsume the EOF character in the data state.
                     */
                    break eofloop;
                case BOGUS_DOCTYPE:
                    /*
                     * Emit that DOCTYPE token.
                     */
                    emitDoctypeToken(0);
                    /*
                     * Reconsume the EOF character in the data state.
                     */
                    break eofloop;
                case CONSUME_CHARACTER_REFERENCE:
                    /*
                     * Unlike the definition is the spec, this state does not
                     * return a value and never requires the caller to
                     * backtrack. This state takes care of emitting characters
                     * or appending to the current attribute value. It also
                     * takes care of that in the case when consuming the entity
                     * fails.
                     */
                    /*
                     * This section defines how to consume an entity. This
                     * definition is used when parsing entities in text and in
                     * attributes.
                     *
                     * The behavior depends on the identity of the next
                     * character (the one immediately after the U+0026 AMPERSAND
                     * character):
                     */

                    emitOrAppendCharRefBuf(returnState);
                    state = returnState;
                    continue;
                case CHARACTER_REFERENCE_HILO_LOOKUP:
                    emitOrAppendCharRefBuf(returnState);
                    state = returnState;
                    continue;
                case CHARACTER_REFERENCE_TAIL:
                    outer: for (;;) {
                        char c = '\u0000';
                        entCol++;
                        /*
                         * Consume the maximum number of characters possible,
                         * with the consumed characters matching one of the
                         * identifiers in the first column of the named
                         * character references table (in a case-sensitive
                         * manner).
                         */
                        hiloop: for (;;) {
                            if (hi == -1) {
                                break hiloop;
                            }
                            if (entCol == NamedCharacters.NAMES[hi].length()) {
                                break hiloop;
                            }
                            if (entCol > NamedCharacters.NAMES[hi].length()) {
                                break outer;
                            } else if (c < NamedCharacters.NAMES[hi].charAt(entCol)) {
                                hi--;
                            } else {
                                break hiloop;
                            }
                        }

                        loloop: for (;;) {
                            if (hi < lo) {
                                break outer;
                            }
                            if (entCol == NamedCharacters.NAMES[lo].length()) {
                                candidate = lo;
                                charRefBufMark = charRefBufLen;
                                lo++;
                            } else if (entCol > NamedCharacters.NAMES[lo].length()) {
                                break outer;
                            } else if (c > NamedCharacters.NAMES[lo].charAt(entCol)) {
                                lo++;
                            } else {
                                break loloop;
                            }
                        }
                        if (hi < lo) {
                            break outer;
                        }
                        continue;
                    }

                    if (candidate == -1) {
                        emitOrAppendCharRefBuf(returnState);
                        state = returnState;
                        continue eofloop;
                    } else {
                        @Const @CharacterName String candidateName = NamedCharacters.NAMES[candidate];
                        if (candidateName.length() == 0
                                || candidateName.charAt(candidateName.length() - 1) != ';') {
                            /*
                             * If the last character matched is not a U+003B
                             * SEMICOLON (;), there is a parse error.
                             */
                            if ((returnState & DATA_AND_RCDATA_MASK) != 0) {
                                /*
                                 * If the entity is being consumed as part of an
                                 * attribute, and the last character matched is
                                 * not a U+003B SEMICOLON (;),
                                 */
                                char ch;
                                if (charRefBufMark == charRefBufLen) {
                                    ch = '\u0000';
                                } else {
                                    ch = charRefBuf[charRefBufMark];
                                }
                                if ((ch >= '0' && ch <= '9')
                                        || (ch >= 'A' && ch <= 'Z')
                                        || (ch >= 'a' && ch <= 'z')) {
                                    /*
                                     * and the next character is in the range
                                     * U+0030 DIGIT ZERO to U+0039 DIGIT NINE,
                                     * U+0041 LATIN CAPITAL LETTER A to U+005A
                                     * LATIN CAPITAL LETTER Z, or U+0061 LATIN
                                     * SMALL LETTER A to U+007A LATIN SMALL
                                     * LETTER Z, then, for historical reasons,
                                     * all the characters that were matched
                                     * after the U+0026 AMPERSAND (&) must be
                                     * unconsumed, and nothing is returned.
                                     */
                                    appendCharRefBufToStrBuf();
                                    state = returnState;
                                    continue eofloop;
                                }
                            }
                            if ((returnState & DATA_AND_RCDATA_MASK) != 0) {
                                errUnescapedAmpersandInterpretedAsCharacterReference();
                            } else {
                                errNotSemicolonTerminated();
                            }
                        }

                        /*
                         * Otherwise, return a character token for the character
                         * corresponding to the entity name (as given by the
                         * second column of the named character references
                         * table).
                         */
                        @Const @NoLength char[] val = NamedCharacters.VALUES[candidate];
                        if (
                        // [NOCPP[
                        val.length == 1
                        // ]NOCPP]
                        // CPPONLY: val[1] == 0
                        ) {
                            emitOrAppendOne(val, returnState);
                        } else {
                            emitOrAppendTwo(val, returnState);
                        }
                        // this is so complicated!
                        if (charRefBufMark < charRefBufLen) {
                            if ((returnState & DATA_AND_RCDATA_MASK) != 0) {
                                appendStrBuf(charRefBuf, charRefBufMark,
                                        charRefBufLen - charRefBufMark);
                            } else {
                                tokenHandler.characters(charRefBuf, charRefBufMark,
                                        charRefBufLen - charRefBufMark);
                            }
                        }
                        charRefBufLen = 0;
                        state = returnState;
                        continue eofloop;
                        /*
                         * If the markup contains I'm &notit; I tell you, the
                         * entity is parsed as "not", as in, I'm ¬it; I tell
                         * you. But if the markup was I'm &notin; I tell you,
                         * the entity would be parsed as "notin;", resulting in
                         * I'm ∉ I tell you.
                         */
                    }
                case CONSUME_NCR:
                case DECIMAL_NRC_LOOP:
                case HEX_NCR_LOOP:
                    /*
                     * If no characters match the range, then don't consume any
                     * characters (and unconsume the U+0023 NUMBER SIGN
                     * character and, if appropriate, the X character). This is
                     * a parse error; nothing is returned.
                     *
                     * Otherwise, if the next character is a U+003B SEMICOLON,
                     * consume that too. If it isn't, there is a parse error.
                     */
                    if (!seenDigits) {
                        errNoDigitsInNCR();
                        emitOrAppendCharRefBuf(returnState);
                        state = returnState;
                        continue;
                    } else {
                        errCharRefLacksSemicolon();
                    }
                    // WARNING previous state sets reconsume
                    handleNcrValue(returnState);
                    state = returnState;
                    continue;
                case CDATA_RSQB:
                    tokenHandler.characters(Tokenizer.RSQB_RSQB, 0, 1);
                    break eofloop;
                case CDATA_RSQB_RSQB:
                    tokenHandler.characters(Tokenizer.RSQB_RSQB, 0, 2);
                    break eofloop;
                case DATA:
                default:
                    break eofloop;
            }
        }
        // case DATA:
        /*
         * EOF Emit an end-of-file token.
         */
        tokenHandler.eof();
        return;
    }

    /**
     * Emits a doctype token.
     *
     * NOTE: The method may set <code>shouldSuspend</code>, so the caller
     * must have this pattern after the state's <code>transition</code> call:
     * <pre>
     * if (shouldSuspend) {
     *     break stateloop;
     * }
     * continue stateloop;
     * </pre>
     *
     * @param pos
     * @throws SAXException
     */
    private void emitDoctypeToken(int pos) throws SAXException {
        // CPPONLY: RememberGt(pos);
        cstart = pos + 1;
        tokenHandler.doctype(doctypeName, publicIdentifier, systemIdentifier,
                forceQuirks);
        // It is OK and sufficient to release these here, since
        // there's no way out of the doctype states than through paths
        // that call this method.
        doctypeName = null;
        Portability.releaseString(publicIdentifier);
        publicIdentifier = null;
        Portability.releaseString(systemIdentifier);
        systemIdentifier = null;
        suspendIfRequestedAfterCurrentNonTextToken();
    }

    /**
     * If a previous call to <code>suspendAfterCurrentTokenIfNotInText()</code>
     * happened in a non-text context, this method turns that deferred suspension
     * request into an immediately-pending suspension request.
     */
    @Inline private void suspendIfRequestedAfterCurrentNonTextToken() {
        if (suspendAfterCurrentNonTextToken) {
            suspendAfterCurrentNonTextToken = false;
            shouldSuspend = true;
        }
    }

    // Making this private until the full Java implementation is done.
    /**
     * Request suspension after the current token if the tokenizer is currently
     * in a non-text state (i.e. it's known that the next token will be a
     * non-text token).
     *
     * Must not be called when <code>tokenizeBuffer()</code> is on the call
     * stack.
     */
    @SuppressWarnings("unused") private void suspendAfterCurrentTokenIfNotInText() {
        switch (stateSave) {
            case DATA:
            case RCDATA:
            case SCRIPT_DATA:
            case RAWTEXT:
            case SCRIPT_DATA_ESCAPED:
            case PLAINTEXT:
            case NON_DATA_END_TAG_NAME: // We haven't yet committed to the next
                                        // token being a non-text token, though
                                        // it could be.
            case SCRIPT_DATA_LESS_THAN_SIGN:
            case SCRIPT_DATA_ESCAPE_START:
            case SCRIPT_DATA_ESCAPE_START_DASH:
            case SCRIPT_DATA_ESCAPED_DASH:
            case SCRIPT_DATA_ESCAPED_DASH_DASH:
            case RAWTEXT_RCDATA_LESS_THAN_SIGN:
            case SCRIPT_DATA_ESCAPED_LESS_THAN_SIGN:
            case SCRIPT_DATA_DOUBLE_ESCAPE_START:
            case SCRIPT_DATA_DOUBLE_ESCAPED:
            case SCRIPT_DATA_DOUBLE_ESCAPED_LESS_THAN_SIGN:
            case SCRIPT_DATA_DOUBLE_ESCAPED_DASH:
            case SCRIPT_DATA_DOUBLE_ESCAPED_DASH_DASH:
            case SCRIPT_DATA_DOUBLE_ESCAPE_END:
                return;
            case TAG_NAME:
            case BEFORE_ATTRIBUTE_NAME:
            case ATTRIBUTE_NAME:
            case AFTER_ATTRIBUTE_NAME:
            case BEFORE_ATTRIBUTE_VALUE:
            case AFTER_ATTRIBUTE_VALUE_QUOTED:
            case BOGUS_COMMENT:
            case MARKUP_DECLARATION_OPEN:
            case DOCTYPE:
            case BEFORE_DOCTYPE_NAME:
            case DOCTYPE_NAME:
            case AFTER_DOCTYPE_NAME:
            case BEFORE_DOCTYPE_PUBLIC_IDENTIFIER:
            case DOCTYPE_PUBLIC_IDENTIFIER_DOUBLE_QUOTED:
            case DOCTYPE_PUBLIC_IDENTIFIER_SINGLE_QUOTED:
            case AFTER_DOCTYPE_PUBLIC_IDENTIFIER:
            case BEFORE_DOCTYPE_SYSTEM_IDENTIFIER:
            case DOCTYPE_SYSTEM_IDENTIFIER_DOUBLE_QUOTED:
            case DOCTYPE_SYSTEM_IDENTIFIER_SINGLE_QUOTED:
            case AFTER_DOCTYPE_SYSTEM_IDENTIFIER:
            case BOGUS_DOCTYPE:
            case COMMENT_START:
            case COMMENT_START_DASH:
            case COMMENT:
            case COMMENT_END_DASH:
            case COMMENT_END:
            case COMMENT_END_BANG:
            case TAG_OPEN:
            case CLOSE_TAG_OPEN:
            case MARKUP_DECLARATION_HYPHEN:
            case MARKUP_DECLARATION_OCTYPE:
            case DOCTYPE_UBLIC:
            case DOCTYPE_YSTEM:
            case AFTER_DOCTYPE_PUBLIC_KEYWORD:
            case BETWEEN_DOCTYPE_PUBLIC_AND_SYSTEM_IDENTIFIERS:
            case AFTER_DOCTYPE_SYSTEM_KEYWORD:
            case SELF_CLOSING_START_TAG:
            case ATTRIBUTE_VALUE_DOUBLE_QUOTED:
            case ATTRIBUTE_VALUE_SINGLE_QUOTED:
            case ATTRIBUTE_VALUE_UNQUOTED:
            case BOGUS_COMMENT_HYPHEN:
            case COMMENT_LESSTHAN:
            case COMMENT_LESSTHAN_BANG:
            case COMMENT_LESSTHAN_BANG_DASH:
            case COMMENT_LESSTHAN_BANG_DASH_DASH:
            case CDATA_START:
            case CDATA_SECTION:
            case CDATA_RSQB:
            case CDATA_RSQB_RSQB:
            case PROCESSING_INSTRUCTION:
            case PROCESSING_INSTRUCTION_QUESTION_MARK:
            case PROCESSING_INSTRUCTION_OPEN:
            case PROCESSING_INSTRUCTION_TARGET:
            case AFTER_PROCESSING_INSTRUCTION_TARGET:
            case PROCESSING_INSTRUCTION_DATA:
            case PROCESSING_INSTRUCTION_QUESTIONABLE:
                break;
            case CONSUME_CHARACTER_REFERENCE:
            case CONSUME_NCR:
            case CHARACTER_REFERENCE_TAIL:
            case HEX_NCR_LOOP:
            case DECIMAL_NRC_LOOP:
            case HANDLE_NCR_VALUE:
            case HANDLE_NCR_VALUE_RECONSUME:
            case CHARACTER_REFERENCE_HILO_LOOKUP:
                if (returnStateSave == DATA || returnStateSave == RCDATA) {
                    return;
                }
                break;
            default:
                assert false : "Incomplete switch";
                return;
        }
        suspendAfterCurrentNonTextToken = true;
    }

    // Making this private until the full Java implementation is done.
    /**
     * Queries if we are about to suspend after the current non-text token due to a request
     * from <code>suspendAfterCurrentTokenIfNotInText()</code>.
     * @return <code>true</code> iff <code>suspendAfterCurrentTokenIfNotInText()</code> was
     * called in a non-text position and the then-current token has not been emitted yet.
     */
    @SuppressWarnings("unused") private boolean suspensionAfterCurrentNonTextTokenPending() {
        return suspendAfterCurrentNonTextToken;
    }

    // [NOCPP[

    @Inline protected char checkChar(@NoLength char[] buf, int pos)
            throws SAXException {
        return buf[pos];
    }

    // ]NOCPP]

    public boolean internalEncodingDeclaration(String internalCharset)
            throws SAXException {
        if (encodingDeclarationHandler != null) {
            return encodingDeclarationHandler.internalEncodingDeclaration(internalCharset);
        }
        return false;
    }

    /**
     * @param val
     * @throws SAXException
     */
    @Inline private void emitOrAppendTwo(@Const @NoLength char[] val, int returnState)
            throws SAXException {
        if ((returnState & DATA_AND_RCDATA_MASK) != 0) {
            appendStrBuf(val[0]);
            appendStrBuf(val[1]);
        } else {
            tokenHandler.characters(val, 0, 2);
        }
    }

    @Inline private void emitOrAppendOne(@Const @NoLength char[] val, int returnState)
            throws SAXException {
        if ((returnState & DATA_AND_RCDATA_MASK) != 0) {
            appendStrBuf(val[0]);
        } else {
            tokenHandler.characters(val, 0, 1);
        }
    }

    public void end() throws SAXException {
        if (!keepBuffer) {
            strBuf = null;
        }
        doctypeName = null;
        if (systemIdentifier != null) {
            Portability.releaseString(systemIdentifier);
            systemIdentifier = null;
        }
        if (publicIdentifier != null) {
            Portability.releaseString(publicIdentifier);
            publicIdentifier = null;
        }
        if (piTarget != null) {
            Portability.releaseString(piTarget);
            piTarget = null;
        }
        tagName = null;
        nonInternedTagName.setNameForNonInterned(null
                // CPPONLY: , false
                );
        attributeName = null;
        // CPPONLY: nonInternedAttributeName.setNameForNonInterned(null);
        tokenHandler.endTokenization();
        if (attributes != null) {
            // [NOCPP[
            attributes = null;
            // ]NOCPP]
            // CPPONLY: attributes.clear(mappingLangToXmlLang);
        }
    }

    @Inline public void requestSuspension() {
        shouldSuspend = true;
    }

    // [NOCPP[

    public void becomeConfident() {
        confident = true;
    }

    /**
     * Returns the nextCharOnNewLine.
     *
     * @return the nextCharOnNewLine
     */
    public boolean isNextCharOnNewLine() {
        return false;
    }

    public boolean isPrevCR() {
        return lastCR;
    }

    /**
     * Returns the line.
     *
     * @return the line
     */
    public int getLine() {
        return -1;
    }

    /**
     * Returns the col.
     *
     * @return the col
     */
    public int getCol() {
        return -1;
    }

    // ]NOCPP]

    @Inline public boolean isInDataState() {
        return (stateSave == DATA);
    }

    public void resetToDataState() {
        clearStrBufAfterUse();
        if (piTarget != null) {
            Portability.releaseString(piTarget);
            piTarget = null;
        }
        charRefBufLen = 0;
        stateSave = Tokenizer.DATA;
        // line = 1; XXX line numbers
        lastCR = false;
        index = 0;
        forceQuirks = false;
        additional = '\u0000';
        entCol = -1;
        firstCharKey = -1;
        lo = 0;
        hi = 0; // will always be overwritten before use anyway
        candidate = -1;
        charRefBufMark = 0;
        value = 0;
        seenDigits = false;
        suspendAfterCurrentNonTextToken = false;
        endTag = false;
        shouldSuspend = false;
        initDoctypeFields();
        containsHyphen = false;
        tagName = null;
        attributeName = null;
        if (newAttributesEachTime) {
            if (attributes != null) {
                Portability.delete(attributes);
                attributes = null;
            }
        }
    }

    public void loadState(Tokenizer other) throws SAXException {
        strBufLen = other.strBufLen;
        if (strBufLen > strBuf.length) {
            strBuf = new char[strBufLen];
        }
        System.arraycopy(other.strBuf, 0, strBuf, 0, strBufLen);

        charRefBufLen = other.charRefBufLen;
        System.arraycopy(other.charRefBuf, 0, charRefBuf, 0, charRefBufLen);

        stateSave = other.stateSave;
        returnStateSave = other.returnStateSave;
        endTagExpectation = other.endTagExpectation;
        endTagExpectationAsArray = other.endTagExpectationAsArray;
        // line = 1; XXX line numbers
        lastCR = other.lastCR;
        index = other.index;
        forceQuirks = other.forceQuirks;
        additional = other.additional;
        entCol = other.entCol;
        firstCharKey = other.firstCharKey;
        lo = other.lo;
        hi = other.hi;
        candidate = other.candidate;
        charRefBufMark = other.charRefBufMark;
        value = other.value;
        seenDigits = other.seenDigits;
        endTag = other.endTag;
        shouldSuspend = false;
        suspendAfterCurrentNonTextToken = false;
        doctypeName = other.doctypeName;

        Portability.releaseString(systemIdentifier);
        if (other.systemIdentifier == null) {
            systemIdentifier = null;
        } else {
            systemIdentifier = Portability.newStringFromString(other.systemIdentifier);
        }

        Portability.releaseString(publicIdentifier);
        if (other.publicIdentifier == null) {
            publicIdentifier = null;
        } else {
            publicIdentifier = Portability.newStringFromString(other.publicIdentifier);
        }

        Portability.releaseString(piTarget);
        if (other.piTarget == null) {
            piTarget = null;
        } else {
            piTarget = Portability.newStringFromString(other.piTarget);
        }
        processingInstructionsEnabled = other.processingInstructionsEnabled;

        containsHyphen = other.containsHyphen;
        if (other.tagName == null) {
            tagName = null;
        } else if (other.tagName.isInterned()) {
            tagName = other.tagName;
        } else {
            // In the C++ case, the atoms in the other tokenizer are from a
            // different tokenizer-scoped atom table. Therefore, we have to
            // obtain the correspoding atom from our own atom table.
            nonInternedTagName.setNameForNonInterned(other.tagName.getName()
                    // CPPONLY: , other.tagName.isCustom()
                    );
            tagName = nonInternedTagName;
        }

        // [NOCPP[
        attributeName = other.attributeName;
        // ]NOCPP]
        // CPPONLY: if (other.attributeName == null) {
        // CPPONLY:     attributeName = null;
        // CPPONLY: } else if (other.attributeName.isInterned()) {
        // CPPONLY:     attributeName = other.attributeName;
        // CPPONLY: } else {
        // CPPONLY:     // In the C++ case, the atoms in the other tokenizer are from a
        // CPPONLY:     // different tokenizer-scoped atom table. Therefore, we have to
        // CPPONLY:     // obtain the correspoding atom from our own atom table.
        // CPPONLY:     nonInternedAttributeName.setNameForNonInterned(other.attributeName.getLocal(AttributeName.HTML));
        // CPPONLY:     attributeName = nonInternedAttributeName;
        // CPPONLY: }

        Portability.delete(attributes);
        if (other.attributes == null) {
            attributes = null;
        } else {
            attributes = other.attributes.cloneAttributes();
        }
    }

    public void initializeWithoutStarting() throws SAXException {
        confident = false;
        if (!keepBuffer) {
            strBuf = null;
        }
        line = 1;
        // CPPONLY: attributeLine = 1;
        // [NOCPP[
        metaBoundaryPassed = false;
        wantsComments = tokenHandler.wantsComments();
        if (!newAttributesEachTime) {
            attributes = new HtmlAttributes(mappingLangToXmlLang);
        }
        // ]NOCPP]
        resetToDataState();
    }

    protected void errGarbageAfterLtSlash() throws SAXException {
    }

    protected void errLtSlashGt() throws SAXException {
    }

    protected void errWarnLtSlashInRcdata() throws SAXException {
    }

    protected void errHtml4LtSlashInRcdata(char folded) throws SAXException {
    }

    protected void errCharRefLacksSemicolon() throws SAXException {
    }

    protected void errNoDigitsInNCR() throws SAXException {
    }

    protected void errGtInSystemId() throws SAXException {
    }

    protected void errGtInPublicId() throws SAXException {
    }

    protected void errNamelessDoctype() throws SAXException {
    }

    protected void errNestedComment() throws SAXException {
    }

    protected void errPrematureEndOfComment() throws SAXException {
    }

    protected void errBogusComment() throws SAXException {
    }

    protected void errUnquotedAttributeValOrNull(char c) throws SAXException {
    }

    protected void errSlashNotFollowedByGt() throws SAXException {
    }

    protected void errNoSpaceBetweenAttributes() throws SAXException {
    }

    protected void errLtOrEqualsOrGraveInUnquotedAttributeOrNull(char c)
            throws SAXException {
    }

    protected void errAttributeValueMissing() throws SAXException {
    }

    protected void errBadCharBeforeAttributeNameOrNull(char c)
            throws SAXException {
    }

    protected void errEqualsSignBeforeAttributeName() throws SAXException {
    }

    protected void errBadCharAfterLt(char c) throws SAXException {
    }

    protected void errLtGt() throws SAXException {
    }

    protected void errProcessingInstruction() throws SAXException {
    }

    protected void errEofInProcessingInstruction() throws SAXException {
    }

    protected void errInvalidFirstCharacterOfProcessingInstructionTarget()
            throws SAXException {
    }

    protected void errInvalidProcessingInstructionTarget()
            throws SAXException {
    }

    protected void errDisallowedProcessingInstructionTarget()
            throws SAXException {
    }

    protected void errUnescapedAmpersandInterpretedAsCharacterReference()
            throws SAXException {
    }

    protected void errNotSemicolonTerminated() throws SAXException {
    }

    protected void errNoNamedCharacterMatch() throws SAXException {
    }

    protected void errQuoteBeforeAttributeName(char c) throws SAXException {
    }

    protected void errQuoteOrLtInAttributeNameOrNull(char c)
            throws SAXException {
    }

    protected void errExpectedPublicId() throws SAXException {
    }

    protected void errBogusDoctype() throws SAXException {
    }

    protected void maybeWarnPrivateUseAstral() throws SAXException {
    }

    protected void maybeWarnPrivateUse(char ch) throws SAXException {
    }

    protected void maybeErrAttributesOnEndTag(HtmlAttributes attrs)
            throws SAXException {
    }

    protected void maybeErrSlashInEndTag(boolean selfClosing)
            throws SAXException {
    }

    protected char errNcrNonCharacter(char ch) throws SAXException {
        return ch;
    }

    protected void errAstralNonCharacter(int ch) throws SAXException {
    }

    protected void errNcrSurrogate() throws SAXException {
    }

    protected char errNcrControlChar(char ch) throws SAXException {
        return ch;
    }

    protected void errNcrCr() throws SAXException {
    }

    protected void errNcrInC1Range() throws SAXException {
    }

    protected void errEofInPublicId() throws SAXException {
    }

    protected void errEofInComment() throws SAXException {
    }

    protected void errEofInDoctype() throws SAXException {
    }

    protected void errEofInAttributeValue() throws SAXException {
    }

    protected void errEofInAttributeName() throws SAXException {
    }

    protected void errEofWithoutGt() throws SAXException {
    }

    protected void errEofInTagName() throws SAXException {
    }

    protected void errEofInEndTag() throws SAXException {
    }

    protected void errEofAfterLt() throws SAXException {
    }

    protected void errNcrOutOfRange() throws SAXException {
    }

    protected void errNcrUnassigned() throws SAXException {
    }

    protected void errDuplicateAttribute() throws SAXException {
    }

    protected void errEofInSystemId() throws SAXException {
    }

    protected void errExpectedSystemId() throws SAXException {
    }

    protected void errMissingSpaceBeforeDoctypeName() throws SAXException {
    }

    protected void errNcrControlChar() throws SAXException {
    }

    protected void errNcrZero() throws SAXException {
    }

    protected void errNoSpaceBetweenDoctypeSystemKeywordAndQuote()
            throws SAXException {
    }

    protected void errNoSpaceBetweenPublicAndSystemIds() throws SAXException {
    }

    protected void errNoSpaceBetweenDoctypePublicKeywordAndQuote()
            throws SAXException {
    }

    protected void noteAttributeWithoutValue() throws SAXException {
    }

    protected void noteUnquotedAttributeValue() throws SAXException {
    }

    /**
     * Sets the encodingDeclarationHandler.
     *
     * @param encodingDeclarationHandler
     *            the encodingDeclarationHandler to set
     */
    public void setEncodingDeclarationHandler(
            EncodingDeclarationHandler encodingDeclarationHandler) {
        this.encodingDeclarationHandler = encodingDeclarationHandler;
    }

    void destructor() {
        Portability.delete(nonInternedTagName);
        nonInternedTagName = null;
        // CPPONLY: Portability.delete(nonInternedAttributeName);
        // CPPONLY: nonInternedAttributeName = null;
        // The translator will write refcount tracing stuff here
        Portability.delete(attributes);
        attributes = null;
    }

    // [NOCPP[

    /**
     * Sets an offset to be added to the position reported to
     * <code>TransitionHandler</code>.
     *
     * @param offset the offset
     */
    public void setTransitionBaseOffset(int offset) {

    }

    // ]NOCPP]

}
