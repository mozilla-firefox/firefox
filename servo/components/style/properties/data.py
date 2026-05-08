# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

import re
import toml
import os
from itertools import groupby
from counted_unknown_properties import COUNTED_UNKNOWN_PROPERTIES

# It is important that the order of these physical / logical variants matches
# the order of the enum variants in logical_geometry.rs
PHYSICAL_SIDES = ["top", "right", "bottom", "left"]
PHYSICAL_CORNERS = ["top-left", "top-right", "bottom-right", "bottom-left"]
PHYSICAL_AXES = ["y", "x"]
PHYSICAL_SIZES = ["height", "width"]
LOGICAL_SIDES = ["block-start", "block-end", "inline-start", "inline-end"]
LOGICAL_CORNERS = ["start-start", "start-end", "end-start", "end-end"]
LOGICAL_SIZES = ["block-size", "inline-size"]
LOGICAL_AXES = ["block", "inline"]

SYSTEM_FONT_LONGHANDS = """font_family font_size font_style
                           font_stretch font_weight""".split()

PRIORITARY_PROPERTIES = set(
    [
        # The writing-mode group has the most priority of all property groups, as
        # sizes like font-size can depend on it.
        "writing-mode",
        "direction",
        "text-orientation",
        # The fonts and colors group has the second priority, as all other lengths
        # and colors depend on them.
        #
        # There are some interdependencies between these, but we fix them up in
        # Cascade::fixup_font_stuff.
        # Needed to properly compute the zoomed font-size.
        "-x-text-scale",
        # Needed to do font-size computation in a language-dependent way.
        "-x-lang",
        # Needed for ruby to respect language-dependent min-font-size
        # preferences properly, see bug 1165538.
        "-moz-min-font-size-ratio",
        # font-size depends on math-depth's computed value.
        "math-depth",
        # Needed to compute the first available font and its used size,
        # in order to compute font-relative units correctly.
        "font-size",
        "font-size-adjust",
        "font-weight",
        "font-stretch",
        "font-style",
        "font-family",
        # color-scheme affects how system colors and light-dark() resolve.
        "color-scheme",
        # forced-color-adjust affects whether colors are adjusted.
        "forced-color-adjust",
        # Zoom affects all absolute lengths.
        "zoom",
        # Line height lengths depend on this.
        "line-height",
        # appearance and -moz-default-appearance control whether
        # @appearance-base rules apply.
        "-moz-default-appearance",
        "appearance",
    ]
)

VISITED_DEPENDENT_PROPERTIES = set(
    [
        "column-rule-color",
        "text-emphasis-color",
        "-webkit-text-fill-color",
        "-webkit-text-stroke-color",
        "text-decoration-color",
        "fill",
        "stroke",
        "caret-color",
        "background-color",
        "border-top-color",
        "border-right-color",
        "border-bottom-color",
        "border-left-color",
        "border-block-start-color",
        "border-inline-end-color",
        "border-block-end-color",
        "border-inline-start-color",
        "outline-color",
        "color",
    ]
)

# Bitfield values for all rule types which can have property declarations.
STYLE_RULE = 1 << 0
PAGE_RULE = 1 << 1
KEYFRAME_RULE = 1 << 2
POSITION_TRY_RULE = 1 << 3
SCOPE_RULE = 1 << 4

ALL_RULES = STYLE_RULE | PAGE_RULE | KEYFRAME_RULE | SCOPE_RULE
DEFAULT_RULES = STYLE_RULE | KEYFRAME_RULE | SCOPE_RULE
DEFAULT_RULES_AND_PAGE = DEFAULT_RULES | PAGE_RULE
DEFAULT_RULES_EXCEPT_KEYFRAME = STYLE_RULE | SCOPE_RULE
DEFAULT_RULES_AND_POSITION_TRY = DEFAULT_RULES | POSITION_TRY_RULE

# Rule name to value dict
RULE_VALUES = {
    "style": STYLE_RULE,
    "page": PAGE_RULE,
    "keyframe": KEYFRAME_RULE,
    "position-try": POSITION_TRY_RULE,
    "scope": SCOPE_RULE,
}


def rule_values_from_arg(rule_types):
    if not rule_types:
        return DEFAULT_RULES
    mask = 0
    for rule in rule_types:
        mask |= RULE_VALUES[rule]
    return mask


def to_rust_ident(name):
    name = name.replace("-", "_")
    if name in ["static", "super", "box", "move"]:  # Rust keywords
        name += "_"
    return name


def idl_method(name, camel_case):
    if name == "float":
        return "CssFloat"
    if name.startswith("-x-"):
        return camel_case[1:]
    return camel_case


def to_snake_case(ident):
    return re.sub("([A-Z]+)", lambda m: "_" + m.group(1).lower(), ident).strip("_")


def to_camel_case(ident):
    return re.sub(
        "(^|_|-)([a-z0-9])", lambda m: m.group(2).upper(), ident.strip("_").strip("-")
    )


def to_camel_case_lower(ident):
    camel = to_camel_case(ident)
    return camel[0].lower() + camel[1:]


# https://drafts.csswg.org/cssom/#css-property-to-idl-attribute
def to_idl_name(name):
    return re.sub("-([a-z])", lambda m: m.group(1).upper(), name)


def parse_aliases(value):
    aliases = {}
    for pair in value:
        [a, v] = pair.split("=")
        aliases[a] = v
    return aliases


