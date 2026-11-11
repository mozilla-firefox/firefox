/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Named so the browser-chrome test harness can pick it out of the console.
 */
export class AccessKeyConflictError extends Error {
  name = "AccessKeyConflictError";
}

/**
 * Whether popups check their accesskeys when they open, which they do in
 * automation so that a test fails on a conflict. A page without chrome
 * privileges, such as about:newtab, sees automation through
 * navigator.webdriver.
 */
const ENABLED =
  typeof Cu == "undefined" ? navigator.webdriver : Cu.isInAutomation;

/**
 * Reports a menu whose items can't all be reached by typing a letter: two
 * items sharing an accesskey, or items with an accesskey beside items without
 * one. An item without an accesskey is reached by the first letter of its
 * label, unless another item claims that letter as its accesskey. Which
 * letters collide depends on the locale, so the check doesn't compare them;
 * a menu has to give every item an accesskey or none.
 *
 * A popup with known conflicts names the bug that tracks fixing them in an
 * `accesskey-conflicts-bug` attribute and is skipped until the attribute goes.
 * An item that shares its accesskey on purpose, so that the key cycles
 * through the items, must list the space-separated ids of the items it shares
 * the key with in an `intended-duplicate-accesskey` attribute, and is left out
 * of the shared-key check. Each id has to name an item of the same menu, and
 * one with an accesskey has to have the same one. An item with the
 * `auto-accesskey` attribute (see auto-accesskey.mjs) is left out too, as is a
 * WebExtension's item, which can't know the items its accesskey collides with.
 * A disabled menuitem with a role attribute (a note, a heading) only displays
 * information and is not one. A menuitem without a label, such as an icon
 * button in a menugroup, can't show an accesskey or be reached by its first
 * letter, so it may lack an accesskey; one it has is still compared with the
 * others.
 *
 * A menu that macOS shows natively ignores accesskeys, so it is skipped along
 * with its submenus.
 *
 * While the document has translations pending, the check waits for them, so
 * that it compares the labels and accesskeys the user will see.
 *
 * @param {Element} popup
 *   An open menupopup or panel-list, named in the message.
 * @returns {Promise<void>}
 *   Resolves once the check has run.
 */
export async function checkAccessKeys(popup) {
  if (
    !ENABLED ||
    popup.hasAttribute("accesskey-conflicts-bug") ||
    (popup.localName == "menupopup" && isShownNatively(popup))
  ) {
    return;
  }
  let doc = popup.ownerDocument;
  if (typeof Cu == "undefined") {
    // L10nMutationsFinished is dispatched only to chrome.
    while (doc.hasPendingL10nMutations) {
      await new Promise(resolve =>
        doc.defaultView.requestAnimationFrame(resolve)
      );
    }
  } else if (doc.hasPendingL10nMutations) {
    await new Promise(resolve =>
      doc.addEventListener("L10nMutationsFinished", resolve, { once: true })
    );
  }

  let allItems =
    popup.localName == "panel-list"
      ? panelListItems(popup)
      : menupopupItems(popup);
  let items = allItems.filter(item => item.visible && item.label);
  let withKey = items.filter(item => item.accesskey);
  if (!withKey.length) {
    return;
  }

  let problems = [];
  let remedies = [];
  let byKey = new Map();
  for (let item of withKey) {
    if (!item.intendedDuplicate) {
      byKey.getOrInsert(item.accesskey.toLowerCase(), []).push(item.label);
    }
  }
  for (let [key, labels] of byKey) {
    if (labels.length > 1) {
      problems.push(`accesskey "${key}" is shared by ${quoteLabels(labels)}`);
    }
  }
  if (problems.length) {
    remedies.push(
      "Fix a shared accesskey, in order of preference: 1) give one of the " +
        "items a different accesskey, 2) move items into a submenu, 3) drop " +
        "the accesskeys of every item in this menu, 4) set " +
        "intended-duplicate-accesskey on one of the items to the ids of the " +
        "items it shares the accesskey with."
    );
  }

  let byId = new Map(
    allItems.filter(item => item.id).map(item => [item.id, item])
  );
  let staleProblems = [];
  for (let item of withKey) {
    if (item.marked && !item.webExtension && !item.sharesWith.length) {
      staleProblems.push(
        `"${item.label}" has an empty intended-duplicate-accesskey`
      );
    }
    for (let id of item.sharesWith) {
      let other = byId.get(id);
      if (!other) {
        staleProblems.push(
          `"${item.label}" names "${id}" in intended-duplicate-accesskey, ` +
            "which is not an item of this menu"
        );
      } else if (
        other.accesskey &&
        other.accesskey.toLowerCase() != item.accesskey.toLowerCase()
      ) {
        staleProblems.push(
          `"${item.label}" names "${id}" in intended-duplicate-accesskey, ` +
            `which has accesskey "${other.accesskey}"`
        );
      }
    }
  }
  if (staleProblems.length) {
    problems.push(...staleProblems);
    remedies.push(
      "Fix intended-duplicate-accesskey by listing the ids of the items in " +
        "this menu that share the accesskey."
    );
  }

  let withoutKey = items.filter(item => !item.accesskey && item.hasText);
  if (withoutKey.length) {
    problems.push(
      `${quoteLabels(withoutKey.map(item => item.label))} ${
        withoutKey.length == 1 ? "has" : "have"
      } no accesskey while ${quoteLabels(withKey.map(item => item.label))} ${
        withKey.length == 1 ? "does" : "do"
      }`
    );
    remedies.push(
      "Fix a missing accesskey, in order of preference: 1) give every item " +
        "an accesskey, 2) move the items that can't have one into a " +
        "submenu, 3) drop the accesskeys of every item in this menu."
    );
  }

  if (!problems.length) {
    return;
  }

  let name = popup.id ? `${popup.localName}#${popup.id}` : popup.localName;
  let error = new AccessKeyConflictError(
    `${name}: ${problems.join("; ")}. ${remedies.join(" ")}`
  );
  if (typeof Cu == "undefined") {
    // Thrown from a microtask so the popup finishes opening first.
    queueMicrotask(() => {
      throw error;
    });
    return;
  }
  let scriptError = Cc["@mozilla.org/scripterror;1"].createInstance(
    Ci.nsIScriptError
  );
  scriptError.init(
    error.toString(),
    popup.ownerDocument.documentURI,
    0,
    0,
    Ci.nsIScriptError.errorFlag,
    "chrome javascript"
  );
  Services.console.logMessage(scriptError);
}

