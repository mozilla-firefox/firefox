// url=https://www.figma.com/design/3WoKOSGtaSjhUHKldHCXbc/Desktop-Components-3?node-id=479-5966&m=dev
// Desktop Components 3

import figma from "figma";
const label = figma.selectedInstance.getString("Label");
const iconSrc = figma.selectedInstance.getBoolean("Show icon", {
  true: "chrome://example.svg",
});
const description = figma.selectedInstance.getBoolean("Show description", {
  true: figma.selectedInstance.getString("Description"),
});

export default {
  id: "moz-box-link-desktop-v3",
  example: figma.code`<moz-box-link
        ${label ? `label="${label}"` : ""}
        ${description ? `description="${description}"` : ""}
        ${iconSrc ? `iconsrc="${iconSrc}"` : ""}
      ></moz-box-link>`,
};
