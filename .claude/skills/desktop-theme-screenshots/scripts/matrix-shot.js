/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* matrix.py pastes this into each probe after capture.js, probelib's PRELUDE
 * (lockState, finishTransitions, liftRemoteControl), `readings` and
 * `contrast()`. */
/* global capture, readings, lockState, finishTransitions, liftRemoteControl, contrast */

const SHOT_PROPS = [
  "color",
  "background-color",
  "background-image",
  "border-color",
  "outline",
  "box-shadow",
  "fill",
  "stroke",
  "opacity",
  "visibility",
];
const DESCENDANT_LIMIT = 2000;

/**
 * The element a selector names in the browser window, where `>>>` steps into
 * the shadow root of the element before it.
 *
 * @param {string} selector
 * @returns {Element}
 */
function shotElement(selector) {
  let scope = window.document;
  let element = null;
  for (const part of selector.split(">>>")) {
    if (!scope) {
      throw new Error(`${selector}: ${element.localName} has no shadow root`);
    }
    element = scope.querySelector(part.trim());
    if (!element) {
      throw new Error(`nothing matches ${selector}`);
    }
    scope = element.openOrClosedShadowRoot;
  }
  return element;
}

/**
 * Hold every CSS animation and Web Animation running in a document and its
 * shadow trees still, so that both halves capture the same frame: finish one
 * that ends, and pause one that repeats forever at its start.
 *
 * @param {Document} doc
 * @returns {{names: string[], paused: Animation[]}} The names of the
 *   animations it held, sorted, and the ones it paused, to play again.
 */
function holdAnimations(doc) {
  const held = new Set();
  const paused = [];
  const scopes = [doc];
  for (let i = 0; i < scopes.length; i++) {
    for (const a of scopes[i].getAnimations()) {
      if (
        doc.defaultView.CSSTransition.isInstance(a) ||
        a.playState != "running"
      ) {
        continue;
      }
      if (a.effect?.getComputedTiming().endTime == Infinity) {
        a.pause();
        a.currentTime = 0;
        paused.push(a);
      } else {
        a.finish();
      }
      held.add(a.animationName || a.id || "(unnamed)");
    }
    for (const el of scopes[i].querySelectorAll("*")) {
      if (el.openOrClosedShadowRoot) {
        scopes.push(el.openOrClosedShadowRoot);
      }
    }
  }
  return { names: [...held].sort(), paused };
}

async function until(test, failure, win, ms = 10000) {
  const end = Date.now() + ms;
  while (!test()) {
    if (Date.now() > end) {
      throw new Error(failure);
    }
    await new Promise(resolve => win.setTimeout(resolve, 50));
  }
}

/**
 * Make a browser window inactive by focusing a second one.
 *
 * @param {Window} win
 * @param {Window} other A browser window OpenBrowserWindow() just opened.
 */
async function deactivate(win, other) {
  await other.delayedStartupPromise;
  other.focus();
  await until(
    () => win.document.documentElement.matches(":-moz-window-inactive"),
    ":-moz-window-inactive did not match",
    win
  );
}

async function reactivate(win, other) {
  other.close();
  win.focus();
  await until(
    () => !win.document.documentElement.matches(":-moz-window-inactive"),
    "the browser window did not become active again",
    win
  );
}

function flatChildren(node) {
  if (node.openOrClosedShadowRoot) {
    return [...node.openOrClosedShadowRoot.children];
  }
  if (node.localName == "slot" && node.assignedNodes().length) {
    return node.assignedElements();
  }
  return [...node.children];
}

/**
 * The computed `props` of an element and of each rendered element in its flat
 * tree, keyed "" for the element itself and by a path such as
 * "#back-button > image.toolbarbutton-icon" for a descendant.
 *
 * @param {Element} root
 * @param {string[]} props
 * @param {boolean} descendants
 */
