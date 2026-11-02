// url=https://www.figma.com/design/PqfaOcMGbX5liEXTTUzeYX/Nova-Components--Experimental-?node-id=538-18711
// Nova Components

import figma from "figma";
// Branch per variant combination.

let template;
if (figma.selectedInstance.getPropertyValue("Type") === "Slot") {
  template = {
    id: "moz-card",
    example: figma.code`<moz-card>
        <p>Card content goes here.</p>
      </moz-card>`,
  };
} else {
  const type = figma.selectedInstance.getEnum("Type", {
    Default: undefined,
    Slot: undefined,
    Accordion: "accordion",
  });
  const heading = figma.selectedInstance.getString("Heading");
  const slot = figma.selectedInstance
    .getInstanceSwap("Slot")
    ?.executeTemplate().example;
  const expanded = figma.selectedInstance.getBoolean("Expanded");
  const iconSrc = figma.selectedInstance.getBoolean("Show icon", {
    true: "chrome://example.svg",
    false: undefined,
  });
  const spacing = figma.selectedInstance.getBoolean("Compact", {
    true: "compact",
    false: undefined,
  });

  template = {
    id: "moz-card",
    example: figma.code`<moz-card
        ${type ? `type="${type}"` : ""}
        ${spacing ? `spacing="${spacing}"` : ""}
        ${expanded ? "expanded" : ""}
        ${heading ? `heading="${heading}"` : ""}
        ${iconSrc ? `iconsrc="${iconSrc}"` : ""}
      >
        ${slot}
      </moz-card>`,
  };
}

export default template;
