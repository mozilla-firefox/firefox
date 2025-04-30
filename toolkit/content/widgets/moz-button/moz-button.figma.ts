import figma, { html } from "@figma/code-connect/html"

const example = (props) => html`
<moz-button
  type=${props.type}
  disabled=${props.disabled}
  size=${props.size}
  iconsrc=${props.iconSrc}
>${props.label}</moz-button>
`;

// Desktop V3 (newest)
figma.connect(
  "https://www.figma.com/design/3WoKOSGtaSjhUHKldHCXbc/Desktop-v3?node-id=1-255&t=1NHTu3TvoKQWOCdP-1",
  {
    props: {
      iconSrc: figma.boolean("Show icon", {
        true: "chrome://example.svg",
        false: undefined,
      }),
      label: figma.string("Label"),
      type: figma.enum("Type", {
        Primary: "primary",
        Destructive: "destructive",
        Ghost: "ghost",
      }),
      disabled: figma.enum("State", {
        Disabled: true,
      }),
      size: figma.enum("Size", {
        Small: "small",
      }),
    },
    example,
  },
)

// Desktop Components text only (deprecated)
figma.connect(
  "https://www.figma.com/design/2ruSnPauajQGprFy6K333u/Desktop-Components?node-id=800-5099&m=dev",
  {
    props: {
      type: figma.enum("Type", {
        Primary: "primary",
        Destructive: "destructive",
      }),
      size: figma.enum("Size", {
        Small: "small",
      }),
      disabled: figma.boolean("Disabled"),
      label: "Label",
      // This is a horrible hack to share the template...
      iconSrc: figma.boolean("Disabled", {
        true: undefined,
        false: undefined,
      }),
    },
    example,
  },
)

// Desktop Components icon only (deprecated)
figma.connect(
  "https://www.figma.com/design/2ruSnPauajQGprFy6K333u/Desktop-Components?node-id=1103-14274&m=dev",
  {
    props: {
      disabled: figma.enum("State", {
        Disabled: true,
      }),
      size: figma.enum("Size", {
        Small: "small",
      }),
      type: figma.boolean("Background", {
        true: "icon",
        false: "ghost icon",
      }),
      iconSrc: "chrome://example.svg",
      // This is a horrible hack to share the template...
      label: figma.boolean("Background", {
        true: undefined,
        false: undefined,
      }),
    },
    example,
  },
)
