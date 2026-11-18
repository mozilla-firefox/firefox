/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

addAccessibleTask(
  `
<div id="ariaButton" role="button">ARIA button</div>
<div id="ariaLog" role="log">ARIA log</div>
<div id="ariaMain" role="main">ARIA main</div>
<div id="ariaRegion" role="region" aria-label="ARIA region">ARIA region</div>
<nav id="ariaUnnamedRegion" role="region">ARIA unnamed region</nav>
<div id="ariaDirectory" role="directory">ARIA directory</div>
<div id="ariaAlertdialog" role="alertdialog">ARIA alertdialog</div>
<div id="ariaFeed" role="feed">ARIA feed</div>
<div role="table">
  <div id="ariaRowgroup" role="rowgroup">ARIA rowgroup</div>
</div>
<div id="ariaSearchbox" role="searchbox">ARIA searchbox</div>
<div id="ariaUnknown" role="unknown">unknown ARIA role</div>
<div id="ariaFallbackUpperCase" role="REGION GROUP">unlabelled region with fallback to group, upper case</div>
<button id="htmlButton">HTML button</button>
<button id="toggleButton" aria-pressed="true">toggle button</button>
<main id="htmlMain">HTML main</main>
<header id="htmlHeader">HTML header</header>
<section id="htmlSection">
  <header id="htmlSectionHeader">HTML header inside section</header>
</section>
<section id="htmlRegion" aria-label="HTML region">HTML region</section>
<fieldset id="htmlFieldset">HTML fieldset</fieldset>
<table>
  <tbody id="htmlTbody" tabindex="-1"><tr><th>HTML tbody</th></tr></tbody>
</table>
<table role="grid">
  <tr>
    <td id="htmlGridcell">HTML implicit gridcell</td>
  </tr>
</table>
<div id="htmlDiv">HTML div</div>
<span id="htmlSpan" aria-label="HTML span">HTML span</span>
<iframe id="iframe"></iframe>
  `,
  async function (browser, docAcc) {
    function testComputedARIARole(id, role) {
      const acc = findAccessibleChildByID(docAcc, id);
      is(acc.computedARIARole, role, `computedARIARole for ${id} is correct`);
    }

    testComputedARIARole("ariaButton", "button");
    testComputedARIARole("ariaLog", "log");
    // Landmarks map to a single Gecko role.
    testComputedARIARole("ariaMain", "main");
    testComputedARIARole("ariaRegion", "region");
    // Unnamed ARIA regions should ignore the ARIA role.
    testComputedARIARole("ariaUnnamedRegion", "navigation");
    // The directory ARIA role is an alias of list.
    testComputedARIARole("ariaDirectory", "list");
    // alertdialog, feed and rowgroupmap to a Gecko role, but it isn't unique.
    testComputedARIARole("ariaAlertdialog", "alertdialog");
    testComputedARIARole("ariaFeed", "feed");
    testComputedARIARole("ariaRowgroup", "rowgroup");
    testComputedARIARole("ariaSearchbox", "searchbox");
    testComputedARIARole("ariaUnknown", "generic");
    // XXX Ideally, ariaFallbackUpperCase would be tested in
    // testing/web-platform/tests/wai-aria/role/fallback-roles.html
    // However, it's unclear in the spec whether ARIA roles are intended to be
    // case insensitive or not, so we can't reasonably assume that in WPT until
    // that's cleared up. See https://github.com/w3c/aria/issues/2548.
    testComputedARIARole("ariaFallbackUpperCase", "group");
    testComputedARIARole("htmlButton", "button");
    // There is only a single ARIA role for buttons, but Gecko uses different
    // roles depending on states.
    testComputedARIARole("toggleButton", "button");
    testComputedARIARole("htmlMain", "main");
    testComputedARIARole("htmlHeader", "banner");
    // <section> only maps to the region ARIA role if it has a label.
    testComputedARIARole("htmlSection", "generic");
    // <header> only maps to the banner role if it is not a child of a
    // sectioning element.
    testComputedARIARole("htmlSectionHeader", "generic");
    testComputedARIARole("htmlRegion", "region");
    // Gecko doesn't have a rowgroup role. Ensure we differentiate for
    // computedARIARole.
    testComputedARIARole("htmlFieldset", "group");
    testComputedARIARole("htmlTbody", "rowgroup");
    // <td> inside <table role="grid"> implicitly maps to ARIA gridcell.
    testComputedARIARole("htmlGridcell", "gridcell");
    // Test generics.
    testComputedARIARole("htmlDiv", "generic");
    testComputedARIARole("htmlSpan", "generic");
    // Some roles can't be mapped to ARIA role tokens.
    testComputedARIARole("iframe", "");
  },
  { chrome: true, topLevel: true }
);
