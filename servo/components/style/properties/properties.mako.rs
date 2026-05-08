/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

// This file is a Mako template: http://www.makotemplates.org/

<%namespace name="helpers" file="/helpers.mako.rs" />
<% from itertools import groupby %>
<% from data import PropertyRestrictions, to_camel_case, RULE_VALUES, SYSTEM_FONT_LONGHANDS, PRIORITARY_PROPERTIES %>

use servo_arc::{Arc, UniqueArc};
use std::{ops, ptr, fmt, mem};

#[cfg(feature = "servo")] use euclid::SideOffsets2D;
#[cfg(feature = "gecko")] use crate::gecko_bindings::structs::{self, NonCustomCSSPropertyId};
#[cfg(feature = "servo")] use crate::logical_geometry::LogicalMargin;
#[cfg(feature = "servo")] use crate::computed_values;
#[cfg(feature = "servo")] use crate::dom::AttributeReferences;
use crate::logical_geometry::WritingMode;
use malloc_size_of::{MallocSizeOf, MallocSizeOfOps};
use crate::computed_value_flags::*;
use cssparser::Parser;
use crate::device::Device;
use crate::parser::ParserContext;
use crate::selector_parser::PseudoElement;
use crate::stylist::Stylist;
use style_traits::{CssStringWriter, CssWriter, KeywordsCollectFn, ParseError, SpecifiedValueInfo, StyleParseErrorKind, ToCss, TypedValueList, ToTyped};
use crate::derives::*;
use crate::stylesheets::{CssRuleType, CssRuleTypes, Origin};
use crate::logical_geometry::{LogicalAxis, LogicalCorner, LogicalSide};
use crate::use_counters::UseCounters;
use crate::rule_tree::StrongRuleNode;
use crate::values::{
    computed,
    resolved,
    specified::{font::SystemFont, length::LineHeightBase, color::ColorSchemeFlags},
};
use std::cell::Cell;
use super::{
    PropertyDeclarationId, PropertyId, NonCustomPropertyId,
    NonCustomPropertyIdSet, PropertyFlags, SourcePropertyDeclaration,
    LonghandIdSet, VariableDeclaration, CustomDeclaration,
    WideKeywordDeclaration, NonCustomPropertyIterator, TransitionPropertyIterator,
};
use debug_unreachable::debug_unreachable;

/// Conversion with fewer impls than From/Into
pub trait MaybeBoxed<Out> {
    /// Convert
    fn maybe_boxed(self) -> Out;
}

impl<T> MaybeBoxed<T> for T {
    #[inline]
    fn maybe_boxed(self) -> T { self }
}

impl<T> MaybeBoxed<Box<T>> for T {
    #[inline]
    fn maybe_boxed(self) -> Box<T> { Box::new(self) }
}

/// A module with all the code for longhand properties.
#[allow(missing_docs)]
pub mod longhands {
<%
    for longhand in data.longhands:
        helpers.longhand(longhand)
%>
}


% if engine == "gecko":
#[allow(unsafe_code, missing_docs)]
pub mod gecko {
    <%include file="/gecko.mako.rs" />
}
% endif


/// A module with code for all the shorthand css properties, and a few
/// serialization helpers.
#[allow(missing_docs)]
pub mod shorthands {
<%
    for shorthand in data.shorthands_except_all():
        helpers.shorthand(shorthand)
%>
}

/// Servo's representation for a property declaration.
#[derive(ToShmem)]
#[repr(u16)]
pub enum PropertyDeclaration {
    % for variant in data.declaration_variants:
    /// ${variant["doc"]}
    ${variant["name"]}(${variant["type"]}),
    % endfor
}

// There's one of these for each parsed declaration so it better be small.
size_of_test!(PropertyDeclaration, 32);

#[repr(C)]
struct PropertyDeclarationVariantRepr<T> {
    tag: u16,
    value: T
}

impl Clone for PropertyDeclaration {
    #[inline]
    fn clone(&self) -> Self {
        use self::PropertyDeclaration::*;

        <%
            [copy, others] = [list(g) for _, g in groupby(data.declaration_variants, key=lambda x: not x["copy"])]
        %>

        let self_tag = unsafe {
            (*(self as *const _ as *const PropertyDeclarationVariantRepr<()>)).tag
        };
        if self_tag <= LonghandId::${copy[-1]["name"]} as u16 {
            #[derive(Clone, Copy)]
            #[repr(u16)]
            enum CopyVariants {
                % for v in copy:
                _${v["name"]}(${v["type"]}),
                % endfor
            }

            unsafe {
                let mut out = mem::MaybeUninit::uninit();
                ptr::write(
                    out.as_mut_ptr() as *mut CopyVariants,
                    *(self as *const _ as *const CopyVariants),
                );
                return out.assume_init();
            }
        }

        // This function ensures that all properties not handled above
        // do not have a specified value implements Copy. If you hit
        // compile error here, you may want to add the type name into
        // Longhand.specified_is_copy in data.py.
        fn _static_assert_others_are_not_copy() {
            struct Helper<T>(T);
            trait AssertCopy { fn assert() {} }
            trait AssertNotCopy { fn assert() {} }
            impl<T: Copy> AssertCopy for Helper<T> {}
            % for ty in sorted(set(x["type"] for x in others)):
            impl AssertNotCopy for Helper<${ty}> {}
            Helper::<${ty}>::assert();
            % endfor
        }

        match *self {
            ${" |\n".join("{}(..)".format(v["name"]) for v in copy)} => {
                unsafe { debug_unreachable!() }
            }
            % for ty, vs in groupby(others, key=lambda x: x["type"]):
            <%
                vs = list(vs)
            %>
            % if len(vs) == 1:
            ${vs[0]["name"]}(ref value) => {
                ${vs[0]["name"]}(value.clone())
            }
            % else:
            ${" |\n".join("{}(ref value)".format(v["name"]) for v in vs)} => {
                unsafe {
                    let mut out = mem::MaybeUninit::uninit();
                    ptr::write(
                        out.as_mut_ptr() as *mut PropertyDeclarationVariantRepr<${ty}>,
                        PropertyDeclarationVariantRepr {
                            tag: *(self as *const _ as *const u16),
                            value: value.clone(),
                        },
                    );
                    out.assume_init()
                }
            }
            % endif
            % endfor
        }
    }
}

impl PartialEq for PropertyDeclaration {
    #[inline]
    fn eq(&self, other: &Self) -> bool {
        use self::PropertyDeclaration::*;

        unsafe {
            let this_repr =
                &*(self as *const _ as *const PropertyDeclarationVariantRepr<()>);
            let other_repr =
                &*(other as *const _ as *const PropertyDeclarationVariantRepr<()>);
            if this_repr.tag != other_repr.tag {
                return false;
            }
            match *self {
                % for ty, vs in groupby(data.declaration_variants, key=lambda x: x["type"]):
                ${" |\n".join("{}(ref this)".format(v["name"]) for v in vs)} => {
                    let other_repr =
                        &*(other as *const _ as *const PropertyDeclarationVariantRepr<${ty}>);
                    *this == other_repr.value
                }
                % endfor
            }
        }
    }
}

impl MallocSizeOf for PropertyDeclaration {
    #[inline]
    fn size_of(&self, ops: &mut MallocSizeOfOps) -> usize {
        use self::PropertyDeclaration::*;

        match *self {
            % for ty, vs in groupby(data.declaration_variants, key=lambda x: x["type"]):
            ${" | ".join("{}(ref value)".format(v["name"]) for v in vs)} => {
                value.size_of(ops)
            }
            % endfor
        }
    }
}


impl PropertyDeclaration {
    /// Returns the given value for this declaration as a particular type.
    /// It's the caller's responsibility to guarantee that the longhand id has the right specified
    /// value representation.
    pub(crate) unsafe fn unchecked_value_as<T>(&self) -> &T {
        &(*(self as *const _ as *const PropertyDeclarationVariantRepr<T>)).value
    }

    /// Dumps the property declaration before crashing.
    #[cold]
    #[cfg(debug_assertions)]
    pub(crate) fn debug_crash(&self, reason: &str) {
        panic!("{}: {:?}", reason, self);
    }
    #[cfg(not(debug_assertions))]
    #[inline(always)]
    pub(crate) fn debug_crash(&self, _reason: &str) {}

    /// Returns whether this is a variant of the Longhand(Value) type, rather
    /// than one of the special variants in extra_variants.
    fn is_longhand_value(&self) -> bool {
        match *self {
            % for v in data.declaration_extra_variants:
            PropertyDeclaration::${v["name"]}(..) => false,
            % endfor
            _ => true,
        }
    }

    /// Like the method on ToCss, but without the type parameter to avoid
    /// accidentally monomorphizing this large function multiple times for
    /// different writers.
    pub fn to_css(&self, dest: &mut CssStringWriter) -> fmt::Result {
        use self::PropertyDeclaration::*;

        let mut dest = CssWriter::new(dest);
        match *self {
            % for ty, vs in groupby(data.declaration_variants, key=lambda x: x["type"]):
            ${" | ".join("{}(ref value)".format(v["name"]) for v in vs)} => {
                value.to_css(&mut dest)
            }
            % endfor
        }
    }

    /// Like the method on ToTyped.
    pub fn to_typed_value_list(&self) -> Option<TypedValueList> {
        use self::PropertyDeclaration::*;

        match *self {
            % for ty, vs in groupby(data.declaration_variants, key=lambda x: x["type"]):
            ${" | ".join("{}(ref value)".format(v["name"]) for v in vs)} => {
                value.to_typed_value_list()
            }
            % endfor
        }
    }

    /// Returns the color value of a given property, for high-contrast-mode tweaks.
    pub(super) fn color_value(&self) -> Option<&crate::values::specified::Color> {
        ${static_longhand_id_set("COLOR_PROPERTIES", lambda p: p.predefined_type == "Color")}
        <%
            # sanity check
            assert data.longhands_by_name["background-color"].predefined_type == "Color"

            color_specified_type = data.longhands_by_name["background-color"].specified_type()
        %>
        let id = self.id().as_longhand()?;
        if !COLOR_PROPERTIES.contains(id) || !self.is_longhand_value() {
            return None;
        }
        let repr = self as *const _ as *const PropertyDeclarationVariantRepr<${color_specified_type}>;
        Some(unsafe { &(*repr).value })
    }
}

/// A module with all the code related to animated properties.
///
/// This needs to be "included" by mako at least after all longhand modules,
/// given they populate the global data.
pub mod animated_properties {
    <%include file="/helpers/animated_properties.mako.rs" />
}

/// A module to group various interesting property counts.
pub mod property_counts {
    /// The number of (non-alias) longhand properties.
    pub const LONGHANDS: usize = ${len(data.longhands)};
    /// The number of (non-alias) shorthand properties.
    pub const SHORTHANDS: usize = ${len(data.shorthands)};
    /// The number of aliases.
    pub const ALIASES: usize = ${len(data.all_aliases())};
    /// The number of counted unknown properties.
    pub const COUNTED_UNKNOWN: usize = ${len(data.counted_unknown_properties)};
    /// The number of (non-alias) longhands and shorthands.
    pub const LONGHANDS_AND_SHORTHANDS: usize = LONGHANDS + SHORTHANDS;
    /// The number of non-custom properties.
    pub const NON_CUSTOM: usize = LONGHANDS_AND_SHORTHANDS + ALIASES;
    /// The number of prioritary properties that we have.
    <% longhand_property_names = set(list(map(lambda p: p.name, data.longhands))) %>
    <% enabled_prioritary_properties = PRIORITARY_PROPERTIES.intersection(longhand_property_names) %>
    pub const PRIORITARY: usize = ${len(enabled_prioritary_properties)};
    /// The max number of longhands that a shorthand other than "all" expands to.
    pub const MAX_SHORTHAND_EXPANDED: usize =
        ${max(len(s.sub_properties) for s in data.shorthands_except_all())};
    /// The max amount of longhands that the `all` shorthand will ever contain.
    pub const ALL_SHORTHAND_EXPANDED: usize = ${data.all_shorthand_length};
    /// The number of animatable properties.
    pub const ANIMATABLE: usize = ${sum(1 for prop in data.longhands if prop.animatable)};
}

% if engine == "gecko":
#[allow(dead_code)]
unsafe fn static_assert_noncustomcsspropertyid() {
    % for i, property in enumerate(data.longhands + data.shorthands + data.all_aliases()):
    std::mem::transmute::<[u8; ${i}], [u8; ${property.noncustomcsspropertyid()} as usize]>([0; ${i}]); // ${property.name}
    % endfor
}
% endif

