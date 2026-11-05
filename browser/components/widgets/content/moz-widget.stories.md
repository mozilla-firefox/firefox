# MozWidget

`moz-widget` is a card that embeds a self-contained, interactive widget in a host surface. Widgets are used for presenting brief updates or completing a quick task.

It clips content to its bounds and uses the dimensions set by the `size` attribute. Content comes from a browser or iframe when `src` is set and otherwise from the default slot. The `error` slot carries the message shown when a widget fails.

```html story
<moz-widget widget-id="weather" size="medium">
  <section style={{ padding: "16px" }}>
    <h3>Los Angeles</h3>
    <p>72°F and sunny</p>
  </section>
</moz-widget>
```

## Code

The source for `moz-widget` can be found under [browser/components/widgets/content/](https://searchfox.org/firefox-main/source/browser/components/widgets/content).

## How to use `moz-widget`

### Importing the element

`moz-widget` is not registered by `customElements.js` and needs to be imported to use:

```html
<script
  type="module"
  src="chrome://browser/content/widgets/moz-widget.mjs"
></script>
```

### Setting the `size`

`size` selects fixed dimensions based on the IAB Medium Rectangle (300x250) but the host surface can override them.
A missing or unrecognized `size` uses the `large` dimensions.
The widget sets `--widget-size` to `small`, `medium`, or `large` for its header and content to query with `@container style()`.

<table>
  <thead>
    <tr>
      <th><code>size</code></th>
      <th>Width</th>
      <th>Height</th>
      <th>Ratio</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td><code>large</code></td>
      <td>300px</td>
      <td>250px</td>
      <td>6:5</td>
    </tr>
    <tr>
      <td><code>medium</code></td>
      <td>300px</td>
      <td>125px</td>
      <td>12:5</td>
    </tr>
    <tr>
      <td><code>small</code></td>
      <td>150px</td>
      <td>125px</td>
      <td>6:5</td>
    </tr>
  </tbody>
</table>

If a surface overrides the sizing, it should keep the dimensions close to the IAB rectangle as New Tab does.

### Setting the `widget-id`

`widget-id` is the registered ID of the hosted widget.

### Setting the `title`

`title` provides the accessible name of the widget.

Set `data-l10n-id` to a Fluent message with a `.title` attribute:

```
widget-weather =
  .title = Weather
```

```html
<moz-widget widget-id="weather" data-l10n-id="widget-weather"></moz-widget>
```

Or set the attribute directly:

```html
<moz-widget widget-id="weather" title="Weather"></moz-widget>
```

### Adding a header

A `moz-widget-header` in the `header` slot appears above the widget content. `moz-widget` imports `moz-widget-header`.

`title` sets the header text:

```html
<moz-widget widget-id="weather" title="Weather">
  <moz-widget-header slot="header" title="LA Weather"></moz-widget-header>
</moz-widget>
```

The `leading` slot holds content before the heading, such as a back button or a badge.

The `trailing` slot holds content at the end of the header.

In a `small` widget, the header hides a `moz-badge` in the `leading` slot, and hides the `trailing` slot when the header shows a menu button.

```html
<moz-widget widget-id="weather" title="Weather">
  <moz-widget-header slot="header" title="Weather">
    <moz-button
      slot="leading"
      type="ghost"
      size="small"
      iconsrc="chrome://global/skin/icons/arrow-left.svg"
      data-l10n-id="weather-back-button"
    ></moz-button>
    <moz-badge slot="leading" type="new" data-l10n-id="weather-new-badge"></moz-badge>
    <moz-button
      slot="trailing"
      size="small"
      iconsrc="chrome://global/skin/icons/arrow-down-12.svg"
      iconposition="end"
      data-l10n-id="weather-units-button"
    ></moz-button>
  </moz-widget-header>
</moz-widget>
```

When `moz-widget`'s `menu` slot holds a `panel-list`, the header shows a menu button that opens it. A `panel-list` without an `id` gets a generated one. An `id` set by the surface has to be unique to its document or shadow root. `menu-label` sets the button's accessible name, which defaults to "More options":

```html
<moz-widget widget-id="weather" title="Weather">
  <moz-widget-header slot="header" data-l10n-id="weather-header"></moz-widget-header>
  <panel-list slot="menu" id="weather-menu">
    <panel-item data-l10n-id="weather-refresh"></panel-item>
    <hr />
    <panel-item data-l10n-id="weather-remove"></panel-item>
  </panel-list>
</moz-widget>
```

```ftl
weather-header =
    .title = Weather
    .menu-label = Weather options
```

### Setting `padded`

`padded` insets the card and rounds the corners of the content.

```html
<moz-widget widget-id="weather" title="Weather" padded>
  <moz-widget-header slot="header" title="Weather"></moz-widget-header>
</moz-widget>
```

### Without a header

Without a header, the content extends to the card's edges. `moz-widget` draws the menu button over the top-end corner of the content, shown while the widget is hovered or focused, or while its menu is open. `menu-label` on `moz-widget` sets the button's accessible name.

```html
<moz-widget widget-id="weather" title="Weather" menu-label="Weather options">
  <panel-list slot="menu">
    <panel-item data-l10n-id="weather-refresh"></panel-item>
  </panel-list>
</moz-widget>
```

### Remote content with `src`

`src` uses a remote frame for the widget content instead of the slotted content.

```html
<moz-widget
  widget-id="weather"
  title="Weather"
  src="moz-extension://UUID/widget.html"
  remote-type="extension"
  group-id="GROUP_ID"
></moz-widget>
```

The frame is a `browser` element in chrome or an `iframe` element in content.

In a browser element, `remote-type="extension"` loads a `moz-extension:` document in the extension process, and `group-id` joins the extension's browsing context group, taken from `WebExtensionPolicy.browsingContextGroupId`.

### Reporting an error

`moz-widget` watches its iframe. An iframe that fails to load puts the widget in its error state: it covers the content with the host's error content, sets the `error` attribute and fires `widget-error`. Set `ready-on-load` to have an iframe load fire `widget-ready`; it is off by default and does not affect a `browser` frame. Gecko fires `error` only where the load never starts, such as a blocked URL; a document that loads and then fails gets an error page, which counts as a successful load.

A host calls `reportError()` to show its error content and `reportReady()` to uncover the content and fire `widget-ready` on every call. The `error` property reads or sets the same state; turning it on fires `widget-error`, and clearing it fires `widget-ready` unless `src` changes in the same update. The content is covered rather than replaced, so the document keeps running and can recover. This also works for slotted content without `src`.

`reportError(reason)` passes the reason on as `widget-error`'s `detail.reason`.

A host should always provide content in the `error` slot, since that is what covers the widget content. Without it a failing widget leaves an empty card.

```html
<moz-widget widget-id="weather" title="Weather" src="moz-extension://UUID/widget.html">
  <div slot="error" class="my-error">Widget unavailable. Check back later.</div>
</moz-widget>
```

A `browser` frame is a top-level browsing context, so it cannot report for itself. A chrome host calls `reportError()` and `reportReady()` for it.
