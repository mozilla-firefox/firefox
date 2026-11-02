// url=https://www.figma.com/design/PqfaOcMGbX5liEXTTUzeYX/Nova-Components--Experimental-?node-id=3907-17659
// Nova Components

import figma from "figma";

const labelInstance = figma.selectedInstance.findInstance("Label");
const description =
  labelInstance.type !== "ERROR"
    ? labelInstance.getBoolean("Show description", {
        true: labelInstance.getString("Description"),
      })
    : undefined;
const label =
  labelInstance.type !== "ERROR" ? labelInstance.getString("Label") : undefined;
const supportPage =
  labelInstance.type !== "ERROR"
    ? labelInstance.getBoolean("Show support link", {
        true: "sumo-slug",
      })
    : undefined;
const iconSrc =
  labelInstance.type !== "ERROR"
    ? labelInstance.getBoolean("Show icon", {
        true: "chrome://example.svg",
      })
    : undefined;

const toggleInstance = figma.selectedInstance.findInstance("Toggle / Input");
const pressed =
  toggleInstance.type !== "ERROR"
    ? (toggleInstance.getBoolean("Checked", {
        true: true,
        false: undefined,
      }) ?? toggleInstance.getEnum("State", { Checked: true }))
    : undefined;
const disabled = figma.selectedInstance.getBoolean("Disabled");

export default {
  id: "moz-toggle",
  example: figma.code`<moz-toggle
        name="example-moz-toggle-name"
        value="example moz-toggle value"
        ${pressed === true ? "pressed" : ""}
        ${disabled ? "disabled" : ""}
        ${label ? `label="${label}"` : ""}
        ${description ? `description="${description}"` : ""}
        ${iconSrc ? `iconsrc="${iconSrc}"` : ""}
        ${supportPage ? `support-page="${supportPage}"` : ""}
      ></moz-toggle> `,
};
