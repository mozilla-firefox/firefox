// url=https://www.figma.com/design/PqfaOcMGbX5liEXTTUzeYX/Nova-Components--Experimental-?node-id=1-1123
// Nova Components

import figma from "figma";
const items = figma.properties.children(["*"]);

export default {
  id: "moz-button-group",
  example: figma.code`<moz-button-group>${items}</moz-button-group>`,
};
