/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html, ifDefined } from "../vendor/lit.all.mjs";
import "./moz-breadcrumb-group.mjs";

export default {
  title: "UI Widgets/Breadcrumb Group",
  component: "moz-breadcrumbs-group",
  parameters: {
    status: "stable",
    fluent: `
moz-breadcrumb-first =
  .label = First page
moz-breadcrumb-second =
  .label = Previous page
moz-breadcrumb-third =
  .label = Current page
    `,
  },
};

const Template = ({ l10nId, width, slot }) => {
  return html`
    <style>
      ${width
        ? `moz-breadcrumb-group {
            width: ${width}px;
            overflow: hidden;
          }`
        : ""}
    </style>
    <moz-breadcrumb-group slot=${ifDefined(slot)}>
      <moz-breadcrumb
        href="about#firstpage"
        data-l10n-id=${ifDefined(l10nId + "-first")}
      ></moz-breadcrumb>
      <moz-breadcrumb
        data-l10n-id=${ifDefined(l10nId + "-second")}
        href="about#prevpage"
      ></moz-breadcrumb>
      <moz-breadcrumb
        href="about#currentpage"
        data-l10n-id=${ifDefined(l10nId + "-third")}
      ></moz-breadcrumb>
    </moz-breadcrumb-group>
  `;
};

export const Default = Template.bind({});
Default.args = {
  l10nId: "moz-breadcrumb",
};
export const NarrowWidth = Template.bind({});
NarrowWidth.args = {
  ...Default.args,
  width: 180,
};
