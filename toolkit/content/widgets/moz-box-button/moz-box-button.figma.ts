// url=https://www.figma.com/design/PqfaOcMGbX5liEXTTUzeYX/Nova-Components--Experimental-?node-id=478-7427
// Nova Components

import figma from "figma";
const label = figma.selectedInstance.getString("Label");
const iconSrc = figma.selectedInstance.getBoolean("Show icon", {
  true: "chrome://example.svg",
});
const description = figma.selectedInstance.getBoolean("Show description", {
  true: figma.selectedInstance.getString("Description"),
});
const disabled = figma.selectedInstance.getEnum("State", {
  Disabled: true,
});

export default {
  id: "moz-box-button",
  example: figma.code`<moz-box-button
        ${label ? `label="${label}"` : ""}
        ${description ? `description="${description}"` : ""}
        ${iconSrc ? `iconsrc="${iconSrc}"` : ""}
        ${disabled ? "disabled" : ""}
      ></moz-box-button>`,
};
