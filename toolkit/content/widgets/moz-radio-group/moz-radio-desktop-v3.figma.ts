// url=https://www.figma.com/design/3WoKOSGtaSjhUHKldHCXbc/Desktop-Components-3?node-id=3907-17657
// Desktop Components v3 (newest)

import figma from "figma";

const labelInstance = figma.selectedInstance.findInstance("Label");
const description =
  labelInstance.type !== "ERROR"
    ? labelInstance.getBoolean("Show description", {
        true: labelInstance.getString("Description"),
      })
    : undefined;
const supportPage =
  labelInstance.type !== "ERROR"
    ? labelInstance.getBoolean("Show support link", {
        true: "sumo-slug",
      })
    : undefined;
const label =
  labelInstance.type !== "ERROR" ? labelInstance.getString("Label") : undefined;
const iconSrc =
  labelInstance.type !== "ERROR"
    ? labelInstance.getBoolean("Show icon", {
        true: "chrome://example.svg",
      })
    : undefined;

const radioInstance = figma.selectedInstance.findInstance("Radio Button");
const checked =
  radioInstance.type !== "ERROR"
    ? (radioInstance.getBoolean("Checked", {
        true: true,
        false: undefined,
      }) ?? radioInstance.getEnum("State", { Checked: true }))
    : undefined;
const disabled = figma.selectedInstance.getBoolean("Disabled");

export default {
  id: "moz-radio-desktop-v3",
  example: figma.code`
      <moz-radio
        ${checked === true ? "checked" : ""}
        ${description ? `description="${description}"` : ""}
        ${disabled ? "disabled" : ""}
        ${iconSrc ? `iconsrc="${iconSrc}"` : ""}
        ${label ? `label="${label}"` : ""}
        name="example-moz-radio-group-name"
        ${supportPage ? `support-page="${supportPage}"` : ""}
        value="example moz-radio value"
      ></moz-radio>
    `,
};
