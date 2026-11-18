```{eval-rst}
.. role:: bash(code)
   :language: bash
```

```{eval-rst}
.. role:: js(code)
   :language: javascript
```

```{eval-rst}
.. role:: python(code)
   :language: python

```

# Fluent to Fluent Migrations

Fluent migrations are used to preserve translations in instances where
strings or files are refactored, resulting in changes that would normally
require a string be translated from scratch (e.g. a [string ID change](../fluent/review.md#changes-to-existing-messages)).
See the [overview](./overview.md) for an explanation of localized string
migrations in general.

:::{important}
**Every migration recipe must be tested locally before you request review.**

```bash
./mach fluent-migration-test python/l10n/fluent_migrations/bug_<number>_<slug>.py
```
Run it, read the summary it prints, and resolve every `ERROR` and `WARNING`
before submitting. See {doc}`testing` for the full output format.
:::

When migrating existing Fluent messages,
it's possible to copy a source directly with {python}`COPY_PATTERN`,
or to apply string replacements and other changes
by extending the {python}`TransformPattern` visitor class.

These transforms work with individual Fluent patterns,
i.e. the body of a Fluent message or one of its attributes.

## Copying Fluent Patterns

Consider for example a patch modifying an existing message to move the original
value to a {js}`alt` attribute.

Original message:

```fluent
about-logins-icon = Warning icon
    .title = Breached website
```

New message:

```fluent
about-logins-breach-icon =
    .alt = Warning icon
    .title = Breached website
```

This type of changes requires a new message identifier, which in turn causes
existing translations to be lost. It’s possible to migrate the existing
translated content with:

```python
from fluent.migrate import COPY_PATTERN

ctx.add_transforms(
    "browser/browser/aboutLogins.ftl",
    "browser/browser/aboutLogins.ftl",
    transforms_from(
        """
about-logins-breach-icon =
    .alt = {COPY_PATTERN(from_path, "about-logins-icon")}
    .title = {COPY_PATTERN(from_path, "about-logins-icon.title")}
""",
        from_path="browser/browser/aboutLogins.ftl",
    ),
)
```

In this specific case, the destination and source files are the same. The dot
notation is used to access attributes: {js}`about-logins-icon.title` matches
the {js}`title` attribute of the message with identifier
{js}`about-logins-icon`, while {js}`about-logins-icon` alone matches the value
of the message.

:::{warning}
The second argument of {python}`COPY_PATTERN` and {python}`TransformPattern`
identifies a pattern, so using the message identifier will not
migrate the message as a whole, with all its attributes, only its value.
:::

## Transforming Fluent Patterns

To apply changes to Fluent messages, you may extend the
{python}`TransformPattern` class to create your transformation.
This is a powerful general-purpose tool, of which {python}`COPY_PATTERN` is the
simplest extension that applies no transformation to the source.

Consider for example a patch copying an existing message to strip out its HTML
content to use as an ARIA value.

Original message:

```fluent
videocontrols-label =
    { $position }<span data-l10n-name="duration"> / { $duration }</span>
```

New message:

```fluent
videocontrols-scrubber =
    .aria-valuetext = { $position } / { $duration }
```

A migration may be applied to create this new message with:

```python
from fluent.migrate.transforms import TransformPattern
import fluent.syntax.ast as FTL


class STRIP_SPAN(TransformPattern):
    def visit_TextElement(self, node):
        node.value = re.sub("</?span[^>]*>", "", node.value)
        return node


def migrate(ctx):
    path = "toolkit/toolkit/global/videocontrols.ftl"
    ctx.add_transforms(
        path,
        path,
        [
            FTL.Message(
                id=FTL.Identifier("videocontrols-scrubber"),
                attributes=[
                    FTL.Attribute(
                        id=FTL.Identifier("aria-valuetext"),
                        value=STRIP_SPAN(path, "videocontrols-label"),
                    ),
                ],
            ),
        ],
    )
```

Note that a custom extension such as {python}`STRIP_SPAN` is not supported by
the {python}`transforms_from` utility, so the list of transforms needs to be
defined explicitly.

Internally, {python}`TransformPattern` extends the [fluent.syntax](https://projectfluent.org/python-fluent/fluent.syntax/stable/)
{python}`Transformer`, which defines the {python}`FTL` AST used here.
As a specific convenience, pattern element visitors such as
{python}`visit_TextElement` are allowed to return a {python}`FTL.Pattern`
to replace themselves with more than one node.

## Common Migration Recipe Patterns

Every example below is taken from a recipe that landed in the Firefox repository in
`python/l10n/fluent_migrations`, but be aware that the folder is pruned
intermittently, so they may no longer exist in tree. An archive of
migration recipes can be found [here](https://github.com/mozilla-l10n/fluent-migrations).

:::{tip}
Start with the `/fluent-migration` Claude skill

Most migrations fall into the common shapes catalogued below and
an in-tree Claude skill exists that handles them:
`.claude/skills/fluent-migration/SKILL.md`.

Agents that read the `.claude/skills/` directory pick it up automatically,
but you can also invoke it explicitly with `/fluent-migration`.
It reads the `.ftl` diff, classifies each changed string,
writes the recipe, and runs `./mach fluent-migration-test` for you.

Treat its output as a first draft that you still review and test before submitting.
:::

Each recipe is complete and ready to copy as a template.
Be sure to replace the bug number, the docstring, and the paths, and keep
`part {index}` as it is.

Each recipe is a snippet of an actual migration, click the "Context" link
to see the whole diff to see the entire context.

### Removing an attribute

Each part the new message keeps is copied from its counterpart, and the dropped
attribute is simply never referenced. ([Context](https://phabricator.services.mozilla.com/D307071))

```python
# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 2048020 - Containers: remove description in the about:preferences#containers for the '+' policy, part {index}."""

    source = "browser/browser/preferences/preferences.ftl"
    target = source

    ctx.add_transforms(
        target,
        target,
        transforms_from(
            """
containers-new-tab-check3 =
    .label = { COPY_PATTERN(from_path, "containers-new-tab-check2.label") }
    .accesskey = { COPY_PATTERN(from_path, "containers-new-tab-check2.accesskey") }
""",
            from_path=source,
        ),
    )
```

The old `containers-new-tab-check2` also had a `.description`, which the new
message drops and the recipe never mentions. Every part the new message does
keep has to be copied.

### Moving text between values and attributes

Text can move in either direction, since {python}`COPY_PATTERN` addresses a value
with `"id"` and an attribute with `"id.attr"`. ([Context](https://phabricator.services.mozilla.com/D289161))

```python
# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 1998985 - Use HTML elements for search mode switcher, part {index}."""

    source = "browser/browser/browser.ftl"

    ctx.add_transforms(
        source,
        source,
        transforms_from(
            """
urlbar-searchmode-button3 =
    .title = {COPY_PATTERN(from_path, "urlbar-searchmode-button2.tooltiptext")}

urlbar-searchmode-bookmarks2 = {COPY_PATTERN(from_path, "urlbar-searchmode-bookmarks.label")}

urlbar-searchmode-popup-add-engine = {COPY_PATTERN(from_path, "search-one-offs-add-engine.label")}
    .title = {COPY_PATTERN(from_path, "search-one-offs-add-engine.tooltiptext")}
""",
            from_path=source,
        ),
    )
```

### Adding an attribute that reuses existing text

The new attribute (`.aria-label`) doesn't exist in the previous version of the source string,
so it's copied from somewhere else. In this case the `.title` attribute of the previous string.
([Context](https://phabricator.services.mozilla.com/D306883))

```python
# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 2040000 - Add Back button to preferences search results header, part {index}."""

    source = "toolkit/toolkit/global/mozPageHeader.ftl"
    target = source

    ctx.add_transforms(
        target,
        target,
        transforms_from(
            """
back-nav-button-title2 =
    .title = {COPY_PATTERN(from_path, "back-nav-button-title.title")}
    .aria-label = {COPY_PATTERN(from_path, "back-nav-button-title.title")}
""",
            from_path=source,
        ),
    )
```

### Splitting one message into two

Both new messages copy the same source pattern, since they display the same text
in two places. ([Context](https://phabricator.services.mozilla.com/D308962))

```python
# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 2048544 - [devtools] Use moz-page-nav in about:debugging, part {index}."""

    path = "devtools/client/aboutdebugging.ftl"
    ctx.add_transforms(
        path,
        path,
        transforms_from(
            """
about-debugging-sidebar-setup2 = {COPY_PATTERN(from_path, "about-debugging-sidebar-setup.name")}

about-debugging-sidebar-setup-title =
    .title = {COPY_PATTERN(from_path, "about-debugging-sidebar-setup.name")}
""",
            from_path=path,
        ),
    )
```

### Moving messages to a different file

`from_path` is the file the strings come from, and the first two arguments of
{python}`ctx.add_transforms` are the file they're going to. ([Context](https://phabricator.services.mozilla.com/D303607))

```python
# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 2043735 - Centralize container colors and icons, part {index}."""

    source = "browser/browser/preferences/containers.ftl"
    target = "toolkit/toolkit/global/contextual-identity.ftl"

    ctx.add_transforms(
        target,
        target,
        transforms_from(
            """
user-context-color-blue =
    .label = {COPY_PATTERN(from_path, "containers-color-blue.label")}
user-context-color-green =
    .label = {COPY_PATTERN(from_path, "containers-color-green.label")}
""",
            from_path=source,
        ),
    )
```

A move that changes nothing else can keep the identifiers, in which case both
sides of each {python}`COPY_PATTERN` are identical.

### Reusing text from a different message

A brand new identifier can still be migrated if its text already exists
somewhere else, which needs its own {python}`ctx.add_transforms` call when the
source is in another file. ([Context](https://phabricator.services.mozilla.com/D301718))

```python
# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 2039925 - Move New Tab preference strings into newtab.ftl, part {index}."""

    source = "browser/browser/preferences/preferences.ftl"
    newtab_target = "browser/browser/newtab/newtab.ftl"

    ctx.add_transforms(
        newtab_target,
        newtab_target,
        transforms_from(
            """
home-prefs-content-header =
    .label = {COPY_PATTERN(from_path, "home-prefs-content-header.label")}
""",
            from_path=source,
        ),
    )

    # home-prefs-firefox-logo-header reuses the translation of the profile
    # window's "{ -brand-short-name } logo" alt text, which lives in a
    # different source file.
    profiles_source = "browser/browser/profiles.ftl"
    ctx.add_transforms(
        newtab_target,
        newtab_target,
        transforms_from(
            """
home-prefs-firefox-logo-header =
    .label = {COPY_PATTERN(from_path, "profile-window-logo.alt")}
""",
            from_path=profiles_source,
        ),
    )
```

:::{warning}
The migration test only proves that the **English** text matches; it can't tell
you that a translation written for one message reads correctly in another
context. Call out any cross-message reuse in the patch so the Fluent reviewer
can confirm it.
:::

### Creating a term from an existing message

Terms are migrated like messages, with the leading `-` as part of the
identifier. ([Context](https://phabricator.services.mozilla.com/D280386))

```python
# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

from fluent.migrate.helpers import transforms_from


def migrate(ctx):
    """Bug 2010181 - Migrate value for MDN brand name, part {index}."""

    source = "browser/browser/preferences/moreFromMozilla.ftl"
    target = "toolkit/toolkit/branding/brandings.ftl"

    ctx.add_transforms(
        target,
        target,
        transforms_from(
            """
-mdn-brand-name = { COPY_PATTERN(from_path, "more-from-moz-mdn-title")}
""",
            from_path=source,
        ),
    )
```

### Trimming characters from a string

A {python}`TransformPattern` applies the same edit to every locale's
translation, which a plain {python}`COPY_PATTERN` can't do. ([Context](https://phabricator.services.mozilla.com/D309387))

```python
# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

import re

import fluent.syntax.ast as FTL
from fluent.migrate.transforms import COPY_PATTERN, TransformPattern


class STRIP_ELLIPSIS(TransformPattern):
    """Strip a trailing ellipsis (U+2026 or '...') from a label."""

    def visit_TextElement(self, node):
        node.value = re.sub(r"\s*(?:…|\.\.\.)\s*$", "", node.value)
        return node


def migrate(ctx):
    """Bug 2041699 - Remove trailing ellipsis from Manage colors button, part {index}."""

    source = "browser/browser/preferences/preferences.ftl"

    ctx.add_transforms(
        source,
        source,
        [
            FTL.Message(
                id=FTL.Identifier("preferences-colors-manage-button2"),
                attributes=[
                    FTL.Attribute(
                        id=FTL.Identifier("label"),
                        value=STRIP_ELLIPSIS(
                            source, "preferences-colors-manage-button.label"
                        ),
                    ),
                    FTL.Attribute(
                        id=FTL.Identifier("accesskey"),
                        value=COPY_PATTERN(
                            source, "preferences-colors-manage-button.accesskey"
                        ),
                    ),
                ],
            ),
        ],
    )
```

### Removing markup or a message reference

Returning {python}`None` from a visitor drops that node, so a wrapper element or
a trailing reference can be taken out of every translation. ([Context](https://phabricator.services.mozilla.com/D308214))

```python
# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

import re

import fluent.syntax.ast as FTL
from fluent.migrate.transforms import TransformPattern


class UNWRAP_LEARN_MORE(TransformPattern):
    """Drop the <span data-l10n-name="link"> wrapper, keeping its inner text."""

    def visit_TextElement(self, node):
        node.value = re.sub(r"</?span[^>]*>", "", node.value)
        return node


class STRIP_LEARN_MORE(TransformPattern):
    """Drop the trailing "{ learn-more }" reference and the whitespace before it."""

    # Strips whitespace at end of string value before { learn-more }
    def visit_TextElement(self, node):
        node.value = node.value.rstrip()
        return node

    # Drops { learn-more } placeable
    def visit_Placeable(self, node):
        if (
            isinstance(node.expression, FTL.MessageReference)
            and node.expression.id.name == "learn-more"
        ):
            return None
        return super().visit_Placeable(node)


def migrate(ctx):
    """Bug 2049610 - [devtools] add mdn icon to links in compat/inactive tooltips, part {index}."""

    path = "devtools/client/tooltips.ftl"
    ctx.add_transforms(
        path,
        path,
        [
            FTL.Message(
                id=FTL.Identifier("devtools-tooltip-learn-more"),
                value=UNWRAP_LEARN_MORE(path, "learn-more"),
            ),
            FTL.Message(
                id=FTL.Identifier("inactive-css-not-grid-or-flex-container-fix-1"),
                value=STRIP_LEARN_MORE(
                    path, "inactive-css-not-grid-or-flex-container-fix"
                ),
            ),
        ],
    )
```

### Rewriting a term reference

Editing the placeable instead of the text keeps the whole translation and only
changes which term it resolves. ([Context](https://phabricator.services.mozilla.com/D319951))

```python
# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

import fluent.syntax.ast as FTL
from fluent.migrate.transforms import COPY_PATTERN, TransformPattern


class SWAP_BRAND_TERM(TransformPattern):
    """Reuse the existing translation, rewriting the brand term reference to
    { -brand-product-name } so the string reads "Firefox" on every channel."""

    def visit_Placeable(self, node):
        if isinstance(
            node.expression, FTL.TermReference
        ) and node.expression.id.name in ("brand-shorter-name", "brand-short-name"):
            node.expression.id = FTL.Identifier("brand-product-name")
        return super().visit_Placeable(node)


def migrate(ctx):
    """Bug 2064925 - Referral entry points say "Share Firefox" on all channels, part {index}."""

    appmenu = "browser/browser/appmenu.ftl"
    ctx.add_transforms(
        appmenu,
        appmenu,
        [
            FTL.Message(
                id=FTL.Identifier("appmenu-referrals2"),
                attributes=[
                    FTL.Attribute(
                        FTL.Identifier("label"),
                        SWAP_BRAND_TERM(appmenu, "appmenu-referrals.label"),
                    ),
                    FTL.Attribute(
                        FTL.Identifier("accesskey"),
                        COPY_PATTERN(appmenu, "appmenu-referrals.accesskey"),
                    ),
                ],
            ),
        ],
    )
```

### Generating an accesskey from the translated label

When a message is migrated and gains an accesskey it didn't have before, rather
than losing translations (since a new attribute would normally require
retranslation), an accesskey is generated from the translation. Note, this is
an exception to best practice. A custom {python}`FluentSource` picks a letter
out of each locale's translated label instead. Locales listed in
`EN_ACCESSKEY_LOCALES` keep the English accesskey, which is read from the
reference file. ([Context](https://phabricator.services.mozilla.com/D333771))

```python
# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

import fluent.syntax.ast as FTL
from fluent.migrate.transforms import COPY_PATTERN, FluentSource, Transform

# Locales that reuse the English accesskeys instead of picking one from the
# translated label.
EN_ACCESSKEY_LOCALES = {
    "as",
    "bn",
    "bn-BD",
    "bn-IN",
    "bo",
    "bqi",
    "brx",
    "en-CA",
    "en-GB",
    "en-US",
    "en-ZA",
    "gn",
    "gu-IN",
    "hi-IN",
    "ja",
    "ja-JP-mac",
    "km",
    "kn",
    "ko",
    "ks",
    "lb",
    "lo",
    "mai",
    "mr",
    "my",
    "ne-NP",
    "or",
    "pa-IN",
    "sat",
    "sco",
    "si",
    "skr",
    "ta",
    "ta-LK",
    "te",
    "tl",
    "trs",
    "ur",
    "vi",
    "zh-CN",
    "zh-TW",
}
DESCENDERS = "gjpqy"
NARROW = "ilI"


class ACCESSKEY_FROM_LABEL(FluentSource):
    """Pick an accesskey from the text of a Fluent label, preferring
    characters that are neither narrow nor have descenders.

    The English accesskey is read from `reference_key` in the reference FTL
    file at `reference_path`.
    """

    def __init__(self, path, key, reference_path, reference_key):
        super().__init__(path, key)
        self.reference_path = reference_path
        self.reference_key = reference_key

    def english_accesskey(self, ctx):
        msg_key, _, attr_key = self.reference_key.partition(".")
        for entry in ctx.reference_resources[self.reference_path].body:
            if isinstance(entry, FTL.Message) and entry.id.name == msg_key:
                for attribute in entry.attributes:
                    if attribute.id.name == attr_key:
                        return attribute.value.clone()
        raise KeyError(f"{self.reference_key} not found in {self.reference_path}")

    def __call__(self, ctx):
        if ctx.locale in EN_ACCESSKEY_LOCALES:
            return self.english_accesskey(ctx)
        pattern = super().__call__(ctx)
        label = "".join(
            element.value
            for element in pattern.elements
            if isinstance(element, FTL.TextElement)
        )
        candidates = [c for c in label if c.isalpha()]
        for allowed in (
            lambda c: c not in NARROW and c not in DESCENDERS,
            lambda c: c not in DESCENDERS,
            lambda c: True,
        ):
            for c in candidates:
                if allowed(c):
                    return Transform.pattern_of(FTL.TextElement(c))
        return self.english_accesskey(ctx)


def migrate(ctx):
    """Bug 2079606 - Give the highlight items and the PDF viewer's comment and highlight items in the content context menu accesskeys, part {index}."""

    path = "toolkit/toolkit/global/textActions.ftl"

    ctx.add_transforms(
        path,
        path,
        [
            FTL.Message(
                id=FTL.Identifier("text-action-highlight-selection-2"),
                attributes=[
                    FTL.Attribute(
                        id=FTL.Identifier("label"),
                        value=COPY_PATTERN(
                            path, "text-action-highlight-selection.label"
                        ),
                    ),
                    FTL.Attribute(
                        id=FTL.Identifier("accesskey"),
                        value=ACCESSKEY_FROM_LABEL(
                            path,
                            "text-action-highlight-selection.label",
                            path,
                            "text-action-highlight-selection-2.accesskey",
                        ),
                    ),
                ],
            ),
        ],
    )
```