function readStyles(root, props, descendants) {
  const read = el => {
    const style = el.documentGlobal.getComputedStyle(el);
    return Object.fromEntries(props.map(p => [p, style.getPropertyValue(p)]));
  };
  const styles = { "": read(root) };
  const queue = [[root, ""]];
  let count = 0;
  while (descendants && queue.length) {
    const [node, path] = queue.shift();
    const kids = flatChildren(node).filter(
      kid =>
        kid.checkVisibility() &&
        !(kid.documentGlobal.XULPopupElement.isInstance(kid) && kid.state != "open")
    );
    const parts = kids.map(kid =>
      kid.id
        ? `#${kid.id}`
        : kid.localName + (kid.classList[0] ? `.${kid.classList[0]}` : "")
    );
    const seen = new Map();
    for (const [i, kid] of kids.entries()) {
      let part = parts[i];
      if (parts.filter(p => p == part).length > 1) {
        seen.set(part, (seen.get(part) ?? 0) + 1);
        part += `[${seen.get(part)}]`;
      }
      const unique = kid.id && kid.getRootNode() == window.document;
      const key =
        (unique && !(part in styles)) || !path ? part : `${path} > ${part}`;
      if (++count > DESCENDANT_LIMIT) {
        styles["…"] = { truncated: `more than ${DESCENDANT_LIMIT} descendants` };
        return styles;
      }
      styles[key] = read(kid);
      queue.push([kid, key]);
    }
  }
  return styles;
}

/**
 * Set attributes and lock a state on an element, hold the document's
 * animations still (holdAnimations(), with the names it held in
 * `readings[name].heldAnimations`), record the element's computed styles and
 * its descendants' in `readings[name].styles`, and capture it. A failure
 * is recorded as `readings[name].error` rather than thrown, so the cell's
 * other shots still run.
 *
 * @param {string} name Names the shot's images and readings.
 * @param {Element|string} element The element, or a selector for it in the
 *   browser window, where `>>>` steps into a shadow root.
 * @param {object} [options]
 * @param {string|string[]} [options.lock] Pseudo-classes to lock, such as
 *   ":hover"; :hover and :active also lock on every ancestor.
 * @param {{[attr: string]: string|boolean|null}} [options.attrs] Attributes
 *   to set on the element for the shot (true sets "", false or null
 *   removes), restored afterwards.
 * @param {string[]} [options.props] CSS properties to read besides
 *   SHOT_PROPS.
 * @param {boolean} [options.inactive] Shoot with the browser window inactive,
 *   which a second window opened and focused for the shot makes it.
 * @param {boolean} [options.descendants] Whether to read the descendants
 *   too (default true).
 * @param {number} [options.pad] capture()'s pad.
 * @param {boolean} [options.contrast] Also record `contrast(element)` in the
 *   locked state.
 */
async function shot(
  name,
  element,
  {
    lock = [],
    attrs = {},
    props = [],
    inactive = false,
    descendants = true,
    pad,
    contrast: judge,
  } = {}
) {
  const state = [lock].flat().join("") || "default";
  let clear = () => {};
  const saved = [];
  let other = null;
  let paused = [];
  let restoreRemoteControl = () => {};
  try {
    if (typeof element == "string") {
      element = shotElement(element);
    }
    for (const [attr, value] of Object.entries(attrs)) {
      saved.push([attr, element.getAttribute(attr)]);
      if (value === null || value === false) {
        element.removeAttribute(attr);
      } else {
        element.setAttribute(attr, value === true ? "" : String(value));
      }
    }
    if (inactive) {
      other = window.OpenBrowserWindow();
      await deactivate(window, other);
    }
    const locked = lockState(element, state);
    clear = locked.clear;
    if (locked.notLocked.length) {
      throw new Error(`${locked.notLocked.join(", ")} did not lock`);
    }
    finishTransitions(element.ownerDocument);
    const held = holdAnimations(element.ownerDocument);
    paused = held.paused;
    // capture() lifts the harness's striped address bar; read without it too.
    restoreRemoteControl = liftRemoteControl([
      element.ownerDocument.documentElement,
    ]);
    readings[name] = {
      styles: readStyles(element, [...SHOT_PROPS, ...props], descendants),
      ...(held.names.length && { heldAnimations: held.names }),
    };
    await capture(name, element, pad === undefined ? {} : { pad });
    // contrast() takes its unlocked baseline before locking the state itself.
    clear();
    if (judge) {
      readings[name].contrast = await contrast(element, { states: [state] });
    }
  } catch (e) {
    readings[name] = { ...readings[name], error: e.message ?? String(e) };
  } finally {
    clear();
    restoreRemoteControl();
    for (const a of paused) {
      a.play();
    }
    for (const [attr, value] of saved.reverse()) {
      if (value === null) {
        element.removeAttribute(attr);
      } else {
        element.setAttribute(attr, value);
      }
    }
    if (other) {
      await reactivate(window, other).catch(e => {
        const error = [readings[name]?.error, e.message ?? String(e)];
        readings[name] = {
          ...readings[name],
          error: error.filter(Boolean).join("; "),
        };
      });
    }
  }
}
