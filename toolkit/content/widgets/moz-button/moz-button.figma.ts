import figma, { html } from "@figma/code-connect/html";

// Nova Components - Button
figma.connect(
  "https://www.figma.com/design/PqfaOcMGbX5liEXTTUzeYX/Nova-Components--Experimental-?node-id=8849-25406",
  {
    props: {
      label: figma.string("Label"),
      iconSrc: figma.boolean("Show icon", {
        true: "chrome://example.svg",
        false: undefined,
      }),
      iconPosition: figma.boolean("Show icon end", {
        true: "end",
        false: undefined,
      }),
      type: figma.enum("Type", {
        Default: undefined,
        Primary: "primary",
        Destructive: "destructive",
        Ghost: "ghost",
        Neutral: "neutral",
      }),
      disabled: figma.enum("State", {
        Disabled: true,
      }),
      size: figma.enum("Size", {
        Small: "small",
        Large: "large",
      }),
      attention: figma.boolean("Show attention dot"),
    },
    example: props => html`
      <moz-button
        type=${props.type}
        disabled=${props.disabled}
        size=${props.size}
        iconsrc=${props.iconSrc}
        iconposition=${props.iconPosition}
        attention=${props.attention}
        >${props.label}</moz-button
      >
    `,
  }
);

// Nova Components - Icon button
figma.connect(
  "https://www.figma.com/design/PqfaOcMGbX5liEXTTUzeYX/Nova-Components--Experimental-?node-id=9401-35716",
  {
    props: {
      type: figma.enum("Type", {
        Default: "icon",
        Ghost: "icon ghost",
        Neutral: "icon neutral",
        Primary: "icon primary",
      }),
      disabled: figma.enum("State", {
        Disabled: true,
      }),
      size: figma.enum("Size", {
        Small: "small",
        Large: "large",
      }),
      attention: figma.boolean("Show attention dot"),
    },
    example: props => html`
      <moz-button
        type=${props.type}
        disabled=${props.disabled}
        size=${props.size}
        iconsrc="chrome://example.svg"
        title="the hidden label"
        attention=${props.attention}
      ></moz-button>
    `,
  }
);

// Nova Components - Split button
figma.connect(
  "https://www.figma.com/design/PqfaOcMGbX5liEXTTUzeYX/Nova-Components--Experimental-?node-id=3368-23162",
  {
    props: {
      size: figma.enum("Size", {
        Small: "small",
        Large: "large",
      }),
    },
    example: props => html`
      <moz-button type="split" size=${props.size} menuid="panel-list">
        Button Label
      </moz-button>
      <panel-list id="panel-list">
        <panel-item>Option One</panel-item>
        <panel-item>Option Two</panel-item>
      </panel-list>
    `,
  }
);

// Nova Components - Toolbar button
figma.connect(
  "https://www.figma.com/design/PqfaOcMGbX5liEXTTUzeYX/Nova-Components--Experimental-?node-id=1-589",
  {
    props: {
      iconSrc: "chrome://example.svg",
      type: "ghost",
      attention: figma.boolean("Show attention dot"),
      disabled: figma.enum("State", {
        Disabled: true,
      }),
      size: figma.enum("Size", {
        Small: "small",
      }),
    },
    example: props => html`
      <moz-button
        type=${props.type}
        disabled=${props.disabled}
        size=${props.size}
        iconsrc=${props.iconSrc}
        title="the hidden label"
        attention=${props.attention}
      ></moz-button>
    `,
  }
);

// Desktop V3 (newest)
figma.connect(
  "https://www.figma.com/design/3WoKOSGtaSjhUHKldHCXbc/Desktop-Components-3?node-id=1-255",
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
    example: props => html`
      <moz-button
        type=${props.type}
        disabled=${props.disabled}
        size=${props.size}
        iconsrc=${props.iconSrc}
        >${props.label}</moz-button
      >
    `,
  }
);

// Icon Button
figma.connect(
  "https://www.figma.com/design/3WoKOSGtaSjhUHKldHCXbc/Desktop-Components-3?node-id=1-589",
  {
    props: {
      iconSrc: "chrome://example.svg",
      type: figma.boolean("Ghost", {
        true: "icon ghost",
        false: "icon",
      }),
      attention: figma.boolean("Show attention dot"),
      disabled: figma.enum("State", {
        Disabled: true,
      }),
      size: figma.enum("Size", {
        Small: "small",
      }),
    },
    example: props => html`
      <moz-button
        type=${props.type}
        disabled=${props.disabled}
        size=${props.size}
        iconsrc=${props.iconSrc}
        title="the hidden label"
        attention=${props.attention}
      ></moz-button>
    `,
  }
);
