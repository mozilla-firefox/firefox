# RTL Guidelines

RTL languages such as Arabic, Hebrew, Persian and Urdu are read and
written from right-to-left, and the user interface for these languages
should be mirrored to ensure the content is easy to understand.

When a UI is changed from LTR to RTL (or vice-versa), it’s often called
mirroring. An RTL layout is the mirror image of an LTR layout, and it
affects layout, text, and graphics.

In RTL, anything that relates to time should be depicted as moving from
right to left. For example, forward points to the left, and backwards
points to the right.

## Mirroring layout

When a UI is mirrored, these changes occur:

- Text fields icons are displayed on the opposite side of a field
- Navigation buttons are displayed in reverse order
- Icons that communicate direction, like arrows, are mirrored
- Text is usually aligned to the right

In CSS, while it's possible to apply a rule for LTR and a separate one
specifically for RTL, it's usually better to use CSS [Logical Properties](https://developer.mozilla.org/en-US/docs/Web/CSS/CSS_Logical_Properties)
which provide the ability to control layout through logical, rather than
physical mappings.

```{eval-rst}
+---------------------------------------------------------+--------------------------------------------------+
| Do                                                      | Don't do                                         |
+---------------------------------------------------------+--------------------------------------------------+
| ``margin-inline-start: 5px``                            | ``margin-left: 5px``                             |
+---------------------------------------------------------+--------------------------------------------------+
| ``padding-inline-end: 5px``                             | ``padding-right: 5px``                           |
+---------------------------------------------------------+--------------------------------------------------+
| ``float: inline-start``                                 | ``float: left``                                  |
+---------------------------------------------------------+--------------------------------------------------+
| ``inset-inline-start: 5px``                             | ``left: 5px``                                    |
+---------------------------------------------------------+--------------------------------------------------+
| ``border-inline-end: 1px``                              | ``border-right: 1px``                            |
+---------------------------------------------------------+--------------------------------------------------+
| ``border-{start/end}-{start/end}-radius: 2px``          | ``border-{top/bottom}-{left/right}-radius: 2px`` |
+---------------------------------------------------------+--------------------------------------------------+
| ``padding: 1px 2px``                                    | ``padding: 1px 2px 1px 2px``                     |
+---------------------------------------------------------+--------------------------------------------------+
| ``margin-block: 1px 3px`` && ``margin-inline: 4px 2px`` | ``margin: 1px 2px 3px 4px``                      |
+---------------------------------------------------------+--------------------------------------------------+
| ``text-align: start`` or ``text-align: match-parent``   | ``text-align: left``                             |
| (depends on the context)                                |                                                  |
+---------------------------------------------------------+--------------------------------------------------+
```

When there is no special RTL-aware property available, or when
left/right properties must be used specifically for RTL, use the pseudo
`:-moz-locale-dir(rtl)` (for XUL documents) or `:dir(rtl)` (for HTML
documents).

For example, this rule covers LTR to display searchicon.svg 7 pixels
from the left:

```css
.search-box {
  background-image: url(chrome://path/to/searchicon.svg);
  background-position: 7px center;
}
```

but an additional rule is necessary to cover RTL and place the search
icon on the right:

```css
.search-box:dir(rtl) {
  background-position-x: right 7px;
}
```

:::{warning}
It may be inappropriate to use logical properties when embedding LTR
within RTL contexts. This is described further in the document.
:::

## Mirroring elements

RTL content also affects the direction in which some icons and images
are displayed, particularly those depicting a sequence of events.

### What to mirror

- Icons or animations that imply directionality or motion like
  back/forward buttons or progress bars
- Icons that imply text direction, like
  {searchfox}`reader-mode.svg <mozilla-central/rev/f9beb753a84aa297713d1565dcd0c5e3c66e4174:browser/themes/shared/icons/reader-mode.svg>`
- Icons that imply location of UI elements in the screen, like
  {searchfox}`sidebar-collapsed-right.svg <firefox-main/rev/b9c3ba706c41c42a7a1b4f6e5b5ba878782b3ab2:browser/themes/shared/icons/sidebar-collapsed-right.svg>`,
  {searchfox}`open-in-new.svg <mozilla-central/rev/f9beb753a84aa297713d1565dcd0c5e3c66e4174:toolkit/themes/shared/icons/open-in-new.svg>`,
  {searchfox}`default theme's preview.svg <mozilla-central/rev/f9beb753a84aa297713d1565dcd0c5e3c66e4174:toolkit/mozapps/extensions/default-theme/preview.svg>`
  or
  {searchfox}`pane-collapse.svg <mozilla-central/rev/74cc0f4dce444fe0757e2a6b8307d19e4d0e0212:devtools/client/debugger/images/pane-collapse.svg>`