impl NonCustomPropertyId {
    /// Get the property name.
    #[inline]
    pub fn name(self) -> &'static str {
        static MAP: [&'static str; property_counts::NON_CUSTOM] = [
            % for property in data.longhands + data.shorthands + data.all_aliases():
            "${property.name}",
            % endfor
        ];
        MAP[self.0 as usize]
    }

    /// Returns whether this property is animatable.
    #[inline]
    pub fn is_animatable(self) -> bool {
        ${static_non_custom_property_id_set("ANIMATABLE", lambda p: p.animatable)}
        ANIMATABLE.contains(self)
    }

    /// Whether this property is enabled for all content right now.
    #[inline]
    pub(super) fn enabled_for_all_content(self) -> bool {
        ${static_non_custom_property_id_set(
            "EXPERIMENTAL",
            lambda p: p.experimental(engine)
        )}

        ${static_non_custom_property_id_set(
            "ALWAYS_ENABLED",
            lambda p: (not p.experimental(engine)) and p.enabled_in_content()
        )}

        let passes_pref_check = || {
            % if engine == "gecko":
                unsafe { structs::nsCSSProps_gPropertyEnabled[self.0 as usize] }
            % else:
                static PREF_NAME: [Option<&str>; ${
                    len(data.longhands) + len(data.shorthands) + len(data.all_aliases())
                }] = [
                    % for property in data.longhands + data.shorthands + data.all_aliases():
                        <%
                            pref = getattr(property, "servo_pref")
                        %>
                        % if pref:
                            {
                                const_assert!(!static_prefs::default_value!("${pref}"));
                                Some("${pref}")
                            },
                        % else:
                            None,
                        % endif
                    % endfor
                ];
                let pref = match PREF_NAME[self.0 as usize] {
                    None => return true,
                    Some(pref) => pref,
                };

                // The assertions above guarantee that the pref defaults to false.
                static_prefs::Preference::get(pref, false)
            % endif
        };

        if ALWAYS_ENABLED.contains(self) {
            return true
        }

        if EXPERIMENTAL.contains(self) && passes_pref_check() {
            return true
        }

        false
    }

    /// Returns whether a given rule allows a given property.
    #[inline]
    pub fn allowed_in_rule(self, rule_types: CssRuleTypes) -> bool {
        debug_assert!(
            rule_types.contains(CssRuleType::Keyframe) ||
            rule_types.contains(CssRuleType::Page) ||
            rule_types.contains(CssRuleType::Style) ||
            rule_types.contains(CssRuleType::Scope) ||
            rule_types.contains(CssRuleType::PositionTry),
            "Given rule type does not allow declarations."
        );

        static MAP: [u32; property_counts::NON_CUSTOM] = [
            % for property in data.longhands + data.shorthands + data.all_aliases():
            % for name in RULE_VALUES:
            % if property.rule_types_allowed & RULE_VALUES[name] != 0:
            CssRuleType::${to_camel_case(name)}.bit() |
            % endif
            % endfor
            0,
            % endfor
        ];
        MAP[self.0 as usize] & rule_types.bits() != 0
    }

    pub(super) fn allowed_in(self, context: &ParserContext) -> bool {
        if !self.allowed_in_rule(context.rule_types()) {
            return false;
        }

        self.allowed_in_ignoring_rule_type(context)
    }


    pub(super) fn allowed_in_ignoring_rule_type(self, context: &ParserContext) -> bool {
        // The semantics of these are kinda hard to reason about, what follows
        // is a description of the different combinations that can happen with
        // these three sets.
        //
        // Experimental properties are generally controlled by prefs, but an
        // experimental property explicitly enabled in certain context (UA or
        // chrome sheets) is always usable in the context regardless of the
        // pref value.
        //
        // Non-experimental properties are either normal properties which are
        // usable everywhere, or internal-only properties which are only usable
        // in certain context they are explicitly enabled in.
        if self.enabled_for_all_content() {
            return true;
        }

        ${static_non_custom_property_id_set(
            "ENABLED_IN_UA_SHEETS",
            lambda p: p.explicitly_enabled_in_ua_sheets()
        )}
        ${static_non_custom_property_id_set(
            "ENABLED_IN_CHROME",
            lambda p: p.explicitly_enabled_in_chrome()
        )}

        if context.stylesheet_origin == Origin::UserAgent &&
            ENABLED_IN_UA_SHEETS.contains(self)
        {
            return true
        }

        if context.chrome_rules_enabled() && ENABLED_IN_CHROME.contains(self) {
            return true
        }

        false
    }

    /// The supported types of this property. The return value should be
    /// style_traits::CssType when it can become a bitflags type.
    pub(super) fn supported_types(&self) -> u8 {
        const SUPPORTED_TYPES: [u8; ${len(data.longhands) + len(data.shorthands)}] = [
            % for prop in data.longhands:
                <${prop.specified_type()} as SpecifiedValueInfo>::SUPPORTED_TYPES,
            % endfor
            % for prop in data.shorthands:
            % if prop.name == "all":
                0, // 'all' accepts no value other than CSS-wide keywords
            % else:
                <shorthands::${prop.ident}::Longhands as SpecifiedValueInfo>::SUPPORTED_TYPES,
            % endif
            % endfor
        ];
        SUPPORTED_TYPES[self.0 as usize]
    }

    /// See PropertyId::collect_property_completion_keywords.
    pub(super) fn collect_property_completion_keywords(&self, f: KeywordsCollectFn) {
        fn do_nothing(_: KeywordsCollectFn) {}
        const COLLECT_FUNCTIONS: [fn(KeywordsCollectFn);
                                  ${len(data.longhands) + len(data.shorthands)}] = [
            % for prop in data.longhands:
                <${prop.specified_type()} as SpecifiedValueInfo>::collect_completion_keywords,
            % endfor
            % for prop in data.shorthands:
            % if prop.name == "all":
                do_nothing, // 'all' accepts no value other than CSS-wide keywords
            % else:
                <shorthands::${prop.ident}::Longhands as SpecifiedValueInfo>::
                    collect_completion_keywords,
            % endif
            % endfor
        ];
        COLLECT_FUNCTIONS[self.0 as usize](f);
    }
}

<%def name="static_non_custom_property_id_set(name, is_member)">
static ${name}: NonCustomPropertyIdSet = NonCustomPropertyIdSet {
    <%
        storage = [0] * int((len(data.longhands) + len(data.shorthands) + len(data.all_aliases()) - 1 + 32) / 32)
        for i, property in enumerate(data.longhands + data.shorthands + data.all_aliases()):
            if is_member(property):
                storage[int(i / 32)] |= 1 << (i % 32)
    %>
    storage: [${", ".join("0x%x" % word for word in storage)}]
};
</%def>

<%def name="static_longhand_id_set(name, is_member)">
static ${name}: LonghandIdSet = LonghandIdSet {
    <%
        storage = [0] * int((len(data.longhands) - 1 + 32) / 32)
        for i, property in enumerate(data.longhands):
            if is_member(property):
                storage[int(i / 32)] |= 1 << (i % 32)
    %>
    storage: [${", ".join("0x%x" % word for word in storage)}]
};
</%def>

<%
    FIRST_LINE_RESTRICTIONS = PropertyRestrictions.first_line(data)
    FIRST_LETTER_RESTRICTIONS = PropertyRestrictions.first_letter(data)
    MARKER_RESTRICTIONS = PropertyRestrictions.marker(data)
    PLACEHOLDER_RESTRICTIONS = PropertyRestrictions.placeholder(data)
    CUE_RESTRICTIONS = PropertyRestrictions.cue(data)

    def restriction_flags(property):
        name = property.name
        flags = []
        if name in FIRST_LINE_RESTRICTIONS:
            flags.append("APPLIES_TO_FIRST_LINE")
        if name in FIRST_LETTER_RESTRICTIONS:
            flags.append("APPLIES_TO_FIRST_LETTER")
        if name in PLACEHOLDER_RESTRICTIONS:
            flags.append("APPLIES_TO_PLACEHOLDER")
        if name in MARKER_RESTRICTIONS:
            flags.append("APPLIES_TO_MARKER")
        if name in CUE_RESTRICTIONS:
            flags.append("APPLIES_TO_CUE")
        return flags

%>

/// A group for properties which may override each other via logical resolution.
#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
#[repr(u8)]
pub enum LogicalGroupId {
    % for i, group in enumerate(data.logical_groups.keys()):
    /// ${group}
    ${to_camel_case(group)} = ${i},
    % endfor
}

impl LogicalGroupId {
    /// Return the list of physical mapped properties for a given logical group.
    fn physical_properties(self) -> &'static [LonghandId] {
        static PROPS: [[LonghandId; 4]; ${len(data.logical_groups)}] = [
        % for group, props in data.logical_groups.items():
        [
            <% physical_props = [p for p in props if p.logical][0].all_physical_mapped_properties(data) %>
            % for phys in physical_props:
            LonghandId::${phys.camel_case},
            % endfor
            % for i in range(len(physical_props), 4):
            LonghandId::${physical_props[0].camel_case},
            % endfor
        ],
        % endfor
        ];
        &PROPS[self as usize]
    }
}

/// A set of logical groups.
#[derive(Clone, Copy, Debug, Default, MallocSizeOf, PartialEq)]
pub struct LogicalGroupSet {
    storage: [u32; (${len(data.logical_groups)} - 1 + 32) / 32]
}

impl LogicalGroupSet {
    /// Creates an empty `NonCustomPropertyIdSet`.
    pub fn new() -> Self {
        Self {
            storage: Default::default(),
        }
    }

    /// Return whether the given group is in the set
    #[inline]
    pub fn contains(&self, g: LogicalGroupId) -> bool {
        let bit = g as usize;
        (self.storage[bit / 32] & (1 << (bit % 32))) != 0
    }

    /// Insert a group the set.
    #[inline]
    pub fn insert(&mut self, g: LogicalGroupId) {
        let bit = g as usize;
        self.storage[bit / 32] |= 1 << (bit % 32);
    }
}


#[repr(u8)]
#[derive(Copy, Clone, Debug)]
pub(crate) enum PrioritaryPropertyId {
    % for p in data.longhands:
    % if p.is_prioritary():
    ${p.camel_case},
    % endif
    % endfor
}

impl PrioritaryPropertyId {
    #[inline]
    pub fn to_longhand(self) -> LonghandId {
        static PRIORITARY_TO_LONGHAND: [LonghandId; property_counts::PRIORITARY] = [
        % for p in data.longhands:
        % if p.is_prioritary():
            LonghandId::${p.camel_case},
        % endif
        % endfor
        ];
        PRIORITARY_TO_LONGHAND[self as usize]
    }
    #[inline]
    pub fn from_longhand(l: LonghandId) -> Option<Self> {
        static LONGHAND_TO_PRIORITARY: [Option<PrioritaryPropertyId>; ${len(data.longhands)}] = [
        % for p in data.longhands:
        % if p.is_prioritary():
            Some(PrioritaryPropertyId::${p.camel_case}),
        % else:
            None,
        % endif
        % endfor
        ];
        LONGHAND_TO_PRIORITARY[l as usize]
    }
}

impl LonghandIdSet {
    /// The set of non-inherited longhands.
    #[inline]
    pub(super) fn reset() -> &'static Self {
        ${static_longhand_id_set("RESET", lambda p: not p.style_struct.inherited)}
        &RESET
    }

    #[inline]
    pub(super) fn discrete_animatable() -> &'static Self {
        ${static_longhand_id_set("DISCRETE_ANIMATABLE", lambda p: p.animation_type == "discrete")}
        &DISCRETE_ANIMATABLE
    }

    #[inline]
    pub(super) fn logical() -> &'static Self {
        ${static_longhand_id_set("LOGICAL", lambda p: p.logical)}
        &LOGICAL
    }

    /// Returns the set of longhands that are ignored when document colors are
    /// disabled.
    #[inline]
    pub(super) fn ignored_when_colors_disabled() -> &'static Self {
        ${static_longhand_id_set(
            "IGNORED_WHEN_COLORS_DISABLED",
            lambda p: p.ignored_when_colors_disabled
        )}
        &IGNORED_WHEN_COLORS_DISABLED
    }

    /// Only a few properties are allowed to depend on the visited state of
    /// links. When cascading visited styles, we can save time by only
    /// processing these properties.
    pub(super) fn visited_dependent() -> &'static Self {
        ${static_longhand_id_set("VISITED_DEPENDENT", lambda p: p.is_visited_dependent())}
        debug_assert!(Self::late_group().contains_all(&VISITED_DEPENDENT));
        &VISITED_DEPENDENT
    }

    #[inline]
    pub(super) fn prioritary_properties() -> &'static Self {
        ${static_longhand_id_set("PRIORITARY_PROPERTIES", lambda p: p.is_prioritary())}
        &PRIORITARY_PROPERTIES
    }

    #[inline]
    pub(super) fn late_group_only_inherited() -> &'static Self {
        ${static_longhand_id_set("LATE_GROUP_ONLY_INHERITED", lambda p: p.style_struct.inherited and not p.is_prioritary())}
        &LATE_GROUP_ONLY_INHERITED
    }

    #[inline]
    pub(super) fn late_group() -> &'static Self {
        ${static_longhand_id_set("LATE_GROUP", lambda p: not p.is_prioritary())}
        &LATE_GROUP
    }

    /// Returns the set of properties that are declared as having no effect on
    /// Gecko <scrollbar> elements or their descendant scrollbar parts.
    #[cfg(debug_assertions)]
    #[cfg(feature = "gecko")]
    #[inline]
    pub fn has_no_effect_on_gecko_scrollbars() -> &'static Self {
        // data.py asserts that has_no_effect_on_gecko_scrollbars is True or
        // False for properties that are inherited and Gecko pref controlled,
        // and is None for all other properties.
        ${static_longhand_id_set(
            "HAS_NO_EFFECT_ON_SCROLLBARS",
            lambda p: p.has_effect_on_gecko_scrollbars is False
        )}
        &HAS_NO_EFFECT_ON_SCROLLBARS
    }

    /// Returns the set of margin properties, for the purposes of <h1> use counters / warnings.
    #[inline]
    pub fn margin_properties() -> &'static Self {
        ${static_longhand_id_set(
            "MARGIN_PROPERTIES",
            lambda p: p.logical_group == "margin"
        )}
        &MARGIN_PROPERTIES
    }

    /// Returns the set of border properties for the purpose of disabling native
    /// appearance.
    #[inline]
    pub fn border_background_properties() -> &'static Self {
        ${static_longhand_id_set(
            "BORDER_BACKGROUND_PROPERTIES",
            lambda p: (p.logical_group and p.logical_group.startswith("border")) or \
                        p in data.shorthands_by_name["border"].sub_properties or \
                        p in data.shorthands_by_name["background"].sub_properties and \
                        p.name not in ["background-blend-mode", "background-repeat"]
        )}
        &BORDER_BACKGROUND_PROPERTIES
    }

    /// Returns properties that are zoom dependent (basically, that contain lengths).
    #[inline]
    pub fn zoom_dependent() -> &'static Self {
        ${static_longhand_id_set("ZOOM_DEPENDENT", lambda p: p.is_zoom_dependent())}
        &ZOOM_DEPENDENT
    }

    /// Note that it's different from zoom_dependent(), as this only includes inherited, physical
    /// properties.
    #[inline]
    pub fn zoom_dependent_inherited_properties() -> &'static Self {
        ${static_longhand_id_set("ZOOM_DEPENDENT_INHERITED", lambda p: p.is_inherited_zoom_dependent_property())}
        &ZOOM_DEPENDENT_INHERITED
    }
}

