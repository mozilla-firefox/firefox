import figma, { html } from "@figma/code-connect/html";

// Nova Components
figma.connect(
  "https://www.figma.com/design/PqfaOcMGbX5liEXTTUzeYX/Nova-Components--Experimental-?node-id=1-1123",
  {
    props: {
      items: figma.children("*"),
    },
    example: props => html`<moz-button-group>${props.items}</moz-button-group>`,
  }
);

// Desktop v3 (newest)
figma.connect(
  "https://www.figma.com/design/3WoKOSGtaSjhUHKldHCXbc/Desktop-Components-3?node-id=1-1123",
  {
    props: {
      items: figma.children("*"),
    },
    example: props => html`<moz-button-group>${props.items}</moz-button-group>`,
  }
);
