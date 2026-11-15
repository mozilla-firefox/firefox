# font-weight

The `use-design-tokens` rule checks that CSS `font-weight` declarations use
design token variables instead of hardcoded values. This ensures consistent
font-weight usage across the application and makes it easier to maintain design
system consistency.

This rule is autofixable and can automatically replace some font-weight values
with appropriate design tokens where possible.

## Examples of incorrect code for this rule

```css
.bold-text {
  font-weight: bold;
}
```

```css
.bolder-text {
  font-weight: bolder;
}
```

```css
.lighter-text {
  font-weight: lighter;
}
```

```css
.custom-text {
  font-weight: 400;
}
```

```css
.heading {
  font-weight: 600;
}
```

```css
.bold-text {
  font-weight: 700;
}
```

```css
.light-text {
  font-weight: 300;
}
```

```css
.heavy-text {
  font-weight: 900;
}
```

## Examples of correct token usage for this rule

```css
.normal-text {
  font-weight: var(--font-weight);
}
```

```css
.semibold-text {
  font-weight: var(--font-weight-semibold);
}
```

```css
.bold-text {
  font-weight: var(--font-weight-bold);
}
```

```css
.button-text {
  font-weight: var(--button-font-weight);
}
```

```css
.heading-text {
  font-weight: var(--font-weight-heading);
}
```

```css
/* Local CSS variables that reference valid font-weight keywords are allowed */
:root {
  --custom-font-weight: normal;
}

.custom-text {
  font-weight: var(--custom-font-weight);
}
```

```css
/* Local CSS variables that reference valid font-weight tokens are allowed */
:root {
  --custom-font-weight: var(--font-weight-semibold);
}

.custom-text {
  font-weight: var(--custom-font-weight);
}
```

```css
.custom-text {
  font-weight: var(--custom-font-weight, var(--font-weight-semibold));
}
```

The rule also allows these non-token values:

```css
.normal-text {
  font-weight: normal;
}
```

```css
.inherited-text {
  font-weight: inherit;
}
```

```css
.initial-text {
  font-weight: initial;
}
```

```css
.revert-text {
  font-weight: revert;
}
```

```css
.revert-layer-text {
  font-weight: revert-layer;
}
```

```css
.unset-text {
  font-weight: unset;
}
```

## Autofix functionality

This rule can automatically fix some violations by replacing values with
appropriate design tokens:

- `200` → `normal`
- `300` → `normal`
- `400` → `normal`
- `lighter` → `normal`
- `500` → `var(--font-weight-semibold)`
- `510` → `var(--font-weight-semibold)`
- `600` → `var(--font-weight-semibold)`
- `700` → `var(--font-weight-bold)`
- `800` → `var(--font-weight-bold)`
- `bold` → `var(--font-weight-bold)`
- `bolder` → `var(--font-weight-bold)`

Examples of autofixable violations:

```css
/* Before */
.normal-text {
  font-weight: 400;
}

/* After autofix */
.normal-text {
  font-weight: normal;
}
```

```css
/* Before */
.semibold-text {
  font-weight: 600;
}

/* After autofix */
.semibold-text {
  font-weight: var(--font-weight-semibold);
}
```

```css
/* Before */
.bold-text {
  font-weight: 700;
}

/* After autofix */
.bold-text {
  font-weight: var(--font-weight-bold);
}
```
