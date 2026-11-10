/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

const table = document.getElementById("table");

function notifyUpdate() {
  window.dispatchEvent(new CustomEvent("CustomKeysUpdate"));
}

async function buildTable() {
  const keys = await RPMSendQuery("CustomKeys:GetKeys");
  for (const category in keys) {
    // Category accordions for groups of shortcuts:
    const categoryCard = document.createElement("moz-card");
    categoryCard.type = "accordion";
    categoryCard.className = "category";
    if (category.startsWith("customkeys-")) {
      // Manually added shortcuts (shortcuts that aren't available in menus):
      categoryCard.setAttribute("data-l10n-id", category);
    } else {
      categoryCard.heading = category;
    }
    categoryCard.headingLevel = 2;
    table.append(categoryCard);

    // Individual shortcut rows:
    const boxGroup = document.createElement("moz-box-group");
    categoryCard.append(boxGroup);
    const categoryKeys = keys[category];

    for (const keyId in categoryKeys) {
      // Row container:
      const row = document.createElement("moz-box-item");
      row.className = "key";
      const key = categoryKeys[keyId];
      row.dataset.id = keyId;

      let keyLabelText = key.title;
      if (key.title.startsWith("customkeys-")) {
        // Manually added shortcuts (shortcuts that aren't available in menus
        // for which Fluent IDs are supplied by CustomKeysParent):
        keyLabelText = await document.l10n.formatValue(key.title);
      }
      row.role = "group";
      row.dataset.label = keyLabelText;

      // All row content goes into the default slot so we own the flex layout
      // and can reflow each row into two lines on smaller screens:
      const keyContent = document.createElement("div");
      keyContent.className = "key-content";

      // Label reflows to the first line on smaller screens:
      const keyLabelContainer = document.createElement("div");
      const keyLabel = document.createElement("span");
      const keyDescription = document.createElement("span");
      keyLabelContainer.className = "key-label-container";
      keyLabel.className = "key-label";
      keyLabel.textContent = keyLabelText;
      if (key.systemAssigned) {
        row.classList.add("system-assigned");
        keyDescription.className = "text-deemphasized";
        keyDescription.setAttribute(
          "data-l10n-id",
          "customkeys-key-system-assigned"
        );
      } else if (key.internal) {
        row.classList.add("internal");
        keyDescription.className = "text-deemphasized";
        keyDescription.setAttribute(
          "data-l10n-id",
          "customkeys-key-unchangeable"
        );
      }
      keyLabelContainer.append(keyLabel);
      keyLabelContainer.append(keyDescription);
      keyContent.append(keyLabelContainer);
      row.ariaLabelledByElements = [keyLabel, keyDescription];

      // Actions reflow to the second line on smaller screens:
      const keyActions = document.createElement("div");
      keyActions.className = "key-actions";

      // Existing shortcut field:
      const inputCurrentKey = document.createElement("moz-input-text");
      inputCurrentKey.className = "currentShortcut";
      if (!key.shortcut) {
        inputCurrentKey.setAttribute(
          "data-l10n-id",
          "customkeys-shortcut-unassigned"
        );
      }
      inputCurrentKey.value = key.shortcut;
      inputCurrentKey.ariaLabel = await document.l10n.formatValue(
        "customkeys-shortcut-input",
        { keyLabel: keyLabelText }
      );
      inputCurrentKey.readonly = true;
      keyActions.append(inputCurrentKey);

      // Change/Edit button:
      const buttonChange = document.createElement("moz-button");
      buttonChange.className = "change";
      buttonChange.setAttribute("data-l10n-id", "customkeys-key-edit");
      buttonChange.type = "icon ghost";
      buttonChange.iconSrc = "chrome://global/skin/icons/edit-outline.svg";
      keyActions.append(buttonChange);

      // New key shortcut:
      const inputNewKey = document.createElement("moz-input-text");
      inputNewKey.className = "newKey";
      inputNewKey.setAttribute("data-l10n-id", "customkeys-key-new");
      inputNewKey.setAttribute("inputlayout", "inline-end");
      keyActions.append(inputNewKey);

      // Clear button:
      const buttonClear = document.createElement("moz-button");
      buttonClear.className = "clear";
      buttonClear.setAttribute("data-l10n-id", "customkeys-key-clear");
      buttonClear.type = "icon ghost";
      buttonClear.iconSrc = "chrome://global/skin/icons/close.svg";
      keyActions.append(buttonClear);

      // Reset/Restore button:
      const buttonReset = document.createElement("moz-button");
      buttonReset.className = "reset";
      buttonReset.setAttribute("data-l10n-id", "customkeys-key-reset");
      buttonReset.type = "icon ghost";
      buttonReset.iconSrc =
        "chrome://global/skin/icons/arrow-counterclockwise-16.svg";
      keyActions.append(buttonReset);

      keyContent.append(keyActions);

      row.append(keyContent);
      boxGroup.append(row);
      updateKey(row, key);
    }
  }
  // Make the first category card to be expanded by default:
  table.querySelector("moz-card").expanded = true;

  notifyUpdate();
}

