# Dynamic Result Types

This document discusses a special category of address bar results called dynamic
result types. Dynamic result types allow you to easily add new types of results
to the address bar.

The intended audience for this document is developers who need to add new kinds
of address bar results.

```{contents}
:depth: 2
```

## Motivation

The address bar provides many different types of results in normal Firefox
usage. For example, when you type a search term, the address bar may show you
search suggestion results from your current search engine. It may also show you
results from your browsing history that match your search. If you typed a
certain phrase like "update Firefox," it will show you a tip result that lets
you know whether Firefox is up to date.

Each of these types of results is built into the address bar implementation. If
you wanted to add a new type of result -- say, a card that shows the weather
forecast when the user types "weather" -- one way to do so would be to add a new
result type. You would need to update all the code paths in the address bar that
relate to result types. For instance, you'd need to update the code path that
handles clicks on results so that your weather card opens an appropriate
forecast URL when clicked; you'd need to update the address bar view (the panel)
so that your card is drawn correctly; you may need to update the keyboard
selection behavior if your card contains elements that can be independently
selected such as different days of the week; and so on.

## Dynamic Result Types

**Dynamic result types** are an alternative way of implementing new result
types. Instead of adding a new built-in type along with all that entails, you
add a new provider subclass that provides a template describing how the view
should draw your result type and indicates which elements are selectable. The
address bar takes care of everything else.

Dynamic result types are essentially an abstraction layer: Support for them as a
general category of results is built into the address bar, and each
implementation of a specific dynamic result type fills in the details.

In addition, dynamic result types can be added at runtime.

## Getting Started

To get a feel for how dynamic result types are implemented, you can look at the
{searchfox}`UrlbarProviderCalculator <browser/components/urlbar/UrlbarProviderCalculator.sys.mjs>`.

The next section describes the specific steps you need to take to add a new
dynamic result type.

## Implementation Steps

This section describes how to add a new dynamic result type.

### 1. Add the provider

As with any type of result, results for dynamic result types must be created by
one or more providers. Make a `UrlbarProvider` subclass for the new provider
and implement all the usual provider methods as you normally would:

```javascript
class MyDynamicResultTypeProvider extends UrlbarProvider {
  // ...
}
```

The `startQuery` method should create `UrlbarResult` objects with the
following two requirements:

- Result types must be `UrlbarShared.RESULT_TYPE.DYNAMIC`.
- Result payloads must have a `dynamicType` property. It will be used in DOM
  IDs, DOM class names, and CSS selectors, so it should not contain any spaces
  or other characters that are invalid in CSS.

The results' sources, other payload properties, and other result properties
aren't relevant to dynamic result types, and you should choose values
appropriate to your use case.

If any elements created in the view for your results can be picked with the
keyboard or mouse, then be sure to implement your provider's
`onEngagement` method.

