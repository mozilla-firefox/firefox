# Figma Code Connect

[Code Connect](https://www.figma.com/code-connect-docs/) is a bridge between your codebase and Figma’s Dev Mode, connecting components in your repositories directly to components in your design files.
We use Code Connect to generate recommended code snippets for our Figma components to improve the developer experience when translating designs to code.

Firefox uses the [Template API](https://developers.figma.com/docs/code-connect/template-api/) (`.figma.ts` files that `import figma from "figma"` and export a `figma.code` snippet).
The legacy `@figma/code-connect/html` parser format (`figma.connect(...)`) is no longer used in-tree.
See the [templates migration guide](https://developers.figma.com/docs/code-connect/templates-migration-guide/) if you need to convert older parser-based files.

## How to set up Code Connect

First, you'll need to install [the Code Connect command line tool](https://www.figma.com/code-connect-docs/quickstart-guide/#install-the-code-connect-command-line-tool) via `npm` (Node 18+):

`npm install --global @figma/code-connect@latest`

Second, you'll either need to add your [Figma personal access token](https://help.figma.com/hc/en-us/articles/8085703771159-Manage-personal-access-tokens) to your terminal environment:

`export FIGMA_ACCESS_TOKEN='your-generated-access-token'`

You could also add your personal access token via `.bashrc`, `.zshrc`, or other appropriate file based on your shell environment.

## How to use Code Connect

### Finding a Figma component to generate the initial Code Connect file

After installing and setting your `FIGMA_ACCESS_TOKEN` in your terminal, you will need to navigate to our Figma library to find a component that needs a Code Connect config file.

You can find a potential component through various ways:

1. Searching through the open dependent bugs on [Bug 1955395 - [meta] Figma Code Connect](https://bugzilla.mozilla.org/show_bug.cgi?id=1955395).
2. Checking the "Explore component behavior" in Figma and seeing if there no code snippet at the bottom of the "Component playground" window, underneath the "Variable modes" section.

**Note:** Not all Figma components will need a Code Connect config.
For example, the ["Radio Button"](https://www.figma.com/design/3WoKOSGtaSjhUHKldHCXbc/Desktop-Components-3?node-id=32-678&m=dev) exists as a standalone Figma component, but it is only used as a subcomponent for the ["Radio Button with label,"](https://www.figma.com/design/3WoKOSGtaSjhUHKldHCXbc/Desktop-Components-3?node-id=3907-17657&m=dev) which more accurately models our `moz-label` web component.

Once you are aware of the Figma component you want to work on, you'll need to navigate to Figma and find the base Figma component.
It should have an icon of a diamond made of four smaller diamonds.
When you select this component, it likely will say **Component set** in the sidebar.

![Figma user interface showing the diamond icon next to "Checkbox"](./img/figma-diamond-example.png)
![Figma user interface showing the diamond icon next to "Component set"](./img/figma-component-set-example.png)

Ensure that you have selected the main Figma component instead of an instance of it.
Code Connect will not setup correctly for an instance.
There should be an option indicated by either a diamond with four smaller diamonds or a diamond with an arrow pointing out from the diamond.
This icon button is used to go to the main component, and will be labelled "Go to main component" once you hover on the icon button.
Use this "Go to main component" button to check if there is a component set higher up in the hierarchy.
Once you've got the main component set, then you can copy a link to the component and move to generating the Code Connect config in your terminal.

![Figma user interface showing the "Go to main component" tooltip text when hovering over the diamond icon with four smaller diamonds](./img/figma-go-to-main-component-one.png)
![Figma user interface showing the "Go to main component" tooltip text when hovering over the diamond with an arrowing pointing out from it](./img/figma-go-to-main-component-two.png)

### Generating the initial Code Connect file for a Figma component

After finding the main component set in the previous step and copying the link to the main component, navigate to your Firefox repo in your terminal.
Code Connect is configured by the checked-in `toolkit/figma.config.json`.
From the repository root, run the following command (please note the single quotes are needed to correctly parse the URL!):

`npx figma connect create --config toolkit/figma.config.json --dir toolkit '<main component set link>'`

For example, this command would look something like `npx figma connect create --config toolkit/figma.config.json --dir toolkit 'https://www.figma.com/design/[...]?node-id=1-255'` where the full URL is the GUID for your design document.
This command will work regardless if you copied the link in Dev Mode (which has an additional query `&m=dev` at the end of the link) or not.

If the Code Connect tool was able to find a component in that copied URL, you will receive the following output in your terminal:

```
Fetching component information from Figma...
Parsing response
Generating Code Connect files...
Code Connect files generated successfully:
<firefox-repo-path>\<component-name>.figma.ts
```

If the Code Connect tool was not able to find a component, it will throw an error and notify you in your terminal output.
The most common reason for this error is the particular node or layer you used for the linked URL is not actually a component but a wrapper for one or more components.
Try navigating a layer above or below the node you used and see if you are able to generate a Code Connect config file using one of those other nodes.

### Modifying the generated config file

While generating the file is a good start, you will need to move it, rename it, and modify it.
You should move this generated file to the component specific folder and rename it to `<component-name>.figma.ts`.
The generator names the file according to the Figma component, so this may not have the same name as the Lit component.
For example, the generator creates a file titled `checkbox.figma.ts`.
This file should be moved to the corresponding folder - in this case `/toolkit/content/widgets/moz-checkbox` - and renamed to `moz-checkbox.figma.ts`.

Template files map Figma properties with `figma.selectedInstance` helpers (`getString`, `getBoolean`, `getEnum`, `findInstance`, …) and render the snippet with `` figma.code`...` ``.
Keep the `// url=...` comment at the top of the file — that is how the template is linked to the Figma component.
Give each template a unique `id`.
When the same Lit component is connected to both Nova and Desktop Components 3, use a `-desktop-v3` suffix for the Desktop library file (for example `moz-badge.figma.ts` and `moz-badge-desktop-v3.figma.ts`).

```ts
// url=https://www.figma.com/design/.../Nova-Components--Experimental-?node-id=3480-22604
import figma from "figma"

const label = figma.selectedInstance.getString("Label")
const iconSrc = figma.selectedInstance.getBoolean("Show icon", {
  true: "chrome://example.svg",
  false: undefined,
})

export default {
  id: "moz-badge",
  example: figma.code`
    <moz-badge
      label="${label}"
      ${iconSrc ? `iconsrc="${iconSrc}"` : ""}
      title="This appears as a tooltip on hover"
    ></moz-badge>
  `,
}
```

Use `` figma.code`...` `` with template interpolation for attributes.
Omit optional attributes with conditionals (for example `${disabled ? "disabled" : ""}`).
Match attribute names to the web component's reflected properties in its `.mjs` file.

**Note:** The generated template may not capture all of the properties that can or should be mapped from Figma.

#### When the property types match

`getBoolean("Checked")` returns the current value from the selected Figma instance.

```ts
const checked = figma.selectedInstance.getBoolean("Checked")
```

#### When the property types are different

Pass a mapping object to convert Figma values into Lit attribute values.

```ts
// Figma State=Disabled will set disabled:true
const disabled = figma.selectedInstance.getEnum("State", {
  Disabled: true,
})
// Figma "Show icon"=true will set iconSrc:"chrome://example.svg"
const iconSrc = figma.selectedInstance.getBoolean("Show icon", {
  true: "chrome://example.svg",
  false: undefined,
})
```

#### Creating the example template

The `example` field becomes the code snippet shown in Figma.
Prefer `` figma.code`...` `` tagged templates for the example snippet.

There is [other prior art available on searchfox](https://searchfox.org/mozilla-central/search?q=figma.ts&path=&case=false&regexp=false) when you search for figma.ts files that will assist you.

### Publishing the modified config file

Code Connect discovery is configured by `toolkit/figma.config.json`.
From the repository root, verify your changes with:

`npx figma connect publish --config toolkit/figma.config.json --dir toolkit --dry-run`

If this check passes, publish with `npx figma connect publish --config toolkit/figma.config.json --dir toolkit`.
After doing this, navigate to the node that you copied the link for earlier in this process (or check the `// url=` comment in the template).
You can test the code snippet by opening "Explore component behavior" under the "Component information" section.

If this generated snippet does not appear to be correct, you can undo the change on Figma with:

`npx figma connect unpublish --config toolkit/figma.config.json --dir toolkit`

Afterwards, commit and submit the file via `moz-phab` as you would any other bug.

## How to find the Code Connect snippet

If you are interacting with an instance of a Figma component and you are in Dev Mode, then you will be able to find a section called "Code Connect" in the "Inspect" sidebar.
The code snippet for the instance will be within that section. Depending on the component, there may be additional comments at the bottom of the code snippet that explains what may be missing from the implementation of the Figma component compared to the Lit component.

![Figma user interface showing the snippet with additional comments](./img/code-connect-snippet-comments.png)

This section is also available if you are interacting with one of the variants of the main Figma component.

![Figma user interface showing the snippet when interacting with main component variants](./img/code-connect-main-component-variant.png)

If you are interacting with the main Figma component itself - the wrapper holding all the variants that may exist - then there will be a "Component information" section under the "Inspect" sidebar.
Click on "Explore component behavior" to inspect the code snippet as you test changing the properties of the Figma component.

![Figma user interface showing the component playground](./img/code-connect-component-playground.png)

For more examples, you can check out [Figma's Code Connect documentation](https://www.figma.com/code-connect-docs/#publish-to-figma-to-make-handoff-a-breeze).

## Code Connect issues/pitfalls/scenarios

### Mapping Figma props back to Lit props

You may encounter cases where in Figma you can toggle some configuration of a component.
For example, given [the "Checkbox" component](https://www.figma.com/design/PqfaOcMGbX5liEXTTUzeYX/Nova-Components--Experimental-?node-id=3907-17658&m=dev), we can toggle a description, a support link, and an icon.
When activating the "Show description" and "Show support link" toggles, we get two new editable text fields, "Description" and "Support link".
To ensure the generated code snippet shows the correct value for these fields, map a Figma boolean to return a string when the boolean is true:

```ts
const labelInstance = figma.selectedInstance.findInstance("Label")
const description =
  labelInstance.type !== "ERROR"
    ? labelInstance.getBoolean("Show description", {
        true: labelInstance.getString("Description"),
      })
    : undefined
const supportPage =
  labelInstance.type !== "ERROR"
    ? labelInstance.getBoolean("Show support link", {
        true: "sumo-slug",
      })
    : undefined
```

In the "Description" case, we use `labelInstance.getString("Description")` inside the `Show description` boolean mapping so that the generated snippet will show the current value in the "Description" text field.
We don't do this for the "Support link" case though, since the way to get the correct generated support link element is by passing a SUMO slug to `<moz-checkbox>`.

### Working with nested components

Some Figma wrappers nest other components. For example, [the Nova "Checkbox" component](https://www.figma.com/design/PqfaOcMGbX5liEXTTUzeYX/Nova-Components--Experimental-?node-id=3907-17658&m=dev) contains nested "Checkbox" and "Label" instances.
Use `findInstance("LayerName")` to read nested properties:

```ts
const labelInstance = figma.selectedInstance.findInstance("Label")
const checkboxInstance = figma.selectedInstance.findInstance("Checkbox")

const description =
  labelInstance.type !== "ERROR"
    ? labelInstance.getBoolean("Show description", {
        true: labelInstance.getString("Description"),
      })
    : undefined
const checked =
  checkboxInstance.type !== "ERROR"
    ? (checkboxInstance.getBoolean("Checked", {
        true: true,
        false: undefined,
      }) ?? checkboxInstance.getEnum("State", { Checked: true }))
    : undefined
```

### Prefer mapping Figma props over ad-hoc snippet conditionals

Template files can use normal JavaScript conditionals, but for simple show/hide cases it is clearer to map the Figma property to the Lit value directly:

```ts
const label = figma.selectedInstance.getString("Label")
// Only provide an icon when the Figma "Show label" toggle is on
const iconSrc = figma.selectedInstance.getBoolean("Show label", {
  true: "chrome://example.svg",
  false: undefined,
})

export default {
  id: "moz-component",
  example: figma.code`
    <moz-component
      label="${label}"
      ${iconSrc ? `iconsrc="${iconSrc}"` : ""}
    ></moz-component>
  `,
}
```

### Match the case of what is present in Figma

Ensure that the names of any components, nested components, and properties that you use in the Code Connect file matches the name used in the Figma file.
For example, if a Figma property is named `sHoW dEsCrIpTiOn` (not that anyone would do that), it will not map correctly if it is referred to as "Show description" in the config file.

### What if something in Figma doesn't map back to anything in the implemented component?

Sometimes that's okay!
Some props will not map back to code and may be there to make using Figma easier.
For example, many of the options under the "State" select for a component do not get mapped to code.
Using the Nova "Checkbox" Figma component as an example again, there are various options for State:

- Default
- Hover
- Active
- Focus
- Disabled

There's nothing we have to pass to `<moz-checkbox>` for it to appear in a "Default" state, so we do not need to map this.
"Hover", "Active", and "Focus" are all CSS pseudo-classes that are handled by the code implementation, we don't have to pass any additional props for these states to behave as expected.
This leaves us with "Disabled" which is something that can be controlled through a property/attribute, therefore we want to make sure this attribute appears in the generated snippet when a Figma user selects "Disabled" in the dropdown.

```ts
const disabled = figma.selectedInstance.getEnum("State", { Disabled: true })
```

Other times, this could be a sign that there's a deviation in the design or the implemented component.
Maybe it's a feature that hasn't been implemented yet, and so we don't want to show any code snippets when that feature is enabled in Figma.
It could be something that was missed when previously creating the Code Connect config for a particular component, there could be a variety of reasons.
It may be worth reaching out to design to make sure the implemented component and the design component are not deviating from each other.

#### An example of a disrepancy between the Lit and Figma components

The following are properties of the `moz-message-bar` component as noted in `toolkit/content/widgets/moz-message-bar.mjs`.

```javascript
/**
 * @property {string} type - The type of the displayed message.
 * @property {string} heading - The heading of the message.
 * @property {string} message - The message text.
 * @property {boolean} dismissable - Whether or not the element is dismissable.
 * @property {string} messageL10nId - l10n ID for the message.
 * @property {string} messageL10nArgs - Any args needed for the message l10n ID.
 */
```

In Storybook, we are able to change the message, heading, and action button label through the Fluent tab.
This mostly maps to the properties in the Figma component, however, we can only update the action button label through the nested Button component.
Additionally, we generally do not include l10n properties in the Code Connect example snippets.
As the nested Button's properties are not currently exposed on the top level of the component, we are not able to reflect any changes to the Button in the Message Bar's example snippet.
Below, the `moz-button` code has been hard-coded.

![Figma user interface showing the message bar code snippet](./img/code-connect-message-bar.png)
