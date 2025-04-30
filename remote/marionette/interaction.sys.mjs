/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  setTimeout: "resource://gre/modules/Timer.sys.mjs",

  accessibility:
    "chrome://remote/content/shared/webdriver/Accessibility.sys.mjs",
  atom: "chrome://remote/content/marionette/atom.sys.mjs",
  dom: "chrome://remote/content/shared/DOM.sys.mjs",
  error: "chrome://remote/content/shared/webdriver/Errors.sys.mjs",
  event: "chrome://remote/content/shared/webdriver/Event.sys.mjs",
  Log: "chrome://remote/content/shared/Log.sys.mjs",
  pprint: "chrome://remote/content/shared/Format.sys.mjs",
  TimedPromise: "chrome://remote/content/marionette/sync.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "logger", () =>
  lazy.Log.get(lazy.Log.TYPES.MARIONETTE)
);

// dragService may be null if it's in the headless mode (e.g., on Linux).
// It depends on the platform, though.
ChromeUtils.defineLazyGetter(lazy, "dragService", () => {
  try {
    return Cc["@mozilla.org/widget/dragservice;1"].getService(
      Ci.nsIDragService
    );
  } catch (e) {
    // If we're in the headless mode, the drag service may be never
    // instantiated.  In this case, an exception is thrown.  Let's ignore
    // any exceptions since without the drag service, nobody can create a
    // drag session.
    return null;
  }
});

/** XUL elements that support disabled attribute. */
const DISABLED_ATTRIBUTE_SUPPORTED_XUL = new Set([
  "ARROWSCROLLBOX",
  "BUTTON",
  "CHECKBOX",
  "COMMAND",
  "DESCRIPTION",
  "KEY",
  "KEYSET",
  "LABEL",
  "MENU",
  "MENUITEM",
  "MENULIST",
  "MENUSEPARATOR",
  "RADIO",
  "RADIOGROUP",
  "RICHLISTBOX",
  "RICHLISTITEM",
  "TAB",
  "TABS",
  "TOOLBARBUTTON",
  "TREE",
]);

/**
 * Common form controls that user can change the value property
 * interactively.
 */
const COMMON_FORM_CONTROLS = new Set(["input", "textarea", "select"]);

/**
 * Input elements that do not fire <tt>input</tt> and <tt>change</tt>
 * events when value property changes.
 */
const INPUT_TYPES_NO_EVENT = new Set([
  "checkbox",
  "radio",
  "file",
  "hidden",
  "image",
  "reset",
  "button",
  "submit",
]);

/** @namespace */
export const interaction = {};

/**
 * Interact with an element by clicking it.
 *
 * The element is scrolled into view before visibility- or interactability
 * checks are performed.
 *
 * Selenium-style visibility checks will be performed
 * if <var>specCompat</var> is false (default).  Otherwise
 * pointer-interactability checks will be performed.  If either of these
 * fail an {@link ElementNotInteractableError} is thrown.
 *
 * If <var>strict</var> is enabled (defaults to disabled), further
 * accessibility checks will be performed, and these may result in an
 * {@link ElementNotAccessibleError} being returned.
 *
 * When <var>el</var> is not enabled, an {@link InvalidElementStateError}
 * is returned.
 *
 * @param {(DOMElement|XULElement)} el
 *     Element to click.
 * @param {boolean=} [strict=false] strict
 *     Enforce strict accessibility tests.
 * @param {boolean=} [specCompat=false] specCompat
 *     Use WebDriver specification compatible interactability definition.
 *
 * @throws {ElementNotInteractableError}
 *     If either Selenium-style visibility check or
 *     pointer-interactability check fails.
 * @throws {ElementClickInterceptedError}
 *     If <var>el</var> is obscured by another element and a click would
 *     not hit, in <var>specCompat</var> mode.
 * @throws {ElementNotAccessibleError}
 *     If <var>strict</var> is true and element is not accessible.
 * @throws {InvalidElementStateError}
 *     If <var>el</var> is not enabled.
 */
