# MozToggle

`moz-toggle` is a toggle element that can be used to switch between two states.
It may be helpful to think of it as a button that can be pressed or unpressed,
corresponding with "on" and "off" states.

```html story
<moz-toggle pressed
            label="Toggle label"
            description="This is a demo toggle for the docs.">
</moz-toggle>
```

## When to use

* Use a toggle for binary controls like on/off or enabled/disabled.
* Use when the action is performed immediately and doesn't require confirmation
  or form submission.
* A toggle is like a switch. If it would be appropriate to use a switch in the
  physical world for this action, it is likely appropriate to use a toggle in
  software.

## When not to use

* If another action is required to execute the choice, use a checkbox (i.e. a
  toggle should not generally be used as part of a form).

## Code

The source for `moz-toggle` can be found under
[toolkit/content/widgets/moz-toggle](https://searchfox.org/firefox-main/source/toolkit/content/widgets/moz-toggle/moz-toggle.mjs).
You can find an examples of `moz-toggle` in use in the Firefox codebase in both
[about:preferences](https://searchfox.org/firefox-main/source/browser/components/preferences/config/privacy.mjs)
and [about:addons](https://searchfox.org/firefox-main/source/toolkit/mozapps/extensions/content/aboutaddons.html#182).

`moz-toggle` can be imported into `.html`/`.xhtml` files:

```html
<script type="module" src="chrome://global/content/elements/moz-toggle.mjs"></script>
```

And used as follows:

```html
<moz-toggle pressed
            label="Label for the toggle"
            description="Longer explanation of what the toggle is for"
            aria-label="Toggle label if label text isn't visible"></moz-toggle>
```

### Fluent usage

Generally the `label`, `description`, and `aria-label` properties of
`moz-toggle` will be provided via [Fluent attributes](https://mozilla-l10n.github.io/localizer-documentation/tools/fluent/basic_syntax.html#attributes).
To get this working you will need to specify a `data-l10n-id` as well as
`data-l10n-attrs` if you're providing a label and a description:

```html
<moz-toggle data-l10n-id="with-label-and-description"
            data-l10n-attrs="label, description"></moz-toggle>
```

In which case your Fluent messages will look something like this:

```
with-label-and-description =
  .label = Label text goes here
  .description = Description text goes here
```

You do not have to specify `data-l10n-attrs` if you're only using an `aria-label`:

```html
<moz-toggle data-l10n-id="with-aria-label-only"></moz-toggle>
```

```
with-aria-label-only =
  .aria-label = aria-label text goes here
```

### Nested fields

`moz-toggle` supports nested or dependent fields via a `nested` named slot.
These fields will be rendered below the toggle element, and will be indented to
visually indicate dependence. Any nested fields will mirror the `disabled` state
of the toggle and will also become `disabled` whenever the toggle is not `pressed`.

When nesting fields it's important to wrap the elements in a `moz-fieldset` to
indicate to assistive technologies that the fields are related, and to provide a
label for the group of controls:

```html
<moz-fieldset label="Label for the group">
  <moz-toggle label="Parent toggle" pressed>
    <moz-checkbox slot="nested" label="Nested checkbox one" value="one"></moz-checkbox>
    <moz-checkbox slot="nested" label="Nested checkbox two" value="two" checked></moz-checkbox>
  </moz-toggle>
</moz-fieldset>
```

```html story
<moz-fieldset label="Label for the group">
  <moz-toggle label="Parent toggle" pressed>
    <moz-checkbox slot="nested" label="Nested checkbox one" value="one"></moz-checkbox>
    <moz-checkbox slot="nested" label="Nested checkbox two" value="two" checked></moz-checkbox>
  </moz-toggle>
</moz-fieldset>
```