function updateKey(row, data) {
  const input = row.querySelector(".currentShortcut");
  input.value = data.shortcut;
  if (!input.value) {
    input.setAttribute("data-l10n-id", "customkeys-shortcut-unassigned");
  } else {
    input.removeAttribute("data-l10n-id");
    input.removeAttribute("placeholder");
  }
  row.classList.toggle("customized", data.isCustomized);
  row.classList.toggle("assigned", !!data.shortcut);
}

// Returns false if the assignment should be cancelled.
async function maybeHandleConflict(data) {
  for (const row of table.querySelectorAll(".key")) {
    if (data.shortcut != row.querySelector(".currentShortcut").value) {
      continue; // Not a conflict.
    }
    const conflictId = row.dataset.id;
    if (conflictId == data.id) {
      // We're trying to assign this key to the shortcut it is already
      // assigned to. We don't need to do anything.
      return false;
    }
    const conflictDesc = row.dataset.label;
    if (
      row.classList.contains("internal") ||
      row.classList.contains("system-assigned")
    ) {
      const [title, body] = await document.l10n.formatValues([
        { id: "customkeys-conflict-unusable-title" },
        {
          id: "customkeys-conflict-unusable-body",
          args: { conflict: conflictDesc },
        },
      ]);
      await RPMSendQuery("CustomKeys:Confirm", {
        title,
        body,
      });
      return false;
    }
    const [title, body, buttonCancel, buttonConfirm] =
      await document.l10n.formatValues([
        { id: "customkeys-conflict-confirm-title" },
        {
          id: "customkeys-conflict-confirm-body",
          args: { conflict: conflictDesc },
        },
        { id: "customkeys-conflict-confirm-button-cancel" },
        { id: "customkeys-conflict-confirm-button-confirm" },
      ]);
    if (
      !(await RPMSendQuery("CustomKeys:Confirm", {
        title,
        body,
        buttonCancel,
        buttonConfirm,
      }))
    ) {
      return false; // user cancels
    }
    // Clear the conflicting key.
    const newData = await RPMSendQuery("CustomKeys:ClearKey", conflictId);
    updateKey(row, newData);
    return true;
  }
  return true;
}

async function onAction(event) {
  const row = event.target.closest("moz-box-item");
  if (!row) {
    return; // event is outside of a shortcut item
  }
  const keyId = row.dataset.id;
  if (event.target.className == "reset") {
    Glean.browserCustomkeys.actions.reset.add();
    const data = await RPMSendQuery("CustomKeys:GetDefaultKey", keyId);
    // If the shortcut is unassigned by default, there can't be a conflict.
    if (!data.shortcut || (await maybeHandleConflict(data))) {
      const newData = await RPMSendQuery("CustomKeys:ResetKey", keyId);
      updateKey(row, newData);
      if (newData.shortcut) {
        row.querySelector(".clear").focus();
      } else {
        // Clear button is hidden for unassigned keys, so we shall place
        // the focus on the previous control (Edit/Change)
        row.querySelector(".change").focus();
      }
      notifyUpdate();
    }
  } else if (event.target.className == "change") {
    Glean.browserCustomkeys.actions.change.add();
    // The "editing" class will cause the Change/Edit button to be replaced by
    // a labelled input for the new key.
    row.classList.add("editing");
    // We need to listen for keys in the parent process because we want to
    // intercept reserved keys, which we can't do in the content process.
    RPMSendAsyncMessage("CustomKeys:CaptureKey", true);
    row.querySelector(".newKey").focus();
  } else if (event.target.className == "clear") {
    Glean.browserCustomkeys.actions.clear.add();
    const newData = await RPMSendQuery("CustomKeys:ClearKey", keyId);
    updateKey(row, newData);
    row.querySelector(".reset").focus();
    notifyUpdate();
  }
}

