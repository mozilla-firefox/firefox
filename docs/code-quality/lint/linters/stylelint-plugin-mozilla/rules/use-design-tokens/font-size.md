# font-size

The `use-design-tokens` rule checks that CSS `font-size` declarations use
design token variables instead of hardcoded values. This ensures consistent
font-size usage across the application and makes it easier to maintain design
system consistency.

## Examples of incorrect code for this rule

```css
.custom-text {
  font-size: 12px;
}
```

```css
.heading {
  font-size: 1rem;
}
```

```css
.small-text {
  font-size: 0.867rem;
}
```

```css
.large-text {
  font-size: 1.2em;
}
```

```css
.percentage-text {
  font-size: 100%;
}
```

```css
.point-text {
  font-size: 16pt;
}
```

## Examples of correct token usage for this rule

```css
.custom-text {
  font-size: var(--font-size-root);
}
```

```css
.small-text {
  font-size: var(--font-size-small);
}
```

```css
.large-text {
  font-size: var(--font-size-large);
}
```

```css
.xlarge-text {
  font-size: var(--font-size-xlarge);
}
```

```css
.xxlarge-text {
  font-size: var(--font-size-xxlarge);
}
```

```css
.xxxlarge-text {
  font-size: var(--font-size-xxxlarge);
}
```

```css
.heading-medium {
  font-size: var(--font-size-heading-medium);
}
```

```css
.heading-large {
  font-size: var(--font-size-heading-large);
}
```

```css
.heading-xlarge {
  font-size: var(--font-size-heading-xlarge);
}
```

```css
/* Local CSS variables that reference valid font-size tokens are allowed */
:root {
  --custom-font-size: var(--font-size-small);
}

.custom-text {
  font-size: var(--custom-font-size);
}
```

```css
.custom-text {
  font-size: var(--custom-font-size, var(--font-size-small));
}
```

The rule also allows the non-token values listed for
{ref}`use-design-tokens-font-size` in the checked properties.
