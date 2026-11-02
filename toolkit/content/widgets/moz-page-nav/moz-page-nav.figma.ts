import figma, { html } from "@figma/code-connect/html";

// Nova Components
figma.connect(
  "https://www.figma.com/design/PqfaOcMGbX5liEXTTUzeYX/Nova-Components--Experimental-?node-id=474-6445",
  {
    props: {
      heading: figma.string("Heading"),
      items: figma.children("*"),
    },
    example: props => html`
      <moz-page-nav heading=${props.heading}> ${props.items} </moz-page-nav>
    `,
  }
);

figma.connect(
  "https://www.figma.com/design/PqfaOcMGbX5liEXTTUzeYX/Nova-Components--Experimental-?node-id=474-5947",
  {
    props: {
      label: figma.string("Label"),
      iconSrc: figma.boolean("Show icon", {
        true: "chrome://example.svg",
      }),
      selected: figma.boolean("Selected"),
    },
    example: props => html`
      <moz-page-nav-button icon=${props.iconSrc} selected=${props.selected}>
        ${props.label}
      </moz-page-nav-button>
    `,
  }
);

// Desktop v3 (newest)
figma.connect(
  "https://www.figma.com/design/3WoKOSGtaSjhUHKldHCXbc/Desktop-Components-3?node-id=474-6445",
  {
    props: {
      heading: figma.string("Heading"),
      items: figma.children("*"),
    },
    example: props => html`
      <moz-page-nav heading=${props.heading}> ${props.items} </moz-page-nav>
    `,
  }
);

figma.connect(
  "https://www.figma.com/design/3WoKOSGtaSjhUHKldHCXbc/Desktop-Components-3?node-id=474-5947",
  {
    props: {
      label: figma.string("Label"),
      iconSrc: figma.boolean("Show icon", {
        true: "chrome://example.svg",
      }),
      rule: figma.boolean("Show separator", {
        true: html`<hr />`,
      }),
      selected: figma.boolean("Selected"),
    },
    example: props => html`
      <moz-page-nav-button icon=${props.iconSrc} selected=${props.selected}>
        ${props.label}
      </moz-page-nav-button>
      ${props.rule}
    `,
  }
);
