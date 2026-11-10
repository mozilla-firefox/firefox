/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * A letter or decimal digit that a single key press can type. Modifier
 * letters, superscript and other non-decimal digits, characters of scripts
 * typed through an IME, and the fullwidth forms of Latin letters and digits
 * don't qualify. The `u` flag matches a character outside the BMP as a whole
 * code point rather than half of a surrogate pair. IsTypeableCharacter in
 * XULMenuParentElement.cpp matches the same characters.
 */
const TYPEABLE_CHARACTER =
  /(?![\p{sc=Han}\p{sc=Hiragana}\p{sc=Katakana}\p{sc=Hangul}\p{sc=Bopomofo}\uFF00-\uFFEF])[\p{Lu}\p{Ll}\p{Lt}\p{Lo}\p{Nd}]/u;

/**
 * Gives each item of a popup that has the `auto-accesskey` attribute the first
 * typeable letter or digit of its label as its accesskey. This is for items
 * with labels that don't come from our strings, such as an extension's menu
 * items. A menu reaches an item without an accesskey by the first typeable
 * letter or digit of its label (see XULMenuParentElement::FindMenuWithShortcut),
 * which fails when another item claims that letter as its accesskey, or when
 * the label has no such character, as when it consists of characters typed
 * through an IME, such as Chinese, Japanese or Korean. Such an item may share
 * its accesskey with other items, and the key then cycles through them.
 *
 * An item with `auto-accesskey="digit"`, or with a non-empty label that has no
 * typeable letter or digit, gets the next digit, in keyboard order from 1 to 9
 * and then 0, that the popup's other items don't use as an accesskey. This
 * suits items such as spelling suggestions, which mostly share their first
 * letter. Once the digits run out, numbering starts over, and a key shared by
 * several items cycles through them.
 *
 * When the popup's other items have no accesskeys, the items get none either,
 * so that they don't take the first letter of another item's label.
 *
 * A menupopup does this on popupshowing and a panel-list when it opens, after
 * the popup's own listeners have added and labeled the items, and again once
 * pending translations have arrived. Code that changes the items of an open
 * popup needs to call this again explicitly.
 *
 * @param {Element} popup
 *   A menupopup or panel-list.
 */
export function assignAutoAccessKeys(popup) {
  assign(popup);
  let doc = popup.ownerDocument;
  if (!doc.hasPendingL10nMutations) {
    return;
  }
  if (typeof Cu == "undefined") {
    // L10nMutationsFinished is dispatched only to chrome.
    let poll = () => {
      if (doc.hasPendingL10nMutations) {
        doc.defaultView.requestAnimationFrame(poll);
      } else {
        assign(popup);
      }
    };
    doc.defaultView.requestAnimationFrame(poll);
  } else {
    doc.addEventListener("L10nMutationsFinished", () => assign(popup), {
      once: true,
    });
  }
}

function assign(popup) {
  let isPanelList = popup.localName == "panel-list";
  let items = popup.getMenuItems({ includeDisabled: true });
  let otherItems = items.filter(
    item =>
      !item.matches("[auto-accesskey], :is(menuitem, menu)[role][disabled]")
  );
  let othersHaveNoKeys =
    otherItems.length &&
    !otherItems.some(item =>
      isPanelList ? item.label?.accessKey : item.getAttribute("accesskey")
    );
  let usedKeys = new Set();
  let itemsToNumber = [];
  for (let item of items) {
    if (!item.hasAttribute("auto-accesskey")) {
      usedKeys.add(
        isPanelList ? item.label?.accessKey : item.getAttribute("accesskey")
      );
      continue;
    }
    let label = isPanelList ? item.textContent : item.getAttribute("label");
    let key =
      othersHaveNoKeys || item.getAttribute("auto-accesskey") == "digit"
        ? ""
        : (label?.match(TYPEABLE_CHARACTER)?.[0] ?? "");
    if (key) {
      item.setAttribute("accesskey", key);
      usedKeys.add(key);
    } else {
      item.removeAttribute("accesskey");
      if (!othersHaveNoKeys && label?.trim()) {
        itemsToNumber.push(item);
      }
    }
  }
  let digits = [..."1234567890"].filter(digit => !usedKeys.has(digit));
  if (digits.length) {
    for (let [i, item] of itemsToNumber.entries()) {
      item.setAttribute("accesskey", digits[i % digits.length]);
    }
  }
}

/**
 * Finds the item of an open panel-list that pressing a key selects: an item
 * with the key as its accesskey, or else an item without an accesskey with a
 * label that starts with the key or has the key as its first typeable letter
 * or digit, so that "w" reaches "🦊 Work".
 *
 * @param {Element[]} items
 *   The items that the key can select, in order.
 * @param {string} key
 *   The typed character.
 * @param {object} options
 * @param {(item: Element) => string} options.getLabel
 *   Returns an item's label.
 * @param {Element} [options.current]
 *   The highlighted or focused item. When several items match, the first match
 *   after it is selected, wrapping around.
 * @param {boolean} [options.matchLabels]
 *   Whether an item can match by its label.
 * @returns {{item: Element, activate: boolean} | null}
 *   The item to select, and whether to activate it because it is the only
 *   match.
 */
export function findItemForKey(
  items,
  key,
  { getLabel, current, matchLabels = true }
) {
  let letter = key.toLowerCase();
  let isMatch = item => item.getAttribute("accesskey")?.toLowerCase() == letter;
  let matches = items.filter(isMatch);
  if (!matches.length && matchLabels) {
    isMatch = item => {
      if (item.getAttribute("accesskey")) {
        return false;
      }
      let label = getLabel(item).trim().toLowerCase();
      return (
        label.startsWith(letter) ||
        label.match(TYPEABLE_CHARACTER)?.[0] == letter
      );
    };
    matches = items.filter(isMatch);
  }
  if (!matches.length) {
    return null;
  }
  if (matches.length == 1) {
    return { item: matches[0], activate: true };
  }
  let after =
    items.findIndex(item => item == current || item.contains(current)) + 1;
  return {
    item: items.slice(after).find(isMatch) ?? matches[0],
    activate: false,
  };
}
