// url=https://www.figma.com/design/3WoKOSGtaSjhUHKldHCXbc/Desktop-Components-3?node-id=1-255
// Desktop V3 (newest)

import figma from "figma";

const showIcon = figma.selectedInstance.getBoolean("Show icon");
const showIconEnd = figma.selectedInstance.getBoolean("Show icon end");
const iconSrc = showIcon || showIconEnd ? "chrome://example.svg" : undefined;
const iconPosition = showIconEnd ? "end" : undefined;
const label = figma.selectedInstance.getString("Label");
const type = figma.selectedInstance.getEnum("Type", {
  Primary: "primary",
  Destructive: "destructive",
  Ghost: "ghost",
});
const disabled = figma.selectedInstance.getEnum("State", {
  Disabled: true,
});
const size = figma.selectedInstance.getEnum("Size", {
  Small: "small",
});

export default {
  id: "moz-button-desktop-v3",
  example: figma.code`
    <moz-button
      ${type ? `type="${type}"` : ""}
      ${size ? `size="${size}"` : ""}
      ${disabled ? "disabled" : ""}
      ${iconSrc ? `iconsrc="${iconSrc}"` : ""}
      ${iconPosition ? `iconposition="${iconPosition}"` : ""}
      label="${label}"
    ></moz-button>
  `,
};
