// url=https://www.figma.com/design/PqfaOcMGbX5liEXTTUzeYX/Nova-Components--Experimental-?node-id=478-6518
// Nova Components - Panel item

import figma from "figma";
const label = figma.selectedInstance.getString("Label");
const iconSrc = figma.selectedInstance.getBoolean("Show icon", {
  true: "chrome://example.svg",
});
const disabled = figma.selectedInstance.getEnum("State", {
  Disabled: true,
});
const badged = figma.selectedInstance.getBoolean("Show badge");
const submenu = figma.selectedInstance.getBoolean("Show submenu");

export default {
  id: "panel-item",
  example: figma.code` <panel-item
          ${iconSrc ? `icon="${iconSrc}"` : ""}
          ${disabled ? "disabled" : ""}
          ${badged ? "badged" : ""}
          ${submenu ? "submenu" : ""}
        >
          ${label}
        </panel-item>`,
};