interaction.clickElement = async function (
  el,
  strict = false,
  specCompat = false
) {
  const a11y = lazy.accessibility.get(strict);
  if (lazy.dom.isXULElement(el)) {
    await chromeClick(el, a11y);
  } else if (specCompat) {
    await webdriverClickElement(el, a11y);
  } else {
    lazy.logger.trace(`Using non spec-compatible element click`);
    await seleniumClickElement(el, a11y);
  }
};

async function webdriverClickElement(el, a11y) {
  const win = getWindow(el);

  // step 3
  if (el.localName == "input" && el.type == "file") {
    throw new lazy.error.InvalidArgumentError(
      "Cannot click <input type=file> elements"
    );
  }

  let containerEl = lazy.dom.getContainer(el);

  // step 4
  if (!lazy.dom.isInView(containerEl)) {
    lazy.dom.scrollIntoView(containerEl);
  }

  // step 5
  // TODO(ato): wait for containerEl to be in view

  // step 6
  // if we cannot bring the container element into the viewport
  // there is no point in checking if it is pointer-interactable
  if (!lazy.dom.isInView(containerEl)) {
    throw new lazy.error.ElementNotInteractableError(
      lazy.pprint`Element ${el} could not be scrolled into view`
    );
  }

  // step 7
  let rects = containerEl.getClientRects();
  let clickPoint = lazy.dom.getInViewCentrePoint(rects[0], win);

  if (lazy.dom.isObscured(containerEl)) {
    throw new lazy.error.ElementClickInterceptedError(
      null,
      {},
      containerEl,
      clickPoint
    );
  }

  let acc = await a11y.assertAccessible(el, true);
  a11y.assertVisible(acc, el, true);
  a11y.assertEnabled(acc, el, true);
  a11y.assertActionable(acc, el);

  // step 8
  if (el.localName == "option") {
    interaction.selectOption(el);
  } else {
    // Synthesize a pointerMove action.
    lazy.event.synthesizeMouseAtPoint(
      clickPoint.x,
      clickPoint.y,
      {
        type: "mousemove",
        allowToHandleDragDrop: true,
      },
      win
    );

    if (lazy.dragService?.getCurrentSession(win)) {
      // Special handling is required if the mousemove started a drag session.
      // In this case, mousedown event shouldn't be fired, and the mouseup should
      // end the session.  Therefore, we should synthesize only mouseup.
      lazy.event.synthesizeMouseAtPoint(
        clickPoint.x,
        clickPoint.y,
        {
          type: "mouseup",
          allowToHandleDragDrop: true,
        },
        win
      );
    } else {
      // step 9
      let clicked = interaction.flushEventLoop(containerEl);

      // Synthesize a pointerDown + pointerUp action.
      lazy.event.synthesizeMouseAtPoint(
        clickPoint.x,
        clickPoint.y,
        { allowToHandleDragDrop: true },
        win
      );

      await clicked;
    }
  }

  // step 10
  // if the click causes navigation, the post-navigation checks are
  // handled by navigate.js
}

async function chromeClick(el, a11y) {
  if (!(await lazy.dom.isEnabled(el))) {
    throw new lazy.error.InvalidElementStateError("Element is not enabled");
  }

  let acc = await a11y.assertAccessible(el, true);
  a11y.assertVisible(acc, el, true);
  a11y.assertEnabled(acc, el, true);
  a11y.assertActionable(acc, el);

  if (el.localName == "option") {
    interaction.selectOption(el);
  } else {
    el.click();
  }
}

