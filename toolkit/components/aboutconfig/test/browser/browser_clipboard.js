/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["test.aboutconfig.copy.false", false],
      ["test.aboutconfig.copy.number", 10],
      ["test.aboutconfig.copy.spaces.1", " "],
      ["test.aboutconfig.copy.spaces.2", "  "],
      ["test.aboutconfig.copy.spaces.3", "   "],
      ["test.aboutconfig.copy.string", "010.5"],
    ],
  });
});

add_task(async function test_copy() {
  await AboutConfigTest.withNewTab(async function () {
    for (let [name, expectedString] of [
      [PREF_BOOLEAN_DEFAULT_TRUE, "true"],
      [PREF_BOOLEAN_USERVALUE_TRUE, "true"],
      [PREF_STRING_DEFAULT_EMPTY, ""],
      ["test.aboutconfig.copy.false", "false"],
      ["test.aboutconfig.copy.number", "10"],
      ["test.aboutconfig.copy.spaces.1", " "],
      ["test.aboutconfig.copy.spaces.2", "  "],
      ["test.aboutconfig.copy.spaces.3", "   "],
      ["test.aboutconfig.copy.string", "010.5"],
    ]) {
      // Limit the number of preferences shown so all the rows are visible.
      this.search(name);
      let row = this.getRow(name);

      let selectText = async target => {
        let { width, height } = target.getBoundingClientRect();

        // We intentionally turn off this a11y check, because the following
        // series of mouse events is purposefully targeting a non-interactive
        // text content. This action does not require the element to have an
        // interactive accessible to be done by assistive technology with caret
        // browsing (when/if supported), this rule check shall be ignored by
        // a11y_checks suite.
        AccessibilityUtils.setEnv({ mustHaveAccessibleRule: false });
        EventUtils.synthesizeMouse(
          target,
          1,
          1,
          { type: "mousedown" },
          this.browser.contentWindow
        );
        EventUtils.synthesizeMouse(
          target,
          width - 1,
          height - 1,
          { type: "mousemove" },
          this.browser.contentWindow
        );
        EventUtils.synthesizeMouse(
          target,
          width - 1,
          height - 1,
          { type: "mouseup" },
          this.browser.contentWindow
        );
        AccessibilityUtils.resetEnv();
      };

      // Drag across the name cell.
      await selectText(row.nameCell);
      Assert.ok(row.nameCell.contains(this.window.getSelection().anchorNode));
      await SimpleTest.promiseClipboardChange(name, async () => {
        await BrowserTestUtils.synthesizeKey(
          "c",
          { accelKey: true },
          this.browser
        );
      });

      // Drag across the value cell.
      await selectText(row.valueCell);
      let selection = this.window.getSelection();
      Assert.ok(row.valueCell.contains(selection.anchorNode));

      if (expectedString !== "") {
        // Non-empty values should have a selection.
        Assert.ok(!selection.isCollapsed);
        await SimpleTest.promiseClipboardChange(expectedString, async () => {
          await BrowserTestUtils.synthesizeKey(
            "c",
            { accelKey: true },
            this.browser
          );
        });
      } else {
        // Nothing is selected for an empty value.
        Assert.equal(selection.toString(), "");
      }
    }
  });
});

add_task(async function test_copy_multiple() {
  await AboutConfigTest.withNewTab(async function () {
    // Lines are separated by a single LF character on all platforms.
    let expectedString =
      "test.aboutconfig.copy.false\tfalse\t\t\n" +
      "test.aboutconfig.copy.number\t10\t\t\n" +
      "test.aboutconfig.copy.spaces.1\t \t\t\n" +
      "test.aboutconfig.copy.spaces.2\t  \t\t\n" +
      "test.aboutconfig.copy.spaces.3\t   \t\t\n" +
      "test.aboutconfig.copy.string\t010.5";

    this.search("test.aboutconfig.copy.");
    let startRow = this.getRow("test.aboutconfig.copy.false");
    let endRow = this.getRow("test.aboutconfig.copy.string");
    let { width, height } = endRow.valueCell.getBoundingClientRect();

    // Drag from the top left of the first row to the bottom right of the last.
    // We intentionally turn off this a11y check, because the following
    // series of mouse events is purposefully targeting a non-interactive
    // text content. This action does not require the element to have an
    // interactive accessible to be done by assistive technology with caret
    // browsing (when/if supported), this rule check shall be ignored by
    // a11y_checks suite.
    AccessibilityUtils.setEnv({ mustHaveAccessibleRule: false });
    EventUtils.synthesizeMouse(
      startRow.nameCell,
      1,
      1,
      { type: "mousedown" },
      this.browser.contentWindow
    );

    EventUtils.synthesizeMouse(
      endRow.valueCell,
      width - 1,
      height - 1,
      { type: "mousemove" },
      this.browser.contentWindow
    );
    EventUtils.synthesizeMouse(
      endRow.valueCell,
      width - 1,
      height - 1,
      { type: "mouseup" },
      this.browser.contentWindow
    );
    AccessibilityUtils.resetEnv();

    await SimpleTest.promiseClipboardChange(expectedString, async () => {
      await BrowserTestUtils.synthesizeKey(
        "c",
        { accelKey: true },
        this.browser
      );
    });
  });
});
