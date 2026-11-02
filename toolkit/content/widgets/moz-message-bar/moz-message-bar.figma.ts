import figma, { html } from "@figma/code-connect/html";

// Nova Components
figma.connect(
  "https://www.figma.com/design/PqfaOcMGbX5liEXTTUzeYX/Nova-Components--Experimental-?node-id=243-1299",
  {
    props: {
      heading: figma.boolean("Show heading", {
        true: figma.string("Heading"),
        false: undefined,
      }),
      message: figma.boolean("Show message", {
        true: figma.string("Message"),
        false: undefined,
      }),
      type: figma.enum("Type", {
        Success: "success",
        Warning: "warning",
        Critical: "critical",
        Information: "information",
      }),
      supportPage: figma.boolean("Show support link", {
        true: "sumo-slug",
      }),
      dismissable: figma.boolean("Dismissible"),
      action: figma.boolean("Show action", {
        true: html`<moz-button slot="actions" label="Label"></moz-button
          ><moz-button slot="actions" label="Label"></moz-button>`,
        false: undefined,
      }),
      buttonGroupProps: figma.nestedProps("Button group", {
        additionalAction: figma.boolean("Show 3rd button", {
          true: html`<moz-button slot="actions" label="Label"></moz-button>`,
          false: undefined,
        }),
      }),
    },
    example: props =>
      html`<moz-message-bar
        type=${props.type}
        message=${props.message}
        heading=${props.heading}
        support-page=${props.supportPage}
        dismissable=${props.dismissable}
        >${props.action}${props.buttonGroupProps
          .additionalAction}</moz-message-bar
      >`,
  }
);

// Desktop v3 (newest)
figma.connect(
  "https://www.figma.com/design/3WoKOSGtaSjhUHKldHCXbc/Desktop-v3?node-id=243-1299&m=dev",
  {
    props: {
      heading: figma.boolean("Show heading", {
        true: figma.string("Heading"),
        false: undefined,
      }),
      message: figma.boolean("Show message", {
        true: figma.string("Message"),
        false: undefined,
      }),
      type: figma.enum("Type", {
        Success: "success",
        Warning: "warning",
        Critical: "critical",
        Information: "information",
      }),
      supportPage: figma.boolean("Show support link", {
        true: "sumo-slug",
      }),
      action: figma.boolean("Show action", {
        true: html`<moz-button slot="actions" label="Label"></moz-button
          ><moz-button slot="actions" label="Label"></moz-button>`,
        false: undefined,
      }),
      buttonGroupProps: figma.nestedProps("Button group", {
        additionalAction: figma.boolean("Show 3rd button", {
          true: html`<moz-button slot="actions" label="Label"></moz-button>`,
          false: undefined,
        }),
      }),
    },
    example: props =>
      html`<moz-message-bar
        type=${props.type}
        message=${props.message}
        heading=${props.heading}
        support-page=${props.supportPage}
        >${props.action}${props.buttonGroupProps
          .additionalAction}</moz-message-bar
      >`,
  }
);