/// An identifier for a given longhand property.
#[derive(Clone, Copy, Eq, Hash, MallocSizeOf, PartialEq, ToComputedValue, ToResolvedValue, ToShmem)]
#[repr(u16)]
pub enum LonghandId {
    % for i, property in enumerate(data.longhands):
        /// ${property.name}
        ${property.camel_case} = ${i},
    % endfor
}

enum LogicalMappingKind {
    Side(LogicalSide),
    Corner(LogicalCorner),
    Axis(LogicalAxis),
}

struct LogicalMappingData {
    group: LogicalGroupId,
    kind: LogicalMappingKind,
}

impl LogicalMappingData {
    fn to_physical(&self, wm: WritingMode) -> LonghandId {
        let index = match self.kind {
            LogicalMappingKind::Side(s) => s.to_physical(wm) as usize,
            LogicalMappingKind::Corner(c) => c.to_physical(wm) as usize,
            LogicalMappingKind::Axis(a) => a.to_physical(wm) as usize,
        };
        self.group.physical_properties()[index]
    }
}

impl LonghandId {
    /// Returns an iterator over all the shorthands that include this longhand.
    pub fn shorthands(self) -> NonCustomPropertyIterator<ShorthandId> {
        // first generate longhand to shorthands lookup map
        //
        // NOTE(emilio): This currently doesn't exclude the "all" shorthand. It
        // could potentially do so, which would speed up serialization
        // algorithms and what not, I guess.
        <%
            from functools import cmp_to_key
            longhand_to_shorthand_map = {}
            num_sub_properties = {}
            for shorthand in data.shorthands:
                num_sub_properties[shorthand.camel_case] = len(shorthand.sub_properties)
                for sub_property in shorthand.sub_properties:
                    if sub_property.ident not in longhand_to_shorthand_map:
                        longhand_to_shorthand_map[sub_property.ident] = []

                    longhand_to_shorthand_map[sub_property.ident].append(shorthand.camel_case)

            def cmp(a, b):
                return (a > b) - (a < b)

            def preferred_order(x, y):
                # Since we want properties in order from most subproperties to least,
                # reverse the arguments to cmp from the expected order.
                result = cmp(num_sub_properties.get(y, 0), num_sub_properties.get(x, 0))
                if result:
                    return result
                # Fall back to lexicographic comparison.
                return cmp(x, y)

            # Sort the lists of shorthand properties according to preferred order:
            # https://drafts.csswg.org/cssom/#concept-shorthands-preferred-order
            for shorthand_list in longhand_to_shorthand_map.values():
                shorthand_list.sort(key=cmp_to_key(preferred_order))
        %>

        // based on lookup results for each longhand, create result arrays
        static MAP: [&'static [ShorthandId]; property_counts::LONGHANDS] = [
        % for property in data.longhands:
            &[
                % for shorthand in longhand_to_shorthand_map.get(property.ident, []):
                    ShorthandId::${shorthand},
                % endfor
            ],
        % endfor
        ];

        NonCustomPropertyIterator {
            filter: NonCustomPropertyId::from(self).enabled_for_all_content(),
            iter: MAP[self as usize].iter(),
        }
    }

    pub(super) fn parse_value<'i, 't>(
        self,
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<PropertyDeclaration, ParseError<'i>> {
        type ParsePropertyFn = for<'i, 't> fn(
            context: &ParserContext,
            input: &mut Parser<'i, 't>,
        ) -> Result<PropertyDeclaration, ParseError<'i>>;
        static PARSE_PROPERTY: [ParsePropertyFn; ${len(data.longhands)}] = [
        % for property in data.longhands:
            longhands::${property.ident}::parse_declared,
        % endfor
        ];
        (PARSE_PROPERTY[self as usize])(context, input)
    }

    /// Return the relevant data to map a particular logical property into physical.
    fn logical_mapping_data(self) -> Option<&'static LogicalMappingData> {
        const LOGICAL_MAPPING_DATA: [Option<LogicalMappingData>; ${len(data.longhands)}] = [
            % for prop in data.longhands:
            % if prop.logical:
            Some(LogicalMappingData {
                group: LogicalGroupId::${to_camel_case(prop.logical_group)},
                kind: ${prop.logical_mapping_kind(data)}
            }),
            % else:
            None,
            % endif
            % endfor
        ];
        LOGICAL_MAPPING_DATA[self as usize].as_ref()
    }

    /// If this is a logical property, return the corresponding physical one in the given
    /// writing mode. Otherwise, return unchanged.
    #[inline]
    pub fn to_physical(self, wm: WritingMode) -> Self {
        let Some(data) = self.logical_mapping_data() else { return self };
        data.to_physical(wm)
    }

    /// Return the logical group of this longhand property.
    pub fn logical_group(self) -> Option<LogicalGroupId> {
        const LOGICAL_GROUP_IDS: [Option<LogicalGroupId>; ${len(data.longhands)}] = [
            % for prop in data.longhands:
            % if prop.logical_group:
            Some(LogicalGroupId::${to_camel_case(prop.logical_group)}),
            % else:
            None,
            % endif
            % endfor
        ];
        LOGICAL_GROUP_IDS[self as usize]
    }

    /// Returns PropertyFlags for given longhand property.
    #[inline(always)]
    pub fn flags(self) -> PropertyFlags {
        const FLAGS: [PropertyFlags; ${len(data.longhands)}] = [
            % for property in data.longhands:
                PropertyFlags::empty()
                % for flag in property.flags + restriction_flags(property):
                    .union(PropertyFlags::${flag})
                % endfor
                ,
            % endfor
        ];
        FLAGS[self as usize]
    }
}

/// An identifier for a given shorthand property.
#[derive(Clone, Copy, Debug, Eq, Hash, MallocSizeOf, PartialEq, ToComputedValue, ToResolvedValue, ToShmem)]
#[repr(u16)]
pub enum ShorthandId {
    % for i, property in enumerate(data.shorthands):
        /// ${property.name}
        ${property.camel_case} = ${i},
    % endfor
}

impl ShorthandId {
    /// Get the longhand ids that form this shorthand.
    pub fn longhands(self) -> NonCustomPropertyIterator<LonghandId> {
        static MAP: [&'static [LonghandId]; property_counts::SHORTHANDS] = [
        % for property in data.shorthands:
            &[
                % for sub in property.sub_properties:
                    LonghandId::${sub.camel_case},
                % endfor
            ],
        % endfor
        ];
        NonCustomPropertyIterator {
            filter: NonCustomPropertyId::from(self).enabled_for_all_content(),
            iter: MAP[self as usize].iter(),
        }
    }

    /// Try to serialize the given declarations as this shorthand.
    ///
    /// Returns an error if writing to the stream fails, or if the declarations
    /// do not map to a shorthand.
    pub fn longhands_to_css(
        self,
        declarations: &[&PropertyDeclaration],
        dest: &mut CssStringWriter,
    ) -> fmt::Result {
        type LonghandsToCssFn = for<'a, 'b> fn(&'a [&'b PropertyDeclaration], &mut CssStringWriter) -> fmt::Result;
        fn all_to_css(_: &[&PropertyDeclaration], _: &mut CssStringWriter) -> fmt::Result {
            // No need to try to serialize the declarations as the 'all'
            // shorthand, since it only accepts CSS-wide keywords (and variable
            // references), which will be handled in
            // get_shorthand_appendable_value.
            Ok(())
        }

        static LONGHANDS_TO_CSS: [LonghandsToCssFn; ${len(data.shorthands)}] = [
            % for shorthand in data.shorthands:
            % if shorthand.ident == "all":
                all_to_css,
            % else:
                shorthands::${shorthand.ident}::to_css,
            % endif
            % endfor
        ];

        LONGHANDS_TO_CSS[self as usize](declarations, dest)
    }

    /// Returns PropertyFlags for the given shorthand property.
    #[inline]
    pub fn flags(self) -> PropertyFlags {
        const FLAGS: [u16; ${len(data.shorthands)}] = [
            % for property in data.shorthands:
                % for flag in property.flags:
                    PropertyFlags::${flag}.bits() |
                % endfor
                0,
            % endfor
        ];
        PropertyFlags::from_bits_retain(FLAGS[self as usize])
    }

    /// Returns the order in which this property appears relative to other
    /// shorthands in idl-name-sorting order.
    #[inline]
    pub fn idl_name_sort_order(self) -> u32 {
        <%
            from data import to_idl_name
            ordered = {}
            sorted_shorthands = sorted(data.shorthands, key=lambda p: to_idl_name(p.ident))
            for order, shorthand in enumerate(sorted_shorthands):
                ordered[shorthand.ident] = order
        %>
        static IDL_NAME_SORT_ORDER: [u32; ${len(data.shorthands)}] = [
            % for property in data.shorthands:
            ${ordered[property.ident]},
            % endfor
        ];
        IDL_NAME_SORT_ORDER[self as usize]
    }

    pub(super) fn parse_into<'i, 't>(
        self,
        declarations: &mut SourcePropertyDeclaration,
        context: &ParserContext,
        input: &mut Parser<'i, 't>,
    ) -> Result<(), ParseError<'i>> {
        type ParseIntoFn = for<'i, 't> fn(
            declarations: &mut SourcePropertyDeclaration,
            context: &ParserContext,
            input: &mut Parser<'i, 't>,
        ) -> Result<(), ParseError<'i>>;

        fn parse_all<'i, 't>(
            _: &mut SourcePropertyDeclaration,
            _: &ParserContext,
            input: &mut Parser<'i, 't>
        ) -> Result<(), ParseError<'i>> {
            // 'all' accepts no value other than CSS-wide keywords
            Err(input.new_custom_error(StyleParseErrorKind::UnspecifiedError))
        }

        static PARSE_INTO: [ParseIntoFn; ${len(data.shorthands)}] = [
            % for shorthand in data.shorthands:
            % if shorthand.ident == "all":
            parse_all,
            % else:
            shorthands::${shorthand.ident}::parse_into,
            % endif
            % endfor
        ];

        (PARSE_INTO[self as usize])(declarations, context, input)
    }
}

/// The counted unknown property list which is used for css use counters.
///
/// FIXME: This should be just #[repr(u8)], but can't be because of ABI issues,
/// see https://bugs.llvm.org/show_bug.cgi?id=44228.
#[derive(Clone, Copy, Debug, Eq, FromPrimitive, Hash, PartialEq)]
#[repr(u32)]
pub enum CountedUnknownProperty {
    % for prop in data.counted_unknown_properties:
    /// ${prop.name}
    ${prop.camel_case},
    % endfor
}

impl CountedUnknownProperty {
    /// Parse the counted unknown property, for testing purposes only.
    pub fn parse_for_testing(property_name: &str) -> Option<Self> {
        ::cssparser::ascii_case_insensitive_phf_map! {
            unknown_ids -> CountedUnknownProperty = {
                % for property in data.counted_unknown_properties:
                "${property.name}" => CountedUnknownProperty::${property.camel_case},
                % endfor
            }
        }
        unknown_ids::get(property_name).cloned()
    }

    /// Returns the underlying index, used for use counter.
    #[inline]
    pub fn bit(self) -> usize {
        self as usize
    }
}

impl PropertyId {
    /// Returns a given property from the given name, _regardless of whether it
    /// is enabled or not_, or Err(()) for unknown properties.
    pub fn parse_unchecked(
        property_name: &str,
        use_counters: Option<&UseCounters>,
    ) -> Result<Self, ()> {
        // A special id for css use counters. ShorthandAlias is not used in the Servo build.
        // That's why we need to allow dead_code.
        pub enum StaticId {
            NonCustom(NonCustomPropertyId),
            CountedUnknown(CountedUnknownProperty),
        }
        ::cssparser::ascii_case_insensitive_phf_map! {
            static_ids -> StaticId = {
                % for i, property in enumerate(data.longhands + data.shorthands + data.all_aliases()):
                "${property.name}" => StaticId::NonCustom(NonCustomPropertyId(${i})),
                % endfor
                % for property in data.counted_unknown_properties:
                "${property.name}" => {
                    StaticId::CountedUnknown(CountedUnknownProperty::${property.camel_case})
                },
                % endfor
            }
        }

        if let Some(id) = static_ids::get(property_name) {
            return Ok(match *id {
                StaticId::NonCustom(id) => PropertyId::NonCustom(id),
                StaticId::CountedUnknown(unknown_prop) => {
                    if let Some(counters) = use_counters {
                        counters.counted_unknown_properties.record(unknown_prop);
                    }
                    // Always return Err(()) because these aren't valid custom property names.
                    return Err(());
                }
            });
        }

        let name = crate::custom_properties::parse_name(property_name)?;
        Ok(PropertyId::Custom(crate::custom_properties::Name::from(name)))
    }
}