- Icons representing objects that are meant to be handheld should look
  like they're being right-handed, like the {searchfox}`magnifying glass icon <mozilla-central/rev/e7c61f4a68b974d5fecd216dc7407b631a24eb8f:toolkit/themes/windows/global/icons/search-textbox.svg>`
- Twisties in their collapsed state. Note that if the context in which
  they appear is LTR (e.g. code in a devtools HTML view), they should
  not be mirrored, even if the user might be using an RTL locale.

### What NOT to mirror

- Text/numbers
- Icons containing text/numbers
- Icons/animations that are direction neutral
- Icons that wouldn't look differently if they'd be mirrored, like {searchfox}`X buttons <mozilla-central/rev/a78233c11a6baf2c308fbed17eb16c6e57b6a2ac:devtools/client/debugger/images/close.svg>`
  or the {searchfox}`bookmark star <mozilla-central/rev/a78233c11a6baf2c308fbed17eb16c6e57b6a2ac:browser/themes/shared/icons/bookmark-hollow.svg>`
  icon, or any other symmetric icon
- Icons that should look the same as LTR, like icons related to code
  (which is always LTR) like
  {searchfox}`tool-webconsole.svg <mozilla-central/rev/74cc0f4dce444fe0757e2a6b8307d19e4d0e0212:devtools/client/themes/images/tool-webconsole.svg>`
- Checkmark icons
- Video/audio player controls
- Product logos
- Order of size dimensions (e.g., `1920x1080` should not become
  `1080x1920`)
- Order of size units (e.g., `10 px` should not become `px 10`
  (unless the size unit is localizable))

### How

The most common way to mirror images is by flipping the X axis:

```css
transform: scaleX(-1);
```

Or, if you're already using `transform` with a different value on the same
element, you can also use `scale`:

```css
scale: -1 1;
```

Note that mirroring images that way doesn't work when the image is a part of
an element with text using `background-image`, because then the text would
be mirrored along with the image, and the image would be positioned incorrectly.
For such cases, try to use a different method for displaying the image,
like having it as an element all on its own.
If that's not possible, add a separate pre-mirrored image asset and specify
it in a separate `:dir(rtl)` rule:

```css
.element-with-icon {
  background-image: url("path/to/image/image.svg");
}

.element-with-icon:dir(rtl) {
  background-image: url("path/to/image/image-rtl.svg");
}
```

For animations like a progress bar, when using `@keyframes` to change
the `transform: translateX()` states, make sure to add a different
`@keyframes` suited for RTL, and target that in a separate `:dir()` rule:

```css
#progressbar {
  animation: progressbar-animation 1s linear infinite;
}

#progressbar:dir(rtl) {
  animation-name: progressbar-animation-rtl;
}

@keyframes progressbar-animation {
  0% {
    transform: translateX(-100px);
  }
  100% {
    transform: translateX(0);
  }
}

@keyframes progressbar-animation-rtl {
  0% {
    transform: translateX(100px);
  }
  100% {
    transform: translateX(0);
  }
}
```

Likewise, if you're using `transform-origin`, make sure to specify the
correct origin for RTL:

```css
#progressbar {
  transform-origin: 0 0;
}

#progressbar:dir(rtl) {
  transform-origin: 100% 0;
}
```

## LTR text inside RTL contexts

By default, in RTL locales, some symbols like `/` and `.` will be moved
around and won't be displayed in the order that they were typed in. This
may be problematic for URLs for instance, where you don't want dots to
change position.

Here's a non-exhaustive list of elements that should be displayed like
they would be in LTR locales:

- Paths (e.g., C:\\Users\\username\\Desktop)
- Full URLs
- Code and code containers (like the DevTools' Inspector or the CSS
  rules panel)
- <about:config> preference names and values
- Telephone numbers
- Usernames & passwords (most sites on the web expect LTR
  usernames/passwords, but there may be exceptions)
- Other text fields where only LTR text is expected

To make sure these are displayed correctly, you can use one of the
following on the relevant element:

- `direction: ltr`
- `dir="ltr"` in HTML

Since the direction of such elements is forced to LTR, the text will
also be aligned to the left, which is undesirable from an UI
perspective, given that is inconsistent with the rest of the RTL UI
which has text usually aligned to the right. You can fix this using
`text-align: match-parent`. In the following screenshot, both text
fields (username and password) and the URL have their direction set to
LTR (to display text correctly), but the text itself is aligned to the
right for consistency with the rest of the UI:

```{image} about-logins-rtl.png
:alt: about:logins textboxes in RTL layout
```

