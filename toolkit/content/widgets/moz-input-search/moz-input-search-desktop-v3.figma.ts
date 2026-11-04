// url=https://www.figma.com/design/3WoKOSGtaSjhUHKldHCXbc/Desktop-Components-3?node-id=2249-12942
// Desktop v3 (newest)

import figma from "figma";
const labelProps = (function () {
  const nestedLayer21 = figma.selectedInstance.findInstance("Label");
  return {
    description:
      nestedLayer21.type !== "ERROR"
        ? nestedLayer21.getBoolean("Show description", {
            true: nestedLayer21.getString("Description"),
          })
        : undefined,
    label:
      nestedLayer21.type !== "ERROR"
        ? nestedLayer21.getString("Label")
        : undefined,
    supportPage:
      nestedLayer21.type !== "ERROR"
        ? nestedLayer21.getBoolean("Show support link", {
            true: "sumo-slug",
          })
        : undefined,
    iconSrc:
      nestedLayer21.type !== "ERROR"
        ? nestedLayer21.getBoolean("Show icon", {
            true: "chrome://example.svg",
          })
        : undefined,
  };
})();
const searchInputProps = (function () {
  const nestedLayer22 = figma.selectedInstance.findInstance("Search input");
  return {
    placeholder:
      nestedLayer22.type !== "ERROR"
        ? nestedLayer22.getBoolean("Show placeholder", {
            true: nestedLayer22.getString("Placeholder"),
          })
        : undefined,
    text:
      nestedLayer22.type !== "ERROR"
        ? nestedLayer22.getString("Text")
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
  id: "moz-input-search-desktop-v3",
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
