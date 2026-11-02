// url=https://www.figma.com/design/PqfaOcMGbX5liEXTTUzeYX/Nova-Components--Experimental-?node-id=9401-35716
// Nova Components - Icon button

import figma from "figma";
const type = figma.selectedInstance.getEnum("Type", {
  Default: "icon",
  Ghost: "icon ghost",
  Neutral: "icon neutral",
  Primary: "icon primary",
});
const disabled = figma.selectedInstance.getEnum("State", {
  Disabled: true,
});
const size = figma.selectedInstance.getEnum("Size", {
  Small: "small",
  Large: "large",
});
const attention = figma.selectedInstance.getBoolean("Show attention dot");

export default {
  id: "moz-button-icon",
  example: figma.code`
      <moz-button
        ${type ? `type="${type}"` : ""}
        ${disabled ? "disabled" : ""}
        ${size ? `size="${size}"` : ""}
        iconsrc="chrome://example.svg"
        title="the hidden label"
        ${attention ? "attention" : ""}
      ></moz-button>
    `,
};
