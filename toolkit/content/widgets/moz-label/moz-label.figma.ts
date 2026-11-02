// url=https://www.figma.com/design/PqfaOcMGbX5liEXTTUzeYX/Nova-Components--Experimental-?node-id=442-2086
// Nova Components

import figma from "figma";
const label = figma.selectedInstance.getString("Label");
const description = figma.selectedInstance.getBoolean("Show description", {
  true: figma.selectedInstance.getString("Description"),
});
const supportPage = figma.selectedInstance.getBoolean("Show support link", {
  true: "sumo-slug",
});
const iconSrc = figma.selectedInstance.getBoolean("Show icon", {
  true: "chrome://example.svg",
});
const disabled = figma.selectedInstance.getBoolean("Disabled");

export default {
  id: "moz-label",
  example: figma.code`
      <label is="moz-label"> ${label} </label>
      <!--
In Figma, the Label component can be configured with description,
support-page, iconsrc, and disabled properties. These are not
attributes of the <label is="moz-label"> element. They are
attributes on components that render a label internally, such as
moz-checkbox, moz-radio, moz-toggle, and moz-fieldset.

The moz-label also accepts an accesskey attribute, and should include
the for attribute corresponding to the id of a corresponding control.
There is a full example below.

<label is="moz-label" accesskey="r" for="radio">
  This is the label
</label>
<input id="radio" type="radio" id="radio"/>
-->
    `,
};
