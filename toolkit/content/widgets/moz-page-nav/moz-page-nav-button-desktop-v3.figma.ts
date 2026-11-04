// url=https://www.figma.com/design/3WoKOSGtaSjhUHKldHCXbc/Desktop-Components-3?node-id=474-5947
// Desktop v3 - Page nav button

import figma from "figma";
const label = figma.selectedInstance.getString("Label");
const iconSrc = figma.selectedInstance.getBoolean("Show icon", {
  true: "chrome://example.svg",
});
const rule = figma.selectedInstance.getBoolean("Show separator", {
  true: "<hr />",
});
const selected = figma.selectedInstance.getBoolean("Selected");

export default {
  id: "moz-page-nav-button-desktop-v3",
  example: figma.code`
      <moz-page-nav-button ${iconSrc ? `iconsrc="${iconSrc}"` : ""} ${selected ? "selected" : ""}>
        ${label}
      </moz-page-nav-button>
      ${rule}
    `,
};