class Vector(object):
    def __init__(
        self,
        need_index=False,
        none_value=None,
        separator='Comma',
        animation_type=None,
        simple_bindings=False,
    ):
        self.need_index = need_index
        self.none_value = none_value
        self.separator = separator
        self.animation_type = animation_type
        self.simple_bindings = simple_bindings


class Keyword(object):
    def __init__(
        self,
        name,
        values,
        gecko_constant_prefix=None,
        gecko_enum_prefix=None,
        extra_gecko_values=None,
        extra_servo_values=None,
        gecko_aliases=None,
        servo_aliases=None,
        gecko_inexhaustive=None,
    ):
        self.name = name
        self.values = values;
        assert isinstance(values, list), name
        if gecko_constant_prefix and gecko_enum_prefix:
            raise TypeError(
                "Only one of gecko_constant_prefix and gecko_enum_prefix "
                "can be specified"
            )
        self.gecko_constant_prefix = gecko_constant_prefix
        self.gecko_enum_prefix = gecko_enum_prefix
        if not gecko_constant_prefix and not gecko_enum_prefix:
            self.gecko_enum_prefix = "Style" + to_camel_case(name.replace("-moz-", "").replace("-webkit-", ""))
        self.extra_gecko_values = extra_gecko_values or []
        self.extra_servo_values = extra_servo_values or []
        self.gecko_aliases = parse_aliases(gecko_aliases or [])
        self.servo_aliases = parse_aliases(servo_aliases or [])
        self.gecko_inexhaustive = gecko_inexhaustive or self.gecko_constant_prefix is not None

    def values_for(self, engine):
        if engine == "gecko":
            return self.values + self.extra_gecko_values
        elif engine == "servo":
            return self.values + self.extra_servo_values
        else:
            raise Exception("Bad engine: " + engine)

    def aliases_for(self, engine):
        if engine == "gecko":
            return self.gecko_aliases
        elif engine == "servo":
            return self.servo_aliases
        else:
            raise Exception("Bad engine: " + engine)

    def gecko_constant(self, value):
        moz_stripped = value.replace("-moz-", "")
        if self.gecko_enum_prefix:
            parts = moz_stripped.replace("-", "_").split("_")
            parts = [p.title() for p in parts]
            return self.gecko_enum_prefix + "::" + "".join(parts)
        else:
            suffix = moz_stripped.replace("-", "_")
            return self.gecko_constant_prefix + "_" + suffix.upper()

    def needs_cast(self):
        return self.gecko_enum_prefix is None

    def maybe_cast(self, type_str):
        return "as " + type_str if self.needs_cast() else ""

    def casted_constant_name(self, value, cast_type):
        if cast_type is None:
            raise TypeError("We should specify the cast_type.")

        if self.gecko_enum_prefix is None:
            return cast_type.upper() + "_" + self.gecko_constant(value)
        else:
            return (
                cast_type.upper()
                + "_"
                + self.gecko_constant(value).upper().replace("::", "_")
            )


def parse_property_aliases(alias_list):
    result = []
    if alias_list:
        for alias in alias_list:
            (name, _, pref) = alias.partition(":")
            result.append((name, pref))
    return result


def to_phys(name, logical, physical):
    return name.replace(logical, physical).replace("inset-", "")


class Property(object):
    def __init__(
        self,
        name,
        spec,
        servo_pref,
        gecko_pref,
        enabled_in,
        rule_types_allowed,
        aliases,
        extra_prefixes,
        flags,
    ):
        self.name = name
        if not spec:
            raise TypeError("Spec should be specified for " + name)
        self.spec = spec
        self.ident = to_rust_ident(name)
        self.camel_case = to_camel_case(self.ident)
        self.servo_pref = servo_pref
        self.gecko_pref = gecko_pref
        self.idl_method = idl_method(name, self.camel_case)
        self.rule_types_allowed = rule_values_from_arg(rule_types_allowed)
        # For enabled_in, the setup is as follows:
        # It needs to be one of the four values: ["", "ua", "chrome", "content"]
        #  * "chrome" implies "ua", and implies that they're explicitly
        #    enabled.
        #  * "" implies the property will never be parsed.
        #  * "content" implies the property is accessible unconditionally,
        #    modulo a pref, set via servo_pref / gecko_pref.
        assert enabled_in in ("", "ua", "chrome", "content")
        self.enabled_in = enabled_in
        self.aliases = parse_property_aliases(aliases)
        self.extra_prefixes = parse_property_aliases(extra_prefixes)
        self.flags = flags.split() if flags else []

    def rule_types_allowed_names(self):
        for name in RULE_VALUES:
            if self.rule_types_allowed & RULE_VALUES[name] != 0:
                yield name

    def experimental(self, engine):
        if engine == "gecko":
            return bool(self.gecko_pref)
        elif engine == "servo":
            return bool(self.servo_pref)
        else:
            raise Exception("Bad engine: " + engine)

    def explicitly_enabled_in_ua_sheets(self):
        return self.enabled_in in ("ua", "chrome")

    def explicitly_enabled_in_chrome(self):
        return self.enabled_in == "chrome"

    def enabled_in_content(self):
        return self.enabled_in == "content"

    def is_visited_dependent(self):
        return self.name in VISITED_DEPENDENT_PROPERTIES

    def is_prioritary(self):
        return self.name in PRIORITARY_PROPERTIES

    def noncustomcsspropertyid(self):
        return "NonCustomCSSPropertyId::eCSSProperty_" + self.ident


