# background-color

The `use-design-tokens` rule checks that CSS `background-color` and
`background` declarations use design token variables instead of hardcoded
values. This ensures consistent background-color usage across the application
and makes it easier to maintain design system consistency.

## Examples of incorrect code for this rule

```css
.card {
  background-color: #191919;
}
```

```css
.custom-button {
  background: url('image.png') rgba(42 42 42 / 0.15);
}
```

```css
button:hover {
  background: rgba(0 0 0 / 0.25);
}
```

```css
:root {
  --my-token: blue;
}

.my-button {
  background: url('image.png') no-repeat center center / auto var(--my-token, oklch(55% 0.21 15));
}
```

```css
.accent-background-color {
  background-color: AccentColor;
}
```

## Examples of correct token usage for this rule

```css
.card {
  background-color: var(--background-color-box);
}
```

```css
.custom-button {
  background: url('image.png') var(--background-color-box);
}
```

```css
button:hover {
  background: var(--background-color-box);
}
```

```css
/* You may set a fallback for a token. */

.my-button {
  background: var(--background-color-box, oklch(55% 0.21 15));
}
```

```css
/* Local CSS variables that reference valid background-color tokens are allowed */

:root {
  --my-token: var(--background-color-box);
}

.my-button {
  background-color: var(--my-token, oklch(55% 0.21 15));
}
```

The rule also allows these non-token values:

```css
.transparent-background-color {
  background-color: transparent;
}
```

```css
.inherited-background-color {
  background-color: inherit;
}
```

```css
.unset-background-color {
  background-color: unset;
}
```

```css
.initial-background-color {
  background-color: initial;
}
```

```css
.current-background-color {
  background-color: currentColor;
}
```

This rule also allows base color tokens, as long as they are defined in a local custom property.

```css
:root {
  --my-token: var(--color-gray-20);
}

.my-button {
  background-color: var(--my-token);
}
```

Functions that use or modify base color tokens are also allowed

```css
:root {
  --my-token: light-dark(var(--color-gray-20), var(--color-gray-80));
}

.my-button {
  background-color: var(--my-token);
}
```

```css
:root {
  --my-token: color-mix(in oklch, var(--color-blue-50) 20%, transparent);
}

.my-button {
  background-color: var(--my-token);
}
```

```css
/* use relative color syntax if modifying a base color token with an oklch function */

:root {
  --my-token: oklch(from var(--color-blue-50) l c h / 20%);
}

.my-button {
  background-color: var(--my-token);
}
```

## Autofix functionality

This rule can automatically fix some violations by replacing hex color values with
appropriate color names. Examples of autofixable violations:

```css
/* Before */
.white-background {
  background-color: #fff;
}

/* After autofix */
.white-background {
  background-color: white;
}
```

```css
/* Before */
.white-background {
  background-color: #ffffff;
}

/* After autofix */
.white-background {
  background-color: white;
}
```

```css
/* Before */
.black-background {
  background-color: #000;
}

/* After autofix */
.black-background {
  background-color: black;
}
```

```css
/* Before */
.black-background {
  background-color: #000000;
}

/* After autofix */
.black-background {
  background-color: black;
}
```

```css
/* Before */
.custom-background {
  background: url('image.png') #fff;
}

/* After autofix */
.custom-background {
  background: url('image.png') white;
}
```

## System Colors

Using system colors, especially for forced colors or high contrast, is allowed.
However, it may be better to use a design system token that already accounts for
those situations and avoid needing the extra media query.

```css
/* Good */
@media (prefers-contrast) {
  .custom-background {
    background-color: ButtonFace;
  }
}

/* Better */
.custom-background {
  background-color: var(--button-background-color);
}
```