impl PropertyDeclaration {
    /// Given a property declaration, return the property declaration id.
    #[inline]
    pub fn id(&self) -> PropertyDeclarationId<'_> {
        match *self {
            PropertyDeclaration::Custom(ref declaration) => {
                return PropertyDeclarationId::Custom(&declaration.name)
            }
            PropertyDeclaration::CSSWideKeyword(ref declaration) => {
                return PropertyDeclarationId::Longhand(declaration.id);
            }
            PropertyDeclaration::WithVariables(ref declaration) => {
                return PropertyDeclarationId::Longhand(declaration.id);
            }
            _ => {}
        }
        // This is just fine because PropertyDeclaration and LonghandId
        // have corresponding discriminants.
        let id = unsafe { *(self as *const _ as *const LonghandId) };
        debug_assert_eq!(id, match *self {
            % for property in data.longhands:
            PropertyDeclaration::${property.camel_case}(..) => LonghandId::${property.camel_case},
            % endfor
            _ => id,
        });
        PropertyDeclarationId::Longhand(id)
    }

    /// Given a declaration, convert it into a declaration for a corresponding
    /// physical property.
    #[inline]
    pub fn to_physical(&self, wm: WritingMode) -> Self {
        match *self {
            PropertyDeclaration::WithVariables(VariableDeclaration {
                id,
                ref value,
            }) => {
                return PropertyDeclaration::WithVariables(VariableDeclaration {
                    id: id.to_physical(wm),
                    value: value.clone(),
                })
            }
            PropertyDeclaration::CSSWideKeyword(WideKeywordDeclaration {
                id,
                keyword,
            }) => {
                return PropertyDeclaration::CSSWideKeyword(WideKeywordDeclaration {
                    id: id.to_physical(wm),
                    keyword,
                })
            }
            PropertyDeclaration::Custom(..) => return self.clone(),
            % for prop in data.longhands:
            PropertyDeclaration::${prop.camel_case}(..) => {},
            % endfor
        }

        let mut ret = self.clone();

        % for prop in data.longhands:
        % for physical_property in prop.all_physical_mapped_properties(data):
        % if physical_property.specified_type() != prop.specified_type():
            <% raise "Logical property %s should share specified value with physical property %s" % \
                     (prop.name, physical_property.name) %>
        % endif
        % endfor
        % endfor

        unsafe {
            let longhand_id = *(&mut ret as *mut _ as *mut LonghandId);

            debug_assert_eq!(
                PropertyDeclarationId::Longhand(longhand_id),
                ret.id()
            );

            // This is just fine because PropertyDeclaration and LonghandId
            // have corresponding discriminants.
            *(&mut ret as *mut _ as *mut LonghandId) = longhand_id.to_physical(wm);

            debug_assert_eq!(
                PropertyDeclarationId::Longhand(longhand_id.to_physical(wm)),
                ret.id()
            );
        }

        ret
    }

    /// Returns whether or not the property is set by a system font
    pub fn get_system(&self) -> Option<SystemFont> {
        match *self {
            % if engine == "gecko":
            % for prop in SYSTEM_FONT_LONGHANDS:
                PropertyDeclaration::${to_camel_case(prop)}(ref prop) => {
                    prop.get_system()
                }
            % endfor
            % endif
            _ => None,
        }
    }
}

#[cfg(feature = "gecko")]
pub use super::gecko::style_structs;

/// The module where all the style structs are defined.
#[cfg(feature = "servo")]
pub mod style_structs {
    use rustc_hash::FxHasher;
    use super::longhands;
    use std::hash::{Hash, Hasher};
    use crate::logical_geometry::PhysicalSide;
    use crate::values::specified::color::ColorSchemeFlags;
    use crate::derives::*;

    <%def name="impl_physical_sides(ident, props)">
        /// Gets the value of the longhand of `${ident}` on the `s` side`.
        pub fn get_${ident}(&self, s: PhysicalSide) -> &longhands::${data.longhands_by_name[props[0]].ident}::computed_value::T {
            match s {
                PhysicalSide::Top => &self.${data.longhands_by_name[props[0]].ident},
                PhysicalSide::Right => &self.${data.longhands_by_name[props[1]].ident},
                PhysicalSide::Bottom => &self.${data.longhands_by_name[props[2]].ident},
                PhysicalSide::Left => &self.${data.longhands_by_name[props[3]].ident},
            }
        }
        /// Sets the value of the longhand of `${ident}` on the `s` side to `v`.
        pub fn set_${ident}(&mut self, s: PhysicalSide, v: longhands::${data.longhands_by_name[props[0]].ident}::computed_value::T) {
            match s {
                PhysicalSide::Top => self.set_${data.longhands_by_name[props[0]].ident}(v),
                PhysicalSide::Right => self.set_${data.longhands_by_name[props[1]].ident}(v),
                PhysicalSide::Bottom => self.set_${data.longhands_by_name[props[2]].ident}(v),
                PhysicalSide::Left => self.set_${data.longhands_by_name[props[3]].ident}(v),
            }
        }
    </%def>

    % for style_struct in data.active_style_structs():
        % if style_struct.name == "Font":
        #[derive(Clone, Debug, MallocSizeOf)]
        #[cfg_attr(feature = "servo", derive(Serialize, Deserialize))]
        % else:
        #[derive(Clone, Debug, MallocSizeOf, PartialEq)]
        % endif
        /// The ${style_struct.name} style struct.
        pub struct ${style_struct.name} {
            % for longhand in style_struct.longhands:
                % if not longhand.logical:
                    /// The ${longhand.name} computed value.
                    pub ${longhand.ident}: longhands::${longhand.ident}::computed_value::T,
                % endif
            % endfor
            % if style_struct.name == "Font":
                /// The font hash, used for font caching.
                pub hash: u64,
            % endif
            % if style_struct.name == "Box":
                /// The display value specified by the CSS stylesheets (without any style adjustments),
                /// which is needed for hypothetical layout boxes.
                pub original_display: longhands::display::computed_value::T,
            % endif
        }
        % if style_struct.name == "Font":
        impl PartialEq for Font {
            fn eq(&self, other: &Font) -> bool {
                self.hash == other.hash
                % for longhand in style_struct.longhands:
                    && self.${longhand.ident} == other.${longhand.ident}
                % endfor
            }
        }
        % endif

        impl ${style_struct.name} {
            % for longhand in style_struct.longhands:
                % if not longhand.logical:
                    % if longhand.ident == "display":
                        /// Set `display`.
                        ///
                        /// We need to keep track of the original display for hypothetical boxes,
                        /// so we need to special-case this.
                        #[allow(non_snake_case)]
                        #[inline]
                        pub fn set_display(&mut self, v: longhands::display::computed_value::T) {
                            self.display = v;
                            self.original_display = v;
                        }
                    % else:
                        /// Set ${longhand.name}.
                        #[allow(non_snake_case)]
                        #[inline]
                        pub fn set_${longhand.ident}(&mut self, v: longhands::${longhand.ident}::computed_value::T) {
                            self.${longhand.ident} = v;
                        }
                    % endif
                    % if longhand.ident == "display":
                        /// Set `display` from other struct.
                        ///
                        /// Same as `set_display` above.
                        /// Thus, we need to special-case this.
                        #[allow(non_snake_case)]
                        #[inline]
                        pub fn copy_display_from(&mut self, other: &Self) {
                            self.display = other.display.clone();
                            self.original_display = other.display.clone();
                        }
                    % else:
                        /// Set ${longhand.name} from other struct.
                        #[allow(non_snake_case)]
                        #[inline]
                        pub fn copy_${longhand.ident}_from(&mut self, other: &Self) {
                            self.${longhand.ident} = other.${longhand.ident}.clone();
                        }
                    % endif
                    /// Reset ${longhand.name} from the initial struct.
                    #[allow(non_snake_case)]
                    #[inline]
                    pub fn reset_${longhand.ident}(&mut self, other: &Self) {
                        self.copy_${longhand.ident}_from(other)
                    }

                    /// Get the computed value for ${longhand.name}.
                    #[allow(non_snake_case)]
                    #[inline]
                    pub fn clone_${longhand.ident}(&self) -> longhands::${longhand.ident}::computed_value::T {
                        self.${longhand.ident}.clone()
                    }

                    /// Whether `self` and `other` have the same computed value for ${longhand.name}.
                    #[allow(non_snake_case)]
                    #[inline]
                    pub fn ${longhand.ident}_equals(&self, other: &Self) -> bool {
                        self.${longhand.ident} == other.${longhand.ident}
                    }
                % endif
                % if longhand.vector and longhand.vector.need_index:
                    /// If this longhand is indexed, get the number of elements.
                    #[allow(non_snake_case)]
                    pub fn ${longhand.ident}_count(&self) -> usize {
                        self.${longhand.ident}.0.len()
                    }

                    /// If this longhand is indexed, get the element at given
                    /// index.
                    #[allow(non_snake_case)]
                    pub fn ${longhand.ident}_at(&self, index: usize)
                        -> longhands::${longhand.ident}::computed_value::SingleComputedValue {
                        self.${longhand.ident}.0[index].clone()
                    }
                % endif
            % endfor
            % if style_struct.name == "Font":
                /// Computes a font hash in order to be able to cache fonts
                /// effectively in GFX and layout.
                pub fn compute_font_hash(&mut self) {
                    let mut hasher: FxHasher = Default::default();
                    self.font_weight.hash(&mut hasher);
                    self.font_stretch.hash(&mut hasher);
                    self.font_style.hash(&mut hasher);
                    self.font_family.hash(&mut hasher);
                    self.hash = hasher.finish()
                }
                /// Create a new Font with the initial values of all members.
                pub fn initial_values() -> Self {
                    Self {
                        % for longhand in style_struct.longhands:
                            % if not longhand.logical:
                                ${longhand.ident}: longhands::${longhand.ident}::get_initial_value(),
                            % endif
                        % endfor
                        hash: 0,
                    }
                 }
            % elif style_struct.name == "InheritedUI":
                /// Returns the ColorSchemeFlags corresponding to the value of `color-scheme`.
                #[inline]
                pub fn color_scheme_bits(&self) -> ColorSchemeFlags {
                    self.color_scheme.bits
                }
            % elif style_struct.name == "Box":
                /// Sets the display property, but without touching original_display,
                /// except when the adjustment comes from root or item display fixups.
                pub fn set_adjusted_display(
                    &mut self,
                    dpy: longhands::display::computed_value::T,
                    is_item_or_root: bool
                ) {
                    self.display = dpy;
                    if is_item_or_root {
                        self.original_display = dpy;
                    }
                }
            % elif style_struct.name == "Margin":
                ${impl_physical_sides("margin", ["margin-top", "margin-right", "margin-bottom", "margin-left"])}
            % elif style_struct.name == "Position":
                ${impl_physical_sides("inset", ["top", "right", "bottom", "left"])}
            % endif
        }

    % endfor
}

% for style_struct in data.active_style_structs():
    impl style_structs::${style_struct.name} {
        % for longhand in style_struct.longhands:
            % if longhand.vector and longhand.vector.need_index:
                /// Iterate over the values of ${longhand.name}.
                #[allow(non_snake_case)]
                #[inline]
                pub fn ${longhand.ident}_iter(&self) -> ${longhand.camel_case}Iter<'_> {
                    ${longhand.camel_case}Iter {
                        style_struct: self,
                        current: 0,
                        max: self.${longhand.ident}_count(),
                    }
                }

                /// Get a value mod `index` for the property ${longhand.name}.
                #[allow(non_snake_case)]
                #[inline]
                pub fn ${longhand.ident}_mod(&self, index: usize)
                    -> longhands::${longhand.ident}::computed_value::SingleComputedValue {
                    self.${longhand.ident}_at(index % self.${longhand.ident}_count())
                }

                /// Clone the computed value for the property.
                #[allow(non_snake_case)]
                #[inline]
                #[cfg(feature = "gecko")]
                pub fn clone_${longhand.ident}(
                    &self,
                ) -> longhands::${longhand.ident}::computed_value::T {
                    longhands::${longhand.ident}::computed_value::List(
                        self.${longhand.ident}_iter().collect()
                    )
                }
            % endif
        % endfor

        % if style_struct.name == "UI":
            /// Returns whether there is any animation specified with
            /// animation-name other than `none`.
            pub fn specifies_animations(&self) -> bool {
                self.animation_name_iter().any(|name| !name.is_none())
            }

            /// Returns whether there are any transitions specified.
            #[cfg(feature = "servo")]
            pub fn specifies_transitions(&self) -> bool {
                (0..self.transition_property_count()).any(|index| {
                    let combined_duration =
                        self.transition_duration_mod(index).seconds().max(0.) +
                        self.transition_delay_mod(index).seconds();
                    combined_duration > 0.
                })
            }

            /// Returns whether animation-timeline is initial value. We need this information to
            /// resolve animation-duration.
            #[cfg(feature = "servo")]
            pub fn has_initial_animation_timeline(&self) -> bool {
                self.animation_timeline_count() == 1 && self.animation_timeline_at(0).is_auto()
            }

            /// Returns whether there is any named progress timeline specified with
            /// scroll-timeline-name other than `none`.
            #[cfg(feature = "gecko")]
            pub fn specifies_scroll_timelines(&self) -> bool {
                self.scroll_timeline_name_iter().any(|name| !name.value.is_none())
            }

            /// Returns whether there is any timeline scope specified.
            #[cfg(feature = "gecko")]
            pub fn specifies_timeline_scope(&self) -> bool {
                !self.mTimelineScope.is_none()
            }

            /// Returns whether there is any named progress timeline specified with
            /// view-timeline-name other than `none`.
            #[cfg(feature = "gecko")]
            pub fn specifies_view_timelines(&self) -> bool {
                self.view_timeline_name_iter().any(|name| !name.value.is_none())
            }

            /// Returns true if animation properties are equal between styles, but without
            /// considering keyframe data and animation-timeline.
            #[cfg(feature = "servo")]
            pub fn animations_equals(&self, other: &Self) -> bool {
                self.animation_name_iter().eq(other.animation_name_iter()) &&
                self.animation_delay_iter().eq(other.animation_delay_iter()) &&
                self.animation_direction_iter().eq(other.animation_direction_iter()) &&
                self.animation_duration_iter().eq(other.animation_duration_iter()) &&
                self.animation_fill_mode_iter().eq(other.animation_fill_mode_iter()) &&
                self.animation_iteration_count_iter().eq(other.animation_iteration_count_iter()) &&
                self.animation_play_state_iter().eq(other.animation_play_state_iter()) &&
                self.animation_timing_function_iter().eq(other.animation_timing_function_iter())
            }

        % elif style_struct.name == "Column":
            /// Whether this is a multicol style.
            #[cfg(feature = "servo")]
            pub fn is_multicol(&self) -> bool {
                !self.column_width.is_auto() || !self.column_count.is_auto()
            }
        % endif
    }

    % for longhand in style_struct.longhands:
        % if longhand.vector and longhand.vector.need_index:
            /// An iterator over the values of the ${longhand.name} properties.
            pub struct ${longhand.camel_case}Iter<'a> {
                style_struct: &'a style_structs::${style_struct.name},
                current: usize,
                max: usize,
            }

            impl<'a> Iterator for ${longhand.camel_case}Iter<'a> {
                type Item = longhands::${longhand.ident}::computed_value::SingleComputedValue;

                fn next(&mut self) -> Option<Self::Item> {
                    self.current += 1;
                    if self.current <= self.max {
                        Some(self.style_struct.${longhand.ident}_at(self.current - 1))
                    } else {
                        None
                    }
                }
            }
        % endif
    % endfor
