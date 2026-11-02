// url=https://www.figma.com/design/PqfaOcMGbX5liEXTTUzeYX/Nova-Components--Experimental-?node-id=33-302
// Nova Components

import figma from "figma";
const labelProps = (function () {
  const nestedLayer15 = figma.selectedInstance.findInstance("Label");
  return {
    description:
      nestedLayer15.type !== "ERROR"
        ? nestedLayer15.getBoolean("Show description", {
            true: nestedLayer15.getString("Description"),
          })
        : undefined,
    label:
      nestedLayer15.type !== "ERROR"
        ? nestedLayer15.getString("Label")
        : undefined,
    supportPage:
      nestedLayer15.type !== "ERROR"
        ? nestedLayer15.getBoolean("Show support link", {
            true: "sumo-slug",
          })
        : undefined,
    iconSrc:
      nestedLayer15.type !== "ERROR"
        ? nestedLayer15.getBoolean("Show icon", {
            true: "chrome://example.svg",
          })
        : undefined,
  };
})();
const textInputProps = (function () {
  const nestedLayer16 = figma.selectedInstance.findInstance("Text input");
  return {
    placeholder:
      nestedLayer16.type !== "ERROR"
        ? nestedLayer16.getBoolean("Show placeholder", {
            true: nestedLayer16.getString("Placeholder"),
          })
        : undefined,
    text:
      nestedLayer16.type !== "ERROR"
        ? nestedLayer16.getString("Text")
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
  id: "moz-input-text",
  example: figma.code`
      <moz-input-text
        ${textInputProps.text ? `value="${textInputProps.text}"` : ""}
        ${labelProps.label ? `label="${labelProps.label}"` : ""}
        ${labelProps.description ? `description="${labelProps.description}"` : ""}
        ${labelProps.supportPage ? `support-page="${labelProps.supportPage}"` : ""}
        ${labelProps.iconSrc ? `iconsrc="${labelProps.iconSrc}"` : ""}
        ${textInputProps.placeholder ? `placeholder="${textInputProps.placeholder}"` : ""}
        ${disabled ? "disabled" : ""}
      >
      </moz-input-text>
      ${errorMessage}
    `,
};
