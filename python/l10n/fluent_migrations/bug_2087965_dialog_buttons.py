# Any copyright is dedicated to the Public Domain.
# http://creativecommons.org/publicdomain/zero/1.0/

import re

import fluent.syntax.ast as FTL
from fluent.migrate.helpers import transforms_from
from fluent.migrate.transforms import COPY, LegacySource, Transform

PARENTHESES_ACCESSKEY_RE = re.compile(r"\s*[(（]&(.)[)）]")
ACCESSKEY_RE = re.compile(r"(?<!&)&(?!&)(.?)")

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


def pick_accesskey(label, avoid=()):
    candidates = [c for c in label if c.isalpha() and c.lower() not in avoid]
    for allowed in (
        lambda c: c not in NARROW and c not in DESCENDERS,
        lambda c: c not in DESCENDERS,
        lambda c: True,
    ):
        for c in candidates:
            if allowed(c):
                return c
    return None


def split_accesskey(text):
    """Split a label with an embedded accesskey into (label, accesskey).

    A marker followed by a space or by nothing is dropped without an
    accesskey, and "&&" is unescaped to "&".
    """
    label, key = text, None
    match = PARENTHESES_ACCESSKEY_RE.search(text)
    if match:
        label, key = text[: match.start()] + text[match.end() :], match.group(1)
    elif match := ACCESSKEY_RE.search(text):
        label = text[: match.start()] + text[match.start() + 1 :]
        key = match.group(1)
        if not key or key.isspace():
            label, key = label.strip(), None
    return label.replace("&&", "&"), key


class LABEL(COPY):
    """Copy a label, removing the accesskey marker."""

    def get_text(self, ctx):
        return split_accesskey(super().get_text(ctx))[0]


def english_attributes(ctx, target, message_id):
    message = next(
        entry
        for entry in ctx.reference_resources[target].body
        if isinstance(entry, FTL.Message) and entry.id.name == message_id
    )
    return {a.id.name: a.value.elements[0].value for a in message.attributes}


def resolve_accesskey(ctx, label, key, target, message_id, avoid=()):
    """Return `key` if set. Otherwise pick an accesskey from the translated
    label, or use the en-US one for EN_ACCESSKEY_LOCALES. If the en-US
    accesskey is in `avoid`, pick another one from the en-US label.
    """
    if key:
        return key
    if ctx.locale not in EN_ACCESSKEY_LOCALES:
        key = pick_accesskey(label, avoid)
        if key:
            return key
    english = english_attributes(ctx, target, message_id)
    if english["accesskey"].lower() not in avoid:
        return english["accesskey"]
    return pick_accesskey(english["label"], avoid) or english["accesskey"]


def group_accesskey(ctx, path, target, group, index):
    earlier, later = set(), set()
    for i, (key, _) in enumerate(group):
        text = ctx.get_legacy_source(path, key)
        if i == index or text is None:
            continue
        if i < index:
            earlier.add(group_accesskey(ctx, path, target, group, i).lower())
        elif explicit := split_accesskey(text)[1]:
            later.add(explicit.lower())
    key, message_id = group[index]
    label, explicit = split_accesskey(ctx.get_legacy_source(path, key))
    if explicit and explicit.lower() in earlier:
        explicit = None
    return resolve_accesskey(ctx, label, explicit, target, message_id, earlier | later)


class ACCESSKEY(LegacySource):
    """Extract the accesskey from a label. If the label has none, pick one
    from the label, or use the en-US one for EN_ACCESSKEY_LOCALES.

    `group` lists the (key, message_id) pairs of buttons shown together in
    a dialog, in priority order. An explicit accesskey already used by an
    earlier button in the group is replaced. A picked accesskey avoids the
    explicit accesskeys of all other buttons in the group and the final
    accesskeys of the buttons before it.
    """

    def __init__(self, path, key, target, group, index):
        super().__init__(path, key)
        self.target = target
        self.group = group
        self.index = index

    def __call__(self, ctx):
        key = group_accesskey(ctx, self.path, self.target, self.group, self.index)
        return Transform.pattern_of(FTL.TextElement(key))


class SEPARATE_ACCESSKEY(LegacySource):
    """Copy an accesskey stored in its own string. If it's empty, or missing
    from the label stored in `label_key`, resolve it like ACCESSKEY does.
    EN_ACCESSKEY_LOCALES keep a non-empty accesskey even if it's missing
    from the label.
    """

    def __init__(self, path, key, label_key, target, message_id):
        super().__init__(path, key)
        self.label_key = label_key
        self.target = target
        self.message_id = message_id

    def __call__(self, ctx):
        label = ctx.get_legacy_source(self.path, self.label_key) or ""
        key = (self.get_text(ctx) or "").strip()
        if key and (ctx.locale in EN_ACCESSKEY_LOCALES or key.lower() in label.lower()):
            return Transform.pattern_of(FTL.TextElement(key))
        key = resolve_accesskey(ctx, label, None, self.target, self.message_id)
        return Transform.pattern_of(FTL.TextElement(key))


def migrate(ctx):
    """Bug 2087965 - Migrate <dialog> and nsIPrompt button labels to Fluent, part {index}."""

    target = "toolkit/toolkit/global/dialog.ftl"

    source = "toolkit/chrome/global/dialog.properties"
    ctx.add_transforms(
        target,
        target,
        transforms_from(
            """
dialog-button-accept =
    .label = {COPY(from_path, "button-accept")}
dialog-button-cancel =
    .label = {COPY(from_path, "button-cancel")}
""",
            from_path=source,
        )
        + [
            FTL.Message(
                id=FTL.Identifier("dialog-button-disclosure"),
                attributes=[
                    FTL.Attribute(
                        FTL.Identifier("label"), COPY(source, "button-disclosure")
                    ),
                    FTL.Attribute(
                        FTL.Identifier("accesskey"),
                        SEPARATE_ACCESSKEY(
                            source,
                            "accesskey-disclosure",
                            "button-disclosure",
                            target,
                            "dialog-button-disclosure",
                        ),
                    ),
                ],
            ),
        ],
    )

    ctx.add_transforms(
        "toolkit/toolkit/global/unknownContentType.ftl",
        "toolkit/toolkit/global/unknownContentType.ftl",
        transforms_from(
            """
unknowncontenttype-accept-save-file =
    .label = {COPY(from_path, "unknownAccept.label")}
""",
            from_path="toolkit/chrome/mozapps/downloads/unknownContentType.properties",
        ),
    )

    source = "toolkit/chrome/global/commonDialogs.properties"

    def label_messages(*group):
        return [
            FTL.Message(
                id=FTL.Identifier(message_id),
                attributes=[
                    FTL.Attribute(FTL.Identifier("label"), LABEL(source, key)),
                    FTL.Attribute(
                        FTL.Identifier("accesskey"),
                        ACCESSKEY(source, key, target, group, index),
                    ),
                ],
            )
            for index, (key, message_id) in enumerate(group)
        ]

    ctx.add_transforms(
        target,
        target,
        [
            *label_messages(("Yes", "dialog-button-yes"), ("No", "dialog-button-no")),
            *label_messages(
                ("Save", "dialog-button-save"), ("DontSave", "dialog-button-dont-save")
            ),
            *label_messages(("Revert", "dialog-button-revert")),
        ],
    )
