import figma, { html } from "@figma/code-connect/html";

// Nova Components
figma.connect(
  "https://www.figma.com/design/PqfaOcMGbX5liEXTTUzeYX/Nova-Components--Experimental-?node-id=3480-22604",
  {
    props: {
      showIcon: figma.boolean("Show icon", {
        true: "chrome://example.svg",
        false: undefined,
      }),
      label: figma.string("Label"),
    },
    example: props => html`
      <moz-badge
        label=${props.label}
        iconsrc=${props.showIcon}
        title="This appears as a tooltip on hover"
      ></moz-badge>
      <!--
  The Figma component allows for a type ("Default" and "New") to be
  set, as well as a disabled state. These properties are not
  currently reflected in the web component.
-->
    `,
  }
);

// Desktop v3
figma.connect(
  "https://www.figma.com/design/3WoKOSGtaSjhUHKldHCXbc/Desktop-Components-3?node-id=3480-22604",
  {
    props: {
      showIcon: figma.boolean("Show icon", {
        true: "chrome://example.svg",
        false: undefined,
      }),
      label: figma.string("Label"),
    },
    example: props => html`
      <moz-badge
        label=${props.label}
        iconsrc=${props.showIcon}
        title="This appears as a tooltip on hover"
      ></moz-badge>
      <!--
  The Figma component allows for a type ("Default" and "New") to be
  set, as well as a disabled state. These properties are not
  currently reflected in the web component.
-->
    `,
  }
);
