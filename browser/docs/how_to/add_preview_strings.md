# Adding preview strings while a feature is under active development

## Overview

When a feature is under active development, its UI strings often change across
multiple release cycles. Translating these unstable strings wastes time for our
localization community, since they may need to be retranslated repeatedly before
the feature ships.

To address this, Firefox supports **preview strings**. Preview strings live in
Fluent files and ship with Firefox, but they are excluded from localization.
Engineers can update these strings freely during development without renaming
them or triggering translation work.

Preview strings must still use Fluent. **Hard-coding strings in JavaScript or
markup is not allowed**, because it is easy to miss replacing them later. The
only exception is for internal debugging pages (for example,
`about:checkerboard`) that are never shown to users in release builds.

Once a feature’s strings are finalized, preview strings can be moved out of
preview so they are translated and included in release builds.

## Prerequisites

It is assumed that you are already familiar with Fluent. If not,
you can read more about it in the [Fluent tutorial](../../l10n/fluent/tutorial.md).

It is also assumed that what you're working on will either be disabled by
default, held to Nightly, or be part of an English-only experiment.

## Steps

### 1. Create a preview Fluent file

This is the file where you will put your preview strings. Create this file under
`browser/locales-preview/`.

For this how-to, we'll call this Fluent file `myNewFeature.ftl`.

**Files to modify:**

- `browser/locales-preview/myNewFeature.ftl`

**Code pattern:**

```ftl
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

my-new-feature-header = { -brand-short-name } is the best
```

**Explanation:**

Put preview Fluent files in `browser/locales-preview/`. Other locations may
work, but this is the supported convention.

### 2. Register the file in jar.mn

**Files to modify:**

- `browser/locales/jar.mn`

**Code pattern:**

```
preview/myNewFeature.ftl      (../locales-preview/myNewFeature.ftl)
```

**Explanation:**

We're mapping the Fluent file on disk to the path that will ultimately be used
to access it in script and markup (`"preview/myNewFeature.ftl"`).

### 3. Load the preview Fluent file in the areas where you need it

Supposing you need the strings in the main browser window, that would be in the
`<head>` of the `browser.xhtml` document. If you needed the strings inside of
the preferences UI, that'd be within the `<head>` of the `preferences.xhtml`
page. If you're not sure where it makes sense to add your strings, ask around.

**Code pattern:**

:::{note}
Our Fluent strings are only available in chrome and in-content browser contexts,
and not in web content contexts. The patterns below are meant to be run within
browser documents and scripts.
:::

```html
<head>
...
  <link rel="localization" href="preview/myNewFeature.ftl" />
</head>
```

It's also possible to load these preview strings in script:

```js
let myWorkInProgressFeatureStrings = new Localization([
  "preview/myNewFeature.ftl",
]);
```

### 4. Build to include the preview file

Use `./mach build faster` or `./mach build` to ensure that your new Fluent file
gets included in the asset package in your local build.

### 5. Use the strings as normal

Build out your feature and ensure that the strings can be accessed like you
would from any non-preview Fluent file.

If you cannot access them, ensure that you loaded them in the right markup /
script. Check the Browser Console for any errors that might indicate load
failures.

### 6. File a bug to move the strings out preview, blocking wide release

**This step is important.** Preview strings do not get translated. File a
tracking bug blocking release so the strings are moved out of preview before
shipping.

### 7. When the strings are ready to be translated, move them out of preview

Once you've reached this point, the strings come out of preview, and will
ride the trains to the Beta channel so that they can be translated.

**Files to modify:**

- `browser/locales-preview/myNewFeature.ftl`
- `browser/locales/jar.mn`
- Anywhere that you used `"preview/myNewFeature.ftl"`.

**Explanation:**

You need to move the Fluent file to where the rest of the non-preview Fluent
files are. That's under `browser/locales/en-US/browser`. Either move the file
directly under that folder, or if the feature is likely to have several Fluent
files, place it under a folder within `browser/locales/en-US/browser` for the
feature.

Then, you can remove the entry that you added in `browser/locales/jar.mn` in
Step 2. Your Fluent file will be automatically registered to be packaged when
placed within `browser/locales/en-US/browser`.

Finally, update all of the places where you used `preview/myNewFeature.ftl` in
markup and script to use `browser/myNewFeature.ftl` or
`browser/myFeatureFolder/myNewFeature.ftl` instead.

## Verification

Finally, run `./mach build faster` or `./mach build` and ensure that your
strings still appear as you would expect.

## See Also

- [Fluent for Firefox Developers](../../l10n/fluent/tutorial.md)
- [A commit that created some preview strings](https://github.com/mozilla-firefox/firefox/commit/f680db26877b03660bec4fd1648e8f1cfb559f18)
  - [A commit that later moved those strings out of preview](https://github.com/mozilla-firefox/firefox/commit/74b69afbf45d3e97d4a4f8ed64bae3f976640ce3)
