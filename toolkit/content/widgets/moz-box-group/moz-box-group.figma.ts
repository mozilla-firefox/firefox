// url=https://www.figma.com/design/PqfaOcMGbX5liEXTTUzeYX/Nova-Components--Experimental-?node-id=496-6135
// Nova Components

import figma from "figma";
const items = figma.properties.children(["*"]);

export default {
  id: "moz-box-group",
  example: figma.code` <moz-box-group type="list"> ${items} </moz-box-group>`,
};
