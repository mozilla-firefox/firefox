/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html, ifDefined } from "../vendor/lit.all.mjs";
import "./moz-page-header.mjs";
import { Default as Breadcrumbs } from "../moz-breadcrumb-group/moz-breadcrumb-group.stories.mjs";

export default {
  title: "UI Widgets/Page Header",
  component: "moz-page-header",
  argTypes: {
    l10nId: {
      options: [
        "moz-page-header-heading",
        "moz-page-header-description",
        "moz-page-header-long",
      ],
      control: { type: "select" },
    },
  },
  parameters: {
    status: "stable",
    fluent: `
moz-page-header-heading =
  .heading = This is the page heading
moz-page-header-description =
  .heading = This is the page heading
  .description = This is a description of the page
moz-page-header-long =
  .heading = Lorem ipsum dolor sit amet, consectetur adipiscing elit. Vestibulum libero enim, luctus eu ante a, maximus imperdiet mi. Suspendisse sodales, nisi et commodo malesuada, lectus.
  .description = Lorem ipsum dolor sit amet, consectetur adipiscing elit. Vestibulum libero enim, luctus eu ante a, maximus imperdiet mi. Suspendisse sodales, nisi et commodo malesuada, lectus.
moz-breadcrumb-first =
  .label = First page
moz-breadcrumb-second =
  .label = Previous page
moz-breadcrumb-third =
  .label = Current page
    `,
  },
};

const Template = ({
  l10nId,
  backButton,
  iconSrc,
  supportPage,
  showBreadcrumbs,
}) => html`
  <moz-page-header
    data-l10n-id=${l10nId}
    iconsrc=${ifDefined(iconSrc)}
    support-page=${ifDefined(supportPage)}
    ?backbutton=${backButton}
  >
    ${showBreadcrumbs
      ? Breadcrumbs({
          slot: "breadcrumbs",
          ...Breadcrumbs.args,
        })
      : ""}
  </moz-page-header>
`;

export const Default = Template.bind({});
Default.args = {
  l10nId: "moz-page-header-heading",
  iconSrc: "",
  backButton: false,
  showBreadcrumbs: false,
};

export const WithBackButton = Template.bind({});
WithBackButton.args = {
  ...Default.args,
  backButton: true,
};

export const WithIcon = Template.bind({});
WithIcon.args = {
  ...WithBackButton.args,
  iconSrc: "chrome://global/skin/icons/highlights.svg",
};

export const WithDescription = Template.bind({});
WithDescription.args = {
  ...WithBackButton.args,
  l10nId: "moz-page-header-description",
};

export const WithSupportPage = Template.bind({});
WithSupportPage.args = {
  ...WithDescription.args,
  supportPage: "test",
};

export const WithBreadcrumbs = Template.bind({});
WithBreadcrumbs.args = {
  ...WithBackButton.args,
  showBreadcrumbs: true,
};