% endfor


#[cfg(feature = "gecko")]
pub use super::gecko::{ComputedValues, ComputedValuesInner};

#[cfg(feature = "servo")]
#[cfg_attr(feature = "servo", derive(Clone, Debug))]
/// Actual data of ComputedValues, to match up with Gecko
pub struct ComputedValuesInner {
    % for style_struct in data.active_style_structs():
        ${style_struct.ident}: Arc<style_structs::${style_struct.name}>,
    % endfor
    custom_properties: crate::custom_properties::ComputedCustomProperties,

    /// The set of attributes used as values in `attr()`
    pub attribute_references: AttributeReferences,

    /// The effective zoom value.
    pub effective_zoom: computed::Zoom,

    /// A set of flags we use to store misc information regarding this style.
    pub flags: ComputedValueFlags,

    /// The writing mode of this computed values struct.
    pub writing_mode: WritingMode,

    /// The rule node representing the ordered list of rules matched for this
    /// node.  Can be None for default values and text nodes.  This is
    /// essentially an optimization to avoid referencing the root rule node.
    pub rules: Option<StrongRuleNode>,

    /// The element's computed values if visited, only computed if there's a
    /// relevant link for this element. A element's "relevant link" is the
    /// element being matched if it is a link or the nearest ancestor link.
    visited_style: Option<Arc<ComputedValues>>,
}

/// The struct that Servo uses to represent computed values.
///
/// This struct contains an immutable atomically-reference-counted pointer to
/// every kind of style struct.
///
/// When needed, the structs may be copied in order to get mutated.
#[cfg(feature = "servo")]
#[cfg_attr(feature = "servo", derive(Clone, Debug))]
pub struct ComputedValues {
    /// The actual computed values
    ///
    /// In Gecko the outer ComputedValues is actually a ComputedStyle, whereas
    /// ComputedValuesInner is the core set of computed values.
    ///
    /// We maintain this distinction in servo to reduce the amount of special
    /// casing.
    inner: ComputedValuesInner,

    /// The pseudo-element that we're using.
    pseudo: Option<PseudoElement>,
}

impl ComputedValues {
    /// Returns the pseudo-element that this style represents.
    #[cfg(feature = "servo")]
    pub fn pseudo(&self) -> Option<PseudoElement> {
        self.pseudo
    }

    /// Returns true if this is the style for a pseudo-element.
    #[cfg(feature = "servo")]
    pub fn is_pseudo_style(&self) -> bool {
        self.pseudo().is_some()
    }

    /// Returns whether this style's display value is equal to contents.
    pub fn is_display_contents(&self) -> bool {
        self.clone_display().is_contents()
    }

    /// Gets a reference to the rule node. Panic if no rule node exists.
    pub fn rules(&self) -> &StrongRuleNode {
        self.rules.as_ref().unwrap()
    }

    /// Returns the visited rules, if applicable.
    pub fn visited_rules(&self) -> Option<&StrongRuleNode> {
        self.visited_style().and_then(|s| s.rules.as_ref())
    }

    /// Gets a reference to the custom properties map (if one exists).
    pub fn custom_properties(&self) -> &crate::custom_properties::ComputedCustomProperties {
        &self.custom_properties
    }

% for prop in data.longhands:
% if not prop.logical:
    /// Gets the computed value of a given property.
    #[inline(always)]
    #[allow(non_snake_case)]
    pub fn clone_${prop.ident}(
        &self,
    ) -> longhands::${prop.ident}::computed_value::T {
        self.get_${prop.style_struct.name_lower}().clone_${prop.ident}()
    }

    /// Gets the computed value of a given property.
    #[inline(always)]
    #[allow(non_snake_case)]
    pub fn ${prop.ident}_equals(&self, other: &Self) -> bool {
        self.get_${prop.style_struct.name_lower}().${prop.ident}_equals(other.get_${prop.style_struct.name_lower}())
    }
% endif
% endfor

    /// Writes the (resolved or computed) value of the given longhand as a string in `dest`.
    ///
    /// TODO(emilio): We should move all the special resolution from
    /// nsComputedDOMStyle to ToResolvedValue instead.
    pub fn computed_or_resolved_value(
        &self,
        property_id: LonghandId,
        context: Option<&mut resolved::Context>,
        dest: &mut CssStringWriter,
    ) -> fmt::Result {
        use crate::values::resolved::ToResolvedValue;
        let mut dest = CssWriter::new(dest);
        let property_id = property_id.to_physical(self.writing_mode);
        match property_id {
            % for specified_type, props in groupby(data.longhands, key=lambda x: x.specified_type()):
            <% props = list(props) %>
            ${" |\n".join("LonghandId::{}".format(p.camel_case) for p in props)} => {
                let value = match property_id {
                    % for prop in props:
                    % if not prop.logical:
                    LonghandId::${prop.camel_case} => self.clone_${prop.ident}(),
                    % endif
                    % endfor
                    _ => unsafe { debug_unreachable!() },
                };
                if let Some(c) = context {
                    c.current_longhand = Some(property_id);
                    value.to_resolved_value(c).to_css(&mut dest)
                } else {
                    value.to_css(&mut dest)
                }
            }
            % endfor
        }
    }

    /// Returns the computed value of the given longhand as a
    /// [`TypedValueList`], if supported.
    pub fn property_value_to_typed_value_list(
        &self,
        property_id: LonghandId,
    ) -> Option<TypedValueList> {
        let property_id = property_id.to_physical(self.writing_mode);
        match property_id {
            % for specified_type, props in groupby(data.longhands, key=lambda x: x.specified_type()):
            <% props = list(props) %>
            ${" |\n".join("LonghandId::{}".format(p.camel_case) for p in props)} => {
                let value = match property_id {
                    % for prop in props:
                    % if not prop.logical:
                    LonghandId::${prop.camel_case} => self.clone_${prop.ident}(),
                    % endif
                    % endfor
                    _ => unsafe { debug_unreachable!() },
                };
                value.to_typed_value_list()
            }
            % endfor
        }
    }

    /// Returns the given longhand's resolved value as a property declaration.
    pub fn computed_or_resolved_declaration(
        &self,
        property_id: LonghandId,
        context: Option<&mut resolved::Context>,
    ) -> PropertyDeclaration {
        use crate::values::resolved::ToResolvedValue;
        use crate::values::computed::ToComputedValue;
        let physical_property_id = property_id.to_physical(self.writing_mode);
        match physical_property_id {
            % for specified_type, props in groupby(data.longhands, key=lambda x: x.specified_type()):
            <% props = list(props) %>
            ${" |\n".join("LonghandId::{}".format(p.camel_case) for p in props)} => {
                let mut computed_value = match physical_property_id {
                    % for prop in props:
                    % if not prop.logical:
                    LonghandId::${prop.camel_case} => self.clone_${prop.ident}(),
                    % endif
                    % endfor
                    _ => unsafe { debug_unreachable!() },
                };
                if let Some(c) = context {
                    c.current_longhand = Some(physical_property_id);
                    let resolved = computed_value.to_resolved_value(c);
                    computed_value = ToResolvedValue::from_resolved_value(resolved);
                }
                let specified = ToComputedValue::from_computed_value(&computed_value);
                % if props[0].boxed:
                let specified = Box::new(specified);
                % endif
                % if len(props) == 1:
                PropertyDeclaration::${props[0].camel_case}(specified)
                % else:
                unsafe {
                    let mut out = mem::MaybeUninit::uninit();
                    ptr::write(
                        out.as_mut_ptr() as *mut PropertyDeclarationVariantRepr<${specified_type}>,
                        PropertyDeclarationVariantRepr {
                            tag: property_id as u16,
                            value: specified,
                        },
                    );
                    out.assume_init()
                }
                % endif
            }
            % endfor
        }
    }

    /// Resolves the currentColor keyword.
    ///
    /// Any color value from computed values (except for the 'color' property
    /// itself) should go through this method.
    ///
    /// Usage example:
    /// let top_color =
    ///   style.resolve_color(&style.get_border().clone_border_top_color());
    #[inline]
    pub fn resolve_color(&self, color: &computed::Color) -> crate::color::AbsoluteColor {
        let current_color = self.get_inherited_text().clone_color();
        color.resolve_to_absolute(&current_color)
    }

    /// Returns which longhand properties have different values in the two
    /// ComputedValues.
    #[cfg(feature = "gecko_debug")]
    pub fn differing_properties(&self, other: &ComputedValues) -> LonghandIdSet {
        let mut set = LonghandIdSet::new();
        % for prop in data.longhands:
        % if not prop.logical:
        if self.clone_${prop.ident}() != other.clone_${prop.ident}() {
            set.insert(LonghandId::${prop.camel_case});
        }
        % endif
        % endfor
        set
    }

    /// Create a `TransitionPropertyIterator` for this styles transition properties.
    pub fn transition_properties<'a>(&'a self) -> TransitionPropertyIterator<'a> {
        TransitionPropertyIterator::from_style(self)
    }
}

#[cfg(feature = "servo")]
impl ComputedValues {
    /// Create a new refcounted `ComputedValues`
    pub fn new(
        pseudo: Option<&PseudoElement>,
        custom_properties: crate::custom_properties::ComputedCustomProperties,
        attribute_references: crate::dom::AttributeReferences,
        writing_mode: WritingMode,
        effective_zoom: computed::Zoom,
        flags: ComputedValueFlags,
        rules: Option<StrongRuleNode>,
        visited_style: Option<Arc<ComputedValues>>,
        % for style_struct in data.active_style_structs():
        ${style_struct.ident}: Arc<style_structs::${style_struct.name}>,
        % endfor
    ) -> Arc<Self> {
        Arc::new(Self {
            inner: ComputedValuesInner {
                custom_properties,
                attribute_references,
                writing_mode,
                rules,
                visited_style,
                effective_zoom,
                flags,
            % for style_struct in data.active_style_structs():
                ${style_struct.ident},
            % endfor
            },
            pseudo: pseudo.cloned(),
        })
    }

    /// Get the initial computed values.
    pub fn initial_values_with_font_override(default_font: super::style_structs::Font) -> Arc<Self> {
        use crate::computed_value_flags::ComputedValueFlags;
        use servo_arc::Arc;
        use super::{ComputedValues, ComputedValuesInner, longhands, style_structs};

        Arc::new(ComputedValues {
            inner: ComputedValuesInner {
                % for style_struct in data.active_style_structs():
                    % if style_struct.name == "Font":
                        font: Arc::new(default_font),
                    <% continue %>
                    % endif %

                    ${style_struct.ident}: Arc::new(style_structs::${style_struct.name} {
                        % for longhand in style_struct.longhands:
                            % if not longhand.logical:
                                ${longhand.ident}: longhands::${longhand.ident}::get_initial_value(),
                            % endif
                        % endfor
                        % if style_struct.name == "Box":
                            original_display: longhands::display::get_initial_value(),
                        % endif
                    }),
                % endfor
                custom_properties: crate::custom_properties::ComputedCustomProperties::default(),
                attribute_references: AttributeReferences::default(),
                writing_mode: WritingMode::empty(),
                rules: None,
                visited_style: None,
                effective_zoom: crate::values::computed::Zoom::ONE,
                flags: ComputedValueFlags::empty(),
            },
            pseudo: None,
        })
    }

    /// Converts the computed values to an Arc<> from a reference.
    pub fn to_arc(&self) -> Arc<Self> {
        // SAFETY: We're guaranteed to be allocated as an Arc<> since the
        // functions above are the only ones that create ComputedValues
        // instances in Servo (and that must be the case since ComputedValues'
        // member is private).
        unsafe { Arc::from_raw_addrefed(self) }
    }

