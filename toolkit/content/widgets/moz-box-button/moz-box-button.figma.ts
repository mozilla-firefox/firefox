import figma, { html } from "@figma/code-connect/html";

// Nova Components
figma.connect(
  "https://www.figma.com/design/PqfaOcMGbX5liEXTTUzeYX/Nova-Components--Experimental-?node-id=478-7427",
  {
    props: {
      label: figma.string("Label"),
      iconSrc: figma.boolean("Show icon", {
        true: "chrome://example.svg",
      }),
      description: figma.boolean("Show description", {
        true: figma.string("Description"),
      }),
      disabled: figma.enum("State", {
        Disabled: true,
      }),
    },
    example: props =>
      html`<moz-box-button
        label=${props.label}
        description=${props.description}
        iconsrc=${props.iconSrc}
        disabled=${props.disabled}
      ></moz-box-button>`,
  }
);

// Desktop Components 3
figma.connect(
  "https://www.figma.com/design/3WoKOSGtaSjhUHKldHCXbc/Desktop-Components-3?node-id=478-7427&m=dev",
  {
    props: {
      label: figma.string("Label"),
      iconSrc: figma.boolean("Show icon", {
        true: "chrome://example.svg",
      }),
      description: figma.boolean("Show description", {
        true: figma.string("Description"),
      }),
      disabled: figma.enum("State", {
        Disabled: true,
      }),
    },
    example: props =>
      html`<moz-box-button
        label=${props.label}
        description=${props.description}
        iconsrc=${props.iconSrc}
        disabled=${props.disabled}
      ></moz-box-button>`,
  }
);
