import figma, { html } from "@figma/code-connect/html";

// Nova Components
figma.connect(
  "https://www.figma.com/design/PqfaOcMGbX5liEXTTUzeYX/Nova-Components--Experimental-?node-id=496-6135",
  {
    props: {
      items: figma.children("*"),
    },
    example: props =>
      html` <moz-box-group type="list"> ${props.items} </moz-box-group>`,
  }
);

// Desktop v3 (newest)
figma.connect(
  "https://www.figma.com/design/3WoKOSGtaSjhUHKldHCXbc/Desktop-Components-3?node-id=496-6135",
  {
    props: {
      items: figma.children("*"),
    },
    example: props =>
      html` <moz-box-group type="list"> ${props.items} </moz-box-group>`,
  }
);
