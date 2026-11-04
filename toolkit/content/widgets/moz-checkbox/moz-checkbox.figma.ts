// url=https://www.figma.com/design/PqfaOcMGbX5liEXTTUzeYX/Nova-Components--Experimental-?node-id=3907-17658
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

const checkboxInstance =
  figma.selectedInstance.findInstance("Checkbox / Input");
const checked =
  checkboxInstance.type !== "ERROR"
    ? (checkboxInstance.getBoolean("Checked", {
        true: true,
        false: undefined,
      }) ?? checkboxInstance.getEnum("State", { Checked: true }))
    : undefined;
const disabled = figma.selectedInstance.getBoolean("Disabled");

export default {
  id: "moz-checkbox",
  example: figma.code`
      <moz-checkbox
        ${label ? `label="${label}"` : ""}
        name="example-moz-checkbox-name"
        value="example moz-checkbox value"
        ${checked === true ? "checked" : ""}
        ${disabled ? "disabled" : ""}
        ${iconSrc ? `iconsrc="${iconSrc}"` : ""}
        ${description ? `description="${description}"` : ""}
        ${supportPage ? `support-page="${supportPage}"` : ""}
      ></moz-checkbox>
      <!--
  The Figma component allows for the moz-checkbox to render in an
  indeterminate state, such as for the parent checkbox of nested
  checkboxes. This is not set with a property for the web component.
-->
    `,
};
