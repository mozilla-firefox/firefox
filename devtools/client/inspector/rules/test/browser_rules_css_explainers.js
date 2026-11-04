/* Any copyright is dedicated to the Public Domain.
 http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Test for CSS explainers in the Rules View.

const TEST_URI = `data:text/html,${encodeURIComponent(`<meta charset=utf8>
  <style>
    :root {
      font-size: 16px;
    }

    div {
      font-size: 24px;
      height: calc(2 * min(1rem, 100px));
    }

    div::after {
      line-height: calc(1em + 1px);
    }

    ol {
      font-size: 36px;
    }

    li {
      line-height: 20px;
      rotate: calc(sibling-index() * 2deg);
      translate: calc(sibling-count() * 1em + 1lh);
    }

    li::after {
      content: "-";
      outline: calc((sibling-count() + sibling-index()) * 1px) solid tomato;
    }

    aside {
      width: 300px;
      height: 200px;
      translate: calc(2 * random(2em, 16px)) calc(4 * random(--name, 2em, 16px));
    }

    aside div {
      width: calc(50% - 2px);
      height: calc(25% + 2px);
    }

    aside div::after {
      content: "fries";
      display: inline-block;
      width: calc(50% - 7px);
      height: calc(25% + 7px);
      margin-block: calc(10%);
    }

    #single-length-percentage {
      line-height: 10px;
      margin: 10% 20px 30em 40lh;
      background-position-x: 90%;
      color: hsl(200 30% 90%);
      --x: 85%;
    }

    #nested-substitutions {
      --attr-name: data-x;
      --var-name: --primary-color;
      --primary-color: tomato;
      height: attr(var(--attr-name) type(<length>));
      color: var(var(--var-name));
    }
  </style>
  <div>CSS explainers</div>
  <ol>
    <li>First</li>
    <li>Second</li>
    <li>Third</li>
  </ol>
  <aside>
    <div>Side</div>
    <section id="single-length-percentage">length-percentage</section>
  </aside>
  <section
    id="nested-substitutions"
    data-x="10rem"
  >nested-substitutions</section>`)}`;

add_task(async function () {
  await pushPref("devtools.inspector.css-explainers", true);
  await pushPref("layout.css.tree-counting-functions.enabled", true);
  await pushPref("layout.css.random.enabled", true);

  await addTab(TEST_URI);
  const { inspector, view } = await openRuleView();

  await selectNode("div", inspector);
  expandPseudoElementContainer(view);

  await assertCssExplainersTooltip({
    view,
    selector: "div",
    propertyName: "height",
    functionIndex: 0,
    expected: {
      functionText: "calc(2 * min(1rem, 100px))",
      tooltipText: [
        "calc(2 * min(1rem, 100px))",
        "calc(2 * min(16px, 100px))",
        "calc(2 * 16px)",
        "32px",
      ].join("\n"),
    },
  });
  await assertCssExplainersTooltip({
    view,
    selector: "div",
    propertyName: "height",
    lengthPercentageIndex: 0,
    expected: {
      lengthPercentageText: "1rem",
      tooltipText: ["1rem", "16px"].join("\n"),
    },
  });

  await assertCssExplainersTooltip({
    view,
    selector: "div",
    propertyName: "height",
    functionIndex: 1,
    expected: {
      functionText: "min(1rem, 100px)",
      tooltipText: ["min(1rem, 100px)", "min(16px, 100px)", "16px"].join("\n"),
    },
  });

  await assertCssExplainersTooltip({
    view,
    selector: "div::after",
    propertyName: "line-height",
    functionIndex: 0,
    expected: {
      functionText: "calc(1em + 1px)",
      tooltipText: ["calc(1em + 1px)", "calc(24px + 1px)", "25px"].join("\n"),
    },
  });

  await assertCssExplainersTooltip({
    view,
    selector: "div::after",
    propertyName: "line-height",
    lengthPercentageIndex: 0,
    expected: {
      lengthPercentageText: "1em",
      tooltipText: ["1em", "24px"].join("\n"),
    },
  });

  const secondLiNodeFront = await getNodeFront("li:nth-of-type(2)", inspector);
  await selectNode(secondLiNodeFront, inspector);
  await assertCssExplainersTooltip({
    view,
    selector: "li",
    propertyName: "rotate",
    functionIndex: 0,
    expected: {
      functionText: "calc(sibling-index() * 2deg)",
      tooltipText: [
        "calc(sibling-index() * 2deg)",
        "calc(2 * 2deg)",
        "4deg",
      ].join("\n"),
    },
  });

  await assertCssExplainersTooltip({
    view,
    selector: "li",
    propertyName: "rotate",
    functionIndex: 1,
    expected: {
      functionText: "sibling-index()",
      tooltipText: ["sibling-index()", "2"].join("\n"),
    },
  });

  await assertCssExplainersTooltip({
    view,
    selector: "li",
    propertyName: "translate",
    functionIndex: 0,
    expected: {
      functionText: "calc(sibling-count() * 1em + 1lh)",
      tooltipText: [
        "calc(sibling-count() * 1em + 1lh)",
        "calc((sibling-count() * 1em) + 1lh)",
        "calc((3 * 36px) + 20px)",
        "calc(108px + 20px)",
        "128px",
      ].join("\n"),
    },
  });

  await assertCssExplainersTooltip({
    view,
    selector: "li",
    propertyName: "translate",
    lengthPercentageIndex: 0,
    expected: {
      lengthPercentageText: "1em",
      tooltipText: ["1em", "36px"].join("\n"),
    },
  });
  await assertCssExplainersTooltip({
    view,
    selector: "li",
    propertyName: "translate",
    lengthPercentageIndex: 1,
    expected: {
      lengthPercentageText: "1lh",
      tooltipText: ["1lh", "20px"].join("\n"),
    },
  });

  await assertCssExplainersTooltip({
    view,
    selector: "li::after",
    propertyName: "outline",
    functionIndex: 0,
    expected: {
      functionText: "calc((sibling-count() + sibling-index()) * 1px)",
      tooltipText: [
        "calc((sibling-count() + sibling-index()) * 1px)",
        "calc((3 + 2) * 1px)",
        "calc(5 * 1px)",
        "5px",
      ].join("\n"),
    },
  });

  info("Select the li::after element in the markup view");
  const secondLiNodeFrontChildren =
    await inspector.markup.walker.children(secondLiNodeFront);
  const secondLiAfterElementNodeFront = secondLiNodeFrontChildren.nodes.at(-1);
  is(
    secondLiAfterElementNodeFront.displayName,
    "::after",
    "Got expected ::after pseudo element"
  );
  await selectNode(secondLiAfterElementNodeFront, inspector);
  await assertCssExplainersTooltip({
    view,
    selector: "li::after",
    propertyName: "outline",
    functionIndex: 0,
    expected: {
      functionText: "calc((sibling-count() + sibling-index()) * 1px)",
      tooltipText: [
        "calc((sibling-count() + sibling-index()) * 1px)",
        "calc((3 + 2) * 1px)",
        "calc(5 * 1px)",
        "5px",
      ].join("\n"),
    },
  });

  info("Select the <aside> to check random() computation");
  await selectNode("aside ", inspector);

  await assertCssExplainersTooltip({
    view,
    selector: "aside",
    propertyName: "translate",
    functionIndex: 0,
    expected: {
      functionText: "calc(2 * random(2em, 16px))",
      tooltipLines: [
        "calc(2 * random(2em, 16px))",
        "calc(2 * random(element-scoped ua-translate-1, 2em, 16px))",
        /calc\(2 \* random\(fixed 0\.\d+, 32px, 16px\)\)/,
        "calc(2 * 32px)",
        "64px",
      ],
    },
  });
  await assertCssExplainersTooltip({
    view,
    selector: "aside",
    propertyName: "translate",
    functionIndex: 2,
    expected: {
      functionText: "calc(4 * random(--name, 2em, 16px))",
      tooltipLines: [
        "calc(4 * random(--name, 2em, 16px))",
        /calc\(4 \* random\(fixed 0\.\d+, 32px, 16px\)\)/,
        "calc(4 * 32px)",
        "128px",
      ],
    },
  });

  info("Select the <div> inside <aside> to check % computation");
  await selectNode("aside div", inspector);

  await assertCssExplainersTooltip({
    view,
    selector: "aside div",
    propertyName: "width",
    functionIndex: 0,
    expected: {
      functionText: "calc(50% - 2px)",
      tooltipText: ["calc(50% - 2px)", "calc(150px - 2px)", "148px"].join("\n"),
    },
  });
  await assertCssExplainersTooltip({
    view,
    selector: "aside div",
    propertyName: "width",
    lengthPercentageIndex: 0,
    expected: {
      lengthPercentageText: "50%",
      tooltipText: ["50%", "150px"].join("\n"),
    },
  });

  await assertCssExplainersTooltip({
    view,
    selector: "aside div",
    propertyName: "height",
    functionIndex: 0,
    expected: {
      functionText: "calc(25% + 2px)",
      tooltipText: ["calc(25% + 2px)", "calc(50px + 2px)", "52px"].join("\n"),
    },
  });

  await assertCssExplainersTooltip({
    view,
    selector: "aside div::after",
    propertyName: "width",
    functionIndex: 0,
    expected: {
      functionText: "calc(50% - 7px)",
      tooltipText: ["calc(50% - 7px)", "calc(150px - 7px)", "143px"].join("\n"),
    },
  });

  await assertCssExplainersTooltip({
    view,
    selector: "aside div::after",
    propertyName: "height",
    functionIndex: 0,
    expected: {
      functionText: "calc(25% + 7px)",
      tooltipText: ["calc(25% + 7px)", "calc(50px + 7px)", "57px"].join("\n"),
    },
  });

  await assertCssExplainersTooltip({
    view,
    selector: "aside div::after",
    propertyName: "margin-block",
    functionIndex: 0,
    expected: {
      functionText: "calc(10%)",
      tooltipText: ["calc(10%)", "10%", "30px"].join("\n"),
    },
  });

  info(
    "Select #single-length-percentage to check tooltip for non-px length/% not inside functions"
  );
  await selectNode("#single-length-percentage", inspector);

  await assertCssExplainersTooltip({
    view,
    selector: "#single-length-percentage",
    propertyName: "margin",
    lengthPercentageIndex: 0,
    expected: {
      lengthPercentageText: "10%",
      tooltipText: ["10%", "30px"].join("\n"),
    },
  });
  await assertCssExplainersTooltip({
    view,
    selector: "#single-length-percentage",
    propertyName: "margin",
    lengthPercentageIndex: 1,
    expected: {
      lengthPercentageText: "30em",
      tooltipText: ["30em", "480px"].join("\n"),
    },
  });
  await assertCssExplainersTooltip({
    view,
    selector: "#single-length-percentage",
    propertyName: "margin",
    lengthPercentageIndex: 2,
    expected: {
      lengthPercentageText: "40lh",
      tooltipText: ["40lh", "400px"].join("\n"),
    },
  });
  await assertCssExplainersTooltip({
    view,
    selector: "#single-length-percentage",
    propertyName: "background-position-x",
    lengthPercentageIndex: 0,
    expected: {
      lengthPercentageText: "90%",
      // the % can't be computed here, but we still show the tooltip, so later we
      // can add some text to explain _why_ we don't support it.
      tooltipText: ["90%"].join("\n"),
    },
  });
  await assertCssExplainersTooltip({
    view,
    selector: "#single-length-percentage",
    propertyName: "color",
    lengthPercentageIndex: 0,
    expected: {
      lengthPercentageText: "30%",
      // the % can't be computed here, it's already the "final" value.
      tooltipText: ["30%"].join("\n"),
    },
  });
  await assertCssExplainersTooltip({
    view,
    selector: "#single-length-percentage",
    propertyName: "color",
    lengthPercentageIndex: 1,
    expected: {
      lengthPercentageText: "90%",
      // the % can't be computed here, it's already the "final" value.
      tooltipText: ["90%"].join("\n"),
    },
  });
  await assertCssExplainersTooltip({
    view,
    selector: "#single-length-percentage",
    propertyName: "--x",
    lengthPercentageIndex: 0,
    expected: {
      lengthPercentageText: "85%",
      // the % can't be computed here since it's in a custom property declaration.
      tooltipText: ["85%"].join("\n"),
    },
  });

  info(
    "Select #nested-substitutions to check tooltip for nested arbitrary functions"
  );
  await selectNode("#nested-substitutions", inspector);
  await assertCssExplainersTooltip({
    view,
    selector: "#nested-substitutions",
    propertyName: "height",
    functionIndex: 0,
    expected: {
      functionText: "attr(var(--attr-name) type(<length>))",
      tooltipText: [
        "attr(var(--attr-name) type(<length>))",
        "10rem",
        "160px",
      ].join("\n"),
    },
  });
  await assertCssExplainersTooltip({
    view,
    selector: "#nested-substitutions",
    propertyName: "height",
    functionIndex: 1,
    expected: {
      functionText: "var(--attr-name)",
      tooltipText: ["var(--attr-name)", "data-x"].join("\n"),
    },
  });
  await assertCssExplainersTooltip({
    view,
    selector: "#nested-substitutions",
    propertyName: "color",
    functionIndex: 0,
    expected: {
      functionText: "var(var(--var-name))",
      tooltipText: ["var(var(--var-name))", "tomato"].join("\n"),
    },
  });
  await assertCssExplainersTooltip({
    view,
    selector: "#nested-substitutions",
    propertyName: "color",
    functionIndex: 1,
    expected: {
      functionText: "var(--var-name)",
      tooltipText: ["var(--var-name)", "--primary-color"].join("\n"),
    },
  });
});

async function assertCssExplainersTooltip({
  view,
  propertyName,
  selector,
  lengthPercentageIndex,
  functionIndex,
  expected,
}) {
  const { valueSpan } = getRuleViewProperty(view, selector, propertyName);

  let el;
  if (Number.isInteger(functionIndex)) {
    // Assert we have a function (e.g. `calc()`, `min()`, …)
    el =
      valueSpan.querySelectorAll(".css-explainers-function-name")[
        functionIndex
      ] || null;
    is(
      el
        .closest("[data-function-expression]")
        .getAttribute("data-function-expression"),
      expected.functionText,
      `Got expected data-function-expression attribute for function at index ${functionIndex} in ${propertyName} declaration`
    );
  } else if (Number.isInteger(lengthPercentageIndex)) {
    // Assert we have a length (e.g. `em`, `vw`, …) or percentage
    el =
      valueSpan.querySelectorAll("[data-length-expression]")[
        lengthPercentageIndex
      ] || null;
    is(
      el.getAttribute("data-length-expression"),
      expected.lengthPercentageText,
      `Got expected data-length-expression attribute for length/percentage at index ${lengthPercentageIndex} in ${propertyName} declaration`
    );
  } else {
    ok(false, "functionIndex or lengthPercentageIndex should be passed");
    return;
  }

  // Ensure that the element can be targetted from EventUtils.
  el.scrollIntoView();

  const tooltip = view.tooltips.getTooltip("interactiveTooltip");
  const onTooltipReady = tooltip.once("shown");
  EventUtils.synthesizeMouseAtCenter(
    el,
    { type: "mousemove" },
    el.ownerDocument.defaultView
  );
  await onTooltipReady;

  if (expected.tooltipText) {
    is(
      tooltip.panel.innerText,
      expected.tooltipText,
      `Tooltip has expected text for ${Number.isInteger(functionIndex) ? "function" : "length/percentage"} at index ${functionIndex} in ${propertyName} declaration`
    );
  } else if (expected.tooltipLines) {
    const tooltipLines = tooltip.panel.innerText.split("\n");
    is(
      tooltipLines.length,
      expected.tooltipLines.length,
      `Tooltip has expected number of lines for ${Number.isInteger(functionIndex) ? "function" : "length/percentage"} at index ${functionIndex} in ${propertyName} declaration`
    );
    for (let i = 0; i < tooltipLines.length; i++) {
      const tooltipLine = tooltipLines[i];
      const expectedLine = expected.tooltipLines[i];
      if (expectedLine instanceof RegExp) {
        Assert.stringMatches(
          tooltipLine,
          expectedLine,
          `Tooltip has matching line ${i + 1} for ${Number.isInteger(functionIndex) ? "function" : "length/percentage"} at index ${functionIndex} in ${propertyName} declaration`
        );
      } else {
        is(
          tooltipLine,
          expectedLine,
          `Tooltip has expected line ${i + 1} for ${Number.isInteger(functionIndex) ? "function" : "length/percentage"} at index ${functionIndex} in ${propertyName} declaration`
        );
      }
    }
  }

  info("Hide the tooltip");
  const onHidden = tooltip.once("hidden");
  // Move the mouse elsewhere to hide the tooltip
  EventUtils.synthesizeMouse(
    el.ownerDocument.body,
    1,
    1,
    { type: "mousemove" },
    el.ownerDocument.defaultView
  );
  await onHidden;
}
