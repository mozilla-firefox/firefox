import figma, { html } from "@figma/code-connect/html";

// Nova Components
figma.connect(
  "https://www.figma.com/design/PqfaOcMGbX5liEXTTUzeYX/Nova-Components--Experimental-?node-id=538-18711",
  {
    props: {
      type: figma.enum("Type", {
        Default: undefined,
        Slot: "slot",
        Accordion: "accordion",
      }),
      heading: figma.string("Heading"),
      slot: figma.instance("Slot"),
      expanded: figma.boolean("Expanded"),
      iconSrc: figma.boolean("Show icon", {
        true: "chrome://example.svg",
        false: undefined,
      }),
    },
    example: props =>
      html`<moz-card
        type=${props.type}
        expanded=${props.expanded}
        heading=${props.heading}
        iconSrc=${props.iconSrc}
      >
        ${props.slot}
      </moz-card>`,
  }
);

figma.connect(
  "https://www.figma.com/design/PqfaOcMGbX5liEXTTUzeYX/Nova-Components--Experimental-?node-id=538-18711",
  {
    variant: { Type: "Slot" },
    example: () =>
      html`<moz-card>
        <p>Card content goes here.</p>
      </moz-card>`,
  }
);

figma.connect(
  "https://www.figma.com/design/PqfaOcMGbX5liEXTTUzeYX/Nova-Components--Experimental-?node-id=477-6023",
  {
    props: {
      expanded: figma.boolean("Expanded", {
        true: "This card is in the 'expanded' state.",
        false: undefined,
      }),
    },
    example: props =>
      html`This subcomponent is created by passing a 'heading' property to the
      moz-card component. See the moz-card for detail. ${props.expanded}`,
  }
);

// Desktop v3 (newest)
figma.connect(
  "https://www.figma.com/design/3WoKOSGtaSjhUHKldHCXbc/Desktop-Components-3?node-id=538-18711",
  {
    props: {
      type: figma.enum("Type", {
        Empty: undefined,
        Slot: "slot",
        Accordion: "accordion",
      }),
      heading: figma.boolean("Show heading", {
        true: figma.string("Heading"),
        false: undefined,
      }),
      slot: figma.instance("Slot"),
      expanded: figma.boolean("Expanded"),
      iconSrc: figma.boolean("Show icon", {
        true: "chrome://example.svg",
        false: undefined,
      }),
    },
    example: props =>
      html`<moz-card
        type=${props.type}
        expanded=${props.expanded}
        heading=${props.heading}
        iconSrc=${props.iconSrc}
      >
        ${props.slot}
      </moz-card>`,
  }
);

figma.connect(
  "https://www.figma.com/design/3WoKOSGtaSjhUHKldHCXbc/Desktop-Components-3?node-id=477-6023",
  {
    props: {
      expanded: figma.boolean("Expanded", {
        true: "This card is in the 'expanded' state.",
        false: undefined,
      }),
    },
    example: props =>
      html`This subcomponent is created by passing a 'heading' property to the
      moz-card component. See the moz-card for detail. ${props.expanded}`,
  }
);
