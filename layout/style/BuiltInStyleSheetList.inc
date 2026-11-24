/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* list of user agent style sheets that GlobalStyleSheetCache manages */

/*
 * STYLE_SHEET(identifier_, url_, flags_)
 *
 * identifier_
 *   An identifier for the style sheet, suitable for use as an enum class value.
 *
 * url_
 *   The URL of the style sheet.
 *
 * flags_
 *   UserStyleSheetType indicating whether the sheet can be safely placed in
 *   shared memory, and the kind of sheet it is.
 */

STYLE_SHEET(ContentEditable, "resource://gre/res/contenteditable.css", UA)
STYLE_SHEET(CounterStyles, "resource://gre-resources/counterstyles.css", UA)
STYLE_SHEET(Forms, "resource://gre-resources/forms.css", UA)
STYLE_SHEET(HTML, "resource://gre-resources/html.css", UA)
STYLE_SHEET(MathML, "resource://gre-resources/mathml.css", UA)
STYLE_SHEET(NoFrames, "resource://gre-resources/noframes.css", UA)
STYLE_SHEET(Quirk, "resource://gre-resources/quirk.css", UA)
STYLE_SHEET(Scrollbars, "resource://gre-resources/scrollbars.css", UA)
STYLE_SHEET(SVG, "resource://gre/res/svg.css", UA)
STYLE_SHEET(UA, "resource://gre-resources/ua.css", UA)
STYLE_SHEET(XUL, "chrome://global/content/xul.css", UAUnshared)
STYLE_SHEET(AccessibleCaret, "resource://gre-resources/accessiblecaret.css",
            Author)
STYLE_SHEET(Details, "resource://gre-resources/details.css", Author)
STYLE_SHEET(Select, "resource://gre-resources/select.css", Author)
