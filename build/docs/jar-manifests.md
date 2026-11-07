(jar-manifests)=

# JAR Manifests

JAR Manifests are plaintext files in the tree that are used to install chrome
files and create {doc}`Chrome Registration <chrome-registration>`
manifests. JAR Manifests are commonly named `jar.mn`. They are declared in `moz.build` files using the `JAR_MANIFESTS` variable, which makes up a collection of `jar.mn` files.
A JAR Manifest names the same installs `moz.build` already expresses, so what it
declares is installed the same way, and packaged into `omni.ja` files in
`browser/` and `toolkit/` when building Firefox.

`jar.mn` files are read by the build system when generating the build backend.
The `jar.mn` is run through the {ref}`preprocessor` before being parsed. In
order to have `@variables@` expanded (such as `@AB_CD@`) throughout the file,
add the line `#filter substitution` at the top of your `jar.mn` file.

The format of a `jar.mn` is fairly simple; it consists of a heading naming the
destination its entries are installed under, written with the historical
`.jar:` syntax, followed by indented lines listing files and chrome
registration instructions.

For a simple `jar.mn` file, see {searchfox}`toolkit/profile/jar.mn <mozilla-central/rev/5b2d2863bd315f232a3f769f76e0eb16cdca7cb0:toolkit/profile/jar.mn>`. For a much
more complex `jar.mn` file, see {searchfox}`toolkit/locales/jar.mn <mozilla-central/rev/5b2d2863bd315f232a3f769f76e0eb16cdca7cb0:toolkit/locales/jar.mn>`. More examples with specific formats and uses are available below.

## Shipping Chrome Files

### General Format

To ship chrome files, an indented line names a destination and the file
installed there:

```
<section>.jar:
  path/in/destination/file_name.xul     (source/tree/location/file_name.xul)
```

Note that file path mappings are listed by destination (left) followed by source (right).

### Same Directory Omission

If the JAR manifest and packaged files live in the same directory, the source path and parentheses can be omitted.
A sample of a `jar.mn` file with omitted source paths and parentheses is {searchfox}`this revision of browser/components/colorways/jar.mn <mozilla-central/rev/5b2d2863bd315f232a3f769f76e0eb16cdca7cb0:browser/components/colorways/jar.mn>`:

```
browser.jar:
  content/browser/colorwaycloset.html
  content/browser/colorwaycloset.css
  content/browser/colorwaycloset.js
```

Writing the following is equivalent, given that the aforementioned files exist in the same directory as the `jar.mn`. Notice the section is named `browser.jar`:

```
browser.jar:
  content/browser/colorwaycloset.html (colorwaycloset.html)
  content/browser/colorwaycloset.css  (colorwaycloset.css)
  content/browser/colorwaycloset.js   (colorwaycloset.js)
```

This manifest is responsible for packaging files needed by Colorway Closet, including
JS scripts, localization files, images (ex. PNGs, AVIFs), and CSS styling. Look at {searchfox}`browser/components/colorways/colorwaycloset.html <mozilla-central/rev/5b2d2863bd315f232a3f769f76e0eb16cdca7cb0:browser/components/colorways/colorwaycloset.html#18>`
to see how a file may be referenced using its chrome URL.

### Absolute Paths

The source tree location may also be an absolute path (taken from the top of the source tree).
One such example can be found in {searchfox}`toolkit/components/pictureinpicture/jar.mn <mozilla-central/rev/2005e8d87ee045f19dac58e5bff32eff7d01bc9b:toolkit/components/pictureinpicture/jar.mn>`:

```
toolkit.jar:
  * content/global/pictureinpicture/player.xhtml   (content/player.xhtml)
  content/global/pictureinpicture/player.js      (content/player.js)
```

### Asterisk Marker (Preprocessing)

An asterisk marker (`*`) at the beginning of the line indicates that the file should be processed by the {ref}`preprocessor` before being packaged.
The file {searchfox}`toolkit/profile/jar.mn <mozilla-central/rev/5b2d2863bd315f232a3f769f76e0eb16cdca7cb0:toolkit/profile/jar.mn>` indicates that the file {searchfox}`toolkit/profile/content/profileDowngrade.xhtml <mozilla-central/rev/2005e8d87ee045f19dac58e5bff32eff7d01bc9b:toolkit/profile/content/profileDowngrade.xhtml#34,36>` should be
run through the preprocessor, since it contains `#ifdef` and `#endif` statements that need to be interpreted:

