# Reusable UI widgets

## Background

Different Firefox surfaces make use of similar UI elements such as cards, menus,
toggles, and message bars. A group of designers and developers have started
working together to create standardized versions of these elements in the form
of new web components. The intention is for these components to encapsulate our
design system, ensure accessibility and usability across the application, and
reduce the maintenance burden associated with supporting multiple different
implementations of the same UI patterns.

Many of these components are being built using the [Lit
library](https://lit.dev/) to take advantage of its templating syntax and
re-rendering logic. All new components are being documented in Storybook in an
effort to create a catalog that engineers and designers can use to see which
components can be easily lifted off the shelf for use throughout Firefox.

If you want to see the progress over time of these new reusable components, we have a [Reusable Component Adoption chart](https://firefoxux.github.io/recomp-metrics/) that you should check out!

## Designing new reusable widgets

Widgets that live at the global level, "UI Widgets", should be created in collaboration with the Design System team.
This ensures consistency with the rest of the elements in the Design System and the existing UI elements.
Otherwise, you should consult with your team and the appropriate designer to create domain-specific UI widgets.
Ideally, these domain widgets should be consistent with the rest of the UI patterns established in Firefox.

### Does an existing widget cover the use case you need?

Before creating a new reusable widget, make sure there isn't a widget you could use already.
When designing a new reusable widget, ensure it is designed for all users.
Here are some questions you can use to help include all users: how will people perceive, operate, and understand this widget? Will the widget use standards proven technology.
[Please refer to the "General Considerations" section of the Mozilla Accessibility Release Guidelines document](https://wiki.mozilla.org/Accessibility/Guidelines#General_Considerations) for more details to ensure your widget adheres to accessibility standards.

### Supporting widget use in different processes

A newly designed widget may need to work in the parent process, the content process, or both depending on your use case.
[See the Process Model document for more information about these different processes](https://firefox-source-docs.mozilla.org/dom/ipc/process_model.html).
You will likely be using your widget in a privileged process (such as the parent or privileged content) with access to `Services`, `XPCOMUtils`, and other globals.
Storybook and other web content do not have access to these privileged globals, so you will need to write workarounds for `Services`, `XPCOMUtils`, chrome URIs for CSS files and assets, etc.
[Check out moz-support-link.mjs and moz-support-link.stories.mjs for an example of a widget being used in the parent/chrome and needing to handle `XPCOMUtils` in Storybook](https://searchfox.org/mozilla-central/search?q=moz-support-link&path=&case=false&regexp=false).
[See moz-toggle.mjs for handling chrome URIs for CSS in Storybook](https://searchfox.org/firefox-main/source/toolkit/content/widgets/moz-toggle/moz-toggle.mjs).
[See moz-label.mjs for an example of handling `Services` in Storybook](https://searchfox.org/firefox-main/source/toolkit/content/widgets/moz-label/moz-label.mjs).

### Autonomous or Customized built-in Custom Elements

There are two types of custom elements, autonomous elements that extend `HTMLElement` and customized built-in elements that extend basic HTML elements.
If you use autonomous elements, you can use Shadow DOM and/or the Lit library.
[Lit does not support customized built-in custom elements](https://github.com/lit/lit-element/issues/879).

In some cases, you may want to provide some functionality on top of a built-in HTML element, [like how `moz-support-link` prepares the `href` value for anchor elements](https://searchfox.org/mozilla-central/rev/3563da061ca2b32f7f77f5f68088dbf9b5332a9f/toolkit/content/widgets/moz-support-link/moz-support-link.mjs#83-89).
In other cases, you may want to focus on creating markup and reacting to changes on the element.
This is where Lit can be useful for declaritively defining the markup and reacting to changes when attributes are updated.

### How will developers use your widget?

What does the interface to your widget look like?
Do you expect developers to use reactive attributes or [slots](https://developer.mozilla.org/en-US/docs/Web/API/Web_components/Using_templates_and_slots#adding_flexibility_with_slots)?
If there are many ways to accomplish the same end result, this could result in future confusion and increase the maintainance cost.

You should write stories for your widget to demonstrate how it can be used.
These stories can be used as guides for new use cases that may appear in the future.
This can also help draw the line for the responsibilities of your widget.

## Adding new design system components

We have a `./mach addwidget` scaffold command to make it easier to create new
reusable components and hook them up to Storybook. Currently this command can
only be used to add a new Lit based web component to `toolkit/content/widgets`.
In the future we may expand it to support options for creating components
without using Lit and for adding components to different directories.
See [Bug 1803677](https://bugzilla.mozilla.org/show_bug.cgi?id=1803677) for more details on these future use cases.

To create a new component, you run:

```sh
# Component names should be in kebab-case and contain at least 1 -.
./mach addwidget component-name
```

The scaffold command will generate the following files:

```sh
└── toolkit
    └── content
        ├── tests
        │   └── widgets
        │       └── test_component_name.html # chrome test
        └── widgets
            └── component-name # new folder for component code
                ├── component-name.css # component specific CSS
                ├── component-name.mjs # Lit based component
                └── component-name.stories.mjs # component stories
```

It will also make modifications to `toolkit/content/jar.mn` to add `chrome://`
URLs for the new files, and to `toolkit/content/tests/widgets/chrome.toml` to
enable running the newly added test.

After running the scaffold command you can start Storybook and you will see
placeholder content that has been generated for your component. You can then
start altering the generated files and see your changes reflected in Storybook.

### Known `browser_all_files_referenced.js` issue

Unfortunately for now [the
browser_all_files_referenced.js test](https://searchfox.org/firefox-main/source/browser/base/content/test/browser-static/browser_all_files_referenced.js)
will fail unless your new component is immediately used somewhere outside
of Storybook. We have plans to fix this issue, [see Bug 1806002 for more details](https://bugzilla.mozilla.org/show_bug.cgi?id=1806002), but for now you can get around it
by updating [this array](https://searchfox.org/mozilla-central/rev/5c922d8b93b43c18bf65539bfc72a30f84989003/browser/base/content/test/static/browser_all_files_referenced.js#113) to include your new chrome filepath.

### Using new design system components

Once you've added a new component to `toolkit/content/widgets` and created
`chrome://` URLs via `toolkit/content/jar.mn` you should be able to start using it
throughout Firefox. In most cases, you should be able to rely on your custom
element getting lazy loaded at the time of first use, provided you are working
in a privileged context where `customElements.js` is available.

**Note** Since [bug 1896837](https://bugzilla.mozilla.org/show_bug.cgi?id=1896837)
lazy loaded UI widgets are loaded at the DOMContentLoaded event. Please notify
the Reusable Components team if you see weirdness due to this.

You can import the component directly into `html`/`xhtml` files via a
`script` tag with `type="module"`:

```html
<script type="module" src="chrome://global/content/elements/your-component-name.mjs"></script>
```

**Note** you will need to add your new widget to [this array in customElements.js](https://searchfox.org/mozilla-central/rev/cde3d4a8d228491e8b7f1bd94c63bbe039850696/toolkit/content/customElements.js#791-810) to ensure it gets lazy loaded on creation.

## Common pitfalls

If you're trying to use a reusable widget but nothing is appearing on the
page it may be due to one of the following issues:

- Omitting the `type="module"` in your `script` tag.
- Wrong file path for the `src` of your imported module.
- Widget is not declared or incorrectly declared in the correct `jar.mn` file.
- Not specifying the `html:` namespace when using a custom HTML element in an
  `xhtml` file. For example the tag should look something like this:

  ```html
  <html:your-component-name></html:your-component-name>
  ```
- Trying to extend a built-in HTML element in Lit. [Because Webkit never
  implemented support for customized built-ins, Lit doesn't support it either.](https://github.com/lit/lit-element/issues/879#issuecomment-1061892879)
  That means if you want to do something like:

  ```js
  customElements.define("cool-button", CoolButton, { extends: "button" });
  ```

  you will need to make a vanilla custom element, you cannot use Lit.
  [For an example of extending an HTML element, see `moz-support-link`](https://searchfox.org/firefox-main/source/toolkit/content/widgets/moz-support-link/moz-support-link.mjs).
