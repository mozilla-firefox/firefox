# Storybook for Firefox

[Storybook](https://storybook.js.org/) is an interactive tool that creates a
playground for UI components. We use Storybook to document our design system,
reusable components, and any specific components you might want to test with
dummy data. [Take a look at our Storybook
instance!](https://firefoxux.github.io/firefox-desktop-components/)

## Background

Storybook lists components that can be reused, and helps document
what common elements we have. It can also list implementation specific
components, but they should be added to the "Domain-Specific UI Widgets" section.

Changes to files directly referenced from Storybook (so basically non-chrome://
paths) should automatically reflect changes in the opened browser. If you make a
change to a `chrome://` referenced file then you'll need to do a hard refresh
(Cmd+Shift+R/Ctrl+Shift+R) to notice the changes. If you're on Windows you may
need to `./mach build faster` to have the `chrome://` URL show the latest version.

## Running Storybook

Installing the npm dependencies and running the `storybook` npm script should be
enough to get Storybook running. This can be done via `./mach storybook`
commands, or with your personal npm/node that happens to be compatible.

Production exports hash stylesheet module identifiers to avoid oversized generated
filenames in Windows checkouts.

### Running with mach commands

This is the recommended approach for installing dependencies and running
Storybook locally.

To install dependencies, start the Storybook server, and launch the Storybook
site in a local build of Firefox, just run:

```sh
# This uses npm ci under the hood to install the package-lock.json exactly.
./mach storybook
```

This single command will first install any missing dependencies then start the
local Storybook server. It will also start your local browser and point it to
`http://localhost:5703` while enabling certain preferences to ensure components
display as expected (specifically `svg.context-properties.content.enabled`,
`layout.forms.input-type-search.enabled`, and `layout.forms.reveal-password-button.enabled`).

It's necessary to use your local build to test in Storybook since `chrome://`
URLs are currently being pulled from the running browser, so any changes to
common-shared.css for example will come from your build.

The Storybook server will continue running and will watch for component file
changes.

#### Alternative mach commands

Although running `./mach storybook` is the most convenient way to interact with
Storybook locally it is also possible to run separate commands to start the
Storybook server and run your local build with the necessary prefs.

If you only want to start the Storybook server - for example in cases where you
already have a local build running - you can pass a `--no-open` flag to `./mach
storybook`:

```sh
# Start the storybook server without launching a local Firefox build.
./mach storybook --no-open
```

If you just want to spin up a local build of Firefox with the required prefs
enabled you can use the `launch` subcommand:

```sh
# In another terminal:
./mach storybook launch
```

This will run your local browser and point it at `http://localhost:5703`.

Alternatively, you can simply navigate to `http://localhost:5703/` or run:

```sh
# In another terminal:
./mach run http://localhost:5703/
```

although with this option certain prefs won't be enabled, so what's displayed in
Storybook may not exactly reflect how components will look when used in Firefox.

### Personal npm

You can use your own `npm` to install and run Storybook. Compatibility is up
to you to sort out.

```sh
cd browser/components/storybook
npm ci # Install the package-lock.json exactly so lockfileVersion won't change.
npm run storybook
```

## Updating Storybook dependencies

On occasion you may need to update or add a npm dependency for Storybook.
This can be done using the version of `npm` packaged with `mach`:

```sh
# Install a dev dependency from within the storybook directory.
cd browser/components/storybook && ../../../mach npm i -D your-package
```

Use `./mach storybook upgrade` to update dependencies within the ranges in `package.json`.

## Adding new stories

Storybook is currently configured to search for story files (any file with a
`.stories.(js|mjs|md)` extension) in `toolkit/content/widgets` and
`browser/components/storybook/stories`.

Stories in `toolkit/content/widgets` are used to document design system
components, also known as UI widgets.
As long as you used `./mach addwidget` correctly, there is no additional setup needed to view your newly created story in Storybook.

Stories in `browser/components/storybook/stories` are used for non-design system components, also called domain-specific UI widgets.
The easiest way to use Storybook for non-design system element is to use `./mach addstory new-component "Your Project"`.
You can also use `./mach addstory new-component "Your Project" --path browser/components/new-component.mjs` where `--path` is the path to your new components' source.
[See the Credential Management/Timeline widget for an example.](https://searchfox.org/mozilla-central/rev/2c11f18f89056a806c299a9d06bfa808718c2e84/browser/components/storybook/stories/login-timeline.stories.mjs#11)

If you want to colocate your story with the code it is documenting you will need
to add to the `stories` array in `.storybook/main.mjs`
so that Storybook knows where to look for your files.

The Storybook docs site has a [good
overview](https://storybook.js.org/docs/web-components/get-started/whats-a-story)
of what's involved in writing a new story. For convenience you can use the [Lit
library](https://lit.dev/) to define the template code for your story, but this
is not a requirement.

### UI Widgets versus Domain-Specific UI Widgets

Widgets that are part of [our design system](https://acorn.firefox.com/latest/acorn.html) and intended to be used across the Mozilla suite of products live under the "UI Widgets" category in Storybook and under `toolkit/content/widgets/` in Firefox.
These global widgets are denoted in code by the `moz-` prefix in their name.
For example, the name `moz-support-link` informs us that this widget is design system compliant and can be used anywhere in Firefox.

Storybook can also be used to help document and prototype widgets that are specific to a part of the codebase and not intended for more global use.
Stories for these types of widgets live under the "Domain-Specific UI Widgets" category, while the code can live in any appropriate folder in `mozilla-central`.
[See the Credential Management folder as an example of a domain specific folder](https://firefoxux.github.io/firefox-desktop-components/?path=/docs/domain-specific-ui-widgets-credential-management-timeline--empty-timeline) and [see the login-timeline.stories.mjs for how to make a domain specific folder in Storybook](https://searchfox.org/firefox-main/source/browser/components/storybook/stories/login-timeline.stories.mjs).
[To add a non-team specific widget to the "Domain-specific UI Widgets" section, see the migration-wizard.stories.mjs file](https://searchfox.org/firefox-main/source/browser/components/storybook/stories/migration-wizard.stories.mjs).

Creating and documenting domain specific UI widgets allows other teams to be aware of and take inspiration from existing UI patterns.
With these widgets, **there is no guarantee that the element will work for your domain.**
If you need to use a domain-specific widget outside of its intended domain, it may be worth discussing how to convert this domain specific widget into a global UI widget.
