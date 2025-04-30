/* Any copyright is dedicated to the Public Domain.
 http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Tests that the rule-view displays correctly on MathML elements.

const TEST_URI = `
  <style>
    div {
      background-color: hotpink;
    }

    math {
      font-size: 36px;
    }

    msubsup {
      background-color: tomato;
    }
  </style>
  <div>
    <math>
      <mfrac>
        <msubsup>
          <mi>a</mi>
          <mi>i</mi>
          <mi>j</mi>
        </msubsup>
        <msub>
          <mi>x</mi>
          <mn style="color: gold;">0</mn>
        </msub>
      </mfrac>
    </math>
  </div>
`;

add_task(async function () {
  await addTab("data:text/html;charset=utf-8," + encodeURIComponent(TEST_URI));
  const { inspector, view } = await openRuleView();

  info("Select the DIV node and verify the rule-view shows rules");
  await selectNode("div", inspector);
  is(
    getRuleViewPropertyValue(view, "div", "background-color"),
    "hotpink",
    "background-color in div rule has expected value"
  );

  info("Select various MathML nodes and check that rules are displayed");
  await selectNode("math", inspector);
  is(
    getRuleViewPropertyValue(view, "math", "font-size"),
    "36px",
    "font-size in math rule has expected value"
  );

  await selectNode("msubsup", inspector);
  is(
    getRuleViewPropertyValue(view, "msubsup", "background-color"),
    "tomato",
    "background-color in msubsup rule has expected value"
  );

  await selectNode("mn", inspector);
  is(
    getRuleViewPropertyValue(view, "element", "color"),
    "gold",
    "color in mn element style has expected value"
  );

  info("Select again the DIV node and verify the rule-view shows rules");
  await selectNode("div", inspector);
  is(
    getRuleViewPropertyValue(view, "div", "background-color"),
    "hotpink",
    "background-color in div rule still has expected value"
  );
});
