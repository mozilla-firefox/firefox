/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { html, ifDefined } from "../vendor/lit.all.mjs";
import { GROUP_TYPES } from "./moz-box-group.mjs";
// eslint-disable-next-line mozilla/no-browser-refs-in-toolkit
import "chrome://browser/content/preferences/widgets/setting-control.mjs";

export default {
  title: "UI Widgets/Box Group",
  component: "moz-box-group",
  argTypes: {
    type: {
      options: Object.keys(GROUP_TYPES),
      mapping: GROUP_TYPES,
      control: "select",
    },
  },
  parameters: {
    status: "stable",
    fluent: `
moz-box-item =
  .label = I'm a box item
  .description = I'm part of a group
moz-box-button-1 =
  .label = I'm a box button in a group
moz-box-button-2 =
  .label = Delete this box button from a group
moz-box-link =
  .label = I'm a box link in a group
moz-box-delete-action =
  .title = Delete I'm a box item
moz-box-edit-action =
  .title = Edit I'm a box item
moz-box-toggle-action =
  .aria-label = Toggle I'm a box item
moz-box-more-action =
  .title = More options, I'm a box item
moz-box-select-action =
  .aria-label = Choose an action for I'm a box item
moz-box-item-reorderable-1 =
  .label = I'm box item number 1
moz-box-item-reorderable-2 =
  .label = I'm box item number 2
moz-box-item-reorderable-3 =
  .label = I'm box item number 3
moz-box-item-reorderable-4 =
  .label = I'm box item number 4
moz-box-item-reorderable-5 =
  .label = I'm box item number 5
moz-box-item-header =
  .label = I'm a header box item
moz-box-button-footer =
  .label = I'm a footer box button
    `,
  },
};

function basicTemplate({ type, hasHeader, hasFooter, hasStatic }) {
  return html`<moz-box-group
      type=${ifDefined(type)}
      @reorder=${handleReorderEvent}
    >
      ${hasHeader
        ? html`<moz-box-item
            slot="header"
            data-l10n-id="moz-box-item-header"
          ></moz-box-item>`
        : ""}
      ${getInnerElements(type, hasStatic)}
      ${hasFooter
        ? html`<moz-box-button
            slot="footer"
            data-l10n-id="moz-box-button-footer"
          ></moz-box-button>`
        : ""}
    </moz-box-group>
    ${type == "list"
      ? html`<moz-button class="delete" @click=${appendItem}>
          Add an item
        </moz-button>`
      : ""}`;
}

function getInnerElements(type, hasStatic) {
  if (type == GROUP_TYPES.reorderable) {
    return reorderableElements(hasStatic);
  }

  return basicElements();
}

function selectAction() {
  return html`<moz-select slot="actions" data-l10n-id="moz-box-select-action">
    <moz-option
      value="1"
      label="Option one"
      iconsrc="chrome://global/skin/icons/info.svg"
    ></moz-option>
    <moz-option
      value="2"
      label="Option two"
      iconsrc="chrome://global/skin/icons/warning.svg"
    ></moz-option>
    <moz-option
      value="3"
      label="Option three"
      iconsrc="chrome://global/skin/icons/settings.svg"
    ></moz-option>
  </moz-select>`;
}

function reorderableElements(hasStatic) {
  const createItems = (length, slot, startIndex = 0) =>
    Array.from({ length }).map(
      (_, i) =>
        html`<moz-box-item
          data-l10n-id=${`moz-box-item-reorderable-${startIndex + i + 1}`}
          slot=${ifDefined(slot)}
        >
          <moz-button
            iconsrc="chrome://global/skin/icons/edit-outline.svg"
            data-l10n-id="moz-box-edit-action"
            slot="actions-start"
          ></moz-button>
          ${selectAction()}
          <moz-toggle
            slot="actions"
            pressed
            data-l10n-id="moz-box-toggle-action"
          ></moz-toggle>
        </moz-box-item>`
    );

  if (hasStatic) {
    return html`${createItems(3)}${createItems(2, "static", 3)}`;
  }
  return html`${createItems(5)}`;
}

