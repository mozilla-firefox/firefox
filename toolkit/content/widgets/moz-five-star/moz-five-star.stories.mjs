/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html, ifDefined } from "../vendor/lit.all.mjs";
import "./moz-five-star.mjs";

export default {
  title: "UI Widgets/Five Star",
  component: "moz-five-star",
  parameters: {
    status: "stable",
    fluent: `
moz-five-star-title =
  .title = This is the title
moz-five-star-aria-label =
  .aria-label = This is the aria-label
    `,
    actions: {
      handles: ["select"],
    },
  },
};

const Template = ({
  rating,
  ariaLabel,
  l10nId,
  selectable,
  selectedHandler,
}) => html`
  <div style="max-width: 400px">
    <moz-five-star
      rating=${rating}
      aria-label=${ifDefined(ariaLabel)}
      data-l10n-id=${ifDefined(l10nId)}
      data-l10n-attrs="aria-label, title"
      ?selectable=${selectable}
      @select=${selectedHandler}
    >
    </moz-five-star>
  </div>
`;

export const FiveStar = Template.bind({});
FiveStar.args = {
  rating: 5.0,
  l10nId: "moz-five-star-aria-label",
};

export const WithTitle = Template.bind({});
WithTitle.args = {
  ...FiveStar.args,
  rating: 0,
  l10nId: "moz-five-star-title",
};

export const Selectable = Template.bind({});
Selectable.args = {
  selectable: true,
  selectedHandler: e => alert(`Selected rating: ${e.detail.rating}`),
};

export const Default = Template.bind({});
Default.args = {
  rating: 3.33,
};
