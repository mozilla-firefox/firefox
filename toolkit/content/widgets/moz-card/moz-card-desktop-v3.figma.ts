// url=https://www.figma.com/design/3WoKOSGtaSjhUHKldHCXbc/Desktop-Components-3?node-id=538-18711
// Desktop v3 (newest)

import figma from "figma";

const type = figma.selectedInstance.getEnum("Type", {
  Empty: undefined,
  Slot: undefined,
  Accordion: "accordion",
});
const heading = figma.selectedInstance.getBoolean("Show heading", {
  true: figma.selectedInstance.getString("Heading"),
  false: undefined,
});
const slot = figma.selectedInstance
  .getInstanceSwap("Slot")
  ?.executeTemplate().example;
const expanded = figma.selectedInstance.getBoolean("Expanded");
const iconSrc = figma.selectedInstance.getBoolean("Show icon", {
  true: "chrome://example.svg",
  false: undefined,
});
const spacing = figma.selectedInstance.getEnum("Spacing", {
  Default: undefined,
  Compact: "compact",
});

export default {
  id: "moz-card-desktop-v3",
  example: figma.code`<moz-card
        ${type ? `type="${type}"` : ""}
        ${spacing ? `spacing="${spacing}"` : ""}
        ${expanded ? "expanded" : ""}
        ${heading ? `heading="${heading}"` : ""}
        ${iconSrc ? `iconsrc="${iconSrc}"` : ""}
      >
        ${slot ?? ""}
      </moz-card>
      <!--
  Code Connect does not reflect the option to add an image on the card.
-->`,
};
