/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Paste this at the head of a scratch browser-chrome test or a
 * `firefox-probe.py --context chrome --async` body, then call
 * `capture(name, element)` once per shot. Do not load it at runtime: the
 * scratch test must not carry a path to this file.
 *
 * `shots.label` distinguishes the two runs and `shots.dir` overrides the
 * output directory; pass them with `--setpref` to mochitest, or to
 * firefox-probe.py when this is pasted into a probe.
 */

/* `info` comes from the mochitest harness this is pasted into. */
/* global info */

const LABEL = Services.prefs.getCharPref("shots.label", "unlabeled");
const OUT_DIR = Services.prefs.getCharPref("shots.dir", PathUtils.tempDir);

/**
 * Write a PNG of an element's padded border box, plus its geometry to the log.
 *
 * @param {string} name Distinguishes shots within one run.
 * @param {Element} element The element to frame, in a chrome window.
 * @param {object} [options]
 * @param {number} [options.pad] CSS px of margin around the border box; widen
 *   it to keep a shadow, outline or other ink overflow in frame.
 * @param {string} [options.background] What shows through transparent areas;
 *   pick a color distinct from the ink under test (a white icon vanishes on
 *   the default white) and check the ink's pixel count.
 */
async function capture(name, element, { pad = 8, background = "white" } = {}) {
  let win = element.documentGlobal;
  let doc = win.document;
  // The harness's remote-control cue paints stripes over the urlbar; hide it
  // for the shot and put it back.
  let root = doc.documentElement;
  let remoteControl = root.getAttribute("remotecontrol");
  root.removeAttribute("remotecontrol");
  let canvas, x, y, w, h, dpr;
  try {
    // A forced or locked state starts its transitions; shoot their end state.
    // document.getAnimations() leaves out shadow trees, so visit each one.
    let scopes = [doc];
    for (let i = 0; i < scopes.length; i++) {
      for (let a of scopes[i].getAnimations()) {
        if (win.CSSTransition.isInstance(a)) {
          a.finish();
        }
      }
      for (let el of scopes[i].querySelectorAll("*")) {
        if (el.openOrClosedShadowRoot) {
          scopes.push(el.openOrClosedShadowRoot);
        }
      }
    }
    await win.promiseDocumentFlushed(() => {});
    let rect = element.getBoundingClientRect();
    if (!rect.width || !rect.height) {
      throw new Error(`capture(${name}): the element has no box`);
    }
    // Whole CSS px, so that origin= maps a window rect into the image exactly.
    x = Math.max(0, Math.floor(rect.left - pad));
    y = Math.max(0, Math.floor(rect.top - pad));
    w = Math.min(Math.ceil(rect.right + pad), Math.floor(win.innerWidth)) - x;
    h = Math.min(Math.ceil(rect.bottom + pad), Math.floor(win.innerHeight)) - y;
    if (w < 1 || h < 1) {
      throw new Error(`capture(${name}): the element is outside the window`);
    }
    dpr = win.devicePixelRatio;
    canvas = doc.createElementNS("http://www.w3.org/1999/xhtml", "canvas");
    canvas.width = Math.ceil(w * dpr);
    canvas.height = Math.ceil(h * dpr);
    let ctx = canvas.getContext("2d");
    // Without this drawWindow draws at 1x into the DPR-sized canvas, so on a
    // HiDPI display the image fills only part of it, which looks entirely
    // plausible beside its pair.
    ctx.scale(dpr, dpr);
    ctx.drawWindow(win, x, y, w, h, background);
  } finally {
    if (remoteControl !== null) {
      root.setAttribute("remotecontrol", remoteControl);
    }
  }
  let base64 = canvas.toDataURL("image/png").split(",")[1];
  let bytes = Uint8Array.from(atob(base64), c => c.charCodeAt(0));
  let path = PathUtils.join(OUT_DIR, `${LABEL}-${name}.png`);
  await IOUtils.write(path, bytes);
  const line = `SHOT ${path} ${w}x${h} dpr=${dpr} origin=${x},${y}`;
  if (typeof info == "function") {
    info(line);
    return;
  }
  // dump() goes to the browser's stdout, which a probe runner discards, so a
  // probe returns globalThis.__shots at its end to get the lines back. A
  // mochitest gets the line from info() instead: a property left on its
  // window fails the test ("test left unexpected property on window").
  dump(line + "\n");
  (globalThis.__shots ??= []).push(line);
}