    /// Serializes the computed value of this property as a string.
    pub fn computed_value_to_string(&self, property: PropertyDeclarationId) -> String {
        match property {
            PropertyDeclarationId::Longhand(id) => {
                let mut context = resolved::Context {
                    style: self,
                    for_property: PropertyId::NonCustom(id.into()),
                    current_longhand: Some(id),
                };
                let mut s = String::new();
                self.computed_or_resolved_value(
                    id,
                    Some(&mut context),
                    &mut s
                ).unwrap();
                s
            }
            PropertyDeclarationId::Custom(name) => {
                // FIXME(bug 1869476): This should use a stylist to determine
                // whether the name corresponds to an inherited custom property
                // and then choose the inherited/non_inherited map accordingly.
                let p = &self.custom_properties;
                let value = p
                    .inherited
                    .get(name)
                    .or_else(|| p.non_inherited.get(name));
                value.map_or(String::new(), |value| value.to_css_string())
            }
        }
    }

    /// Calls the given function for each cached lazy pseudo-element style.
    pub fn each_cached_lazy_pseudo<F>(&self, mut _f: F)
    where
        F: FnMut(&Self),
    {
        // Servo doesn't currently cache lazy pseudo-element styles.
    }
}

#[cfg(feature = "servo")]
impl ops::Deref for ComputedValues {
    type Target = ComputedValuesInner;
    fn deref(&self) -> &ComputedValuesInner {
        &self.inner
    }
}

#[cfg(feature = "servo")]
impl ops::DerefMut for ComputedValues {
    fn deref_mut(&mut self) -> &mut ComputedValuesInner {
        &mut self.inner
    }
}

#[cfg(feature = "servo")]
impl ComputedValuesInner {
    /// Share ComputedValues but with different flags.
    pub fn clone_with_flags(&self, flags: ComputedValueFlags, pseudo: Option<&PseudoElement>) -> Arc<ComputedValues> {
        Arc::new(ComputedValues {
            inner: Self {
                custom_properties: self.custom_properties.clone(),
                attribute_references: self.attribute_references.clone(),
                writing_mode: self.writing_mode.clone(),
                rules: self.rules.clone(),
                visited_style: self.visited_style.clone(),
                flags,
                effective_zoom: self.effective_zoom.clone(),
                % for style_struct in data.active_style_structs():
                ${style_struct.ident}: self.${style_struct.ident}.clone(),
                % endfor
            },
            pseudo: pseudo.cloned(),
        })
    }

    /// Returns the visited style, if any.
    pub fn visited_style(&self) -> Option<&ComputedValues> {
        self.visited_style.as_deref()
    }

    % for style_struct in data.active_style_structs():
        /// Clone the ${style_struct.name} struct.
        #[inline]
        pub fn clone_${style_struct.name_lower}(&self) -> Arc<style_structs::${style_struct.name}> {
            self.${style_struct.ident}.clone()
        }

        /// Get a immutable reference to the ${style_struct.name} struct.
        #[inline]
        pub fn get_${style_struct.name_lower}(&self) -> &style_structs::${style_struct.name} {
            &self.${style_struct.ident}
        }

        /// Get a mutable reference to the ${style_struct.name} struct.
        #[inline]
        pub fn mutate_${style_struct.name_lower}(&mut self) -> &mut style_structs::${style_struct.name} {
            Arc::make_mut(&mut self.${style_struct.ident})
        }
    % endfor

    /// Gets a reference to the rule node. Panic if no rule node exists.
    pub fn rules(&self) -> &StrongRuleNode {
        self.rules.as_ref().unwrap()
    }

    #[inline]
    /// Returns whether the "content" property for the given style is completely
    /// ineffective, and would yield an empty `::before` or `::after`
    /// pseudo-element.
    pub fn ineffective_content_property(&self) -> bool {
        use crate::values::generics::counters::Content;
        match self.get_counters().content {
            Content::Normal | Content::None => true,
            Content::Items(ref items) => items.items.is_empty()
        }
    }

    /// Whether the current style is multicolumn.
    #[inline]
    pub fn is_multicol(&self) -> bool {
        self.get_column().is_multicol()
    }

    /// Get the logical computed inline size.
    #[inline]
    pub fn content_inline_size(&self) -> &computed::Size {
        let position_style = self.get_position();
        if self.writing_mode.is_vertical() {
            &position_style.height
        } else {
            &position_style.width
        }
    }

    /// Get the logical computed block size.
    #[inline]
    pub fn content_block_size(&self) -> &computed::Size {
        let position_style = self.get_position();
        if self.writing_mode.is_vertical() { &position_style.width } else { &position_style.height }
    }

    /// Get the logical computed min inline size.
    #[inline]
    pub fn min_inline_size(&self) -> &computed::Size {
        let position_style = self.get_position();
        if self.writing_mode.is_vertical() { &position_style.min_height } else { &position_style.min_width }
    }

    /// Get the logical computed min block size.
    #[inline]
    pub fn min_block_size(&self) -> &computed::Size {
        let position_style = self.get_position();
        if self.writing_mode.is_vertical() { &position_style.min_width } else { &position_style.min_height }
    }

    /// Get the logical computed max inline size.
    #[inline]
    pub fn max_inline_size(&self) -> &computed::MaxSize {
        let position_style = self.get_position();
        if self.writing_mode.is_vertical() { &position_style.max_height } else { &position_style.max_width }
    }

    /// Get the logical computed max block size.
    #[inline]
    pub fn max_block_size(&self) -> &computed::MaxSize {
        let position_style = self.get_position();
        if self.writing_mode.is_vertical() { &position_style.max_width } else { &position_style.max_height }
    }

    /// Get the logical computed padding for this writing mode.
    #[inline]
    pub fn logical_padding(&self) -> LogicalMargin<&computed::LengthPercentage> {
        let padding_style = self.get_padding();
        LogicalMargin::from_physical(self.writing_mode, SideOffsets2D::new(
            &padding_style.padding_top.0,
            &padding_style.padding_right.0,
            &padding_style.padding_bottom.0,
            &padding_style.padding_left.0,
        ))
    }

    /// Gets the logical computed margin from this style.
    #[inline]
    pub fn logical_margin(&self) -> LogicalMargin<&computed::Margin> {
        let margin_style = self.get_margin();
        LogicalMargin::from_physical(self.writing_mode, SideOffsets2D::new(
            &margin_style.margin_top,
            &margin_style.margin_right,
            &margin_style.margin_bottom,
            &margin_style.margin_left,
        ))
    }

    /// Gets the logical position from this style.
    #[inline]
    pub fn logical_position(&self) -> LogicalMargin<&computed::Inset> {
        // FIXME(SimonSapin): should be the writing mode of the containing block, maybe?
        let position_style = self.get_position();
        LogicalMargin::from_physical(self.writing_mode, SideOffsets2D::new(
            &position_style.top,
            &position_style.right,
            &position_style.bottom,
            &position_style.left,
        ))
    }

    /// Return true if the effects force the transform style to be Flat
    pub fn overrides_transform_style(&self) -> bool {
        use crate::computed_values::mix_blend_mode::T as MixBlendMode;

        let effects = self.get_effects();
        // TODO(gw): Add clip-path, isolation, mask-image, mask-border-source when supported.
        effects.opacity < 1.0 ||
           !effects.filter.0.is_empty() ||
           !effects.clip.is_auto() ||
           effects.mix_blend_mode != MixBlendMode::Normal
    }

    /// <https://drafts.csswg.org/css-transforms/#grouping-property-values>
    pub fn get_used_transform_style(&self) -> computed_values::transform_style::T {
        use crate::computed_values::transform_style::T as TransformStyle;

        let box_ = self.get_box();

        if self.overrides_transform_style() {
            TransformStyle::Flat
        } else {
            // Return the computed value if not overridden by the above exceptions
            box_.transform_style
        }
    }
}

/// A reference to a style struct of the parent, or our own style struct.
pub enum StyleStructRef<'a, T: 'static> {
    /// A borrowed struct from the parent, for example, for inheriting style.
    Borrowed(&'a T),
    /// An owned struct, that we've already mutated.
    Owned(UniqueArc<T>),
    /// Temporarily vacated, will panic if accessed
    Vacated,
}

impl<'a, T: 'a> StyleStructRef<'a, T>
where
    T: Clone,
{
    /// Ensure a mutable reference of this value exists, either cloning the
    /// borrowed value, or returning the owned one.
    pub fn mutate(&mut self) -> &mut T {
        if let StyleStructRef::Borrowed(v) = *self {
            *self = StyleStructRef::Owned(UniqueArc::new(v.clone()));
        }

        match *self {
            StyleStructRef::Owned(ref mut v) => v,
            StyleStructRef::Borrowed(..) => unreachable!(),
            StyleStructRef::Vacated => panic!("Accessed vacated style struct")
        }
    }

    /// Whether this is pointer-equal to the struct we're going to copy the
    /// value from.
    ///
    /// This is used to avoid allocations when people write stuff like `font:
    /// inherit` or such `all: initial`.
    #[inline]
    pub fn ptr_eq(&self, struct_to_copy_from: &T) -> bool {
        match *self {
            StyleStructRef::Owned(..) => false,
            StyleStructRef::Borrowed(s) => {
                s as *const T == struct_to_copy_from as *const T
            }
            StyleStructRef::Vacated => panic!("Accessed vacated style struct")
        }
    }

    /// Extract a unique Arc from this struct, vacating it.
    ///
    /// The vacated state is a transient one, please put the Arc back
    /// when done via `put()`. This function is to be used to separate
    /// the struct being mutated from the computed context
    pub fn take(&mut self) -> UniqueArc<T> {
        use std::mem::replace;
        let inner = replace(self, StyleStructRef::Vacated);

        match inner {
            StyleStructRef::Owned(arc) => arc,
            StyleStructRef::Borrowed(s) => UniqueArc::new(s.clone()),
            StyleStructRef::Vacated => panic!("Accessed vacated style struct"),
        }
    }

    /// Replace vacated ref with an arc
    pub fn put(&mut self, arc: UniqueArc<T>) {
        debug_assert!(matches!(*self, StyleStructRef::Vacated));
        *self = StyleStructRef::Owned(arc);
    }

    /// Get a mutable reference to the owned struct, or `None` if the struct
    /// hasn't been mutated.
    pub fn get_if_mutated(&mut self) -> Option<&mut T> {
        match *self {
            StyleStructRef::Owned(ref mut v) => Some(v),
            StyleStructRef::Borrowed(..) => None,
            StyleStructRef::Vacated => panic!("Accessed vacated style struct")
        }
    }

    /// Returns an `Arc` to the internal struct, constructing one if
    /// appropriate.
    pub fn build(self) -> Arc<T> {
        match self {
            StyleStructRef::Owned(v) => v.shareable(),
            // SAFETY: We know all style structs are arc-allocated.
            StyleStructRef::Borrowed(v) => unsafe { Arc::from_raw_addrefed(v) },
            StyleStructRef::Vacated => panic!("Accessed vacated style struct")
        }
    }
}

impl<'a, T: 'a> ops::Deref for StyleStructRef<'a, T> {
    type Target = T;

    fn deref(&self) -> &T {
        match *self {
            StyleStructRef::Owned(ref v) => &**v,
            StyleStructRef::Borrowed(v) => v,
            StyleStructRef::Vacated => panic!("Accessed vacated style struct")
        }
    }
}

/// A type used to compute a struct with minimal overhead.
///
/// This allows holding references to the parent/default computed values without
/// actually cloning them, until we either build the style, or mutate the
/// inherited value.
pub struct StyleBuilder<'a> {
    /// The device we're using to compute style.
    ///
    /// This provides access to viewport unit ratios, etc.
    pub device: &'a Device,

    /// The stylist we're using to compute style except for media queries.
    /// device is used in media queries instead.
    pub stylist: Option<&'a Stylist>,

    /// The style we're inheriting from.
    ///
    /// This is effectively
    /// `parent_style.unwrap_or(device.default_computed_values())`.
    inherited_style: &'a ComputedValues,

    /// The style we're getting reset structs from.
    reset_style: &'a ComputedValues,

    /// The rule node representing the ordered list of rules matched for this
    /// node.
    pub rules: Option<StrongRuleNode>,

    /// The computed custom properties and attributes.
    pub substitution_functions: crate::custom_properties::ComputedSubstitutionFunctions,

    /// The set of attributes used as values in `attr()`
    pub attribute_references: crate::dom::AttributeReferences,

    /// Non-custom properties that are considered invalid at compute time
    /// due to cyclic dependencies with custom properties.
    /// e.g. `--foo: 1em; font-size: var(--foo)` where `--foo` is registered.
    pub invalid_non_custom_properties: LonghandIdSet,

    /// The pseudo-element this style will represent.
    pub pseudo: Option<&'a PseudoElement>,

    /// Whether we have mutated any reset structs since the the last time
    /// `clear_modified_reset` was called.  This is used to tell whether the
    /// `StyleAdjuster` did any work.
    modified_reset: bool,

    /// Whether this is the style for the root element.
    pub is_root_element: bool,

    /// The writing mode flags.
    ///
    /// TODO(emilio): Make private.
    pub writing_mode: WritingMode,

    /// The color-scheme bits. Needed because they may otherwise be different between visited and
    /// unvisited colors.
    pub color_scheme: ColorSchemeFlags,

    /// The effective zoom.
    pub effective_zoom: computed::Zoom,

    /// The effective zoom for inheritance (the "specified" zoom on this element).
    pub effective_zoom_for_inheritance: computed::Zoom,

    /// Flags for the computed value.
    pub flags: Cell<ComputedValueFlags>,

    /// The element's style if visited, only computed if there's a relevant link
    /// for this element.  A element's "relevant link" is the element being
    /// matched if it is a link or the nearest ancestor link.
    pub visited_style: Option<Arc<ComputedValues>>,
    % for style_struct in data.active_style_structs():
        ${style_struct.ident}: StyleStructRef<'a, style_structs::${style_struct.name}>,
    % endfor
}

