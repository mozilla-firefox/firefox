import figma, { html } from "@figma/code-connect/html";

// Nova Components
figma.connect(
  "https://www.figma.com/design/PqfaOcMGbX5liEXTTUzeYX/Nova-Components--Experimental-?node-id=479-5966",
  {
    props: {
      label: figma.string("Label"),
      iconSrc: figma.boolean("Show icon", {
        true: "chrome://example.svg",
      }),
      description: figma.boolean("Show description", {
        true: figma.string("Description"),
      }),
    },
    example: props =>
      html`<moz-box-link
        label=${props.label}
        description=${props.description}
        iconsrc=${props.iconSrc}
      ></moz-box-link>`,
  }
);

// Desktop Components 3
figma.connect(
  "https://www.figma.com/design/3WoKOSGtaSjhUHKldHCXbc/Desktop-Components-3?node-id=479-5966&m=dev",
  {
    props: {
      label: figma.string("Label"),
      iconSrc: figma.boolean("Show icon", {
        true: "chrome://example.svg",
      }),
      description: figma.boolean("Show description", {
        true: figma.string("Description"),
      }),
    },
    example: props =>
      html`<moz-box-link
        label=${props.label}
        description=${props.description}
        iconsrc=${props.iconSrc}
      ></moz-box-link>`,
  }
);
