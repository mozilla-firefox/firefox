/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/**
 * This tests dynamic results.
 */

"use strict";

const DYNAMIC_TYPE_NAME = "test";

const DYNAMIC_TYPE_VIEW_TEMPLATE = {
  children: [
    {
      name: "selectable",
      tag: "span",
      attributes: {
        selectable: "true",
      },
    },
    {
      name: "text",
      tag: "span",
    },
    {
      name: "buttonBox",
      tag: "span",
      children: [
        {
          name: "button1",
          tag: "span",
          attributes: {
            role: "button",
            attribute_to_remove: "value",
          },
        },
        {
          name: "button2",
          tag: "span",
          attributes: {
            role: "button",
          },
        },
      ],
    },
  ],
};

const IS_UPGRADING_SCHEMELESS = SpecialPowers.getBoolPref(
  "dom.security.https_first_schemeless"
);
// eslint-disable-next-line sdl/no-insecure-url
const DEFAULT_URL_SCHEME = IS_UPGRADING_SCHEMELESS ? "https://" : "http://";
const DUMMY_PAGE =
  DEFAULT_URL_SCHEME +
  "example.com/browser/browser/base/content/test/general/dummy_page.html";

// Tests that the view is created correctly from the view template.
add_task(async function viewCreated() {
  await withDynamicTypeProvider(async () => {
    // Do a search.
    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: "test",
      waitForFocus: SimpleTest.waitForFocus,
    });

    // Get the row.
    let row = await UrlbarTestUtils.waitForAutocompleteResultAt(window, 1);
    Assert.equal(
      row.result.type,
      UrlbarShared.RESULT_TYPE.DYNAMIC,
      "row.result.type"
    );
    Assert.equal(
      row.getAttribute("dynamicType"),
      DYNAMIC_TYPE_NAME,
      "row[dynamicType]"
    );
    Assert.ok(
      !row.hasAttribute("has-url"),
      "Row should not have has-url since view template does not contain .urlbarView-url"
    );
    let inner = row.querySelector(".urlbarView-row-inner");
    Assert.ok(inner, ".urlbarView-row-inner should exist");

    // Check the DOM.
    checkDOM(inner, DYNAMIC_TYPE_VIEW_TEMPLATE.children);

    await UrlbarTestUtils.promisePopupClose(window);
  });
});

// Tests that the view is updated correctly.
async function checkViewUpdated(provider) {
  await withDynamicTypeProvider(async () => {
    // Test a few different search strings.  The dynamic result view will be
    // updated to reflect the current string.
    for (let searchString of ["test", "some other string", "and another"]) {
      // Do a search.
      await UrlbarTestUtils.promiseAutocompleteResultPopup({
        window,
        value: searchString,
        waitForFocus: SimpleTest.waitForFocus,
      });

      let row = await UrlbarTestUtils.waitForAutocompleteResultAt(window, 1);
      let text = row.querySelector(
        `.urlbarView-dynamic-${DYNAMIC_TYPE_NAME}-text`
      );

      // The "searchString" attribute of these elements should be updated.
      let elementNames = ["selectable", "text", "button1", "button2"];
      for (let name of elementNames) {
        let element = row.querySelector(
          `.urlbarView-dynamic-${DYNAMIC_TYPE_NAME}-${name}`
        );
        Assert.equal(
          element.getAttribute("searchString"),
          searchString,
          'element.getAttribute("searchString")'
        );
      }

      let button1 = row.querySelector(
        `.urlbarView-dynamic-${DYNAMIC_TYPE_NAME}-button1`
      );

      Assert.equal(
        button1.hasAttribute("attribute_to_remove"),
        false,
        "Attribute should be removed"
      );

      // text.textContent should be updated.
      Assert.equal(
        text.textContent,
        `result.payload.searchString is: ${searchString}`,
        "text.textContent"
      );

      await UrlbarTestUtils.promisePopupClose(window);
    }
  }, provider);
}

add_task(async function checkViewUpdatedPlain() {
  await checkViewUpdated(new TestProvider());
});

