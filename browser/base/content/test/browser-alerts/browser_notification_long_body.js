/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

async function openAlertWindow(body) {
  let args = Cc["@mozilla.org/array;1"].createInstance(Ci.nsIMutableArray);
  args.appendElement(null);
  for (let value of ["Test title", body]) {
    let str = Cc["@mozilla.org/supports-string;1"].createInstance(
      Ci.nsISupportsString
    );
    str.data = value;
    args.appendElement(str);
  }

  let win = Services.ww.openWindow(
    null,
    "chrome://global/content/alerts/alert.xhtml",
    "_blank",
    "chrome,titlebar=no,popup=yes",
    args
  );
  await BrowserTestUtils.waitForEvent(win, "load");
  await new Promise(resolve =>
    win.requestAnimationFrame(() => win.requestAnimationFrame(resolve))
  );
  return win;
}

add_task(async function test_long_body_is_clamped() {
  let body = Array.from({ length: 12 }, (_, i) => `Line ${i} of the body`).join(
    "\n"
  );
  let win = await openAlertWindow(body);
  let label = win.document.getElementById("alertTextLabel");

  Assert.equal(label.textContent, body, "The full body text is kept");
  Assert.greater(
    label.scrollHeight,
    label.clientHeight,
    "The body is clamped to a limited number of lines"
  );
  Assert.equal(
    label.getAttribute("tooltiptext"),
    body,
    "The tooltip shows the full body"
  );

  win.close();
});

add_task(async function test_long_unbroken_word_is_clamped() {
  let body = "x".repeat(400);
  let win = await openAlertWindow(body);
  let label = win.document.getElementById("alertTextLabel");

  Assert.greater(
    label.scrollHeight,
    label.clientHeight,
    "A long word wraps and is clamped"
  );
  Assert.equal(
    label.getAttribute("tooltiptext"),
    body,
    "The tooltip shows the full body"
  );

  win.close();
});

add_task(async function test_short_body_is_not_clamped() {
  let win = await openAlertWindow("Short body");
  let label = win.document.getElementById("alertTextLabel");

  Assert.equal(label.textContent, "Short body", "The body text is shown");
  Assert.equal(
    label.scrollHeight,
    label.clientHeight,
    "A short body is not clamped"
  );
  Assert.ok(!label.hasAttribute("tooltiptext"), "A short body has no tooltip");

  win.close();
});
