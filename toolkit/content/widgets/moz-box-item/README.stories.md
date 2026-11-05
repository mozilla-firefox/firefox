# MozBoxItem

`moz-box-item` is a component that can be used separately or together with `moz-box-button` and `moz-box-link` as a part of a `moz-box-group` to display related content and actions.

```html story
<moz-box-item style={{ width: "400px" }}
              label="I'm a box item">
</moz-box-item>
```

## Code

The source for `moz-box-item` can be found under [toolkit/content/widgets/moz-box-item/](https://searchfox.org/firefox-main/source/toolkit/content/widgets/moz-box-item)

## How to use `moz-box-item`

### Importing the element

Like other custom elements, you should usually be able to rely on `moz-box-item` getting lazy loaded at the time of first use.
See [this documentation](https://firefox-source-docs.mozilla.org/browser/components/storybook/docs/README.reusable-widgets.stories.html#using-new-design-system-components) for more information on using design system custom elements.

### Setting the `label` and `description`

In order to set a label and description, use the `label` and `description` attributes.
In general, the label and description should be controlled by Fluent.

```html
<moz-box-item label="I'm a box item" description="Some description of the item"></moz-box-item>
```

```html story
<moz-box-item style={{ width: "400px" }}
              label="I'm a box item"
              description="Some description of the item">
</moz-box-item>
```

### Setting an icon

In order to have an icon appear next to the label, use the `.iconSrc` property or `iconsrc` attribute.

```html
<moz-box-item label="I'm a box item"
              description="Some description of the item"
              iconsrc="chrome://global/skin/icons/highlights.svg">
</moz-box-item>
```

```html story
<moz-box-item style={{ width: "400px" }}
              label="I'm a box item"
              description="Some description of the item"
              iconsrc="chrome://global/skin/icons/highlights.svg">
</moz-box-item>
```

### Setting the layout of the content

You can set a layout style for the box content using the `layout` attribute.  There are 2 layout options: `default` or `large-icon`.
The `default` layout will set a smaller icon next to the label. Use `large-icon` layout to display a bigger, vertically centered icon.

```html
<moz-box-item label="I'm a box item"
              description="Some description of the item"
              layout="large-icon"
              iconsrc="chrome://global/skin/icons/info.svg">
</moz-box-item>
```

```html story
<moz-box-item style={{ width: "400px" }}
              label="I'm a box item"
              description="Some description of the item"
              layout="large-icon"
              iconsrc="chrome://global/skin/icons/info.svg">
</moz-box-item>
```

### Adding actions to the `moz-box-item`

The `moz-box-item` component supports 2 slots for actions: `actions` and `actions-start`. You should set the correct `aria-label` for any icon buttons that are included in actions using fluent, since we cannot programmatically associate the button with the label in the shadow DOM.

The `actions` slot goes after the label/description/etc. Using this slot is a preferred way to ad actions to the component.

```html
<moz-box-item label="I'm a box item">
  <moz-button label="Click me!" slot="actions"></moz-button>
</moz-box-item>
```

```html story
<moz-box-item style={{ width: "400px" }} label="I'm a box item">
  <moz-button label="Click me!" slot="actions"></moz-button>
</moz-box-item>
```

Use `slot="actions-start"` if you need to place an action at the beginning of the container. If `moz-box-item` is a part of the `moz-box-group`, the number of items in `actions-start` slot should be consistent across the entire group.

```html
<moz-box-item label="I'm a box item">
  <moz-button iconsrc="chrome://global/skin/icons/delete.svg"
              aria-label="Delete I'm a box item"
              slot="actions-start">
  </moz-button>
</moz-box-item>
```

```html story
<moz-box-item style={{ width: "400px" }} label="I'm a box item">
  <moz-button iconsrc="chrome://global/skin/icons/delete.svg"
              aria-label="Delete I'm a box item"
              slot="actions-start">
  </moz-button>
</moz-box-item>
```

### Using the default slot

Use the default slot if you need to place a custom content into the `moz-box-item`.

```html
<moz-box-item>
  <div class="slotted">
    <img src="resource://content-accessible/illustrations/security-error.svg"
    alt="Confused Kit is looking at an orange exclamation mark" />
    <span>This is an example message</span>
    <span class="text-deemphasized">
      Message description would go down here
    </span>
  </div>
</moz-box-item>
```

```html story
<moz-box-item style={{ width: "280px" }}>
  <div style={{ display: "flex", justifyContent: "center", alignItems: "center", flexDirection: "column", textAlign: "center" }}>
    <img src="resource://content-accessible/illustrations/security-error.svg" style={{ width: "150px", marginBlockEnd: "16px" }}
    alt="Confused Kit is looking at an orange exclamation mark" />
    <span>This is an example message</span>
    <span class="text-deemphasized">
      Message description would go down here
    </span>
  </div>
</moz-box-item>
```

### Fluent usage

The `label` and `description` attributes of `moz-box-item` will generally be provided via [Fluent attributes](https://mozilla-l10n.github.io/localizer-documentation/tools/fluent/basic_syntax.html#attributes).
The relevant `data-l10n-attrs` are set automatically, so to get things working you just need to supply a `data-l10n-id` as you would with any other element.

For example, the following Fluent messages:

```
moz-box-item-label =
  .label = I'm a box item
moz-box-item-label-description =
  .label = I'm a box item
  .description = Some description of the item
```

would be used to set attributes on the different `moz-box-item` elements as follows:

```html
<moz-box-item data-l10n-id="moz-box-item-label"></moz-box-item>
<moz-box-item data-l10n-id="moz-box-item-label-description"></moz-box-item>
```
