// url=https://www.figma.com/design/3WoKOSGtaSjhUHKldHCXbc/Desktop-Components-3?node-id=474-6445
// Desktop v3 (newest)

import figma from "figma";
const heading = figma.selectedInstance.getString("Heading");
const items = figma.properties.children(["*"]);

export default {
  id: "moz-page-nav-desktop-v3",
  example: figma.code`
      <moz-page-nav ${heading ? `heading="${heading}"` : ""}> ${items} </moz-page-nav>
    `,
};
