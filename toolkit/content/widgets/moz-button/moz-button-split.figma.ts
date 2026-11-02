// url=https://www.figma.com/design/PqfaOcMGbX5liEXTTUzeYX/Nova-Components--Experimental-?node-id=3368-23162
// Nova Components - Split button

import figma from "figma";

const buttonInstance = figma.selectedInstance.findInstance("Button");
const label =
  buttonInstance.type !== "ERROR"
    ? buttonInstance.getString("Label")
    : undefined;
const size = figma.selectedInstance.getEnum("Size", {
  Default: "default",
  Small: "small",
  Large: "large",
});
const disabled =
  buttonInstance.type !== "ERROR"
    ? buttonInstance.getEnum("State", {
        Disabled: true,
      })
    : undefined;

export default {
  id: "moz-button-split",
  example: figma.code`
    <moz-button
      type="split"
      ${size ? `size="${size}"` : ""}
      ${disabled ? "disabled" : ""}
      ${label ? `label="${label}"` : ""}
      menuid="panel-list"
    ></moz-button>
    <panel-list id="panel-list">
      <panel-item>Option One</panel-item>
      <panel-item>Option Two</panel-item>
    </panel-list>
  `,
};
