// url=https://www.figma.com/design/PqfaOcMGbX5liEXTTUzeYX/Nova-Components--Experimental-?node-id=2249-12942
// Nova Components

import figma from "figma";
const labelProps = (function () {
  const nestedLayer19 = figma.selectedInstance.findInstance("Label");
  return {
    description:
      nestedLayer19.type !== "ERROR"
        ? nestedLayer19.getBoolean("Show description", {
            true: nestedLayer19.getString("Description"),
          })
        : undefined,
    label:
      nestedLayer19.type !== "ERROR"
        ? nestedLayer19.getString("Label")
        : undefined,
    supportPage:
      nestedLayer19.type !== "ERROR"
        ? nestedLayer19.getBoolean("Show support link", {
            true: "sumo-slug",
          })
        : undefined,
    iconSrc:
      nestedLayer19.type !== "ERROR"
        ? nestedLayer19.getBoolean("Show icon", {
            true: "chrome://example.svg",
          })
        : undefined,
  };
})();
const searchInputProps = (function () {
  const nestedLayer20 = figma.selectedInstance.findInstance("Search input");
  return {
    placeholder:
      nestedLayer20.type !== "ERROR"
        ? nestedLayer20.getBoolean("Show placeholder", {
            true: nestedLayer20.getString("Placeholder"),
          })
        : undefined,
    text:
      nestedLayer20.type !== "ERROR"
        ? nestedLayer20.getString("Text")
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
  id: "moz-input-search",
  example: figma.code`
      <moz-input-search
        ${labelProps.label ? `label="${labelProps.label}"` : ""}
        ${searchInputProps.text ? `value="${searchInputProps.text}"` : ""}
        ${labelProps.iconSrc ? `iconsrc="${labelProps.iconSrc}"` : ""}
        ${labelProps.description ? `description="${labelProps.description}"` : ""}
        ${labelProps.supportPage ? `support-page="${labelProps.supportPage}"` : ""}
        ${searchInputProps.placeholder ? `placeholder="${searchInputProps.placeholder}"` : ""}
        ${disabled ? "disabled" : ""}
      >
      </moz-input-search>
      ${errorMessage}
    `,
};
