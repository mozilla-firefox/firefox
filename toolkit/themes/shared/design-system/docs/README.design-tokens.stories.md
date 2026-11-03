# Design Tokens
## Background
Our design tokens are stored in JSON, the benefit of which is that they can be converted or translated into other languages or tools (e.g CSS, Swift, Kotlin, Figma).

For more information on how design tokens are named and organized, see the [Acorn Design System docs](https://acorn.firefox.com/latest/desktop/design-tokens/how-design-tokens-work/overview-YYp6MVjt).

## Design token tiers
Design tokens are split into different levels: base, application, and component. The global `tokens-shared.css`, `tokens-brand.css` and `tokens-platform.css` files include the base, application and *some* component tokens. Component tokens that are generally used for styling outside of reusable moz- components like button, card, input, sidebar, panel, etc are included.

While there are many global tokens, some component tokens are stored next to their component and aren't always imported by default. The moz- components keep their design token definitions next to their CSS/JS in `toolkit/content/widgets/moz-*/`, and the `browser/themes/shared/` folder also has design tokens. This means design tokens for tabbrowser, urlbar, etc can be kept with the code that uses them.

Only design tokens in the `toolkit/themes/shared/design-system/src/tokens/` folder are included in the global design tokens. Any other design tokens will be built next to their source `component.tokens.json` file and need to be manually included where they're needed.

## Quick start
`src/tokens` holds our source of truth for design tokens in `mozilla-central` under the [design-system](https://searchfox.org/firefox-main/source/toolkit/themes/shared/design-system) folder in `toolkit/themes/shared`. The CSS design token files in that folder come from the JSON files. If you need to modify a design token file, you should be editing the JSON.

The tokens are organized into files with the `*.tokens.json` suffix, where the first part of the filename defines the top-level category of token. For example, `border.tokens.json` outputs `--border-*` tokens.

In order for us to be able to define design tokens in one place (the JSON files) and allow all platforms to consume design tokens in their specific format, we use a build system called [Style Dictionary](https://amzn.github.io/style-dictionary/#/).

Here's how to build design tokens for desktop:

```sh
$ ./mach buildtokens
```

If successful, you should see Style Dictionary building all of our tokens files within the `design-system/dist` folder. Otherwise, Style Dictionary can also generate helpful errors to help you debug.

At the end, we're capable of transforming JSON notation into CSS:

```json
{
 "color": {
  "blue": {
    "0": {
      "value": "oklch(97% 0.05 260)"
    },
    "10": {
      "value": "oklch(90% 0.13 260)"
    },
    "20": {
      "value": "oklch(83% 0.17 260)"
    },
    "30": {
      "value": "oklch(76% 0.2 260)"
    },
    "40": {
      "value": "oklch(69% 0.22 260)"
    },
    "50": {
      "value": "oklch(62% 0.24 260)"
    },
    "60": {
      "value": "oklch(55% 0.24 260)"
    },
    "70": {
      "value": "oklch(48% 0.2 260)"
    },
    "80": {
      "value": "oklch(41% 0.17 260)"
    },
    "90": {
      "value": "oklch(34% 0.14 260)"
    },
    "100": {
      "value": "oklch(27% 0.1 260)"
    },
    "110": {
      "value": "oklch(20% 0.05 260)"
    }
  },
 }
}
```

```css
--color-blue-0: oklch(97% 0.05 260);
--color-blue-10: oklch(90% 0.13 260);
--color-blue-20: oklch(83% 0.17 260);
--color-blue-30: oklch(76% 0.2 260);
--color-blue-40: oklch(69% 0.22 260);
--color-blue-50: oklch(62% 0.24 260);
--color-blue-60: oklch(55% 0.24 260);
--color-blue-70: oklch(48% 0.2 260);
--color-blue-80: oklch(41% 0.17 260);
--color-blue-90: oklch(34% 0.14 260);
--color-blue-100: oklch(27% 0.1 260);
--color-blue-110: oklch(20% 0.05 260);
```

Neat!

## Testing

We have basic tests in place to ensure that our built CSS files stay up to date
and that new tokens are properly categorized. These tests will fail if the JSON
is modified but the command to build the tokens CSS files doesn't run, or if the
tokens CSS files are modified directly without changing the JSON.

Our tests are Node-based to allow us to install and work with the
`style-dictionary` library, and follow a format that has been used previously
for Devtools and New Tab tests.

You can run the tests locally using the following command:

```sh
$ ./mach npm test --prefix=toolkit/themes/shared/design-system
```

The same tests run in CI as `node(design-tokens)`. They compare every CSS file
the build writes against the checked-in copy, including the component
`*.tokens.css` files next to their `*.tokens.json`. They don't run the
`build-figma-nova` step, so they can't catch a stale or pruned
`*.nova.tokens.json`; only the `./mach buildtokens && git status --short` check
described under "Overriding tokens" does. For the same reason, build with
`./mach buildtokens` rather than `npm run build`, which skips the
`build-figma-nova` step that `./mach buildtokens` runs first.

The CI task only runs when a file under `toolkit/themes/shared/design-system/`
changes, so a patch that edits only component token files elsewhere has to run
the tests locally.

## JSON format deep dive

Style Dictionary works by ingesting JSON files with tokens data and performing various platform-specific transformations to output token files formatted for different languages. The library has certain quirks and limitations that we had to take into consideration when coming up with a JSON format for representing our tokens. The following is a bit of a "how to" guide for reading and adding to [`src/tokens`](https://searchfox.org/firefox-main/source/toolkit/themes/shared/design-system/src/tokens) for anyone who needs to consume our tokens or add new tokens.

Every `*.tokens.css` file in the tree is generated, and the `*.tokens.json` beside it is the source. They are not confined to `dist/`: around eighteen component sheets sit next to the code they style, under `browser/themes/shared/` (`urlbar.tokens.css`, `tab.tokens.css`) and `toolkit/content/widgets/` (`moz-badge.tokens.css`, `panel-list.tokens.css`). Each one opens with a header saying so. The trap is that looking up what a token resolves to lands you in exactly the file you must not edit, so check the filename suffix and the header before changing a token declaration. A hand-written theme file in the same directory, such as `urlbar.css`, is fair game; its `.tokens.css` sibling is not.

The generator reads `src/tokens/base/` and the directories listed in `COMPONENT_TOKEN_PATHS` in [`token-paths.mjs`](https://searchfox.org/firefox-main/source/toolkit/themes/shared/design-system/config/helpers/general/token-paths.mjs). Tokens from `src/tokens/components/` go into the global sheets in `dist/`, while each base `*.tokens.json` under `toolkit/content/widgets/` and `browser/themes/shared/` is built into a `*.tokens.css` beside it. An override file such as `moz-badge.nova.tokens.json` gets no sheet of its own; its values go into the base file's sheet, under `@media -moz-pref("browser.nova.enabled")` for Nova.

A `comment` next to a token's `value` is written as a CSS comment above every declaration of that token, in every layer it is declared in, forced colors and Nova included. A comment about one side's value, such as how a light-mode literal was derived, then sits above the other layers' declarations too, so put such a derivation in the commit message instead. A `comment` inside a `nova` block stays in the JSON and isn't written to the CSS.

Adding one token legitimately dirties four generated files: the component sheet, plus `dist/semantic-categories.mjs`, `dist/tokens-table.mjs` and `dist/tokens-figma-primitives.json`. That is propagation rather than drift - confirm it by checking that each hunk names your token.

### Naming

When going from token name to JSON, each place we would insert a hyphen, underscore, or case change in a variable name translates to a layer of nesting in our JSON. That means a token with the CSS variable name `--border-radius-circle` would be represented as:

```json
"border": {
  "radius": {
    "circle": {
      "value": "9999px"
    }
  }
}
```

The `value` key is required as this is the indicator [Style Dictionary looks for](https://amzn.github.io/style-dictionary/#/architecture?id=_4a-transform-the-tokens) to know what sections of the JSON to parse into distinct tokens. It gets omitted from the final variable name.

For simple cases `value` will be either a string or a number, but when we have to take media queries or themes into account `value` will be an object. Examples of these cases will be explained in more detail below.

### `@base`

When reading through our JSON you will see many instances where we have used an `@base` key, always appearing right before a `value` key indicating that everything up to that point should be parsed as a token. For example our `--font-weight` CSS token is represented like this in our JSON:

```json
"font": {
  "weight": {
    "@base": {
      "value": "normal"
    }
  }
}
```
This is a workaround for a [known](https://github.com/amzn/style-dictionary/issues/119) [limitation](https://github.com/amzn/style-dictionary/issues/366) of Style Dictionary where it doesn't support nested token names that appear after a `value` key. If we want to have both `--font-weight` and `--font-weight-semibold` CSS tokens they need to be represented as distinct objects with their own `value`s:

```json
"font": {
  "weight": {
    "@base": {
      "value": "normal"
    },
    "semibold": {
      "value": 600
    }
  }
}
```
`@base` is a special indicator we chose to enable Style Dictionary to parse our tokens correctly while still generating our desired variable names. Much like `value`, it ultimately [gets removed](https://searchfox.org/mozilla-central/rev/159929cd10b8fba135c72a497d815ab2dd5a521c/toolkit/themes/shared/design-system/tokens-config.js#94-96) from the final token name.

You will also see many places where `@base` is referenced in a value definition. This is a bit of a gotcha - even though `@base` isn't part of the token name, we still need to include it when using Style Dictionary's syntax for [variable references/aliases](https://amzn.github.io/style-dictionary/#/tokens?id=referencing-aliasing). That means the following CSS:

```css
--input-min-height: var(--button-min-height);
```

Will look like this in our JSON:

```json
"input": {
  "min": {
    "height": {
      "value": "{button.min.height.@base}"
    }
  }
}
```

### High Contrast Mode (HCM) media queries

We need to be able to change our token values to support both the `prefers-contrast` and `forced-colors` [media queries](https://firefox-source-docs.mozilla.org/accessible/HCMMediaQueries.html). We employ `prefersContrast` and `forcedColors` keys nested in a token's `value` key to indicate that we need to generate a media query entry for that token. For example this JSON:

```json
"border": {
  "color": {
    "interactive": {
      "@base": {
        "value": {
          "prefersContrast": "{text.color.@base}",
          "forcedColors": "ButtonText",
        }
      }
    },
  }
}
```
results in the following CSS:

```scss
/* tokens-shared.css */

:root,
:host(.anonymous-content-host) {
  @layer tokens-prefers-contrast {
    @media (prefers-contrast) {
      /** Border **/
      --border-color-interactive: var(--text-color);
    }
  }

  @layer tokens-forced-colors {
    @media (forced-colors) {
      /** Border **/
      --border-color-interactive: ButtonText;
    }
  }
}
```

The layers and media queries are nested inside a single `:root, :host(.anonymous-content-host)` selector, as recommended by the [HCM media query guidelines](https://firefox-source-docs.mozilla.org/accessible/HCMMediaQueries.html#writing-maintainable-frontend-code).

### Theming

Our JSON supports two dimensions of theming designed around the needs of the Firefox desktop client; light and dark themes, and brand and platform themes.

#### Light and dark themes

We employ `light` and `dark` keys in our `value` objects to indicate when a given token should have different values in our light and dark themes:

```json
"background": {
  "color": {
    "box": {
      "value": {
        "light": "{color.white}",
        "dark": "{color.gray.80}",
      }
    },
  }
}
```

The above JSON communicates that `--background-color-box` should have the value of `var(--color-white)` in our light theme and `var(--color-gray-80)` in our dark theme.

If a token has the same value for both the light and dark themes it will either have a string or number as its `value` or it will use a `default` key:

```json
"button": {
  "background": {
    "color": {
      "disabled": {
        "value": {
          "default": "{button.background.color.@base}",
        }
      }
    }
  }
}
```

The above JSON indicates that `--button-background-color-disabled` will have the value of `var(--button-background-color)` regardless of theme.

#### Global token brand and platform themes

The Firefox desktop client consists of [two distinct surfaces](https://acorn.firefox.com/latest/resources/browser-anatomy/desktop-ZaxCgqkt#section-anatomy-fd); "the chrome," or the UI of the browser application that surrounds web pages, and the web content itself which is often referred to as "in-content." Firefox UI development spans both surfaces since our `about:` pages are "in-content" pages. In our design system we distinguish between these surfaces by using the terminology of `platform` vs `brand`. Chrome specific token values live in [`tokens-platform.css`](https://searchfox.org/firefox-main/source/toolkit/themes/shared/design-system/dist/tokens-platform.css) and in-content specific token values live in [`tokens-brand.css`](https://searchfox.org/firefox-main/source/toolkit/themes/shared/design-system/dist/tokens-brand.css).

We use `platform` and `brand` keys in our JSON to indicate when a token has a surface-specific value. For example this token definition:

```json
"text": {
  "color": {
    "@base": {
      "value": {
        "brand": {
          "light": "{color.gray.100}",
          "dark": "{color.gray.0}"
        },
        "platform": {
          "default": "currentColor"
        }
      }
    },
  }
}
```
communicates that `--text-color` should have the value `currentColor` in `tokens-platform.css` for chrome surfaces, and the value `light-dark(var(--color-gray-100), var(--color-gray-0))` in `tokens-brand.css` for in-content pages. The resulting CSS spans multiple files:

```scss
/* tokens-platform.css */
:root,
:host(.anonymous-content-host) {
  @layer tokens-foundation {
    /** Text **/
    --text-color: currentColor;
  }
}
```

```scss
/* tokens-brand.css */
:root,
:host(.anonymous-content-host) {
  @layer tokens-foundation {
    /** Text **/
    --text-color: light-dark(var(--color-gray-100), var(--color-gray-0));
  }
}
```

#### Chrome native OS theme

Sometimes rather than using our own theme, we defer to the user's OS theme, this only happens in the browser chrome and is referred to as the "native theme." The `(-moz-native-theme)` media query matches when `browser.theme.native-theme` is true, which it is by default on Linux only, unless the document forces the non-native theme. [`LightweightThemeConsumer.sys.mjs`](https://searchfox.org/firefox-main/source/toolkit/modules/LightweightThemeConsumer.sys.mjs) forces it for every theme other than `System theme — auto`, and sets the root attributes that decide which layer paints the chrome:

<table>
  <thead>
    <tr>
      <th>Theme</th>
      <th>Root attributes</th>
      <th><code>(-moz-native-theme)</code></th>
      <th>Base token layers (before overrides)</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td><code>System theme — auto</code> on Linux</td>
      <td><code>theme-in-app</code></td>
      <td>matches</td>
      <td><code>tokens-foundation</code></td>
    </tr>
    <tr>
      <td><code>System theme — auto</code> on Windows and macOS</td>
      <td><code>theme-in-app</code></td>
      <td>doesn't match (the pref is false)</td>
      <td><code>tokens-browser-theme</code></td>
    </tr>
    <tr>
      <td><code>Light</code> and <code>Dark</code></td>
      <td><code>theme-in-app</code></td>
      <td>doesn't match (forced non-native)</td>
      <td><code>tokens-browser-theme</code></td>
    </tr>
    <tr>
      <td>Alpenglow and add-on themes</td>
      <td><code>lwtheme</code></td>
      <td>doesn't match (forced non-native)</td>
      <td><code>tokens-foundation</code></td>
    </tr>
    <tr>
      <td>Any theme under forced colors</td>
      <td>The default theme is swapped in (<code>browser.theme.forced-colors-override.enabled</code>): no <code>lwtheme</code>, and <code>theme-in-app</code> as the selected theme sets it</td>
      <td>as for <code>System theme — auto</code></td>
      <td><code>tokens-forced-colors</code> over <code>tokens-prefers-contrast</code> where it matches, then <code>tokens-foundation</code></td>
    </tr>
  </tbody>
</table>

A lightweight theme therefore paints the same `tokens-foundation` values as the native theme, even though `(-moz-native-theme)` doesn't match for it. When a token has a `nativeTheme` value, that value goes into `tokens-foundation` and controls both the native theme and lightweight themes, while the token's light/dark value goes into the `tokens-browser-theme` layer for the in-app themes.

```json
{
  "color.accent.primary.@base": {
    "value": {
      "light": "{color.blue.60}",
      "dark": "{color.cyan.30}",
      "nativeTheme": "AccentColor",
      "forcedColors": "ButtonText"
    }
  }
}
```

This snippet will use the light/dark value in-content, and in the chrome under the `Light` and `Dark` themes and under `System theme — auto` on Windows and macOS. Under the native theme and lightweight themes it will use `AccentColor`, and `ButtonText` in HCM.

So to give the chrome an in-app color without breaking the native theme and lightweight themes, put a value that follows the system or lightweight theme (e.g. a color-mix of `currentColor`, `{toolbar.background.color}`) in `nativeTheme`, and the fixed in-app color in `light`/`dark`.

The "browser theme" values end up in the `tokens-browser-theme` layer, which needs a narrower selector than the other layers and so gets its own top-level rule:

```scss
/* tokens-platform.css */
:root:is([theme-in-app], :not([lwtheme])),
:host(.anonymous-content-host) {
  @layer tokens-browser-theme {
    @media not ((forced-colors) or (-moz-native-theme)) {
      /** Color **/
      --color-accent-primary: light-dark(var(--color-blue-60), var(--color-cyan-30));
    }
  }
}
```

### rem is the system font size in chrome

`toolkit/themes/shared/global-shared.css` sets `:root { font: message-box }`, and the `font` shorthand carries `font-size`, so a chrome document's root font size is the system UI font size: roughly 11px on macOS, 12px on Windows, and 14 to 15px on Linux. Only content documents get 16px. Every rem-valued token scales with that.

The space scale is 16px-based, so every space token runs short in chrome - 25% short at a 12px root. That is a property of the scale rather than of any one patch: it was once calibrated to a 15px chrome root and was deliberately rebased onto a clean 16px basis. Two consequences for a patch that swaps a fixed-px value for a space token:

* The two match only at a 16px root, so never call them equivalent without saying at what root size, and never assume 16px for anything in `browser/` or `toolkit/` chrome CSS.
* Where the quantity must not scale with the system font - a geometric bleed, an overlay inset, an icon box - pick a fixed-px token instead. `--size-item-*`, `--icon-size-*`, `--size-image-*` and `--size-layout-*` are all literal px.

### Adding new tokens

In order to add a new token you will need to make changes to the appropriate file in `src/tokens` or add a new file if needed - any files generated based on our JSON token definitions should never be modified directly. You should be able to work backwards from the variable name in whatever language you're working with to figure out the correct JSON structure.

Once you've made your changes, you can generate the new tokens CSS files by running `./mach buildtokens`.

If you encounter errors or get unexpected results when running the build command it may be because:

* You forgot to nest everything in a `value` key and consequently Style Dictionary is not parsing your tokens correctly, or;
* You omitted `@base` when referencing another token, or;
* You are referencing another token that you haven't defined in the JSON yet - Style Dictionary should give descriptive error messages in this case.

If you need any help feel free to reach out in #firefox-reusable-components on Slack or #reusable-components on Matrix.

### Built-in theme manifests read token JSON

The AI window and private window themes in [`browser/themes/addons/`](https://searchfox.org/firefox-main/source/browser/themes/addons) don't check in their `manifest.json`. [`process_tokens.py`](https://searchfox.org/firefox-main/source/browser/themes/addons/process_tokens.py) generates it at build time from the theme's `manifest.in.json` and the token JSON files that `browser/themes/addons/moz.build` lists as the `GeneratedFile` inputs. A `{a.b.c}` reference in the `colors` or `properties` of the manifest's `theme` or `dark_theme` is a path into those files, with the file name as its first segment and a `*.nova.tokens.json` merged over its base file; a reference anywhere else, such as in `images`, is copied unchanged. The script takes the token's `nova` value when it has one, then picks the `light` or `dark` side for the manifest's `theme` or `dark_theme`, falling back to `default` and then `brand`.

The references are substituted at build time, and a reference inside a token's value is followed too, so `{toolbar.field.border.color.focus}` also depends on `focus.outline.color`. When the theme applies, its `colors` are parsed as standalone colors with `currentColor` taken as black, so a token that resolves where it is used (e.g. a color-mix of `currentColor`) can't feed one; spell such a color out in `manifest.in.json`. A `theme_experiment` property reaches CSS as written, so a color expression in one still resolves where it is used. Renaming or removing a token a manifest references, or deleting a listed `*.nova.tokens.json` (which `./mach buildtokens` does when an override becomes empty), breaks the build unless the same commit updates the manifest or `moz.build`. Reshaping a referenced token's `value` so that none of those keys applies writes an unusable value into the manifest without an error. Before such a change, search `browser/themes/addons/*/manifest.in.json` for the token's path, then the token files `browser/themes/addons/moz.build` lists for the tokens that reference it, and `moz.build` for its file.

### Overriding tokens

Our build process provides a mechanism to override design tokens behind a pref. To do so, you will need to update [override-identifiers.mjs](https://searchfox.org/firefox-main/source/toolkit/themes/shared/design-system/config/helpers/general/override-identifiers.mjs), adding an `id` and a `pref` to classify the token overrides. For example, to override design tokens for Nova, we use this configuration:

```js
{
  id: "nova",
  pref: "browser.nova.enabled",
}
```

Then you can create new JSON files in `src/tokens` that end with a suffix using the `id` (e.g. `*.nova.tokens.json`) and follow the JSON format to define the tokens that are different from the design system defaults. They should follow the same structure as the defaults so the tokens end up with the same names.

The high contrast overrides are emitted into cascade layers. A single `@layer` statement at the top of `dist/tokens-shared.css` and of every component `*.tokens.css` declares all of them in precedence order, ending `tokens-prefers-contrast`, `tokens-prefers-contrast-nova`, `tokens-forced-colors`, `tokens-forced-colors-nova`, so that forced colors wins when a surface is in both regimes at once. `getLayerString` in [`get-layer-string.mjs`](https://searchfox.org/firefox-main/source/toolkit/themes/shared/design-system/config/helpers/desktop-format/get-layer-string.mjs) generates the statement: the three theme layers (`tokens-foundation`, `tokens-browser-theme`, `tokens-foundation-brand`), then those three again with each override `id` as a suffix, then each of the two accessibility layers followed by its override twins. Each document or shadow tree orders layers by the first appearance of their names, and a later statement cannot reorder a name already registered. A layer name missing from the statement is therefore ordered after every layer registered before its first use, which normally includes the forced colors and prefers-contrast layers, so its values beat them; add a new layer to `getLayerString` rather than naming it only in a sheet. Anything a consumer declares outside a layer beats all of them, whatever the specificity - see the [CSS guidelines](https://firefox-source-docs.mozilla.org/code-quality/coding-style/css_guidelines.html#an-unlayered-declaration-outranks-every-cascade-layer).

When you run `./mach buildtokens`, the CSS output will include the new tokens under a media query using the `pref` that you defined. For example:

```scss
:root,
:host(.anonymous-content-host) {
  @media -moz-pref("browser.nova.enabled") {
    @layer tokens-foundation-nova {
      /* Your token overrides */
    }
  }
}
```

The generator emits only the tokens that actually have an override - `shouldSkipToken` drops the rest - so an override sheet references far more tokens than it defines. The others resolve from the ungated foundation layer, which every consumer of `tokens-shared.css` gets. A token missing from an override sheet is therefore usually not a regression; the question to ask is whether the ungated layer carries the same value, not whether the override sheet still defines it.

`./mach buildtokens` prunes as well as generating. A `build-figma-nova` step re-derives every `*.nova.tokens.json` as a delta against its base `*.tokens.json` and unlinks the ones that come out empty (`writeTokens` in [`src/figma-import.mjs`](https://searchfox.org/firefox-main/source/toolkit/themes/shared/design-system/src/figma-import.mjs)). So once a base token holds the same literal value its override did, the next build deletes that override file. Restoring it by hand does not survive the following build, and the deletion belongs in the patch that made the values agree.

`ignoreFigma` on a base token stops Figma from writing an override for it, on the understanding that the token should use the base value. It does not preserve an existing override. To keep one, declare it as a key named after the override identifier inside the base `*.tokens.json` instead, which the importer keeps:

```json
{
  "value": {
    "default": "...",
    "nova": { "comment": "why", "value": "..." }
  }
}
```

Whether a token patch is complete comes down to one check:

```sh
$ ./mach buildtokens && git status --short
```

Empty output means the committed tree is a fixed point of the generator. Any output means the patch and the generator disagree, and the generator wins. Run it after a rebase too, since an upstream change to the token config can stale generated files your patch owns without touching a line of it.

#### Importing from Figma

Figma's values reach the `*.nova.tokens.json` files through two committed exports in `src/`. `figma-variables-all.json` mirrors the Figma file's variable collections, and `./mach buildtokens --import-figma --remote` rewrites it from the Figma API. `nova-export-clean-variables.json` is the subset the build reads: a plain `./mach buildtokens` maps it onto the override files and never reads the mirror.

`./mach buildtokens --import-figma` updates the subset from the mirror before building. It takes every token whose Nova value the mirror would change, so a difference already sitting between the two files comes along with whatever just changed in Figma; read the subset's diff before committing an import. `--match=<path substring>` narrows the import to matching tokens and `--all` takes every change. With neither, it asks in an editor which tokens to take, and when stdin is not a terminal it takes them all.

The import does not override a token whose base `*.tokens.json` holds a `nova` key, so a Figma change to that token reaches the tree only once the key is removed.