async function seleniumClickElement(el, a11y) {
  let win = getWindow(el);

  let visibilityCheckEl = el;
  if (el.localName == "option") {
    visibilityCheckEl = lazy.dom.getContainer(el);
  }

  if (!(await lazy.dom.isVisible(visibilityCheckEl))) {
    throw new lazy.error.ElementNotInteractableError();
  }

  if (!(await lazy.dom.isEnabled(el))) {
    throw new lazy.error.InvalidElementStateError("Element is not enabled");
  }

  let acc = await a11y.assertAccessible(el, true);
  a11y.assertVisible(acc, el, true);
  a11y.assertEnabled(acc, el, true);
  a11y.assertActionable(acc, el);

  if (el.localName == "option") {
    interaction.selectOption(el);
  } else {
    let rects = el.getClientRects();
    let centre = lazy.dom.getInViewCentrePoint(rects[0], win);
    let opts = {};
    lazy.event.synthesizeMouseAtPoint(centre.x, centre.y, opts, win);
  }
}

/**
 * Select <tt>&lt;option&gt;</tt> element in a <tt>&lt;select&gt;</tt>
 * list.
 *
 * Because the dropdown list of select elements are implemented using
 * native widget technology, our trusted synthesised events are not able
 * to reach them.  Dropdowns are instead handled mimicking DOM events,
 * which for obvious reasons is not ideal, but at the current point in
 * time considered to be good enough.
 *
 * @param {HTMLOptionElement} el
 *     Option element to select.
 *
 * @throws {TypeError}
 *     If <var>el</var> is a XUL element or not an <tt>&lt;option&gt;</tt>
 *     element.
 * @throws {Error}
 *     If unable to find <var>el</var>'s parent <tt>&lt;select&gt;</tt>
 *     element.
 */
interaction.selectOption = function (el) {
  if (lazy.dom.isXULElement(el)) {
    throw new TypeError("XUL dropdowns not supported");
  }
  if (el.localName != "option") {
    throw new TypeError(lazy.pprint`Expected <option> element, got ${el}`);
  }

  let containerEl = lazy.dom.getContainer(el);

  lazy.event.mouseover(containerEl);
  lazy.event.mousemove(containerEl);
  lazy.event.mousedown(containerEl);
  containerEl.focus();

  if (!el.disabled) {
    // Clicking <option> in <select> should not be deselected if selected.
    // However, clicking one in a <select multiple> should toggle
    // selectedness the way holding down Control works.
    if (containerEl.multiple) {
      el.selected = !el.selected;
    } else if (!el.selected) {
      el.selected = true;
    }
    lazy.event.input(containerEl);
    lazy.event.change(containerEl);
  }

  lazy.event.mouseup(containerEl);
  lazy.event.click(containerEl);
  containerEl.blur();
};

/**
 * Clears the form control or the editable element, if required.
 *
 * Before clearing the element, it will attempt to scroll it into
 * view if it is not already in the viewport.  An error is raised
 * if the element cannot be brought into view.
 *
 * If the element is a submittable form control and it is empty
 * (it has no value or it has no files associated with it, in the
 * case it is a <code>&lt;input type=file&gt;</code> element) or
 * it is an editing host and its <code>innerHTML</code> content IDL
 * attribute is empty, this function acts as a no-op.
 *
 * @param {Element} el
 *     Element to clear.
 *
 * @throws {InvalidElementStateError}
 *     If element is disabled, read-only, non-editable, not a submittable
 *     element or not an editing host, or cannot be scrolled into view.
 */
interaction.clearElement = function (el) {
  if (lazy.dom.isDisabled(el)) {
    throw new lazy.error.InvalidElementStateError(
      lazy.pprint`Element is disabled: ${el}`
    );
  }
  if (lazy.dom.isReadOnly(el)) {
    throw new lazy.error.InvalidElementStateError(
      lazy.pprint`Element is read-only: ${el}`
    );
  }
  if (!lazy.dom.isEditable(el)) {
    throw new lazy.error.InvalidElementStateError(
      lazy.pprint`Unable to clear element that cannot be edited: ${el}`
    );
  }

  if (!lazy.dom.isInView(el)) {
    lazy.dom.scrollIntoView(el);
  }
  if (!lazy.dom.isInView(el)) {
    throw new lazy.error.ElementNotInteractableError(
      lazy.pprint`Element ${el} could not be scrolled into view`
    );
  }

  if (lazy.dom.isEditingHost(el)) {
    clearContentEditableElement(el);
  } else {
    clearResettableElement(el);
  }
};