// Tests that selection correctly moves through buttons and selectables in a
// dynamic result.
add_task(async function selection() {
  await withDynamicTypeProvider(async () => {
    // Add a visit so we have at least one result after the dynamic result.
    await PlacesUtils.history.clear();
    await PlacesTestUtils.addVisits("http://example.com/test");

    // Do a search.
    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: "test",
      waitForFocus: SimpleTest.waitForFocus,
    });

    // Sanity check that the dynamic result is at index 1.
    let row = await UrlbarTestUtils.waitForAutocompleteResultAt(window, 1);
    Assert.equal(
      row.result.type,
      UrlbarShared.RESULT_TYPE.DYNAMIC,
      "row.result.type"
    );

    // The heuristic result will be selected.  TAB from the heuristic through
    // all the selectable elements in the dynamic result.
    let selectables = ["selectable", "button1", "button2"];
    for (let name of selectables) {
      let element = row.querySelector(
        `.urlbarView-dynamic-${DYNAMIC_TYPE_NAME}-${name}`
      );
      Assert.ok(element, "Sanity check element");
      EventUtils.synthesizeKey("KEY_Tab");
      Assert.equal(
        UrlbarTestUtils.getSelectedElement(window),
        element,
        `Selected element: ${name}`
      );
      Assert.equal(
        UrlbarTestUtils.getSelectedRowIndex(window),
        1,
        "Row at index 1 selected"
      );
      Assert.equal(UrlbarTestUtils.getSelectedRow(window), row, "Row selected");
    }

    // TAB again to select the result after the dynamic result.
    EventUtils.synthesizeKey("KEY_Tab");
    Assert.equal(
      UrlbarTestUtils.getSelectedRowIndex(window),
      2,
      "Row at index 2 selected"
    );
    Assert.notEqual(
      UrlbarTestUtils.getSelectedRow(window),
      row,
      "Row is not selected"
    );

    // SHIFT+TAB back through the dynamic result.
    for (let name of selectables.reverse()) {
      let element = row.querySelector(
        `.urlbarView-dynamic-${DYNAMIC_TYPE_NAME}-${name}`
      );
      Assert.ok(element, "Sanity check element");
      EventUtils.synthesizeKey("KEY_Tab", { shiftKey: true });
      Assert.equal(
        UrlbarTestUtils.getSelectedElement(window),
        element,
        `Selected element: ${name}`
      );
      Assert.equal(
        UrlbarTestUtils.getSelectedRowIndex(window),
        1,
        "Row at index 1 selected"
      );
      Assert.equal(UrlbarTestUtils.getSelectedRow(window), row, "Row selected");
    }

    // SHIFT+TAB again to select the heuristic result.
    EventUtils.synthesizeKey("KEY_Tab", { shiftKey: true });
    Assert.equal(
      UrlbarTestUtils.getSelectedRowIndex(window),
      0,
      "Row at index 0 selected"
    );
    Assert.notEqual(
      UrlbarTestUtils.getSelectedRow(window),
      row,
      "Row is not selected"
    );

    await UrlbarTestUtils.promisePopupClose(window);
    await PlacesUtils.history.clear();
  });
});

// Tests picking elements in a dynamic result.
add_task(async function pick() {
  await withDynamicTypeProvider(async provider => {
    let selectables = ["selectable", "button1", "button2"];
    for (let i = 0; i < selectables.length; i++) {
      let selectable = selectables[i];

      // Do a search.
      await UrlbarTestUtils.promiseAutocompleteResultPopup({
        window,
        value: "test",
        waitForFocus: SimpleTest.waitForFocus,
      });

      // Sanity check that the dynamic result is at index 1.
      let row = await UrlbarTestUtils.waitForAutocompleteResultAt(window, 1);
      Assert.equal(
        row.result.type,
        UrlbarShared.RESULT_TYPE.DYNAMIC,
        "row.result.type"
      );

      // The heuristic result will be selected.  TAB from the heuristic
      // to the selectable element.
      let element = row.querySelector(
        `.urlbarView-dynamic-${DYNAMIC_TYPE_NAME}-${selectable}`
      );
      Assert.ok(element, "Sanity check element");
      EventUtils.synthesizeKey("KEY_Tab", { repeat: i + 1 });
      Assert.equal(
        UrlbarTestUtils.getSelectedElement(window),
        element,
        `Selected element: ${name}`
      );

      // Pick the element.
      let pickPromise = provider.promisePick();
      await UrlbarTestUtils.promisePopupClose(window, () =>
        EventUtils.synthesizeKey("KEY_Enter")
      );
      let [result, pickedElement] = await pickPromise;
      UrlbarTestUtils.assertPickedResult(
        result,
        pickedElement,
        row.result,
        element
      );
    }
  });
});

