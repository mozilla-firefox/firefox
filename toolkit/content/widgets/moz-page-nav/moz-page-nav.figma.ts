// url=https://www.figma.com/design/PqfaOcMGbX5liEXTTUzeYX/Nova-Components--Experimental-?node-id=474-6445
// Nova Components

import figma from "figma";
const heading = figma.selectedInstance.getString("Heading");
const items = figma.properties.children(["*"]);

export default {
  id: "moz-page-nav",
  example: figma.code`
      <moz-page-nav ${heading ? `heading="${heading}"` : ""}> ${items} </moz-page-nav>
    `,
};
