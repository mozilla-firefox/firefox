# Token layers: the high-contrast JSON keys, which layer each theme paints

## 1. Writing a high-contrast value into the token's JSON

The `prefersContrast` and `forcedColors` keys of a `value` object, and of the
`value` inside a `nova` block, feed the `tokens-prefers-contrast[-nova]` and
`tokens-forced-colors[-nova]` layers
(`toolkit/themes/shared/design-system/docs/README.design-tokens.stories.md`
"High Contrast Mode (HCM) media queries"). The trigger is any value side, base
or `nova`, that writes a literal (e.g. `color-mix()`, `light-dark()`, a hex)
where a reference used to supply high-contrast handling, or where its sibling
tokens have it. The fix is a `forcedColors` or `prefersContrast` entry that
references a token with the handling:

```json
"nova": {
  "comment": "color-mix with currentColor cannot be expressed in Figma",
  "value": {
    "nativeTheme": "color-mix(in srgb, currentColor 22%, transparent)",
    "default": "{<another token>}",
    "forcedColors": "{<a token that has the handling>}"
  }
}
```

A token's `comment` lands above its declaration in every layer, so put a
derivation of one side's value in the commit message (README "JSON format deep
dive").

Verify with `./mach buildtokens` and read the diff of the generated
`*.tokens.css`: it should contain the declarations the hand-written block did,
and the block in the consumer sheet then goes.

## 2. Which layer each theme kind paints

`toolkit/themes/shared/design-system/docs/README.design-tokens.stories.md`
The default theme paints `tokens-foundation` on Linux, where
`browser.theme.native-theme` is true, and the in-app layer on Windows and macOS;
Light and Dark paint the in-app layer everywhere; Alpenglow and add-on themes
paint `tokens-foundation`; forced colors swaps in the default theme. README
"Chrome native OS theme" has the table, and the in-app layer's selector and
media query. Each layer's `-nova` twin
paints over it while `browser.nova.enabled` is on, which it is by default.

`--setpref browser.theme.native-theme=false` reaches the in-app layer on Linux
and changes nothing on Windows and macOS, where the pref is already false. Tell
the regime from the root: `lwtheme` without `theme-in-app` is a lightweight
theme, which still reads `(-moz-native-theme)` false; `theme-in-app` with no
`lwtheme` is the native or the in-app theme, and `(-moz-native-theme)` tells
them apart. Setting `lwtheme` on the root with no `theme-in-app` drops a probe
back to the foundation layer the way a lightweight theme does.

A system theme and a lightweight theme share the foundation layer, so only a
`nativeTheme` value controls them, and a bug filed against either says nothing
about the in-app pair. Put a value that follows the system or lightweight theme
in `nativeTheme` and the fixed in-app color in `light`/`dark` (README "Chrome
native OS theme"); `git grep -l nativeTheme -- '*.tokens.json'` lists the tokens
with this shape.
