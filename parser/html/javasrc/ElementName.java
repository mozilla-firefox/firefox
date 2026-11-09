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

package nu.validator.htmlparser.impl;

// uncomment to regenerate self
//import java.io.BufferedReader;
//import java.io.File;
//import java.io.FileInputStream;
//import java.io.IOException;
//import java.io.InputStreamReader;
//import java.util.Arrays;
//import java.util.Collections;
//import java.util.HashMap;
//import java.util.LinkedList;
//import java.util.List;
//import java.util.Map;
//import java.util.Map.Entry;
//import java.util.regex.Matcher;
//import java.util.regex.Pattern;

import nu.validator.htmlparser.annotation.Inline;
import nu.validator.htmlparser.annotation.Local;
import nu.validator.htmlparser.annotation.NoLength;
import nu.validator.htmlparser.annotation.Unsigned;
import nu.validator.htmlparser.common.Interner;

public final class ElementName
        // uncomment when regenerating self
//        implements Comparable<ElementName>
{

    /**
     * The mask for extracting the dispatch group.
     */
    public static final int GROUP_MASK = 127;

    /**
     * Indicates that the element is not a pre-interned element. Forbidden on
     * preinterned elements.
     */
    public static final int NOT_INTERNED = (1 << 30);

    /**
     * Indicates that the element is in the "special" category. This bit should
     * not be pre-set on MathML or SVG specials--only on HTML specials.
     */
    public static final int SPECIAL = (1 << 29);

    /**
     * The element is foster-parenting. This bit should be pre-set on elements
     * that are foster-parenting as HTML.
     */
    public static final int FOSTER_PARENTING = (1 << 28);

    /**
     * The element is scoping. This bit should be pre-set on elements that are
     * scoping as HTML.
     */
    public static final int SCOPING = (1 << 27);

    /**
     * The element is scoping as SVG.
     */
    public static final int SCOPING_AS_SVG = (1 << 26);

    /**
     * The element is scoping as MathML.
     */
    public static final int SCOPING_AS_MATHML = (1 << 25);

    /**
     * The element is an HTML integration point.
     */
    public static final int HTML_INTEGRATION_POINT = (1 << 24);

    /**
     * The element has an optional end tag.
     */
    public static final int OPTIONAL_END_TAG = (1 << 23);

    private @Local String name;

    private @Local String camelCaseName;

    // CPPONLY: private @HtmlCreator Object htmlCreator;

    // CPPONLY: private @SvgCreator Object svgCreator;

    /**
     * The lowest 7 bits are the dispatch group. The high bits are flags.
     */
    public final int flags;

    @Inline public @Local String getName() {
        return name;
    }

    @Inline public @Local String getCamelCaseName() {
        return camelCaseName;
    }

    // CPPONLY: @Inline public @HtmlCreator Object getHtmlCreator() {
    // CPPONLY: return htmlCreator;
    // CPPONLY: }

    // CPPONLY: @Inline public @SvgCreator Object getSvgCreator() {
    // CPPONLY: return svgCreator;
    // CPPONLY: }

    @Inline public int getFlags() {
        return flags;
    }

    @Inline public int getGroup() {
        return flags & ElementName.GROUP_MASK;
    }

    @Inline public boolean isInterned() {
        return (flags & ElementName.NOT_INTERNED) == 0;
    }

    @Inline static int levelOrderBinarySearch(int[] data, int key) {
        int n = data.length;
        int i = 0;

        while (i < n) {
            int val = data[i];
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

    @Inline static ElementName elementNameByBuffer(@NoLength char[] buf,
            int length) {
        if (length == 3) {
            // `div` is less common on Speedometer 3.1 than lengths 5 and 6, but
            // surely `div` must be more common on the Web.
            if (Portability.bufferStartsWithLiteralAssumeSufficientLength(buf, "div")) {
                return ElementName.DIV;
            }
            // Not necessary for Speedometer 3.1 but otherwise obvious
            if (Portability.bufferStartsWithLiteralAssumeSufficientLength(buf, "img")) {
                return ElementName.IMG;
            }
        } else if (length == 4) {
            // `span` is less common on Speedometer 3.1 than lengths 5 and 6, but
            // surely `span` must be more common on the Web.
            if (Portability.bufferStartsWithLiteralAssumeSufficientLength(buf, "span")) {
                return ElementName.SPAN;
            }
            // Should `link` and `meta` go here for full page loads?
        } else if (length == 2) {
            if (Portability.bufferStartsWithLiteralAssumeSufficientLength(buf, "li")) {
                return ElementName.LI;
            }
            if (Portability.bufferStartsWithLiteralAssumeSufficientLength(buf, "ul")) {
                return ElementName.UL;
            }
            // Other workloads might benefit from `th`, `td`, and `tr` here.
        } else if (length == 1) {
            // `a` is less common on Speedometer 3.1 than lengths 5 and 6, but
            // surely `a` must be more common on the Web.
            if (buf[0] == 'a') {
                return ElementName.A;
            }
            // Not necessary for Speedometer 3.1 but otherwise obvious
            if (buf[0] == 'p') {
                return ElementName.P;
            }
            // Other workloads might benefit from `i` and `b` here.
        } else if (length == 5) {
            if (Portability.bufferStartsWithLiteralAssumeSufficientLength(buf, "label")) {
                return ElementName.LABEL;
            }
            if (Portability.bufferStartsWithLiteralAssumeSufficientLength(buf, "input")) {
                return ElementName.INPUT;
            }
            // Should `style` go here for other workloads?
        } else if (length == 6) {
            if (Portability.bufferStartsWithLiteralAssumeSufficientLength(buf, "button")) {
                return ElementName.BUTTON;
            }
            if (Portability.bufferStartsWithLiteralAssumeSufficientLength(buf, "strong")) {
                return ElementName.STRONG;
            }
            // Should `script` go here for other workloads?
        }
        @Unsigned int hash = ElementName.bufToHash(buf, length);
        int[] hashes;
        hashes = ElementName.ELEMENT_HASHES;
        int index = levelOrderBinarySearch(hashes, hash);
        if (index < 0) {
            return null;
        } else {
            ElementName elementName = ElementName.ELEMENT_NAMES[index];
            @Local String name = elementName.name;
            if (!Portability.localEqualsBuffer(name, buf, length)) {
                return null;
            }
            return elementName;
        }
    }

    /**
     * This method has to return a unique positive integer for each well-known
     * lower-cased element name.
     *
     * @param buf
     * @param len
     * @return
     */
    @Inline private static @Unsigned int bufToHash(@NoLength char[] buf,
            int length) {
        @Unsigned int len = length;
        @Unsigned int first = buf[0];
        first <<= 19;
        @Unsigned int second = 1 << 23;
        @Unsigned int third = 0;
        @Unsigned int fourth = 0;
        @Unsigned int fifth = 0;
        if (length >= 4) {
            second = buf[length - 4];
            second <<= 4;
            third = buf[length - 3];
            third <<= 9;
            fourth = buf[length - 2];
            fourth <<= 14;
            fifth = buf[length - 1];
            fifth <<= 24;
        } else if (length == 3) {
            second = buf[1];
            second <<= 4;
            third = buf[2];
            third <<= 9;
        } else if (length == 2) {
            second = buf[1];
            second <<= 24;
        }
        return len + first + second + third + fourth + fifth;
    }

    private ElementName(@Local String name, @Local String camelCaseName,
            // CPPONLY: @HtmlCreator Object htmlCreator, @SvgCreator Object
            // CPPONLY: svgCreator,
            int flags) {
        this.name = name;
        this.camelCaseName = camelCaseName;
        // CPPONLY: this.htmlCreator = htmlCreator;
        // CPPONLY: this.svgCreator = svgCreator;
        this.flags = flags;
    }

    public ElementName() {
        this.name = null;
        this.camelCaseName = null;
        // CPPONLY: this.htmlCreator = NS_NewHTMLUnknownElement;
        // CPPONLY: this.svgCreator = NS_NewSVGUnknownElement;
        this.flags = TreeBuilder.OTHER | NOT_INTERNED;
    }

    public void destructor() {
        // The translator adds refcount debug code here.
    }

    @Inline public void setNameForNonInterned(@Local String name
    // CPPONLY: , boolean custom
    ) {
        // No need to worry about refcounting the local name, because in the
        // C++ case the scoped atom table remembers its own atoms.
        this.name = name;
        this.camelCaseName = name;
        // CPPONLY: if (custom) {
        // CPPONLY: this.htmlCreator = null;
        // CPPONLY: } else {
        // CPPONLY: this.htmlCreator = NS_NewHTMLUnknownElement;
        // CPPONLY: }
        // The assertion below relies on TreeBuilder.OTHER being zero!
        // TreeBuilder.OTHER isn't referenced here, because it would create
        // a circular C++ header dependency given that this method is inlined.
        assert this.flags == ElementName.NOT_INTERNED;
    }

    // CPPONLY: @Inline public boolean isCustom() {
    // CPPONLY: return this.htmlCreator == null;
    // CPPONLY: }

    public static final ElementName ANNOTATION_XML = new ElementName(
            "annotation-xml", "annotation-xml",
            // CPPONLY: NS_NewHTMLUnknownElement, NS_NewSVGUnknownElement,
            TreeBuilder.ANNOTATION_XML | SCOPING_AS_MATHML);

    public static final ElementName DIV = new ElementName("div", "div",
         // CPPONLY: NS_NewHTMLDivElement,
         // CPPONLY: NS_NewSVGUnknownElement,
         TreeBuilder.DIV_OR_BLOCKQUOTE_OR_CENTER_OR_MENU | SPECIAL);

    public static final ElementName IMG = new ElementName("img", "img",
         // CPPONLY: NS_NewHTMLImageElement,
         // CPPONLY: NS_NewSVGUnknownElement,
         TreeBuilder.IMG | SPECIAL);

    public static final ElementName SPAN = new ElementName("span", "span",
         // CPPONLY: NS_NewHTMLSpanElement,
         // CPPONLY: NS_NewSVGUnknownElement,
         TreeBuilder.RUBY_OR_SPAN_OR_SUB_OR_SUP_OR_VAR);

    public static final ElementName LI = new ElementName("li", "li",
         // CPPONLY: NS_NewHTMLLIElement,
         // CPPONLY: NS_NewSVGUnknownElement,
         TreeBuilder.LI | SPECIAL | OPTIONAL_END_TAG);

    public static final ElementName UL = new ElementName("ul", "ul",
         // CPPONLY: NS_NewHTMLSharedListElement,
         // CPPONLY: NS_NewSVGUnknownElement,
         TreeBuilder.UL_OR_OL_OR_DL | SPECIAL);

    public static final ElementName A = new ElementName("a", "a",
         // CPPONLY: NS_NewHTMLAnchorElement,
         // CPPONLY: NS_NewSVGAElement,
         TreeBuilder.A);

    public static final ElementName P = new ElementName("p", "p",
         // CPPONLY: NS_NewHTMLParagraphElement,
         // CPPONLY: NS_NewSVGUnknownElement,
         TreeBuilder.P | SPECIAL | OPTIONAL_END_TAG);

    public static final ElementName LABEL = new ElementName("label", "label",
         // CPPONLY: NS_NewHTMLLabelElement,
         // CPPONLY: NS_NewSVGUnknownElement,
         TreeBuilder.OTHER);

    public static final ElementName INPUT = new ElementName("input", "input",
         // CPPONLY: NS_NewHTMLInputElement,
         // CPPONLY: NS_NewSVGUnknownElement,
         TreeBuilder.INPUT | SPECIAL);

    public static final ElementName BUTTON = new ElementName("button", "button",
         // CPPONLY: NS_NewHTMLButtonElement,
         // CPPONLY: NS_NewSVGUnknownElement,
         TreeBuilder.BUTTON | SPECIAL);

    public static final ElementName STRONG = new ElementName("strong", "strong",
         // CPPONLY: NS_NewHTMLElement,
         // CPPONLY: NS_NewSVGUnknownElement,
         TreeBuilder.B_OR_BIG_OR_CODE_OR_EM_OR_I_OR_S_OR_SMALL_OR_STRIKE_OR_STRONG_OR_TT_OR_U);

    // START CODE ONLY USED FOR GENERATING CODE uncomment and run to regenerate

//    private static final Pattern HTML_TAG_DEF = Pattern.compile(
//            "^HTML_TAG\\(([^,]+),\\s*([^,]+),\\s*[^,]+\\).*$");
//
//    private static final Pattern HTML_HTMLELEMENT_TAG_DEF = Pattern.compile(
//            "^HTML_HTMLELEMENT_TAG\\(([^\\)]+)\\).*$");
//
//    private static final Pattern SVG_TAG_DEF = Pattern.compile(
//            "^SVG_(?:FROM_PARSER_)?TAG\\(([^,]+),\\s*([^\\)]+)\\).*$");
//
//    private static final Map<String, String> htmlMap = new HashMap<String, String>();
//
//    private static final Map<String, String> svgMap = new HashMap<String, String>();
//
//    private static final String[] HARDCODED_HTML_ELEMENTS = { "input", "strong", "a", "p", "li", "span", "div", "ul", "img", "button", "label" };
//
//    private static final String[] HARDCODED_SVG_ELEMENTS = { "a" };
//
//    private static void ingestHtmlTags(File htmlList) throws IOException {
//        // This doesn't need to be efficient, so let's make it easy to write.
//        BufferedReader htmlReader = new BufferedReader(
//                new InputStreamReader(new FileInputStream(htmlList), "utf-8"));
//        try {
//            String line;
//            while ((line = htmlReader.readLine()) != null) {
//                if (!line.startsWith("HTML_")) {
//                    continue;
//                }
//                if (line.startsWith("HTML_OTHER")) {
//                    continue;
//                }
//                Matcher m = HTML_TAG_DEF.matcher(line);
//                if (m.matches()) {
//                    String iface = m.group(2);
//                    if ("Unknown".equals(iface)) {
//                        continue;
//                    }
//                    htmlMap.put(m.group(1), "NS_NewHTML" + iface + "Element");
//                } else {
//                    m = HTML_HTMLELEMENT_TAG_DEF.matcher(line);
//                    if (!m.matches()) {
//                        throw new RuntimeException(
//                                "Malformed HTML element definition: " + line);
//                    }
//                    htmlMap.put(m.group(1), "NS_NewHTMLElement");
//                }
//            }
//        } finally {
//            htmlReader.close();
//        }
//    }
//
//    private static boolean isHardcodedHtmlElement(String name) {
//        for (int i = 0; i < HARDCODED_HTML_ELEMENTS.length; i++) {
//            if (HARDCODED_HTML_ELEMENTS[i].equals(name)) {
//                return true;
//            }
//        }
//        return false;
//    }
//
//    private static boolean isHardcodedSvgElement(String name) {
//        for (int i = 0; i < HARDCODED_SVG_ELEMENTS.length; i++) {
//            if (HARDCODED_SVG_ELEMENTS[i].equals(name)) {
//                return true;
//            }
//        }
//        return false;
//    }
//
//    private static void ingestSvgTags(File svgList) throws IOException {
//        // This doesn't need to be efficient, so let's make it easy to write.
//        BufferedReader svgReader = new BufferedReader(
//                new InputStreamReader(new FileInputStream(svgList), "utf-8"));
//        try {
//            String line;
//            while ((line = svgReader.readLine()) != null) {
//                if (!line.startsWith("SVG_")) {
//                    continue;
//                }
//                Matcher m = SVG_TAG_DEF.matcher(line);
//                if (!m.matches()) {
//                    throw new RuntimeException(
//                            "Malformed SVG element definition: " + line);
//                }
//                String name = m.group(1);
//                if ("svgSwitch".equals(name)) {
//                    name = "switch";
//                }
//                svgMap.put(name, "NS_NewSVG" + m.group(2) + "Element");
//            }
//        } finally {
//            svgReader.close();
//        }
//    }
//
//    private static String htmlCreator(String name) {
//        String creator = htmlMap.remove(name);
//        if (creator != null) {
//            return creator;
//        }
//        return "NS_NewHTMLUnknownElement";
//    }
//
//    private static String svgCreator(String name) {
//        String creator = svgMap.remove(name);
//        if (creator != null) {
//            return creator;
//        }
//        return "NS_NewSVGUnknownElement";
//    }
//
//    /**
//     * @see java.lang.Object#toString()
//     */
//    @Override public String toString() {
//        return "(\"" + name + "\", \"" + camelCaseName + "\", \n// CPP"
//                + "ONLY: " + htmlCreator(name) + ",\n// CPP" + "ONLY: "
//                + svgCreator(camelCaseName) + ", \n" + decomposedFlags() + ")";
//    }
//
//    private String decomposedFlags() {
//        StringBuilder buf = new StringBuilder("TreeBuilder.");
//        buf.append(treeBuilderGroupToName());
//        if ((flags & SPECIAL) != 0) {
//            buf.append(" | SPECIAL");
//        }
//        if ((flags & FOSTER_PARENTING) != 0) {
//            buf.append(" | FOSTER_PARENTING");
//        }
//        if ((flags & SCOPING) != 0) {
//            buf.append(" | SCOPING");
//        }
//        if ((flags & SCOPING_AS_MATHML) != 0) {
//            buf.append(" | SCOPING_AS_MATHML");
//        }
//        if ((flags & SCOPING_AS_SVG) != 0) {
//            buf.append(" | SCOPING_AS_SVG");
//        }
//        if ((flags & OPTIONAL_END_TAG) != 0) {
//            buf.append(" | OPTIONAL_END_TAG");
//        }
//        return buf.toString();
//    }
//
//    private String constName() {
//        char[] buf = new char[name.length()];
//        for (int i = 0; i < name.length(); i++) {
//            char c = name.charAt(i);
//            if (c == '-') {
//                if (!"annotation-xml".equals(name)) {
//                    throw new RuntimeException(
//                            "Non-annotation-xml element name with hyphen: "
//                                    + name);
//                }
//                buf[i] = '_';
//            } else if (c >= '0' && c <= '9') {
//                buf[i] = c;
//            } else {
//                buf[i] = (char) (c - 0x20);
//            }
//        }
//        return new String(buf);
//    }
//
//    private int hash() {
//        return bufToHash(name.toCharArray(), name.length());
//    }
//
//    public int compareTo(ElementName other) {
//        int thisHash = this.hash();
//        int otherHash = other.hash();
//        if (thisHash < otherHash) {
//            return -1;
//        } else if (thisHash == otherHash) {
//            return 0;
//        } else {
//            return 1;
//        }
//    }
//
//    private String treeBuilderGroupToName() {
//        switch (getGroup()) {
//            case TreeBuilder.OTHER:
//                return "OTHER";
//            case TreeBuilder.A:
//                return "A";
//            case TreeBuilder.BASE:
//                return "BASE";
//            case TreeBuilder.BODY:
//                return "BODY";
//            case TreeBuilder.BR:
//                return "BR";
//            case TreeBuilder.BUTTON:
//                return "BUTTON";
//            case TreeBuilder.CAPTION:
//                return "CAPTION";
//            case TreeBuilder.COL:
//                return "COL";
//            case TreeBuilder.COLGROUP:
//                return "COLGROUP";
//            case TreeBuilder.FONT:
//                return "FONT";
//            case TreeBuilder.FORM:
//                return "FORM";
//            case TreeBuilder.FRAME:
//                return "FRAME";
//            case TreeBuilder.FRAMESET:
//                return "FRAMESET";
//            case TreeBuilder.IMAGE:
//                return "IMAGE";
//            case TreeBuilder.INPUT:
//                return "INPUT";
//            case TreeBuilder.LI:
//                return "LI";
//            case TreeBuilder.LINK_OR_BASEFONT_OR_BGSOUND:
//                return "LINK_OR_BASEFONT_OR_BGSOUND";
//            case TreeBuilder.MATH:
//                return "MATH";
//            case TreeBuilder.META:
//                return "META";
//            case TreeBuilder.SVG:
//                return "SVG";
//            case TreeBuilder.HEAD:
//                return "HEAD";
//            case TreeBuilder.HR:
//                return "HR";
//            case TreeBuilder.HTML:
//                return "HTML";
//            case TreeBuilder.KEYGEN:
//                return "KEYGEN";
//            case TreeBuilder.NOBR:
//                return "NOBR";
//            case TreeBuilder.NOFRAMES:
//                return "NOFRAMES";
//            case TreeBuilder.NOSCRIPT:
//                return "NOSCRIPT";
//            case TreeBuilder.OPTGROUP:
//                return "OPTGROUP";
//            case TreeBuilder.OPTION:
//                return "OPTION";
//            case TreeBuilder.P:
//                return "P";
//            case TreeBuilder.PLAINTEXT:
//                return "PLAINTEXT";
//            case TreeBuilder.SCRIPT:
//                return "SCRIPT";
//            case TreeBuilder.SELECT:
//                return "SELECT";
//            case TreeBuilder.STYLE:
//                return "STYLE";
//            case TreeBuilder.TABLE:
//                return "TABLE";
//            case TreeBuilder.TEXTAREA:
//                return "TEXTAREA";
//            case TreeBuilder.TITLE:
//                return "TITLE";
//            case TreeBuilder.TEMPLATE:
//                return "TEMPLATE";
//            case TreeBuilder.TR:
//                return "TR";
//            case TreeBuilder.XMP:
//                return "XMP";
//            case TreeBuilder.TBODY_OR_THEAD_OR_TFOOT:
//                return "TBODY_OR_THEAD_OR_TFOOT";
//            case TreeBuilder.TD_OR_TH:
//                return "TD_OR_TH";
//            case TreeBuilder.DD_OR_DT:
//                return "DD_OR_DT";
//            case TreeBuilder.H1_OR_H2_OR_H3_OR_H4_OR_H5_OR_H6:
//                return "H1_OR_H2_OR_H3_OR_H4_OR_H5_OR_H6";
//            case TreeBuilder.OBJECT:
//                return "OBJECT";
//            case TreeBuilder.OUTPUT:
//                return "OUTPUT";
//            case TreeBuilder.MARQUEE_OR_APPLET:
//                return "MARQUEE_OR_APPLET";
//            case TreeBuilder.PRE_OR_LISTING:
//                return "PRE_OR_LISTING";
//            case TreeBuilder.B_OR_BIG_OR_CODE_OR_EM_OR_I_OR_S_OR_SMALL_OR_STRIKE_OR_STRONG_OR_TT_OR_U:
//                return "B_OR_BIG_OR_CODE_OR_EM_OR_I_OR_S_OR_SMALL_OR_STRIKE_OR_STRONG_OR_TT_OR_U";
//            case TreeBuilder.UL_OR_OL_OR_DL:
//                return "UL_OR_OL_OR_DL";
//            case TreeBuilder.IFRAME:
//                return "IFRAME";
//            case TreeBuilder.NOEMBED:
//                return "NOEMBED";
//            case TreeBuilder.EMBED:
//                return "EMBED";
//            case TreeBuilder.IMG:
//                return "IMG";
//            case TreeBuilder.AREA_OR_WBR:
//                return "AREA_OR_WBR";
//            case TreeBuilder.DIV_OR_BLOCKQUOTE_OR_CENTER_OR_MENU:
//                return "DIV_OR_BLOCKQUOTE_OR_CENTER_OR_MENU";
//            case TreeBuilder.FIELDSET:
//                return "FIELDSET";
//            case TreeBuilder.ADDRESS_OR_ARTICLE_OR_ASIDE_OR_DETAILS_OR_DIALOG_OR_DIR_OR_FIGCAPTION_OR_FIGURE_OR_FOOTER_OR_HEADER_OR_HGROUP_OR_MAIN_OR_NAV_OR_SEARCH_OR_SECTION_OR_SUMMARY:
//                return "ADDRESS_OR_ARTICLE_OR_ASIDE_OR_DETAILS_OR_DIALOG_OR_DIR_OR_FIGCAPTION_OR_FIGURE_OR_FOOTER_OR_HEADER_OR_HGROUP_OR_MAIN_OR_NAV_OR_SEARCH_OR_SECTION_OR_SUMMARY";
//            case TreeBuilder.RUBY_OR_SPAN_OR_SUB_OR_SUP_OR_VAR:
//                return "RUBY_OR_SPAN_OR_SUB_OR_SUP_OR_VAR";
//            case TreeBuilder.RB_OR_RTC:
//                return "RB_OR_RTC";
//            case TreeBuilder.RT_OR_RP:
//                return "RT_OR_RP";
//            case TreeBuilder.PARAM_OR_SOURCE_OR_TRACK:
//                return "PARAM_OR_SOURCE_OR_TRACK";
//            case TreeBuilder.MGLYPH_OR_MALIGNMARK:
//                return "MGLYPH_OR_MALIGNMARK";
//            case TreeBuilder.MI_MO_MN_MS_MTEXT:
//                return "MI_MO_MN_MS_MTEXT";
//            case TreeBuilder.ANNOTATION_XML:
//                return "ANNOTATION_XML";
//            case TreeBuilder.FOREIGNOBJECT_OR_DESC:
//                return "FOREIGNOBJECT_OR_DESC";
//        }
//        return null;
//    }
//
//    private static void fillLevelOrderArray(List<ElementName> sorted, int depth,
//            int rootIdx, ElementName[] levelOrder) {
//        if (rootIdx >= levelOrder.length) {
//            return;
//        }
//
//        if (depth > 0) {
//            fillLevelOrderArray(sorted, depth - 1, rootIdx * 2 + 1, levelOrder);
//        }
//
//        if (!sorted.isEmpty()) {
//            levelOrder[rootIdx] = sorted.remove(0);
//        }
//
//        if (depth > 0) {
//            fillLevelOrderArray(sorted, depth - 1, rootIdx * 2 + 2, levelOrder);
//        }
//    }
//
//    /**
//     * Regenerate self
//     *
//     * The args should be the paths to m-c files
//     * parser/htmlparser/nsHTMLTagList.h and dom/svg/SVGTagList.h.
//     */
//    public static void main(String[] args) {
//        File htmlList = new File(args[0]);
//        File svgList = new File(args[1]);
//        try {
//            ingestHtmlTags(htmlList);
//        } catch (IOException e) {
//            throw new RuntimeException(e);
//        }
//        try {
//            ingestSvgTags(svgList);
//        } catch (IOException e) {
//            throw new RuntimeException(e);
//        }
//
//        Arrays.sort(ELEMENT_NAMES);
//        for (int i = 0; i < ELEMENT_NAMES.length; i++) {
//            int hash = ELEMENT_NAMES[i].hash();
//            if (hash < 0) {
//                System.err.println("Negative hash: " + ELEMENT_NAMES[i].name);
//                return;
//            }
//            for (int j = i + 1; j < ELEMENT_NAMES.length; j++) {
//                if (hash == ELEMENT_NAMES[j].hash()) {
//                    System.err.println(
//                            "Hash collision: " + ELEMENT_NAMES[i].name + ", "
//                                    + ELEMENT_NAMES[j].name);
//                    return;
//                }
//            }
//        }
//        for (int i = 0; i < ELEMENT_NAMES.length; i++) {
//            ElementName el = ELEMENT_NAMES[i];
//            System.out.println(
//                    "public static final ElementName " + el.constName()
//                            + " = new ElementName" + el.toString() + ";");
//        }
//
//        LinkedList<ElementName> sortedNames = new LinkedList<ElementName>();
//        Collections.addAll(sortedNames, ELEMENT_NAMES);
//        ElementName[] levelOrder = new ElementName[ELEMENT_NAMES.length];
//        int bstDepth = (int) Math.ceil(
//                Math.log(ELEMENT_NAMES.length) / Math.log(2));
//        fillLevelOrderArray(sortedNames, bstDepth, 0, levelOrder);
//
//        System.out.println(
//                "private final static @NoLength ElementName[] ELEMENT_NAMES = {");
//        for (int i = 0; i < levelOrder.length; i++) {
//            ElementName el = levelOrder[i];
//            System.out.println(el.constName() + ",");
//        }
//        System.out.println("};");
//        System.out.println("private final static int[] ELEMENT_HASHES = {");
//        for (int i = 0; i < levelOrder.length; i++) {
//            ElementName el = levelOrder[i];
//            System.out.println(Integer.toString(el.hash()) + ",");
//        }
//        System.out.println("};");
//
//        for (Entry<String, String> entry : htmlMap.entrySet()) {
//            String n = entry.getKey();
//            if (!isHardcodedHtmlElement(n)) {
//                System.err.println("Missing HTML element: " + n);
//            }
//        }
//        for (Entry<String, String> entry : svgMap.entrySet()) {
//            String n = entry.getKey();
//            if (!isHardcodedSvgElement(n)) {
//                System.err.println("Missing SVG element: " + entry.getKey());
//            }
//        }
//    }

    // START GENERATED CODE
public static final ElementName BIG = new ElementName("big", "big",
// CPPONLY: NS_NewHTMLElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.B_OR_BIG_OR_CODE_OR_EM_OR_I_OR_S_OR_SMALL_OR_STRIKE_OR_STRONG_OR_TT_OR_U);
public static final ElementName BDI = new ElementName("bdi", "bdi",
// CPPONLY: NS_NewHTMLElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.OTHER);
public static final ElementName BDO = new ElementName("bdo", "bdo",
// CPPONLY: NS_NewHTMLElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.OTHER);
public static final ElementName COL = new ElementName("col", "col",
// CPPONLY: NS_NewHTMLTableColElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.COL | SPECIAL);
public static final ElementName DEL = new ElementName("del", "del",
// CPPONLY: NS_NewHTMLModElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.OTHER);
public static final ElementName DFN = new ElementName("dfn", "dfn",
// CPPONLY: NS_NewHTMLElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.OTHER);
public static final ElementName DIR = new ElementName("dir", "dir",
// CPPONLY: NS_NewHTMLSharedElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.ADDRESS_OR_ARTICLE_OR_ASIDE_OR_DETAILS_OR_DIALOG_OR_DIR_OR_FIGCAPTION_OR_FIGURE_OR_FOOTER_OR_HEADER_OR_HGROUP_OR_MAIN_OR_NAV_OR_SEARCH_OR_SECTION_OR_SUMMARY | SPECIAL);
public static final ElementName INS = new ElementName("ins", "ins",
// CPPONLY: NS_NewHTMLModElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.OTHER);
public static final ElementName KBD = new ElementName("kbd", "kbd",
// CPPONLY: NS_NewHTMLElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.OTHER);
public static final ElementName MAP = new ElementName("map", "map",
// CPPONLY: NS_NewHTMLMapElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.OTHER);
public static final ElementName NAV = new ElementName("nav", "nav",
// CPPONLY: NS_NewHTMLElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.ADDRESS_OR_ARTICLE_OR_ASIDE_OR_DETAILS_OR_DIALOG_OR_DIR_OR_FIGCAPTION_OR_FIGURE_OR_FOOTER_OR_HEADER_OR_HGROUP_OR_MAIN_OR_NAV_OR_SEARCH_OR_SECTION_OR_SUMMARY | SPECIAL);
public static final ElementName PRE = new ElementName("pre", "pre",
// CPPONLY: NS_NewHTMLPreElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.PRE_OR_LISTING | SPECIAL);
public static final ElementName B = new ElementName("b", "b",
// CPPONLY: NS_NewHTMLElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.B_OR_BIG_OR_CODE_OR_EM_OR_I_OR_S_OR_SMALL_OR_STRIKE_OR_STRONG_OR_TT_OR_U);
public static final ElementName RTC = new ElementName("rtc", "rtc",
// CPPONLY: NS_NewHTMLElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.RB_OR_RTC | OPTIONAL_END_TAG);
public static final ElementName SUB = new ElementName("sub", "sub",
// CPPONLY: NS_NewHTMLElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.RUBY_OR_SPAN_OR_SUB_OR_SUP_OR_VAR);
public static final ElementName SVG = new ElementName("svg", "svg",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGSVGElement,
TreeBuilder.SVG);
public static final ElementName SUP = new ElementName("sup", "sup",
// CPPONLY: NS_NewHTMLElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.RUBY_OR_SPAN_OR_SUB_OR_SUP_OR_VAR);
public static final ElementName SET = new ElementName("set", "set",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGSetElement,
TreeBuilder.OTHER);
public static final ElementName USE = new ElementName("use", "use",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGUseElement,
TreeBuilder.OTHER);
public static final ElementName VAR = new ElementName("var", "var",
// CPPONLY: NS_NewHTMLElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.RUBY_OR_SPAN_OR_SUB_OR_SUP_OR_VAR);
public static final ElementName G = new ElementName("g", "g",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGGElement,
TreeBuilder.OTHER);
public static final ElementName WBR = new ElementName("wbr", "wbr",
// CPPONLY: NS_NewHTMLElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.AREA_OR_WBR | SPECIAL);
public static final ElementName XMP = new ElementName("xmp", "xmp",
// CPPONLY: NS_NewHTMLPreElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.XMP | SPECIAL);
public static final ElementName I = new ElementName("i", "i",
// CPPONLY: NS_NewHTMLElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.B_OR_BIG_OR_CODE_OR_EM_OR_I_OR_S_OR_SMALL_OR_STRIKE_OR_STRONG_OR_TT_OR_U);
public static final ElementName Q = new ElementName("q", "q",
// CPPONLY: NS_NewHTMLSharedElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.OTHER);
public static final ElementName S = new ElementName("s", "s",
// CPPONLY: NS_NewHTMLElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.B_OR_BIG_OR_CODE_OR_EM_OR_I_OR_S_OR_SMALL_OR_STRIKE_OR_STRONG_OR_TT_OR_U);
public static final ElementName U = new ElementName("u", "u",
// CPPONLY: NS_NewHTMLElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.B_OR_BIG_OR_CODE_OR_EM_OR_I_OR_S_OR_SMALL_OR_STRIKE_OR_STRONG_OR_TT_OR_U);
public static final ElementName H1 = new ElementName("h1", "h1",
// CPPONLY: NS_NewHTMLHeadingElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.H1_OR_H2_OR_H3_OR_H4_OR_H5_OR_H6 | SPECIAL);
public static final ElementName H2 = new ElementName("h2", "h2",
// CPPONLY: NS_NewHTMLHeadingElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.H1_OR_H2_OR_H3_OR_H4_OR_H5_OR_H6 | SPECIAL);
public static final ElementName H3 = new ElementName("h3", "h3",
// CPPONLY: NS_NewHTMLHeadingElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.H1_OR_H2_OR_H3_OR_H4_OR_H5_OR_H6 | SPECIAL);
public static final ElementName H4 = new ElementName("h4", "h4",
// CPPONLY: NS_NewHTMLHeadingElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.H1_OR_H2_OR_H3_OR_H4_OR_H5_OR_H6 | SPECIAL);
public static final ElementName H5 = new ElementName("h5", "h5",
// CPPONLY: NS_NewHTMLHeadingElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.H1_OR_H2_OR_H3_OR_H4_OR_H5_OR_H6 | SPECIAL);
public static final ElementName H6 = new ElementName("h6", "h6",
// CPPONLY: NS_NewHTMLHeadingElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.H1_OR_H2_OR_H3_OR_H4_OR_H5_OR_H6 | SPECIAL);
public static final ElementName AREA = new ElementName("area", "area",
// CPPONLY: NS_NewHTMLAreaElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.AREA_OR_WBR | SPECIAL);
public static final ElementName DATA = new ElementName("data", "data",
// CPPONLY: NS_NewHTMLDataElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.OTHER);
public static final ElementName FEFUNCA = new ElementName("fefunca", "feFuncA",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGFEFuncAElement,
TreeBuilder.OTHER);
public static final ElementName METADATA = new ElementName("metadata", "metadata",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGMetadataElement,
TreeBuilder.OTHER);
public static final ElementName META = new ElementName("meta", "meta",
// CPPONLY: NS_NewHTMLMetaElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.META | SPECIAL);
public static final ElementName TEXTAREA = new ElementName("textarea", "textarea",
// CPPONLY: NS_NewHTMLTextAreaElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.TEXTAREA | SPECIAL);
public static final ElementName FEFUNCB = new ElementName("fefuncb", "feFuncB",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGFEFuncBElement,
TreeBuilder.OTHER);
public static final ElementName RB = new ElementName("rb", "rb",
// CPPONLY: NS_NewHTMLElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.RB_OR_RTC | OPTIONAL_END_TAG);
public static final ElementName DESC = new ElementName("desc", "desc",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGDescElement,
TreeBuilder.FOREIGNOBJECT_OR_DESC | SCOPING_AS_SVG);
public static final ElementName DD = new ElementName("dd", "dd",
// CPPONLY: NS_NewHTMLElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.DD_OR_DT | SPECIAL | OPTIONAL_END_TAG);
public static final ElementName BGSOUND = new ElementName("bgsound", "bgsound",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.LINK_OR_BASEFONT_OR_BGSOUND | SPECIAL);
public static final ElementName EMBED = new ElementName("embed", "embed",
// CPPONLY: NS_NewHTMLEmbedElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.EMBED | SPECIAL);
public static final ElementName FEBLEND = new ElementName("feblend", "feBlend",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGFEBlendElement,
TreeBuilder.OTHER);
public static final ElementName FEFLOOD = new ElementName("feflood", "feFlood",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGFEFloodElement,
TreeBuilder.OTHER);
public static final ElementName HEAD = new ElementName("head", "head",
// CPPONLY: NS_NewHTMLSharedElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.HEAD | SPECIAL | OPTIONAL_END_TAG);
public static final ElementName LEGEND = new ElementName("legend", "legend",
// CPPONLY: NS_NewHTMLLegendElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.OTHER);
public static final ElementName NOEMBED = new ElementName("noembed", "noembed",
// CPPONLY: NS_NewHTMLElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.NOEMBED | SPECIAL);
public static final ElementName TD = new ElementName("td", "td",
// CPPONLY: NS_NewHTMLTableCellElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.TD_OR_TH | SPECIAL | SCOPING | OPTIONAL_END_TAG);
public static final ElementName THEAD = new ElementName("thead", "thead",
// CPPONLY: NS_NewHTMLTableSectionElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.TBODY_OR_THEAD_OR_TFOOT | SPECIAL | FOSTER_PARENTING | OPTIONAL_END_TAG);
public static final ElementName ASIDE = new ElementName("aside", "aside",
// CPPONLY: NS_NewHTMLElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.ADDRESS_OR_ARTICLE_OR_ASIDE_OR_DETAILS_OR_DIALOG_OR_DIR_OR_FIGCAPTION_OR_FIGURE_OR_FOOTER_OR_HEADER_OR_HGROUP_OR_MAIN_OR_NAV_OR_SEARCH_OR_SECTION_OR_SUMMARY | SPECIAL);
public static final ElementName ARTICLE = new ElementName("article", "article",
// CPPONLY: NS_NewHTMLElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.ADDRESS_OR_ARTICLE_OR_ASIDE_OR_DETAILS_OR_DIALOG_OR_DIR_OR_FIGCAPTION_OR_FIGURE_OR_FOOTER_OR_HEADER_OR_HGROUP_OR_MAIN_OR_NAV_OR_SEARCH_OR_SECTION_OR_SUMMARY | SPECIAL);
public static final ElementName ANIMATE = new ElementName("animate", "animate",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGAnimateElement,
TreeBuilder.OTHER);
public static final ElementName BASE = new ElementName("base", "base",
// CPPONLY: NS_NewHTMLSharedElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.BASE | SPECIAL);
public static final ElementName BLOCKQUOTE = new ElementName("blockquote", "blockquote",
// CPPONLY: NS_NewHTMLSharedElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.DIV_OR_BLOCKQUOTE_OR_CENTER_OR_MENU | SPECIAL);
public static final ElementName CODE = new ElementName("code", "code",
// CPPONLY: NS_NewHTMLElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.B_OR_BIG_OR_CODE_OR_EM_OR_I_OR_S_OR_SMALL_OR_STRIKE_OR_STRONG_OR_TT_OR_U);
public static final ElementName CIRCLE = new ElementName("circle", "circle",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGCircleElement,
TreeBuilder.OTHER);
public static final ElementName CITE = new ElementName("cite", "cite",
// CPPONLY: NS_NewHTMLElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.OTHER);
public static final ElementName ELLIPSE = new ElementName("ellipse", "ellipse",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGEllipseElement,
TreeBuilder.OTHER);
public static final ElementName FETURBULENCE = new ElementName("feturbulence", "feTurbulence",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGFETurbulenceElement,
TreeBuilder.OTHER);
public static final ElementName FEMERGENODE = new ElementName("femergenode", "feMergeNode",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGFEMergeNodeElement,
TreeBuilder.OTHER);
public static final ElementName FEIMAGE = new ElementName("feimage", "feImage",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGFEImageElement,
TreeBuilder.OTHER);
public static final ElementName FEMERGE = new ElementName("femerge", "feMerge",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGFEMergeElement,
TreeBuilder.OTHER);
public static final ElementName FETILE = new ElementName("fetile", "feTile",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGFETileElement,
TreeBuilder.OTHER);
public static final ElementName FRAME = new ElementName("frame", "frame",
// CPPONLY: NS_NewHTMLFrameElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.FRAME | SPECIAL);
public static final ElementName FIGURE = new ElementName("figure", "figure",
// CPPONLY: NS_NewHTMLElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.ADDRESS_OR_ARTICLE_OR_ASIDE_OR_DETAILS_OR_DIALOG_OR_DIR_OR_FIGCAPTION_OR_FIGURE_OR_FOOTER_OR_HEADER_OR_HGROUP_OR_MAIN_OR_NAV_OR_SEARCH_OR_SECTION_OR_SUMMARY | SPECIAL);
public static final ElementName FECOMPOSITE = new ElementName("fecomposite", "feComposite",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGFECompositeElement,
TreeBuilder.OTHER);
public static final ElementName IMAGE = new ElementName("image", "image",
// CPPONLY: NS_NewHTMLElement,
// CPPONLY: NS_NewSVGImageElement,
TreeBuilder.IMAGE);
public static final ElementName IFRAME = new ElementName("iframe", "iframe",
// CPPONLY: NS_NewHTMLIFrameElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.IFRAME | SPECIAL);
public static final ElementName LINE = new ElementName("line", "line",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGLineElement,
TreeBuilder.OTHER);
public static final ElementName MARQUEE = new ElementName("marquee", "marquee",
// CPPONLY: NS_NewHTMLMarqueeElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.MARQUEE_OR_APPLET | SPECIAL | SCOPING);
public static final ElementName POLYLINE = new ElementName("polyline", "polyline",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGPolylineElement,
TreeBuilder.OTHER);
public static final ElementName PICTURE = new ElementName("picture", "picture",
// CPPONLY: NS_NewHTMLPictureElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.OTHER);
public static final ElementName SOURCE = new ElementName("source", "source",
// CPPONLY: NS_NewHTMLSourceElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.PARAM_OR_SOURCE_OR_TRACK);
public static final ElementName STRIKE = new ElementName("strike", "strike",
// CPPONLY: NS_NewHTMLElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.B_OR_BIG_OR_CODE_OR_EM_OR_I_OR_S_OR_SMALL_OR_STRIKE_OR_STRONG_OR_TT_OR_U);
public static final ElementName STYLE = new ElementName("style", "style",
// CPPONLY: NS_NewHTMLStyleElement,
// CPPONLY: NS_NewSVGStyleElement,
TreeBuilder.STYLE | SPECIAL);
public static final ElementName TABLE = new ElementName("table", "table",
// CPPONLY: NS_NewHTMLTableElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.TABLE | SPECIAL | FOSTER_PARENTING | SCOPING);
public static final ElementName TITLE = new ElementName("title", "title",
// CPPONLY: NS_NewHTMLTitleElement,
// CPPONLY: NS_NewSVGTitleElement,
TreeBuilder.TITLE | SPECIAL | SCOPING_AS_SVG);
public static final ElementName TIME = new ElementName("time", "time",
// CPPONLY: NS_NewHTMLTimeElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.OTHER);
public static final ElementName TEMPLATE = new ElementName("template", "template",
// CPPONLY: NS_NewHTMLTemplateElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.TEMPLATE | SPECIAL | SCOPING);
public static final ElementName ALTGLYPHDEF = new ElementName("altglyphdef", "altGlyphDef",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.OTHER);
public static final ElementName GLYPHREF = new ElementName("glyphref", "glyphRef",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.OTHER);
public static final ElementName DIALOG = new ElementName("dialog", "dialog",
// CPPONLY: NS_NewHTMLDialogElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.ADDRESS_OR_ARTICLE_OR_ASIDE_OR_DETAILS_OR_DIALOG_OR_DIR_OR_FIGCAPTION_OR_FIGURE_OR_FOOTER_OR_HEADER_OR_HGROUP_OR_MAIN_OR_NAV_OR_SEARCH_OR_SECTION_OR_SUMMARY | SPECIAL);
public static final ElementName FEFUNCG = new ElementName("fefuncg", "feFuncG",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGFEFuncGElement,
TreeBuilder.OTHER);
public static final ElementName FEDIFFUSELIGHTING = new ElementName("fediffuselighting", "feDiffuseLighting",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGFEDiffuseLightingElement,
TreeBuilder.OTHER);
public static final ElementName FESPECULARLIGHTING = new ElementName("fespecularlighting", "feSpecularLighting",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGFESpecularLightingElement,
TreeBuilder.OTHER);
public static final ElementName LISTING = new ElementName("listing", "listing",
// CPPONLY: NS_NewHTMLPreElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.PRE_OR_LISTING | SPECIAL);
public static final ElementName ALTGLYPH = new ElementName("altglyph", "altGlyph",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.OTHER);
public static final ElementName CLIPPATH = new ElementName("clippath", "clipPath",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGClipPathElement,
TreeBuilder.OTHER);
public static final ElementName MGLYPH = new ElementName("mglyph", "mglyph",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.MGLYPH_OR_MALIGNMARK);
public static final ElementName MATH = new ElementName("math", "math",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.MATH);
public static final ElementName MPATH = new ElementName("mpath", "mpath",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGMPathElement,
TreeBuilder.OTHER);
public static final ElementName PATH = new ElementName("path", "path",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGPathElement,
TreeBuilder.OTHER);
public static final ElementName TH = new ElementName("th", "th",
// CPPONLY: NS_NewHTMLTableCellElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.TD_OR_TH | SPECIAL | SCOPING | OPTIONAL_END_TAG);
public static final ElementName SEARCH = new ElementName("search", "search",
// CPPONLY: NS_NewHTMLElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.ADDRESS_OR_ARTICLE_OR_ASIDE_OR_DETAILS_OR_DIALOG_OR_DIR_OR_FIGCAPTION_OR_FIGURE_OR_FOOTER_OR_HEADER_OR_HGROUP_OR_MAIN_OR_NAV_OR_SEARCH_OR_SECTION_OR_SUMMARY | SPECIAL);
public static final ElementName SWITCH = new ElementName("switch", "switch",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGSwitchElement,
TreeBuilder.OTHER);
public static final ElementName TEXTPATH = new ElementName("textpath", "textPath",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGTextPathElement,
TreeBuilder.OTHER);
public static final ElementName MI = new ElementName("mi", "mi",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.MI_MO_MN_MS_MTEXT | SCOPING_AS_MATHML);
public static final ElementName LINK = new ElementName("link", "link",
// CPPONLY: NS_NewHTMLLinkElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.LINK_OR_BASEFONT_OR_BGSOUND | SPECIAL);
public static final ElementName MARK = new ElementName("mark", "mark",
// CPPONLY: NS_NewHTMLElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.OTHER);
public static final ElementName MALIGNMARK = new ElementName("malignmark", "malignmark",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.MGLYPH_OR_MALIGNMARK);
public static final ElementName MASK = new ElementName("mask", "mask",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGMaskElement,
TreeBuilder.OTHER);
public static final ElementName TRACK = new ElementName("track", "track",
// CPPONLY: NS_NewHTMLTrackElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.PARAM_OR_SOURCE_OR_TRACK | SPECIAL);
public static final ElementName DL = new ElementName("dl", "dl",
// CPPONLY: NS_NewHTMLSharedListElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.UL_OR_OL_OR_DL | SPECIAL);
public static final ElementName HTML = new ElementName("html", "html",
// CPPONLY: NS_NewHTMLSharedElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.HTML | SPECIAL | SCOPING | OPTIONAL_END_TAG);
public static final ElementName OL = new ElementName("ol", "ol",
// CPPONLY: NS_NewHTMLSharedListElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.UL_OR_OL_OR_DL | SPECIAL);
public static final ElementName SMALL = new ElementName("small", "small",
// CPPONLY: NS_NewHTMLElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.B_OR_BIG_OR_CODE_OR_EM_OR_I_OR_S_OR_SMALL_OR_STRIKE_OR_STRONG_OR_TT_OR_U);
public static final ElementName SYMBOL = new ElementName("symbol", "symbol",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGSymbolElement,
TreeBuilder.OTHER);
public static final ElementName ALTGLYPHITEM = new ElementName("altglyphitem", "altGlyphItem",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.OTHER);
public static final ElementName ANIMATETRANSFORM = new ElementName("animatetransform", "animateTransform",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGAnimateTransformElement,
TreeBuilder.OTHER);
public static final ElementName ACRONYM = new ElementName("acronym", "acronym",
// CPPONLY: NS_NewHTMLElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.OTHER);
public static final ElementName EM = new ElementName("em", "em",
// CPPONLY: NS_NewHTMLElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.B_OR_BIG_OR_CODE_OR_EM_OR_I_OR_S_OR_SMALL_OR_STRIKE_OR_STRONG_OR_TT_OR_U);
public static final ElementName FORM = new ElementName("form", "form",
// CPPONLY: NS_NewHTMLFormElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.FORM | SPECIAL);
public static final ElementName PARAM = new ElementName("param", "param",
// CPPONLY: NS_NewHTMLSharedElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.PARAM_OR_SOURCE_OR_TRACK | SPECIAL);
public static final ElementName ANIMATEMOTION = new ElementName("animatemotion", "animateMotion",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGAnimateMotionElement,
TreeBuilder.OTHER);
public static final ElementName CAPTION = new ElementName("caption", "caption",
// CPPONLY: NS_NewHTMLTableCaptionElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.CAPTION | SPECIAL | SCOPING);
public static final ElementName FIGCAPTION = new ElementName("figcaption", "figcaption",
// CPPONLY: NS_NewHTMLElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.ADDRESS_OR_ARTICLE_OR_ASIDE_OR_DETAILS_OR_DIALOG_OR_DIR_OR_FIGCAPTION_OR_FIGURE_OR_FOOTER_OR_HEADER_OR_HGROUP_OR_MAIN_OR_NAV_OR_SEARCH_OR_SECTION_OR_SUMMARY | SPECIAL);
public static final ElementName MN = new ElementName("mn", "mn",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.MI_MO_MN_MS_MTEXT | SCOPING_AS_MATHML);
public static final ElementName KEYGEN = new ElementName("keygen", "keygen",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.KEYGEN | SPECIAL);
public static final ElementName MAIN = new ElementName("main", "main",
// CPPONLY: NS_NewHTMLElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.ADDRESS_OR_ARTICLE_OR_ASIDE_OR_DETAILS_OR_DIALOG_OR_DIR_OR_FIGCAPTION_OR_FIGURE_OR_FOOTER_OR_HEADER_OR_HGROUP_OR_MAIN_OR_NAV_OR_SEARCH_OR_SECTION_OR_SUMMARY | SPECIAL);
public static final ElementName OPTION = new ElementName("option", "option",
// CPPONLY: NS_NewHTMLOptionElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.OPTION | OPTIONAL_END_TAG);
public static final ElementName POLYGON = new ElementName("polygon", "polygon",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGPolygonElement,
TreeBuilder.OTHER);
public static final ElementName PATTERN = new ElementName("pattern", "pattern",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGPatternElement,
TreeBuilder.OTHER);
public static final ElementName SECTION = new ElementName("section", "section",
// CPPONLY: NS_NewHTMLElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.ADDRESS_OR_ARTICLE_OR_ASIDE_OR_DETAILS_OR_DIALOG_OR_DIR_OR_FIGCAPTION_OR_FIGURE_OR_FOOTER_OR_HEADER_OR_HGROUP_OR_MAIN_OR_NAV_OR_SEARCH_OR_SECTION_OR_SUMMARY | SPECIAL);
public static final ElementName TSPAN = new ElementName("tspan", "tspan",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGTSpanElement,
TreeBuilder.OTHER);
public static final ElementName AUDIO = new ElementName("audio", "audio",
// CPPONLY: NS_NewHTMLAudioElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.OTHER);
public static final ElementName MO = new ElementName("mo", "mo",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.MI_MO_MN_MS_MTEXT | SCOPING_AS_MATHML);
public static final ElementName VIDEO = new ElementName("video", "video",
// CPPONLY: NS_NewHTMLVideoElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.OTHER);
public static final ElementName COLGROUP = new ElementName("colgroup", "colgroup",
// CPPONLY: NS_NewHTMLTableColElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.COLGROUP | SPECIAL | OPTIONAL_END_TAG);
public static final ElementName FEDISPLACEMENTMAP = new ElementName("fedisplacementmap", "feDisplacementMap",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGFEDisplacementMapElement,
TreeBuilder.OTHER);
public static final ElementName HGROUP = new ElementName("hgroup", "hgroup",
// CPPONLY: NS_NewHTMLElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.ADDRESS_OR_ARTICLE_OR_ASIDE_OR_DETAILS_OR_DIALOG_OR_DIR_OR_FIGCAPTION_OR_FIGURE_OR_FOOTER_OR_HEADER_OR_HGROUP_OR_MAIN_OR_NAV_OR_SEARCH_OR_SECTION_OR_SUMMARY | SPECIAL);
public static final ElementName RP = new ElementName("rp", "rp",
// CPPONLY: NS_NewHTMLElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.RT_OR_RP | OPTIONAL_END_TAG);
public static final ElementName OPTGROUP = new ElementName("optgroup", "optgroup",
// CPPONLY: NS_NewHTMLOptGroupElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.OPTGROUP | OPTIONAL_END_TAG);
public static final ElementName SAMP = new ElementName("samp", "samp",
// CPPONLY: NS_NewHTMLElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.OTHER);
public static final ElementName STOP = new ElementName("stop", "stop",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGStopElement,
TreeBuilder.OTHER);
public static final ElementName BR = new ElementName("br", "br",
// CPPONLY: NS_NewHTMLBRElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.BR | SPECIAL);
public static final ElementName ABBR = new ElementName("abbr", "abbr",
// CPPONLY: NS_NewHTMLElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.OTHER);
public static final ElementName ANIMATECOLOR = new ElementName("animatecolor", "animateColor",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.OTHER);
public static final ElementName CENTER = new ElementName("center", "center",
// CPPONLY: NS_NewHTMLElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.DIV_OR_BLOCKQUOTE_OR_CENTER_OR_MENU | SPECIAL);
public static final ElementName HR = new ElementName("hr", "hr",
// CPPONLY: NS_NewHTMLHRElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.HR | SPECIAL);
public static final ElementName FEFUNCR = new ElementName("fefuncr", "feFuncR",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGFEFuncRElement,
TreeBuilder.OTHER);
public static final ElementName FECOMPONENTTRANSFER = new ElementName("fecomponenttransfer", "feComponentTransfer",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGFEComponentTransferElement,
TreeBuilder.OTHER);
public static final ElementName FILTER = new ElementName("filter", "filter",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGFilterElement,
TreeBuilder.OTHER);
public static final ElementName FOOTER = new ElementName("footer", "footer",
// CPPONLY: NS_NewHTMLElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.ADDRESS_OR_ARTICLE_OR_ASIDE_OR_DETAILS_OR_DIALOG_OR_DIR_OR_FIGCAPTION_OR_FIGURE_OR_FOOTER_OR_HEADER_OR_HGROUP_OR_MAIN_OR_NAV_OR_SEARCH_OR_SECTION_OR_SUMMARY | SPECIAL);
public static final ElementName FEGAUSSIANBLUR = new ElementName("fegaussianblur", "feGaussianBlur",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGFEGaussianBlurElement,
TreeBuilder.OTHER);
public static final ElementName HEADER = new ElementName("header", "header",
// CPPONLY: NS_NewHTMLElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.ADDRESS_OR_ARTICLE_OR_ASIDE_OR_DETAILS_OR_DIALOG_OR_DIR_OR_FIGCAPTION_OR_FIGURE_OR_FOOTER_OR_HEADER_OR_HGROUP_OR_MAIN_OR_NAV_OR_SEARCH_OR_SECTION_OR_SUMMARY | SPECIAL);
public static final ElementName MARKER = new ElementName("marker", "marker",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGMarkerElement,
TreeBuilder.OTHER);
public static final ElementName METER = new ElementName("meter", "meter",
// CPPONLY: NS_NewHTMLMeterElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.OTHER);
public static final ElementName NOBR = new ElementName("nobr", "nobr",
// CPPONLY: NS_NewHTMLElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.NOBR);
public static final ElementName TR = new ElementName("tr", "tr",
// CPPONLY: NS_NewHTMLTableRowElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.TR | SPECIAL | FOSTER_PARENTING | OPTIONAL_END_TAG);
public static final ElementName ADDRESS = new ElementName("address", "address",
// CPPONLY: NS_NewHTMLElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.ADDRESS_OR_ARTICLE_OR_ASIDE_OR_DETAILS_OR_DIALOG_OR_DIR_OR_FIGCAPTION_OR_FIGURE_OR_FOOTER_OR_HEADER_OR_HGROUP_OR_MAIN_OR_NAV_OR_SEARCH_OR_SECTION_OR_SUMMARY | SPECIAL);
public static final ElementName CANVAS = new ElementName("canvas", "canvas",
// CPPONLY: NS_NewHTMLCanvasElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.OTHER);
public static final ElementName DEFS = new ElementName("defs", "defs",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGDefsElement,
TreeBuilder.OTHER);
public static final ElementName DETAILS = new ElementName("details", "details",
// CPPONLY: NS_NewHTMLDetailsElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.ADDRESS_OR_ARTICLE_OR_ASIDE_OR_DETAILS_OR_DIALOG_OR_DIR_OR_FIGCAPTION_OR_FIGURE_OR_FOOTER_OR_HEADER_OR_HGROUP_OR_MAIN_OR_NAV_OR_SEARCH_OR_SECTION_OR_SUMMARY | SPECIAL);
public static final ElementName MS = new ElementName("ms", "ms",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.MI_MO_MN_MS_MTEXT | SCOPING_AS_MATHML);
public static final ElementName NOFRAMES = new ElementName("noframes", "noframes",
// CPPONLY: NS_NewHTMLElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.NOFRAMES | SPECIAL);
public static final ElementName PROGRESS = new ElementName("progress", "progress",
// CPPONLY: NS_NewHTMLProgressElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.OTHER);
public static final ElementName DT = new ElementName("dt", "dt",
// CPPONLY: NS_NewHTMLElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.DD_OR_DT | SPECIAL | OPTIONAL_END_TAG);
public static final ElementName APPLET = new ElementName("applet", "applet",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.MARQUEE_OR_APPLET | SPECIAL | SCOPING);
public static final ElementName BASEFONT = new ElementName("basefont", "basefont",
// CPPONLY: NS_NewHTMLElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.LINK_OR_BASEFONT_OR_BGSOUND | SPECIAL);
public static final ElementName DATALIST = new ElementName("datalist", "datalist",
// CPPONLY: NS_NewHTMLDataListElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.OTHER);
public static final ElementName FOREIGNOBJECT = new ElementName("foreignobject", "foreignObject",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGForeignObjectElement,
TreeBuilder.FOREIGNOBJECT_OR_DESC | SCOPING_AS_SVG);
public static final ElementName FIELDSET = new ElementName("fieldset", "fieldset",
// CPPONLY: NS_NewHTMLFieldSetElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.FIELDSET | SPECIAL);
public static final ElementName FRAMESET = new ElementName("frameset", "frameset",
// CPPONLY: NS_NewHTMLFrameSetElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.FRAMESET | SPECIAL);
public static final ElementName FEOFFSET = new ElementName("feoffset", "feOffset",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGFEOffsetElement,
TreeBuilder.OTHER);
public static final ElementName FESPOTLIGHT = new ElementName("fespotlight", "feSpotLight",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGFESpotLightElement,
TreeBuilder.OTHER);
public static final ElementName FEPOINTLIGHT = new ElementName("fepointlight", "fePointLight",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGFEPointLightElement,
TreeBuilder.OTHER);
public static final ElementName FEDISTANTLIGHT = new ElementName("fedistantlight", "feDistantLight",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGFEDistantLightElement,
TreeBuilder.OTHER);
public static final ElementName FONT = new ElementName("font", "font",
// CPPONLY: NS_NewHTMLFontElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.FONT);
public static final ElementName LINEARGRADIENT = new ElementName("lineargradient", "linearGradient",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGLinearGradientElement,
TreeBuilder.OTHER);
public static final ElementName MTEXT = new ElementName("mtext", "mtext",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.MI_MO_MN_MS_MTEXT | SCOPING_AS_MATHML);
public static final ElementName NOSCRIPT = new ElementName("noscript", "noscript",
// CPPONLY: NS_NewHTMLElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.NOSCRIPT | SPECIAL);
public static final ElementName RT = new ElementName("rt", "rt",
// CPPONLY: NS_NewHTMLElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.RT_OR_RP | OPTIONAL_END_TAG);
public static final ElementName OBJECT = new ElementName("object", "object",
// CPPONLY: NS_NewHTMLObjectElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.OBJECT | SPECIAL | SCOPING);
public static final ElementName OUTPUT = new ElementName("output", "output",
// CPPONLY: NS_NewHTMLOutputElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.OUTPUT);
public static final ElementName PLAINTEXT = new ElementName("plaintext", "plaintext",
// CPPONLY: NS_NewHTMLElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.PLAINTEXT | SPECIAL);
public static final ElementName TT = new ElementName("tt", "tt",
// CPPONLY: NS_NewHTMLElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.B_OR_BIG_OR_CODE_OR_EM_OR_I_OR_S_OR_SMALL_OR_STRIKE_OR_STRONG_OR_TT_OR_U);
public static final ElementName RECT = new ElementName("rect", "rect",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGRectElement,
TreeBuilder.OTHER);
public static final ElementName RADIALGRADIENT = new ElementName("radialgradient", "radialGradient",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGRadialGradientElement,
TreeBuilder.OTHER);
public static final ElementName SELECT = new ElementName("select", "select",
// CPPONLY: NS_NewHTMLSelectElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.SELECT | SPECIAL | SCOPING);
public static final ElementName SELECTEDCONTENT = new ElementName("selectedcontent", "selectedcontent",
// CPPONLY: NS_NewHTMLSelectedContentElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.OTHER);
public static final ElementName SLOT = new ElementName("slot", "slot",
// CPPONLY: NS_NewHTMLSlotElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.OTHER);
public static final ElementName SCRIPT = new ElementName("script", "script",
// CPPONLY: NS_NewHTMLScriptElement,
// CPPONLY: NS_NewSVGScriptElement,
TreeBuilder.SCRIPT | SPECIAL);
public static final ElementName TFOOT = new ElementName("tfoot", "tfoot",
// CPPONLY: NS_NewHTMLTableSectionElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.TBODY_OR_THEAD_OR_TFOOT | SPECIAL | FOSTER_PARENTING | OPTIONAL_END_TAG);
public static final ElementName TEXT = new ElementName("text", "text",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGTextElement,
TreeBuilder.OTHER);
public static final ElementName MENU = new ElementName("menu", "menu",
// CPPONLY: NS_NewHTMLMenuElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.DIV_OR_BLOCKQUOTE_OR_CENTER_OR_MENU | SPECIAL);
public static final ElementName FEDROPSHADOW = new ElementName("fedropshadow", "feDropShadow",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGFEDropShadowElement,
TreeBuilder.OTHER);
public static final ElementName VIEW = new ElementName("view", "view",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGViewElement,
TreeBuilder.OTHER);
public static final ElementName FECOLORMATRIX = new ElementName("fecolormatrix", "feColorMatrix",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGFEColorMatrixElement,
TreeBuilder.OTHER);
public static final ElementName FECONVOLVEMATRIX = new ElementName("feconvolvematrix", "feConvolveMatrix",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGFEConvolveMatrixElement,
TreeBuilder.OTHER);
public static final ElementName BODY = new ElementName("body", "body",
// CPPONLY: NS_NewHTMLBodyElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.BODY | SPECIAL | OPTIONAL_END_TAG);
public static final ElementName FEMORPHOLOGY = new ElementName("femorphology", "feMorphology",
// CPPONLY: NS_NewHTMLUnknownElement,
// CPPONLY: NS_NewSVGFEMorphologyElement,
TreeBuilder.OTHER);
public static final ElementName RUBY = new ElementName("ruby", "ruby",
// CPPONLY: NS_NewHTMLElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.RUBY_OR_SPAN_OR_SUB_OR_SUP_OR_VAR);
public static final ElementName SUMMARY = new ElementName("summary", "summary",
// CPPONLY: NS_NewHTMLSummaryElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.ADDRESS_OR_ARTICLE_OR_ASIDE_OR_DETAILS_OR_DIALOG_OR_DIR_OR_FIGCAPTION_OR_FIGURE_OR_FOOTER_OR_HEADER_OR_HGROUP_OR_MAIN_OR_NAV_OR_SEARCH_OR_SECTION_OR_SUMMARY | SPECIAL);
public static final ElementName TBODY = new ElementName("tbody", "tbody",
// CPPONLY: NS_NewHTMLTableSectionElement,
// CPPONLY: NS_NewSVGUnknownElement,
TreeBuilder.TBODY_OR_THEAD_OR_TFOOT | SPECIAL | FOSTER_PARENTING | OPTIONAL_END_TAG);
private final static @NoLength ElementName[] ELEMENT_NAMES = {
AUDIO,
FEIMAGE,
FRAMESET,
H5,
TH,
METER,
SELECT,
SVG,
HEAD,
TITLE,
ANIMATETRANSFORM,
HR,
NOFRAMES,
NOSCRIPT,
VIEW,
INS,
I,
FEFUNCB,
BASE,
LINE,
FESPECULARLIGHTING,
MASK,
MN,
SAMP,
FOOTER,
CANVAS,
BASEFONT,
FEDISTANTLIGHT,
PLAINTEXT,
TFOOT,
FEMORPHOLOGY,
COL,
PRE,
VAR,
H1,
FEFUNCA,
BGSOUND,
THEAD,
CITE,
FIGURE,
SOURCE,
GLYPHREF,
MGLYPH,
MI,
OL,
PARAM,
POLYGON,
FEDISPLACEMENTMAP,
ANIMATECOLOR,
FECOMPONENTTRANSFER,
HEADER,
TR,
DETAILS,
DT,
FOREIGNOBJECT,
FESPOTLIGHT,
LINEARGRADIENT,
OBJECT,
RECT,
SLOT,
MENU,
FECONVOLVEMATRIX,
SUMMARY,
BDI,
DFN,
MAP,
RTC,
SET,
WBR,
S,
H3,
AREA,
META,
DESC,
FEBLEND,
NOEMBED,
ARTICLE,
CODE,
FETURBULENCE,
FETILE,
IMAGE,
POLYLINE,
STYLE,
TEMPLATE,
FEFUNCG,
ALTGLYPH,
MPATH,
SWITCH,
MARK,
DL,
SYMBOL,
EM,
CAPTION,
MAIN,
SECTION,
VIDEO,
RP,
BR,
CENTER,
FEFUNCR,
FILTER,
FEGAUSSIANBLUR,
MARKER,
NOBR,
ADDRESS,
DEFS,
MS,
PROGRESS,
APPLET,
DATALIST,
FIELDSET,
FEOFFSET,
FEPOINTLIGHT,
FONT,
MTEXT,
RT,
OUTPUT,
TT,
RADIALGRADIENT,
SELECTEDCONTENT,
SCRIPT,
TEXT,
FEDROPSHADOW,
FECOLORMATRIX,
BODY,
RUBY,
TBODY,
BIG,
BDO,
DEL,
DIR,
KBD,
NAV,
B,
SUB,
SUP,
USE,
G,
XMP,
Q,
U,
H2,
H4,
H6,
DATA,
METADATA,
TEXTAREA,
RB,
DD,
EMBED,
FEFLOOD,
LEGEND,
TD,
ASIDE,
ANIMATE,
BLOCKQUOTE,
CIRCLE,
ELLIPSE,
FEMERGENODE,
FEMERGE,
FRAME,
FECOMPOSITE,
IFRAME,
MARQUEE,
PICTURE,
STRIKE,
TABLE,
TIME,
ALTGLYPHDEF,
DIALOG,
FEDIFFUSELIGHTING,
LISTING,
CLIPPATH,
MATH,
PATH,
SEARCH,
TEXTPATH,
LINK,
MALIGNMARK,
TRACK,
HTML,
SMALL,
ALTGLYPHITEM,
ACRONYM,
FORM,
ANIMATEMOTION,
FIGCAPTION,
KEYGEN,
OPTION,
PATTERN,
TSPAN,
MO,
COLGROUP,
HGROUP,
OPTGROUP,
STOP,
ABBR,
};
private final static int[] ELEMENT_HASHES = {
1914900309,
1749715159,
2001349720,
943718402,
1805647874,
1971465813,
2008125638,
60347747,
1733890180,
1757146773,
1881498736,
1967128578,
1988763672,
2005719336,
2060065124,
55110883,
63438849,
1699324759,
1747814436,
1752979652,
1783388498,
1854245076,
1902641154,
1941178676,
1967795958,
1982935782,
1999397992,
2001392798,
2006896969,
2008851557,
2085266636,
51961587,
58773795,
61925907,
876609538,
1682547543,
1730965751,
1740181637,
1748359220,
1749905526,
1756474198,
1766992520,
1803876550,
1818755074,
1870135298,
1889085973,
1906087319,
1934172497,
1965334268,
1967788867,
1968836118,
1973420034,
1983633431,
1998585858,
2001309869,
2001392795,
2004635806,
2006028454,
2007601444,
2008325940,
2021937364,
2068523856,
2092255447,
51435587,
52486755,
57206291,
59821379,
60354131,
62450211,
68681729,
910163970,
1679960596,
1686491348,
1715310660,
1733054663,
1737099991,
1747176599,
1748100148,
1749656156,
1749801286,
1751288021,
1755076808,
1756625221,
1757268168,
1783210839,
1797585096,
1803929861,
1806806678,
1854228692,
1864368130,
1874102998,
1881669634,
1899272519,
1904412884,
1907661127,
1925844629,
1938817026,
1963982850,
1966223078,
1967760215,
1967795910,
1968053806,
1971461414,
1971938532,
1982173479,
1983533124,
1986527234,
1990037800,
1998724870,
2000525512,
2001349704,
2001349736,
2001392796,
2001495140,
2005324101,
2005925890,
2006329158,
2006974466,
2007781534,
2008305999,
2008340774,
2008994116,
2051837468,
2068523853,
2083120164,
2091479332,
2092557349,
51434643,
51438659,
52485715,
52488851,
56151587,
57733651,
59768833,
60345171,
60352339,
61395251,
62390273,
62973651,
67633153,
69730305,
893386754,
926941186,
960495618,
1681770564,
1686489160,
1689922072,
1703936002,
1730150402,
1732381397,
1733076167,
1736200310,
1738539010,
1747048757,
1747306711,
1747838298,
1748225318,
1749395095,
1749673195,
1749723735,
1749813541,
1749932347,
1751386406,
1753362711,
1755148615,
1756600614,
1757137429,
1757157700,
1763839627,
1782357526,
1783388497,
1786534215,
1798686984,
1803929812,
1805502724,
1806805526,
1807599880,
1853642948,
1854228698,
1857653029,
1868312196,
1874053333,
1881288348,
1881613047,
1884120164,
1898223949,
1900845386,
1903302038,
1905563974,
1906135367,
1907959605,
1919418370,
1932928296,
1935549734,
1939219752,
1941221172,
1965115924,
};
}
