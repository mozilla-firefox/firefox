# use-boolean-forced-colors-query (stylelint)

This rule disallows `(forced-colors: active)` in `@media` queries, in favor of
the boolean `(forced-colors)`.

Under Windows high contrast mode, a chrome document is in the `forced-colors:
requested` state rather than `active` (see
[High contrast mode](/code-quality/coding-style/css_guidelines.md#high-contrast-mode)
in the CSS guidelines). `(forced-colors: active)` does not match there, so a
chrome rule written against it never applies. A content document is either
`active` or `none`, so the boolean query is equivalent there.

## Rule Scope

The rule applies to every `@media` rule, nested ones included, in CSS and SCSS,
and checks `not (forced-colors: active)` too. It does not report
`(forced-colors: none)`, which is false under `requested` just as
`not (forced-colors)` is.

The rule cannot see a query outside an `@media` prelude: the media list of an
`@import`, a `@custom-media` definition, a SCSS `$variable` holding a query, a
`matchMedia()` string in JS, or an HTML `media=` attribute.

## Autofix

The rule replaces `(forced-colors: active)` with `(forced-colors)`, and keeps
a comment elsewhere in the prelude. A comment inside the feature's own
parentheses, as in `(forced-colors /* … */: active)`, is dropped.

In a chrome document under `requested`, the fix changes behavior in both
directions: `(forced-colors)` starts to match there, and
`not (forced-colors)` stops matching where `not (forced-colors: active)` did.

## Examples of incorrect usage for this rule

```css
@media (forced-colors: active) {
  .button { border-color: ButtonText; }
}

@media not (forced-colors: active) {
  .button { box-shadow: var(--box-shadow-level-1); }
}
```

## Examples of correct usage for this rule

```css
@media (forced-colors) {
  .button { border-color: ButtonText; }
}

@media not (forced-colors) {
  .button { box-shadow: var(--box-shadow-level-1); }
}
```

## Disabling the rule

The boolean query matches wherever `(forced-colors: active)` does. A negated
query that has to keep matching in chrome under `requested` can stay
`not (forced-colors: active)` with a disable comment.
