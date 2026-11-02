import figma, { html } from "@figma/code-connect/html";

// Nova Components
figma.connect(
  "https://www.figma.com/design/PqfaOcMGbX5liEXTTUzeYX/Nova-Components--Experimental-?node-id=2249-12942",
  {
    props: {
      labelProps: figma.nestedProps("Label", {
        description: figma.boolean("Show description", {
          true: figma.string("Description"),
        }),
        label: figma.string("Label"),
        supportPage: figma.boolean("Show support link", {
          true: "sumo-slug",
        }),
        iconSrc: figma.boolean("Show icon", {
          true: "chrome://example.svg",
        }),
      }),
      searchInputProps: figma.nestedProps("Search input", {
        placeholder: figma.boolean("Show placeholder", {
          true: figma.string("Placeholder"),
        }),
        text: figma.string("Text"),
      }),
      errorMessage: figma.enum("State", {
        // error: figma.string("Error message"), not yet implemented in the design system
        Error: html` <!--
  The error message functionality is not currently
  available with design system components,
  and will need to be implemented in context for now.
-->`,
      }),
      disabled: figma.enum("State", { Disabled: true }),
    },
    example: props => html`
      <moz-input-search
        label=${props.labelProps.label}
        value=${props.searchInputProps.text}
        iconsrc=${props.labelProps.iconSrc}
        description=${props.labelProps.description}
        support-page=${props.labelProps.supportPage}
        placeholder=${props.searchInputProps.placeholder}
        disabled=${props.disabled}
      >
      </moz-input-search>
      ${props.errorMessage}
    `,
  }
);

// Desktop v3 (newest)
figma.connect(
  "https://www.figma.com/design/3WoKOSGtaSjhUHKldHCXbc/Desktop-Components-3?node-id=2249-12942",
  {
    props: {
      labelProps: figma.nestedProps("Label", {
        description: figma.boolean("Show description", {
          true: figma.string("Description"),
        }),
        label: figma.string("Label"),
        supportPage: figma.boolean("Show support link", {
          true: "sumo-slug",
        }),
        iconSrc: figma.boolean("Show icon", {
          true: "chrome://example.svg",
        }),
      }),
      searchInputProps: figma.nestedProps("Search input", {
        placeholder: figma.boolean("Show placeholder", {
          true: figma.string("Placeholder"),
        }),
        text: figma.string("Text"),
      }),
      errorMessage: figma.enum("State", {
        // error: figma.string("Error message"), not yet implemented in the design system
        Error: html` <!--
  The error message functionality is not currently
  available with design system components,
  and will need to be implemented in context for now.
-->`,
      }),
      disabled: figma.enum("State", { Disabled: true }),
    },
    example: props => html`
      <moz-input-search
        label=${props.labelProps.label}
        value=${props.searchInputProps.text}
        iconsrc=${props.labelProps.iconSrc}
        description=${props.labelProps.description}
        support-page=${props.labelProps.supportPage}
        placeholder=${props.searchInputProps.placeholder}
        disabled=${props.disabled}
      >
      </moz-input-search>
      ${props.errorMessage}
    `,
  }
);
