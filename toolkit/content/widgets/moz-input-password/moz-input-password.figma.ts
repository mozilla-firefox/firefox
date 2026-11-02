// url=https://www.figma.com/design/PqfaOcMGbX5liEXTTUzeYX/Nova-Components--Experimental-?node-id=2249-13164
// Nova Components

import figma from "figma";
const labelProps = (function () {
  const nestedLayer23 = figma.selectedInstance.findInstance("Label");
  return {
    description:
      nestedLayer23.type !== "ERROR"
        ? nestedLayer23.getBoolean("Show description", {
            true: nestedLayer23.getString("Description"),
          })
        : undefined,
    label:
      nestedLayer23.type !== "ERROR"
        ? nestedLayer23.getString("Label")
        : undefined,
    supportPage:
      nestedLayer23.type !== "ERROR"
        ? nestedLayer23.getBoolean("Show support link", {
            true: "sumo-slug",
          })
        : undefined,
    iconSrc:
      nestedLayer23.type !== "ERROR"
        ? nestedLayer23.getBoolean("Show icon", {
            true: "chrome://example.svg",
          })
        : undefined,
  };
})();
const passwordInputProps = (function () {
  const nestedLayer24 = figma.selectedInstance.findInstance("Password input");
  return {
    placeholder:
      nestedLayer24.type !== "ERROR"
        ? nestedLayer24.getBoolean("Show placeholder", {
            true: nestedLayer24.getString("Placeholder"),
          })
        : undefined,
    text:
      nestedLayer24.type !== "ERROR"
        ? nestedLayer24.getString("Text")
        : undefined,
  };
})();
const errorMessage = figma.selectedInstance.getEnum("State", {
  Error:
    " <!--\n  The error message functionality is not currently\n  available with design system components,\n  and will need to be implemented in context for now.\n-->",
});
const disabled = figma.selectedInstance.getEnum("State", {
  Disabled: true,
});

export default {
  id: "moz-input-password",
  example: figma.code`
      <moz-input-password
        ${labelProps.label ? `label="${labelProps.label}"` : ""}
        ${passwordInputProps.text ? `value="${passwordInputProps.text}"` : ""}
        ${labelProps.iconSrc ? `iconsrc="${labelProps.iconSrc}"` : ""}
        ${disabled ? "disabled" : ""}
        ${labelProps.description ? `description="${labelProps.description}"` : ""}
        ${labelProps.supportPage ? `support-page="${labelProps.supportPage}"` : ""}
        ${passwordInputProps.placeholder ? `placeholder="${passwordInputProps.placeholder}"` : ""}
      >
      </moz-input-password>
      ${errorMessage}
    `,
};
