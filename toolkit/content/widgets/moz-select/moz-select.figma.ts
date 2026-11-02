import figma, { html } from "@figma/code-connect/html";

// Nova Components
// Dropdown
figma.connect(
  "https://www.figma.com/design/PqfaOcMGbX5liEXTTUzeYX/Nova-Components--Experimental-?node-id=5858-8185",
  {
    props: {
      label: figma.nestedProps("Label", {
        text: figma.string("Label"),
        description: figma.string("Description"),
        showSupportLink: figma.boolean("Show support link", {
          true: "support-page-endpoint-example",
          false: undefined,
        }),
        showIcon: figma.boolean("Show icon", {
          true: "chrome://example.svg",
          false: undefined,
        }),
      }),
      select: figma.nestedProps("Select / Input", {
        value: figma.string("Label"),
      }),
      disabled: figma.boolean("Disabled"),
    },
    example: props => html`
      <moz-select
        label=${props.label.text}
        name="example-moz-select"
        value=${props.select.value}
        disabled=${props.disabled}
        iconsrc=${props.label.showIcon}
        description=${props.label.description}
        support-page=${props.label.showSupportLink}
      >
        <moz-option value="option1">Option 1</moz-option>
        <moz-option value="option2">Option 2</moz-option>
      </moz-select>
    `,
  }
);

// Desktop V3
// Menu / Native / Item
figma.connect(
  "https://www.figma.com/design/3WoKOSGtaSjhUHKldHCXbc/Desktop-Components-3?node-id=236-1121",
  {
    props: {
      label: figma.string("Label"),
      disabled: figma.enum("State", { Disabled: true }),
      checked: figma.boolean("Checked"),
      iconSrc: figma.boolean("Checked", {
        true: "chrome://example.svg",
        false: undefined,
      }),
    },
    example: props =>
      html` <moz-option
        value=${props.label}
        iconsrc=${props.iconSrc}
        selected=${props.checked}
        disabled=${props.disabled}
      >
        ${props.label}
      </moz-option>`,
  }
);

// Dropdown
figma.connect(
  "https://www.figma.com/design/3WoKOSGtaSjhUHKldHCXbc/Desktop-Components-3?node-id=3907-19254",
  {
    props: {
      label: figma.nestedProps("Label", {
        text: figma.string("Label"),
        description: figma.string("Description"),
        showSupportLink: figma.boolean("Show support link", {
          true: "support-page-endpoint-example",
          false: undefined,
        }),
        showIcon: figma.boolean("Show icon", {
          true: "chrome://example.svg",
          false: undefined,
        }),
      }),
      select: figma.nestedProps("Dropdown / Button", {
        value: figma.string("Label"),
      }),
      disabled: figma.boolean("Disabled"),
      expandedMenu: figma.nestedProps("Dropdown / Menu / Native", {
        options: figma.children("*"),
      }),
    },
    example: props => html`
      <moz-select
        label=${props.label.text}
        name="example-moz-select"
        value=${props.select.value}
        disabled=${props.disabled}
        iconsrc=${props.label.showIcon}
        description=${props.label.description}
        support-page=${props.label.showSupportLink}
      >
        ${props.expandedMenu.options}
      </moz-select>
    `,
  }
);
