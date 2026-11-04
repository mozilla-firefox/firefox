// url=https://www.figma.com/design/PqfaOcMGbX5liEXTTUzeYX/Nova-Components--Experimental-?node-id=475-5657
// Nova Components

import figma from "figma";
const label = figma.selectedInstance.getString("Label");
const disabled = figma.selectedInstance.getEnum("State", {
  Disabled: true,
});

export default {
  id: "color-picker",
  example: figma.code`
      <moz-input-color
        ${label ? `label="${label}"` : ""}
        ${disabled ? "disabled" : ""}
        value="#D9D9D9"
      />
    `,
};
