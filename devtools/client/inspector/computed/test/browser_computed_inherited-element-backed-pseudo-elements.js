/* Any copyright is dedicated to the Public Domain.
 http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Check that inherited element backed pseudo element declarations (e.g. ::details-content)
// are properly displayed

const TEST_URI = `
  <style>
    details {
      color: gold;
    }
    details::details-content {
      color: dodgerblue;
    }
    details summary {
      color: violet;
    }

    details#nested {
      color: cyan;
    }
    details#nested summary {
      color: hotpink;
    }
    details#nested::details-content {
      color: rgb(10, 20, 30);
    }
  </style>
  <details open>
    <summary>
      Top-level summary
      <details id=nested open>
        <summary>nested summary</summary>
        <p id=matches>in nested details</p>
      </details>
    </summary>
  </details>`;

add_task(async function () {
  await pushPref("layout.css.details-content.enabled", true);
  await addTab("data:text/html;charset=utf-8," + encodeURIComponent(TEST_URI));
  const { inspector, view } = await openComputedView();
  await selectNode("p#matches", inspector);

  info("Checking the property itself");
  is(
    getComputedViewPropertyView(view, "color").valueNode.textContent,
    "rgb(10, 20, 30)",
    "Got expected computed value for color on p#matches"
  );

  info("Checking matched selectors");
  const container = await getComputedViewMatchedRules(view, "color");
  Assert.deepEqual(
    [...container.querySelectorAll("p")].map(matchEl =>
      [...matchEl.querySelectorAll("div")].map(el => el.textContent)
    ),
    [
      ["details#nested::details-content", "rgb(10, 20, 30)"],
      ["details::details-content", "dodgerblue"],
      ["details#nested", "cyan"],
      ["details", "gold"],
      ["details summary", "violet"],
      [":root", "canvastext"],
    ],
    "Got the expected matched selectors, including ::details-content ones"
  );
});
