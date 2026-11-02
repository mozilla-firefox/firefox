// url=https://www.figma.com/design/3WoKOSGtaSjhUHKldHCXbc/Desktop-Components-3?node-id=477-6023
// Desktop v3 - Card heading subcomponent

import figma from "figma";
const expanded = figma.selectedInstance.getBoolean("Expanded", {
  true: "This card is in the 'expanded' state.",
  false: undefined,
});

export default {
  id: "moz-card-heading-desktop-v3",
  example: figma.code`This subcomponent is created by passing a 'heading' property to the
      moz-card component. See the moz-card for detail. ${expanded ?? ""}`,
};
