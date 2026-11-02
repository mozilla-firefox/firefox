import figma, { html } from "@figma/code-connect/html";

// Nova Components
figma.connect(
  "https://www.figma.com/design/PqfaOcMGbX5liEXTTUzeYX/Nova-Components--Experimental-?node-id=479-6030",
  {
    props: {
      labelProps: figma.nestedProps("Label", {
        label: figma.string("Label"),
        iconSrc: figma.boolean("Show icon", {
          true: "chrome://example.svg",
        }),
        description: figma.boolean("Show description", {
          true: figma.string("Description"),
        }),
      }),
      layout: figma.enum("Type", {
        "Large icon": "large-icon",
        "Medium icon": "medium-icon",
      }),
      startActions: figma.enum("Type", {
        "Actions preset": html`<moz-button
          type="icon ghost"
          iconsrc="chrome://example.svg"
          aria-label="start action button"
          slot="actions-start"
        ></moz-button>`,
        "Actions slots": html`<moz-button
          type="icon ghost"
          iconsrc="chrome://example.svg"
          aria-label="start action button"
          slot="actions-start"
        ></moz-button>`,
      }),
      endActions: figma.enum("Type", {
        "Actions preset": html`<moz-button
            type="icon ghost"
            iconsrc="chrome://example.svg"
            aria-label="end action button"
            slot="actions"
          ></moz-button
          ><moz-toggle aria-label="end toggle" slot="actions"></moz-toggle>`,
        "Actions slots": html`<moz-button
            type="icon ghost"
            iconsrc="chrome://example.svg"
            aria-label="end action button"
            slot="actions"
          ></moz-button
          ><moz-toggle aria-label="end toggle" slot="actions"></moz-toggle>`,
      }),
    },
    example: props =>
      html`<moz-box-item
        label=${props.labelProps.label}
        description=${props.labelProps.description}
        iconsrc=${props.labelProps.iconSrc}
        layout=${props.layout}
      >
        ${props.startActions}${props.endActions}
      </moz-box-item>`,
  }
);

figma.connect(
  "https://www.figma.com/design/PqfaOcMGbX5liEXTTUzeYX/Nova-Components--Experimental-?node-id=479-6030",
  {
    variant: { Type: "Slot" },
    example: () =>
      html`<moz-box-item>
        <p>
          You can put whatever you want in here! Use your imagination! (Just
          don't pass other props as they won't work/get overridden by the slot)
        </p>
      </moz-box-item>`,
  }
);

// Desktop Components 3
figma.connect(
  "https://www.figma.com/design/3WoKOSGtaSjhUHKldHCXbc/Desktop-Components-3?node-id=479-6030&m=dev",
  {
    props: {
      label: figma.string("Label"),
      iconSrc: figma.boolean("Show icon", {
        true: "chrome://example.svg",
      }),
      description: figma.boolean("Show description", {
        true: figma.string("Description"),
      }),
      layout: figma.enum("Type", {
        "Large icon": "large-icon",
        "Medium icon": "medium-icon",
      }),
      startActions: figma.enum("Type", {
        "Actions preset": html`<moz-button
          type="icon ghost"
          iconsrc="chrome://example.svg"
          aria-label="start action button"
          slot="actions-start"
        ></moz-button>`,
        "Actions slots": html`<moz-button
          type="icon ghost"
          iconsrc="chrome://example.svg"
          aria-label="start action button"
          slot="actions-start"
        ></moz-button>`,
      }),
      endActions: figma.enum("Type", {
        "Actions preset": html`<moz-button
            type="icon ghost"
            iconsrc="chrome://example.svg"
            aria-label="end action button"
            slot="actions"
          ></moz-button
          ><moz-toggle aria-label="end toggle" slot="actions"></moz-toggle>`,
        "Actions slots": html`<moz-button
            type="icon ghost"
            iconsrc="chrome://example.svg"
            aria-label="end action button"
            slot="actions"
          ></moz-button
          ><moz-toggle aria-label="end toggle" slot="actions"></moz-toggle>`,
      }),
    },
    example: props =>
      html`<moz-box-item
        label=${props.label}
        description=${props.description}
        iconsrc=${props.iconSrc}
        layout=${props.layout}
      >
        ${props.startActions}${props.endActions}
      </moz-box-item>`,
  }
);

figma.connect(
  "https://www.figma.com/design/3WoKOSGtaSjhUHKldHCXbc/Desktop-Components-3?node-id=479-6030&m=dev",
  {
    variant: { Type: "Slot" },
    example: () =>
      html`<moz-box-item>
        <p>
          You can put whatever you want in here! Use your imagination! (Just
          don't pass other props as they won't work/get overridden by the slot)
        </p>
      </moz-box-item>`,
  }
);
