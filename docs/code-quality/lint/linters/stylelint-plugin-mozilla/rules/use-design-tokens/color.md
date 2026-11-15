# color

The `use-design-tokens` rule checks that CSS `color` declarations use
design token variables instead of hardcoded values. This ensures consistent
text-color usage across the application and makes it easier to maintain design
system consistency.

## Examples of incorrect code for this rule

```css
.card {
  color: #191919;
}
```

```css
.custom-button {
  color: rgba(42 42 42 / 0.15);
}
```

```css
button:hover {
  color: rgba(0 0 0 / 0.25);
}
```

```css
.element {
  color: oklch(69% 0.19 15);
}
```

```css
:root {
  --my-token: blue;
}

.my-button {
  color: var(--my-token, oklch(55% 0.21 15));
}
```

## Examples of correct token usage for this rule

```css
.card {
  color: var(--text-color);
}
```

```css
.custom-button {
  color: var(--text-color);
}
```

```css
button:hover {
  color: var(--text-color);
}
```

```css
/* You may set a fallback for a token. */

.my-button {
  color: var(--text-color, oklch(55% 0.21 15));
}
```

```css
/* Local CSS variables that reference valid text-color tokens are allowed */

:root {
  --my-token: var(--text-color);
}

.my-button {
  color: var(--my-token);
}
```

The rule also allows these non-token values:

```css
.inherited-text-color {
  color: inherit;
}
```

```css
.initial-text-color {
  color: initial;
}
```

```css
.revert-text-color {
  color: revert;
}
```

```css
.revert-layer-text-color {
  color: revert-layer;
}
```

```css
.unset-text-color {
  color: unset;
}
```

```css
.current-text-color {
  color: currentColor;
}
```

```css
.current-text-color {
  color: white;
}
```

```css
.current-text-color {
  color: black;
}
```

This rule also allows base color tokens, as long as they are defined in a local custom property.

```css
:root {
  --my-token: var(--color-gray-20);
}

.my-button {
  color: var(--my-token);
}
```

Functions that use or modify base color tokens are also allowed

```css
:root {
  --my-token: light-dark(var(--color-gray-20), var(--color-gray-80));
}

.my-button {
  color: var(--my-token);
}
```

```css
:root {
  --my-token: color-mix(in oklch, var(--color-blue-50) 20%, transparent);
}

.my-button {
  color: var(--my-token);
}
```

```css
/* use relative color syntax if modifying a base color token with an oklch function */

:root {
  --my-token: oklch(from var(--color-blue-50) l c h / 20%);
}

.my-button {
  color: var(--my-token);
}
```

## Autofix functionality

This rule can automatically fix some violations by replacing hex color values with
appropriate color names. Examples of autofixable violations:

```css
/* Before */
.a {
  color: #fff;
}

/* After autofix */
.a {
  color: white;
}
```

```css
/* Before */
.a {
  color: #ffffff;
}

/* After autofix */
.a {
  color: white;
}
```

```css
/* Before */
.a {
  color: #FFF;
}

/* After autofix */
.a {
  color: white;
}
```

```css
/* Before */
.a {
  color: #FFFFFF;
}

/* After autofix */
.a {
  color: white;
}
```

```css
/* Before */
.a {
  color: #000;
}

/* After autofix */
.a {
  color: black;
}
```

```css
/* Before */
.a {
  color: #000000;
}

/* After autofix */
.a {
  color: black;
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
    color: LinkText;
  }
}

/* Better */
.a {
  color: var(--link-color);
}
```