However, since the direction in LTR, this also means that the start/end
properties will correspond to left/right respectively, which is probably
not what you expect. This means you have to use extra rules instead of
using logical properties.

Here's a full code example:

```css
.url {
  direction: ltr; /* Force text direction to be LTR */

  /* `start` (the default value) will correspond to `left`,
   * so we match the parent's direction in order to align the text to the right */
  text-align: match-parent;
}

/* :dir(ltr/rtl) isn't meaningful on .url, since it has direction: ltr, hence
 * why it is matched on .container. */
.container:dir(ltr) .url {
  padding-left: 1em;
}

.container:dir(rtl) .url {
  padding-right: 1em;
}
```

:::{note}
The LTR rule is separate from the global rule to avoid having the
left padding apply on RTL without having to reset it in the RTL rule.
:::

### Auto-directionality

Sometimes, the text direction on an element should vary dynamically
depending on the situation. This can be the case for a search input for
instance, a user may input a query in an LTR language, but may also
input a query in a RTL language. In this case, the search input has to
dynamically pick the correct directionality based on the first word, in
order to display the query text correctly. The typical way to do this is
to use `dir="auto"` in HTML. It is essential that
`text-align: match-parent` is set, to avoid having the text alignment
change based on the query, and logical properties also cannot be used on
the element itself given they can change meaning depending on the query.

## Testing

To test for RTL layouts in Firefox, you can go to <about:config> and
set `intl.l10n.pseudo` to `bidi`, or select the `Enable "bidi" locale`
option in the 3-dots menu in the {doc}`Browser Toolbox </devtools-user/browser_toolbox/index>`.
The Firefox UI should immediately flip, but a restart may be required
to take effect in some Firefox features and interactions.

:::{note}
When testing with `intl.l10n.pseudo` set to `bidi`, you may see some
oddities regarding text ordering due to the nature of displaying LTR
text in RTL layout.

```{image} about-protections-rtl.png
:alt: about:protections in RTL layout- English vs. Hebrew
```

This shouldn't be an issue when using an actual RTL build or language pack.
:::

Setting `dir="rtl"` on the root element of a chrome document doesn't
replace the `bidi` locale. The `:-moz-locale-dir()` pseudo-class matches
the document's locale direction, which follows the app locale and
`intl.l10n.pseudo` unless the root element has a `localedir="ltr"` or
`localedir="rtl"` attribute. After a `dir` flip, `:dir(rtl)` rules match
but `:-moz-locale-dir(rtl)` rules don't, so the page shows a mix of LTR and
RTL styling.

An element with `dir="auto"`, a `<bdi>` element and text under
`unicode-bidi: plaintext` take their direction from their own text
rather than from the UI (see [Auto-directionality](#auto-directionality)).
Test them with text in the opposite direction to the UI as well: RTL
text such as Hebrew in an LTR UI, and LTR text such as English in an
RTL UI.

### How to spot RTL-related issues

- Punctuation marks should appear on the left side of a
  word/sentence/paragraph on RTL, so if a *localizable* string appears
  in the UI with a dot, colon, ellipsis, question or exclamation mark
  on the right side of the text, this probably means that the text
  field is forced to be displayed as LTR.
- If icons/images/checkmarks do not appear on the opposite side of
  text, when compared to LTR.
- If buttons (like the close button, "OK" and "Cancel" etc.) do not
  appear on the opposite side of the UI and not in the opposite order,
  when compared to LTR.
- If paddings/margins/borders are not the same from the opposite side,
  when compared to LTR.
- Although Hebrew uses `1 2 3`, all the other RTL locales we support
  should use `١ ٢ ٣` as digits. So if you see `1 2 3` on any such
  locale, that likely indicates a bug.
- If navigating in the UI using the left/right arrow keys does not
  select the correct element (i.e., pressing Left selects an item on
  the right).
- If navigating in the UI using the Tab key does not focus elements
  from right to left, in an RTL context.
- If code is displayed as RTL (e.g., `;padding: 20px` - the semicolon
  should appear on the right side of the code). Code can still be
  aligned to the right if it appears in an RTL context.

## See also

- [RTL Best
  Practices](https://docs.google.com/document/d/1Rc8rvwsLI06xArFQouTinSh3wNte9Sqn9KWi1r7xY4Y/edit#heading=h.pw54h41h12ct)
- Building RTL-Aware Web Apps & Websites: [Part
  1](https://hacks.mozilla.org/2015/09/building-rtl-aware-web-apps-and-websites-part-1/),
  [Part
  2](https://hacks.mozilla.org/2015/10/building-rtl-aware-web-apps-websites-part-2/)

## Credits

Google's [Material Design guide for
RTL](https://material.io/design/usability/bidirectionality.html)