class Longhand(Property):
    def __init__(
        self,
        style_struct,
        name,
        initial_value=None,
        initial_specified_value=None,
        parse_method='parse',
        spec=None,
        animation_type="normal",
        keyword=None,
        predefined_type=None,
        servo_pref=None,
        gecko_pref=None,
        enabled_in="content",
        gecko_ffi_name=None,
        has_effect_on_gecko_scrollbars=None,
        rule_types_allowed=None,
        cast_type="u8",
        logical=False,
        logical_group=None,
        aliases=None,
        extra_prefixes=None,
        boxed=False,
        flags=None,
        allow_quirks=False,
        ignored_when_colors_disabled=False,
        vector=None,
        servo_restyle_damage="rebuild_box",
        affects=None,
    ):
        Property.__init__(
            self,
            name=name,
            spec=spec,
            servo_pref=servo_pref,
            gecko_pref=gecko_pref,
            enabled_in=enabled_in,
            rule_types_allowed=rule_types_allowed,
            aliases=aliases,
            extra_prefixes=extra_prefixes,
            flags=flags,
        )

        self.affects = affects
        self.flags += self.affects_flags()

        self.parse_method = parse_method
        self.initial_value = initial_value
        self.initial_specified_value = initial_specified_value
        self.keyword = keyword
        self.predefined_type = predefined_type
        self.style_struct = style_struct
        self.has_effect_on_gecko_scrollbars = has_effect_on_gecko_scrollbars
        assert (
            has_effect_on_gecko_scrollbars in [None, False, True]
            and not style_struct.inherited
            or (gecko_pref is None and enabled_in != "")
            == (has_effect_on_gecko_scrollbars is None)
        ), (
            "Property "
            + name
            + ": has_effect_on_gecko_scrollbars must be "
            + "specified, and must have a value of True or False, iff a "
            + "property is inherited and is behind a Gecko pref or internal"
        )
        self.gecko_ffi_name = gecko_ffi_name or "m" + self.camel_case
        self.cast_type = cast_type
        self.logical = logical
        self.logical_group = logical_group
        if self.logical:
            assert logical_group, f"Property {name} must have a logical group"

        self.boxed = boxed
        self.allow_quirks = allow_quirks
        self.ignored_when_colors_disabled = ignored_when_colors_disabled
        self.vector = Vector(**vector) if vector is not None else None

        assert animation_type in ["none", "normal", "discrete"]
        self.animation_type = animation_type
        self.animatable = animation_type != "none"

        # See compute_damage for the various values this can take
        self.servo_restyle_damage = servo_restyle_damage

    def affects_flags(self):
        # Layout is the stronger hint. This property animation affects layout
        # or frame construction. `display` or `width` are examples that should
        # use this.
        if self.affects == "layout":
            return ["AFFECTS_LAYOUT"]
        # This property doesn't affect layout, but affects overflow.
        # `transform` and co. are examples of this.
        if self.affects == "overflow":
            return ["AFFECTS_OVERFLOW"]
        # This property affects the rendered output but doesn't affect layout.
        # `opacity`, `color`, or `z-index` are examples of this.
        if self.affects == "paint":
            return ["AFFECTS_PAINT"]
        # This property doesn't affect rendering in any way.
        # `user-select` is an example of this.
        assert self.affects == "", (
            "Property "
            + self.name
            + ': affects must be specified and be one of ["layout", "overflow", "paint", ""], see Longhand.affects_flags for documentation'
        )
        return []

    @staticmethod
    def type():
        return "longhand"

    # For a given logical property, return the kind of mapping we need to
    # perform, and which logical value we represent, in a tuple.
    def logical_mapping_data(self, data):
        if not self.logical:
            return []
        # Sizes and axes are basically the same for mapping, we just need
        # slightly different replacements (block-size -> height, etc rather
        # than -x/-y) below.
        for [ty, logical_items, physical_items] in [
            ["Side", LOGICAL_SIDES, PHYSICAL_SIDES],
            ["Corner", LOGICAL_CORNERS, PHYSICAL_CORNERS],
            ["Axis", LOGICAL_SIZES, PHYSICAL_SIZES],
            ["Axis", LOGICAL_AXES, PHYSICAL_AXES],
        ]:
            candidate = [s for s in logical_items if s in self.name]
            if candidate:
                assert len(candidate) == 1
                return [ty, candidate[0], logical_items, physical_items]
        assert False, "Don't know how to deal with " + self.name

    def logical_mapping_kind(self, data):
        assert self.logical
        [kind, item, _, _] = self.logical_mapping_data(data)
        return "LogicalMappingKind::{}(Logical{}::{})".format(
            kind, kind, to_camel_case(item.replace("-size", ""))
        )

    # For a given logical property return all the physical property names
    # corresponding to it.
    def all_physical_mapped_properties(self, data):
        if not self.logical:
            return []
        [_, logical_side, _, physical_items] = self.logical_mapping_data(data)
        return [
            data.longhands_by_name[to_phys(self.name, logical_side, physical_side)]
            for physical_side in physical_items
        ]

    def may_be_disabled_in(self, shorthand, engine):
        if engine == "gecko":
            return self.gecko_pref and self.gecko_pref != shorthand.gecko_pref
        elif engine == "servo":
            return self.servo_pref and self.servo_pref != shorthand.servo_pref
        else:
            raise Exception("Bad engine: " + engine)

    def base_type(self):
        if self.predefined_type and not self.vector:
            return "crate::values::specified::{}".format(self.predefined_type)
        return "longhands::{}::SpecifiedValue".format(self.ident)

    def specified_type(self):
        if self.predefined_type and not self.vector:
            ty = "crate::values::specified::{}".format(self.predefined_type)
        else:
            ty = "longhands::{}::SpecifiedValue".format(self.ident)
        if self.boxed:
            ty = "Box<{}>".format(ty)
        return ty

    def is_zoom_dependent(self):
        if not self.predefined_type:
            return False
        # TODO: Get this from SpecifiedValueInfo or so instead; see bug 1887627.
        return self.predefined_type in {
            "BorderSideWidth",
            "BorderSpacing",
            "BoxShadow",
            "Filter",
            "FontSize",
            "Inset",
            "Length",
            "LengthPercentage",
            "LengthPercentageOrAuto",
            "LetterSpacing",
            "LineHeight",
            "LineWidth",
            "MaxSize",
            "Margin",
            "NonNegativeLength",
            "NonNegativeLengthOrAuto",
            "NonNegativeLengthOrNumber",
            "NonNegativeLengthOrNumberRect",
            "NonNegativeLengthPercentage",
            "NonNegativeLengthPercentageOrAuto",
            "NonNegativeLengthPercentageOrNormal",
            "Position",
            "PositionOrAuto",
            "SimpleShadow",
            "Size",
            "SVGLength",
            "SVGStrokeDashArray",
            "SVGWidth",
            "TextDecorationLength",
            "TextDecorationInset",
            "TextIndent",
            "WordSpacing",
        }

    def is_inherited_zoom_dependent_property(self):
        if self.logical:
            return False
        if not self.style_struct.inherited:
            return False
        return self.is_zoom_dependent()

    def specified_is_copy(self):
        if self.vector or self.boxed:
            return False
        if self.predefined_type:
            return self.predefined_type in {
                "AlignmentBaseline",
                "Appearance",
                "AnimationComposition",
                "AnimationDirection",
                "AnimationFillMode",
                "AnimationPlayState",
                "BaselineSource",
                "BreakBetween",
                "BreakWithin",
                "BackgroundRepeat",
                "BorderImageRepeat",
                "BorderStyle",
                "table::CaptionSide",
                "Clear",
                "Contain",
                "ContentVisibility",
                "ContainerType",
                "Display",
                "DominantBaseline",
                "FillRule",
                "Float",
                "FontLanguageOverride",
                "FontStyle",
                "FontSynthesis",
                "FontSynthesisStyle",
                "FontVariantEastAsian",
                "FontVariantLigatures",
                "FontVariantNumeric",
                "GridAutoFlow",
                "ImageRendering",
                "Inert",
                "PositionArea",
                "PositionAreaKeyword",
                "PositionProperty",
                "ContentDistribution",
                "ItemPlacement",
                "SelfAlignment",
                "JustifyItems",
                "LineBreak",
                "MasonryAutoFlow",
                "MozTheme",
                "BoolInteger",
                "text::MozControlCharacterVisibility",
                "MozScriptMinSize",
                "MozScriptSizeMultiplier",
                "TransformBox",
                "TextDecorationSkipInk",
                "OffsetRotate",
                "OutlineStyle",
                "Overflow",
                "OverflowAnchor",
                "OverflowWrap",
                "OverscrollBehavior",
                "PageOrientation",
                "PointerEvents",
                "PositionTryOrder",
                "PositionVisibility",
                "PrintColorAdjust",
                "ForcedColorAdjust",
                "Resize",
                "RubyPosition",
                "SVGPaintOrder",
                "ScrollbarGutter",
                "ScrollSnapAlign",
                "ScrollSnapAxis",
                "ScrollSnapStop",
                "ScrollSnapStrictness",
                "ScrollSnapType",
                "TextAlign",
                "TextAlignLast",
                "TextAutospace",
                "TextBoxEdge",
                "TextBoxTrim",
                "TextDecorationLine",
                "TextEmphasisPosition",
                "TextJustify",
                "TextTransform",
                "TextUnderlinePosition",
                "TouchAction",
                "TransformStyle",
                "UserFocus",
                "UserSelect",
                "VectorEffect",
                "WordBreak",
                "WritingModeProperty",
                "XSpan",
                "XTextScale",
            }
        if self.name == "overflow-y":
            return True
        return bool(self.keyword)

    def animated_type(self):
        assert self.animatable
        computed = "<{} as ToComputedValue>::ComputedValue".format(self.base_type())
        if self.animation_type == "discrete":
            return computed
        return "<{} as ToAnimatedValue>::AnimatedValue".format(computed)


