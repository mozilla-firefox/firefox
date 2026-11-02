// url=https://www.figma.com/design/PqfaOcMGbX5liEXTTUzeYX/Nova-Components--Experimental-?node-id=479-6030
// Nova Components
// Code Connect cannot expose properties of nested components placed in slots;
// slotted markup in the snippet is fixed and Dev Mode will not update it when
// nested instance properties change in Figma.

import figma from "figma";

const boxType = figma.selectedInstance.getPropertyValue("Type");

let template;
if (boxType === "Slot") {
  const slotContent =
    figma.selectedInstance.getInstanceSwap("Slot")?.executeTemplate().example ??
    `<p>
          You can put whatever you want in here! Use your imagination! (Just
          don't pass other props as they won't work/get overridden by the slot)
        </p>`;

  template = {
    id: "moz-box-item",
    example: figma.code`<moz-box-item>${slotContent}</moz-box-item>`,
  };
} else {
  const labelInstance = figma.selectedInstance.findInstance("Label");
  const label =
    labelInstance.type !== "ERROR"
      ? labelInstance.getString("Label")
      : undefined;
  const iconSrc =
    labelInstance.type !== "ERROR"
      ? labelInstance.getBoolean("Show icon", {
          true: "chrome://example.svg",
        })
      : undefined;
  const description =
    labelInstance.type !== "ERROR"
      ? labelInstance.getBoolean("Show description", {
          true: labelInstance.getString("Description"),
        })
      : undefined;
  const layout = figma.selectedInstance.getEnum("Type", {
    "Large icon": "large-icon",
    "Medium icon": "medium-icon",
  });
  const startActions = figma.selectedInstance.getEnum("Type", {
    "Actions preset":
      '<moz-button\n          type="icon ghost"\n          iconsrc="chrome://example.svg"\n          aria-label="start action button"\n          slot="actions-start"\n        ></moz-button>',
    "Actions slots":
      '<moz-button\n          type="icon ghost"\n          iconsrc="chrome://example.svg"\n          aria-label="start action button"\n          slot="actions-start"\n        ></moz-button>',
  });
  const endActions = figma.selectedInstance.getEnum("Type", {
    "Actions preset":
      '<moz-button\n            type="icon ghost"\n            iconsrc="chrome://example.svg"\n            aria-label="end action button"\n            slot="actions"\n          ></moz-button\n          ><moz-toggle aria-label="end toggle" slot="actions"></moz-toggle>',
    "Actions slots":
      '<moz-button\n            type="icon ghost"\n            iconsrc="chrome://example.svg"\n            aria-label="end action button"\n            slot="actions"\n          ></moz-button\n          ><moz-toggle aria-label="end toggle" slot="actions"></moz-toggle>',
  });

  template = {
    id: "moz-box-item",
    example: figma.code`<moz-box-item
        ${label ? `label="${label}"` : ""}
        ${description ? `description="${description}"` : ""}
        ${iconSrc ? `iconsrc="${iconSrc}"` : ""}
        ${layout ? `layout="${layout}"` : ""}
      >
        ${startActions ?? ""}${endActions ?? ""}
      </moz-box-item>`,
  };
}

export default template;