function clearContentEditableElement(el) {
  if (el.innerHTML === "") {
    return;
  }
  el.focus();
  el.innerHTML = "";
  el.blur();
}

function clearResettableElement(el) {
  if (!lazy.dom.isMutableFormControl(el)) {
    throw new lazy.error.InvalidElementStateError(
      lazy.pprint`Not an editable form control: ${el}`
    );
  }

  let isEmpty;
  switch (el.type) {
    case "file":
      isEmpty = !el.files.length;
      break;

    default:
      isEmpty = el.value === "";
      break;
  }

  if (el.validity.valid && isEmpty) {
    return;
  }

  el.focus();
  el.value = "";
  lazy.event.change(el);
  el.blur();
}

/**
 * Waits until the event loop has spun enough times to process the
 * DOM events generated by clicking an element, or until the document
 * is unloaded.
 *
 * @param {Element} el
 *     Element that is expected to receive the click.
 *
 * @returns {Promise}
 *     Promise is resolved once <var>el</var> has been clicked
 *     (its <code>click</code> event fires), the document is unloaded,
 *     or a 500 ms timeout is reached.
 */
interaction.flushEventLoop = async function (el) {
  const win = el.ownerGlobal;
  let unloadEv, clickEv;

  let spinEventLoop = resolve => {
    unloadEv = resolve;
    clickEv = event => {
      lazy.logger.trace(`Received DOM event click for ${event.target}`);
      if (win.closed) {
        resolve();
      } else {
        lazy.setTimeout(resolve, 0);
      }
    };

    win.addEventListener("unload", unloadEv, { mozSystemGroup: true });
    el.addEventListener("click", clickEv, { mozSystemGroup: true });
  };
  let removeListeners = () => {
    // only one event fires
    win.removeEventListener("unload", unloadEv);
    el.removeEventListener("click", clickEv);
  };

  return new lazy.TimedPromise(spinEventLoop, {
    timeout: 500,
    throws: null,
  }).then(removeListeners);
};

/**
 * If <var>el<var> is a textual form control, or is contenteditable,
 * and no previous selection state exists, move the caret to the end
 * of the form control.
 *
 * The element has to be a <code>&lt;input type=text&gt;</code> or
 * <code>&lt;textarea&gt;</code> element, or have the contenteditable
 * attribute set, for the cursor to be moved.
 *
 * @param {Element} el
 *     Element to potential move the caret in.
 */
interaction.moveCaretToEnd = function (el) {
  if (!lazy.dom.isDOMElement(el)) {
    return;
  }

  let isTextarea = el.localName == "textarea";
  let isInputText = el.localName == "input" && el.type == "text";

  if (isTextarea || isInputText) {
    if (el.selectionEnd == 0) {
      let len = el.value.length;
      el.setSelectionRange(len, len);
    }
  } else if (el.isContentEditable) {
    let selection = getWindow(el).getSelection();
    selection.setPosition(el, el.childNodes.length);
  }
};

/**
 * Performs checks if <var>el</var> is keyboard-interactable.
 *
 * To decide if an element is keyboard-interactable various properties,
 * and computed CSS styles have to be evaluated. Whereby it has to be taken
 * into account that the element can be part of a container (eg. option),
 * and as such the container has to be checked instead.
 *
 * @param {Element} el
 *     Element to check.
 *
 * @returns {boolean}
 *     True if element is keyboard-interactable, false otherwise.
 */
interaction.isKeyboardInteractable = function (el) {
  const win = getWindow(el);

  // body and document element are always keyboard-interactable
  if (el.localName === "body" || el === win.document.documentElement) {
    return true;
  }

  // context menu popups do not take the focus from the document.
  const menuPopup = el.closest("menupopup");
  if (menuPopup) {
    if (menuPopup.state !== "open") {
      // closed menupopups are not keyboard interactable.
      return false;
    }

    const menuItem = el.closest("menuitem");
    if (menuItem) {
      // hidden or disabled menu items are not keyboard interactable.
      return !menuItem.disabled && !menuItem.hidden;
    }

    return true;
  }

  return Services.focus.elementIsFocusable(el, 0);
};

