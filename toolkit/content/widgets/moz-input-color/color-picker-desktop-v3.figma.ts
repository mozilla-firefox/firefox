// url=https://www.figma.com/design/3WoKOSGtaSjhUHKldHCXbc/Desktop-Components-3?node-id=475-5657
// Desktop V3

import figma from "figma";
const label = figma.selectedInstance.getString("Label");
const disabled = figma.selectedInstance.getEnum("State", {
  Disabled: true,
});

export default {
  id: "color-picker-desktop-v3",
  example: figma.code`
      <moz-input-color
        ${label ? `label="${label}"` : ""}
        ${disabled ? "disabled" : ""}
        value="#D9D9D9"
      />
    `,
};
