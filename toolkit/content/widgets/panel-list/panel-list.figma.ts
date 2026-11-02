// url=https://www.figma.com/design/PqfaOcMGbX5liEXTTUzeYX/Nova-Components--Experimental-?node-id=478-7070
// Nova Components

import figma from "figma";
const items = figma.properties.children(["*"]);

export default {
  id: "panel-list",
  example: figma.code` <panel-list open id="panel-list"> ${items} </panel-list>`,
};
