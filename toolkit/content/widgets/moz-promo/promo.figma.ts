import figma, { html } from "@figma/code-connect/html";

// Nova Components
figma.connect(
  "https://www.figma.com/design/PqfaOcMGbX5liEXTTUzeYX/Nova-Components--Experimental-?node-id=4429-687",
  {
    props: {
      body: figma.string("Body"),
      heading: figma.boolean("Show heading", {
        true: figma.string("Heading"),
        false: undefined,
      }),
      image: figma.boolean("Show image", {
        true: "path/to/image.png",
        false: undefined,
      }),
      imageAlignment: figma.boolean("Show image", {
        true: figma.enum("Image position", {
          Left: "start",
          Right: "end",
          Bottom: "center",
        }),
        false: undefined,
      }),
      actions: figma.boolean("Show actions", {
        true: html`
          <moz-button slot="actions">Label</moz-button>
          <a is="moz-support-link" slot="support-link" href="#"></a>
        `,
        false: undefined,
      }),
    },
    example: props => html`
      <moz-promo
        heading=${props.heading}
        message=${props.body}
        imageSrc=${props.image}
        imageAlignment=${props.imageAlignment}
      >
        ${props.actions}
      </moz-promo>
      <!--
  The following figma options are not currently supported by moz-promo:
  - Action position
  - Image size
  - Image display
-->
    `,
  }
);

// Desktop V3
figma.connect(
  "https://www.figma.com/design/3WoKOSGtaSjhUHKldHCXbc/Desktop-Components-3?node-id=4429-687",
  {
    props: {
      body: figma.string("Body"),
      heading: figma.boolean("Show heading", {
        true: figma.string("Heading"),
        false: undefined,
      }),
      image: figma.boolean("Show image", {
        true: "path/to/image.png",
        false: undefined,
      }),
      imageAlignment: figma.boolean("Show image", {
        true: figma.enum("Image position", {
          Left: "start",
          Right: "end",
          Bottom: "center",
        }),
        false: undefined,
      }),
      actions: figma.boolean("Show actions", {
        true: html`
          <moz-button slot="actions">Label</moz-button>
          <a is="moz-support-link" slot="support-link" href="#"></a>
        `,
        false: undefined,
      }),
    },
    example: props => html`
      <moz-promo
        heading=${props.heading}
        message=${props.body}
        imageSrc=${props.image}
        imageAlignment=${props.imageAlignment}
      >
        ${props.actions}
      </moz-promo>
      <!--
  The following figma options are not currently supported by moz-promo:
  - Action position
  - Image size
  - Image display
-->
    `,
  }
);