// Tests picking elements in a dynamic result.
add_task(async function shouldNavigate() {
  /**
   * A dummy provider that providers results with a `url` property.
   */
  class TestShouldNavigateProvider extends TestProvider {
    /**
     * @param {object} context - Data regarding the context of the query.
     * @param {Function} addCallback - Function to add a result to the query.
     */
    async startQuery(context, addCallback) {
      addCallback(
        this,
        new UrlbarResult({
          type: UrlbarShared.RESULT_TYPE.DYNAMIC,
          source: UrlbarShared.RESULT_SOURCE.OTHER_LOCAL,
          suggestedIndex: 1,
          payload: {
            dynamicType: DYNAMIC_TYPE_NAME,
            url: DUMMY_PAGE,
            searchString: context.searchString,
          },
        })
      );
    }
  }

  await withDynamicTypeProvider(async provider => {
    // Do a search.
    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: "test",
      waitForFocus: SimpleTest.waitForFocus,
    });

    // Sanity check that the dynamic result is at index 1.
    let row = await UrlbarTestUtils.waitForAutocompleteResultAt(window, 1);
    Assert.equal(
      row.result.type,
      UrlbarShared.RESULT_TYPE.DYNAMIC,
      "row.result.type"
    );

    // The heuristic result will be selected.  TAB from the heuristic
    // to the selectable element.
    let element = row.querySelector(
      `.urlbarView-dynamic-${DYNAMIC_TYPE_NAME}-selectable`
    );
    Assert.ok(element, "Sanity check element");
    EventUtils.synthesizeKey("KEY_Tab", { repeat: 1 });
    Assert.equal(
      UrlbarTestUtils.getSelectedElement(window),
      element,
      `Selected element: ${name}`
    );

    // Pick the element.
    let pickPromise = provider.promisePick();
    await UrlbarTestUtils.promisePopupClose(window, () =>
      EventUtils.synthesizeKey("KEY_Enter")
    );
    // Verify that onEngagement was still called.
    let [result, pickedElement] = await pickPromise;
    UrlbarTestUtils.assertPickedResult(
      result,
      pickedElement,
      row.result,
      element
    );

    await BrowserTestUtils.browserLoaded(gBrowser.selectedBrowser);
    is(
      gBrowser.currentURI.spec,
      DUMMY_PAGE,
      "We navigated to payload.url when result selected"
    );

    BrowserTestUtils.startLoadingURIString(
      gBrowser.selectedBrowser,
      "about:home"
    );
    await BrowserTestUtils.browserLoaded(
      gBrowser.selectedBrowser,
      false,
      "about:home"
    );

    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: "test",
      waitForFocus: SimpleTest.waitForFocus,
    });

    row = await UrlbarTestUtils.waitForAutocompleteResultAt(window, 1);
    element = row.querySelector(
      `.urlbarView-dynamic-${DYNAMIC_TYPE_NAME}-selectable`
    );

    pickPromise = provider.promisePick();
    EventUtils.synthesizeMouseAtCenter(element, {});
    [result, pickedElement] = await pickPromise;
    UrlbarTestUtils.assertPickedResult(
      result,
      pickedElement,
      row.result,
      element
    );

    await BrowserTestUtils.browserLoaded(gBrowser.selectedBrowser);
    is(
      gBrowser.currentURI.spec,
      DUMMY_PAGE,
      "We navigated to payload.url when result is clicked"
    );
  }, new TestShouldNavigateProvider());
});

