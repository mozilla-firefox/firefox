// url=https://www.figma.com/design/PqfaOcMGbX5liEXTTUzeYX/Nova-Components--Experimental-?node-id=474-5947
// Nova Components - Page nav button

import figma from "figma";
const label = figma.selectedInstance.getString("Label");
const iconSrc = figma.selectedInstance.getBoolean("Show icon", {
  true: "chrome://example.svg",
});
const selected = figma.selectedInstance.getBoolean("Selected");

export default {
  id: "moz-page-nav-button",
  example: figma.code`
      <moz-page-nav-button ${iconSrc ? `iconsrc="${iconSrc}"` : ""} ${selected ? "selected" : ""}>
        ${label}
      </moz-page-nav-button>
    `,
};
