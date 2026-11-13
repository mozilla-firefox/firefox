/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const BODY_LIMIT_PREF = "devtools.netmonitor.bodyLimit";

add_task(test_body_limit_value);
add_task(test_body_limit_save);
add_task(test_body_limit_cancel);
add_task(test_body_limit_invalid_values);
add_task(test_body_limit_reset);
add_task(test_body_limit_external_change_while_editing);

async function test_body_limit_value() {
  await pushPref(BODY_LIMIT_PREF, 1000000);
  const { toolbox, root } = await openSettings();
  const bodyLimit = await getBodyLimit(root);
  const getValue = () => bodyLimit.shadowRoot.querySelector("#value");
  is(getValue().textContent, "1 MB", "The size is formatted");

  await changeBodyLimit(toolbox, bodyLimit, () =>
    pushPref(BODY_LIMIT_PREF, 2000)
  );
  is(getValue().textContent, "2 kB", "The size follows the pref");

  await changeBodyLimit(toolbox, bodyLimit, () => pushPref(BODY_LIMIT_PREF, 0));
  is(
    bodyLimit.ownerDocument.l10n.getAttributes(getValue()).id,
    "devtools-settings-netmonitor-body-limit-unlimited",
    "Zero is shown as unlimited"
  );

  await closeTabAndToolbox();
}

async function test_body_limit_save() {
  await pushPref(BODY_LIMIT_PREF, 1024);
  const { toolbox, root } = await openSettings();
  const bodyLimit = await getBodyLimit(root);

  const input = await editBodyLimit(bodyLimit);
  is(input.value, "1024", "The input shows the body limit pref");
  is(bodyLimit.shadowRoot.activeElement, input, "The input is focused");
  is(
    input.selectionEnd - input.selectionStart,
    input.value.length,
    "The input's value is selected"
  );
  ok(
    !bodyLimit.shadowRoot.querySelector("#edit"),
    "The edit button is hidden while editing"
  );

  await changeBodyLimit(toolbox, bodyLimit, () => {
    typeBodyLimit(input, "2048");
    EventUtils.synthesizeKey("KEY_Enter", {}, bodyLimit.ownerGlobal);
  });
  is(Services.prefs.getIntPref(BODY_LIMIT_PREF), 2048, "Enter saves");
  ok(!bodyLimit.shadowRoot.querySelector("#input"), "Saving closes the input");
  is(
    bodyLimit.shadowRoot.activeElement,
    bodyLimit.shadowRoot.querySelector("#edit"),
    "Saving with Enter focuses the edit button"
  );

  typeBodyLimit(await editBodyLimit(bodyLimit), "4096");
  await changeBodyLimit(toolbox, bodyLimit, () =>
    pressButton(bodyLimit.shadowRoot.querySelector("#save"))
  );
  is(Services.prefs.getIntPref(BODY_LIMIT_PREF), 4096, "The save button saves");
  is(
    bodyLimit.shadowRoot.activeElement,
    bodyLimit.shadowRoot.querySelector("#edit"),
    "The save button focuses the edit button"
  );

  typeBodyLimit(await editBodyLimit(bodyLimit), "0");
  await changeBodyLimit(toolbox, bodyLimit, () =>
    EventUtils.synthesizeKey("KEY_Enter", {}, bodyLimit.ownerGlobal)
  );
  is(Services.prefs.getIntPref(BODY_LIMIT_PREF), 0, "Zero is saved");

  const tooLarge = await editBodyLimit(bodyLimit);
  typeBodyLimit(tooLarge, "99999999999");
  ok(tooLarge.matches(":invalid"), "A number too large is marked invalid");
  await changeBodyLimit(toolbox, bodyLimit, () =>
    EventUtils.synthesizeKey("KEY_Enter", {}, bodyLimit.ownerGlobal)
  );
  is(
    Services.prefs.getIntPref(BODY_LIMIT_PREF),
    2147483647,
    "A number over the integer pref limit is clamped"
  );

  await closeTabAndToolbox();
}

async function test_body_limit_cancel() {
  await pushPref(BODY_LIMIT_PREF, 1024);
  const { toolbox, root } = await openSettings();
  const bodyLimit = await getBodyLimit(root);

  const input = await editBodyLimit(bodyLimit);
  typeBodyLimit(input, "abc");
  ok(input.matches(":invalid"), "A non-number is marked invalid");
  EventUtils.synthesizeKey("KEY_Escape", {}, bodyLimit.ownerGlobal);
  await bodyLimit.updateComplete;
  is(Services.prefs.getIntPref(BODY_LIMIT_PREF), 1024, "Escape cancels");
  ok(!bodyLimit.shadowRoot.querySelector("#input"), "Escape closes the input");
  ok(!toolbox.splitConsole, "Escape doesn't open the split console");
  is(
    bodyLimit.shadowRoot.activeElement,
    bodyLimit.shadowRoot.querySelector("#edit"),
    "Escape focuses the edit button"
  );

  const reopened = await editBodyLimit(bodyLimit);
  is(reopened.value, "1024", "Editing again shows the body limit pref");
  ok(!reopened.matches(":invalid"), "Editing again isn't marked invalid");

  await closeTabAndToolbox();
}

