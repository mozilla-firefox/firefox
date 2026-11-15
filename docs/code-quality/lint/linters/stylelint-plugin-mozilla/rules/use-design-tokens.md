# use-design-tokens

This rule checks that CSS declarations use design token variables instead of
hardcoded values. This ensures consistent design system usage across the
application and makes it easier to maintain design system consistency.

## Configuration

The rule accepts `true`, which checks every supported property, or `null`,
which disables it:

```javascript
"stylelint-plugin-mozilla/use-design-tokens": true
```

## Token categories

Each property maps to one or more property types in the plugin's
{searchfox}`config.mjs <tools/lint/stylelint/stylelint-plugin-mozilla/config.mjs>`, and each type
lists the token categories it accepts. A token in one of the type's
`tokenTypes` categories, or named in its `allowedTokens`, can be used
directly. A token in one of its `aliasTokenTypes` categories passes only
through a custom property declared in the same stylesheet.

```css
/* Incorrect: --text-color is a text-color token, which fill doesn't accept directly */
.icon {
  fill: var(--text-color);
}

/* Correct: --icon-color is an icon-color token */
.icon {
  fill: var(--icon-color);
}

/* Correct: text-color is one of fill's aliasTokenTypes */
:root {
  --my-icon-color: var(--text-color);
}

.icon {
  fill: var(--my-icon-color);
}
```

Where a property should paint the value of a token from another category, a
token of an accepted category can reference it in its `*.tokens.json`.

## Autofix

With `--fix`, the rule replaces a raw value with the token that the property
type's `customFixes` in `config.mjs` maps it to, and reports a value with no
entry without fixing it. A mapping can cross units: `Size` maps both `0.75rem`
and `12px` to `--size-item-xsmall`, which is `12px`.

## Checked properties

[Checked properties](use-design-tokens/checked-properties.md) lists every
property the rule checks, grouped by property type, with the token categories
and other values each type accepts and the values it fixes. It is generated
from `config.mjs`.

The other pages below give examples of correct and incorrect usage for some of
the properties:

```{toctree}
:glob: true
:maxdepth: 1

use-design-tokens/checked-properties
use-design-tokens/*
```
