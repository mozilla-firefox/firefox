// url=https://www.figma.com/design/3WoKOSGtaSjhUHKldHCXbc/Desktop-Components-3?node-id=1-589
// Icon Button

import figma from "figma";
const iconSrc = "chrome://example.svg";
const type = figma.selectedInstance.getBoolean("Ghost", {
  true: "icon ghost",
  false: "icon",
});
const attention = figma.selectedInstance.getBoolean("Show attention dot");
const disabled = figma.selectedInstance.getEnum("State", {
  Disabled: true,
});
const size = figma.selectedInstance.getEnum("Size", {
  Small: "small",
});

export default {
  id: "moz-button-icon-desktop-v3",
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