For help on implementing providers in general, see the address bar's
[Architecture Overview](https://firefox-source-docs.mozilla.org/browser/urlbar/overview.html#urlbarprovider).

If you are creating the provider in the internal address bar implementation in
mozilla-central, then don't forget to register it in a `ProvidersManager`
instance.

### 2. Implement the provider's getViewTemplate method

`getViewTemplate` is a provider method particular to dynamic result type
providers. It is called by the providers manager for each result the provider
produces and returns a **view template**: a plain object that declaratively
describes the DOM subtree to build for the result. The manager stores it on the
result as `payload.viewTemplate` and the view uses it when it builds the row.
See [View Templates] for the shape of this object.

Add the `getViewTemplate` method to the provider:

```javascript
/**
 * @param {UrlbarResult} result
 *   The result whose view will be created.
 * @returns {ViewTemplate}
 *   The view template describing the DOM to build for the result.
 */
getViewTemplate(result) {
  return {
    children: // ...
  };
}
```

Because `getViewTemplate` is called per result, a provider can return
different templates for different results if needed. In practice most
providers return the same static template regardless of the result.

### 3. Implement the provider's getViewUpdate method

`getViewUpdate` is a provider method particular to dynamic result type
providers. Its job is to update the view DOM for a specific result. It's called
by the providers manager for each result the provider produces, and returns an
object called a view update object. The manager stores it on the result as
`payload.viewUpdate` and the view applies once it has built the row.

The view template from step 2 describes how to build the DOM structure for all
results of the dynamic result type. The view update object, in this step,
describes how to fill in that structure for a specific result.

Add the `getViewUpdate` method to the provider:

```javascript
/**
 * Returns a view update object that describes how to update the view DOM
 * for a given result.
 *
 * @param {UrlbarResult} result
 *   The view update object describes how to update the view DOM for this
 *   particular result.
 */
getViewUpdate(result) {
  let viewUpdate = {
    // ...
  };
  return viewUpdate;
}
```

`result` is the result from the provider for which the view update is being
requested.

The return value is a view update object. It describes in a declarative manner
the updates that should be performed on the view DOM. See [View Update Objects]
for a description of this object.

### 4. Style the results

If you are creating the provider in the internal address bar implementation in
mozilla-central, then add styling to {searchfox}`dynamic-results.css <browser/themes/shared/urlbar/dynamic-results.css>`.

The rest of this section will discuss the CSS rules you need to use to style
your results.

There are two DOM annotations that are useful for styling. The first is the
`dynamicType` attribute that is set on result rows, and the second is a class
that is set on child elements created from the view template.

#### dynamicType Row Attribute

The topmost element in the view corresponding to a result is called a
**row**. Rows have a class of `urlbarView-row`, and rows corresponding to
results of a dynamic result type have an attribute called `dynamicType`. The
value of this attribute is the name of the dynamic result type that was chosen
in step 1 earlier.

Rows of a specific dynamic result type can therefore be selected with the
following CSS selector, where `TYPE_NAME` is the name of the type:

```css
.urlbarView-row[dynamicType=TYPE_NAME]
```

#### Child Element Class

As discussed in [View Templates], each object in the view template can have a
`name` property. The elements in the view corresponding to the objects in the
view template receive a class named
`urlbarView-dynamic-TYPE_NAME-ELEMENT_NAME`, where `TYPE_NAME` is the name
of the dynamic result type, and `ELEMENT_NAME` is the name of the object in
the view template.

Elements in dynamic result type rows can therefore be selected with the
following:

```css
.urlbarView-dynamic-TYPE_NAME-ELEMENT_NAME
```

If an object in the view template does not have a `name` property, then it
won't receive the class and it therefore can't be selected using this selector.

## View Templates

A **view template** is a plain JS object that declaratively describes how to
build the DOM for a dynamic result type. When a result of a particular dynamic
result type is shown in the view, the type's view template is used to construct
the part of the view that represents the type in general.

### Properties

A view template object is a tree-like nested structure where each object in the
nesting represents a DOM element to be created. This tree-like structure is
achieved using the `children` property described below. Each object in the
structure may include the following properties:

`{string} name`

: The name of the object. This is required for all objects in the structure
  except the root object and serves two important functions:

  1. The element created for the object will automatically have a class named
     `urlbarView-dynamic-${dynamicType}-${name}`, where `dynamicType` is the
     name of the dynamic result type. The element will also automatically have
     an attribute `name` whose value is this name. The class and attribute
     allow the element to be styled in CSS.
  2. The name is used when updating the view, as described in [View Update
     Objects](#view-update-objects).

  Names must be unique within a view template, but they don't need to be
  globally unique. In other words, two different view templates can use the same
  names, and other unrelated DOM elements can use the same names in their IDs
  and classes.

`{string} tag`

: The element tag name of the object. This is required for all objects in the
  structure except the root object and declares the kind of element that will be
  created for the object: `span`, `div`, `img`, etc.

`{object} [attributes]`

: An optional mapping from attribute names to values. For each name-value pair,
  an attribute is set on the element created for the object.

  A special `selectable` attribute tells the view that the element is
  selectable with the keyboard. The element will automatically participate in
  the view's keyboard selection behavior.

  Similarly, the `role=button` ARIA attribute will also automatically allow
  the element to participate in keyboard selection. The `selectable` attribute
  is not necessary when `role=button` is specified.

`{array} [children]`

: An optional list of children. Each item in the array must be an object as
  described in this section. For each item, a child element as described by the
  item is created and added to the element created for the parent object.

`{array} [classList]`

: An optional list of classes. Each class will be added to the element created
  for the object by calling `element.classList.add()`.

### Example

Let's return to the weather forecast example from [earlier](#motivation). For
each result of our weather forecast dynamic result type, we might want to
display a label for a city name along with two buttons for today's and
tomorrow's forecasted high and low temperatures. The view template might look
like this:

```javascript
{
  children: [
    {
      name: "cityLabel",
      tag: "span",
    },
    {
      name: "today",
      tag: "div",
      classList: ["day"],
      attributes: {
        selectable: true,
      },
      children: [
        {
          name: "todayLabel",
          tag: "span",
          classList: ["dayLabel"],
        },
        {
          name: "todayLow",
          tag: "span",
          classList: ["temperature", "temperatureLow"],
        },
        {
          name: "todayHigh",
          tag: "span",
          classList: ["temperature", "temperatureHigh"],
        },
      },
    },
    {
      name: "tomorrow",
      tag: "div",
      classList: ["day"],
      attributes: {
        selectable: true,
      },
      children: [
        {
          name: "tomorrowLabel",
          tag: "span",
          classList: ["dayLabel"],
        },
        {
          name: "tomorrowLow",
          tag: "span",
          classList: ["temperature", "temperatureLow"],
        },
        {
          name: "tomorrowHigh",
          tag: "span",
          classList: ["temperature", "temperatureHigh"],
        },
      },
    },
  ],
}
```

Observe that we set the special `selectable` attribute on the `today` and
`tomorrow` elements so they can be selected with the keyboard.

## View Update Objects

A **view update object** is a plain JS object that declaratively describes how
to update the DOM for a specific result of a dynamic result type. When a result
of a dynamic result type is shown in the view, a view update object is requested
from the result's provider and is used to update the DOM for that result.

Note the difference between view update objects, described in this section, and
view templates, described in the previous section. View templates are used to
build a general DOM structure appropriate for all results of a particular
dynamic result type. View update objects are used to fill in that structure for
a specific result.

When a result is shown in the view, the view builds a DOM subtree from the view
template the result's provider returned from ``getViewTemplate``. Next, the
view requests a view update object for the result from its provider. The view
update object tells the view which result-specific attributes to set on which
elements, result-specific text content to set on elements, and so on. View
update objects cannot create new elements or otherwise modify the structure
of the result's DOM subtree.

Typically the view update object is based on the result's payload.

### Properties

The view update object is a nested structure with two levels. It looks like
this:

```javascript
{
  name1: {
    // individual update object for name1
  },
  name2: {
    // individual update object for name2
  },
  name3: {
    // individual update object for name3
  },
  // ...
}
```

The top level maps object names from the view template to individual update
objects. The individual update objects tell the view how to update the elements
with the specified names. If a particular element doesn't need to be updated,
then it doesn't need an entry in the view update object.

Each individual update object can have the following properties:

`{object} [attributes]`

: A mapping from attribute names to values. Each name-value pair results in an
  attribute being set on the element.

`{object} [style]`

: A plain object that can be used to add inline styles to the element, like
  `display: none`. `element.style` is updated for each name-value pair in
  this object.

`{object} [l10n]`

: An `{ id, args }` object that will be passed to
  `document.l10n.setAttributes()`.

`{string} [textContent]`

: A string that will be set as `element.textContent`.

### Example

Continuing our weather forecast example, the view update object needs to update
several things that we declared in our view template:

- The city label
- The "today" label
- Today's low and high temperatures
- The "tomorrow" label
- Tomorrow's low and high temperatures

Typically, each of these, with the possible exceptions of the "today" and
"tomorrow" labels, would come from our results' payloads. There's an important
connection between what's in the view and what's in the payloads: The data in
the payloads serves the information shown in the view.

Our view update object would then look something like this:

```javascript
{
  cityLabel: {
    textContent: result.payload.city,
  },
  todayLabel: {
    textContent: "Today",
  },
  todayLow: {
    textContent: result.payload.todayLow,
  },
  todayHigh: {
    textContent: result.payload.todayHigh,
  },
  tomorrowLabel: {
    textContent: "Tomorrow",
  },
  tomorrowLow: {
    textContent: result.payload.tomorrowLow,
  },
  tomorrowHigh: {
    textContent: result.payload.tomorrowHigh,
  },
}
```

## Accessibility

Just like built-in types, dynamic result types support a11y in the view, and you
should make sure your view implementation is fully accessible.

Since the views for dynamic result types are implemented using view templates
and view update objects, in practice supporting a11y for dynamic result types
means including appropriate [ARIA attributes][aria] in the view template and
view update objects, using the `attributes` property.

The view assigns the IDs of the elements it creates for a dynamic result, but
`getViewUpdate` runs before the row exists, so providers can't refer to them.
ARIA attributes that take an ID reference therefore can't be set from a
provider today, but it's possible to add support for that in the future.

Usually, accessible address bar results require the ARIA attribute
`role=group` on their top-level DOM element to indicate that all the child
elements in the result's DOM subtree form a logical group. This attribute can be
set on the root object in the view template.

### Example

Continuing the weather forecast example, we'd like for screen readers to
announce the city when the result is selected. Since we can't point
`aria-labelledby` at the city label's element, set `aria-label` on the root from
the view update instead, where the city name is available:

```javascript
getViewUpdate(result) {
  return {
    root: {
      attributes: {
        "aria-label": result.payload.city,
      },
    },
    // *snipping the view update object example from earlier*
  };
}
```

Here we're using the name "root" to refer to the root object in the view
template, so we also need to update our view template by adding the `name`
property to the top-level object, like this:

```javascript
{
  name: "root",
  attributes: {
    role: "group",
  },
  children: [
    {
      name: "cityLabel",
      tag: "span",
    },
    // *snipping the view template example from earlier*
  ],
}
```

Note that we've also included the `role=group` ARIA attribute on the root, as
discussed above. We could have included it in the view update object instead of
the view template, but since it doesn't depend on a specific result, the view
template makes more sense.

## Mimicking Built-in Address Bar Results

Sometimes it's desirable to create a new result type that looks and behaves like
the usual built-in address bar results. Two conveniences are available that are
useful in this case.

### URL Navigation

If a result's payload includes a string `url` property, picking the result
will navigate to the URL. The `onEngagement` method of the result's provider
will be called before navigation.

### Text Highlighting

Most built-in address bar results emphasize occurrences of the user's search
string in their text by boldfacing matching substrings. Search suggestion
results do the opposite by emphasizing the portion of the suggestion that the
user has not yet typed. This emphasis feature is called **highlighting**, and
it's also available to the results of dynamic result types.

Highlighting for dynamic result types is a fairly automated process. The text
that you want to highlight must be present as a property in your result
payload. Instead of setting the property to a string value as you normally
would, set it to an array with two elements, where the first element is the text
and the second element is a `UrlbarShared.HIGHLIGHT` value, like the `title`
payload property in the following example:

```javascript
let result = new UrlbarResult({
  type: UrlbarShared.RESULT_TYPE.DYNAMIC,
  source: UrlbarShared.RESULT_SOURCE.OTHER_NETWORK,
  payload: {
    title: [
      "Some result title",
      UrlbarShared.HIGHLIGHT.TYPED,
    ],
    // *more payload properties*
  }
});
```

Your view template must create an element corresponding to the payload
property. That is, it must include an object where the value of the `name`
property is the name of the payload property, like this:

```javascript
{
  children: [
    {
      name: "title",
      tag: "span",
    },
    // ...
  ],
}
```

In contrast, your view update objects must *not* include an update for the
element. That is, they must not include a property whose name is the name of the
payload property.

Instead, when the view is ready to update the DOM of your results, it will
automatically find the elements corresponding to the payload property, set their
`textContent` to the text value in the array, and apply the appropriate
highlighting, as described next.

There are two possible `UrlbarShared.HIGHLIGHT` values. Each controls how
highlighting is performed:

`UrlbarShared.HIGHLIGHT.TYPED`

: Substrings in the payload text that match the user's search string will be
  emphasized.

`UrlbarShared.HIGHLIGHT.SUGGESTED`

: If the user's search string appears in the payload text, then the remainder of
  the text following the matching substring will be emphasized.

## Appendix A: Examples

This section lists some example and real-world consumers of dynamic result
types.

{searchfox}`Tab-to-Search Provider <browser/components/urlbar/UrlbarProviderTabToSearch.sys.mjs>`

: This is a built-in provider in mozilla-central that uses dynamic result types.

[aria]: https://developer.mozilla.org/en-US/docs/Web/Accessibility/ARIA
