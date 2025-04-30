/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

const {
  accessibility: {
    AUDIT_TYPE,
    ISSUE_TYPE: {
      [AUDIT_TYPE.KEYBOARD]: {
        FOCUSABLE_NO_SEMANTICS,
        FOCUSABLE_POSITIVE_TABINDEX,
        INTERACTIVE_NO_ACTION,
        INTERACTIVE_NOT_FOCUSABLE,
        MOUSE_INTERACTIVE_ONLY,
        NO_FOCUS_VISIBLE,
      },
      [AUDIT_TYPE.TEXT_LABEL]: {
        AREA_NO_NAME_FROM_ALT,
        DIALOG_NO_NAME,
        DOCUMENT_NO_TITLE,
        EMBED_NO_NAME,
        FIGURE_NO_NAME,
        FORM_FIELDSET_NO_NAME,
        FORM_FIELDSET_NO_NAME_FROM_LEGEND,
        FORM_NO_NAME,
        FORM_NO_VISIBLE_NAME,
        FORM_OPTGROUP_NO_NAME_FROM_LABEL,
        FRAME_NO_NAME,
        HEADING_NO_CONTENT,
        HEADING_NO_NAME,
        IFRAME_NO_NAME_FROM_TITLE,
        IMAGE_NO_NAME,
        INTERACTIVE_NO_NAME,
        MATHML_GLYPH_NO_NAME,
        TOOLBAR_NO_NAME,
      },
    },
  },
} = require("resource://devtools/shared/constants.js");

// Used in accessible component for properties tree rendering.
exports.TREE_ROW_HEIGHT = 21;

// Initial sidebar width.
exports.SIDEBAR_WIDTH = "350px";

// When value is updated either in the tree or sidebar.
exports.VALUE_FLASHING_DURATION = 500;
// When new row is selected, flash highlighter.
exports.VALUE_HIGHLIGHT_DURATION = 1000;

// If the panel width is smaller than given amount of pixels,
// the sidebar automatically switches from 'landscape' to 'portrait' mode.
exports.PORTRAIT_MODE_WIDTH = 700;

// Names for Redux actions.
exports.FETCH_CHILDREN = "FETCH_CHILDREN";
exports.UPDATE_DETAILS = "UPDATE_DETAILS";
exports.RESET = "RESET";
exports.SELECT = "SELECT";
exports.HIGHLIGHT = "HIGHLIGHT";
exports.UNHIGHLIGHT = "UNHIGHLIGHT";
exports.ENABLE = "ENABLE";
exports.UPDATE_CAN_BE_DISABLED = "UPDATE_CAN_BE_DISABLED";
exports.UPDATE_CAN_BE_ENABLED = "UPDATE_CAN_BE_ENABLED";
exports.UPDATE_PREF = "UPDATE_PREF";
exports.FILTER_TOGGLE = "FILTER_TOGGLE";
exports.AUDIT = "AUDIT";
exports.AUDITING = "AUDITING";
exports.AUDIT_PROGRESS = "AUDIT_PROGRESS";
exports.SIMULATE = "SIMULATE";
exports.UPDATE_DISPLAY_TABBING_ORDER = "UPDATE_DISPLAY_TABBING_ORDER";

// List of filters for accessibility checks.
exports.FILTERS = {
  NONE: "NONE",
  ALL: "ALL",
  [AUDIT_TYPE.CONTRAST]: "CONTRAST",
  [AUDIT_TYPE.KEYBOARD]: "KEYBOARD",
  [AUDIT_TYPE.TEXT_LABEL]: "TEXT_LABEL",
};

// Ordered accessible properties to be displayed by the accessible component.
exports.ORDERED_PROPS = [
  "name",
  "role",
  "actions",
  "value",
  "DOMNode",
  "description",
  "keyboardShortcut",
  "childCount",
  "indexInParent",
  "states",
  "relations",
  "attributes",
];

// Accessible events (emitted by accessible front) that the accessible component
// listens to for a current accessible.
exports.ACCESSIBLE_EVENTS = [
  "actions-change",
  "attributes-change",
  "description-change",
  "name-change",
  "reorder",
  "shortcut-change",
  "states-change",
  "text-change",
  "value-change",
  "index-in-parent-change",
];

// URL constants
exports.A11Y_LEARN_MORE_LINK =
  "https://firefox-source-docs.mozilla.org/devtools-user/accessibility_inspector/";
exports.A11Y_CONTRAST_LEARN_MORE_LINK =
  "https://developer.mozilla.org/docs/Web/Accessibility/Guides/Understanding_WCAG/Perceivable/" +
  "Color_contrast?utm_source=devtools&utm_medium=a11y-panel-checks-color-contrast";
