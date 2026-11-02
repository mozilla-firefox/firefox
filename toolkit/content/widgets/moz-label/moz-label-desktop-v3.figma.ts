// url=https://www.figma.com/design/3WoKOSGtaSjhUHKldHCXbc/Desktop-Components-3?node-id=442-2086
// Desktop v3

import figma from "figma";
const label = figma.selectedInstance.getString("Label");

export default {
  id: "moz-label-desktop-v3",
  example: figma.code`
      <label is="moz-label"> ${label} </label>
      <!--
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
