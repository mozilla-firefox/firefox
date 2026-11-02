import figma, { html } from "@figma/code-connect/html";

// Nova Components
figma.connect(
  "https://www.figma.com/design/PqfaOcMGbX5liEXTTUzeYX/Nova-Components--Experimental-?node-id=2719-28745",
  {
    props: {
      showLegend: figma.boolean("Show legend", {
        true: figma.string("Label"),
        false: undefined,
      }),
      showDescription: figma.boolean("Show description", {
        true: figma.string("Description"),
        false: undefined,
      }),
      showSupportLink: figma.boolean("Show support link", {
        true: "support-page-endpoint-example",
        false: undefined,
      }),
      iconSrc: figma.boolean("Show legend", {
        true: figma.boolean("Show icon", {
          true: "chrome://example.svg",
          false: undefined,
        }),
        false: undefined,
      }),
      children: figma.children("*"),
    },
    example: props => html`
      <moz-fieldset
        label=${props.showLegend}
        iconsrc=${props.iconSrc}
        description=${props.showDescription}
        support-page=${props.showSupportLink}
      >
        ${props.children}
      </moz-fieldset>
      <!--
  The moz-fieldset component supports setting the heading level
  ('1' to '6' corresponding to <h1> to <h6>) of the label and a
  disabled state.

  These properties are not currently reflected in the Figma
  component properties.

  The Figma component currently allows for the label icon to display
  without label text. This will be fixed. The Code Connect snippet will
  reflect the correct behavior.
-->
    `,
  }
);

// Desktop V3
figma.connect(
  "https://www.figma.com/design/3WoKOSGtaSjhUHKldHCXbc/Desktop-Components-3?node-id=2719-28745",
  {
    props: {
      showLegend: figma.boolean("Show legend", {
        true: figma.string("Label"),
        false: undefined,
      }),
      showDescription: figma.boolean("Show description", {
        true: figma.string("Description"),
        false: undefined,
      }),
      showSupportLink: figma.boolean("Show support link", {
        true: "support-page-endpoint-example",
        false: undefined,
      }),
      iconSrc: figma.boolean("Show legend", {
        true: figma.boolean("Show icon", {
          true: "chrome://example.svg",
          false: undefined,
        }),
        false: undefined,
      }),
      children: figma.children("*"),
    },
    example: props => html`
      <moz-fieldset
        label=${props.showLegend}
        iconsrc=${props.iconSrc}
        description=${props.showDescription}
        support-page=${props.showSupportLink}
      >
        ${props.children}
      </moz-fieldset>
      <!--
  The moz-fieldset component supports setting the heading level
  ('1' to '6' corresponding to <h1> to <h6>) of the label and a
  disabled state.

  These properties are not currently reflected in the Figma
  component properties.

  The Figma component currently allows for the label icon to display
  without label text. This will be fixed. The Code Connect snippet will
  reflect the correct behavior.
-->
    `,
  }
);