// Tests applying highlighting to a dynamic result.
add_task(async function highlighting() {
  /**
   * Provides a dynamic result with highlighted text.
   */
  class TestHighlightProvider extends TestProvider {
    startQuery(context, addCallback) {
      this._tokens = context.tokens;
      let result = new UrlbarResult({
        type: UrlbarShared.RESULT_TYPE.DYNAMIC,
        source: UrlbarShared.RESULT_SOURCE.OTHER_LOCAL,
        suggestedIndex: 1,
        payload: {
          dynamicType: DYNAMIC_TYPE_NAME,
          text: "Test title",
        },
        highlights: {
          text: UrlbarShared.HIGHLIGHT.TYPED,
        },
      });
      addCallback(this, result);
    }

    getViewUpdate(result) {
      let { value: textContent, highlights } =
        result.getDisplayableValueAndHighlights("text", {
          tokens: this._tokens,
        });
      return {
        text: { textContent, highlights },
      };
    }
  }

  // Test that highlighting is applied.
  await withDynamicTypeProvider(async () => {
    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: "title",
      waitForFocus: SimpleTest.waitForFocus,
    });

    let row = await UrlbarTestUtils.waitForAutocompleteResultAt(window, 1);
    Assert.equal(
      row.result.type,
      UrlbarShared.RESULT_TYPE.DYNAMIC,
      "row.result.type"
    );
    let parentTextNode = row.querySelector(
      `.urlbarView-dynamic-${DYNAMIC_TYPE_NAME}-text`
    );
    let highlightedTextNode = row.querySelector(
      `.urlbarView-dynamic-${DYNAMIC_TYPE_NAME}-text > strong`
    );
    Assert.equal(parentTextNode.firstChild.textContent, "Test ");
    Assert.equal(
      highlightedTextNode.textContent,
      "title",
      "The highlighting was applied successfully."
    );

    // Do two more searches using search strings that don't appear in the title.
    // Nothing should be highlighted. The first search will reuse the row from
    // the search above. After that, we'll close the view and revert the urlbar
    // so that the second search will use an entirely new row.
    for (let value of ["yyyyy", "zzzzz"]) {
      await UrlbarTestUtils.promiseAutocompleteResultPopup({
        window,
        value,
        waitForFocus: SimpleTest.waitForFocus,
      });
      row = await UrlbarTestUtils.waitForAutocompleteResultAt(window, 1);
      Assert.equal(
        row.result.type,
        UrlbarShared.RESULT_TYPE.DYNAMIC,
        "row.result.type"
      );
      parentTextNode = row.querySelector(
        `.urlbarView-dynamic-${DYNAMIC_TYPE_NAME}-text`
      );
      highlightedTextNode = row.querySelector(
        `.urlbarView-dynamic-${DYNAMIC_TYPE_NAME}-text > strong`
      );
      Assert.equal(parentTextNode.firstChild.textContent, "Test title");
      Assert.ok(!highlightedTextNode, "The <strong> child node was deleted.");

      await UrlbarTestUtils.promisePopupClose(window);
      gURLBar.handleRevert();
    }
  }, new TestHighlightProvider());

  /**
   * Provides a dynamic result with highlighted text that is then overridden.
   */
  class TestHighlightProviderOveridden extends TestHighlightProvider {
    getViewUpdate(_result) {
      return {
        text: {
          textContent: "Test title",
        },
      };
    }
  }

  // Test that highlighting is not applied when overridden from getViewUpdate.
  await withDynamicTypeProvider(async () => {
    // Do a search.
    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: "test",
      waitForFocus: SimpleTest.waitForFocus,
    });

    let row = await UrlbarTestUtils.waitForAutocompleteResultAt(window, 1);
    Assert.equal(
      row.result.type,
      UrlbarShared.RESULT_TYPE.DYNAMIC,
      "row.result.type"
    );
    let parentTextNode = row.querySelector(
      `.urlbarView-dynamic-${DYNAMIC_TYPE_NAME}-text`
    );
    let highlightedTextNode = row.querySelector(
      `.urlbarView-dynamic-${DYNAMIC_TYPE_NAME}-text > strong`
    );
    Assert.equal(
      parentTextNode.firstChild.textContent,
      "Test title",
      "No highlighting was applied"
    );
    Assert.ok(!highlightedTextNode, "The <strong> child node was deleted.");
  }, new TestHighlightProviderOveridden());
});

// View templates that contain a top-level `.urlbarView-url` element should
// cause `has-url` to be set on `.urlbarView-row`.
add_task(async function hasUrlTopLevel() {
  await doAttributesTest({
    viewTemplate: {
      name: "url",
      tag: "span",
      classList: ["urlbarView-url"],
    },
    viewUpdate: {
      url: {
        textContent: "https://example.com/",
      },
    },
    expectedAttributes: {
      "has-url": true,
    },
  });
});

