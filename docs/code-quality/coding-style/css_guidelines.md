# CSS Guidelines

This document contains guidelines defining how CSS inside the Firefox
codebase should be written, it is notably relevant for Firefox front-end
engineers.

## Basics

Here are some basic tips that can optimize reviews if you are changing
CSS:

- Avoid `!important` but if you have to use it, make sure it's
  obvious why you're using it (ideally with a comment). The
  [Overriding CSS] section contains more information about this.

- Avoid magic numbers; prefer automatic sizing or alignment methods.
  Some examples to avoid:

  - absolutely positioned elements
  - hardcoded values such as: `vertical-align: -2px;` . The reason
    you should avoid such "hardcoded" values is that, they don't
    necessarily work for all font-size configurations.

- Avoid setting styles in JavaScript. It's generally better to set a
  class and then specify the styles in CSS.

- `classList` is generally better than `className`. There's less
  chance of overwriting an existing class.

- Only use generic selectors such as `:last-child`, when it is what
  you mean semantically. If not, using a semantic class name is more
  descriptive and usually better.

### Boilerplate

Make sure each file starts with the standard copyright header (see
[License Boilerplate](https://www.mozilla.org/MPL/headers/)).

### Before adding more CSS

It is good practice to check if the CSS that is being written is needed,
it can be the case that a common component has been already written
could be reused with or without changes. Most of the time, the common
component already follows the a11y/theme standards defined in this
guide. So, when possible, always prefer editing common components to
writing your own.

Also, it is good practice to introduce a common class when the new
element you are styling reuses some styles from another element, this
allows the maintenance cost and the amount of code duplication to be
reduced.

## Formatting

### Spacing & Indentation

- 2 spaces indentation is preferred
- Add a space after each comma, **except** within color functions:

```css
linear-gradient(to bottom, black 1px, rgba(255,255,255,0.2) 1px)
```

- Always add a space before `!important`.

### Omit units on 0 values

Do this:

```css
margin: 0;
```

Not this:

```css
margin: 0px;
```

### Use expanded syntax

It is often harder to understand what the shorthand is doing and the
shorthand can also hide some unwanted default values. It is good to
privilege expanded syntax to make your intentions explicit.

Do this:

```css
border-color: red;
```

Not this:

```css
border: red;
```

### Put multiple selectors on different lines

Do this:

```css
h1,
h2,
h3 {
  font-family: sans-serif;
  text-align: center;
}
```

Not this:

```css
h1, h2, h3 {
  font-family: sans-serif;
  text-align: center;
}
```

### Naming standards for class names

- `lower-case-with-dashes` is the most common.
- But `camelCase` is also used sometimes. Try to follow the style of
  existing or related code.

### Other tips

- Assume `="true"` in attribute selectors.

  - Example: Use `option[checked]`, not `option[checked="true"]`.

- Avoid ID selectors unless it is really the wanted goal, since IDs
  have higher specificity and therefore are harder to override.

- Using descendant selectors is good practice for performance when
  possible:

  - For example:
    `.autocomplete-item[selected] > .autocomplete-item-title` would
    be more efficient than
    `.autocomplete-item[selected] .autocomplete-item-title`

## Overriding CSS

Before overriding any CSS rules, check whether overriding is really
needed. Sometimes, when copy-pasting older code, it happens that the
code in question contains unnecessary overrides. This could be because
the CSS that it was overriding got removed in the meantime. In this
case, dropping the override should work.

It is also good practice to look at whether the rule you are overriding
is still needed: maybe the UX spec for the component has changed and
that rule can actually be updated or removed. When this is the case,
don't be afraid to remove or update that rule.

Once the two things above have been checked, check if the other rule you
are overriding contains `!important`, if that is case, try putting it
in question, because it might have become obsolete.

Afterwards, check the specificity of the other selector; if it is
causing your rule to be overridden, you can try reducing its
specificity, either by simplifying the selector or by changing where the
rule is placed in the stylesheet. If this isn't possible, you can also
try introducing a `:not()` to prevent the other rule from applying,
this is especially relevant for different element states (`:hover`,
`:active`, `[checked]` or `[disabled]`). However, never try to
increase the selector of the rule you are adding as it can easily become
hard to understand.

Finally, once you have checked all the things above, you can permit
yourself to use `!important` along with a comment why it is needed.

### An unlayered declaration outranks every cascade layer

A declaration inside a cascade layer loses to every unlayered one, whatever the
specificity. The generated design-token sheets put their high contrast overrides
in layers, so a component or an embedder that ships its own unlayered copy of
some tokens overrides all of those overrides at once.

That is how high contrast support goes silently dead for one surface, with
nothing in the diff to show for it. The shape to look for is an unlayered copy
carrying a `@media (forced-colors)` counterpart but no
`@media (prefers-contrast)` one: on Windows it mostly still works, because
forced colors is the regime there, while on Linux and macOS, where
`prefers-contrast` is the only signal reaching content, the surface loses high
contrast entirely.

[Scoping token overrides](/accessible/HCMMediaQueries.md#scoping-token-overrides-with-media-not-forced-colors)
covers the companion case: an override that does sit in a layer still needs
`@media (not (forced-colors))` to leave the token layer's system color in place.

### Overriding across a shadow boundary

A custom property is substituted where it is declared. A variable declared on
`:host, :root` in terms of another variable captures the root's value, so an
embedder overriding the inner variable further down the tree does not change
it. When an override "isn't working", check whether the consuming declaration
already resolved at the root.

An outer-tree `::part()` rule beats the widget's own shadow rule, whatever the
specificity inside the shadow root. An embedder's unscoped `widget::part(x)`
rule therefore overrides that widget's internal metrics for every instance on
the page, including instances it was never written for, so scope such a rule to
a class the embedder's own instances opt into. A part inside a nested shadow
root is out of reach of a page `::part()` selector altogether.

## Using CSS variables

### Adding new variables

Before adding new CSS variables, please consider the following
questions:

1. **Is the variable value changed at runtime?**
   *(Either from JavaScript or overridden by another CSS file)*
   **If the answer is no**, consider using a preprocessor variable or
   inlining the value.
2. **Is the variable value used multiple times?**
   **If the answer is no and the value isn't changed at runtime**, then
   you likely don't need a CSS variable.
3. **Is there an alternative to using the variable like inheriting or
   using the \`\`currentcolor\`\` keyword?**
   Using inheriting or using `currentcolor` will prevent repetition of
   the value and it is usually good practice to do so.

In general, it's good to first think of how some CSS could be written
cleanly without the CSS variable(s) and then think of how the CSS
variable could improve that CSS.

A custom property defined in a shipped stylesheet and never referenced from CSS
fails `browser_parsable_css.js` unless an entry in that test's
`propNameAllowlist` matches it. An allowlist entry that no longer matches
anything fails the test too, on the platforms the entry applies to.

### Using variables

#### Use the variable according to its naming

Do this:

```css
xul|tab:hover {
  background-color: var(--in-content-box-background-hover);
}
```

Not this:

```css
#certificateErrorDebugInformation {
  background-color: var(--in-content-box-background-hover);
}
```

## Localization

### Text Direction

- For margins, padding and borders, use
  `inline-start`/`inline-end` rather than `left`/`right`.
  *Example:* Use `margin-inline-start: 3px;` instead of
  `margin-left: 3px`.
- For RTL-aware positioning (left/right), use
  `inset-inline-start`/`inset-inline-end`.
- For RTL-aware float layouts, `float: inline-start|inline-end` can
  be used instead of `float: left|right`.
- The RTL-aware equivalents of
  `border-{top/bottom}-{left/right}-radius` are
  `border-{start/end}-{start/end}-radius`
- When there is no special RTL-aware property available, use the pseudo
  `:-moz-locale-dir(ltr|rtl)` (for XUL files) or `:dir(ltr|rtl)`
  (for HTML files).
- Remember that while a tab content's scrollbar still shows on the
  right in RTL, an overflow scrollbar will show on the left.
- Write `padding: 0 3px 4px;` instead of
  `padding: 0 3px 4px 3px;`. This makes it more obvious that the
  padding is symmetrical (so RTL won't be an issue).

:::{note}
See [CSS Logical Properties and
Values](https://developer.mozilla.org/en-US/docs/Web/CSS/CSS_Logical_Properties)
for more information.
:::

## Writing cross-platform CSS

Firefox supports many different platforms and each of those platforms
can contain many different configurations:

- Windows 7, 8 and 10

  - Default theme
  - Aero basic (Windows 7, 8)
  - Windows classic (Windows 7)
  - High contrast (All versions)

- Linux

- macOS

### File structure

- The `browser/` directory contains styles specific to Firefox
- The `toolkit/` directory contains styles that are shared across all
  toolkit applications (Thunderbird and SeaMonkey)

Under each of those two directories, there is a `themes` directory
containing 4 sub-directories:

- `shared`
- `linux`
- `osx`
- `windows`

The `shared` directories contain styles shared across all 3 platforms,
while the other 3 directories contain styles respective to their
platform.

For new CSS, when possible try to privilege using the `shared`
directory, instead of writing the same CSS for the 3 platform specific
directories, especially for large blocks of CSS.

#### Content CSS vs. Theme CSS

The following directories also contain CSS:

- `browser/base/content/`
- `toolkit/content/`

These directories contain content CSS, that applies on all platforms,
which is styling deemed to be essential for the browser to behave
correctly. To determine whether some CSS is theme-side or content-side,
it is useful to know that certain CSS properties are going to lean one
way or the other: color - 99% of the time it will be theme CSS, overflow
\- 99% content.

| 99% theme                                                          | 70% theme                    | 70% content                                              | 99% content                                                                 |
| ------------------------------------------------------------------ | ---------------------------- | -------------------------------------------------------- | --------------------------------------------------------------------------- |
| font-\*, color, \*-color, border-\*, -moz-appearance [^footnote-1] | line-height, padding, margin | cursor, width, max-width, top, bottom [^footnote-2], etc | overflow, direction, display, \*-align, align-\*, \*-box-\*, flex-\*, order |

If some CSS is layout or functionality related, then it is likely
content CSS. If it is esthetics related, then it is likely theme CSS.

When importing your stylesheets, it's best to import the content CSS
before the theme CSS, that way the theme values get to override the
content values (which is probably what you want), and you're going to
want them both after the global values, so your imports will look like
this:

```html
<?xml-stylesheet href="chrome://global/skin/global.css" type="text/css"?>
<?xml-stylesheet href="chrome://browser/content/path/module.css" type="text/css"?>
<?xml-stylesheet href="chrome://browser/skin/path/module.css" type="text/css"?>
```

[^footnote-1]: -moz-appearance is tricky. Generally, when specifying
    -moz-appearance: foo; you're giving hints as to how something should
    act, however -moz-appearance: none; is probably saying 'ignore
    browser preconceptions - I want a blank sheet', so that's more
    visual. However -moz-appearance values aren't implemented and don't
    behave consistently across platforms, so idealism aside
    -moz-appearance should always be in theme CSS.

[^footnote-2]: However there is probably a better way than using absolute
    positioning.

### Chrome versus in-content

A stylesheet shared between a chrome window and an `about:` page does not behave
the same in both, and most of the differences are invisible in the source.

- **`rem` means something different in each.** Chrome's root font size comes from
  the system UI font (`:root { font: message-box }`), so it is 11 to 15px
  depending on the platform and never the 16px a generic page gets. A privileged
  `about:` page is not 16px either: `about:newtab` sets
  `--font-size-root: 15px`. One shared rem-valued value therefore resolves to
  three different lengths across chrome, New Tab and the web. `em` is affected
  the same way in most cases, since the font size it resolves against is
  system-dependent too. Hardcoded `px` font sizes are a lint error in any case:
  the `use-design-tokens` rule covers `font-size`. Menus and many popups reset
  `font` to `menu`, `message-box` or `-moz-pull-down-menu` (`menu.css`,
  `xul.css`), so a change to the root font size does not reach their text,
  while their `rem` lengths still follow the root.
- **Chrome-only conditions never match in a page.**
  `:root[uidensity="compact"]`, the `:root[lwt-*]` theme selectors, anything
  relying on `xul.css` defaults such as `user-select` or `-moz-user-focus`, and
  anything keyed on a XUL-only attribute. From the page's point of view a shared
  sheet's chrome branches are dead code, so check which realm a rule can reach
  before fixing it, and do not assume a compact-density rule protects the in-page
  case.

### UI density

`browser.uidensity` is 0 for normal, 1 for compact and 2 for touch density
(`gUIDensity` in `browser.js`). Compact and touch set `uidensity="compact"` or
`uidensity="touch"` on the window's root, and normal density removes the
attribute. The pref is not the only input:

- With Nova enabled and no user value for `browser.uidensity`, a window
  compacts itself when it is small: when 36px is more than
  `browser.compactmode.auto.threshold` (0.05) of its height, i.e. under 720px,
  or, while the sidebar launcher is shown collapsed, when 56px is more than that
  share of its width, i.e. under 1120px. Popups and windows sized for
  fingerprinting resistance skip this.
- In Windows tablet mode, touch density takes over when the density is
  automatic in that same sense, or when it is normal and
  `browser.touchmode.auto` is set.

### Colors

For common areas of the Firefox interface (panels, toolbar buttons,
etc.), mozilla-central often comes with some useful CSS variables that
are adjusted with the correct values for different platform
configurations, so using those CSS variables can definitively save some
testing time, as you can assume they already work correctly.

Using the `currentcolor` keyword or inheriting is also good practice,
because sometimes the needed value is already in the color or on the
parent element. This is especially useful in conjunction with icons
using `-moz-context-properties: fill;` where the icon can adjust to
the right platform color automatically from the text color. It is also
possible to use `currentcolor` with other properties like
`opacity` or `fill-opacity` to have different
opacities of the platform color.

#### Colors that resolve per realm

Two more axes resolve differently in chrome than in the content area, with no
preference that looks wrong on either side:

- **The color scheme.** Chrome's comes from `browser.theme.toolbar-theme`
  (`PreferenceSheet::ColorSchemeSettingForChrome`): 0 for dark, 1 for light and
  anything else for system. Content's comes from
  `layout.css.prefers-color-scheme.content-override`, with the same 0 and 1,
  and for any other value from `browser.theme.content-theme`
  (`ThemeDerivedColorSchemeForContent`), read the same way. While a page's
  colors are replaced by the user's preferred colors, content instead takes the
  scheme from how dark that background color is (`PreferenceSheet::Prefs::Load`).
  `LightweightThemeConsumer` writes both `browser.theme.*` prefs when
  `updateGlobalThemeData` is true, so these updates can replace values set at
  launch. AI windows and the private-window theme skip these writes, and so
  does darkening a private window while the system scheme is light. For
  `toolbar-theme`, a theme with a dark variant, Alpenglow for one, gets 0 or 1
  for the variant in use. The default theme counts as having one only under a
  dark system scheme, where it gets 0. Otherwise `color_scheme` selects dark
  (0), light (1) or system (2); the built-in Light and Dark themes declare one.
  Without a declared scheme, the default and in-app themes get 2 and any other
  theme gets how dark its toolbar is. `ui.systemUsesDarkTheme` overrides the
  system scheme. With `browser.theme.unified-color-scheme` false,
  `content-theme` takes `content_color_scheme` or `color_scheme`, and 2 when
  neither declares a scheme; when that pref is true, `content-theme` follows the
  toolbar scheme. The two realms can disagree, so one `light-dark()` value
  resolves to different sides in them.
  Under Windows high contrast mode the light set is additionally forced
  (`mMustUseLightSystemColors`): always in chrome, and in content only once
  colors are being forced there.
- **Stand-ins for native colors.** With `privacy.resistFingerprinting` or
  `ui.use_standins_for_native_colors`, a content document's system colors are
  fixed stand-in values rather than the OS ones, while chrome keeps the real
  ones. A fix built on system colors can therefore look right on your own
  machine and still be wrong for a user who resists fingerprinting.

[Colors and high contrast mode](/accessible/ColorsAndHighContrastMode.md)
describes how Gecko picks between those palettes in `PreferenceSheet::Load`.

#### Focus rings

The UA's `:focus-visible` rule sets `outline: 1px auto` (`ua.css`), and an
`auto` outline paints its ring in the element's `accent-color`, subject to
forced colors, whatever `outline-color` says (`Theme::PaintAutoStyleOutline`).
A rule that sets only `outline-color` therefore changes nothing on screen,
although the computed value reads back as set. Set `outline-style` together
with the color, or use the `outline` shorthand.

### High contrast mode

Four regimes act on the same stylesheet independently: forced colors,
`prefers-contrast`, the color scheme, and the fingerprinting-resistance
stand-ins. Each of them resolves differently in a chrome document than in a
page, and two of them differ by platform. So a widget shared between the toolbar
and an `about:` page can sit in a different regime on each side, and naming the
regime each side is in comes before calling the difference a bug.

[HCM media queries](/accessible/HCMMediaQueries.md) says which query to write
for which audience, and the [HCM CSS self-check guide](/accessible/HCMCSSChecklist.md)
is the checklist a patch is reviewed against. What follows is how the regimes
themselves resolve.

Forced colors and `prefers-contrast` are separate signals with separate sources.

Forced colors has three states rather than two
(`nsPresContext::UpdateForcedColors`):

- `active` - colors are being forced, and the forced-color adjustment described
  below rewrites author color declarations. A content document reaches this
  state through `browser.display.document_color_use`:

  | value | meaning | default on |
  | ----- | ------------------------------------ | ------------- |
  | 0     | mirror the OS high contrast setting  | Windows       |
  | 1     | never force colors                   | Linux, macOS  |
  | 2     | always force colors                  | -             |

- `requested` - a chrome document on Windows while the OS is in high contrast
  mode. `@media (forced-colors)` matches, but colors are not forced and no
  declaration is rewritten. The state exists so that chrome can reuse its
  forced-colors rules rather than spelling out `(forced-colors) or
  ((-moz-platform: windows) and (prefers-contrast))`. In chrome, then,
  `forced-colors` is a request to reduce the palette to system colors, which the
  stylesheet has to honor itself.
- `none` - every other case.

A bare `@media (forced-colors)` therefore matches in both the `active` and the
`requested` state, so the query matching does not mean the adjustment is
running. Ask for `(forced-colors: active)` where that difference matters;
`forced-colors: requested` only parses in a chrome stylesheet.

`prefers-contrast` has no such split. It tracks
`LookAndFeel::IntID::UseAccessibilityTheme` in both realms, which is Windows high
contrast mode, the macOS "Increase contrast" setting, a GTK high-contrast theme,
or the desktop portal's `prefers-contrast` setting. It is also graded off the
measured contrast ratio of the realm's own default colors
(`Gecko_MediaFeatures_PrefersContrast`): below 4.5 it reports `less`, at or above
7 it reports `more`, and `custom` in between. `(prefers-contrast)` can therefore
match while `(prefers-contrast: more)` does not, and a rule written against
`more` does nothing under a mid-contrast theme.

Taken together, on Windows a shared widget's in-page copy has its colors
rewritten for it while the toolbar copy is only asked to use system colors, and
everywhere else neither copy is forced and `prefers-contrast` is the only signal
reaching either. "Only in the in-page copy" and "only on Windows" are often the
same finding.

#### Content area

On Windows high contrast mode, in the content area, Gecko does some
automatic color adjustments regarding page colors. Part of those
adjustments include making all `box-shadow` invisible, so this is
something to be aware of if you create a focus ring or a border using
the `box-shadow` property: consider using a `border` or an
`outline` if you want the border/focus ring to stay visible in
high-contrast mode. An example of such bug is [bug
1516767](https://bugzilla.mozilla.org/show_bug.cgi?id=1516767).

Another adjustment to be aware of is that Gecko drops a `background-image`
with any layer other than a `url()` when colors are forced, so a gradient
disappears (see "The forced-color adjustment" below). Consider using
an actual `<img>` tag (for HTML documents) or `list-style-image`
(for XUL documents) if rendering the image is important.

If you are not using Windows, one way to test against those adjustments
on other platforms is:

- Going to <about:preferences>
- Clicking on the "Colors..." button in the "Fonts & Colors"
  sub-section of the "Language and Appearance" section
- Under "Override the colors specified by the page with your selections
  above", select the "Always" option

That option is `browser.display.document_color_use` set to 2. Setting the pref
directly does the same thing, and `ui.useAccessibilityTheme` set to 1 turns
`prefers-contrast` on in both realms. Both take effect without a restart, so
together they reach every regime from a machine that is not in high contrast
mode. Resetting `ui.useAccessibilityTheme` to its default value does not always
take effect immediately, so re-check the media query rather than assuming it
flipped back, and restart the browser if it did not.

#### Chrome area

The automatic adjustments previously mentioned only apply to pages
rendered in the content area. The chrome area of Firefox uses colors as
authored, which is why using pre-defined variables, `currentcolor` or
inheritance is useful to integrate with the system theme with little
hassle.

If not, as a last resort, using [system
colors](https://developer.mozilla.org/en-US/docs/Web/CSS/color_value#system_colors)
also works for non-default Windows themes or Linux. In general, the
following colors are used:

- `-moz-Field`: textbox or field background colors, also used as the
  background color of listboxes or trees.
- `-moz-FieldText`: textbox or field text colors, also used as the
  text color of listboxes or trees.
- `-moz-Dialog`: window or dialog background color.
- `-moz-DialogText`: window or dialog text color.
- `GrayText`: used on disabled items as text color. Do not use it on
  text that is not disabled to deemphasize text, because it does not
  guarantee a sufficient contrast ratio for non-disabled text.
- `ThreeDShadow`: Used as border on elements.
- `ThreeDLightShadow`: Used as light border on elements.

Using the background/text pairs is especially important to ensure the
contrast is respected in all situations. Never mix custom text colors
with a system background color and vice-versa.

Note that using system colors is only useful for the chrome area, since
content area colors are overridden by Gecko anyway.

To put a window's chrome under forced colors on any platform, set
`window.browsingContext.forcedColorsOverride = "active"` from chrome JS. The
override is checked before any pref (`nsPresContext::UpdateForcedColors`), so
the chrome reaches the `active` state and its declarations are rewritten as
described below. The override applies to the top-level browsing context, and
each tab's browser is its own top-level context, so the pages stay out. Windows
high contrast mode puts chrome in the `requested` state instead. Adding
`forced-color-adjust: none` on the chrome root stops the rewriting, but the
state stays `active`: a bare `(forced-colors)` query still matches, while
`(forced-colors: active)` and `(forced-colors: requested)` queries answer the
other way round from Windows. The override does not make `prefers-contrast`
match either. On Windows, setting `ui.useAccessibilityTheme` to 1 without the
override reaches the `requested` state and matches `prefers-contrast`, but the
system colors stay those of the OS theme.

#### The forced-color adjustment

Once a document is actually forcing colors - the `active` state above, not
`requested` - the cascade rewrites author color
declarations (`servo/components/style/properties/cascade.rs`). Three things are
exempt: rules from the UA and User origins, anything under
`forced-color-adjust: none`, and CSS-wide keywords. Everything else is filtered,
and the filter is not the same for every property:

- A bare system color and `currentcolor` are honored in every color property
  except the ones listed below as reverting whatever they are set to.
- `transparent` is honored for `background-color` and `color`, but not for
  `border-color`; that carve-out was removed in [bug
  1740924](https://bugzilla.mozilla.org/show_bug.cgi?id=1740924).
  `outline-color`, `column-rule-color`, `text-decoration-color`,
  `text-emphasis-color`, `-webkit-text-fill-color` and
  `-webkit-text-stroke-color` follow the `border-color` rule.
- A `var()` is judged by its substituted value.
- A `light-dark()`, `color-mix()` or `contrast-color()` is kept when every color
  inside it would be kept (`Color::honored_in_forced_colors_mode`). So
  `color-mix(in srgb, currentcolor 25%, transparent)` survives as a
  `background-color` and is replaced as a `border-color`, and one variable reads
  as a faint fill behind a box and a solid `CanvasText` line around it. A
  relative color such as `rgb(from ButtonText r g b / 50%)` is rejected even
  when its origin is a system color, unless it resolves to `transparent` where
  `transparent` is honored.
- A rejected `background-color` becomes the canvas background with its alpha
  preserved. Any other rejected color reverts to its UA value.
- `box-shadow`, `text-shadow`, `accent-color`, `caret-color`,
  `scrollbar-color`, `color-scheme` and `border-image-source` revert to their UA
  value whatever they are set to.
- A `background-image` with only `url()` and `none` layers is kept, as long as
  `browser.display.permit_backplate` is on (the default). Any other layer, such
  as a gradient, reverts the whole declaration to `none`.
- `fill`, `stroke`, `stop-color`, `flood-color`, `lighting-color`, `opacity`
  and `filter` are not filtered at all. An icon follows the regime only through
  `currentcolor` or a system color in its own `fill` or `stroke`, including one
  it receives as `context-fill` or `context-stroke`.

The filter therefore fails in both directions, and both shapes exist in the tree:

- **A line drawn with `background-color` vanishes.** An `hr` styled as a 1px
  separator, a `::before` hairline or a divider `div` each become exactly the
  surface they sit on. Drawing the line as a border survives instead, because a
  rejected border reverts to its UA value: `ButtonBorder` on text inputs,
  `textarea`, `select` and buttons (`-moz-buttonhoverborder` on hover,
  `-moz-buttonactiveborder` on `:active:hover`), `ThreeDFace` on `fieldset`,
  `ThreeDShadow` and `ThreeDHighlight` on `progress`, and `currentcolor`
  elsewhere, radio, checkbox and range inputs included.
  The swap is not geometrically free,
  though: a used border width floors to whole device pixels where a height does
  not, so keep the box's block size and add `box-sizing: border-box`.
- **A transparent border used as a spacer becomes a frame.** The
  `border: Npx solid transparent` idiom with `background-clip: padding-box`,
  which insets a background while keeping the full-height hit area, paints an
  `Npx` `CanvasText` box around every element that uses it. Give it a system
  color matching the surface behind it under `@media (forced-colors: active)`,
  and check what that surface actually computes to rather than assuming it is
  `Canvas`.

`forced-color-adjust: none` is not a per-property escape hatch. It inherits, so
it exempts the element and its whole subtree; reach for it only where the colors
themselves are the content.

### Writing media queries

#### Boolean media queries

Do this:

```css
@media (-moz-mac-yosemite-theme: 0) {
```

Not this:

```css
@media not all and (-moz-mac-yosemite-theme) {
```

#### Privilege CSS for most common configuration

It is better to put the most common configuration (latest version of an
OS, or default theme for example) outside of the media query. In the
following example, `-moz-mac-yosemite-theme` targets macOS 10.10 and
higher, so it should be privileged over the styling for macOS 10.9.

Do this:

```css
@media (-moz-mac-yosemite-theme: 0) {
  #placesList {
    box-shadow: inset -2px 0 0 hsla(0,0%,100%,.2);
  }
}
```

Not this:

```css
#placesList {
  box-shadow: inset -2px 0 0 hsla(0,0%,100%,.2);
}

@media (-moz-mac-yosemite-theme) {
  #placesList {
    box-shadow: none;
  }
}
```

#### Chrome-only media features go by the stylesheet, not the document

Whether chrome-only features are available is a parse-time property of the
stylesheet's URL (`chrome_rules_enabled()`), not of the document the sheet ends
up in. Chrome rules parse in `chrome://` and `resource://` sheets, in `about:`
sheets that are not content-accessible (`URLExtraData::ChromeRulesEnabled`),
and in UA and user sheets. So `-moz-native-theme`, `-moz-pref()` and the rest
still evaluate inside a `chrome://` stylesheet loaded into an `about:` page.
Meanwhile `matchMedia("(-moz-native-theme)")` called from that page's own script parses at
content origin, does not know the feature, and always reports false.

Reading the page's `matchMedia` and concluding the sheet took the other branch is
the trap. To find out which branch a chrome sheet took, check a custom property
that only that branch sets.

## Theme support

Besides the default theme, Firefox comes with three built-in themes
(`BuiltInThemeConfig.sys.mjs`): Light and Dark, which are in-app themes, and
Alpenglow, which behaves like a WebExtension theme. In addition to this, Firefox
supports a variety of WebExtension themes that can be installed from AMO. For
testing purposes, [here is an example of a WebExtension
theme.](https://addons.mozilla.org/en-US/firefox/addon/arc-dark-theme-we/)

`LightweightThemeConsumer.sys.mjs` applies the theme to a window. In ordinary
windows, `theme-in-app` on the root marks a selected default or in-app theme,
`lwtheme` marks an effective WebExtension theme, and `theme-effective-id` names
the effective theme. AI windows with cached theme data always set
`theme-in-app`. It sets `forceNonNativeTheme` on the document for every
effective theme but the default.
While `(forced-colors)` matches, it applies the default theme in place of any
other, unless `browser.theme.forced-colors-override.enabled` is false. With a
WebExtension theme selected, this substitution can leave both `theme-in-app`
and `lwtheme` absent in an ordinary window.

For a WebExtension theme, Alpenglow included, the theme's colors are written
inline on the root element as custom properties, through `ThemeVariableMap.sys.mjs`
and the toolkit map in `LightweightThemeConsumer.sys.mjs`. The default and in-app
themes write none, so their colors come from the app's own CSS. A themed surface
follows a WebExtension theme only where its stylesheet reads the custom property
the map writes. A rule that redefines that property on a descendant replaces the
theme color there, as `#DateTimePickerPanel` does in `content-area.css`. So check
the stylesheet that styles a wrong-looking surface before the theme's manifest.

### Writing theme-friendly CSS

- Some CSS variables that are pre-adjusted for different platforms are
  also pre-adjusted for themes, so it's again a good idea to use them
  for theme support.

- The text color of elements often contains valuable information from
  the theme colors, so `currentcolor`/inheritance is again a good
  idea for theme support.

- Never write CSS specially for the built-in Light and Dark themes, for
  example under `:root[theme-in-app]`, unless that CSS isn't supposed to affect
  WebExtension themes.

- These selectors can be used to target themed areas, though in general it's
  recommended to try to avoid them and use `light-dark()` to get the right
  colors automatically:

  - `:root[lwt-toolbar-field="light/dark"]`: explicitly light or dark address bar and
    searchbar.
  - `:root[lwt-toolbar-field-focus="light/dark"]`: explicitly light or dark address bar and
    searchbar in the focused state.
  - `:root[lwt-popup="light/dark"]`: explicitly light or dark arrow panels
    and autocomplete panels.
  - `:root[lwt-sidebar="light/dark"]`: explicitly light or dark sidebars.

- If you'd like a different shade of a themed area and no CSS variable
  is adequate, using colors with alpha transparency is usually a good
  idea, as it will preserve the original theme author's color hue.

### Variables

For clarity, CSS variables that are only used when a theme is enabled
have the `--lwt-` prefix.

## Layout & performance

### Layout

Mixing XUL flexbox and HTML flexbox can lead to undefined behavior.

### CSS selectors

When targeting the root element of a page, using `:root` is the most
performant way of doing so.

### Reflows and style flushes

See {doc}`/performance/bestpractices`
for more information about this.

## Misc

### Text aliasing

When convenient, avoid setting the `opacity` property on
text as it will cause text to be aliased differently.

### HDPI support

It's recommended to use SVG since it keeps the CSS clean when supporting
multiple resolutions. See the {doc}`svg_guidelines` for more information
on SVG usage.

However, if only 1x and 2x PNG assets are available, you can use this
`@media` query to target higher density displays (HDPI):

```css
@media (min-resolution: 1.1dppx)
```

#### Some properties are rounded to device pixels, but not others

A declared `border-*-width`, `outline-width` or `column-rule-width` is rounded
down to whole device pixels when it is computed, with a floor of one device
pixel for a width that is not zero (`snap_as_border_width`). A width left at its
initial `medium` value is not rounded and stays 3 CSS pixels. `outline-offset`
is rounded the same way where `layout.css.outline-offset.snapping` asks for it:
1 rounds it everywhere and is the Nightly default, and 2 rounds it only in
chrome documents and is the default elsewhere. `padding`, `margin`, `gap` and
`height` are not rounded. Trading a border for padding of the same length, or a
1px-tall background box for a 1px border, is therefore not geometry-neutral
unless the length lands on whole device pixels, and what has to be integral is
the length in *device* pixels rather than in CSS pixels. A whole number of CSS
pixels is not exempt: at a device pixel ratio of 1.25 a `1px` border is used as
0.8px while `1px` of padding stays 1px, so the box changes size and everything
after it moves.

The consequences are easy to miss because the default ratio hides them, so set
`layout.css.devPixelsPerPx` (a string pref; `"-1.0"` restores automatic) and
check the fractional ratios 1.25, 1.5 and 1.75 before calling such a swap neutral.
A device pixel is a whole number of app units, of which a CSS pixel has 60.
`ComputeAppUnitsPerDevPixelForWidgetScale` rounds 60 divided by the scale to the
nearest integer, at least 1, and the ratio you get is 60 divided by that
integer, which is not always the representable ratio nearest the one you asked
for: `devPixelsPerPx` set to 1.75 runs at 60/34, about 1.7647.
Where padding has to reproduce the snapping, `round(down, X, env(hairline))`
does it, with a `max(env(hairline), ...)` guard against rounding to zero;
`browser/themes/shared/urlbar.css` uses that form. The CSSWG has
[resolved on `round(line-width, ...)`](https://github.com/w3c/csswg-drafts/issues/3720#issuecomment-3999235838)
as the syntax for this, which nothing implements yet. Keeping the original height
and adding `box-sizing: border-box` is the other way to hold a box exactly.

`env(hairline)` is one device pixel. Unlike the chrome-only media features that
also go by origin, it uses the stylesheet's URL alone. It resolves in a sheet
with a `chrome://`, `resource://` or eligible `about:` URL, in either realm,
`about:newtab`'s own CSS included. UA or user origin alone does not enable it,
so a `file:` sheet such as `userChrome.css` gets no value. Where it has no
value, the declaration behaves as `unset`:
a `padding` or a `margin` computes to 0, a `border` shorthand loses its style
and reads 0px, and a `border-width` longhand with its style declared separately
falls back to `medium` (3px).
