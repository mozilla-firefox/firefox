/* Any copyright is dedicated to the Public Domain.
 http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Tests that inherited element-backed pseudo element rules are properly displayed in
// the Rules view and that declarations are properly overridden.

const TEST_URI = `
  <style>
    details {
      --x: blue;
      color: gold;

      summary { color: violet; }

      &::details-content {
        --x: tomato;
        color: dodgerblue;
        background-color: rgb(200 0 0 / 0.1);
      }
    }

    /* use :where() to have a lower specificity than the details rule above */
    :where(body > details)::details-content {
      color: forestgreen;
    }

    p {
      outline-color: var(--x);

      &::after {
        content: " meow";
        color: green;
      }
    }

    details#in-summary {
      color: cyan;

      & summary {
        color: hotpink;
      }

      &::details-content {
        --x: rebeccapurple;
        color: brown;
      }
    }

    details#vip::details-content {
      color: blue !important;
    }

    details#vip::details-content {
      color: red;
    }
  </style>
  <details open>
    <summary>
      Top-level summary
      <details id=in-summary open>
        <summary>nested summary</summary>
        details in summary
        <p>child of details in summary</p>
      </details>
    </summary>
    top-level details
    <summary id=non-functional-summary>not a real summary</summary>
    <p>in top-level details</p>
    /* don't use an id so the "inherited from" section would have the same text as the
       section for the parent details. This will assert that we do get separate inhertied
       section for those different "levels" */
    <details class=in-details open>
      <summary>nested details summary</summary>
      nested details
      <p>child of nested details</p>
    </details>
  </details>
  <details id=vip open>
    <summary>s</summary>
    <article>hello</hello>
  </details>