class Shorthand(Property):
    def __init__(
        self,
        name,
        sub_properties,
        spec=None,
        servo_pref=None,
        gecko_pref=None,
        kind=None,
        allow_quirks=False,
        derive_serialize=False,
        derive_value_info=True,
        enabled_in="content",
        rule_types_allowed=None,
        aliases=None,
        extra_prefixes=None,
        flags=None,
    ):
        Property.__init__(
            self,
            name=name,
            spec=spec,
            servo_pref=servo_pref,
            gecko_pref=gecko_pref,
            enabled_in=enabled_in,
            rule_types_allowed=rule_types_allowed,
            aliases=aliases,
            extra_prefixes=extra_prefixes,
            flags=flags,
        )
        self.sub_properties = sub_properties
        self.derive_serialize = derive_serialize
        self.derive_value_info = derive_value_info
        self.allow_quirks = allow_quirks
        self.kind = kind

    def get_animatable(self):
        for sub in self.sub_properties:
            if sub.animatable:
                return True
        return False

    animatable = property(get_animatable)

    @staticmethod
    def type():
        return "shorthand"


class Alias(object):
    def __init__(self, name, original, gecko_pref):
        self.name = name
        self.ident = to_rust_ident(name)
        self.camel_case = to_camel_case(self.ident)
        self.idl_method = idl_method(name, self.camel_case)
        self.original = original
        self.enabled_in = original.enabled_in
        self.animatable = original.animatable
        self.servo_pref = original.servo_pref
        self.gecko_pref = gecko_pref
        self.rule_types_allowed = original.rule_types_allowed
        self.flags = original.flags

    @staticmethod
    def type():
        return "alias"

    def rule_types_allowed_names(self):
        for name in RULE_VALUES:
            if self.rule_types_allowed & RULE_VALUES[name] != 0:
                yield name

    def experimental(self, engine):
        if engine == "gecko":
            return bool(self.gecko_pref)
        elif engine == "servo":
            return bool(self.servo_pref)
        else:
            raise Exception("Bad engine: " + engine)

    def explicitly_enabled_in_ua_sheets(self):
        return self.enabled_in in ["ua", "chrome"]

    def explicitly_enabled_in_chrome(self):
        return self.enabled_in == "chrome"

    def enabled_in_content(self):
        return self.enabled_in == "content"

    def noncustomcsspropertyid(self):
        return "NonCustomCSSPropertyId::eCSSPropertyAlias_%s" % self.ident


