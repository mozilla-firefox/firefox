# no-unguarded-high-contrast-token-overrides (stylelint)

This rule disallows redefining a design token that has a high contrast value
with a literal value, or with a token that lacks one of those values, unless the
declaration sits under a guard that excludes each high contrast condition the
token has a value for.

The token sheets give a token its high contrast values in the
`tokens-prefers-contrast` and `tokens-forced-colors` cascade layers. A
consumer sheet that redefines the token outranks all of them: on the element
the token sheet declares on (`:root`, or a component's `:host`), an unlayered
declaration beats every layer, and on a descendant a declaration beats the
inherited value whatever its layer. So the new value also applies in high
contrast, and the token's high contrast value is lost. The guard depends on the
token:

| The token has a value in | Guard the override with |
| --- | --- |
| the forced-colors layer only | `@media not (forced-colors)` |
| the prefers-contrast layer, with or without the forced-colors layer | `@media not (prefers-contrast)` |

Forced colors imply prefers-contrast, so `not (prefers-contrast)` also excludes
forced colors. `(forced-colors: none)` and `(prefers-contrast: no-preference)`
are equivalent guards. Write those in New Tab's SCSS: the Sass
version New Tab compiles with rejects `@media not (…)` and turns
`@media (not (…))` into `@media (false)`. See
[Scoping token overrides](/accessible/HCMMediaQueries.md#scoping-token-overrides-with-media-not-forced-colors).

## Rule Scope

The rule checks a declaration of a token that has a value in a
`tokens-prefers-contrast` or `tokens-forced-colors` layer of any generated
token sheet, Nova layers included. `./mach buildtokens` lists these tokens in
`toolkit/themes/shared/design-system/dist/high-contrast-tokens.mjs`.

The check covers every listed token, whatever its type, so an opacity or a
gradient token counts as much as a color. A value counts as a literal when it
holds anything other than a token reference: a color, `transparent`,
`currentColor`, a number, a keyword, a function such as `rgb()` or
`linear-gradient()`, or a base color token such as `--color-gray-80` or
`--color-white-alpha-20`. That holds at the top level, inside `light-dark()` or
`color-mix()`, and in a `var()` fallback. System colors, the deprecated ones
included, and CSS-wide keywords such as `inherit` are not literals.

A `var()` reference to another design token, fallbacks included, is reported
when that token has no value under one of the conditions the redefined token
has. A token has a value under a condition when it has one of its own, or when
each of its other values is a plain `var()` of a token that has one. A
computed value, such as a color-mix() of such a token, does not count, since
under forced colors it would mix the system color. A reference to a custom property that
is not a design token, such as one the sheet defines locally, cannot be judged
and is not reported.

The rule accepts a guard nested at any depth and combined with other
conditions, and nested guards that together exclude every condition.
`not (forced-colors: active)` counts as a forced-colors guard, and
`use-boolean-forced-colors-query` rewrites it to the boolean form. It skips:

- a declaration in a `tokens-*` layer;
- a declaration in a block that applies only under the token's high contrast
  conditions, such as `@media (forced-colors)` or `@media (prefers-contrast)`,
  which is deliberate high contrast styling. Forced colors imply
  prefers-contrast, so `@media (forced-colors)` also counts for a token with
  only a prefers-contrast value. The reverse does not hold: a
  `@media (prefers-contrast)` block also applies under forced colors, so a
  token's forced-colors value still needs `not (forced-colors)` there;
- a declaration in a rule whose selector requires `[lwtheme]`: forced colors
  replace an add-on theme with the default theme unless the user turns off
  `browser.theme.forced-colors-override.enabled` to keep it, and under
  prefers-contrast the theme's own colors already win. A selector that requires
  `[lwtheme]` only through a double negation such as `:not(:not([lwtheme]))`
  does not get the exemption;
- the generated token sheets themselves.

A consumer's own named layer is not exempt, since it usually loads after the
token sheets and outranks their layers.

## Options

`ignoreForcedColors: true` checks only the prefers-contrast values, so a token
with only a forced-colors value is not checked.

## Autofix

`./mach lint --fix` wraps the declaration in a nested `@media` block with the
guard the message names, inside the declaration's own rule. A declaration right
after it that needs the same guard joins the same block. In New Tab's SCSS the
fix writes the forms without `not`. It leaves alone a declaration with a
disable comment or a comment at the end of its line.

The fix changes rendering in high contrast, because it lets the token's own
high contrast value show where the literal used to. That value can suit a
surface badly: light text on a screen that keeps its dark background turns
dark, and a selected row's background changes while its text color comes from
a token with no prefers-contrast value. Look at the result under
prefers-contrast, with `ui.useAccessibilityTheme=1` and
`browser.display.document_color_use=1`, before landing it.

## Examples of incorrect usage for this rule

```scss
/* --button-border-color has forced-colors and prefers-contrast values. */
:root {
  --button-border-color: #fff;

  @media not (forced-colors) {
    --button-border-color: #fff;
  }
}
```

## Examples of correct usage for this rule

```scss
:root {
  @media not (prefers-contrast) {
    --button-border-color: #fff;
  }
}
```

```scss
// New Tab
.widget {
  @media (prefers-contrast: no-preference) {
    --button-border-color: #fff;
  }
}
```

## Disabling the rule

Disable it where an override already gives at least the contrast the token's
own value would, or sits on a background that stays the same in high contrast
mode, such as an image, or where the token's high contrast value would be wrong
for the element, with a comment saying why:

```css
.promo-banner {
  background-image: url("chrome://browser/content/promo/night-sky.avif");
  /* stylelint-disable-next-line stylelint-plugin-mozilla/no-unguarded-high-contrast-token-overrides -- light text on the dark image */
  --text-color: #fbfbfe;
}
```

```css
/* The value is a background shorthand that draws the autoscroll icon, which the forced-colors color would drop. */
/* stylelint-disable-next-line stylelint-plugin-mozilla/no-unguarded-high-contrast-token-overrides */
--panel-background-color: rgba(249, 249, 250, 0.8) no-repeat center var(--autoscroll-background-image);
```
