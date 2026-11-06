# Nova layers and overrides

Nova is the shipped look: `browser.nova.enabled` defaults to `true`, and the
`-nova` layers, the `*.nova.tokens.json` files, the `nova` key and the Figma
import build it. The pref-off path is unmaintained.

## 1. A `nova` block replaces Figma's Nova values for its token

Paths in this section are relative to `toolkit/themes/shared/design-system/`.
The `nova` key's shape and what `ignoreFigma` means are in
`toolkit/themes/shared/design-system/docs/README.design-tokens.stories.md`
"Overriding tokens".

`src/figma-import.mjs`, the `build-figma-nova` step, runs in every
`./mach buildtokens` before the CSS build, after the import that only
`--import-figma` adds, and rewrites each `*.nova.tokens.json` from the
curated export, except for a token with a `nova` key or `ignoreFigma` on its
base value, which it skips wholesale (`walkUpdateNovaTokens`). The same README's
"Importing from Figma" says which export the build reads and what
`--import-figma` brings in besides the change you meant; read it before standing
in for a Figma update. The export has no native-theme mode, so the importer
copies the base token's `nativeTheme` into each Nova entry it writes: change it
in the base `*.tokens.json`, and the next build copies it into the Nova file. A
token it skips gets no copy, so a `nova` block has only the `nativeTheme`
written into it. The export also holds entries for tokens no code declares yet,
which the importer applies to the first token added under that name.
`ignoreFigma` changes the generated Nova sheets on every build.

**A `nova` block without `nativeTheme` paints its light and dark values in the
native and lightweight themes too.** The generator splits a value into the
native-theme and in-app layers only when it has a `nativeTheme`
(`config/helpers/desktop-format/should-transform-browser-theme-value.mjs`), so
the whole value lands in `tokens-foundation-nova` and
`tokens-browser-theme-nova` gets no declaration. Write `nativeTheme` into the
block whenever the base token has one. Any other side the block leaves out is
missing from the Nova CSS, where the base value paints instead, and from the
Figma export, where `config/helpers/figma-format/transform-token-value.mjs`
substitutes the literal string `transparent` for a light or dark value it cannot
find, and for a `forcedColors` value that is absent when light and dark differ,
so `dist/tokens-figma-theme.json` and `dist/<id>/tokens-figma-theme.json` (which
the Figma token importer reads) hold a color the browser never paints. The
export cannot tell a lossy `transparent` from a genuine one, so resolve the
token.

**Price a `nova` or `ignoreFigma` key before accepting it**, in a committed
tree: delete the key, run `./mach buildtokens`, and read what the generator
writes into the component's `*.nova.tokens.json` and into the Nova layer of
`dist/tokens-shared.css`; that is what the key suppresses.
`token-resolve.py <dist> --before HEAD` shows the same as resolved values
against the committed tree. Repeat every side you do not mean to change inside
the `nova` value beside the key you do, then restore everything the generator
rewrote, not only `dist/`. It writes under every root of `COMPONENT_TOKEN_PATHS`
(`config/helpers/general/token-paths.mjs`) and into the New Tab Nova sheet:

```sh
git checkout HEAD -- toolkit/themes/shared/design-system toolkit/content/widgets \
  browser/themes/shared browser/extensions/newtab/content-src/styles/nova
./mach buildtokens && git status --short    # back at the fixed point
```

A sibling block that omits a side has the same defect. The fixed-point check in
SKILL.md §6 "`./mach buildtokens` generates *and* prunes" cannot catch it: the
export is the generator's output, so it is already at the fixed point.

## 2. Where the Nova layers apply

The shared sheets declare their Nova layers under `-moz-pref()`, which parses
only in the sheets css_guidelines lists, whatever document loads it
(css_guidelines "Chrome-only media features go by the stylesheet, not the
document"). Every page that links them by `chrome://` URL, in-content pages
included, therefore paints the Nova layers while `browser.nova.enabled` is on,
the default.

A generated sheet outside `dist/` can declare its Nova layer under a condition
of its own: New Tab's `_tokens.scss` declares `@layer tokens-foundation-nova`
under a root class, not `-moz-pref()`. Read the condition off the sheet's
selector and trace what sets it, so the probe reaches that layer.
