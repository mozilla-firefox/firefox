/* Any copyright is dedicated to the Public Domain.
 http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Tests that the correct editable fields are focused when shift tabbing
// through the rule view.

const TEST_URI = `
  <style type='text/css'>
  #testid {
    background-color: blue;
    color: red;
    margin: 0;
    padding: 0;
  }
  div {
    border-color: red
  }
  </style>
  <div id='testid'>Styled Node</div>
`;

add_task(async function () {
  await addTab("data:text/html;charset=utf-8," + encodeURIComponent(TEST_URI));
  const { inspector, view } = await openRuleView();
  await selectNode("#testid", inspector);
  await testEditableFieldFocus(inspector, view, "VK_TAB", { shiftKey: true });
});

async function testEditableFieldFocus(
  inspector,
  view,
  commitKey,
  options = {}
) {
  let ruleEditor = getRuleViewRuleEditor(view, 2);
  const editor = await focusEditableField(view, ruleEditor.selectorText);
  is(
    inplaceEditor(ruleEditor.selectorText),
    editor,
    "Focus should be in the 'div' rule selector"
  );

  ruleEditor = getRuleViewRuleEditor(view, 1);

  await focusNextField(view, ruleEditor, commitKey, options);
  assertEditor(
    view,
    ruleEditor.newPropSpan,
    "Focus should have moved to the new property span"
  );

  for (const textProp of ruleEditor.rule.textProps.toReversed()) {
    const propEditor = textProp.editor;

    await focusNextField(view, ruleEditor, commitKey, options);
    await assertEditor(
      view,
      propEditor.valueSpan,
      "Focus should have moved to the property value"
    );

    await focusNextFieldAndExpectChange(view, ruleEditor, commitKey, options);
    await assertEditor(
      view,
      propEditor.nameSpan,
      "Focus should have moved to the property name"
    );
  }

  ruleEditor = getRuleViewRuleEditor(view, 1);

  await focusNextField(view, ruleEditor, commitKey, options);
  await assertEditor(
    view,
    ruleEditor.selectorText,
    "Focus should have moved to the '#testid' rule selector"
  );

  ruleEditor = getRuleViewRuleEditor(view, 0);

  await focusNextField(view, ruleEditor, commitKey, options);
  assertEditor(
    view,
    ruleEditor.newPropSpan,
    "Focus should have moved to the new property span"
  );
}

async function focusNextFieldAndExpectChange(
  view,
  ruleEditor,
  commitKey,
  options
) {
  const onRuleViewChanged = view.once("ruleview-changed");
  await focusNextField(view, ruleEditor, commitKey, options);
  await onRuleViewChanged;
}

async function focusNextField(view, ruleEditor, commitKey, options) {
  const onFocus = once(ruleEditor.element, "focus", true);
  EventUtils.synthesizeKey(commitKey, options, view.styleWindow);
  await onFocus;
}

function assertEditor(view, element, message) {
  const editor = inplaceEditor(view.styleDocument.activeElement);
  is(inplaceEditor(element), editor, message);
}
