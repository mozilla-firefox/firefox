# box-shadow

The `use-design-tokens` rule checks that CSS `box-shadow` declarations use
design token variables instead of hardcoded values. This ensures consistent
box-shadow usage across the application and makes it easier to maintain design
system consistency.

## Examples of incorrect code for this rule

```css
.button {
  box-shadow: 0 2px 4px rgba(0, 0, 0, 0.1);
}
```

```css
.element {
  box-shadow: inset 0 1px 2px rgba(0, 0, 0, 0.1);
}
```

## Examples of correct token usage for this rule

```css
.card {
  box-shadow: var(--card-box-shadow);
}
```

```css
.card-hover {
  box-shadow: var(--card-box-shadow-hover);
}
```

```css
.level-1-shadow {
  box-shadow: var(--box-shadow-level-1);
}
```

```css
.level-2-shadow {
  box-shadow: var(--box-shadow-level-2);
}
```

```css
.level-3-shadow {
  box-shadow: var(--box-shadow-level-3);
}
```

```css
.level-4-shadow {
  box-shadow: var(--box-shadow-level-4);
}
```

```css
.popup {
  box-shadow: var(--popup-box-shadow);
}
```

```css
.tab {
  box-shadow: var(--tab-box-shadow-selected);
}
```

The rule also allows these non-token values:

```css
.inherited-shadow {
  box-shadow: inherit;
}
```

```css
.initial-shadow {
  box-shadow: initial;
}
```

```css
.revert-shadow {
  box-shadow: revert;
}
```

```css
.revert-layer-shadow {
  box-shadow: revert-layer;
}
```

```css
.unset-shadow {
  box-shadow: unset;
}
```

```css
.no-shadow {
  box-shadow: none;
}
```