// View templates that contain a descendant `.urlbarView-url` element should
// cause `has-url` to be set on `.urlbarView-row`.
add_task(async function hasUrlDescendant() {
  await doAttributesTest({
    viewTemplate: {
      children: [
        {
          children: [
            {
              children: [
                {
                  name: "url",
                  tag: "span",
                  classList: ["urlbarView-url"],
                },
              ],
            },
          ],
        },
      ],
    },
    viewUpdate: {
      url: {
        textContent: "https://example.com/",
      },
    },
    expectedAttributes: {
      "has-url": true,
    },
  });
});

// View templates that contain a top-level `.urlbarView-action` element should
// cause `has-action` to be set on `.urlbarView-row`.
add_task(async function hasActionTopLevel() {
  await doAttributesTest({
    viewTemplate: {
      name: "action",
      tag: "span",
      classList: ["urlbarView-action"],
    },
    viewUpdate: {
      action: {
        textContent: "Some action text",
      },
    },
    expectedAttributes: {
      "has-action": true,
    },
  });
});

// View templates that contain a descendant `.urlbarView-action` element should
// cause `has-action` to be set on `.urlbarView-row`.
add_task(async function hasActionDescendant() {
  await doAttributesTest({
    viewTemplate: {
      children: [
        {
          children: [
            {
              children: [
                {
                  name: "action",
                  tag: "span",
                  classList: ["urlbarView-action"],
                },
              ],
            },
          ],
        },
      ],
    },
    viewUpdate: {
      action: {
        textContent: "Some action text",
      },
    },
    expectedAttributes: {
      "has-action": true,
    },
  });
});

// Tests whether 'dynamicType' attribute is cleared.
add_task(async function clear_dynamicType_attribute() {
  /**
   * A dummy provider that providers results with a `url` property.
   */
  class TestIndex0Provider extends TestProvider {
    /**
     * @param {object} context - Data regarding the context of the query.
     * @param {Function} addCallback - Function to add a result to the query.
     */
    async startQuery(context, addCallback) {
      addCallback(
        this,
        new UrlbarResult({
          type: UrlbarShared.RESULT_TYPE.DYNAMIC,
          source: UrlbarShared.RESULT_SOURCE.OTHER_LOCAL,
          suggestedIndex: 0,
          payload: {
            dynamicType: DYNAMIC_TYPE_NAME,
          },
        })
      );
    }
  }

  await withDynamicTypeProvider(async provider => {
    // Do a search.
    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: "test",
      waitForFocus: SimpleTest.waitForFocus,
    });
    // Check the dynamicType.
    let row = await UrlbarTestUtils.waitForAutocompleteResultAt(window, 0);
    Assert.equal(row.getAttribute("dynamicType"), "test");

    // Unregister the provider to show normal result.
    let providersManager = ProvidersManager.getInstanceForSap("urlbar");
    providersManager.unregisterProvider(provider);
    // Do a search again.
    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: "test",
      waitForFocus: SimpleTest.waitForFocus,
    });
    Assert.ok(!row.hasAttribute("dynamicType"));

    await UrlbarTestUtils.promisePopupClose(window);
  }, new TestIndex0Provider());
});

// View templates that contain descendant `.urlbarView-url` and
// `.urlbarView-action` elements should cause `has-url` and `has-action` to be
// set on `.urlbarView-row`.
add_task(async function hasUrlAndActionDescendant() {
  await doAttributesTest({
    viewTemplate: {
      children: [
        {
          children: [
            {
              children: [
                {
                  name: "url",
                  tag: "span",
                  classList: ["urlbarView-url"],
                },
              ],
            },
            {
              name: "action",
              tag: "span",
              classList: ["urlbarView-action"],
            },
          ],
        },
      ],
    },
    viewUpdate: {
      url: {
        textContent: "https://example.com/",
      },
      action: {
        textContent: "Some action text",
      },
    },
    expectedAttributes: {
      "has-url": true,
      "has-action": true,
    },
  });
});

async function doAttributesTest({
  viewTemplate,
  viewUpdate,
  expectedAttributes,
}) {
  expectedAttributes = {
    "has-url": false,
    "has-action": false,
    ...expectedAttributes,
  };

  let provider = new TestProvider();
  provider.getViewTemplate = () => viewTemplate;
  provider.getViewUpdate = () => viewUpdate;

  await withDynamicTypeProvider(async () => {
    await UrlbarTestUtils.promiseAutocompleteResultPopup({
      window,
      value: "test",
      waitForFocus: SimpleTest.waitForFocus,
    });

    let row = await UrlbarTestUtils.waitForAutocompleteResultAt(window, 1);
    Assert.equal(
      row.result.type,
      UrlbarShared.RESULT_TYPE.DYNAMIC,
      "Sanity check: The expected row is present"
    );
    for (let [name, expected] of Object.entries(expectedAttributes)) {
      Assert.equal(
        row.hasAttribute(name),
        expected,
        "Row should have attribute as expected: " + name
      );
    }

    await UrlbarTestUtils.promisePopupClose(window);
  }, provider);
}

