// url=https://www.figma.com/design/3WoKOSGtaSjhUHKldHCXbc/Desktop-Components-3?node-id=1-1123
// Desktop v3 (newest)

import figma from "figma";
const items = figma.properties.children(["*"]);

export default {
  id: "moz-button-group-desktop-v3",
  example: figma.code`<moz-button-group>${items}</moz-button-group>`,
};
