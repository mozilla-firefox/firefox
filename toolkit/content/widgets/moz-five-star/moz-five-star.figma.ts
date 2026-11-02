// url=https://www.figma.com/design/PqfaOcMGbX5liEXTTUzeYX/Nova-Components--Experimental-?node-id=4213-3017
// Nova Components

import figma from "figma";
const rating = figma.selectedInstance.getString("Rating");

export default {
  id: "moz-five-star",
  example: figma.code`
      <moz-five-star ${rating ? `rating="${rating}"` : ""} selectable />
    `,
};
