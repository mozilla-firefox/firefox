/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// A plain `import` of a resource://*.sys.mjs module doesn't work here: this
// document's <script type="module"> uses the standard web-platform module
// loader, which can't resolve resource:// sys.mjs files -- those live in a
// separate, privileged module registry reached only via importESModule.
const { getEffectiveList, removeSite, whenReady } = ChromeUtils.importESModule(
  "resource:///modules/SiteAllowlist.sys.mjs"
);

const list = document.getElementById("list");
const emptyMessage = document.getElementById("empty-message");

async function onRemove(entry) {
  await removeSite(entry);
  render();
}

function buildRow(entry) {
  const row = document.createElement("moz-box-item");
  row.label = entry;

  const removeButton = document.createElement("moz-button");
  removeButton.slot = "actions";
  removeButton.type = "icon ghost";
  removeButton.iconSrc = "chrome://global/skin/icons/delete.svg";
  document.l10n.setAttributes(removeButton, "about-allowlist-remove-button", {
    entry,
  });
  removeButton.addEventListener("click", () => onRemove(entry));
  row.append(removeButton);

  return row;
}

function render() {
  const entries = getEffectiveList();
  list.replaceChildren();
  emptyMessage.hidden = !!entries.length;
  if (!entries.length) {
    return;
  }
  const boxGroup = document.createElement("moz-box-group");
  for (const entry of entries) {
    boxGroup.append(buildRow(entry));
  }
  list.append(boxGroup);
}

whenReady().then(render);