function basicElements() {
  return html`<moz-box-item data-l10n-id="moz-box-item">
      ${selectAction()}
      <moz-button
        iconsrc="chrome://global/skin/icons/edit-outline.svg"
        data-l10n-id="moz-box-edit-action"
        type="ghost"
        slot="actions"
      ></moz-button>
      <moz-toggle
        slot="actions"
        pressed
        data-l10n-id="moz-box-toggle-action"
      ></moz-toggle>
      <moz-button
        iconsrc="chrome://global/skin/icons/more.svg"
        data-l10n-id="moz-box-more-action"
        slot="actions-start"
      ></moz-button>
    </moz-box-item>
    <moz-box-link data-l10n-id="moz-box-link"></moz-box-link>
    <moz-box-button data-l10n-id="moz-box-button-1"></moz-box-button>
    <moz-box-item data-l10n-id="moz-box-item">
      <moz-button
        iconsrc="chrome://global/skin/icons/edit-outline.svg"
        data-l10n-id="moz-box-edit-action"
        type="ghost"
        slot="actions-start"
      ></moz-button>
      <moz-button
        iconsrc="chrome://global/skin/icons/more.svg"
        data-l10n-id="moz-box-more-action"
        slot="actions-start"
      ></moz-button>
    </moz-box-item>
    <moz-box-button
      iconsrc="chrome://global/skin/icons/delete.svg"
      @click=${deleteItem}
      data-l10n-id="moz-box-button-2"
    ></moz-box-button> `;
}

const deleteItem = event => {
  event.target.remove();
};

const appendItem = event => {
  let group = event.target.getRootNode().querySelector("moz-box-group");

  let boxItem = document.createElement("moz-box-item");
  boxItem.label = "New box item";
  boxItem.description = "New items are added to the list";

  let actionButton = document.createElement("moz-button");
  actionButton.addEventListener("click", () => boxItem.remove());
  actionButton.iconSrc = "chrome://global/skin/icons/delete.svg";
  actionButton.slot = "actions";
  actionButton.setAttribute("data-l10n-id", "moz-box-delete-action");
  boxItem.append(actionButton);

  group.prepend(boxItem);
};

/**
 * Handles the reorder event from moz-box-group. Since we're not using
 * Lit for updates in this case, we need to manually reorder the elements.
 *
 * @param {CustomEvent} event - The reorder event.
 * @param {object} event.detail - Detail object containing reorder information.
 * @param {Element} event.detail.draggedElement - The element being reordered.
 * @param {Element} event.detail.targetElement - The target element to reorder relative to.
 * @param {number} event.detail.position - Position relative to target (-1 for before, 0 for after).
 */
const handleReorderEvent = event => {
  let group = event.target.getRootNode().querySelector("moz-box-group");
  let { draggedElement, targetElement, position } = event.detail;
  let moveBefore = position === -1;

  if (moveBefore) {
    group.insertBefore(draggedElement, targetElement);
  } else {
    group.insertBefore(draggedElement, targetElement.nextElementSibling);
  }

  draggedElement.focus();
  group.updateItems();
};

// Example with all child elements wrapped in setting-control/setting-group,
// which is the most common use case in Firefox preferences.
function wrappedTemplate({ type, hasHeader, hasFooter }) {
  return html`<setting-control
    .config=${getConfig({ type, hasHeader, hasFooter })}
    .setting=${DEFAULT_SETTING}
    .getSetting=${getSetting}
  ></setting-control>`;
}

