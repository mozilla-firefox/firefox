# size

The `use-design-tokens` rule checks that CSS size-related property declarations
use design token variables instead of hardcoded values. This ensures consistent
sizing across the application and makes it easier to maintain design system
consistency.

{ref}`use-design-tokens-size` in the checked properties lists the properties it
applies to.

Note that the following properties accept both space and size tokens:

- `inset` and all of its longhand properties
- Positioning attributes: `left`, `right`, `top`, and `bottom`

Use size tokens when these properties represent element dimensions, and space tokens when they
represent spacing/positioning offsets.

## Examples of incorrect code for this rule

```css
.card {
  min-width: 48px;
}
```

```css
.button {
  height: 0.75rem;
}
```

```css
.icon {
  width: 20px;
}
```

```css
.icon {
  width: calc(2 * 16px);
}
```

```css
:root {
  --local-size: 24px;
}

.button {
  min-height: var(--local-size);
}
```

## Examples of correct token usage for this rule

```css
.card {
  min-width: var(--size-item-large);
}
```

```css
.button {
  height: var(--size-item-xsmall);
}
```

```css
.icon {
  width: var(--icon-size-medium);
}
```

```css
.icon {
  width: var(--icon-size-medium, 28px);
}
```

```css
.icon {
  width: calc(2 * var(--icon-size-medium));
}
```

```css
.icon {
  width: calc(2 * var(--icon-size-medium, 28px));
}
```

```css
:root {
  --local-size: var(--size-item-small);
}

.button {
  min-height: var(--local-size);
}
```

```css
.positioned-element {
  inset: var(--size-item-large);
}
```

```css
.positioned-element {
  top: var(--size-item-medium);
  left: var(--size-item-small);
}
```

The rule also allows the non-token values listed for
{ref}`use-design-tokens-size` in the checked properties.

## Autofix functionality

This rule can automatically fix some violations by replacing raw size values
with appropriate size tokens. {ref}`use-design-tokens-size` in the checked
properties lists every value it fixes. For example:

```css
/* Before */
.a {
  height: 0.75rem;
}

/* After autofix */
.a {
  height: var(--size-item-xsmall);
}
```

```css
/* Before */
.a {
  width: 1rem;
}

/* After autofix */
.a {
  width: var(--size-item-small);
}
```

```css
/* Before */
.a {
  max-inline-size: calc(16px + 32px);
}

/* After autofix */
.a {
  max-inline-size: calc(var(--size-item-small) + var(--size-item-large));
}
```
