# AMRemoteSettings Overview

`AMRemoteSettings` is a singleton that is responsible for fetching data from a RemoteSettings collection named
`"main/addons-manager-settings"` to remotely control a set of Add-ons related preferences (hardcoded inside
the AMRemoteSettings definition part of a given Firefox release).

`AMRemoteSettings` will process the collection data when the RemoteSettings `"sync"` event is fired and on
late browser startup if there is already some data stored locally in the collection (and so if the pref value is
manually changed, then it would be set again to the value stored in the RemoteSettings collection data, either
on the next browser startup or on the next time the `"sync"` event is fired again).

:::{warning}
Any <about:config> preference that AMRemoteSettings singleton sets while processing the RemoteSettings collection data
are **never cleared automatically** (e.g. if in a future Firefox version AMRemoteSettings does not handle a particular
settings anymore, or if the RemoteSettings entry is removed from the collection on the RemoteSettings service side).

In practice this makes this feature a good choice when the remotely controlled <about:config> preferences is related to:

- Firefox Add-ons features in the process of being deprecated and then fully removed in a future Firefox release
- or controlled settings that are never removed from the `"main/addons-manager-settings"` collection.

In general before changing controlled settings in the `"main/addons-manager-settings"` collection it is advisable to:

- make sure to default value for the related <about:config> preferences has been changed in the `main` branch first,
  and to let it ride the release train and get some backing time on release;
- only after that consider changing the value controlled by `"main/addons-manager-settings"` collection,
  to set the same value on older Firefox releases where the previous default value was set.
:::

The `AMRemoteSettings` singleton queries RemoteSettings and processes all the entries got from the
`"main/addons-manager-settings"` collection, besides entries that may be filtered out by RemoteSettings based on
the `"filter_expression"` property (See <https://remote-settings.readthedocs.io/en/latest/target-filters.html#how>).

Each of the entries created in the `"main/addons-manager-settings"` collection and is expected to match the JSONSchema
described in the {ref}`JSON Schema section below <json-schema>`.

For each entry found in the collection, only the properties that are explicitly included in the
`AMRemoteSettings.RS_ENTRIES_MAP` object are going to be processed (e.g. new Firefox versions may have introduced new
ones that older Firefox version will just ignore):

- Each of the `AMRemoteSettings.RS_ENTRIES_MAP` properties:

  - represents a group of settings (e.g. the property named `"installTriggerDeprecation"`) is responsible of controlling
    <about:config> preferences related to the InstallTrigger deprecation
  - is set to an array of string, which lists the <about:config> preferences names that can actually be controlled by the
    related group of settings (e.g. `"installTriggerDeprecation"` can only control one preference,
    `"extensions.InstallTrigger.enabled"`, which controls the visibility of window.InstallTrigger).

:::{warning}
Any other <about:config> preference names that are not listed explicitly in the `AMRemoteSettings.RS_ENTRIES_MAP` config
is expected to be ignored, even if allowed by a new version of the collection's JSONSchema (this is the expected behavior
and prevents the introduction of unexpected behaviors on older Firefox versions that may not be expecting new settings groups
that may be introduced in Firefox releases that followed it).

Any <about:config> preference with an unexpected value type is going to be ignored as well (look to the `AMRemoteSettings.processEntries`
to confirm which preferences values types are already expected and handled accordingly).
:::

(controlled-settings-groups)=

# AMRemoteSettings - Controlled Settings Groups

## installTriggerDeprecation

Group of settings to control InstallTrigger deprecation (Bug 1754441)

- **extensions.InstallTrigger.enabled** (boolean), controls the availability of the InstallTrigger global:

  - Turning this to false will be hiding it completely

    :::{note}
    Turning this off can potentially impact UA detection on website being using it to detect
    Firefox. The WebCompat team should be consulted before setting this to `false` by default in
    Nightly or across all Firefox >= 100 versions through the `"addons-manager-settings"`
    RemoteSettings collection).
    :::

## quarantinedDomains

Group of settings to control the list of quarantined domains (Bug 1832791)

