# border-color

The `use-design-tokens` rule checks that CSS border-color related declarations
(border-color, border, outline, outline-color, and their variants) use design
token variables instead of hardcoded values. This ensures consistent border-color
usage across the application and makes it easier to maintain design system
consistency.

## Examples of incorrect code for this rule

```css
.card {
  border-color: #191919;
}
```

```css
.custom-button {
  border: 3px dashed rgba(42 42 42 / 0.15);
}
```

```css
.error {
  outline-color: rgba(255 0 0 / 0.25);
}
```

```css
.element {
  border-top-color: oklch(69% 0.19 15);
}
```

```css
:root {
  --my-token: blue;
}

.my-button {
  border: 1px solid var(--my-token, oklch(55% 0.21 15));
}
```

## Examples of correct token usage for this rule

```css
.card {
  border-color: var(--border-color);
}
```

```css
.custom-button {
  border: 3px dashed var(--border-color);
}
```

```css
.error {
  outline-color: var(--border-color-error);
}
```

```css
/* You may set a fallback for a token. */

.my-button {
  border: 1px solid var(--border-color, oklch(55% 0.21 15));
}
```

```css
/* Local CSS variables that reference valid border-color tokens are allowed */

:root {
  --my-token: var(--border-color);
}

.my-button {
  border: 1px solid var(--my-token);
}
```

The rule also allows these non-token values:

```css
.transparent-border-color {
  border-color: transparent;
}
```

```css
.current-border-color {
  border-color: currentColor;
}
```

```css
.white-border-color {
  border-color: white;
}
```

```css
.black-border-color {
  border-color: black;
}
```

```css
.inherited-border-color {
  border-color: inherit;
}
```

```css
.initial-border-color {
  border-color: initial;
}
```

```css
.revert-border-color {
  border-color: revert;
}
```

```css
.revert-layer-border-color {
  border-color: revert-layer;
}
```

```css
.unset-border-color {
  border-color: unset;
}
```

```css
.border-none {
  border: none;
}
```

```css
.border-zero {
  border: 0;
}
```

This rule also allows base color tokens, as long as they are defined in a local custom property.

```css
:root {
  --my-token: var(--color-gray-20);
}

.my-button {
  border-color: var(--my-token);
}
```

Functions that use or modify base color tokens are also allowed

```css
:root {
  --my-token: light-dark(var(--color-gray-20), var(--color-gray-80));
}

.my-button {
  border-color: var(--my-token);
}
```

```css
:root {
  --my-token: color-mix(in oklch, var(--color-blue-50) 20%, transparent);
}

.my-button {
  border-color: var(--my-token);
}
```

```css
/* use relative color syntax if modifying a base color token with an oklch function */

:root {
  --my-token: oklch(from var(--color-blue-50) l c h / 20%);
}

.my-button {
  border-color: var(--my-token);
}
```

## Autofix functionality

This rule can automatically fix some violations by replacing hex color values with
appropriate color names. Examples of autofixable violations:

```css
/* Before */
.a {
  border-color: #fff;
}

/* After autofix */
.a {
  border-color: white;
}
```

```css
/* Before */
.a {
  border-color: #ffffff;
}

/* After autofix */
.a {
  border-color: white;
}
```

```css
/* Before */
.a {
  border-color: #FFF;
}

/* After autofix */
.a {
  border-color: white;
}
```

```css
/* Before */
.a {
  border-color: #FFFFFF;
}

/* After autofix */
.a {
  border-color: white;
}
```

```css
/* Before */
.a {
  border-color: #000;
}

/* After autofix */
.a {
  border-color: black;
}
```

```css
/* Before */
.a {
  border-color: #000000;
}

/* After autofix */
.a {
  border-color: black;
}
```

## System Colors

Using system colors, especially for forced colors or high contrast, is allowed.
However, it may be better to use a design system token that already accounts for
those situations and avoid needing the extra media query.

```css
/* Good */
@media (prefers-contrast) {
  .a {
    border-color: ButtonBorder;
  }
}

/* Better */
.a {
  border-color: var(--button-border-color);
}
```
