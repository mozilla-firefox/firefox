// url=https://www.figma.com/design/PqfaOcMGbX5liEXTTUzeYX/Nova-Components--Experimental-?node-id=2719-28745
// Nova Components

import figma from "figma";
const showLegend = figma.selectedInstance.getBoolean("Show legend", {
  true: figma.selectedInstance.getString("Label"),
  false: undefined,
});
const showDescription = figma.selectedInstance.getBoolean("Show description", {
  true: figma.selectedInstance.getString("Description"),
  false: undefined,
});
const showSupportLink = figma.selectedInstance.getBoolean("Show support link", {
  true: "support-page-endpoint-example",
  false: undefined,
});
const iconSrc = figma.selectedInstance.getBoolean("Show legend", {
  true: figma.selectedInstance.getBoolean("Show icon", {
    true: "chrome://example.svg",
    false: undefined,
  }),
  false: undefined,
});
const children = figma.properties.children(["*"]);

export default {
  id: "moz-fieldset",
  example: figma.code`
      <moz-fieldset
        ${showLegend ? `label="${showLegend}"` : ""}
        ${iconSrc ? `iconsrc="${iconSrc}"` : ""}
        ${showDescription ? `description="${showDescription}"` : ""}
        ${showSupportLink ? `support-page="${showSupportLink}"` : ""}
      >
        ${children}
      </moz-fieldset>
      <!--
  The moz-fieldset component supports setting the heading level
  ('1' to '6' corresponding to <h1> to <h6>) of the label and a
  disabled state.

  These properties are not currently reflected in the Figma
  component properties.

  The Figma component currently allows for the label icon to display
  without label text. This will be fixed. The Code Connect snippet will
  reflect the correct behavior.
-->
    `,
};
