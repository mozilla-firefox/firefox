import figma, { html } from "@figma/code-connect/html";

// Nova Components
figma.connect(
  "https://www.figma.com/design/PqfaOcMGbX5liEXTTUzeYX/Nova-Components--Experimental-?node-id=475-5657",
  {
    props: {
      label: figma.string("Label"),
      disabled: figma.enum("State", {
        Disabled: true,
      }),
    },
    example: props => html`
      <moz-input-color
        label=${props.label}
        disabled=${props.disabled}
        value="#D9D9D9"
      />
    `,
  }
);

// Desktop V3
figma.connect(
  "https://www.figma.com/design/3WoKOSGtaSjhUHKldHCXbc/Desktop-Components-3?node-id=475-5657",
  {
    props: {
      label: figma.string("Label"),
      disabled: figma.enum("State", {
        Disabled: true,
      }),
    },
    example: props => html`
      <moz-input-color
        label=${props.label}
        disabled=${props.disabled}
        value="#D9D9D9"
      />
    `,
  }
);
