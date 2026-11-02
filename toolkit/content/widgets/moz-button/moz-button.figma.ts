// url=https://www.figma.com/design/PqfaOcMGbX5liEXTTUzeYX/Nova-Components--Experimental-?node-id=8849-25406
// Nova Components - Button

import figma from "figma";

const label = figma.selectedInstance.getString("Label");
const showIcon = figma.selectedInstance.getBoolean("Show icon");
const showIconEnd = figma.selectedInstance.getBoolean("Show icon end");
const iconSrc = showIcon || showIconEnd ? "chrome://example.svg" : undefined;
const iconPosition = showIconEnd ? "end" : undefined;
const type = figma.selectedInstance.getEnum("Type", {
  Default: "default",
  Primary: "primary",
  Destructive: "destructive",
  Ghost: "ghost",
  Muted: "muted",
  Neutral: "neutral",
});
const disabled = figma.selectedInstance.getEnum("State", {
  Disabled: true,
});
const size = figma.selectedInstance.getEnum("Size", {
  Default: "default",
  Small: "small",
  Large: "large",
});
const attention = figma.selectedInstance.getBoolean("Show attention dot");

export default {
  id: "moz-button",
  example: figma.code`
    <moz-button
      type="${type}"
      size="${size}"
      ${disabled ? "disabled" : ""}
      ${iconSrc ? `iconsrc="${iconSrc}"` : ""}
      ${iconPosition ? `iconposition="${iconPosition}"` : ""}
      ${attention ? "attention" : ""}
      label="${label}"
    ></moz-button>
  `,
};