- **extensions.quarantinedDomains.list** (string), controls the list of domains to be quarantined

  :::{note}
  The WebExtensions and Add-ons Operations teams should be consulted before applying changes to
  the list of quarantined domains.
  :::

## How to define new remotely controlled settings

:::{note}
Please update the {ref}`JSON Schema <json-schema>` and {ref}`UI Schema <ui-schema>` in this documentation page updated when the ones used on the
RemoteSettings service side have been changed, and document new controlled settings in the section {ref}`Controlled Settings Groups <controlled-settings-groups>`.
:::

- Confirm that the {ref}`JSON Schema <json-schema>` and {ref}`UI Schema <ui-schema>` included in this page are in sync with the one actually used on the
  RemoteSettings service side, and use it as the starting point to update it to include a new type on remotely controlled setting:

  - choose a new unique string for the group of settings to be used in the `definitions` and `properties`
    objects (any that isn't already used in the existing JSON Schema `definitions`), possibly choosing a name
    that helps to understand what the purpose of the entry.

  - add a new JSONSchema for the new group of settings in the `definitions` property

    - each of the properties included in the new definition should be named after the name of the <about:config> pref
      being controlled, their types should match the type expected by the pref (e.g. `"boolean"` for a boolean preference).
    - make sure to add a description property to the definition and to each of the controlled preferences, which should
      describe what is the settings group controlling and what is the expected behavior on the values allowed.

- Add a new entry to `"AMRemoteSettings.RS_ENTRIES_MAP"` with the choosen `"id"` as its key and
  the array of the <about:config> preferences names are its value.

- If the value type of a controlled preference isn't yet supported, the method `AMRemoteSettings.processEntries` has to be
  updated to handle the new value type (otherwise the preference value will be just ignored).

- Add a new test to cover the expected behaviors on the new remotely controlled settings, the following RemoteSettings
  documentation page provides some useful pointers:

  - [Unit tests](/services/settings/index.md#unit-tests)

- Refer to the RemoteSettings docs for more details about managing the JSONSchema for the `"main/addons-manager-settings"`
  collection and how to test it interactively in a Firefox instance:

  - <https://remote-settings.readthedocs.io/en/latest/getting-started.html>
  - [Create new remote settings](/services/settings/index.md#create-new-remote-settings)
  - [Remote Settings Dev Tools](/services/settings/index.md#remote-settings-dev-tools)

(json-schema)=

# AMRemoteSettings - JSON Schema

The entries part of the `"addons-manager-settings"` collection are validated using a JSON Schema structured as follows:

- The mandatory `"id"` property

  - defaults to `"AddonManagerSettings"` (which enforces only one entry in the collection as the preferred use case)
  - **should NOT be changed unless there is a specific need to create separate collection entries which target or exclude specific Firefox versions.**
  - when changed and multiple entries are created in this collection, it is advisable to:

    - set the id to a short string value that make easier to understand the purpose of the additional entry in the collection
      (eg. `AddonManagerSettings-fx98-99` for an entry created that targets Firefox 98 and 99)
    - make sure only one applied to each targeted Firefox version ranges, or at least that each entry is controlling a different settings group

- Each supported group of controlled settings is described by its own property (e.g. `"installTriggerDeprecation"`)

  - JSON Schema for each group of settings is defined in an entry of the `"definitions"` property.
  - The definition for each of the groups defined in the schema should be defined as a `"oneOf"` array including an entry
    of `"type": "null"` and ``` "default"` set to ``null ``` to omit the group of settings by default in new records.
  - In addition to the `"type": "null"` schema, each group of settings is expected to include in the `"oneOf"` array
    a second entry of `"type": "object"` and the controlled <about:config> preferences part of the group listed in
    the `"properties"`.

```{literalinclude} ./AMRemoteSettings-JSONSchema.json
:language: json
```

(ui-schema)=

## UI Schema

In addition to the JSON Schema, a separate json called `"UI schema"` is associated to the `"addons-manager-settings"` collection,
and it can be used to customize the form auto-generated based on the JSONSchema data.

:::{note}
Extending this schema is only needed if it can help to make the RemoteSettings collection easier to manage
and less error prone.
:::

```{literalinclude} ./AMRemoteSettings-UISchema.json
:language: json
```
