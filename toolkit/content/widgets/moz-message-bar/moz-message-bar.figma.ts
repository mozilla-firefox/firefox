// url=https://www.figma.com/design/PqfaOcMGbX5liEXTTUzeYX/Nova-Components--Experimental-?node-id=243-1299
// Nova Components

import figma from "figma";
const heading = figma.selectedInstance.getBoolean("Show heading", {
  true: figma.selectedInstance.getString("Heading"),
  false: undefined,
});
const message = figma.selectedInstance.getBoolean("Show message", {
  true: figma.selectedInstance.getString("Message"),
  false: undefined,
});
const type = figma.selectedInstance.getEnum("Type", {
  Success: "success",
  Warning: "warning",
  Critical: "critical",
  Information: "information",
});
const supportPage = figma.selectedInstance.getBoolean("Show support link", {
  true: "sumo-slug",
});
const dismissable = figma.selectedInstance.getBoolean("Dismissible");
const action = figma.selectedInstance.getBoolean("Show action", {
  true: '<moz-button slot="actions" label="Label"></moz-button\n          ><moz-button slot="actions" label="Label"></moz-button>',
  false: undefined,
});
const buttonGroupProps = (function () {
  const nestedLayer13 = figma.selectedInstance.findInstance("Button group");
  return {
    additionalAction:
      nestedLayer13.type !== "ERROR"
        ? nestedLayer13.getBoolean("Show 3rd button", {
            true: '<moz-button slot="actions" label="Label"></moz-button>',
            false: undefined,
          })
        : undefined,
  };
})();

export default {
  id: "moz-message-bar",
  example: figma.code`<moz-message-bar
        ${type ? `type="${type}"` : ""}
        ${message ? `message="${message}"` : ""}
        ${heading ? `heading="${heading}"` : ""}
        ${supportPage ? `support-page="${supportPage}"` : ""}
        ${dismissable ? "dismissable" : ""}
        >${action ?? ""}${buttonGroupProps.additionalAction ?? ""}</moz-message-bar
      >`,
};
