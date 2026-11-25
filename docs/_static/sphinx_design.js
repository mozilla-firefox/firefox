/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

(function () {
  "use strict";

  var dropdownClassName = "sd-dropdown";

  // Mirrors the id normalization docutils applies to the anchor-id-* classes.
  function normalizeAnchor(anchor) {
    return anchor
      .toLowerCase()
      .replace(/[^a-z0-9]+/g, "-")
      .replace(/^[-0-9]+|-+$/g, "");
  }

  function getDecodedHash() {
    var hash = window.location.hash.substring(1);
    try {
      return decodeURIComponent(hash);
    } catch (e) {
      return hash;
    }
  }

  function getDropdownElement() {
    var anchor = getDecodedHash();
    if (!anchor) {
      return false;
    }

    var candidates = [anchor, normalizeAnchor(anchor)];
    for (var i = 0; i < candidates.length; i++) {
      var element = document.getElementById(candidates[i]);
      if (element && element.classList.contains(dropdownClassName)) {
        return element;
      }
    }
    return false;
  }

  function setupDropdownLink() {
    var dropdowns = document.getElementsByClassName(dropdownClassName);
    for (var i = 0; i < dropdowns.length; i++) {
      for (var j = 0; j < dropdowns[i].classList.length; j++) {
        if (dropdowns[i].classList[j].startsWith("anchor-id-")) {
          dropdowns[i].id = dropdowns[i].classList[j].replace("anchor-id-", "");
        }
      }

      var aTag = document.createElement("a");
      aTag.setAttribute("href", "#" + dropdowns[i].id);
      aTag.classList.add("dropdown-link");
      aTag.innerHTML = "¶";

      var summaryElement =
        dropdowns[i].getElementsByClassName("sd-summary-title")[0];
      summaryElement.insertBefore(
        aTag,
        summaryElement.getElementsByClassName("docutils")[0]
      );
    }
  }

  function scrollToDropdown() {
    var dropdownElement = getDropdownElement();
    if (dropdownElement) {
      dropdownElement.open = true;
      dropdownElement.scrollIntoView(true);
    }
  }

  // Initialize dropdown link
  window.addEventListener("DOMContentLoaded", () => {
    if (document.getElementsByClassName(dropdownClassName).length) {
      setupDropdownLink();
      window.onhashchange = scrollToDropdown;
    }
  });

  // Scroll to and open the dropdown direct links
  window.onload = () => {
    if (document.getElementsByClassName(dropdownClassName).length) {
      scrollToDropdown();
    }
  };
})();
