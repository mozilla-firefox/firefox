// url=https://www.figma.com/design/PqfaOcMGbX5liEXTTUzeYX/Nova-Components--Experimental-?node-id=1-589
// Nova Components - Toolbar button

import figma from "figma";
const iconSrc = "chrome://example.svg";
const type = "ghost";
const attention = figma.selectedInstance.getBoolean("Show attention dot");
const disabled = figma.selectedInstance.getEnum("State", {
  Disabled: true,
});
const size = figma.selectedInstance.getEnum("Size", {
  Small: "small",
});

export default {
  id: "moz-button-toolbar",
  example: figma.code`
      <moz-button
        ${type ? `type="${type}"` : ""}
        ${disabled ? "disabled" : ""}
        ${size ? `size="${size}"` : ""}
        ${iconSrc ? `iconsrc="${iconSrc}"` : ""}
        title="the hidden label"
        ${attention ? "attention" : ""}
      ></moz-button>
    `,
};