async function test_body_limit_invalid_values() {
  await pushPref(BODY_LIMIT_PREF, 1024);
  const { root } = await openSettings();
  const bodyLimit = await getBodyLimit(root);

  for (const value of ["-5", "1e3", "1.5", "abc", ""]) {
    const input = await editBodyLimit(bodyLimit);
    typeBodyLimit(input, value);
    EventUtils.synthesizeKey("KEY_Enter", {}, bodyLimit.ownerGlobal);
    await bodyLimit.updateComplete;
    is(
      Services.prefs.getIntPref(BODY_LIMIT_PREF),
      1024,
      `"${value}" isn't saved`
    );
  }

  await closeTabAndToolbox();
}

async function test_body_limit_reset() {
  const defaultValue = Services.prefs
    .getDefaultBranch("")
    .getIntPref(BODY_LIMIT_PREF);
  await pushPref(BODY_LIMIT_PREF, defaultValue);
  const { toolbox, root } = await openSettings();
  const bodyLimit = await getBodyLimit(root);
  const getReset = () => bodyLimit.shadowRoot.querySelector("#reset");
  ok(!getReset(), "The reset button is hidden for the default value");

  await changeBodyLimit(toolbox, bodyLimit, () =>
    pushPref(BODY_LIMIT_PREF, 2048)
  );
  ok(getReset(), "The reset button is shown for a changed value");

  await editBodyLimit(bodyLimit);
  ok(!getReset(), "The reset button is hidden while editing");
  EventUtils.synthesizeKey("KEY_Escape", {}, bodyLimit.ownerGlobal);
  await bodyLimit.updateComplete;

  await changeBodyLimit(toolbox, bodyLimit, () => pressButton(getReset()));
  ok(
    !Services.prefs.prefHasUserValue(BODY_LIMIT_PREF),
    "Resetting clears the pref"
  );
  ok(!getReset(), "Resetting hides the reset button");
  is(
    bodyLimit.shadowRoot.activeElement,
    bodyLimit.shadowRoot.querySelector("#edit"),
    "Resetting focuses the edit button"
  );

  await closeTabAndToolbox();
}

async function test_body_limit_external_change_while_editing() {
  await pushPref(BODY_LIMIT_PREF, 1024);
  const { toolbox, root } = await openSettings();
  const bodyLimit = await getBodyLimit(root);

  const input = await editBodyLimit(bodyLimit);
  typeBodyLimit(input, "2048");
  await changeBodyLimit(toolbox, bodyLimit, () =>
    pushPref(BODY_LIMIT_PREF, 4000)
  );
  is(input.value, "2048", "An external pref change keeps the pending edit");

  EventUtils.synthesizeKey("KEY_Escape", {}, bodyLimit.ownerGlobal);
  await bodyLimit.updateComplete;
  is(
    Services.prefs.getIntPref(BODY_LIMIT_PREF),
    4000,
    "Cancelling keeps the external pref change"
  );
  is(
    bodyLimit.shadowRoot.querySelector("#value").textContent,
    "4 kB",
    "Cancelling shows the external pref change"
  );
  await closeTabAndToolbox();
}

/**
 * Find the body limit setting once it has rendered.
 *
 * @param {DevToolsSettings} root The settings panel's root element
 * @return {Promise<DevToolsSettingsBodyLimit>}
 */
async function getBodyLimit(root) {
  const bodyLimit = root.shadowRoot.querySelector(
    "#settings-netmonitor-body-limit"
  );
  await bodyLimit.updateComplete;
  return bodyLimit;
}

/**
 * Click the edit button and wait for the input to show.
 *
 * @param {DevToolsSettingsBodyLimit} bodyLimit The body limit setting
 * @return {Promise<HTMLInputElement>}
 */
async function editBodyLimit(bodyLimit) {
  bodyLimit.shadowRoot.querySelector("#edit").click();
  await bodyLimit.updateComplete;
  return bodyLimit.shadowRoot.querySelector("#input");
}

/**
 * Change the body limit pref and wait for the toolbox to apply it.
 *
 * @param {Toolbox} toolbox The toolbox showing the settings panel
 * @param {DevToolsSettingsBodyLimit} bodyLimit The body limit setting
 * @param {Function} action Changes the body limit pref
 */
async function changeBodyLimit(toolbox, bodyLimit, action) {
  const onApplied = toolbox.once("new-configuration-applied");
  await action();
  await onApplied;
  await bodyLimit.updateComplete;
}

/**
 * Press a button with the keyboard, as a user would.
 *
 * @param {MozButton} button The button to press
 */
function pressButton(button) {
  button.focus();
  EventUtils.synthesizeKey("KEY_Enter", {}, button.ownerGlobal);
}

/**
 * Replace the input's value by typing, as a user would.
 *
 * @param {HTMLInputElement} input The body limit input
 * @param {string} value The text to type
 */
function typeBodyLimit(input, value) {
  input.select();
  if (value !== "") {
    EventUtils.sendString(value, input.ownerGlobal);
  } else {
    EventUtils.synthesizeKey("KEY_Backspace", {}, input.ownerGlobal);
  }
}
