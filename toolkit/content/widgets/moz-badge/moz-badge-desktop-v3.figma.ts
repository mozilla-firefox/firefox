// url=https://www.figma.com/design/3WoKOSGtaSjhUHKldHCXbc/Desktop-Components-3?node-id=3480-22604
// Desktop v3

import figma from "figma";
const showIcon = figma.selectedInstance.getBoolean("Show icon", {
  true: "chrome://example.svg",
  false: undefined,
});
const label = figma.selectedInstance.getString("Label");

export default {
  id: "moz-badge-desktop-v3",
  example: figma.code`
      <moz-badge
        ${label ? `label="${label}"` : ""}
        ${showIcon ? `iconsrc="${showIcon}"` : ""}
        title="This appears as a tooltip on hover"
      ></moz-badge>
      <!--
  The Figma component allows for a type ("Default" and "New") to be
  set, as well as a disabled state. These properties are not
  currently reflected in the web component.
-->
    `,
};