class Method(object):
    def __init__(self, name, return_type=None, arg_types=None, is_mut=False):
        self.name = name
        self.return_type = return_type
        self.arg_types = arg_types or []
        self.is_mut = is_mut

    def arg_list(self):
        args = ["_: " + x for x in self.arg_types]
        args = ["&mut self" if self.is_mut else "&self"] + args
        return ", ".join(args)

    def signature(self):
        sig = "fn %s(%s)" % (self.name, self.arg_list())
        if self.return_type:
            sig = sig + " -> " + self.return_type
        return sig

    def declare(self):
        return self.signature() + ";"

    def stub(self):
        return self.signature() + "{ unimplemented!() }"


class StyleStruct(object):
    def __init__(self, name, inherited, gecko_name=None):
        self.gecko_struct_name = "Gecko" + name
        self.name = name
        self.name_lower = to_snake_case(name)
        self.ident = to_rust_ident(self.name_lower)
        self.longhands = []
        self.inherited = inherited
        self.gecko_name = gecko_name or name
        self.gecko_ffi_name = "nsStyle" + self.gecko_name
        self.document_dependent = self.gecko_name in ["Font", "Visibility", "Text"]


class Descriptor(object):
    def __init__(self, name, type, parser=None, gecko_pref=None, ignore_malloc_size_of=None):
        self.name = name
        self.type = type
        self.parser = parser
        self.gecko_pref = gecko_pref
        self.ignore_malloc_size_of = ignore_malloc_size_of
        self.ident = to_rust_ident(name)
        self.camel_case = to_camel_case(name)


