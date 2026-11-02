import figma, { html } from "@figma/code-connect/html";

// Nova Components
figma.connect(
  "https://www.figma.com/design/PqfaOcMGbX5liEXTTUzeYX/Nova-Components--Experimental-?node-id=442-2086",
  {
    props: {
      label: figma.string("Label"),
      description: figma.boolean("Show description", {
        true: figma.string("Description"),
      }),
      supportPage: figma.boolean("Show support link", {
        true: "sumo-slug",
      }),
      iconSrc: figma.boolean("Show icon", {
        true: "chrome://example.svg",
      }),
      disabled: figma.boolean("Disabled"),
    },
    example: props => html`
      <label is="moz-label"> ${props.label} </label>
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
  }
);

// Desktop v3
figma.connect(
  "https://www.figma.com/design/3WoKOSGtaSjhUHKldHCXbc/Desktop-Components-3?node-id=442-2086",
  {
    props: {
      label: figma.string("Label"),
    },
    example: props => html`
      <label is="moz-label"> ${props.label} </label>
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
  }
);
