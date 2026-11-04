// url=https://www.figma.com/design/3WoKOSGtaSjhUHKldHCXbc/Desktop-Components-3?node-id=236-1121
// Menu / Native / Item

import figma from "figma";
const label = figma.selectedInstance.getString("Label");
const disabled = figma.selectedInstance.getEnum("State", {
  Disabled: true,
});
const checked = figma.selectedInstance.getBoolean("Checked");
const iconSrc = figma.selectedInstance.getBoolean("Show icon", {
  true: "chrome://example.svg",
  false: undefined,
});

export default {
  id: "moz-select-menu-item-desktop-v3",
  example: figma.code` <moz-option
        ${label ? `value="${label}"` : ""}
        ${iconSrc ? `iconsrc="${iconSrc}"` : ""}
        ${checked ? "selected" : ""}
        ${disabled ? "disabled" : ""}
      >
        ${label}
      </moz-option>`,
};
