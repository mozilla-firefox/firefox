// url=https://www.figma.com/design/PqfaOcMGbX5liEXTTUzeYX/Nova-Components--Experimental-?node-id=4429-687
// Nova Components

import figma from "figma";

const vibrant = figma.selectedInstance.getBoolean("Vibrant");
const body = figma.selectedInstance.getString("Body");
const heading = figma.selectedInstance.getBoolean("Show heading", {
  true: figma.selectedInstance.getString("Heading"),
  false: undefined,
});
const image = figma.selectedInstance.getBoolean("Show image", {
  true: "path/to/image.png",
  false: undefined,
});
const imageAlignment = figma.selectedInstance.getBoolean("Show image", {
  true: figma.selectedInstance.getEnum("Image position", {
    Left: "start",
    Right: "end",
    Bottom: "center",
  }),
  false: undefined,
});
const actions = figma.selectedInstance.getBoolean("Show actions", {
  true: '\n          <moz-button slot="actions">Label</moz-button>\n          <a is="moz-support-link" slot="support-link" href="#"></a>\n        ',
  false: undefined,
});

export default {
  id: "promo",
  example: figma.code`
      <moz-promo
        ${vibrant ? 'type="vibrant"' : ""}
        ${heading ? `heading="${heading}"` : ""}
        ${body ? `message="${body}"` : ""}
        ${image ? `imageSrc="${image}"` : ""}
        ${imageAlignment ? `imageAlignment="${imageAlignment}"` : ""}
      >
        ${actions ?? ""}
      </moz-promo>
      <!--
  The following figma options are not currently supported by moz-promo:
  - Action position
  - Image size
  - Image display
-->
    `,
};