class PropertiesData(object):
    def __init__(self, engine):
        self.engine = engine
        self.longhands = []
        self.longhands_by_name = {}
        self.logical_groups = {}
        self.longhand_aliases = []
        self.shorthands = []
        self.shorthands_by_name = {}
        self.shorthand_aliases = []
        self.counted_unknown_properties = [
            CountedUnknownProperty(p) for p in COUNTED_UNKNOWN_PROPERTIES
        ]

        self.style_structs = [
            StyleStruct("Background", inherited=False),
            StyleStruct("Border", inherited=False),
            StyleStruct("Box", inherited=False, gecko_name="Display"),
            StyleStruct("Column", inherited=False),
            StyleStruct("Counters", inherited=False, gecko_name="Content"),
            StyleStruct("Effects", inherited=False),
            StyleStruct("Font", inherited=True),
            StyleStruct("InheritedBox", inherited=True, gecko_name="Visibility"),
            StyleStruct("InheritedSVG", inherited=True, gecko_name="SVG"),
            StyleStruct("InheritedTable", inherited=True, gecko_name="TableBorder"),
            StyleStruct("InheritedText", inherited=True, gecko_name="Text"),
            StyleStruct("InheritedUI", inherited=True, gecko_name="UI"),
            StyleStruct("List", inherited=True),
            StyleStruct("Margin", inherited=False),
            StyleStruct("Outline", inherited=False),
            StyleStruct("Padding", inherited=False),
            StyleStruct("Page", inherited=False),
            StyleStruct("Position", inherited=False),
            StyleStruct("SVG", inherited=False, gecko_name="SVGReset"),
            StyleStruct("Table", inherited=False),
            StyleStruct("Text", inherited=False, gecko_name="TextReset"),
            StyleStruct("UI", inherited=False, gecko_name="UIReset"),
            StyleStruct("XUL", inherited=False),
        ]

        longhands_toml = toml.loads(open(os.path.join(os.path.dirname(__file__), "longhands.toml")).read())
        for name, args in longhands_toml.items():
            style_struct = self.style_struct_by_name_lower(args["struct"])
            del args['struct']

            # Handle keyword properties
            if 'keyword' in args:
                keyword_dict = args.pop('keyword')
                if 'values' not in keyword_dict:
                    raise TypeError(f"{name}: keyword should have 'values'")
                values = keyword_dict.pop('values')
                keyword = Keyword(name, values, **keyword_dict)
                self.declare_longhand(style_struct, name, keyword=keyword, **args)
            else:
                # Handle predefined_type properties
                if 'type' not in args:
                    raise TypeError(f"{name} should have a type")
                args['predefined_type'] = args.pop('type')
                if 'initial' not in args and not args.get('vector'):
                    raise TypeError(f"{name} should have an initial value (only vector properties should lack one)")
                args['initial_value'] = args.pop('initial', None)
                self.declare_longhand(style_struct, name, **args)

        for group, props in self.logical_groups.items():
            logical_count = sum(1 for p in props if p.logical)
            if logical_count * 2 != len(props):
                raise RuntimeError(f"Logical group {group} has unbalanced logical / physical properties")

        # After this code, `data.longhands` is sorted in the following order:
        # - first all keyword variants and all variants known to be Copy,
        # - second all the other variants, such as all variants with the same field
        #   have consecutive discriminants.
        # The variable `variants` contain the same entries as `data.longhands` in
        # the same order, but must exist separately to the data source, because
        # we then need to add three additional variants `WideKeywordDeclaration`,
        # `VariableDeclaration` and `CustomDeclaration`.
        self.declaration_variants = []
        for property in self.longhands:
            self.declaration_variants.append({
                "name": property.camel_case,
                "type": property.specified_type(),
                "doc": "`" + property.name + "`",
                "copy": property.specified_is_copy(),
            })
        groups = {}
        keyfunc = lambda x: x["type"]
        sortkeys = {}
        # WARNING: It is *really* important for the variants of `LonghandId`
        # and `PropertyDeclaration` to be defined in the exact same order,
        # with the exception of `CSSWideKeyword`, `WithVariables` and `Custom`,
        # which don't exist in `LonghandId`.
        for ty, group in groupby(sorted(self.declaration_variants, key=keyfunc), keyfunc):
            group = list(group)
            groups[ty] = group
            for v in group:
                if len(group) == 1:
                    sortkeys[v["name"]] = (not v["copy"], 1, v["name"], "")
                else:
                    sortkeys[v["name"]] = (not v["copy"], len(group), ty, v["name"])
        # It is extremely important to sort the `data.longhands` array here so
        # that it is in the same order as `variants`, for `LonghandId` and
        # `PropertyDeclarationId` to coincide.
        self.longhands.sort(key=lambda x: sortkeys[x.camel_case])
        self.declaration_variants.sort(key=lambda x: sortkeys[x["name"]])
        self.declaration_extra_variants = [
            {
                "name": "CSSWideKeyword",
                "type": "WideKeywordDeclaration",
                "doc": "A CSS-wide keyword.",
                "copy": False,
            },
            {
                "name": "WithVariables",
                "type": "VariableDeclaration",
                "doc": "An unparsed declaration.",
                "copy": False,
            },
            {
                "name": "Custom",
                "type": "CustomDeclaration",
                "doc": "A custom property declaration.",
                "copy": False,
            },
        ]
        for v in self.declaration_extra_variants:
            self.declaration_variants.append(v)
            groups[v["type"]] = [v]

        shorthands_toml = toml.loads(open(os.path.join(os.path.dirname(__file__), "shorthands.toml")).read())
        for name, args in shorthands_toml.items():
            self.declare_shorthand(name, **args)
        self.declare_all_shorthand()

        self.font_face_descriptors = self._load_descriptors("font_face_descriptors.toml")
        self.counter_style_descriptors = self._load_descriptors("counter_style_descriptors.toml")
        self.property_descriptors = self._load_descriptors("property_descriptors.toml")
        self.view_transition_descriptors = self._load_descriptors("view_transition_descriptors.toml")


    def declare_all_shorthand(self):
        # We don't define the 'all' shorthand using the regular helpers:shorthand
        # mechanism, since it causes some very large types to be generated.
        #
        # Make sure logical properties appear before its physical
        # counter-parts, in order to prevent bugs like:
        #
        #   https://bugzilla.mozilla.org/show_bug.cgi?id=1410028
        #
        # FIXME(emilio): Adopt the resolution from:
        #
        #   https://github.com/w3c/csswg-drafts/issues/1898
        #
        # when there is one, whatever that is.
        logical_longhands = []
        other_longhands = []
        for p in self.longhands:
            if p.name in ['direction', 'unicode-bidi']:
                continue;
            if not p.enabled_in_content() and not p.experimental(self.engine):
                continue;
            if "style" not in p.rule_types_allowed_names():
                continue;
            if p.logical:
                logical_longhands.append(p)
            else:
                other_longhands.append(p)

        # Cache locality when iterating over the `all` shorthand is important
        # for transition handling, so we sort by style struct.
        # We technically don't care about the logical prop order (because those
        # are not animated themselves), but we sort them the same way for
        # consistency.
        logical_longhands.sort(key=lambda p: p.style_struct.name)
        other_longhands.sort(key=lambda p: p.style_struct.name)

        all_names = list(map(lambda p: p.name, logical_longhands + other_longhands))

        self.all_shorthand_length = len(all_names)
        self.declare_shorthand(
            "all",
            all_names,
            spec="https://drafts.csswg.org/css-cascade-3/#all-shorthand"
        )


    def _load_descriptors(self, filename):
        path = os.path.join(os.path.dirname(__file__), filename)
        data = toml.loads(open(path).read())
        return [Descriptor(name, **args) for name, args in data.items()]

    def style_struct_by_name_lower(self, name):
        for s in self.style_structs:
            if s.name_lower == name:
                return s
        raise TypeError(f"Unexpected struct name {name}")

    def active_style_structs(self):
        return [s for s in self.style_structs if s.longhands]

    def add_prefixed_aliases(self, property):
        for prefix, pref in property.extra_prefixes:
            property.aliases.append(("-%s-%s" % (prefix, property.name), pref))

    def declare_longhand(self, style_struct, name, extra_gecko_aliases=None, engine=None, **kwargs):
        if engine and self.engine != engine:
            return
        if extra_gecko_aliases and self.engine == "gecko":
            kwargs.setdefault('aliases', []).extend(extra_gecko_aliases)
        longhand = Longhand(style_struct, name, **kwargs)
        self.add_prefixed_aliases(longhand)
        longhand.aliases = [Alias(xp[0], longhand, xp[1]) for xp in longhand.aliases]
        self.longhand_aliases += longhand.aliases
        style_struct.longhands.append(longhand)
        self.longhands.append(longhand)
        self.longhands_by_name[name] = longhand
        if longhand.logical_group:
            self.logical_groups.setdefault(
                longhand.logical_group, []
            ).append(longhand)

        return longhand

    def declare_shorthand(self, name, sub_properties, extra_gecko_sub_properties=None, extra_gecko_aliases=None, engine=None, *args, **kwargs):
        if engine and self.engine != engine:
            return
        if self.engine == "gecko":
            if extra_gecko_sub_properties:
                sub_properties.extend(extra_gecko_sub_properties)
            if extra_gecko_aliases:
                kwargs.setdefault('aliases', []).extend(extra_gecko_aliases)
        sub_properties = [self.longhands_by_name[s] for s in sub_properties]
        shorthand = Shorthand(name, sub_properties, *args, **kwargs)
        self.add_prefixed_aliases(shorthand)
        shorthand.aliases = [Alias(xp[0], shorthand, xp[1]) for xp in shorthand.aliases]
        self.shorthand_aliases += shorthand.aliases
        self.shorthands.append(shorthand)
        self.shorthands_by_name[name] = shorthand
        return shorthand

    def shorthands_except_all(self):
        return [s for s in self.shorthands if s.name != "all"]

    def all_aliases(self):
        return self.longhand_aliases + self.shorthand_aliases

    def all_properties_and_aliases(self):
        return self.longhands + self.shorthands + self.longhand_aliases + self.shorthand_aliases