```
* content/mozapps/profile/profileDowngrade.xhtml  (content/profileDowngrade.xhtml)
```

A local build installs a preprocessed file as a copy, so an edit to it needs
`./mach build faster` (see [In a Local Build](#in-a-local-build)).

### Base Path, Variables, Wildcards and Localized Files

The section name may be preceded with a base path between square brackets.
The file {searchfox}`toolkit/locales/jar.mn <mozilla-central/rev/5b2d2863bd315f232a3f769f76e0eb16cdca7cb0:toolkit/locales/jar.mn>` uses a base path so that the destination is under a `localization` directory,
which is a {searchfox}`special directory parsed by mozbuild <mozilla-central/rev/2005e8d87ee045f19dac58e5bff32eff7d01bc9b:python/mozbuild/mozpack/packager/l10n.py#260-265>`.

It is also named according to the value passed by the variable `@AB_CD@`, normally a locale. Note the use of the preprocessor directive `#filter substitution` at the top of the file for replacing the variable with the value:

```
#filter substitution

...

[localization] @AB_CD@.jar:
  crashreporter                                    (%crashreporter/**/*.ftl)
  toolkit                                          (%toolkit/**/*.ftl)
```

The percentage sign in front of the source paths designates the locale to target as a source. By default, this is `en-US`. With this specific example, {searchfox}`/toolkit/locales/en-US <toolkit/locales/en-US>` would be targeted.
Otherwise, the file from an alternate localization source tree `/l10n/<locale>/toolkit/` is read if building a localized version.
The wildcards in `**/*.ftl` tell the processor to install all Fluent files within the `crashreporter` and `toolkit` directories, as well as their subdirectories.

## Registering Chrome

{doc}`Chrome Registration <chrome-registration>` instructions are marked with a percent sign (`%`) at the beginning of the
line, and must be part of a section. Any additional percent signs are replaced
with an appropriate relative URL of the destination the section names.

A `<jarfilename>.manifest` is created next to the content it registers, and a
`manifest` line referring to it is added to the `chrome.manifest` of the
directory the content is installed into.

### Example

The file {searchfox}`browser/themes/addons/jar.mn <mozilla-central/rev/5b2d2863bd315f232a3f769f76e0eb16cdca7cb0:browser/themes/addons/jar.mn>` registers a `resource` chrome package under the name `builtin-themes`. Its source files are in `%content/builtin-themes/`:

```
browser.jar:
  %  resource builtin-themes %content/builtin-themes/

  content/builtin-themes/alpenglow                 (alpenglow/*.svg)
  content/builtin-themes/alpenglow/manifest.json   (alpenglow/manifest.json)
```

Notice how other files declare an installation destination using the `builtin-themes` resource that is defined. As such, a SVG file `preview.svg` for a theme `Alpenglow` may be loaded using the resource URL `resource://builtin-themes/alpenglow/preview.svg`
so that a preview of the theme is available on `about:addons`. See {doc}`Chrome Registration <chrome-registration>` for more details on `resource` and other manifest instructions.

## In a Local Build

A local build installs the files a JAR manifest names into the objdir unpacked;
`omni.ja` files are only made when the build is packaged.

- A plain entry is installed as a symlink to its source file, so an edit to the
  source file is live the next time Firefox starts, with no rebuild. Where the
  build cannot create a symlink, it copies the file instead, and the copy is
  only refreshed by the next `./mach build faster`.
- An entry marked with `*` is installed as a preprocessed copy on every
  platform, so an edit to it, or to any file it `#include`s, needs
  `./mach build faster` before the next start. `browser/base/content/browser.xhtml`
  is one, and an edit to an `.inc.xhtml` file it includes counts as an edit to
  it.
- The `chrome.manifest` files holding the
  {doc}`Chrome Registration <chrome-registration>` instructions are generated,
  and they and the installed files match the JAR manifests as of the last build.
  After the source tree moves to another revision, such as after a rebase or a
  branch switch, a file the new revision added is not installed yet. Loading it
  prints `Missing chrome or resource URL:` with its URL, or crashes with
  `Missing chrome or resource URLs:` in automation, until `./mach build faster`
  installs the new revision's files and regenerates the manifests.
