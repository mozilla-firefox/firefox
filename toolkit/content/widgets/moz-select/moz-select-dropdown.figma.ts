// url=https://www.figma.com/design/PqfaOcMGbX5liEXTTUzeYX/Nova-Components--Experimental-?node-id=5858-8185
// Dropdown

import figma from "figma";

const labelInstance = figma.selectedInstance.findInstance("Label");
const labelText =
  labelInstance.type !== "ERROR" ? labelInstance.getString("Label") : undefined;
const description =
  labelInstance.type !== "ERROR"
    ? labelInstance.getString("Description")
    : undefined;
const supportPage =
  labelInstance.type !== "ERROR"
    ? labelInstance.getBoolean("Show support link", {
        true: "support-page-endpoint-example",
        false: undefined,
      })
    : undefined;
const iconSrc =
  labelInstance.type !== "ERROR"
    ? labelInstance.getBoolean("Show icon", {
        true: "chrome://example.svg",
        false: undefined,
      })
    : undefined;

const selectInstance = figma.selectedInstance.findInstance("Select / Input");
const value =
  selectInstance.type !== "ERROR"
    ? selectInstance.getString("Label")
    : undefined;
const disabled = figma.selectedInstance.getBoolean("Disabled");

export default {
  id: "moz-select-dropdown",
  example: figma.code`
      <moz-select
        ${labelText ? `label="${labelText}"` : ""}
        name="example-moz-select"
        ${value ? `value="${value}"` : ""}
        ${disabled ? "disabled" : ""}
        ${iconSrc ? `iconsrc="${iconSrc}"` : ""}
        ${description ? `description="${description}"` : ""}
        ${supportPage ? `support-page="${supportPage}"` : ""}
      >
        <moz-option value="option1">Option 1</moz-option>
        <moz-option value="option2">Option 2</moz-option>
      </moz-select>
    `,
};
