// url=https://www.figma.com/design/3WoKOSGtaSjhUHKldHCXbc/Desktop-Components-3?node-id=478-6518
// Desktop v3 - Panel item

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
  id: "panel-item-desktop-v3",
  example: figma.code` <panel-item
          ${iconSrc ? `icon="${iconSrc}"` : ""}
          ${disabled ? "disabled" : ""}
          ${badged ? "badged" : ""}
          ${submenu ? "submenu" : ""}
        >
          ${label}
        </panel-item>`,
};
