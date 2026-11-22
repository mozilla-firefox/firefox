# HCM CSS Self-Check Guide

Use these checks when writing or reviewing CSS that targets high contrast mode (HCM) or Increase Contrast (IC) users.

For background on the media queries and colour systems referenced here, see the documentation on [HCM Media Queries](HCMMediaQueries.md) and [Colors and High Contrast Mode](ColorsAndHighContrastMode.md). You should read and understand these documents before moving forward with the checks below.

:::{note}
If you work with an AI agent, the in-tree {doc}`desktop-theme </ai-agent-tools/skills/desktop-theme/SKILL>` skill applies these checks when it writes or reviews CSS for high contrast mode, and the {doc}`accessibility-frontend-review </ai-agent-tools/skills/accessibility-frontend-review/SKILL>` skill applies them when it reviews a patch for accessibility.
:::

Note: These checks are ordered intentionally, please complete them in order.

---

## Architecture

### Check 1: Have I identified all places where HCM overrides are necessary?

**When writing HCM-specific CSS, our goal is to write as little CSS as possible.** Our design system is a huge help here. The design system token layer system (`toolkit/themes/shared/design-system/dist/tokens-shared.css`) includes three cascade layers — `tokens-foundation`, `tokens-prefers-contrast`, and `tokens-forced-colors` — that automatically override token values in contrast modes. Any variable whose value is (or cascades to) a design system token gets HCM adaptation for free.