exports.A11Y_SIMULATION_DOCUMENTATION_LINK =
  "https://firefox-source-docs.mozilla.org/devtools-user/accessibility_inspector/simulation/";

const A11Y_TEXT_LABEL_LINK_BASE =
  "https://developer.mozilla.org/docs/Web/Accessibility/Guides/Understanding_WCAG/Text_labels_and_names" +
  "?utm_source=devtools&utm_medium=a11y-panel-checks-text-label";

const A11Y_TEXT_LABEL_LINK_IDS = {
  [AREA_NO_NAME_FROM_ALT]:
    "Use_alt_attribute_to_label_area_elements_that_have_the_href_attribute",
  [DIALOG_NO_NAME]: "Dialogs_should_be_labeled",
  [DOCUMENT_NO_TITLE]: "Documents_must_have_a_title",
  [EMBED_NO_NAME]: "Embedded_content_must_be_labeled",
  [FIGURE_NO_NAME]: "Figures_with_optional_captions_should_be_labeled",
  [FORM_FIELDSET_NO_NAME]: "Fieldset_elements_must_be_labeled",
  [FORM_FIELDSET_NO_NAME_FROM_LEGEND]: "Use_a_legend_to_label_a_fieldset",
  [FORM_NO_NAME]: "Form_elements_must_be_labeled",
  [FORM_NO_VISIBLE_NAME]: "Form_elements_should_have_a_visible_text_label",
  [FORM_OPTGROUP_NO_NAME_FROM_LABEL]:
    "Use_label_attribute_on_optgroup_elements",
  [FRAME_NO_NAME]: "Frame_elements_must_be_labeled",
  [HEADING_NO_NAME]: "Headings_must_be_labeled",
  [HEADING_NO_CONTENT]: "Headings_should_have_visible_text_content",
  [IFRAME_NO_NAME_FROM_TITLE]: "Use_title_attribute_to_describe_iframe_content",
  [IMAGE_NO_NAME]: "Content_with_images_must_be_labeled",
  [INTERACTIVE_NO_NAME]: "Interactive_elements_must_be_labeled",
  [MATHML_GLYPH_NO_NAME]: "Use_alt_attribute_to_label_mglyph_elements",
  [TOOLBAR_NO_NAME]:
    "Toolbars_must_be_labeled_when_there_is_more_than_one_toolbar",
};

const A11Y_TEXT_LABEL_LINKS = {};
for (const key in A11Y_TEXT_LABEL_LINK_IDS) {
  A11Y_TEXT_LABEL_LINKS[key] =
    `${A11Y_TEXT_LABEL_LINK_BASE}#${A11Y_TEXT_LABEL_LINK_IDS[key]}`;
}
exports.A11Y_TEXT_LABEL_LINKS = A11Y_TEXT_LABEL_LINKS;

const A11Y_KEYBOARD_LINK_BASE =
  "https://developer.mozilla.org/docs/Web/Accessibility/Guides/Understanding_WCAG/Keyboard" +
  "?utm_source=devtools&utm_medium=a11y-panel-checks-keyboard";

const A11Y_KEYBOARD_LINK_IDS = {
  [FOCUSABLE_NO_SEMANTICS]:
    "Focusable_elements_should_have_interactive_semantics",
  [FOCUSABLE_POSITIVE_TABINDEX]:
    "Avoid_using_tabindex_attribute_greater_than_zero",
  [INTERACTIVE_NO_ACTION]:
    "Interactive_elements_must_be_able_to_be_activated_using_a_keyboard",
  [INTERACTIVE_NOT_FOCUSABLE]: "Interactive_elements_must_be_focusable",
  [MOUSE_INTERACTIVE_ONLY]:
    "Clickable_elements_must_be_focusable_and_should_have_interactive_semantics",
  [NO_FOCUS_VISIBLE]: "Focusable_element_must_have_focus_styling",
};

const A11Y_KEYBOARD_LINKS = {};
for (const key in A11Y_KEYBOARD_LINK_IDS) {
  A11Y_KEYBOARD_LINKS[key] =
    `${A11Y_KEYBOARD_LINK_BASE}#${A11Y_KEYBOARD_LINK_IDS[key]}`;
}
exports.A11Y_KEYBOARD_LINKS = A11Y_KEYBOARD_LINKS;

// Lists of preference names and keys.
const PREFS = {
  SCROLL_INTO_VIEW: "SCROLL_INTO_VIEW",
};

exports.PREFS = PREFS;
exports.PREF_KEYS = {
  [PREFS.SCROLL_INTO_VIEW]: "devtools.accessibility.scroll-into-view",
};