/**
 * Provides a dynamic result.
 */
class TestProvider extends UrlbarTestUtils.TestProvider {
  async startQuery(context, addCallback) {
    addCallback(
      this,
      new UrlbarResult({
        type: UrlbarShared.RESULT_TYPE.DYNAMIC,
        source: UrlbarShared.RESULT_SOURCE.OTHER_LOCAL,
        suggestedIndex: 1,
        payload: {
          dynamicType: DYNAMIC_TYPE_NAME,
          searchString: context.searchString,
        },
      })
    );
  }

  getViewTemplate(_result) {
    return DYNAMIC_TYPE_VIEW_TEMPLATE;
  }

  getViewUpdate(result) {
    return {
      selectable: {
        textContent: "Selectable",
        attributes: {
          searchString: result.payload.searchString,
        },
      },
      text: {
        textContent: `result.payload.searchString is: ${result.payload.searchString}`,
        attributes: {
          searchString: result.payload.searchString,
        },
      },
      button1: {
        textContent: "Button 1",
        attributes: {
          searchString: result.payload.searchString,
          attribute_to_remove: null,
        },
      },
      button2: {
        textContent: "Button 2",
        attributes: {
          searchString: result.payload.searchString,
        },
      },
    };
  }

  onEngagement(queryContext, controller, details) {
    if (this._pickPromiseResolve) {
      let { result, element } = details;
      this._pickPromiseResolve([result, element]);
      delete this._pickPromiseResolve;
      delete this._pickPromise;
    }
  }

  promisePick() {
    this._pickPromise = new Promise(resolve => {
      this._pickPromiseResolve = resolve;
    });
    return this._pickPromise;
  }
}

/**
 * Provides a dynamic result.
 *
 * @param {object} callback - Function that runs the body of the test.
 * @param {object} provider - The dummy provider to use.
 */
async function withDynamicTypeProvider(
  callback,
  provider = new TestProvider()
) {
  // Add a provider of the dynamic type.
  let providersManager = ProvidersManager.getInstanceForSap("urlbar");
  providersManager.registerProvider(provider);

  await callback(provider);

  // Clean up.
  providersManager.unregisterProvider(provider);
}

function checkDOM(parentNode, expectedChildren) {
  info(
    `checkDOM: Checking parentNode id=${parentNode.id} className=${parentNode.className}`
  );
  for (let i = 0; i < expectedChildren.length; i++) {
    let child = expectedChildren[i];
    let actualChild = parentNode.children[i];
    info(`checkDOM: Checking expected child: ${JSON.stringify(child)}`);
    Assert.ok(actualChild, "actualChild should exist");
    Assert.equal(actualChild.tagName, child.tag, "child.tag");
    Assert.equal(actualChild.getAttribute("name"), child.name, "child.name");
    Assert.ok(
      actualChild.classList.contains(
        `urlbarView-dynamic-${DYNAMIC_TYPE_NAME}-${child.name}`
      ),
      "child.name should be in classList"
    );
    // We have to use startsWith/endsWith since the middle of the ID is a random
    // number.
    Assert.ok(actualChild.id.startsWith("urlbarView-row-"));
    Assert.ok(
      actualChild.id.endsWith(child.name),
      "The child was assigned the correct ID."
    );
    for (let [name, value] of Object.entries(child.attributes || {})) {
      if (name == "attribute_to_remove") {
        Assert.equal(
          actualChild.hasAttribute(name),
          false,
          `attribute: ${name}`
        );
        continue;
      }
      Assert.equal(actualChild.getAttribute(name), value, `attribute: ${name}`);
    }
    for (let name of child.classList || []) {
      Assert.ok(actualChild.classList.contains(name), `classList: ${name}`);
    }
    if (child.children) {
      checkDOM(actualChild, child.children);
    }
  }
}
