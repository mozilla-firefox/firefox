/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// NSMenuItem has no maximum width, so native menu items cut long labels
// themselves to the element's max-width, like crop="end" does for non-native
// menus.

const LONG_LABEL =
  "A tab group with a very long name that would make the menu far too wide";

function nativeTitle(id) {
  return window.windowUtils.getNativeMenuItemTitle(id);
}

function addMenuItem(id, maxWidth) {
  const item = document.createXULElement("menuitem");
  item.id = id;
  item.setAttribute("label", LONG_LABEL);
  if (maxWidth) {
    item.style.maxWidth = maxWidth;
  }
  document.getElementById("menu_EditPopup").appendChild(item);
  registerCleanupFunction(() => item.remove());
  return item;
}

add_task(async function test_label_cut_to_max_width() {
  const capped = addMenuItem("test-capped-item", "120px");
  addMenuItem("test-uncapped-item", null);

  // The menupopup creates the shadow DOM its items are slotted into, and
  // without which they have no style, on popupshowing. Opening a native menu
  // fires that before building the items, but forceUpdateNativeMenuAt doesn't.
  document.getElementById("menu_EditPopup").ensureInitialized();
  is(getComputedStyle(capped).maxWidth, "120px", "max-width applies");

  // Items inserted into a closed menu are only built when it next opens.
  window.windowUtils.forceUpdateNativeMenuAt("");

  const title = nativeTitle("test-capped-item");
  ok(title.endsWith("…"), `long label is cut with an ellipsis: "${title}"`);
  Assert.less(title.length, LONG_LABEL.length, "cut title is shorter");
  ok(
    LONG_LABEL.startsWith(title.slice(0, -1)),
    "cut title keeps the start of the label"
  );
  Assert.greater(title.length, 1, "cut title keeps some of the label");

  is(
    nativeTitle("test-uncapped-item"),
    LONG_LABEL,
    "label is left whole without a max-width"
  );

  capped.setAttribute("label", "Short");
  is(nativeTitle("test-capped-item"), "Short", "a label that fits is kept");

  capped.setAttribute("label", LONG_LABEL);
  is(
    nativeTitle("test-capped-item"),
    title,
    "changing the label back cuts it again"
  );
});
