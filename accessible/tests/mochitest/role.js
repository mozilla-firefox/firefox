/* import-globals-from common.js */

// //////////////////////////////////////////////////////////////////////////////
// Role constants

const ROLE_ALERT = nsIAccessibleRole.ROLE_ALERT;
const ROLE_ARTICLE = nsIAccessibleRole.ROLE_ARTICLE;
const ROLE_ANIMATION = nsIAccessibleRole.ROLE_ANIMATION;
const ROLE_APPLICATION = nsIAccessibleRole.ROLE_APPLICATION;
const ROLE_APP_ROOT = nsIAccessibleRole.ROLE_APP_ROOT;
const ROLE_BLOCKQUOTE = nsIAccessibleRole.ROLE_BLOCKQUOTE;
const ROLE_BUTTONMENU = nsIAccessibleRole.ROLE_BUTTONMENU;
const ROLE_CANVAS = nsIAccessibleRole.ROLE_CANVAS;
const ROLE_CAPTION = nsIAccessibleRole.ROLE_CAPTION;
const ROLE_CELL = nsIAccessibleRole.ROLE_CELL;
const ROLE_CHECKBUTTON = nsIAccessibleRole.ROLE_CHECKBUTTON;
const ROLE_CHECK_MENU_ITEM = nsIAccessibleRole.ROLE_CHECK_MENU_ITEM;
const ROLE_CHROME_WINDOW = nsIAccessibleRole.ROLE_CHROME_WINDOW;
const ROLE_CODE = nsIAccessibleRole.ROLE_CODE;
const ROLE_COLUMNHEADER = nsIAccessibleRole.ROLE_COLUMNHEADER;
const ROLE_COMBOBOX = nsIAccessibleRole.ROLE_COMBOBOX;
const ROLE_COMBOBOX_LIST = nsIAccessibleRole.ROLE_COMBOBOX_LIST;
const ROLE_COMBOBOX_OPTION = nsIAccessibleRole.ROLE_COMBOBOX_OPTION;
const ROLE_COMMENT = nsIAccessibleRole.ROLE_COMMENT;
const ROLE_CONTENT_DELETION = nsIAccessibleRole.ROLE_CONTENT_DELETION;
const ROLE_CONTENT_INSERTION = nsIAccessibleRole.ROLE_CONTENT_INSERTION;
const ROLE_DATE_EDITOR = nsIAccessibleRole.ROLE_DATE_EDITOR;
const ROLE_DEFINITION = nsIAccessibleRole.ROLE_DEFINITION;
const ROLE_DEFINITION_LIST = nsIAccessibleRole.ROLE_DEFINITION_LIST;
const ROLE_DETAILS = nsIAccessibleRole.ROLE_DETAILS;
const ROLE_DIAGRAM = nsIAccessibleRole.ROLE_DIAGRAM;
const ROLE_DIALOG = nsIAccessibleRole.ROLE_DIALOG;
const ROLE_DOCUMENT = nsIAccessibleRole.ROLE_DOCUMENT;
const ROLE_EDITCOMBOBOX = nsIAccessibleRole.ROLE_EDITCOMBOBOX;
const ROLE_EMPHASIS = nsIAccessibleRole.ROLE_EMPHASIS;
const ROLE_ENTRY = nsIAccessibleRole.ROLE_ENTRY;
const ROLE_FIGURE = nsIAccessibleRole.ROLE_FIGURE;
const ROLE_FOOTNOTE = nsIAccessibleRole.ROLE_FOOTNOTE;
const ROLE_FLAT_EQUATION = nsIAccessibleRole.ROLE_FLAT_EQUATION;
const ROLE_FORM = nsIAccessibleRole.ROLE_FORM;
const ROLE_FORM_LANDMARK = nsIAccessibleRole.ROLE_FORM_LANDMARK;
const ROLE_GRAPHIC = nsIAccessibleRole.ROLE_GRAPHIC;
const ROLE_GRID = nsIAccessibleRole.ROLE_GRID;
const ROLE_GRID_CELL = nsIAccessibleRole.ROLE_GRID_CELL;
const ROLE_GROUPING = nsIAccessibleRole.ROLE_GROUPING;
const ROLE_HEADING = nsIAccessibleRole.ROLE_HEADING;
const ROLE_IMAGE_MAP = nsIAccessibleRole.ROLE_IMAGE_MAP;
const ROLE_INTERNAL_FRAME = nsIAccessibleRole.ROLE_INTERNAL_FRAME;
const ROLE_LABEL = nsIAccessibleRole.ROLE_LABEL;
const ROLE_LANDMARK = nsIAccessibleRole.ROLE_LANDMARK;
const ROLE_LINK = nsIAccessibleRole.ROLE_LINK;
const ROLE_LIST = nsIAccessibleRole.ROLE_LIST;
const ROLE_LISTBOX = nsIAccessibleRole.ROLE_LISTBOX;
const ROLE_LISTITEM = nsIAccessibleRole.ROLE_LISTITEM;
const ROLE_LISTITEM_MARKER = nsIAccessibleRole.ROLE_LISTITEM_MARKER;
const ROLE_MARK = nsIAccessibleRole.ROLE_MARK;
const ROLE_MATHML_MATH = nsIAccessibleRole.ROLE_MATHML_MATH;
const ROLE_MATHML_IDENTIFIER = nsIAccessibleRole.ROLE_MATHML_IDENTIFIER;
const ROLE_MATHML_NUMBER = nsIAccessibleRole.ROLE_MATHML_NUMBER;
const ROLE_MATHML_OPERATOR = nsIAccessibleRole.ROLE_MATHML_OPERATOR;
const ROLE_MATHML_TEXT = nsIAccessibleRole.ROLE_MATHML_TEXT;
const ROLE_MATHML_EMPTY_GROUP = nsIAccessibleRole.ROLE_MATHML_EMPTY_GROUP;
const ROLE_MATHML_GLYPH = nsIAccessibleRole.ROLE_MATHML_GLYPH;
const ROLE_MATHML_ROW = nsIAccessibleRole.ROLE_MATHML_ROW;
const ROLE_MATHML_FRACTION = nsIAccessibleRole.ROLE_MATHML_FRACTION;
const ROLE_MATHML_SQUARE_ROOT = nsIAccessibleRole.ROLE_MATHML_SQUARE_ROOT;
const ROLE_MATHML_ROOT = nsIAccessibleRole.ROLE_MATHML_ROOT;
const ROLE_MATHML_FENCED = nsIAccessibleRole.ROLE_MATHML_FENCED;
const ROLE_MATHML_ENCLOSED = nsIAccessibleRole.ROLE_MATHML_ENCLOSED;
const ROLE_MATHML_STYLE = nsIAccessibleRole.ROLE_MATHML_STYLE;
const ROLE_MATHML_SUB = nsIAccessibleRole.ROLE_MATHML_SUB;
const ROLE_MATHML_SUP = nsIAccessibleRole.ROLE_MATHML_SUP;
const ROLE_MATHML_SUB_SUP = nsIAccessibleRole.ROLE_MATHML_SUB_SUP;
const ROLE_MATHML_UNDER = nsIAccessibleRole.ROLE_MATHML_UNDER;
const ROLE_MATHML_OVER = nsIAccessibleRole.ROLE_MATHML_OVER;
const ROLE_MATHML_UNDER_OVER = nsIAccessibleRole.ROLE_MATHML_UNDER_OVER;
const ROLE_MATHML_MULTISCRIPTS = nsIAccessibleRole.ROLE_MATHML_MULTISCRIPTS;
const ROLE_MATHML_TABLE = nsIAccessibleRole.ROLE_MATHML_TABLE;
const ROLE_MATHML_LABELED_ROW = nsIAccessibleRole.ROLE_MATHML_LABELED_ROW;
const ROLE_MATHML_TABLE_ROW = nsIAccessibleRole.ROLE_MATHML_TABLE_ROW;
const ROLE_MATHML_CELL = nsIAccessibleRole.ROLE_MATHML_CELL;
const ROLE_MATHML_LINK = nsIAccessibleRole.ROLE_MATHML_LINK;
const ROLE_MATHML_ERROR = nsIAccessibleRole.ROLE_MATHML_ERROR;
const ROLE_MATHML_STACK = nsIAccessibleRole.ROLE_MATHML_STACK;
const ROLE_MATHML_LONG_DIVISION = nsIAccessibleRole.ROLE_MATHML_LONG_DIVISION;
const ROLE_MATHML_STACK_GROUP = nsIAccessibleRole.ROLE_MATHML_STACK_GROUP;
const ROLE_MATHML_STACK_ROW = nsIAccessibleRole.ROLE_MATHML_STACK_ROW;
const ROLE_MATHML_STACK_CARRIES = nsIAccessibleRole.ROLE_MATHML_STACK_CARRIES;
const ROLE_MATHML_STACK_CARRY = nsIAccessibleRole.ROLE_MATHML_STACK_CARRY;
const ROLE_MATHML_STACK_LINE = nsIAccessibleRole.ROLE_MATHML_STACK_LINE;
const ROLE_MENUBAR = nsIAccessibleRole.ROLE_MENUBAR;
const ROLE_MENUITEM = nsIAccessibleRole.ROLE_MENUITEM;
const ROLE_MENUPOPUP = nsIAccessibleRole.ROLE_MENUPOPUP;
const ROLE_METER = nsIAccessibleRole.ROLE_METER;
const ROLE_NAVIGATION = nsIAccessibleRole.ROLE_NAVIGATION;
const ROLE_NON_NATIVE_DOCUMENT = nsIAccessibleRole.ROLE_NON_NATIVE_DOCUMENT;
const ROLE_NOTHING = nsIAccessibleRole.ROLE_NOTHING;
const ROLE_NOTE = nsIAccessibleRole.ROLE_NOTE;
const ROLE_OPTION = nsIAccessibleRole.ROLE_OPTION;
const ROLE_OUTLINE = nsIAccessibleRole.ROLE_OUTLINE;
const ROLE_OUTLINEITEM = nsIAccessibleRole.ROLE_OUTLINEITEM;
const ROLE_PAGETAB = nsIAccessibleRole.ROLE_PAGETAB;
const ROLE_PAGETABLIST = nsIAccessibleRole.ROLE_PAGETABLIST;
const ROLE_PANE = nsIAccessibleRole.ROLE_PANE;
const ROLE_PARAGRAPH = nsIAccessibleRole.ROLE_PARAGRAPH;
const ROLE_PARENT_MENUITEM = nsIAccessibleRole.ROLE_PARENT_MENUITEM;
const ROLE_PASSWORD_TEXT = nsIAccessibleRole.ROLE_PASSWORD_TEXT;
const ROLE_PROGRESSBAR = nsIAccessibleRole.ROLE_PROGRESSBAR;
const ROLE_PROPERTYPAGE = nsIAccessibleRole.ROLE_PROPERTYPAGE;
const ROLE_PUSHBUTTON = nsIAccessibleRole.ROLE_PUSHBUTTON;
const ROLE_RADIOBUTTON = nsIAccessibleRole.ROLE_RADIOBUTTON;
const ROLE_RADIO_GROUP = nsIAccessibleRole.ROLE_RADIO_GROUP;
const ROLE_RADIO_MENU_ITEM = nsIAccessibleRole.ROLE_RADIO_MENU_ITEM;
const ROLE_REGION = nsIAccessibleRole.ROLE_REGION;
const ROLE_RICH_OPTION = nsIAccessibleRole.ROLE_RICH_OPTION;
const ROLE_ROW = nsIAccessibleRole.ROLE_ROW;
const ROLE_ROWHEADER = nsIAccessibleRole.ROLE_ROWHEADER;
const ROLE_SCROLLBAR = nsIAccessibleRole.ROLE_SCROLLBAR;
const ROLE_SEARCHBOX = nsIAccessibleRole.ROLE_SEARCHBOX;
const ROLE_SECTION = nsIAccessibleRole.ROLE_SECTION;
const ROLE_SEPARATOR = nsIAccessibleRole.ROLE_SEPARATOR;
const ROLE_SLIDER = nsIAccessibleRole.ROLE_SLIDER;
const ROLE_SPINBUTTON = nsIAccessibleRole.ROLE_SPINBUTTON;
const ROLE_STATICTEXT = nsIAccessibleRole.ROLE_STATICTEXT;
const ROLE_STATUSBAR = nsIAccessibleRole.ROLE_STATUSBAR;
const ROLE_STRONG = nsIAccessibleRole.ROLE_STRONG;
const ROLE_SUBSCRIPT = nsIAccessibleRole.ROLE_SUBSCRIPT;
const ROLE_SUGGESTION = nsIAccessibleRole.ROLE_SUGGESTION;
const ROLE_SUPERSCRIPT = nsIAccessibleRole.ROLE_SUPERSCRIPT;
const ROLE_SUMMARY = nsIAccessibleRole.ROLE_SUMMARY;
const ROLE_SWITCH = nsIAccessibleRole.ROLE_SWITCH;
const ROLE_TABLE = nsIAccessibleRole.ROLE_TABLE;
const ROLE_TERM = nsIAccessibleRole.ROLE_TERM;
const ROLE_TEXT = nsIAccessibleRole.ROLE_TEXT;
const ROLE_TEXT_CONTAINER = nsIAccessibleRole.ROLE_TEXT_CONTAINER;
const ROLE_TEXT_LEAF = nsIAccessibleRole.ROLE_TEXT_LEAF;
const ROLE_TIME = nsIAccessibleRole.ROLE_TIME;
const ROLE_TIME_EDITOR = nsIAccessibleRole.ROLE_TIME_EDITOR;
const ROLE_TOGGLE_BUTTON = nsIAccessibleRole.ROLE_TOGGLE_BUTTON;
const ROLE_TOOLBAR = nsIAccessibleRole.ROLE_TOOLBAR;
const ROLE_TOOLTIP = nsIAccessibleRole.ROLE_TOOLTIP;
const ROLE_TREE_TABLE = nsIAccessibleRole.ROLE_TREE_TABLE;
const ROLE_WHITESPACE = nsIAccessibleRole.ROLE_WHITESPACE;