/**
 * Updates an `<input type=file>`'s file list with given `paths`.
 *
 * Hereby will the file list be appended with `paths` if the
 * element allows multiple files. Otherwise the list will be
 * replaced.
 *
 * @param {HTMLInputElement} el
 *     An `input type=file` element.
 * @param {Array.<string>} paths
 *     List of full paths to any of the files to be uploaded.
 *
 * @throws {InvalidArgumentError}
 *     If `path` doesn't exist.
 */
interaction.uploadFiles = async function (el, paths) {
  let files = [];

  if (el.hasAttribute("multiple")) {
    // for multiple file uploads new files will be appended
    files = Array.prototype.slice.call(el.files);
  } else if (paths.length > 1) {
    throw new lazy.error.InvalidArgumentError(
      lazy.pprint`Element ${el} doesn't accept multiple files`
    );
  }

  for (let path of paths) {
    let file;

    try {
      file = await File.createFromFileName(path);
    } catch (e) {
      throw new lazy.error.InvalidArgumentError("File not found: " + path);
    }

    files.push(file);
  }

  el.mozSetFileArray(files);
};

/**
 * Sets a form element's value.
 *
 * @param {DOMElement} el
 *     An form element, e.g. input, textarea, etc.
 * @param {string} value
 *     The value to be set.
 *
 * @throws {TypeError}
 *     If <var>el</var> is not an supported form element.
 */
interaction.setFormControlValue = function (el, value) {
  if (!COMMON_FORM_CONTROLS.has(el.localName)) {
    throw new TypeError("This function is for form elements only");
  }

  el.value = value;

  if (INPUT_TYPES_NO_EVENT.has(el.type)) {
    return;
  }

  lazy.event.input(el);
  lazy.event.change(el);
};

/**
 * Send keys to element.
 *
 * @param {DOMElement|XULElement} el
 *     Element to send key events to.
 * @param {Array.<string>} value
 *     Sequence of keystrokes to send to the element.
 * @param {object=} options
 * @param {boolean=} options.strictFileInteractability
 *     Run interactability checks on `<input type=file>` elements.
 * @param {boolean=} options.accessibilityChecks
 *     Enforce strict accessibility tests.
 * @param {boolean=} options.webdriverClick
 *     Use WebDriver specification compatible interactability definition.
 */
interaction.sendKeysToElement = async function (
  el,
  value,
  {
    strictFileInteractability = false,
    accessibilityChecks = false,
    webdriverClick = false,
  } = {}
) {
  const a11y = lazy.accessibility.get(accessibilityChecks);

  if (webdriverClick) {
    await webdriverSendKeysToElement(
      el,
      value,
      a11y,
      strictFileInteractability
    );
  } else {
    await legacySendKeysToElement(el, value, a11y);
  }
};

async function webdriverSendKeysToElement(
  el,
  value,
  a11y,
  strictFileInteractability
) {
  const win = getWindow(el);

  if (el.type !== "file" || strictFileInteractability) {
    let containerEl = lazy.dom.getContainer(el);

    if (!lazy.dom.isInView(containerEl)) {
      lazy.dom.scrollIntoView(containerEl);
    }

    // TODO: Wait for element to be keyboard-interactible
    if (!interaction.isKeyboardInteractable(containerEl)) {
      throw new lazy.error.ElementNotInteractableError(
        lazy.pprint`Element ${el} is not reachable by keyboard`
      );
    }

    if (win.document.activeElement !== containerEl) {
      containerEl.focus();
      // This validates the correct element types internally
      interaction.moveCaretToEnd(containerEl);
    }
  }

  let acc = await a11y.assertAccessible(el, true);
  a11y.assertActionable(acc, el);

  if (el.type == "file") {
    let paths = value.split("\n");
    await interaction.uploadFiles(el, paths);

    lazy.event.input(el);
    lazy.event.change(el);
  } else if (el.type == "date" || el.type == "time") {
    interaction.setFormControlValue(el, value);
  } else {
    lazy.event.sendKeys(value, win);
  }
}

