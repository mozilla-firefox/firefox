requestLongerTimeout(2);

/**
 * Checks that the page renders without issue, and that the expected elements
 * are there.
 */
add_task(async function test_about_inference_enabled() {
  await openAboutInference({
    runInPage: async ({ selectors }) => {
      const { document, window } = content;

      function checkElementIsVisible(expectVisible, name) {
        const expected = expectVisible ? "visible" : "hidden";
        const element = document.querySelector(selectors[name]);
        ok(Boolean(element), `Element ${name} was found.`);
        const { visibility } = window.getComputedStyle(element);
        is(
          visibility,
          expected,
          `Element ${name} was not ${expected} but should be.`
        );
      }
      checkElementIsVisible(true, "pageHeader");
      const element = document.querySelector(selectors.warning);
      const { display } = window.getComputedStyle(element);
      is(display, "none", "The warning should be hidden");
    },
  });
});

/**
 * Checks that the page renders with a warning when ml is disabled.
 */
add_task(async function test_about_inference_disabled() {
  await openAboutInference({
    prefs: [["browser.ml.enable", false]],
    runInPage: async ({ selectors }) => {
      const { document, window } = content;
      const element = document.querySelector(selectors.warning);
      const { display } = window.getComputedStyle(element);
      is(display, "block", "The warning should be visible");
      is(
        element.getAttribute("data-l10n-id"),
        "about-inference-ml-disabled",
        "The warning asks to enable local inference"
      );
      document.getElementById("warning.enable").click();
      await ContentTaskUtils.waitForCondition(
        () => window.getComputedStyle(element).display === "none",
        "Enabling local inference hides the warning"
      );
    },
  });
});
/**
 * Checks that the inference process is shown on the page
 */
add_task(async function test_about_inference_process() {
  await openAboutInference({
    runInference: true,
    runInPage: async ({ selectors }) => {
      function waitForInnerHTML(selector, substring, interval = 100) {
        return new Promise((resolve, reject) => {
          const { document } = content;
          const element = document.querySelector(selector);
          if (!element) {
            reject(new Error(`No element found with selector: ${selector}`));
            return;
          }
          const checkInnerHTML = () => {
            info(`Checking innerHTML of element with selector: ${selector}`);
            if (element.innerHTML.includes(substring)) {
              info(
                `Substring "${substring}" found in element with selector: ${selector}`
              );
              resolve();
            } else {
              // eslint-disable-next-line mozilla/no-arbitrary-setTimeout
              content.setTimeout(checkInnerHTML, interval);
            }
          };
          checkInnerHTML();
        });
      }

      // When the process is shown, we display its memory size in MB
      await waitForInnerHTML(selectors.processes, "MB");
    },
  });
});

/**
 * Lists the inference devices through the devices panel.
 */
add_task(async function test_about_inference_devices() {
  const { cleanup } = await setup({
    prefs: [["browser.ml.llama.hwInference", true]],
  });
  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:inference",
    true
  );
  try {
    await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
      const byId = id => content.document.getElementById(id);
      byId("subtab-devices").click();
      Assert.ok(byId("inference-pad").hidden, "The pad gives way");
      await ContentTaskUtils.waitForCondition(
        () => byId("devices.table").querySelector("td"),
        "The devices panel lists the inference devices",
        100,
        300
      );
      const kinds = [...byId("devices.table").querySelectorAll("tbody tr")].map(
        tr => tr.children[2].textContent
      );
      Assert.ok(kinds.includes("cpu"), "The CPU is listed");
    });
  } finally {
    BrowserTestUtils.removeTab(tab);
    await cleanup();
  }
});

/**
 * Runs a generation through the text generation panel.
 */
add_task(async function test_about_inference_text_generation() {
  const { cleanup } = await setup({
    prefs: [
      ["browser.ml.llama.hwInference", true],
      [
        "browser.ml.modelHubRootUrl",
        "chrome://mochitests/content/browser/toolkit/components/ml/tests/browser/data",
      ],
    ],
  });
  const tab = await BrowserTestUtils.openNewForegroundTab(
    gBrowser,
    "about:inference",
    true
  );
  try {
    await SpecialPowers.spawn(tab.linkedBrowser, [], async () => {
      const byId = id => content.document.getElementById(id);
      byId("subtab-devices").click();
      await ContentTaskUtils.waitForCondition(
        () => byId("devices.table").querySelector("td"),
        "The devices panel lists the inference devices",
        100,
        300
      );
      const kinds = [...byId("devices.table").querySelectorAll("tbody tr")].map(
        tr => tr.children[2].textContent
      );
      Assert.equal(
        byId("tg.device").options.length,
        2 + kinds.filter(kind => kind !== "cpu").length,
        "Every accelerator joins the text generation device select"
      );
      byId("subtab-text-generation").click();
      // Let the accessibility tree pick up the unhidden panel.
      await new Promise(resolve => content.requestAnimationFrame(resolve));
      await new Promise(resolve => content.requestAnimationFrame(resolve));
      byId("tg.hub").value = "";
      byId("tg.model").value = "Mozilla/test-llama/TinyStories-656K.Q8_0.gguf";
      byId("tg.prompt").value = "Once upon a time";
      byId("tg.maxTokens").value = "8";
      byId("tg.threads").value = "1";
      byId("tg.engines").value = "2";
      byId("tg.runs").value = "2";
      Assert.ok(
        byId("tg.kvCacheDtype").querySelector("option[value=q8_0]").disabled,
        "A quantized KV cache needs flash attention"
      );
      byId("tg.flashAttn").click();
      Assert.ok(
        !byId("tg.kvCacheDtype").querySelector("option[value=q8_0]").disabled,
        "Flash attention allows a quantized KV cache"
      );
      byId("tg.flashAttn").click();
      Assert.ok(
        byId("tg.kvCacheDtype").querySelector("option[value=q8_0]").disabled,
        "Turning flash attention off disables it again"
      );
      byId("tg.run").click();
      await ContentTaskUtils.waitForCondition(
        () => !byId("tg.run").disabled,
        "The runs finish",
        500,
        120
      );
      Assert.ok(
        byId("tg.summary").querySelector("td"),
        `The summary appears after every run: ${byId("tg.output").value}`
      );
      const rows = [...byId("tg.results").querySelectorAll("tbody tr")].map(
        tr => [...tr.children].map(td => td.textContent)
      );
      Assert.equal(rows.length, 4, "Two engines ran twice each");
      Assert.notEqual(rows[0][2], "-", "An engine's first run reports init");
      Assert.equal(rows[1][2], "-", "Its second run does not");
      Assert.notEqual(rows[0][7], "-", "Output tokens are reported");
      Assert.notEqual(rows[0][9], "-", "Throughput is reported");
      const summary = byId("tg.summary").querySelectorAll("tbody tr");
      Assert.equal(summary.length, 6, "Every timing is summarised");
    });
  } finally {
    BrowserTestUtils.removeTab(tab);
    await cleanup();
  }
});