const getConfig = ({ type, hasHeader, hasFooter }) => ({
  id: "exampleWrapped",
  control: "moz-box-group",
  controlAttrs: {
    type,
  },
  items: [
    ...(hasHeader
      ? [
          {
            id: "header",
            control: "moz-box-item",
            l10nId: "moz-box-item-header",
            slot: "header",
          },
        ]
      : []),
    {
      id: "item1",
      control: "moz-box-item",
      l10nId: "moz-box-item",
      options: [
        {
          id: "slotted-button",
          control: "moz-button",
          l10nId: "moz-box-edit-action",
          iconSrc: "chrome://global/skin/icons/edit-outline.svg",
          controlAttrs: {
            type: "ghost",
            slot: "actions",
          },
        },
        {
          id: "slotted-toggle",
          control: "moz-toggle",
          l10nId: "moz-box-toggle-action",
          controlAttrs: {
            slot: "actions",
          },
        },
        {
          id: "slotted-icon-button",
          control: "moz-button",
          l10nId: "moz-box-more-action",
          iconSrc: "chrome://global/skin/icons/more.svg",
          controlAttrs: {
            slot: "actions",
          },
        },
      ],
    },
    {
      id: "link1",
      control: "moz-box-link",
      l10nId: "moz-box-link",
    },
    {
      id: "button1",
      control: "moz-box-button",
      l10nId: "moz-box-button-1",
    },
    {
      id: "item2",
      control: "moz-box-item",
      l10nId: "moz-box-item",
      options: [
        {
          id: "slotted-button-start",
          control: "moz-button",
          l10nId: "moz-box-edit-action",
          iconSrc: "chrome://global/skin/icons/edit-outline.svg",
          controlAttrs: {
            type: "ghost",
            slot: "actions-start",
          },
        },
        {
          id: "slotted-icon-button-start",
          control: "moz-button",
          l10nId: "moz-box-more-action",
          iconSrc: "chrome://global/skin/icons/more.svg",
          controlAttrs: {
            slot: "actions-start",
          },
        },
      ],
    },
    {
      id: "button2",
      control: "moz-box-button",
      l10nId: "moz-box-button-2",
    },
    ...(hasFooter
      ? [
          {
            id: "footer",
            control: "moz-box-button",
            l10nId: "moz-box-button-footer",
            slot: "footer",
          },
        ]
      : []),
  ],
});

const DEFAULT_SETTING = {
  value: 1,
  on() {},
  off() {},
  userChange() {},
  getControlConfig: c => c,
  controllingExtensionInfo: {},
  visible: true,
};

function getSetting() {
  return {
    value: true,
    on() {},
    off() {},
    userChange() {},
    visible: () => true,
    getControlConfig: c => c,
    controllingExtensionInfo: {},
  };
}

const standardTemplateHtml = ({ scrollable }) => html`
  <style>
    moz-box-group {
      --box-group-max-height: ${scrollable ? "250px" : "unset"};
    }

    .delete {
      margin-top: var(--space-medium);
    }
  </style>
`;

const Template = ({
  type,
  hasHeader,
  hasFooter,
  scrollable,
  wrapped,
  hasStatic,
}) => html`
  ${standardTemplateHtml({ scrollable })}
  ${wrapped
    ? wrappedTemplate({ type, hasHeader, hasFooter })
    : basicTemplate({ type, hasHeader, hasFooter, hasStatic })}
`;

const ReorderableTemplate = ({
  type,
  hasHeader,
  hasFooter,
  scrollable,
  hasStatic,
}) => html`
  ${standardTemplateHtml({ scrollable })}
  ${basicTemplate({ type, hasHeader, hasFooter, hasStatic })}
`;

export const Default = Template.bind({});
Default.args = {
  type: "default",
  hasHeader: false,
  hasFooter: false,
  scrollable: false,
  wrapped: false,
  hasStatic: false,
};

export const List = Template.bind({});
List.args = {
  ...Default.args,
  type: "list",
};

export const Reorderable = ReorderableTemplate.bind({});
Reorderable.args = {
  type: "reorderable",
  hasHeader: false,
  hasFooter: false,
  scrollable: false,
};

export const ReorderableWithStatic = ReorderableTemplate.bind({});
ReorderableWithStatic.args = {
  ...Reorderable.args,
  hasStatic: true,
};

export const ListWithHeaderAndFooter = Template.bind({});
ListWithHeaderAndFooter.args = {
  ...List.args,
  hasHeader: true,
  hasFooter: true,
};

export const Scrollable = Template.bind({});
Scrollable.args = {
  ...ListWithHeaderAndFooter.args,
  scrollable: true,
};

export const Wrapped = Template.bind({});
Wrapped.args = {
  ...ListWithHeaderAndFooter.args,
  wrapped: true,
};
