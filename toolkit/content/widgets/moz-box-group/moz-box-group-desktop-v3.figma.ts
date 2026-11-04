// url=https://www.figma.com/design/3WoKOSGtaSjhUHKldHCXbc/Desktop-Components-3?node-id=496-6135
// Desktop v3 (newest)

import figma from "figma";
const items = figma.properties.children(["*"]);

export default {
  id: "moz-box-group-desktop-v3",
  example: figma.code` <moz-box-group type="list"> ${items} </moz-box-group>`,
};