`;

add_task(async function () {
  await addTab("data:text/html;charset=utf-8," + encodeURIComponent(TEST_URI));
  const { inspector, view } = await openRuleView();

  await selectNode("summary", inspector);
  Assert.deepEqual(
    getInheritedHeadersText(view),
    ["Inherited from details"],
    "There's no inherited ::details-content header when top-level <summary> is selected"
  );

  await selectNode("body > details > p", inspector);
  Assert.deepEqual(
    getInheritedHeadersText(view),
    ["Inherited from details::details-content", "Inherited from details"],
    "Got expected inherited headers when children of top-level <details> is selected"
  );

  is(
    view.element.querySelector(
      ".ruleview-header + #pseudo-elements-container .ruleview-selector-pseudo-class"
    ).textContent,
    "::after",
    "The ::after pseudo element rules is properly displayed in its section"
  );

  ok(
    !isPropertyOverridden(view, 6, { color: "dodgerblue" }),
    "color property in ::details-content is not overridden"
  );
  ok(
    isPropertyOverridden(view, 7, { color: "forestgreen" }),
    "color property in lower specificity ::details-content is overridden"
  );
  ok(
    isPropertyOverridden(view, 9, { color: "gold" }),
    "color property in details is overridden"
  );

  checkCSSVariableOutput(
    view,
    "p",
    "outline-color",
    "inspector-variable",
    "tomato"
  );

  info("Check rules and declarations for details in summary");
  await selectNode("details#in-summary", inspector);
  Assert.deepEqual(
    getInheritedHeadersText(view),
    ["Inherited from summary"],
    "Got expected inherited headers when <details> in <summary> is selected"
  );

  await selectNode("details#in-summary summary", inspector);
  Assert.deepEqual(
    getInheritedHeadersText(view),
    ["Inherited from details#in-summary"],
    "Got expected inherited headers when nested <summary> is selected"
  );

  await selectNode("details#in-summary p", inspector);
  Assert.deepEqual(
    getInheritedHeadersText(view),
    [
      "Inherited from details#in-summary::details-content",
      "Inherited from details#in-summary",
      "Inherited from summary",
    ],
    "Got expected inherited headers when nested <details> child is selected"
  );

  is(
    view.element.querySelector(
      ".ruleview-header + #pseudo-elements-container .ruleview-selector-pseudo-class"
    ).textContent,
    "::after",
    "The ::after pseudo element rules is properly displayed in its section"
  );

  ok(
    !isPropertyOverridden(view, 6, { color: "brown" }),
    "color property in #detail#in-summary::details-content is not overridden"
  );
  ok(
    isPropertyOverridden(view, 7, { color: "dodgerblue" }),
    "color property in ::details-content is overridden"
  );
  ok(
    isPropertyOverridden(view, 9, { color: "cyan" }),
    "color property in details#in-summary is overridden"
  );
  ok(
    isPropertyOverridden(view, 10, { color: "gold" }),
    "color property in details is overridden"
  );
  ok(
    isPropertyOverridden(view, 12, { color: "violet" }),
    "color property in summary is overridden"
  );

  checkCSSVariableOutput(
    view,
    "p",
    "outline-color",
    "inspector-variable",
    "rebeccapurple"
  );

  info("Check rules and declarations for second summary inside details");
  // when a <details> element has multiple <summary> children, only the first one is
  // actually interactive. The other ones are placed inside the ::details-content
  await selectNode("summary#non-functional-summary", inspector);
  Assert.deepEqual(
    getInheritedHeadersText(view),
    ["Inherited from details::details-content", "Inherited from details"],
    "Got expected inherited headers when non functional summary is selected"
  );

  ok(
    !isPropertyOverridden(view, 1, { color: "violet" }),
    "color property in summary is not overridden when non functional summary is selected"
  );
  ok(
    isPropertyOverridden(view, 3, { color: "dodgerblue" }),
    "color property in details::details-content is overridden when non functional summary is selected"
  );
  ok(
    isPropertyOverridden(view, 4, { color: "forestgreen" }),
    "color property in :where(body > details)::details-content is overridden when non functional summary is selected"
  );
  ok(
    isPropertyOverridden(view, 6, { color: "gold" }),
    "color property in details is overridden when non functional summary is selected"
  );

  info("Check rules and declarations for details in details");
  await selectNode("details.in-details", inspector);
  Assert.deepEqual(
    getInheritedHeadersText(view),
    ["Inherited from details::details-content"],
    "Got expected inherited headers when <details> in <details> is selected"
  );
  ok(
    !isPropertyOverridden(view, 4, { color: "gold" }),
    "color property in details is not overridden for details in details"
  );
  ok(
    isPropertyOverridden(view, 6, { color: "forestgreen" }),
    "color property in where(body > details)::details-content is overridden for details in details"
  );

  info("Check rules and declarations for children of details in details");
  await selectNode("details.in-details p", inspector);
  Assert.deepEqual(
    getInheritedHeadersText(view),
    [
      // this is for the body > details > details::details-content pseudo
      "Inherited from details::details-content",
      // this is for the body > details::details-content pseudo
      "Inherited from details::details-content",
      "Inherited from details",
    ],
    "Got expected inherited headers when children <details> in <details> is selected"
  );

  ok(
    !isPropertyOverridden(view, 6, { color: "dodgerblue" }),
    "color property in inherited details::details-content is not overridden for child of details in details"
  );
  ok(
    isPropertyOverridden(view, 8, { color: "forestgreen" }),
    "color property in inherited :where(body > details)::details-content is overridden for child of details in details"
  );
  ok(
    isPropertyOverridden(view, 10, { color: "gold" }),
    "color property in inherited details is overridden for child of details in details"
  );

  info(
    "Check that properties in inherited element-backed pseudo element rules are properly picked when using !important"
  );
  await selectNode("#vip article", inspector);
  Assert.deepEqual(
    getInheritedHeadersText(view),
    [
      "Inherited from details#vip::details-content",
      "Inherited from details#vip",
    ],
    "Got expected inherited headers"
  );

  ok(
    isPropertyOverridden(view, 2, { color: "red" }),
    "non-important color property in #vip::details-content is overridden"
  );
  ok(
    !isPropertyOverridden(view, 3, { color: "blue" }),
    "important color property in #vip::details-content is not overridden"
  );
  ok(
    isPropertyOverridden(view, 4, { color: "dodgerblue" }),
    "non important color property in details::details-content is overridden"
  );
  ok(
    isPropertyOverridden(view, 5, { color: "forestgreen" }),
    "non important color property in :where(body > details)::details-content is overridden"
  );
  ok(
    isPropertyOverridden(view, 7, { color: "gold" }),
    "non important color property in details is overridden"
  );
});

function getInheritedHeadersText(view) {
  return [...view.element.querySelectorAll(".ruleview-header-inherited")].map(
    el => el.textContent
  );
}

function isPropertyOverridden(view, ruleIndex, property) {
  return getTextProperty(
    view,
    ruleIndex,
    property
  ).editor.element.classList.contains("ruleview-overridden");
}