// //////////////////////////////////////////////////////////////////////////////
// Public methods

/**
 * Test that the role of the given accessible is the role passed in.
 *
 * @param aAccOrElmOrID  the accessible, DOM element or ID to be tested.
 * @param aRole  The role that is to be expected.
 */
function testRole(aAccOrElmOrID, aRole) {
  var role = getRole(aAccOrElmOrID);
  is(role, aRole, "Wrong role for " + prettyName(aAccOrElmOrID) + "!");
}

/**
 * Return the role of the given accessible. Return -1 if accessible could not
 * be retrieved.
 *
 * @param aAccOrElmOrID  [in] The accessible, DOM element or element ID the
 *                       accessible role is being requested for.
 */
function getRole(aAccOrElmOrID) {
  var acc = getAccessible(aAccOrElmOrID);
  if (!acc) {
    return -1;
  }

  var role = -1;
  try {
    role = acc.role;
  } catch (e) {
    ok(false, "Role for " + aAccOrElmOrID + " could not be retrieved!");
  }

  return role;
}

/**
 * Analogy of SimpleTest.is function used to check the role.
 */
function isRole(aIdentifier, aRole, aMsg) {
  var role = getRole(aIdentifier);
  if (role == -1) {
    return;
  }

  if (role == aRole) {
    ok(true, aMsg);
    return;
  }

  var got = roleToString(role);
  var expected = roleToString(aRole);

  ok(false, aMsg + "got '" + got + "', expected '" + expected + "'");
}