def _add_logical_props(data, props):
    groups = set()
    for prop in props:
        if prop not in data.longhands_by_name:
            assert data.engine == "servo"
            continue
        prop = data.longhands_by_name[prop]
        if prop.logical_group:
            groups.add(prop.logical_group)
    for group in groups:
        for prop in data.logical_groups[group]:
            props.add(prop.name)


# These are probably Gecko bugs and should be supported per spec.
def _remove_common_first_line_and_first_letter_properties(props, engine):
    if engine == "gecko":
        props.remove("tab-size")
        props.remove("hyphens")
        props.remove("line-break")
        props.remove("text-align-last")
        props.remove("text-emphasis-position")
        props.remove("text-emphasis-style")
        props.remove("text-emphasis-color")
        props.remove("text-wrap-style")

    props.remove("overflow-wrap")
    props.remove("text-align")
    props.remove("text-justify")
    props.remove("white-space-collapse")
    props.remove("text-wrap-mode")
    props.remove("word-break")
    props.remove("text-indent")


class PropertyRestrictions:
    @staticmethod
    def logical_group(data, group):
        return [p.name for p in data.logical_groups[group]]

    @staticmethod
    def shorthand(data, shorthand):
        if shorthand not in data.shorthands_by_name:
            return []
        return [p.name for p in data.shorthands_by_name[shorthand].sub_properties]

    @staticmethod
    def spec(data, spec_path):
        return [p.name for p in data.longhands if spec_path in p.spec]

    # https://svgwg.org/svg2-draft/propidx.html
    @staticmethod
    def svg_text_properties():
        props = set(
            [
                "fill",
                "fill-opacity",
                "fill-rule",
                "paint-order",
                "stroke",
                "stroke-dasharray",
                "stroke-dashoffset",
                "stroke-linecap",
                "stroke-linejoin",
                "stroke-miterlimit",
                "stroke-opacity",
                "stroke-width",
                "text-rendering",
                "vector-effect",
            ]
        )
        return props

    @staticmethod
    def webkit_text_properties():
        props = set(
            [
                # Kinda like css-text?
                "-webkit-text-stroke-width",
                "-webkit-text-fill-color",
                "-webkit-text-stroke-color",
            ]
        )
        return props

    # https://drafts.csswg.org/css-pseudo/#first-letter-styling
    @staticmethod
    def first_letter(data):
        props = set(
            [
                "color",
                "opacity",
                "float",
                "initial-letter",
                # Kinda like css-fonts?
                "-moz-osx-font-smoothing",
                "alignment-baseline",
                "baseline-shift",
                "baseline-source",
                "line-height",
                # Kinda like css-backgrounds?
                "background-blend-mode",
            ]
            + PropertyRestrictions.shorthand(data, "padding")
            + PropertyRestrictions.shorthand(data, "margin")
            + PropertyRestrictions.spec(data, "css-fonts")
            + PropertyRestrictions.spec(data, "css-backgrounds")
            + PropertyRestrictions.spec(data, "css-text")
            + PropertyRestrictions.spec(data, "css-shapes")
            + PropertyRestrictions.spec(data, "css-text-decor")
        )
        props = props.union(PropertyRestrictions.svg_text_properties())
        props = props.union(PropertyRestrictions.webkit_text_properties())

        _add_logical_props(data, props)

        _remove_common_first_line_and_first_letter_properties(props, data.engine)
        return props

    # https://drafts.csswg.org/css-pseudo/#first-line-styling
    @staticmethod
    def first_line(data):
        props = set(
            [
                # Per spec.
                "color",
                "opacity",
                # Kinda like css-fonts?
                "-moz-osx-font-smoothing",
                "alignment-baseline",
                "baseline-shift",
                "baseline-source",
                "line-height",
                # Kinda like css-backgrounds?
                "background-blend-mode",
            ]
            + PropertyRestrictions.spec(data, "css-fonts")
            + PropertyRestrictions.spec(data, "css-backgrounds")
            + PropertyRestrictions.spec(data, "css-text")
            + PropertyRestrictions.spec(data, "css-text-decor")
        )
        props = props.union(PropertyRestrictions.svg_text_properties())
        props = props.union(PropertyRestrictions.webkit_text_properties())

        # These are probably Gecko bugs and should be supported per spec.
        for prop in PropertyRestrictions.shorthand(data, "border"):
            props.remove(prop)
        for prop in PropertyRestrictions.shorthand(data, "border-radius"):
            props.remove(prop)
        props.remove("box-shadow")

        _remove_common_first_line_and_first_letter_properties(props, data.engine)
        return props

    # https://drafts.csswg.org/css-pseudo/#placeholder
    #
    # The spec says that placeholder and first-line have the same restrictions,
    # but that's not true in Gecko and we also allow a handful other properties
    # for ::placeholder.
    @staticmethod
    def placeholder(data):
        props = PropertyRestrictions.first_line(data)
        props.add("opacity")
        props.add("text-overflow")
        props.add("text-align")
        props.add("text-justify")
        for p in PropertyRestrictions.shorthand(data, "text-wrap"):
            props.add(p)
        for p in PropertyRestrictions.shorthand(data, "white-space"):
            props.add(p)
        # ::placeholder can't be SVG text
        props -= PropertyRestrictions.svg_text_properties()
        # Historically ::placeholder's line-height was !important in the UA sheet.
        props.remove("line-height")

        return props

    # https://drafts.csswg.org/css-lists-3/#marker-properties
    @staticmethod
    def marker(data):
        return set(
            [
                "color",
                "content",
                "counter-increment",
                "counter-reset",
                "counter-set",
                "cursor",
                "direction",
                "hyphens",
                "line-height",
                "quotes",
                "text-combine-upright",
                "text-emphasis-color",
                "text-emphasis-position",
                "text-emphasis-style",
                "text-orientation",
                "text-shadow",
                "text-transform",
                "unicode-bidi",
                "-moz-osx-font-smoothing",
            ]
            + PropertyRestrictions.shorthand(data, "animation-range")
            + PropertyRestrictions.shorthand(data, "text-wrap")
            + PropertyRestrictions.shorthand(data, "white-space")
            + PropertyRestrictions.spec(data, "css-fonts")
            + PropertyRestrictions.spec(data, "css-animations")
            + PropertyRestrictions.spec(data, "css-transitions")
        )

    # https://www.w3.org/TR/webvtt1/#the-cue-pseudo-element
    @staticmethod
    def cue(data):
        return set(
            [
                "color",
                "opacity",
                "visibility",
                "text-shadow",
                "text-combine-upright",
                "ruby-position",
                # XXX Should these really apply to cue?
                "-moz-osx-font-smoothing",
                # FIXME(emilio): background-blend-mode should be part of the
                # background shorthand, and get reset, per
                # https://drafts.fxtf.org/compositing/#background-blend-mode
                "background-blend-mode",
            ]
            + PropertyRestrictions.shorthand(data, "text-decoration")
            + PropertyRestrictions.shorthand(data, "text-wrap")
            + PropertyRestrictions.shorthand(data, "white-space")
            + PropertyRestrictions.shorthand(data, "background")
            + PropertyRestrictions.shorthand(data, "outline")
            + PropertyRestrictions.shorthand(data, "font")
            + PropertyRestrictions.shorthand(data, "font-synthesis")
        )


class CountedUnknownProperty:
    def __init__(self, name):
        self.name = name
        self.ident = to_rust_ident(name)
        self.camel_case = to_camel_case(self.ident)