/**
 * Whether macOS shows a menupopup natively. Only the outermost menupopup of a
 * native menu reports isNativeMenu; its submenus don't.
 *
 * @param {Element} menupopup
 * @returns {boolean}
 */
function isShownNatively(menupopup) {
  let outermost = menupopup;
  let parentPopup;
  while ((parentPopup = outermost.parentElement?.closest("menupopup"))) {
    outermost = parentPopup;
  }
  return outermost.isNativeMenu;
}

function menupopupItems(popup) {
  return Array.from(
    popup.querySelectorAll(
      ":is(:scope, :scope > menugroup) > :is(menuitem, menu)"
    ),
    item => ({
      id: item.id,
      visible: !item.matches("[hidden], [role][disabled]"),
      marked: item.hasAttribute("intended-duplicate-accesskey"),
      webExtension: item.classList.contains("webextension-menuitem"),
      sharesWith: sharesWith(item),
      accesskey: item.getAttribute("accesskey"),
      intendedDuplicate: item.matches(
        "[intended-duplicate-accesskey], [auto-accesskey], .webextension-menuitem"
      ),
      hasText: item.matches("[label], [value]"),
      label:
        item.getAttribute("label") ||
        item.getAttribute("aria-label") ||
        item.getAttribute("value"),
    })
  );
}

function panelListItems(popup) {
  return Array.from(popup.querySelectorAll("panel-item"), item => ({
    id: item.id,
    visible: !item.hidden,
    marked: item.hasAttribute("intended-duplicate-accesskey"),
    webExtension: false,
    sharesWith: sharesWith(item),
    accesskey: item.label?.accessKey,
    intendedDuplicate: item.matches(
      "[intended-duplicate-accesskey], [auto-accesskey]"
    ),
    hasText: true,
    label: (item.label?.textContent ?? item.textContent).trim(),
  }));
}

/**
 * The ids an item lists in its intended-duplicate-accesskey attribute, naming
 * the items it shares its accesskey with.
 *
 * @param {Element} item
 * @returns {string[]}
 */
function sharesWith(item) {
  return (item.getAttribute("intended-duplicate-accesskey") ?? "")
    .split(/\s+/)
    .filter(Boolean);
}

function quoteLabels(labels) {
  return labels.map(label => `"${label}"`).join(", ");
}
