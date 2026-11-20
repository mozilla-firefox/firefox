/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const XHTML_NS = "http://www.w3.org/1999/xhtml";
const STYLE_INSPECTOR_PROPERTIES =
  "devtools/shared/locales/styleinspector.properties";

loader.lazyGetter(this, "L10N_EMPTY", function () {
  const { LocalizationHelper } = require("resource://devtools/shared/l10n.js");
  const l10n = new LocalizationHelper(STYLE_INSPECTOR_PROPERTIES);
  return l10n.getStr("rule.variableEmpty");
});

class CssExplainersTooltipHelper {
  /**
   * Fill the tooltip with container information.
   */
  async setContent({ expression, property, pseudoElement, rule }, tooltip) {
    const res = await rule.domRule.getCssExplainersData(
      property,
      expression,
      pseudoElement,
      rule.inherited
    );

    const { doc } = tooltip;

    const tooltipContainer = doc.createElementNS(XHTML_NS, "div");
    tooltipContainer.classList.add("devtools-tooltip-css-explainers");

    if (!res.length) {
      tooltipContainer.append(`Couldn't compute data for "${expression}"`);
    } else {
      const ol = doc.createElementNS(XHTML_NS, "ol");

      for (const step of res) {
        const li = doc.createElementNS(XHTML_NS, "li");
        if (step === "") {
          li.append(`<${L10N_EMPTY}>`);
          li.classList.add("empty");
        } else {
          li.append(step);
        }

        ol.append(li);
      }
      tooltipContainer.append(ol);
    }

    await tooltip.replaceChildrenLocalized(tooltipContainer);
  }
}

module.exports = CssExplainersTooltipHelper;
