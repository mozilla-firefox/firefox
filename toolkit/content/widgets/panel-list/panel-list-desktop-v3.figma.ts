// url=https://www.figma.com/design/3WoKOSGtaSjhUHKldHCXbc/Desktop-Components-3?node-id=478-7070
// Desktop Components v3 (newest)

import figma from "figma";
const items = figma.properties.children(["*"]);

export default {
  id: "panel-list-desktop-v3",
  example: figma.code` <panel-list open id="panel-list"> ${items} </panel-list>`,
};