In addition to the `.css` files above, you can view the [design system tokens table here](https://firefoxux.github.io/firefox-desktop-components/?path=/story/docs-tokens-table--default). Make sure to select "Chrome" or "Content" as is appropriate for your work.

**Variables that use design system tokens generally do NOT need explicit `forced-colors` overrides.** For example:

```css
/* These adapt automatically in HCM — no override needed */
color: var(--button-text-color);               /* → ButtonText in forced-colors */
background: var(--button-background-color);    /* → ButtonFace in forced-colors */
border-color: var(--button-border-color);      /* → ButtonText in forced-colors */
```

**Variables with raw custom values DO need explicit `forced-colors` overrides.** These are those holding hardcoded hex colors, `light-dark()` calls, `rgba()` values, gradients, or references to brand palette tokens (like `--color-violet-80`) which do not resolve to system colors.

```css
/* These do NOT adapt — explicit forced-colors overrides required */
--my-bg: light-dark(var(--color-gray-0), var(--color-gray-80));
--my-accent: var(--color-violet-90);
--my-gradient: linear-gradient(83deg, #5119dc 0.73%, var(--color-violet-30) 98.05%);
--my-hover-bg: rgba(191, 143, 204, 0.2);
```

**Audit step:** Scan all CSS custom properties in your `:root` / `:host` block. For each one, ask: does this value ultimately resolve to a [CSS system color keyword](https://www.w3.org/TR/css-color-4/#css-system-colors) in `tokens-forced-colors`? If yes, verify the token is appropriate (other checks below still apply). If no, it needs an override. Note that only the keywords listed in the spec are valid system colors — values like `inherit`, named colors like `teal`, or `transparent` are not CSS system colors and will not adapt correctly in HCM. If your component uses no design system tokens at all, pay particularly close attention to ensuring every color property has an explicit override.

---

### Check 2: Am I using the right media query for the right audience?

`forced-colors` and `prefers-contrast` are not interchangeable. You can read more about their differences, with examples, in our [HCM Media Query documentation](HCMMediaQueries.md).

| Query | Fires for |
|---|---|
| `forced-colors: active` | Windows HCM, Firefox HCM |
| `prefers-contrast: more` | macOS Increase Contrast **and** most `forced-colors` users (contrast ratio > 7:1) |

**It is important to note:**: most HCM users also trigger `prefers-contrast`. A bare `@media (prefers-contrast)` block applies to *both* IC and HCM users. If that is not your intent, you must use a more specific media query.

macOS "Increase Contrast" and Linux HCM alone do **not** trigger `forced-colors`.

When a component needs distinct treatment for IC and HCM users, the recommended split is:

```scss
:root {
  @media (prefers-contrast) {
    /* Applies to ALL contrast users (IC + HCM).
       Safe to use here: borders, border-width increases.
       Do NOT use CSS system color keywords here — IC users keep the
       default Firefox color scheme. */
  }

  @media (prefers-contrast) and (not (forced-colors)) {
    /* IC users only.
       Keep original colors. You may optionally improve a foreground
       color for readability here, but only using non-system colors. */
  }

  @media (forced-colors) {
    /* HCM users only.
       Design system tokens that resolve to CSS system colors are
       required here. Do not use system color keywords directly. */
  }
}
```

A rule that belongs in both IC and HCM (e.g. removing child outlines from a button with both an icon and a label so only the button wrapper shows a focus ring) should use bare `@media (prefers-contrast)` without the `not (forced-colors)` exclusion.

**IC-specific color improvements:** IC users on macOS keep the default Firefox color scheme and should not receive a reduced-palette experience. If a foreground color modification on a control could meaningfully improve contrast for IC users, it can go in the `prefers-contrast and (not (forced-colors))` block — but only using non-system color tokens. There are currently no design system tokens specifically for IC color improvements beyond borders.

If you are uncertain which media query applies to your use case, reach out to the accessibility team in #Accessibility for guidance.

---

### Check 3: Am I overriding tokens in the correct places?

Two main rules govern how HCM overrides should be written:

**Nest media queries inside `:root` / `:host`, not on specific elements.** Do not create a separate top-level `@media` rule wrapping a new `:root` declaration. Nesting inside the existing block keeps all variable definitions for a component in one place and makes the full cascade visible at a glance.

```scss
/* WRONG — separate top-level @media rule instead of nesting inside :root */
:root {
  --my-wrong-text-color: rgb(123, 123, 123);
}

@media (forced-colors) {
  :root {
    --my-wrong-text-color: var(--text-color-design-system-token);
  }
}

/* WRONG — @media nested at element level instead of in :root */
.myWrongClass {
  color: var(--my-wrong-text-color);
  @media (forced-colors) {
    color: var(--text-color-design-system-token);
  }
}

/* CORRECT */
:root {
  --my-bg: #ffffff;
  --my-text-color: rgb(123, 123, 123);

  @media (forced-colors) {
    --my-bg: var(--background-design-system-token);
    --my-text-color: var(--text-color-design-system-token);
  }
}

.myClass {
  background-color: var(--my-bg);
  color: var(--my-text-color);
}
```

**Override the existing variable; do not introduce new HCM-specific ones.** Do not create `-hcm-`-suffixed variables or other HCM-specific names. Overriding the existing variable means the element rule can stay simple and clean.

```scss
/* WRONG — introduces an HCM-specific variable; --my-btn-text-color still resolves to the
   light-dark() value in forced-colors, so the element must also carry its own override */
:root {
  --my-wrong-btn-text-color: light-dark(var(--color-violet-80), var(--color-white));

  @media (forced-colors) {
    --my-wrong-btn-hcm-text-color: var(--button-text-color);
  }
}
.myWrongBtn {
  color: var(--my-wrong-btn-text-color); /* still unresolved in forced-colors */

  @media (forced-colors) {
    color: var(--my-wrong-btn-HCM-text-color); /* element-level override now required */
  }
}

/* CORRECT — element-level CSS is unchanged; HCM is handled entirely at :root */
:root {
  --my-btn-text-color: light-dark(var(--color-violet-80), var(--color-white));

  @media (forced-colors) {
    --my-btn-text-color: var(--button-text-color);
  }
}
.myBtn {
  color: var(--my-btn-text-color);
}
```

---

## Token Selection

These checks apply when you're writing `forced-colors` overrides. They ensure you pick tokens that are correct, consistent, and safe across all HCM themes.

---

### Check 4: Are my foreground/background pairs correct, and am I mapping them semantically?

When choosing tokens for an element in HCM, identify the semantic role of the element (interactive control, selected item, static page content, link text, disabled content), find the appropriate token family in the [tokens table](https://firefoxux.github.io/firefox-desktop-components/?path=/story/docs-tokens-table--default), then use that family consistently across background, text, and border. Do not mix component-specific tokens with generic ones, or tokens from unrelated components.

```css
/* WRONG — mixing component families */
--my-wrong-bg: var(--button-background-color-primary);
--my-wrong-text: var(--text-color); /* generic, not button family */
--my-wrong-border var(--border-color-interactive); /* evaluates to a button color, but a better match exists */

/* WRONG — mixing component family types (button general, primary button, ghost button) */
--my-wrong-bg: var(--button-background-color);
--my-wrong-text: var(--button-text-color-primary);
--my-wrong-border: var(--button-border-color-ghost);

/* CORRECT — same family throughout */
--my-bg: var(--button-background-color-primary);
--my-text: var(--button-text-color-primary);
--my-border: var(--button-border-color-primary);
```

**Token families inside our design system tokens set have already been audited for HCM compliance**. By sticking with one family, you're guaranteed to avoid pair mismatches.

#### What is a pair mismatch?

CSS system colors come in guaranteed-contrast pairs. Contrast is only guaranteed *within* a pair. Mixing a foreground from one pair with a background from another removes that guarantee and can produce unreadable combinations on some themes.

Do not use CSS system color keywords directly. Use design system tokens — they resolve to the correct system colors through the `tokens-forced-colors` layer. Consult the [Firefox design system tokens table](https://firefoxux.github.io/firefox-desktop-components/?path=/story/docs-tokens-table--default) to identify which tokens are available and what they resolve to in forced-colors mode.

---

### Check 5: Are my tokens semantically appropriate for the property they set?

A token named `background-color` should only be used to set backgrounds. A token named `text-color` should only be used for text (foreground). Do not use a background token to set a text color variable, even if it happens to resolve to the right system color in a specific theme.

```css
/* WRONG — background token used to set text color */
--my-wrong-btn-text: var(--button-background-color-primary);
/* In HCM this resolves to ButtonText via --color-accent-primary,
   but the token name says "background" and the usage says "text" */

/* CORRECT */
--my-btn-text: var(--button-text-color-primary);
/* This also resolves to ButtonText, and the token name is
   appropriate and consistent */
```

Beyond property type, also verify that the token's component matches your use case. When multiple tokens resolve to the same CSS system color, they are not interchangeable — prefer the one whose name most accurately describes the element being styled. A token that resolves correctly but names the wrong component is still a semantic mismatch, and makes the code harder to audit in future.

```css
/* WRONG — despite being a background token, this token describes a list item;
   it is not appropriate for a button background */
--my-wrong-button-background: var(--background-color-list-item);

/* CORRECT */
--my-button-background: var(--button-background-color);
```

If no token exists whose name is a good semantic match for your use case, that is worth noting — it may indicate the design system doesn't yet cover your component. Please reach out to the accessibility team directly in #Accessibility for guidance.

---

### Check 6: Are my tokens from matching interaction states?

Background, text, and border tokens for a given state (hover, active, focus, selected, current, etc.) must all carry the same state suffix. Mixing states produces pairs that may be unreadable on some HCM themes — hover backgrounds and default-state text colors can resolve to the same system color, making content invisible.

```css
/* WRONG — hover-state background paired with default-state text */
--my-btn-bg: var(--button-background-color-hover); /* hover state */
--my-wrong-btn-text: var(--button-text-color); /* default state ← mismatch */

/* CORRECT — both hover state */
--my-btn-bg: var(--button-background-color-hover);
--my-btn-text: var(--button-text-color-hover);
```

---

### Check 7: Does any information rely on color alone to communicate meaning?

HCM reduces the available palette to a small set of system colors. If any information in the component is conveyed by color alone, it will be lost in HCM. This is also a violation of our general a11y guidelines and [WCAG](https://www.w3.org/TR/WCAG22/#use-of-color). Check that:

- **Selected states remain visually distinct** from unselected states, even when both use system colors. A selected item should use the selection token pair rather than only differing from its neighbor by hue or opacity.
- **Disabled/inactive states remain distinct** from enabled states. Use disabled tokens (ex. `--button-text-color-disabled`) for disabled foreground rather than relying on reduced opacity.
- **Status or severity** (e.g. error, warning, success) is not indicated only by color. If an icon or label is the only indicator, ensure it is present and its text/icon uses an appropriate foreground token.

```scss
/* WRONG — selected state differs from default only by color,
   which may become indistinguishable across HCM themes */
:root {
  @media (forced-colors) {
    --my-wrong-item-text: var(--button-text-color);
    --my-wrong-item-bg: var(--button-background-color);
    --my-wrong-item-bg-selected: var(--button-background-color); /* <- no distinction */
    /* no selected foreground color specified */
  }
}

/* CORRECT — selected state uses the dedicated selection token pair */
:root {
  @media (forced-colors) {
    --my-item-text: var(--button-text-color);
    --my-item-bg: var(--button-background-color);
    --my-item-bg-selected: var(--color-accent-primary-selected);
    --my-item-text-selected: var(--text-color-accent-primary-selected);
  }
}
```

---

### Check 8: Have I verified that my chosen tokens actually have `forced-colors` variants?

Not every design system token has an entry in the `tokens-forced-colors` layer. A token without a forced-colors override resolves to its foundation or `tokens-prefers-contrast` value in HCM, which may be a non-system color.

Before using a token in a `forced-colors` block, look it up in the [Firefox design system tokens table](https://firefoxux.github.io/firefox-desktop-components/?path=/story/docs-tokens-table--default). Ensure your chosen token has an entry in the High Contrast Mode column. If it does not, reach out to the accessibility team in #Accessibility for guidance.

In addition, you should build and test your patch on Windows when possible.
Testing chrome patches in Firefox using FF HCM will not produce the same results as testing on Windows with Windows HCM enabled. Please read about the differences between FF HCM and Windows HCM in the [Colors and High Contrast Mode in Firefox](ColorsAndHighContrastMode.md) documentation.

---

### Check 9: Do ghost components have appropriate tokens set in HCM?

The design system's `--button-*-ghost-*` tokens already have `forced-colors` overrides that add a visible border in the default state (ghost buttons are borderless by default) and override hover and active states. If a component uses ghost styling in non-HCM, do not override the ghost tokens to non-ghost values in the `forced-colors` block — that creates a state mismatch between modes. Let the ghost tokens cascade naturally through the design system. If you've chosen custom color values for your ghost component, override those styles with ghost tokens in HCM.

```{code-block} scss
:force:

/* WRONG — non-HCM uses ghost state, forced-colors switches to default state */
:host {
  --my-wrong-ghost-btn-bg: var(--button-background-color-ghost);

  @media (forced-colors) {
    --my-wrong-ghost-btn-bg: var(--button-background-color); /* <- this will remove the ghost styling, causing a state mismatch */
  }
}

/* CORRECT */
:host {
  --my-ghost-btn-bg: var(--button-background-color-ghost);
}

/* ALSO CORRECT */
:host {
  --my-ghost-btn-bg: color-mix(in srgb, --button-background-color, #fff, 20%);

  @media (forced-colors) {
    --my-ghost-btn-bg: var(--button-background-color-ghost);
  }
}
```

---

## Elements and Features

These checks address specific UI elements and CSS features that require particular handling in contrast modes.

---

### Check 10: Am I adding borders to all elements that require them, for all contrast users?

Controls, sidebars, toolbars, dialogs, cards, and sections require a solid border of at least 1px in contrast modes. Borders provide visual separation where color alone is insufficient, and are important for both IC and HCM users. Ensure adequate padding around the border so it does not reduce legibility of the content inside.

In HCM, border tokens are supplied by our design system for most token family types and states. In Increase Contrast (IC), border color should match the component's foreground color (often with `currentColor`).

If the same border is appropriate for both IC and HCM, set it in a bare `@media (prefers-contrast)` block. The `forced-colors` block's value will override it for HCM users due to source order:

```scss
:root {
  --my-item-border-width: 0px;
  --my-item-border-color: transparent;

  @media (prefers-contrast) {
    /* 1px solid border using the component's foreground color, for all contrast users */
    --my-item-border-width: var(--border-width);
    --my-item-border-color: currentColor;
  }

  @media (forced-colors) {
    /* Override to use the token matching the component family */
    /* Keep the border width token above, as it is appropriate for both modes */
    --my-item-border-color: var(--button-border-color);
  }
}

.my-element {
  border: var(--my-item-border-width) solid var(--my-item-border-color);
  padding: var(--space-small);
}
```

---

### Check 11: Have I removed or replaced visual effects that don't belong in HCM?

Blur effects, drop shadows, semi-opaque surfaces, and gradient backgrounds must be removed or replaced with solid equivalents in HCM. These effects rely on color blending that is meaningless or visually broken against a forced system-color palette.

| Effect | HCM replacement |
|---|---|
| Drop shadow | 1px solid border using the component's foreground color token |
| Gradient background | Solid background using the appropriate `background-color` token |
| Semi-opaque surface | Solid background (`var(--background-color-box)`) with `opacity:1;`|
| Blur | Remove entirely |

```scss
:root {
  --my-shadow: drop-shadow(-16px -16px red);
  --my-gradient: linear-gradient(135deg, var(--color-violet-50), var(--color-blue-50));
  --my-overlay-bg: rgba(0, 0, 0, 0.5);
  --my-border-width: 0px;
  --my-custom-opacity: 0.1;

  @media (forced-colors) {
    --my-shadow: none;
    --my-gradient: none;
    --my-overlay-bg: var(--background-color-canvas);
    --my-border-width: var(--border-width); /* border replaces shadow */
    --my-custom-opacity: 1;
  }
}
```

---

### Check 12: Have I handled SVG colors correctly for HCM?

SVGs fall into two categories with different HCM requirements:

**Decorative SVGs**, or SVGs that duplicate information already available in an accessible format (e.g. a triangle icon next to a text label starting with "Warning"). These SVGs may use colors outside the HCM palette. No override is needed.

**Informational or interactive SVGs** — those that uniquely communicate information or function as controls — must use only HCM palette colors. An example of an SVG in this category is the down-chevron on a custom accordian element.
Generally, button tokens are appropriate when these items are interactive and traditional background/text tokens are appropriate when they are static. These components should also gain borders in HCM.

```scss
/* Interactive icon SVG — must adapt in HCM */
:root {
  --my-chevron-fill: var(--color-violet-60);
  --my-chevron-stroke: var(--color-violet-80);

  @media (forced-colors) {
    --my-chevron-fill: var(--button-background-color);
    --my-chevron-stroke: var(--button-border-color);
  }
}
```
