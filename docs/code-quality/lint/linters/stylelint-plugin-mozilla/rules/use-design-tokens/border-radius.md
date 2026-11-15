# border-radius

The `use-design-tokens` rule checks that CSS `border-radius` declarations use
design token variables instead of hardcoded values. This ensures consistent
border-radius usage across the application and makes it easier to maintain design
system consistency.

## Examples of incorrect code for this rule

```css
.custom-button {
  border-radius: 0.5rem;
}
```

```css
.card {
  border-radius: 8px;
}
```

```css
.avatar {
  border-radius: 50%;
}
```

## Examples of correct token usage for this rule

```css
.custom-button {
  border-radius: var(--border-radius-small);
}
```

```css
.custom-button {
  border-radius: var(--border-radius-medium);
}
```

```css
.custom-button {
  border-radius: var(--border-radius-circle);
}
```

```css
.custom-button {
  border-radius: var(--border-radius-large);
}
```

```css
/* Local CSS variables that reference valid border-radius tokens are allowed */
:root {
  --custom-border-radius: var(--border-radius-small);
}

.custom-button {
  border-radius: var(--custom-border-radius);
}
```

```css
.custom-button {
  border-radius: var(--custom-border-radius, --border-radius-small);
}
```

The rule also allows the non-token values listed for
{ref}`use-design-tokens-border-radius` in the checked properties.

## Autofix functionality

This rule can automatically fix some violations by replacing raw values with
appropriate design tokens. {ref}`use-design-tokens-border-radius` in the checked
properties lists every value it fixes. For example:

```css
/* Before */
.a {
  border-radius: 50%;
}

/* After autofix */
.a {
  border-radius: var(--border-radius-circle);
}
```

```css
/* Before */
.a {
  border-radius: 100%;
}

/* After autofix */
.a {
  border-radius: var(--border-radius-circle);
}
```

```css
/* Before */
.a {
  border-radius: 1000px;
}

/* After autofix */
.a {
  border-radius: var(--border-radius-circle);
}
```

```css
/* Before */
.a {
  border-radius: 4px;
}

/* After autofix */
.a {
  border-radius: var(--border-radius-small);
}
```

```css
/* Before */
.a {
  border-radius: 8px;
}

/* After autofix */
.a {
  border-radius: var(--border-radius-medium);
}
```

```css
/* Before */
.a {
  border-radius: 16px;
}

/* After autofix */
.a {
  border-radius: var(--border-radius-large);
}
```

```css
/* Before */
.a {
  border-radius: 4px 8px;
}

/* After autofix */
.a {
  border-radius: var(--border-radius-small) var(--border-radius-medium);
}
```
