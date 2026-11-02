// url=https://www.figma.com/design/3WoKOSGtaSjhUHKldHCXbc/Desktop-Components-3?node-id=4213-3017
// Desktop V3

import figma from "figma";
const rating = figma.selectedInstance.getString("Rating");

export default {
  id: "moz-five-star-desktop-v3",
  example: figma.code`
      <moz-five-star ${rating ? `rating="${rating}"` : ""} selectable />
    `,
};
