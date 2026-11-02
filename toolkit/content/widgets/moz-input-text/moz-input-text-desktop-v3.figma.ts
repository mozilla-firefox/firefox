// url=https://www.figma.com/design/3WoKOSGtaSjhUHKldHCXbc/Desktop-Components-3?node-id=33-302
// Desktop v3 (newest)

import figma from "figma";
const labelProps = (function () {
  const nestedLayer17 = figma.selectedInstance.findInstance("Label");
  return {
    description:
      nestedLayer17.type !== "ERROR"
        ? nestedLayer17.getBoolean("Show description", {
            true: nestedLayer17.getString("Description"),
          })
        : undefined,
    label:
      nestedLayer17.type !== "ERROR"
        ? nestedLayer17.getString("Label")
        : undefined,
    supportPage:
      nestedLayer17.type !== "ERROR"
        ? nestedLayer17.getBoolean("Show support link", {
            true: "sumo-slug",
          })
        : undefined,
    iconSrc:
      nestedLayer17.type !== "ERROR"
        ? nestedLayer17.getBoolean("Show icon", {
            true: "chrome://example.svg",
          })
        : undefined,
  };
})();
const textInputProps = (function () {
  const nestedLayer18 = figma.selectedInstance.findInstance("Text input");
  return {
    placeholder:
      nestedLayer18.type !== "ERROR"
        ? nestedLayer18.getBoolean("Show placeholder", {
            true: nestedLayer18.getString("Placeholder"),
          })
        : undefined,
    text:
      nestedLayer18.type !== "ERROR"
        ? nestedLayer18.getString("Text")
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
  id: "moz-input-text-desktop-v3",
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