impl<'a> StyleBuilder<'a> {
    /// Trivially construct a `StyleBuilder`.
    pub fn new(
        device: &'a Device,
        stylist: Option<&'a Stylist>,
        parent_style: Option<&'a ComputedValues>,
        pseudo: Option<&'a PseudoElement>,
        rules: Option<StrongRuleNode>,
        is_root_element: bool,
    ) -> Self {
        let reset_style = device.default_computed_values();
        let inherited_style = parent_style.unwrap_or(reset_style);

        let flags = inherited_style.flags.inherited();
        Self {
            device,
            stylist,
            inherited_style,
            reset_style,
            pseudo,
            rules,
            modified_reset: false,
            is_root_element,
            substitution_functions: crate::custom_properties::ComputedSubstitutionFunctions::default(),
            attribute_references: crate::dom::AttributeReferences::default(),
            invalid_non_custom_properties: LonghandIdSet::default(),
            writing_mode: inherited_style.writing_mode,
            effective_zoom: inherited_style.effective_zoom,
            effective_zoom_for_inheritance: computed::Zoom::ONE,
            color_scheme: inherited_style.get_inherited_ui().color_scheme_bits(),
            flags: Cell::new(flags),
            visited_style: None,
            % for style_struct in data.active_style_structs():
            % if style_struct.inherited:
            ${style_struct.ident}: StyleStructRef::Borrowed(inherited_style.get_${style_struct.name_lower}()),
            % else:
            ${style_struct.ident}: StyleStructRef::Borrowed(reset_style.get_${style_struct.name_lower}()),
            % endif
            % endfor
        }
    }

    /// NOTE(emilio): This is done so we can compute relative units with respect
    /// to the parent style, but all the early properties / writing-mode / etc
    /// are already set to the right ones on the kid.
    ///
    /// Do _not_ actually call this to construct a style, this should mostly be
    /// used for animations.
    pub fn for_derived_style(
        device: &'a Device,
        stylist: Option<&'a Stylist>,
        style_to_derive_from: &'a ComputedValues,
        parent_style: Option<&'a ComputedValues>,
    ) -> Self {
        let reset_style = device.default_computed_values();
        let inherited_style = parent_style.unwrap_or(reset_style);
        let map = crate::custom_properties::ComputedSubstitutionFunctions::new(
            Some(style_to_derive_from.custom_properties().clone()),
            None,
        );
        Self {
            device,
            stylist,
            inherited_style,
            reset_style,
            pseudo: None,
            modified_reset: false,
            is_root_element: false,
            rules: None,
            attribute_references: crate::dom::AttributeReferences::default(),
            substitution_functions: map,
            invalid_non_custom_properties: LonghandIdSet::default(),
            writing_mode: style_to_derive_from.writing_mode,
            effective_zoom: style_to_derive_from.effective_zoom,
            effective_zoom_for_inheritance: Self::zoom_for_inheritance(style_to_derive_from.get_box().clone_zoom(), inherited_style),
            color_scheme: style_to_derive_from.get_inherited_ui().color_scheme_bits(),
            flags: Cell::new(style_to_derive_from.flags),
            visited_style: None,
            % for style_struct in data.active_style_structs():
            ${style_struct.ident}: StyleStructRef::Borrowed(
                style_to_derive_from.get_${style_struct.name_lower}()
            ),
            % endfor
        }
    }

    /// Copy the reset properties from `style`.
    pub fn copy_reset_from(&mut self, style: &'a ComputedValues) {
        % for style_struct in data.active_style_structs():
        % if not style_struct.inherited:
        self.${style_struct.ident} =
            StyleStructRef::Borrowed(style.get_${style_struct.name_lower}());
        % endif
        % endfor
    }

    % for property in data.longhands:
    % if not property.logical:
    % if not property.style_struct.inherited:
    /// Inherit `${property.ident}` from our parent style.
    #[allow(non_snake_case)]
    pub fn inherit_${property.ident}(&mut self) {
        let inherited_struct =
            self.inherited_style.get_${property.style_struct.name_lower}();

        self.modified_reset = true;
        self.add_flags(ComputedValueFlags::INHERITS_RESET_STYLE);

        % if property.ident == "content" or property.ident == "display":
        self.add_flags(ComputedValueFlags::DISPLAY_OR_CONTENT_DEPEND_ON_INHERITED_STYLE);
        % endif

        if self.${property.style_struct.ident}.ptr_eq(inherited_struct) {
            return;
        }

        self.${property.style_struct.ident}.mutate()
            .copy_${property.ident}_from(inherited_struct);
    }
    % else:
    /// Reset `${property.ident}` to the initial value.
    #[allow(non_snake_case)]
    pub fn reset_${property.ident}(&mut self) {
        let reset_struct =
            self.reset_style.get_${property.style_struct.name_lower}();

        if self.${property.style_struct.ident}.ptr_eq(reset_struct) {
            return;
        }

        self.${property.style_struct.ident}.mutate()
            .reset_${property.ident}(reset_struct);
    }
    % endif

    % if not property.vector or property.vector.simple_bindings or engine == "servo":
    /// Set the `${property.ident}` to the computed value `value`.
    #[allow(non_snake_case)]
    pub fn set_${property.ident}(
        &mut self,
        value: longhands::${property.ident}::computed_value::T
    ) {
        % if not property.style_struct.inherited:
        self.modified_reset = true;
        % endif

        self.${property.style_struct.ident}.mutate()
            .set_${property.ident}(
                value,
                % if property.logical:
                self.writing_mode,
                % endif
            );
    }
    % endif
    % endif
    % endfor
    <% del property %>

    /// Inherits style from the parent element, accounting for the default
    /// computed values that need to be provided as well.
    pub fn for_inheritance(
        device: &'a Device,
        stylist: Option<&'a Stylist>,
        parent: Option<&'a ComputedValues>,
        pseudo: Option<&'a PseudoElement>,
    ) -> Self {
        // Rebuild the visited style from the parent, ensuring that it will also
        // not have rules.  This matches the unvisited style that will be
        // produced by this builder.  This assumes that the caller doesn't need
        // to adjust or process visited style, so we can just build visited
        // style here for simplicity.
        let visited_style = parent.and_then(|parent| {
            parent.visited_style().map(|style| {
                Self::for_inheritance(
                    device,
                    stylist,
                    Some(style),
                    pseudo,
                ).build()
            })
        });
        let custom_properties = if let Some(p) = parent { p.custom_properties().clone() } else { crate::custom_properties::ComputedCustomProperties::default() };
        let mut ret = Self::new(
            device,
            stylist,
            parent,
            pseudo,
            /* rules = */ None,
            /* is_root_element = */ false,
        );
        ret.substitution_functions.custom_properties = custom_properties;
        ret.visited_style = visited_style;
        ret
    }

    /// Returns whether we have a visited style.
    pub fn has_visited_style(&self) -> bool {
        self.visited_style.is_some()
    }

    /// Returns whether we're a pseudo-elements style.
    pub fn is_pseudo_element(&self) -> bool {
        self.pseudo.map_or(false, |p| !p.is_anon_box())
    }

    /// Returns the style we're getting reset properties from.
    pub fn default_style(&self) -> &'a ComputedValues {
        self.reset_style
    }

    % for style_struct in data.active_style_structs():
        /// Gets an immutable view of the current `${style_struct.name}` style.
        pub fn get_${style_struct.name_lower}(&self) -> &style_structs::${style_struct.name} {
            &self.${style_struct.ident}
        }

        /// Gets a mutable view of the current `${style_struct.name}` style.
        pub fn mutate_${style_struct.name_lower}(&mut self) -> &mut style_structs::${style_struct.name} {
            % if not style_struct.inherited:
            self.modified_reset = true;
            % endif
            self.${style_struct.ident}.mutate()
        }

        /// Gets a mutable view of the current `${style_struct.name}` style.
        pub fn take_${style_struct.name_lower}(&mut self) -> UniqueArc<style_structs::${style_struct.name}> {
            % if not style_struct.inherited:
            self.modified_reset = true;
            % endif
            self.${style_struct.ident}.take()
        }

        /// Gets a mutable view of the current `${style_struct.name}` style.
        pub fn put_${style_struct.name_lower}(&mut self, s: UniqueArc<style_structs::${style_struct.name}>) {
            self.${style_struct.ident}.put(s)
        }

        /// Gets a mutable view of the current `${style_struct.name}` style,
        /// only if it's been mutated before.
        pub fn get_${style_struct.name_lower}_if_mutated(&mut self)
                                                         -> Option<&mut style_structs::${style_struct.name}> {
            self.${style_struct.ident}.get_if_mutated()
        }

        /// Reset the current `${style_struct.name}` style to its default value.
        pub fn reset_${style_struct.name_lower}_struct(&mut self) {
            self.${style_struct.ident} =
                StyleStructRef::Borrowed(self.reset_style.get_${style_struct.name_lower}());
        }
    % endfor
    <% del style_struct %>

    /// Returns whether this computed style represents a floated object.
    pub fn is_floating(&self) -> bool {
        self.get_box().clone_float().is_floating()
    }

    /// Returns whether this computed style represents an absolutely-positioned
    /// object.
    pub fn is_absolutely_positioned(&self) -> bool {
        self.get_box().clone_position().is_absolutely_positioned()
    }

    /// Whether this style has a top-layer style.
    #[cfg(feature = "servo")]
    pub fn in_top_layer(&self) -> bool {
        matches!(self.get_box().clone__servo_top_layer(),
                 longhands::_servo_top_layer::computed_value::T::Top)
    }

    /// Whether this style has a top-layer style.
    #[cfg(feature = "gecko")]
    pub fn in_top_layer(&self) -> bool {
        matches!(self.get_box().clone__moz_top_layer(),
                 longhands::_moz_top_layer::computed_value::T::Auto)
    }

    /// Clears the "have any reset structs been modified" flag.
    pub fn clear_modified_reset(&mut self) {
        self.modified_reset = false;
    }

    /// Returns whether we have mutated any reset structs since the the last
    /// time `clear_modified_reset` was called.
    pub fn modified_reset(&self) -> bool {
        self.modified_reset
    }

    /// Return the current flags.
    #[inline]
    pub fn flags(&self) -> ComputedValueFlags {
        self.flags.get()
    }

    /// Add a flag to the current builder.
    #[inline]
    pub fn add_flags(&self, flag: ComputedValueFlags) {
        let flags = self.flags() | flag;
        self.flags.set(flags);
    }

    /// Removes a flag to the current builder.
    #[inline]
    pub fn remove_flags(&self, flag: ComputedValueFlags) {
        let flags = self.flags() & !flag;
        self.flags.set(flags);
    }

    /// Turns this `StyleBuilder` into a proper `ComputedValues` instance.
    pub fn build(self) -> Arc<ComputedValues> {
        ComputedValues::new(
            self.pseudo,
            self.substitution_functions.custom_properties,
            self.attribute_references,
            self.writing_mode,
            self.effective_zoom,
            self.flags.get(),
            self.rules,
            self.visited_style,
            % for style_struct in data.active_style_structs():
            self.${style_struct.ident}.build(),
            % endfor
        )
    }

    /// Get the substitution function maps if necessary.
    pub fn substitution_functions(&self) -> &crate::custom_properties::ComputedSubstitutionFunctions {
        &self.substitution_functions
    }

    /// Get the custom properties map if necessary.
    pub fn custom_properties(&self) -> &crate::custom_properties::ComputedCustomProperties {
        &self.substitution_functions.custom_properties
    }

    /// Get the inherited custom properties map.
    pub fn inherited_custom_properties(&self) -> &crate::custom_properties::ComputedCustomProperties {
        &self.inherited_style.custom_properties
    }

    /// Access to various information about our inherited styles.  We don't
    /// expose an inherited ComputedValues directly, because in the
    /// ::first-line case some of the inherited information needs to come from
    /// one ComputedValues instance and some from a different one.

    /// Inherited writing-mode.
    pub fn inherited_writing_mode(&self) -> &WritingMode {
        &self.inherited_style.writing_mode
    }

    /// The effective zoom value that we should multiply absolute lengths by.
    pub fn effective_zoom(&self) -> computed::Zoom {
        self.effective_zoom
    }

    /// The zoom specified on this element.
    pub fn specified_zoom(&self) -> computed::Zoom {
        self.get_box().clone_zoom()
    }

    /// Computes effective_zoom and effective_zoom_for_inheritance based on the current style
    /// information.
    pub fn recompute_effective_zooms(&mut self) {
        let specified = self.specified_zoom();
        self.effective_zoom = self.inherited_style.effective_zoom.compute_effective(specified);
        self.effective_zoom_for_inheritance = Self::zoom_for_inheritance(specified, self.inherited_style);
    }

    fn zoom_for_inheritance(specified: computed::Zoom, inherited_style: &ComputedValues) -> computed::Zoom {
        if specified.is_document() {
            // If our inherited effective zoom has derived to zero, there's not much we can do.
            // This value is not exposed to content anyways (it's used for scrollbars and to avoid
            // zoom affecting canvas).
            inherited_style.effective_zoom.inverted().unwrap_or(computed::Zoom::ONE)
        } else {
            specified
        }
    }

    /// The computed value flags of our parent.
    #[inline]
    pub fn get_parent_flags(&self) -> ComputedValueFlags {
        self.inherited_style.flags
    }

    /// Calculate the line height, given the currently resolved line-height and font.
    pub fn calc_line_height(
        &self,
        device: &Device,
        line_height_base: LineHeightBase,
        writing_mode: WritingMode,
    ) -> computed::NonNegativeLength {
        use crate::computed_value_flags::ComputedValueFlags;
        let (font, flag) = match line_height_base {
            LineHeightBase::CurrentStyle => (
                self.get_font(),
                ComputedValueFlags::DEPENDS_ON_SELF_FONT_METRICS,
            ),
            LineHeightBase::InheritedStyle => (
                self.get_parent_font(),
                ComputedValueFlags::DEPENDS_ON_INHERITED_FONT_METRICS,
            ),
        };
        let line_height = font.clone_line_height();
        if matches!(line_height, computed::LineHeight::Normal) {
            self.add_flags(flag);
        }
        let lh = device.calc_line_height(&font, writing_mode, None);
        if line_height_base == LineHeightBase::InheritedStyle {
            // Apply our own zoom if our style source is the parent style.
            computed::NonNegativeLength::new(self.effective_zoom_for_inheritance.zoom(lh.px()))
        } else {
            lh
        }
    }

    /// And access to inherited style structs.
    % for style_struct in data.active_style_structs():
        /// Gets our inherited `${style_struct.name}`.  We don't name these
        /// accessors `inherited_${style_struct.name_lower}` because we already
        /// have things like "box" vs "inherited_box" as struct names.  Do the
        /// next-best thing and call them `parent_${style_struct.name_lower}`
        /// instead.
        pub fn get_parent_${style_struct.name_lower}(&self) -> &style_structs::${style_struct.name} {
            self.inherited_style.get_${style_struct.name_lower}()
        }
    % endfor
}