async function onKey({ data }) {
  const input = document.activeElement;
  const row = input.closest("moz-box-item");
  data.id = row.dataset.id;
  if (data.isModifier) {
    // This is a modifier. Display it, but don't assign yet. We assign when the
    // main key is pressed (below).
    input.value = data.modifierString;
    await input.updateComplete;
    // Select the input's text so screen readers will report it.
    input.select();
    return;
  }
  if (!data.isValid) {
    input.value = await document.l10n.formatValue("customkeys-key-invalid");
    await input.updateComplete;
    input.select();
    return;
  }
  if (await maybeHandleConflict(data)) {
    const newData = await RPMSendQuery("CustomKeys:ChangeKey", data);
    updateKey(row, newData);
  }
  RPMSendAsyncMessage("CustomKeys:CaptureKey", false);
  row.classList.remove("editing");
  row.querySelector(".change").focus();
  notifyUpdate();
}

function onFocusLost(event) {
  if (event.target.className == "newKey") {
    // If the input loses focus, cancel editing of the key.
    RPMSendAsyncMessage("CustomKeys:CaptureKey", false);
    const row = event.target.closest("moz-box-item");
    row.classList.remove("editing");
    // Clear any modifiers that were displayed, ready for the next edit.
    event.target.value = "";
  }
}

function clearSearchHighlights(row) {
  const labelEl = row.querySelector(".key-label");
  if (labelEl.querySelector(".search-highlight")) {
    labelEl.textContent = row.dataset.label;
  }
}

function applySearchHighlights(query, row) {
  const labelEl = row.querySelector(".key-label");
  if (!labelEl) {
    return;
  }
  const text = row.dataset.label;
  const lower = text.toLowerCase();
  const frag = document.createDocumentFragment();
  let lastIndex = 0;
  let i = -1;
  while ((i = lower.indexOf(query, lastIndex)) >= 0) {
    if (i > lastIndex) {
      frag.append(text.slice(lastIndex, i));
    }
    const mark = document.createElement("mark");
    mark.className = "search-highlight";
    mark.textContent = text.slice(i, i + query.length);
    frag.append(mark);
    lastIndex = i + query.length;
  }
  if (lastIndex < text.length) {
    frag.append(text.slice(lastIndex));
  }
  labelEl.replaceChildren(frag);
}

function onSearchInput(event) {
  const query = event.target.value.toLowerCase();
  const cards = table.querySelectorAll(".category");

  for (const row of table.querySelectorAll(".key")) {
    const isMatching =
      !query || row.dataset.label.toLowerCase().includes(query);
    row.hidden = !isMatching;
    // ToDo: Remove when bug 1964412 is fixed:
    row.classList.toggle("hidden", !isMatching);
    if (query) {
      applySearchHighlights(query, row);
    } else {
      clearSearchHighlights(row);
    }
  }
  for (const [i, card] of cards.entries()) {
    // Show and expand a category card only if it has at least 1 shown key
    // and expand only the first category when search input is cleared.
    const hasMatches = card.querySelector(".key:not([hidden])");
    card.hidden = !hasMatches;
    card.expanded = query ? hasMatches : i === 0;
    // ToDo: Remove when bug 1964412 is fixed:
    if (hasMatches) {
      card.classList.remove("hidden");
    } else {
      card.classList.add("hidden");
    }
  }

  notifyUpdate();
}

async function onResetAll() {
  Glean.browserCustomkeys.actions.reset_all.add();
  const [title, body, buttonCancel, buttonConfirm] =
    await document.l10n.formatValues([
      { id: "customkeys-reset-all-confirm-title" },
      { id: "customkeys-reset-all-confirm-body" },
      { id: "customkeys-reset-all-confirm-button-cancel" },
      { id: "customkeys-reset-all-confirm-button-confirm" },
    ]);
  if (
    !(await RPMSendQuery("CustomKeys:Confirm", {
      title,
      body,
      buttonCancel,
      buttonConfirm,
    }))
  ) {
    return; // user cancels
  }
  await RPMSendQuery("CustomKeys:ResetAll");
  const keysByCat = await RPMSendQuery("CustomKeys:GetKeys");
  const keysById = {};
  for (const category in keysByCat) {
    const categoryKeys = keysByCat[category];
    for (const keyId in categoryKeys) {
      keysById[keyId] = categoryKeys[keyId];
    }
  }
  for (const row of table.querySelectorAll(".key")) {
    const data = keysById[row.dataset.id];
    if (data) {
      updateKey(row, data);
    }
  }
  notifyUpdate();
}

buildTable();
table.addEventListener("click", onAction);
RPMAddMessageListener("CustomKeys:CapturedKey", onKey);
table.addEventListener("focusout", onFocusLost);
customElements.whenDefined("customkeys-sidebar").then(async () => {
  await document.querySelector("customkeys-sidebar").updateComplete;
  document.getElementById("search").addEventListener("input", onSearchInput);
  document.getElementById("resetAll").addEventListener("click", onResetAll);
});
Glean.browserCustomkeys.opened.add();
