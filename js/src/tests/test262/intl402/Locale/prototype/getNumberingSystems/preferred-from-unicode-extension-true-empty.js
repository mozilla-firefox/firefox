// |reftest| shell-option(--enable-intl-locale-info) skip-if(!this.hasOwnProperty('Intl')||!this.Intl.Locale.prototype.hasOwnProperty('firstDayOfWeek')||!xulRuntime.shell) -- Intl.Locale-info is not enabled unconditionally, requires shell-options
// Copyright 2026 André Bargull. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-intl.locale.prototype.getNumberingSystems
description: >
  Returns the preferred numbering systems from the "nu" Unicode extension.
info: |
  NumberingSystemsOfLocale ( loc )
  1. If loc.[[NumberingSystem]] is not undefined, then
    a. Return CreateArrayFromList(« loc.[[NumberingSystem]] »).
  ...
features: [Intl.Locale, Intl.Locale-info]
---*/

const locales = [
  "und-u-nu",
  "und-u-nu-true",
  "en-u-nu",
  "en-u-nu-true",
];

for (let locale of locales) {
  let loc = new Intl.Locale(locale);
  let numberingSystems = loc.getNumberingSystems();

  assert.sameValue(
    loc.numberingSystem,
    "",
    `numberingSystem with locale "${locale}"`
  );

  assert.compareArray(
    numberingSystems,
    [""],
    `numberingSystems with locale "${locale}"`
  );
}

reportCompare(0, 0);