/// A per-longhand function that performs the CSS cascade for that longhand.
pub type CascadePropertyFn =
    unsafe extern "Rust" fn(
        declaration: &PropertyDeclaration,
        context: &mut computed::Context,
    );

/// A per-longhand array of functions to perform the CSS cascade on each of
/// them, effectively doing virtual dispatch.
pub static CASCADE_PROPERTY: [CascadePropertyFn; ${len(data.longhands)}] = [
    % for property in data.longhands:
        longhands::${property.ident}::cascade_property,
    % endfor
];

/// An identifier for a given alias property.
#[derive(Clone, Copy, Eq, PartialEq, MallocSizeOf)]
#[repr(u16)]
pub enum AliasId {
    % for i, property in enumerate(data.all_aliases()):
        /// ${property.name}
        ${property.camel_case} = ${i},
    % endfor
}

impl fmt::Debug for AliasId {
    fn fmt(&self, formatter: &mut fmt::Formatter) -> fmt::Result {
        let name = NonCustomPropertyId::from(*self).name();
        formatter.write_str(name)
    }
}

impl AliasId {
    /// Returns the property we're aliasing, as a longhand or a shorthand.
    #[inline]
    pub fn aliased_property(self) -> NonCustomPropertyId {
        static MAP: [NonCustomPropertyId; ${len(data.all_aliases())}] = [
        % for alias in data.all_aliases():
            % if alias.original.type() == "longhand":
            NonCustomPropertyId::from_longhand(LonghandId::${alias.original.camel_case}),
            % else:
            <% assert alias.original.type() == "shorthand" %>
            NonCustomPropertyId::from_shorthand(ShorthandId::${alias.original.camel_case}),
            % endif
        % endfor
        ];
        MAP[self as usize]
    }
}

/// Call the given macro with tokens like this for each longhand and shorthand properties
/// that is enabled in content:
///
/// ```
/// [CamelCaseName, SetCamelCaseName, PropertyId::Longhand(LonghandId::CamelCaseName)],
/// ```
///
/// NOTE(emilio): Callers are responsible to deal with prefs.
#[macro_export]
macro_rules! css_properties_accessors {
    ($macro_name: ident) => {
        $macro_name! {
            % for kind, props in [("Longhand", data.longhands), ("Shorthand", data.shorthands)]:
                % for property in props:
                    % if property.enabled_in_content():
                        % for prop in [property] + property.aliases:
                            % if '-' in prop.name:
                                [${prop.ident.capitalize()}, Set${prop.ident.capitalize()},
                                 PropertyId::NonCustom(${kind}Id::${property.camel_case}.into())],
                            % endif
                            [${prop.camel_case}, Set${prop.camel_case},
                             PropertyId::NonCustom(${kind}Id::${property.camel_case}.into())],
                        % endfor
                    % endif
                % endfor
            % endfor
        }
    }
}

/// Call the given macro with tokens like this for each longhand properties:
///
/// ```
/// { snake_case_ident }
/// ```
#[macro_export]
macro_rules! longhand_properties_idents {
    ($macro_name: ident) => {
        $macro_name! {
            % for property in data.longhands:
                { ${property.ident} }
            % endfor
        }
    }
}

// Large pages generate tens of thousands of ComputedValues.
#[cfg(feature = "gecko")]
size_of_test!(ComputedValues, 248);
#[cfg(feature = "servo")]
size_of_test!(ComputedValues, 224);

// FFI relies on this.
size_of_test!(Option<Arc<ComputedValues>>, 8);

// There are two reasons for this test to fail:
//
//   * Your changes made a specified value type for a given property go
//     over the threshold. In that case, you should try to shrink it again
//     or, if not possible, mark the property as boxed in the property
//     definition.
//
//   * Your changes made a specified value type smaller, so that it no
//     longer needs to be boxed. In this case you just need to remove
//     boxed=True from the property definition. Nice job!
#[cfg(target_pointer_width = "64")]
#[allow(dead_code)] // https://github.com/rust-lang/rust/issues/96952
const BOX_THRESHOLD: usize = 24;
% for longhand in data.longhands:
#[cfg(target_pointer_width = "64")]
% if longhand.boxed:
const_assert!(std::mem::size_of::<longhands::${longhand.ident}::SpecifiedValue>() > BOX_THRESHOLD);
% else:
const_assert!(std::mem::size_of::<longhands::${longhand.ident}::SpecifiedValue>() <= BOX_THRESHOLD);
% endif
% endfor

% if engine == "servo":
% for effect_name in ["repaint", "recalculate_overflow", "rebuild_stacking_context", "rebuild_box"]:
pub(crate) fn restyle_damage_${effect_name} (old: &ComputedValues, new: &ComputedValues) -> bool {
    % for style_struct in data.active_style_structs():
        <% longhands_affected = [effect_name in longhand.servo_restyle_damage.split() for longhand in style_struct.longhands if not longhand.logical] %>
        % if any(longhands_affected):
        let old_${style_struct.name_lower} = old.get_${style_struct.name_lower}();
        let new_${style_struct.name_lower} = new.get_${style_struct.name_lower}();
        if !std::ptr::eq(old_${style_struct.name_lower}, new_${style_struct.name_lower}) {
            if
            % for longhand in style_struct.longhands:
            % if effect_name in longhand.servo_restyle_damage.split() and not longhand.logical:
                old_${style_struct.name_lower}.${longhand.ident} != new_${style_struct.name_lower}.${longhand.ident} ||
            % endif
            % endfor
            false {
                return true;
            }
        }
        % endif
    % endfor
    false
}
% endfor
% endif

/// Descriptor types for @-rules like @font-face and @counter-style.
<%def name="generate_descriptors(descriptors)">
use super::*;
#[allow(unused_imports)]
use crate::values::specified;

/// Descriptor identifier.
#[derive(Clone, Copy, Debug, Eq, Hash, FromPrimitive, Parse, PartialEq)]
#[repr(u8)]
pub enum DescriptorId {
    % for descriptor in descriptors:
    /// The "${descriptor.name}" descriptor.
    ${descriptor.camel_case},
    % endfor
}

impl DescriptorId {
    /// The total number of descriptors.
    pub const COUNT: usize = ${len(descriptors)};

    /// The CSS name of this descriptor.
    pub fn name(&self) -> &'static str {
        const NAMES: [&'static str; DescriptorId::COUNT] = [
        % for descriptor in descriptors:
            "${descriptor.name}",
        % endfor
        ];
        NAMES[*self as usize]
    }
}

/// All descriptor values.
#[derive(Clone, Debug, Default, ToShmem, PartialEq, MallocSizeOf)]
pub struct Descriptors {
    % for descriptor in descriptors:
    /// The "${descriptor.name}" descriptor value.
    % if descriptor.ignore_malloc_size_of:
    #[ignore_malloc_size_of = "${descriptor.ignore_malloc_size_of}"]
    % endif
    pub ${descriptor.ident}: Option<${descriptor.type}>,
    % endfor
}

impl Descriptors {
    /// Gets a descriptor in CSS syntax.
    pub fn get(&self, id: DescriptorId, dest: &mut CssStringWriter) -> fmt::Result {
        let mut dest = CssWriter::new(dest);
        match id {
        % for descriptor in descriptors:
            DescriptorId::${descriptor.camel_case} => self.${descriptor.ident}.to_css(&mut dest),
        % endfor
        }
    }

    /// Parses a given descriptor. Returns whether the descriptor changed.
    pub fn set<'i, 't>(&mut self, id: DescriptorId, context: &ParserContext, input: &mut Parser<'i, 't>) -> Result<bool, ParseError<'i>> {
        use crate::parser::Parse;
        // DeclarationParser also calls parse_entirely so we’d normally not need to, but in this
        // case we do because we set the value as a side effect rather than returning it.
        match id {
        % for descriptor in descriptors:
            DescriptorId::${descriptor.camel_case} => {
                let value = Some(input.parse_entirely(|i|
                    % if descriptor.parser:
                        ${descriptor.type}::${descriptor.parser}(context, i)
                    % else:
                        Parse::parse(context, i)
                    % endif
                )?);
                let change = self.${descriptor.ident} != value;
                self.${descriptor.ident} = value;
                Ok(change)
            },
        % endfor
        }
    }

    /// Removes a descriptor. Returns true if it used to be set.
    pub fn remove(&mut self, id: DescriptorId) -> bool {
        match id {
        % for descriptor in descriptors:
            DescriptorId::${descriptor.camel_case} => self.${descriptor.ident}.take().is_some(),
        % endfor
        }
    }

    /// Returns the count of set descriptors.
    pub fn len(&self) -> usize {
        let mut count = 0;
        % for descriptor in descriptors:
        if self.${descriptor.ident}.is_some() {
            count += 1;
        }
        % endfor
        count
    }

    /// Returns the descriptor at position `i`.
    pub fn at(&self, i: usize) -> Option<DescriptorId> {
        let mut cur = 0;
        % for descriptor in descriptors:
        if self.${descriptor.ident}.is_some() {
            if cur == i {
                return Some(DescriptorId::${descriptor.camel_case});
            }
            cur += 1;
        }
        % endfor
        let _ = cur; // Silences warning on the last descriptor
        None
    }
}

impl ToCss for Descriptors {
    fn to_css<W>(&self, dest: &mut CssWriter<W>) -> fmt::Result
    where
        W: fmt::Write,
    {
        use std::fmt::Write;
        % for descriptor in descriptors:
        if let Some(ref value) = self.${descriptor.ident} {
            dest.write_str("${descriptor.name}: ")?;
            value.to_css(dest)?;
            dest.write_str("; ")?;
        }
        % endfor
        Ok(())
    }
}

/// Parser for descriptor declarations in at-rules.
pub struct DescriptorParser<'a, 'b: 'a> {
    /// The parser context.
    pub context: &'a ParserContext<'b>,
    /// The descriptors to parse into.
    pub descriptors: &'a mut Descriptors,
}

impl<'a, 'b, 'i> cssparser::AtRuleParser<'i> for DescriptorParser<'a, 'b> {
    type Prelude = ();
    type AtRule = ();
    type Error = StyleParseErrorKind<'i>;
}

impl<'a, 'b, 'i> cssparser::QualifiedRuleParser<'i> for DescriptorParser<'a, 'b> {
    type Prelude = ();
    type QualifiedRule = ();
    type Error = StyleParseErrorKind<'i>;
}

impl<'a, 'b, 'i> cssparser::RuleBodyItemParser<'i, (), StyleParseErrorKind<'i>>
    for DescriptorParser<'a, 'b>
{
    fn parse_qualified(&self) -> bool {
        false
    }
    fn parse_declarations(&self) -> bool {
        true
    }
}

impl<'a, 'b, 'i> cssparser::DeclarationParser<'i> for DescriptorParser<'a, 'b> {
    type Declaration = ();
    type Error = StyleParseErrorKind<'i>;

    fn parse_value<'t>(
        &mut self,
        name: cssparser::CowRcStr<'i>,
        input: &mut Parser<'i, 't>,
        _declaration_start: &cssparser::ParserState,
    ) -> Result<(), ParseError<'i>> {
        let Ok(id) = DescriptorId::from_ident(name.as_ref()) else {
            return Err(
                input.new_custom_error(
                    selectors::parser::SelectorParseErrorKind::UnexpectedIdent(name.clone())
                )
            );
        };
        self.descriptors.set(id, self.context, input)?;
        Ok(())
    }
}
</%def>

/// Generated code for @font-face descriptors.
pub mod font_face {
    use crate::font_face::*;
${generate_descriptors(data.font_face_descriptors)}
}

/// Generated code for @counter-style descriptors.
pub mod counter_style {
    use crate::counter_style::*;
${generate_descriptors(data.counter_style_descriptors)}
}

/// Generated code for @property descriptors.
pub mod property {
    use crate::properties_and_values::rule::*;
${generate_descriptors(data.property_descriptors)}
}

/// Generated code for @view-transition descriptors.
pub mod view_transition {
    use crate::stylesheets::view_transition_rule::*;
${generate_descriptors(data.view_transition_descriptors)}
}
