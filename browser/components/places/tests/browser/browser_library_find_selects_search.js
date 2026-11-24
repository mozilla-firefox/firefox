/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_task(async function test_find_selects_existing_search_text() {
  const library = await promiseLibrary("History");
  registerCleanupFunction(() => promiseLibraryClosed(library));

  const searchFilter = library.document.getElementById("searchFilter");
  const searchText = "mozilla";
  searchFilter.value = searchText;
  await searchFilter.updateComplete;

  library.document.getElementById("placeContent").focus();
  Assert.notEqual(
    library.document.activeElement,
    searchFilter,
    "The search box is not focused before using the find shortcut"
  );

  EventUtils.synthesizeKey("f", { accelKey: true }, library);
  await TestUtils.waitForCondition(
    () => library.document.activeElement == searchFilter,
    "Waiting for the search box to be focused"
  );

  const { inputEl } = searchFilter;
  Assert.equal(inputEl.value, searchText, "The search text is kept");
  Assert.equal(inputEl.selectionStart, 0, "The selection starts at 0");
  Assert.equal(
    inputEl.selectionEnd,
    searchText.length,
    "The whole search text is selected"
  );
});
