# use-design-tokens

This rule checks that CSS declarations use design token variables instead of
hardcoded values. This ensures consistent design system usage across the
application and makes it easier to maintain design system consistency.

The rule supports multiple CSS properties, each of which can be enabled or
disabled independently. See the documentation below for details about each
supported property.

## Configuration

The rule accepts the following configuration options:

- `true` - Enables checking for all supported CSS properties

- `null` - Disables the rule entirely

- An object with CSS property names as keys, which disables specific properties:

  - All properties are enabled by default when using object configuration
  - Set a property to `null` to disable checking for that property

Examples:

```javascript
// Enable all property checks
"stylelint-plugin-mozilla/use-design-tokens": true

// Disable the rule entirely
"stylelint-plugin-mozilla/use-design-tokens": null

// Disable specific properties (common in rollouts)
// All other properties remain enabled by default
"stylelint-plugin-mozilla/use-design-tokens": {
  "box-shadow": null
}
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

Some checked properties have no page below (e.g. `fill`, `stroke`, `opacity`,
`border-width`, `background-position`, `flex-basis`). The categories they
accept are in {searchfox}`config.mjs <tools/lint/stylelint/stylelint-plugin-mozilla/config.mjs>`.

## Autofix

With `--fix`, the rule replaces a raw value with the token that the property
type's `customFixes` in `config.mjs` maps it to, and reports a value with no
entry without fixing it. A mapping can cross units: `Size` maps both `0.75rem`
and `12px` to `--size-item-xsmall`, which is `12px`.

## Supported Properties

The following CSS properties are supported by this rule. Click on a property
name to see examples of correct and incorrect usage:

```{toctree}
:glob: true
:maxdepth: 1

use-design-tokens/*
```
