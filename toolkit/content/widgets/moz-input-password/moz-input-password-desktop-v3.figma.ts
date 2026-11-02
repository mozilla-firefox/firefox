// url=https://www.figma.com/design/3WoKOSGtaSjhUHKldHCXbc/Desktop-Components-3?node-id=2249-13164
// Desktop v3 (newest)

import figma from "figma";
const labelProps = (function () {
  const nestedLayer25 = figma.selectedInstance.findInstance("Label");
  return {
    description:
      nestedLayer25.type !== "ERROR"
        ? nestedLayer25.getBoolean("Show description", {
            true: nestedLayer25.getString("Description"),
          })
        : undefined,
    label:
      nestedLayer25.type !== "ERROR"
        ? nestedLayer25.getString("Label")
        : undefined,
    supportPage:
      nestedLayer25.type !== "ERROR"
        ? nestedLayer25.getBoolean("Show support link", {
            true: "sumo-slug",
          })
        : undefined,
    iconSrc:
      nestedLayer25.type !== "ERROR"
        ? nestedLayer25.getBoolean("Show icon", {
            true: "chrome://example.svg",
          })
        : undefined,
  };
})();
const passwordInputProps = (function () {
  const nestedLayer26 = figma.selectedInstance.findInstance("Password input");
  return {
    placeholder:
      nestedLayer26.type !== "ERROR"
        ? nestedLayer26.getBoolean("Show placeholder", {
            true: nestedLayer26.getString("Placeholder"),
          })
        : undefined,
    text:
      nestedLayer26.type !== "ERROR"
        ? nestedLayer26.getString("Text")
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
  id: "moz-input-password-desktop-v3",
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
