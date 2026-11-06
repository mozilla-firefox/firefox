# Built-in theme manifests read token JSON paths

`toolkit/themes/shared/design-system/docs/README.design-tokens.stories.md`
"Built-in theme manifests read token JSON" has how the manifests resolve token
paths and which edits break the build. Before reshaping, renaming or deleting a
token, grep the manifests for its path:

```sh
rg -n -F '{<a.b.c path>' browser/themes/addons/*/manifest.in.json
```

A manifest also reaches a token through another token's value, so grep the
`*.tokens.json` inputs that `browser/themes/addons/moz.build` lists for the path
too, and repeat for each token that refers to it until no new reference turns
up: `toolbar.field.border.color.focus` reaches `focus.outline.color` that way.

When the fixed-point check (SKILL.md §6) shows a deleted or rewritten
`*.nova.tokens.json`, grep the build file for it:

```sh
rg -n -F '<file>.nova.tokens.json' browser/themes/addons/moz.build
```

A hit means the same commit updates the manifest or `moz.build`.

The build substitutes token strings without evaluating them. A `colors` entry is
parsed when the theme applies, with `currentColor` resolved against black, even
inside a color-mix, so a color that resolves where it is used is spelled out in
`manifest.in.json`. A `theme_experiment` `properties` entry reaches CSS as a raw
custom-property value, so its `currentColor` resolves at the element that reads
it.
