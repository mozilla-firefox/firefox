/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// eslint-disable-next-line import-x/no-unresolved
import { html, ifDefined } from "lit.all.mjs";
import "./moz-widget.mjs";

export default {
  title: "Domain-specific UI Widgets/Widget",
  component: "moz-widget",
  parameters: {
    fluent: `
moz-widget-title =
  .title = Localized widget
moz-widget-back-button =
  .title = Back
  .aria-label = Back
moz-widget-units-button =
  .label = Units
moz-widget-menu-refresh = Refresh
moz-widget-menu-remove = Remove widget
    `,
  },
  argTypes: {
    size: {
      options: ["small", "medium", "large"],
      control: { type: "select" },
    },
    l10nId: {
      options: [undefined, "moz-widget-title"],
      control: { type: "select" },
    },
    badge: {
      options: [undefined, "new"],
      control: { type: "select" },
    },
  },
};

const WIDGET_DOCUMENT = `data:text/html,${encodeURIComponent(`
  <!doctype html>
  <body style="margin: 0;">
    <section style="padding: 1rem;">
      <h3>Framed widget</h3>
      <p>A document of its own.</p>
    </section>
  </body>
`)}`;

const headerTemplate = ({
  headerTitle,
  badge,
  backButton,
  trailingButton,
  menuLabel,
}) =>
  headerTitle || badge || backButton || trailingButton
    ? html`<moz-widget-header
        slot="header"
        title=${ifDefined(headerTitle)}
        menu-label=${ifDefined(menuLabel)}
      >
        ${backButton
          ? html`<moz-button
              slot="leading"
              type="ghost"
              size="small"
              iconsrc="chrome://global/skin/icons/arrow-left.svg"
              data-l10n-id="moz-widget-back-button"
            ></moz-button>`
          : ""}
        ${badge
          ? html`<moz-badge slot="leading" type=${badge}></moz-badge>`
          : ""}
        ${trailingButton
          ? html`<moz-button
              slot="trailing"
              size="small"
              iconsrc="chrome://global/skin/icons/arrow-down-12.svg"
              iconposition="end"
              data-l10n-id="moz-widget-units-button"
            ></moz-button>`
          : ""}
      </moz-widget-header>`
    : "";

const menuTemplate = menu =>
  menu
    ? html`<panel-list slot="menu" id="moz-widget-menu">
        <panel-item data-l10n-id="moz-widget-menu-refresh"></panel-item>
        <hr />
        <panel-item data-l10n-id="moz-widget-menu-remove"></panel-item>
      </panel-list>`
    : "";

const Template = ({
  size,
  widgetId,
  title,
  l10nId,
  padded,
  menu,
  menuLabel,
  ...header
}) => html`
  <moz-widget
    size=${size}
    widget-id=${widgetId}
    title=${title}
    data-l10n-id=${ifDefined(l10nId)}
    ?padded=${padded}
    menu-label=${ifDefined(menuLabel)}
  >
    ${headerTemplate({ ...header, menuLabel })} ${menuTemplate(menu)}
    <section style="padding: 1rem;">
      <p>Any HTML can go in the default slot.</p>
    </section>
  </moz-widget>
`;

// Stand-in for the error content a host slots in; New Tab supplies its own.
// A widget with no header passes its name, shown where the header would be.
const errorTemplate = title => html`
  <div
    slot="error"
    role="alert"
    style="block-size: 100%; box-sizing: border-box; padding: ${title
      ? "var(--space-medium)"
      : "0"}; display: flex; flex-direction: column; gap: var(--space-small);"
  >
    ${title
      ? html`<p style="margin: 0; font-size: var(--font-size-small);">
          ${title}
        </p>`
      : ""}
    <div
      style="flex: 1; display: flex; flex-direction: column; align-items: center; justify-content: center; gap: var(--space-small); text-align: center; color: var(--text-color-deemphasized);"
    >
      <svg
        viewBox="0 0 16 16"
        aria-hidden="true"
        style="inline-size: var(--icon-size); block-size: var(--icon-size); flex-shrink: 0; fill: var(--icon-color-warning);"
      >
        <path
          fill-rule="evenodd"
          clip-rule="evenodd"
          d="M7.975 1c1.024 0 2.05.514 2.622 1.542l4.386 7.888c1.074 1.932-.24 4.34-2.446 4.454a88 88 0 0 1-9.035 0c-2.218-.114-3.556-2.497-2.487-4.443l4.33-7.885A2.97 2.97 0 0 1 7.975 1M8 10.25A.875.875 0 1 0 8 12a.875.875 0 0 0 0-1.75M8 5a.75.75 0 0 0-.75.75v2.5a.75.75 0 0 0 1.5 0v-2.5A.75.75 0 0 0 8 5"
        />
      </svg>
      <p style="margin: 0; font-size: var(--font-size-small);">
        Widget unavailable. Check back later.
      </p>
    </div>
  </div>
`;

const FramedTemplate = ({
  size,
  widgetId,
  title,
  l10nId,
  padded,
  src,
  error,
  menu,
  menuLabel,
  ...header
}) => html`
  <moz-widget
    size=${size}
    widget-id=${widgetId}
    title=${title}
    data-l10n-id=${ifDefined(l10nId)}
    ?padded=${padded}
    menu-label=${ifDefined(menuLabel)}
    src=${src}
    ?error=${error}
  >
    ${headerTemplate({ ...header, menuLabel })} ${menuTemplate(menu)}
    ${error ? errorTemplate(header.headerTitle ? undefined : title) : ""}
  </moz-widget>
`;

export const Default = Template.bind({});
Default.args = {
  size: "medium",
  widgetId: "example",
  title: "Example widget",
  headerTitle: "Example widget",
  badge: undefined,
  backButton: false,
  trailingButton: false,
  menu: false,
  menuLabel: "Widget options",
  padded: false,
};

export const Framed = FramedTemplate.bind({});
Framed.args = {
  ...Default.args,
  title: "Framed widget",
  headerTitle: "Framed widget",
  src: WIDGET_DOCUMENT,
  error: false,
};

export const Padded = FramedTemplate.bind({});
Padded.args = {
  ...Framed.args,
  padded: true,
};

export const HeaderActions = Template.bind({});
HeaderActions.args = {
  ...Default.args,
  badge: "new",
  backButton: true,
  trailingButton: true,
  menu: true,
};

export const WithoutHeader = FramedTemplate.bind({});
WithoutHeader.args = {
  ...Framed.args,
  headerTitle: undefined,
  menu: true,
};