async function legacySendKeysToElement(el, value, a11y) {
  const win = getWindow(el);

  if (el.type == "file") {
    el.focus();
    await interaction.uploadFiles(el, [value]);

    lazy.event.input(el);
    lazy.event.change(el);
  } else if (el.type == "date" || el.type == "time") {
    interaction.setFormControlValue(el, value);
  } else {
    let visibilityCheckEl = el;
    if (el.localName == "option") {
      visibilityCheckEl = lazy.dom.getContainer(el);
    }

    if (!(await lazy.dom.isVisible(visibilityCheckEl))) {
      throw new lazy.error.ElementNotInteractableError(
        "Element is not visible"
      );
    }

    let acc = await a11y.assertAccessible(el, true);
    a11y.assertActionable(acc, el);

    interaction.moveCaretToEnd(el);
    el.focus();
    lazy.event.sendKeys(value, win);
  }
}

/**
 * Determine the element displayedness of an element.
 *
 * @param {DOMElement|XULElement} el
 *     Element to determine displayedness of.
 * @param {boolean=} [strict=false] strict
 *     Enforce strict accessibility tests.
 *
 * @returns {boolean}
 *     True if element is displayed, false otherwise.
 */
interaction.isElementDisplayed = async function (el, strict = false) {
  let win = getWindow(el);
  let displayed = await lazy.atom.isElementDisplayed(el, win);

  let a11y = lazy.accessibility.get(strict);
  return a11y.assertAccessible(el).then(acc => {
    a11y.assertVisible(acc, el, displayed);
    return displayed;
  });
};

/**
 * Check if element is enabled.
 *
 * @param {DOMElement|XULElement} el
 *     Element to test if is enabled.
 *
 * @returns {boolean}
 *     True if enabled, false otherwise.
 */
interaction.isElementEnabled = async function (el, strict = false) {
  let enabled = true;
  let win = getWindow(el);

  if (lazy.dom.isXULElement(el)) {
    // check if XUL element supports disabled attribute
    if (DISABLED_ATTRIBUTE_SUPPORTED_XUL.has(el.tagName.toUpperCase())) {
      if (
        el.hasAttribute("disabled") &&
        el.getAttribute("disabled") === "true"
      ) {
        enabled = false;
      }
    }
  } else if (
    ["application/xml", "text/xml"].includes(win.document.contentType)
  ) {
    enabled = false;
  } else {
    enabled = await lazy.dom.isEnabled(el);
  }

  let a11y = lazy.accessibility.get(strict);
  return a11y.assertAccessible(el).then(acc => {
    a11y.assertEnabled(acc, el, enabled);
    return enabled;
  });
};

/**
 * Determines if the referenced element is selected or not, with
 * an additional accessibility check if <var>strict</var> is true.
 *
 * This operation only makes sense on input elements of the checkbox-
 * and radio button states, and option elements.
 *
 * @param {(DOMElement|XULElement)} el
 *     Element to test if is selected.
 * @param {boolean=} [strict=false] strict
 *     Enforce strict accessibility tests.
 *
 * @returns {boolean}
 *     True if element is selected, false otherwise.
 *
 * @throws {ElementNotAccessibleError}
 *     If <var>el</var> is not accessible when <var>strict</var> is true.
 */
interaction.isElementSelected = function (el, strict = false) {
  let selected = lazy.dom.isSelected(el);

  let a11y = lazy.accessibility.get(strict);
  return a11y.assertAccessible(el).then(acc => {
    a11y.assertSelected(acc, el, selected);
    return selected;
  });
};

function getWindow(el) {
  // eslint-disable-next-line mozilla/use-ownerGlobal
  return el.ownerDocument.defaultView;
}
